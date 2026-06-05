#include "RenderingInternal.h"
#include "Include/Slug.h"
#include "Include/UIRenderer.h"
#include "Include/Platform.h"

WindowState    g_WindowState;
RenderState    g_RenderState;
SDL_GPUDevice* g_GPUDevice = NULL;

RenderSettings g_RenderSettings = {
    .enableOcclusion = true,
    .enableHBAO      = true,
    .enableMLAA      = true,
    .showMLAAEdges   = false,
    .enableSDSM      = false,
    .hbaoRadius      = 1.3f,
    .hbaoBias        = 0.5f,
    .hbaoIntensity   = 2.0f,
    .hbaoPower       = 2.0f,
    .mlaaThreshold   = 0.08f,
    .exposure        = 1.0f,
    .gamma           = 2.2f,
    .godRayIntensity = 2.5f,
    .lodDistanceModifier = 0.25f,
    .sunYaw          = 116.565f,
    .sunPitch        = 63.435f,
    .shadowMaxDistance       = SHADOW_MAX_DISTANCE,
    .shadowCameraDistance    = SHADOW_CAMERA_DISTANCE,
    .shadowCasterDepthMargin = SHADOW_CASTER_DEPTH_MARGIN,
    .shadowCascadeOverlap    = SHADOW_CASCADE_OVERLAP,
    .shadowSplitNearDistance = SHADOW_SPLIT_NEAR_DISTANCE,
    .shadowPSSMLambda        = SHADOW_PSSM_LAMBDA
};

#define RESIZE_RELEASE_DELAY 4u

typedef struct FrameTextureSet_
{
    SDL_GPUTexture* tex_depth;
    SDL_GPUTexture* tex_hiz_depth;
    SDL_GPUTexture* tex_color;
    SDL_GPUTexture* tex_gbuffer_tangent;
    SDL_GPUTexture* tex_gbuffer_albedo_metallic;
    SDL_GPUTexture* tex_gbuffer_shadow_roughness;
    SDL_GPUTexture* tex_post;
    SDL_GPUTexture* tex_hiz;
    SDL_GPUTexture* tex_sdsm_bounds;
    SDL_GPUTexture* tex_hbao;
    SDL_GPUTexture* tex_hbao_blur;
    SDL_GPUTexture* tex_hbao_normal;
    SDL_GPUTexture* tex_mlaa_edge_mask;
    SDL_GPUTexture* tex_mlaa_edge_count;
    SDL_GPUTexture* tex_mlaa_output;
    u64 releaseFrame;
} FrameTextureSet;

static FrameTextureSet g_ResizeReleaseQueue[RESIZE_RELEASE_DELAY];
static u64 g_RenderFrameIndex;

extern void UIRenderCallback(void); // Editor.c

