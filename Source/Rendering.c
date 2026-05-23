#include "RenderingInternal.h"

WindowState    g_WindowState;
RenderState    g_RenderState;
SDL_GPUDevice* g_GPUDevice = NULL;

static bool g_EnableOcclusion = true;
static bool g_EnableHBAO = true;

extern void UIRenderCallback(void); // Editor.c

static void InitRenderSetBuffers(RenderSetBuffers* buffers, RenderSet* set)
{
    size_t groupBytes  = set->maxGroups * sizeof(PrimitiveGroup);
    size_t entityBytes = set->maxEntities * sizeof(Entity);

    buffers->primitiveGroup    = CreateBuffer(set->primitiveGroups, groupBytes, BReadCompute, "CPPrimitiveGroups");
    buffers->drawSparseIndices = CreateBuffer(NULL, set->maxEntities * sizeof(u32), BReadRasterBit | BReadCompute | BWriteComputeBit, "CPDrawSparseIndices");
    buffers->sparseToDense     = CreateBuffer(set->sparseID, set->maxEntities * sizeof(u32), BReadCompute, "CPSparseToDense");
    buffers->drawArgs          = CreateBuffer(NULL, set->maxGroups * sizeof(SDL_GPUIndexedIndirectDrawCommand), BIndirectBit | BReadCompute | BWriteComputeBit, "CPDrawArgs");
    buffers->denseToPrimitive  = CreateBuffer(set->denseToPrimitiveIndex, set->maxEntities * sizeof(u32), BReadCompute, "CPDenseToPrimitive");
    buffers->entity            = CreateBuffer(set->entities, entityBytes, BReadRasterBit | BReadCompute, "CPEntities");
    buffers->visibilityMask    = CreateBuffer(NULL, set->maxEntities * sizeof(u32), BReadCompute | BWriteComputeBit, "CPVisibilityMask");
    buffers->visibleCount      = CreateBuffer(NULL, sizeof(u32), BReadCompute | BWriteComputeBit, "CPVisibleCount");
    buffers->dispatchArgs      = CreateBuffer(NULL, sizeof(u32) * 6, BIndirectBit | BWriteComputeBit, "CPDispatchArgs");
    buffers->visibleSparseIndices = CreateBuffer(NULL, set->maxEntities * sizeof(u32), BReadCompute | BWriteComputeBit, "CPVisibleSparseIndices");
}

void InitBuffers(void)
{
    InitRenderSetBuffers(&g_RenderState.skinnedBuffers, &skinnedSet);
    InitRenderSetBuffers(&g_RenderState.surfaceBuffers, &surfaceSet);

    AnimInitBuffers();
    const size_t animatedVertexSize = sizeof(u32) * 2 * MAX_ANIMATED_VERTEX;
    g_RenderState.skinnedVertexBuffer = CreateBuffer(gGFX.SkinnedVertexBuffer, MAX_SKINNED_SOURCE_VERTEX * sizeof(ASkinedVertex), BVertexBit | BReadCompute, "CPSkinnedVertexBuffer");
    g_RenderState.surfaceVertexBuffer = CreateBuffer(gGFX.SurfaceVertexBuffer, MAX_VERTEX * sizeof(AVertex), BVertexBit, "CPSurfaceVertexBuffer");
    g_RenderState.indexBuffer = CreateBuffer(gGFX.IndexBuffer, MAX_INDEX * sizeof(int), SDL_GPU_BUFFERUSAGE_INDEX, "CPIndexBuffer");
    g_RenderState.skinnedAnimatedVertices = CreateBuffer(NULL, animatedVertexSize, BReadRasterBit | BWriteComputeBit, "CPAnimatedVertices");
    g_RenderState.lineBuffer = CreateBuffer(NULL, sizeof(ALineVertex) * MAX_LINE_COUNT, BVertexBit | BWriteComputeBit, "CPLineVertexBuffer");
    g_RenderState.lineDrawArgsBuffer = CreateBuffer(NULL, sizeof(u32) * 8, BIndirectBit | BWriteComputeBit, "CPLinedrawArgsBuffer");
    UIInit();
}

static void ReleaseWindowFrameTextures(WindowState* winstate)
{
    SDL_ReleaseGPUTexture(g_GPUDevice, winstate->tex_depth);
    SDL_ReleaseGPUTexture(g_GPUDevice, winstate->tex_hiz_depth);
    SDL_ReleaseGPUTexture(g_GPUDevice, winstate->tex_color);
    SDL_ReleaseGPUTexture(g_GPUDevice, winstate->tex_gbuffer_tangent);
    SDL_ReleaseGPUTexture(g_GPUDevice, winstate->tex_gbuffer_albedo_metallic);
    SDL_ReleaseGPUTexture(g_GPUDevice, winstate->tex_gbuffer_shadow_roughness);
    SDL_ReleaseGPUTexture(g_GPUDevice, winstate->tex_post);
    SDL_ReleaseGPUTexture(g_GPUDevice, winstate->tex_hiz);
    SDL_ReleaseGPUTexture(g_GPUDevice, winstate->tex_hbao);
    SDL_ReleaseGPUTexture(g_GPUDevice, winstate->tex_hbao_blur);
    SDL_ReleaseGPUTexture(g_GPUDevice, winstate->tex_hbao_normal);
}

static void ResizeWindowFrameTextures(WindowState* winstate, u32 width, u32 height)
{
    ReleaseWindowFrameTextures(winstate);
    winstate->tex_depth     = CreateDepthTexture(width, height);
    winstate->tex_hiz_depth = CreateHiZDepthTexture(width, height);
    winstate->tex_color     = CreateSceneColorTexture(width, height, SDL_GPU_SAMPLECOUNT_1);
    winstate->tex_gbuffer_tangent = CreateGBufferTangentTexture(width, height);
    winstate->tex_gbuffer_albedo_metallic = CreateGBufferAlbedoMetallicTexture(width, height);
    winstate->tex_gbuffer_shadow_roughness = CreateGBufferShadowRoughnessTexture(width, height);
    winstate->tex_post      = CreatePostProcessTexture(width, height);
    winstate->tex_hiz       = CreateHiZTexture(width, height, &winstate->hiz_mip_count);
    u32 hbaoWidth = Maxu32(width / 2u, 1u);
    u32 hbaoHeight = Maxu32(height / 2u, 1u);
    winstate->tex_hbao        = CreateHBAOTexture(hbaoWidth, hbaoHeight);
    winstate->tex_hbao_blur   = CreateHBAOTexture(hbaoWidth, hbaoHeight);
    winstate->tex_hbao_normal = CreateHBAONormalTexture(hbaoWidth, hbaoHeight);
    winstate->hiz_width     = width;
    winstate->hiz_height    = height;
    winstate->hiz_valid     = false;
}

static SDL_GPUColorTargetInfo MakeMainColorTarget(WindowState* winstate)
{
    SDL_GPUColorTargetInfo target;
    SDL_zero(target);
    target.load_op  = SDL_GPU_LOADOP_CLEAR;
    target.store_op = SDL_GPU_STOREOP_STORE;
    target.clear_color.a = 1.0f;
    target.texture  = winstate->tex_color;
    target.cycle    = true;
    return target;
}

static SDL_GPUColorTargetInfo MakeLoadedSceneColorTarget(WindowState* winstate)
{
    SDL_GPUColorTargetInfo target;
    SDL_zero(target);
    target.load_op  = SDL_GPU_LOADOP_LOAD;
    target.store_op = SDL_GPU_STOREOP_STORE;
    target.texture  = winstate->tex_color;
    target.cycle    = false;
    return target;
}

static SDL_GPUColorTargetInfo MakeLoadedPostTarget(WindowState* winstate)
{
    SDL_GPUColorTargetInfo target;
    SDL_zero(target);
    target.load_op  = SDL_GPU_LOADOP_LOAD;
    target.store_op = SDL_GPU_STOREOP_STORE;
    target.texture  = winstate->tex_post;
    target.cycle    = false;
    return target;
}