static void InitRenderSetBuffers(RenderSetBuffers* buffers, RenderSet* set)
{
    size_t groupBytes  = set->maxGroups * sizeof(PrimitiveGroup);
    size_t entityBytes = set->maxEntities * sizeof(Entity);
    size_t lodMultiplier = MESH_LOD_COUNT;

    buffers->primitiveGroup    = CreateBuffer(set->primitiveGroups, groupBytes, BReadCompute, "CPPrimitiveGroups");
    buffers->drawSparseIndices = CreateBuffer(NULL, set->maxEntities * lodMultiplier * sizeof(u32), BReadRasterBit | BReadCompute | BWriteComputeBit, "CPDrawSparseIndices");
    buffers->sparseToDense     = CreateBuffer(set->sparseID, set->maxEntities * sizeof(u32), BReadCompute, "CPSparseToDense");
    buffers->drawArgs          = CreateBuffer(NULL, set->maxGroups * lodMultiplier * sizeof(SDL_GPUIndexedIndirectDrawCommand), BIndirectBit | BReadCompute | BWriteComputeBit, "CPDrawArgs");
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
    g_RenderState.skinnedVertexBuffer = CreateBuffer(NULL, MAX_SKINNED_SOURCE_VERTEX * sizeof(ASkinedVertex), BVertexBit | BReadCompute, "CPSkinnedVertexBuffer");
    g_RenderState.surfaceVertexBuffer = CreateBuffer(NULL, MAX_VERTEX * sizeof(AVertex), BVertexBit, "CPSurfaceVertexBuffer");
    g_RenderState.indexBuffer = CreateBuffer(NULL, MAX_INDEX * sizeof(int), SDL_GPU_BUFFERUSAGE_INDEX, "CPIndexBuffer");
    if (gGFX.NumSkinnedVertices > 0) UpdateGPUBuffer(g_RenderState.skinnedVertexBuffer, gGFX.SkinnedVertexBuffer, gGFX.NumSkinnedVertices * sizeof(ASkinedVertex), 0);
    if (gGFX.NumSurfaceVertices > 0) UpdateGPUBuffer(g_RenderState.surfaceVertexBuffer, gGFX.SurfaceVertexBuffer, gGFX.NumSurfaceVertices * sizeof(AVertex), 0);
    if (gGFX.NumIndices > 0) UpdateGPUBuffer(g_RenderState.indexBuffer, gGFX.IndexBuffer, gGFX.NumIndices * sizeof(u32), 0);
    g_RenderState.skinnedAnimatedVertices = CreateBuffer(NULL, animatedVertexSize, BReadRasterBit | BWriteComputeBit, "CPAnimatedVertices");
    g_RenderState.shadowCascadeBuffer = CreateBuffer(NULL, sizeof(mat4x4) * SHADOW_CASCADE_COUNT + sizeof(float) * 4,
                                                      BReadRasterBit | BReadCompute | BWriteComputeBit, "CPShadowCascadeBuffer");
    g_RenderState.lineBuffer = CreateBuffer(NULL, sizeof(ALineVertex) * MAX_LINE_COUNT, BVertexBit | BWriteComputeBit, "CPLineVertexBuffer");
    g_RenderState.lineDrawArgsBuffer = CreateBuffer(NULL, sizeof(u32) * 8, BIndirectBit | BWriteComputeBit, "CPLinedrawArgsBuffer");
    UIInit();
}

static void ReleaseFrameTextureSet(FrameTextureSet* set)
{
    if (set->tex_depth) SDL_ReleaseGPUTexture(g_GPUDevice, set->tex_depth);
    if (set->tex_hiz_depth) SDL_ReleaseGPUTexture(g_GPUDevice, set->tex_hiz_depth);
    if (set->tex_color) SDL_ReleaseGPUTexture(g_GPUDevice, set->tex_color);
    if (set->tex_gbuffer_tangent) SDL_ReleaseGPUTexture(g_GPUDevice, set->tex_gbuffer_tangent);
    if (set->tex_gbuffer_albedo_metallic) SDL_ReleaseGPUTexture(g_GPUDevice, set->tex_gbuffer_albedo_metallic);
    if (set->tex_gbuffer_shadow_roughness) SDL_ReleaseGPUTexture(g_GPUDevice, set->tex_gbuffer_shadow_roughness);
    if (set->tex_post) SDL_ReleaseGPUTexture(g_GPUDevice, set->tex_post);
    if (set->tex_hiz) SDL_ReleaseGPUTexture(g_GPUDevice, set->tex_hiz);
    if (set->tex_sdsm_bounds) SDL_ReleaseGPUTexture(g_GPUDevice, set->tex_sdsm_bounds);
    if (set->tex_hbao) SDL_ReleaseGPUTexture(g_GPUDevice, set->tex_hbao);
    if (set->tex_hbao_blur) SDL_ReleaseGPUTexture(g_GPUDevice, set->tex_hbao_blur);
    if (set->tex_hbao_normal) SDL_ReleaseGPUTexture(g_GPUDevice, set->tex_hbao_normal);
    if (set->tex_mlaa_edge_mask) SDL_ReleaseGPUTexture(g_GPUDevice, set->tex_mlaa_edge_mask);
    if (set->tex_mlaa_edge_count) SDL_ReleaseGPUTexture(g_GPUDevice, set->tex_mlaa_edge_count);
    if (set->tex_mlaa_output) SDL_ReleaseGPUTexture(g_GPUDevice, set->tex_mlaa_output);
    *set = (FrameTextureSet){0};
}

static void ReleaseQueuedResizeTextures(bool force)
{
    for (u32 i = 0; i < RESIZE_RELEASE_DELAY; i++)
    {
        FrameTextureSet* set = &g_ResizeReleaseQueue[i];
        if (!set->releaseFrame) continue;
        if (force || g_RenderFrameIndex >= set->releaseFrame) ReleaseFrameTextureSet(set);
    }
}