static void MakeGBufferTargets(WindowState* winstate, SDL_GPUColorTargetInfo targets[3])
{
    for (u32 i = 0; i < 3u; i++)
    {
        SDL_zero(targets[i]);
        targets[i].load_op = SDL_GPU_LOADOP_CLEAR;
        targets[i].store_op = SDL_GPU_STOREOP_STORE;
        targets[i].cycle = true;
    }
    targets[0].texture = winstate->tex_gbuffer_tangent;
    targets[1].texture = winstate->tex_gbuffer_albedo_metallic;
    targets[2].texture = winstate->tex_gbuffer_shadow_roughness;
    targets[1].clear_color.a = 0.0f;
    targets[2].clear_color.r = 1.0f;
    targets[2].clear_color.g = 1.0f;
}

static SDL_GPUDepthStencilTargetInfo MakeDepthTarget(SDL_GPUTexture* texture, SDL_GPULoadOp loadOp, bool cycle)
{
    SDL_GPUDepthStencilTargetInfo target;
    SDL_zero(target);
    target.clear_depth      = 1.0f;
    target.load_op          = loadOp;
    target.store_op         = SDL_GPU_STOREOP_STORE;
    target.stencil_load_op  = SDL_GPU_LOADOP_DONT_CARE;
    target.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    target.texture          = texture;
    target.cycle            = cycle;
    return target;
}

static SDL_GPUDepthStencilTargetInfo MakeShadowDepthTarget(SDL_GPUTexture* texture, u32 layer)
{
    (void)layer;
    return MakeDepthTarget(texture, SDL_GPU_LOADOP_CLEAR, false);
}

static SDL_GPUColorTargetInfo MakeHiZDepthTarget(WindowState* winstate)
{
    SDL_GPUColorTargetInfo target;
    SDL_zero(target);
    target.load_op  = SDL_GPU_LOADOP_CLEAR;
    target.store_op = SDL_GPU_STOREOP_STORE;
    target.clear_color.r = 1.0f;
    target.texture = winstate->tex_hiz_depth;
    target.cycle = true;
    return target;
}

static SDL_GPUColorTargetInfo MakeShadowColorTarget(WindowState* winstate, u32 layer)
{
    SDL_GPUColorTargetInfo target;
    SDL_zero(target);
    target.load_op  = SDL_GPU_LOADOP_CLEAR;
    target.store_op = SDL_GPU_STOREOP_STORE;
    target.clear_color.r = 1.0f;
    target.texture = winstate->tex_shadow_color;
    target.layer_or_depth_plane = layer;
    target.cycle = false;
    return target;
}

static void UploadRenderSetEntities(RenderSet* set, RenderSetBuffers* buffers)
{
    if (set->numEntities == 0) return;
    UpdateGPUBuffer(buffers->entity, set->entities, set->numEntities * sizeof(Entity), 0ull);
    UpdateGPUBuffer(buffers->sparseToDense, set->sparseID, set->numEntities * sizeof(u32), 0ull);
}

static void CullScene(SDL_GPUCommandBuffer* cmd, FrustumPlanes planes, mat4x4 viewProj, bool enableHiZ, bool outputSkinnedVisibility)
{
    DispatchCullDrawArgsCompute(cmd, &skinnedSet, &g_RenderState.skinnedBuffers, planes, viewProj, enableHiZ, outputSkinnedVisibility);
    DispatchCullDrawArgsCompute(cmd, &surfaceSet, &g_RenderState.surfaceBuffers, planes, viewProj, enableHiZ, false);
}

static void AnimateSkinned(SDL_GPUCommandBuffer* cmd)
{
    DispatchAnimationCompute(cmd, &skinnedSet);
    DispatchAnimateVerticesCompute(cmd, &skinnedSet);
}

void Render(void)
{
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(g_GPUDevice);
    if (!cmd)
    {
        AX_WARN("Failed to acquire command buffer :%s", SDL_GetError());
        Quit(2);
    }

    static int swapchainLogged = 0;
    SDL_GPUTexture* swapchainTexture;
    Uint32 screenW, screenH;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, g_SDLWindow, &swapchainTexture, &screenW, &screenH))
    {
        if (swapchainLogged++ < 4) AX_WARN("Failed to acquire swapchain texture: %s", SDL_GetError());
        Quit(2);
    }

    if (swapchainTexture == NULL)
    {
        if (swapchainLogged++ < 4) AX_WARN("Failed to acquire swapchain texture");
        SDL_CancelGPUCommandBuffer(cmd);
        return;
    }

    WindowState* winstate = &g_WindowState;
    if (winstate->prev_drawablew != screenW || winstate->prev_drawableh != screenH)
        ResizeWindowFrameTextures(winstate, screenW, screenH);
    winstate->prev_drawablew = screenW;
    winstate->prev_drawableh = screenH;

    if (GetKeyReleased(SDLK_O))
    {
        g_EnableOcclusion = !g_EnableOcclusion;
        AX_LOG("Hi-Z occlusion %s", g_EnableOcclusion ? "enabled" : "disabled");
    }
    if (GetKeyReleased(SDLK_H))
    {
        g_EnableHBAO = !g_EnableHBAO;
        AX_LOG("HBAO %s", g_EnableHBAO ? "enabled" : "disabled");
    }

    SDL_GPUColorTargetInfo        color_target      = MakeMainColorTarget(winstate);
    SDL_GPUColorTargetInfo        color_load_target = MakeLoadedSceneColorTarget(winstate);
    SDL_GPUColorTargetInfo        post_load_target  = MakeLoadedPostTarget(winstate);
    SDL_GPUColorTargetInfo        gbuffer_targets[3];
    MakeGBufferTargets(winstate, gbuffer_targets);
    SDL_GPUDepthStencilTargetInfo depth_target      = MakeDepthTarget(winstate->tex_depth, SDL_GPU_LOADOP_CLEAR, true);
    SDL_GPUDepthStencilTargetInfo main_depth_target = MakeDepthTarget(winstate->tex_depth, SDL_GPU_LOADOP_LOAD, false);
    SDL_GPUColorTargetInfo        hiz_depth_target  = MakeHiZDepthTarget(winstate);
    UploadRenderSetEntities(&skinnedSet, &g_RenderState.skinnedBuffers);
    UploadRenderSetEntities(&surfaceSet, &g_RenderState.surfaceBuffers);

    mat4x4 viewProj = M44Multiply(g_Camera.view, g_Camera.projection);
    bool enableHiZ  = g_EnableOcclusion && winstate->hiz_valid;
    mat4x4 hiZViewProj = enableHiZ ? winstate->hiz_view_proj : viewProj;

    FrustumPlanes cameraFrustum = CreateFrustumPlanes(viewProj);
    CullScene(cmd, cameraFrustum, hiZViewProj, enableHiZ, true);
    AnimateSkinned(cmd);

    static ShadowCascadeData cachedShadowCascades;
    static u32 shadowFrameIndex = 0;
    static bool shadowCacheValid = false;
    static const u8 cascadeCadance[] = { 0xF, 0x5, 0x2 }; // 1111, 0101, 0010

    ShadowCascadeData shadowCascades = GetShadowCascades();
    u32 shadowFrame = (shadowFrameIndex++) & 3; 
    u8 frameBit = (1 << shadowFrame); 

    for (u32 cascade = 0; cascade < SHADOW_CASCADE_COUNT; cascade++)
    {
        bool updateCascade = !shadowCacheValid || ((cascadeCadance[cascade] & frameBit) != 0);
        if (!updateCascade) continue;

        cachedShadowCascades.lightViewProj[cascade] = shadowCascades.lightViewProj[cascade];
        cachedShadowCascades.splitDistances[cascade] = shadowCascades.splitDistances[cascade];

        mat4x4 shadowViewProj = cachedShadowCascades.lightViewProj[cascade];
        FrustumPlanes shadowFrustum = CreateFrustumPlanes(shadowViewProj);
        // planes.planes[4] = planes.planes[5] = VecZero(); // disable near, far plane frustum check
        CullScene(cmd, shadowFrustum, shadowViewProj, false, false);

        SDL_GPUColorTargetInfo shadow_color_target = MakeShadowColorTarget(winstate, cascade);
        SDL_GPUDepthStencilTargetInfo shadow_depth_target = MakeShadowDepthTarget(winstate->tex_shadow_depth, cascade);
        RenderDepth(cmd, &(DepthPassContext){
            .colorTarget     = &shadow_color_target,
            .depthTarget     = &shadow_depth_target,
            .skinnedPipeline = g_RenderState.skinnedShadowPipeline,
            .surfacePipeline = g_RenderState.surfaceShadowPipeline,
            .viewProj        = shadowViewProj
        });
    }

    CullScene(cmd, cameraFrustum, hiZViewProj, enableHiZ, true);

    RenderDepth(cmd, &(DepthPassContext){
        .colorTarget     = &hiz_depth_target,
        .depthTarget     = &depth_target,
        .skinnedPipeline = g_RenderState.skinnedDepthPipeline,
        .surfacePipeline = g_RenderState.surfaceDepthPipeline,
        .viewProj        = viewProj
    });
    RenderScene(cmd, &(ScenePassContext){
        .colorTargets = gbuffer_targets,
        .numColorTargets = SDL_arraysize(gbuffer_targets),
        .depthTarget = &main_depth_target,
        .shadowCascades = cachedShadowCascades,
        .viewProj    = viewProj
    });
    DispatchHBAOCompute(cmd, g_EnableHBAO, screenW, screenH);
    DispatchDeferredLightingCompute(cmd, screenW, screenH, viewProj);
    RenderLines(cmd, &color_load_target, &main_depth_target, viewProj);
    DispatchHiZBuildCompute(cmd);

    winstate->hiz_view_proj = viewProj;
    winstate->hiz_valid = true;
    shadowCacheValid = true;

    DispatchTonemapCompute(cmd, winstate->tex_color, winstate->tex_hiz_depth, winstate->tex_post, screenW, screenH, viewProj);
    RenderSlugDemo(cmd, &post_load_target, &main_depth_target, viewProj);
    
    UIBeginFrame();
    UIRenderCallback();
    UIEndFrame(cmd, &post_load_target);

    SDL_GPUBlitInfo blit_info;
    SDL_zero(blit_info);
    blit_info.source.texture = winstate->tex_post;
    blit_info.source.w = screenW;
    blit_info.source.h = screenH;
    blit_info.destination.texture = swapchainTexture;
    blit_info.destination.w = screenW;
    blit_info.destination.h = screenH;
    blit_info.load_op = SDL_GPU_LOADOP_DONT_CARE;
    blit_info.filter = SDL_GPU_FILTER_LINEAR;
    SDL_BlitGPUTexture(cmd, &blit_info);
    SDL_SubmitGPUCommandBuffer(cmd);
}