static void QueueWindowFrameTexturesForRelease(WindowState* winstate)
{
    FrameTextureSet old = {
        .tex_depth = winstate->tex_depth,
        .tex_hiz_depth = winstate->tex_hiz_depth,
        .tex_color = winstate->tex_color,
        .tex_gbuffer_tangent = winstate->tex_gbuffer_tangent,
        .tex_gbuffer_albedo_metallic = winstate->tex_gbuffer_albedo_metallic,
        .tex_gbuffer_shadow_roughness = winstate->tex_gbuffer_shadow_roughness,
        .tex_post = winstate->tex_post,
        .tex_hiz = winstate->tex_hiz,
        .tex_sdsm_bounds = winstate->tex_sdsm_bounds,
        .tex_hbao = winstate->tex_hbao,
        .tex_hbao_blur = winstate->tex_hbao_blur,
        .tex_hbao_normal = winstate->tex_hbao_normal,
        .tex_mlaa_edge_mask = winstate->tex_mlaa_edge_mask,
        .tex_mlaa_edge_count = winstate->tex_mlaa_edge_count,
        .tex_mlaa_output = winstate->tex_mlaa_output,
        .releaseFrame = g_RenderFrameIndex + RESIZE_RELEASE_DELAY
    };

    winstate->tex_depth = NULL;
    winstate->tex_hiz_depth = NULL;
    winstate->tex_color = NULL;
    winstate->tex_gbuffer_tangent = NULL;
    winstate->tex_gbuffer_albedo_metallic = NULL;
    winstate->tex_gbuffer_shadow_roughness = NULL;
    winstate->tex_post = NULL;
    winstate->tex_hiz = NULL;
    winstate->tex_sdsm_bounds = NULL;
    winstate->tex_hbao = NULL;
    winstate->tex_hbao_blur = NULL;
    winstate->tex_hbao_normal = NULL;
    winstate->tex_mlaa_edge_mask = NULL;
    winstate->tex_mlaa_edge_count = NULL;
    winstate->tex_mlaa_output = NULL;

    u32 slot = (u32)(g_RenderFrameIndex % RESIZE_RELEASE_DELAY);
    ReleaseFrameTextureSet(&g_ResizeReleaseQueue[slot]);
    g_ResizeReleaseQueue[slot] = old;
}

static void ResizeWindowFrameTextures(WindowState* winstate, u32 width, u32 height)
{
    QueueWindowFrameTexturesForRelease(winstate);
    winstate->tex_depth     = CreateDepthTexture(width, height);
    winstate->tex_hiz_depth = CreateHiZDepthTexture(width, height);
    winstate->tex_color     = CreateSceneColorTexture(width, height, SDL_GPU_SAMPLECOUNT_1);
    winstate->tex_gbuffer_tangent = CreateGBufferTangentTexture(width, height);
    winstate->tex_gbuffer_albedo_metallic = CreateGBufferAlbedoMetallicTexture(width, height);
    winstate->tex_gbuffer_shadow_roughness = CreateGBufferShadowRoughnessTexture(width, height);
    winstate->tex_post      = CreatePostProcessTexture(width, height);
    winstate->tex_hiz       = CreateHiZTexture(width, height, &winstate->hiz_mip_count);
    winstate->tex_sdsm_bounds = CreateSDSMDepthBoundsTexture(width, height, &winstate->sdsm_mip_count);
    u32 hbaoWidth = Maxu32(width / 2u, 1u);
    u32 hbaoHeight = Maxu32(height / 2u, 1u);
    winstate->tex_hbao        = CreateHBAOTexture(hbaoWidth, hbaoHeight);
    winstate->tex_hbao_blur   = CreateHBAOTexture(hbaoWidth, hbaoHeight);
    winstate->tex_hbao_normal = CreateHBAONormalTexture(hbaoWidth, hbaoHeight);
    winstate->tex_mlaa_edge_mask = CreateMLAAEdgeMaskTexture(width, height);
    winstate->tex_mlaa_edge_count = CreateMLAAEdgeCountTexture(width, height);
    winstate->tex_mlaa_output = CreateMLAAOutputTexture(width, height);
    winstate->hiz_width     = width;
    winstate->hiz_height    = height;
    winstate->hiz_valid     = false;
    winstate->sdsm_valid    = false;
    Camera_RecalculateProjection(&g_Camera, (s32)width, (s32)height);
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

static SDL_GPUColorTargetInfo MakeLoadedTextureTarget(SDL_GPUTexture* texture)
{
    SDL_GPUColorTargetInfo target;
    SDL_zero(target);
    target.load_op  = SDL_GPU_LOADOP_LOAD;
    target.store_op = SDL_GPU_STOREOP_STORE;
    target.texture  = texture;
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
    SDL_GPUDepthStencilTargetInfo target = MakeDepthTarget(texture, SDL_GPU_LOADOP_CLEAR, false);
    target.mip_level = layer;
    return target;
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
    target.mip_level = layer;
    target.cycle = false;
    return target;
}

static void UploadRenderSetEntities(RenderSet* set, RenderSetBuffers* buffers)
{
    if (set->numEntities == 0) return;
    UpdateGPUBuffer(buffers->entity, set->entities, set->numEntities * sizeof(Entity), 0ull);
}

static void CullScene(SDL_GPUCommandBuffer* cmd, FrustumPlanes planes, mat4x4 viewProj, bool enableHiZ, bool enableSurfaceLOD, u32 forcedLOD)
{
    DispatchCullDrawArgsCompute(cmd, &skinnedSet, &g_RenderState.skinnedBuffers, planes, viewProj, enableHiZ, false, false, true, forcedLOD);
    DispatchCullDrawArgsCompute(cmd, &surfaceSet, &g_RenderState.surfaceBuffers, planes, viewProj, enableHiZ, false, false, enableSurfaceLOD, forcedLOD);
}

static void GatherSkinnedAnimationVisibility(SDL_GPUCommandBuffer* cmd, FrustumPlanes cameraFrustum, mat4x4 cameraViewProj, bool enableHiZ)
{
    DispatchCullDrawArgsCompute(cmd, &skinnedSet, &g_RenderState.skinnedBuffers, cameraFrustum, cameraViewProj,
                                enableHiZ, true, true, true, ~0u);

    ShadowCascadeData cascades = GetShadowCascades();
    for (u32 cascade = 0; cascade < SHADOW_CASCADE_COUNT; cascade++)
    {
        mat4x4 shadowViewProj = cascades.lightViewProj[cascade];
        FrustumPlanes shadowFrustum = CreateFrustumPlanes(shadowViewProj);
        DispatchCullDrawArgsCompute(cmd, &skinnedSet, &g_RenderState.skinnedBuffers, shadowFrustum, shadowViewProj,
                                    false, true, false, false, 1u);
    }
}

static void AnimateSkinned(SDL_GPUCommandBuffer* cmd)
{
    DispatchAnimationCompute(cmd, &skinnedSet);
    DispatchAnimateVerticesCompute(cmd, &skinnedSet);
}

static void UploadShadowCascadeBuffer(const ShadowCascadeData* cascades)
{
    struct {
        mat4x4 lightViewProj[SHADOW_CASCADE_COUNT];
        float  splitDistances[4];
    } gpuCascades;

    for (u32 cascade = 0; cascade < SHADOW_CASCADE_COUNT; cascade++)
    {
        gpuCascades.lightViewProj[cascade] = M44Transpose(cascades->lightViewProj[cascade]);
        gpuCascades.splitDistances[cascade] = cascades->splitDistances[cascade];
    }
    gpuCascades.splitDistances[3] = cascades->splitDistances[SHADOW_CASCADE_COUNT - 1u];

    UpdateGPUBuffer(g_RenderState.shadowCascadeBuffer, &gpuCascades, sizeof(gpuCascades), 0);
}

static ShadowCascadeData CascadedShadowmaps(SDL_GPUCommandBuffer* cmd)
{
    static ShadowCascadeData cachedShadowCascades;
    static u32 shadowFrameIndex = 0;
    static bool shadowCacheValid = false;
    static const u8 cascadeCadance[] = { 0x5D, 0x22, 0x80 }; // 01011101, 00100010, 10000000
    ShadowCascadeData shadowCascades = GetShadowCascades();
    bool updateCascades[SHADOW_CASCADE_COUNT];
    u32 shadowFrame = (shadowFrameIndex++) & 7;
    u8 frameBit = (1u << shadowFrame);

    for (u32 cascade = 0; cascade < SHADOW_CASCADE_COUNT; cascade++)
    {
        bool updateCascade = !shadowCacheValid || ((cascadeCadance[cascade] & frameBit) != 0);
        updateCascades[cascade] = updateCascade;
        if (!updateCascade) continue;

        cachedShadowCascades.lightViewProj[cascade] = shadowCascades.lightViewProj[cascade];
        cachedShadowCascades.splitDistances[cascade] = shadowCascades.splitDistances[cascade];
    }

    UploadShadowCascadeBuffer(&cachedShadowCascades);

    for (u32 cascade = 0; cascade < SHADOW_CASCADE_COUNT; cascade++)
    {
        if (!updateCascades[cascade]) continue;

        mat4x4 shadowViewProj = cachedShadowCascades.lightViewProj[cascade];
        FrustumPlanes shadowFrustum = CreateFrustumPlanes(shadowViewProj);
        // planes.planes[4] = planes.planes[5] = VecZero(); // disable near, far plane frustum check
        CullScene(cmd, shadowFrustum, shadowViewProj, false, false, 1u);

        WindowState* winstate = &g_WindowState;
        SDL_GPUColorTargetInfo shadow_color_target = MakeShadowColorTarget(winstate, cascade);
        SDL_GPUDepthStencilTargetInfo shadow_depth_target = MakeShadowDepthTarget(winstate->tex_shadow_depth, cascade);
        RenderDepth(cmd, &(DepthPassContext){
            .colorTarget       = &shadow_color_target,
            .depthTarget       = &shadow_depth_target,
            .skinnedPipeline   = g_RenderState.skinnedShadowPipeline,
            .surfacePipeline   = g_RenderState.surfaceShadowPipeline,
            .viewProj          = shadowViewProj,
            .cascadeIndex      = cascade,
            .useShadowCascades = true,
            .alphaClip         = false,
            .enableLOD         = false
        });
    }
    shadowCacheValid = true;
    return cachedShadowCascades;
}

static ShadowCascadeData SampleDistributionShadowMaps(SDL_GPUCommandBuffer* cmd, mat4x4 viewProj)
{
    ShadowCascadeData shadowCascades = GetShadowCascades();
    DispatchSDSMSetupShadowsCompute(cmd, viewProj);

    for (u32 cascade = 0; cascade < SHADOW_CASCADE_COUNT; cascade++)
    {
        mat4x4 shadowViewProj = shadowCascades.lightViewProj[cascade];
        CullScene(cmd, CreateFrustumPlanes(shadowViewProj), shadowViewProj, false, false, 1u);

        WindowState* winstate = &g_WindowState;
        SDL_GPUColorTargetInfo shadow_color_target = MakeShadowColorTarget(winstate, cascade);
        SDL_GPUDepthStencilTargetInfo shadow_depth_target = MakeShadowDepthTarget(winstate->tex_shadow_depth, cascade);
        RenderDepth(cmd, &(DepthPassContext){
            .colorTarget       = &shadow_color_target,
            .depthTarget       = &shadow_depth_target,
            .skinnedPipeline   = g_RenderState.skinnedShadowPipeline,
            .surfacePipeline   = g_RenderState.surfaceShadowPipeline,
            .viewProj          = shadowViewProj,
            .cascadeIndex      = cascade,
            .useShadowCascades = true,
            .alphaClip         = false,
            .enableLOD         = false
        });
    }
    return shadowCascades;
}

void Render(void)
{
    g_RenderFrameIndex++;
    ReleaseQueuedResizeTextures(false);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(g_GPUDevice);
    if (!cmd)
    {
        AX_WARN("Failed to acquire command buffer :%s", SDL_GetError());
        Quit(2);
    }

    static int swapchainLogged = 0;
    SDL_GPUTexture* swapchainTexture;
    Uint32 screenW, screenH;
    if (!SDL_AcquireGPUSwapchainTexture(cmd, g_SDLWindow, &swapchainTexture, &screenW, &screenH))
    {
        if (swapchainLogged++ < 4) AX_WARN("Failed to acquire swapchain texture: %s", SDL_GetError());
        SDL_CancelGPUCommandBuffer(cmd);
        return;
    }

    if (swapchainTexture == NULL || screenW == 0u || screenH == 0u)
    {
        if (swapchainLogged++ < 4) AX_WARN("Swapchain texture unavailable");
        SDL_CancelGPUCommandBuffer(cmd);
        return;
    }

    WindowState* winstate = &g_WindowState;
    if (winstate->prev_width != screenW || winstate->prev_height != screenH)
    {
        ResizeWindowFrameTextures(winstate, screenW, screenH);
        swapchainLogged = 0;
    }
    winstate->prev_width = screenW;
    winstate->prev_height = screenH;

    SDL_GPUColorTargetInfo        color_target      = MakeMainColorTarget(winstate);
    SDL_GPUColorTargetInfo        color_load_target = MakeLoadedSceneColorTarget(winstate);
    SDL_GPUColorTargetInfo        gbuffer_targets[3];
    MakeGBufferTargets(winstate, gbuffer_targets);
    SDL_GPUDepthStencilTargetInfo depth_target      = MakeDepthTarget(winstate->tex_depth, SDL_GPU_LOADOP_CLEAR, true);
    SDL_GPUDepthStencilTargetInfo main_depth_target = MakeDepthTarget(winstate->tex_depth, SDL_GPU_LOADOP_LOAD, false);
    SDL_GPUColorTargetInfo        hiz_depth_target  = MakeHiZDepthTarget(winstate);
    UploadRenderSetEntities(&skinnedSet, &g_RenderState.skinnedBuffers);
    UploadRenderSetEntities(&surfaceSet, &g_RenderState.surfaceBuffers);

    mat4x4 viewProj = M44Multiply(g_Camera.view, g_Camera.projection);
    bool enableHiZ  = g_RenderSettings.enableOcclusion && winstate->hiz_valid;
    mat4x4 hiZViewProj = enableHiZ ? winstate->hiz_view_proj : viewProj;

    FrustumPlanes cameraFrustum = CreateFrustumPlanes(viewProj);
    GatherSkinnedAnimationVisibility(cmd, cameraFrustum, hiZViewProj, enableHiZ);
    AnimateSkinned(cmd);
    ShadowCascadeData shadowCascades = g_RenderSettings.enableSDSM ? 
                                       SampleDistributionShadowMaps(cmd, viewProj) : 
                                       CascadedShadowmaps(cmd);

    CullScene(cmd, cameraFrustum, hiZViewProj, enableHiZ, true, ~0u);

    RenderDepth(cmd, &(DepthPassContext){
        .colorTarget       = &hiz_depth_target,
        .depthTarget       = &depth_target,
        .skinnedPipeline   = g_RenderState.skinnedDepthPipeline,
        .surfacePipeline   = g_RenderState.surfaceDepthPipeline,
        .viewProj          = viewProj,
        .cascadeIndex      = 0,
        .useShadowCascades = false,
        .alphaClip         = true,
        .enableLOD         = true
    });
    RenderScene(cmd, &(ScenePassContext){
        .colorTargets    = gbuffer_targets,
        .numColorTargets = SDL_arraysize(gbuffer_targets),
        .depthTarget     = &main_depth_target,
        .shadowCascades  = shadowCascades,
        .viewProj        = viewProj
    });
    DispatchHBAOCompute(cmd, g_RenderSettings.enableHBAO, screenW, screenH);
    DispatchDeferredLightingCompute(cmd, screenW, screenH, viewProj);
    RenderLines(cmd, &color_load_target, &main_depth_target, viewProj);
    DispatchHiZBuildCompute(cmd);
    if (g_RenderSettings.enableSDSM) DispatchSDSMDepthBoundsCompute(cmd);

    winstate->hiz_view_proj = viewProj;
    winstate->hiz_valid = true;

    DispatchTonemapCompute(cmd, winstate->tex_color, winstate->tex_hiz_depth, winstate->tex_post, screenW, screenH, viewProj);
    SDL_GPUTexture* finalTexture = winstate->tex_post;
    if (g_RenderSettings.enableMLAA && winstate->tex_mlaa_output)
    {
        DispatchMLAACompute(cmd, winstate->tex_post, winstate->tex_mlaa_output, screenW, screenH, g_RenderSettings.mlaaThreshold, g_RenderSettings.showMLAAEdges);
        finalTexture = winstate->tex_mlaa_output;
    }

    SDL_GPUColorTargetInfo final_load_target = MakeLoadedTextureTarget(finalTexture);
    RenderSlugDemo(cmd, &final_load_target, &main_depth_target, viewProj);
    
    UIBeginFrame();
    UIRenderCallback();
    UIEndFrame(cmd, &final_load_target);

    SDL_GPUBlitInfo blit_info;
    SDL_zero(blit_info);
    blit_info.source.texture = finalTexture;
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
    ReleaseQueuedResizeTextures(true);
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