void RendererInit(void)
{
    InitRenderPipelines();
    SlugInitDemo();
}

static void DestroyRenderSetBuffers(RenderSetBuffers* buffers)
{
    if (buffers->entity)               SDL_ReleaseGPUBuffer(g_GPUDevice, buffers->entity);
    if (buffers->primitiveGroup)       SDL_ReleaseGPUBuffer(g_GPUDevice, buffers->primitiveGroup);
    if (buffers->drawSparseIndices)    SDL_ReleaseGPUBuffer(g_GPUDevice, buffers->drawSparseIndices);
    if (buffers->drawArgs)             SDL_ReleaseGPUBuffer(g_GPUDevice, buffers->drawArgs);
    if (buffers->denseToPrimitive)     SDL_ReleaseGPUBuffer(g_GPUDevice, buffers->denseToPrimitive);
    if (buffers->sparseToDense)        SDL_ReleaseGPUBuffer(g_GPUDevice, buffers->sparseToDense);
    if (buffers->visibleSparseIndices) SDL_ReleaseGPUBuffer(g_GPUDevice, buffers->visibleSparseIndices);
    if (buffers->visibilityMask)       SDL_ReleaseGPUBuffer(g_GPUDevice, buffers->visibilityMask);
    if (buffers->visibleCount)         SDL_ReleaseGPUBuffer(g_GPUDevice, buffers->visibleCount);
    if (buffers->dispatchArgs)         SDL_ReleaseGPUBuffer(g_GPUDevice, buffers->dispatchArgs);
}

void DestroyPipeline(void)
{
    DestroyRenderSetBuffers(&g_RenderState.skinnedBuffers);
    DestroyRenderSetBuffers(&g_RenderState.surfaceBuffers);
    if (g_RenderState.skinnedVertexBuffer)     SDL_ReleaseGPUBuffer(g_GPUDevice, g_RenderState.skinnedVertexBuffer);
    if (g_RenderState.skinnedAnimatedVertices) SDL_ReleaseGPUBuffer(g_GPUDevice, g_RenderState.skinnedAnimatedVertices);
    if (g_RenderState.surfaceVertexBuffer)     SDL_ReleaseGPUBuffer(g_GPUDevice, g_RenderState.surfaceVertexBuffer);
    if (g_RenderState.indexBuffer)             SDL_ReleaseGPUBuffer(g_GPUDevice, g_RenderState.indexBuffer);
    if (g_RenderState.lineBuffer)              SDL_ReleaseGPUBuffer(g_GPUDevice, g_RenderState.lineBuffer);
    if (g_RenderState.lineDrawArgsBuffer)      SDL_ReleaseGPUBuffer(g_GPUDevice, g_RenderState.lineDrawArgsBuffer);
    if (g_RenderState.uiShapeBuffer)           SDL_ReleaseGPUBuffer(g_GPUDevice, g_RenderState.uiShapeBuffer);
    if (g_RenderState.uiShapeDrawArgsBuffer)   SDL_ReleaseGPUBuffer(g_GPUDevice, g_RenderState.uiShapeDrawArgsBuffer);
    UIDestroy();
    SlugDestroyDemo();
    if (g_RenderState.textureDescriptorBuffer) SDL_ReleaseGPUBuffer(g_GPUDevice, g_RenderState.textureDescriptorBuffer);
    if (g_RenderState.materialBuffer)          SDL_ReleaseGPUBuffer(g_GPUDevice, g_RenderState.materialBuffer);
    if (g_RenderState.sampler)                 SDL_ReleaseGPUSampler(g_GPUDevice, g_RenderState.sampler);
    if (g_RenderState.hiZSampler)              SDL_ReleaseGPUSampler(g_GPUDevice, g_RenderState.hiZSampler);
    if (g_RenderState.shadowSampler)           SDL_ReleaseGPUSampler(g_GPUDevice, g_RenderState.shadowSampler);
    if (g_RenderState.albedoPages.handle)      SDL_ReleaseGPUTexture(g_GPUDevice, g_RenderState.albedoPages.handle);
    if (g_RenderState.normalPages.handle)      SDL_ReleaseGPUTexture(g_GPUDevice, g_RenderState.normalPages.handle);
    if (g_RenderState.metallicRoughnessPages.handle) SDL_ReleaseGPUTexture(g_GPUDevice, g_RenderState.metallicRoughnessPages.handle);
    DestroyRenderPipelines();

    SDL_zero(g_RenderState);
    g_GPUDevice = NULL;
}

void Quit(s32 rc)
{
    DestroyPipeline();
    GraphicsDestroy();
    exit(rc);
}
