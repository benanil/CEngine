#include "RenderingInternal.h"
#include "Include/Slug.h"
#include "Include/UIRenderer.h"
#include "Include/Platform.h"
#include "Include/Animation.h"
#include "Include/Scene.h"

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
    SDL_GPUTexture* tex_hbao;
    SDL_GPUTexture* tex_hbao_blur;
    SDL_GPUTexture* tex_hbao_normal;
    SDL_GPUTexture* tex_mlaa_edge_mask;
    SDL_GPUTexture* tex_mlaa_edge_count;
    SDL_GPUTexture* tex_mlaa_output;
    u64 releaseFrame;
} FrameTextureSet;

WindowState    g_WindowState;
RenderState    g_RenderState;
SDL_GPUDevice* g_GPUDevice = NULL;
LightGPU       g_RenderLights[MAX_LIGHT_COUNT];

RenderSettings g_RenderSettings = {
    .enableOcclusion             = true,
    .enableHBAO                  = true,
    .enableMLAA                  = true,
    .showMLAAEdges               = false,
    .enableLocalLights           = true,
    .enableLightFrustumCulling   = true,
    .enableLightOcclusionCulling = true,
    .showLightRects              = false,
    .hbaoRadius                  = 1.3f,
    .hbaoBias                    = 0.5f,
    .hbaoIntensity               = 2.0f,
    .hbaoPower                   = 2.0f,
    .mlaaThreshold               = 0.08f,
    .exposure                    = 1.0f,
    .gamma                       = 2.2f,
    .godRayIntensity             = 2.5f,
    .lodDistanceModifier         = 0.75f,
    .sunYaw                      = 116.565f,
    .sunPitch                    = 63.435f,
    .shadowMaxDistance           = SHADOW_MAX_DISTANCE,
    .shadowCameraDistance        = SHADOW_CAMERA_DISTANCE,
    .shadowCasterDepthMargin     = SHADOW_CASTER_DEPTH_MARGIN,
    .shadowCascadeOverlap        = SHADOW_CASCADE_OVERLAP,
    .shadowSplitNearDistance     = SHADOW_SPLIT_NEAR_DISTANCE,
    .shadowPSSMLambda            = SHADOW_PSSM_LAMBDA,
    .maxVisiblePointShadows      = 8.0f,
    .maxVisibleSpotShadows       = 8.0f
};

static u64 g_RenderFrameIndex;

static RenderLightDebugInfo g_LightDebugInfo;
static FrameTextureSet g_ResizeReleaseQueue[RESIZE_RELEASE_DELAY];

extern void UIRenderCallback(void); // Editor.c

extern ShadowCascadeData CascadedShadowmaps(SDL_GPUCommandBuffer* cmd);

static void ReleaseFrameTextureSet(FrameTextureSet* set)
{
    if (set->tex_depth)                    SDL_ReleaseGPUTexture(g_GPUDevice, set->tex_depth);
    if (set->tex_hiz_depth)                SDL_ReleaseGPUTexture(g_GPUDevice, set->tex_hiz_depth);
    if (set->tex_color)                    SDL_ReleaseGPUTexture(g_GPUDevice, set->tex_color);
    if (set->tex_gbuffer_tangent)          SDL_ReleaseGPUTexture(g_GPUDevice, set->tex_gbuffer_tangent);
    if (set->tex_gbuffer_albedo_metallic)  SDL_ReleaseGPUTexture(g_GPUDevice, set->tex_gbuffer_albedo_metallic);
    if (set->tex_gbuffer_shadow_roughness) SDL_ReleaseGPUTexture(g_GPUDevice, set->tex_gbuffer_shadow_roughness);
    if (set->tex_post)                     SDL_ReleaseGPUTexture(g_GPUDevice, set->tex_post);
    if (set->tex_hiz)                      SDL_ReleaseGPUTexture(g_GPUDevice, set->tex_hiz);
    if (set->tex_hbao)                     SDL_ReleaseGPUTexture(g_GPUDevice, set->tex_hbao);
    if (set->tex_hbao_blur)                SDL_ReleaseGPUTexture(g_GPUDevice, set->tex_hbao_blur);
    if (set->tex_hbao_normal)              SDL_ReleaseGPUTexture(g_GPUDevice, set->tex_hbao_normal);
    if (set->tex_mlaa_edge_mask)           SDL_ReleaseGPUTexture(g_GPUDevice, set->tex_mlaa_edge_mask);
    if (set->tex_mlaa_edge_count)          SDL_ReleaseGPUTexture(g_GPUDevice, set->tex_mlaa_edge_count);
    if (set->tex_mlaa_output)              SDL_ReleaseGPUTexture(g_GPUDevice, set->tex_mlaa_output);
    *set = (FrameTextureSet){0};
}

static void QueueWindowFrameTexturesForRelease(WindowState* winstate)
{
    FrameTextureSet old = {
        .tex_depth                    = winstate->tex_depth,
        .tex_hiz_depth                = winstate->tex_hiz_depth,
        .tex_color                    = winstate->tex_color,
        .tex_gbuffer_tangent          = winstate->tex_gbuffer_tangent,
        .tex_gbuffer_albedo_metallic  = winstate->tex_gbuffer_albedo_metallic,
        .tex_gbuffer_shadow_roughness = winstate->tex_gbuffer_shadow_roughness,
        .tex_post                     = winstate->tex_post,
        .tex_hiz                      = winstate->tex_hiz,
        .tex_hbao                     = winstate->tex_hbao,
        .tex_hbao_blur                = winstate->tex_hbao_blur,
        .tex_hbao_normal              = winstate->tex_hbao_normal,
        .tex_mlaa_edge_mask           = winstate->tex_mlaa_edge_mask,
        .tex_mlaa_edge_count          = winstate->tex_mlaa_edge_count,
        .tex_mlaa_output              = winstate->tex_mlaa_output,
        .releaseFrame                 = g_RenderFrameIndex + RESIZE_RELEASE_DELAY
    };
    winstate->tex_depth = winstate->tex_hiz_depth = winstate->tex_color = winstate->tex_gbuffer_tangent 
                        = winstate->tex_gbuffer_albedo_metallic = winstate->tex_gbuffer_shadow_roughness 
                        = winstate->tex_post = winstate->tex_hiz = winstate->tex_hbao 
                        = winstate->tex_hbao_blur = winstate->tex_hbao_normal = winstate->tex_mlaa_edge_mask 
                        = winstate->tex_mlaa_edge_count = winstate->tex_mlaa_output = NULL;
    
    u32 slot = (u32)(g_RenderFrameIndex % RESIZE_RELEASE_DELAY);
    ReleaseFrameTextureSet(&g_ResizeReleaseQueue[slot]);
    g_ResizeReleaseQueue[slot] = old;
}

void RendererSetLights(const LightGPU* lights, u32 count)
{
    if (!lights && count > 0u)
    {
        AX_WARN("RendererSetLights called with null lights");
        count = 0u;
    }
    count = Minu32(count, MAX_LIGHT_COUNT);
    if (count > 0u) MemCopy(g_RenderLights, lights, sizeof(LightGPU) * count);
    g_RenderState.numLights = count;
    g_LightDebugInfo.totalLights = count;
    g_LightDebugInfo.maxLights = MAX_LIGHT_COUNT;
}

u32 RendererGetLightCount(void)
{
    return g_RenderState.numLights;
}

RenderLightDebugInfo RendererGetLightDebugInfo(void)
{
    g_LightDebugInfo.totalLights = g_RenderState.numLights;
    if (!g_RenderSettings.enableLocalLights || g_RenderState.numLights == 0u)
        g_LightDebugInfo.submittedLights = 0u;
    g_LightDebugInfo.maxLights = MAX_LIGHT_COUNT;
    return g_LightDebugInfo;
}

void CreateRenderSetBuffers(RenderSetBuffers* buffers, u32 maxEntities, u32 maxGroups)
{
    size_t groupBytes  = maxGroups * sizeof(PrimitiveGroup);
    size_t entityBytes = maxEntities * sizeof(Entity);
    size_t lodMultiplier = MESH_LOD_COUNT;

    buffers->primitiveGroup    = CreateBuffer(NULL, groupBytes, BReadCompute, "CPPrimitiveGroups");
    buffers->drawSparseIndices = CreateBuffer(NULL, maxEntities * lodMultiplier * sizeof(u32), BReadRasterBit |  BWriteComputeBit, "CPDrawSparseIndices");
    buffers->sparseToDense     = CreateBuffer(NULL, maxEntities * sizeof(u32), BReadCompute, "CPSparseToDense");
    buffers->drawArgs          = CreateBuffer(NULL, maxGroups * lodMultiplier * sizeof(SDL_GPUIndexedIndirectDrawCommand), BIndirectBit | BWriteComputeBit, "CPDrawArgs");
    buffers->denseToPrimitive  = CreateBuffer(NULL, maxEntities * sizeof(u32), BReadCompute, "CPDenseToPrimitive");
    buffers->entity            = CreateBuffer(NULL, entityBytes, BReadRasterBit | BReadCompute, "CPEntities");
    buffers->visibilityMask    = CreateBuffer(NULL, maxEntities * sizeof(u32), BWriteComputeBit, "CPVisibilityMask");
    buffers->visibleCount      = CreateBuffer(NULL, sizeof(u32), BWriteComputeBit, "CPVisibleCount");
    buffers->dispatchArgs      = CreateBuffer(NULL, sizeof(u32) * 6, BIndirectBit | BWriteComputeBit, "CPDispatchArgs");
    buffers->visibleSparseIndices = CreateBuffer(NULL, maxEntities * sizeof(u32), BWriteComputeBit, "CPVisibleSparseIndices");
}

// re-uploads the render set data that only changes when entities or bundles are added/removed
static void UploadRenderSetStatics(const RenderSet* set, RenderSetBuffers* buffers)
{
    UpdateGPUBuffer(buffers->primitiveGroup, set->primitiveGroups, set->maxGroups * sizeof(PrimitiveGroup), 0);
    UpdateGPUBuffer(buffers->sparseToDense, set->sparseID, set->maxEntities * sizeof(u32), 0);
    UpdateGPUBuffer(buffers->denseToPrimitive, set->denseToPrimitiveIndex, set->maxEntities * sizeof(u32), 0);
}

// geometry ranges loaders queued for upload, flushed once the gpu buffers exist.
// element units, bundles can land anywhere in the mega buffers now
#define GEOMETRY_DIRTY_MAX 32u

typedef struct GeometryDirtyRange_ { u32 begin, end; } GeometryDirtyRange;
static GeometryDirtyRange g_GeometryDirty[GeometryBuffer_Count][GEOMETRY_DIRTY_MAX];
static u32 g_NumGeometryDirty[GeometryBuffer_Count];

void Rendering_QueueGeometryUpload(GeometryBufferKind kind, u32 begin, u32 end)
{
    if (end <= begin || kind < 0 || kind >= GeometryBuffer_Count) return;
    u32* num = &g_NumGeometryDirty[kind];
    if (*num == GEOMETRY_DIRTY_MAX)
    {
        // full, widen the last range instead of dropping the upload
        GeometryDirtyRange* last = &g_GeometryDirty[kind][GEOMETRY_DIRTY_MAX - 1u];
        last->begin = Minu32(last->begin, begin);
        last->end   = Maxu32(last->end, end);
        return;
    }
    g_GeometryDirty[kind][(*num)++] = (GeometryDirtyRange){ begin, end };
}

// uploads the queued geometry ranges, called every frame and on init
static void UploadDirtyGeometry(void)
{
    if (!g_RenderState.indexBuffer) return;

    SDL_GPUBuffer* gpuBuffers[GeometryBuffer_Count] = {
        g_RenderState.skinned.vertexBuffer, g_RenderState.surface.vertexBuffer, g_RenderState.indexBuffer
    };
    const u8* sources[GeometryBuffer_Count] = {
        (const u8*)gGFX.SkinnedVertexBuffer, (const u8*)gGFX.SurfaceVertexBuffer, (const u8*)gGFX.IndexBuffer
    };
    const size_t strides[GeometryBuffer_Count] = { sizeof(ASkinedVertex), sizeof(AVertex), sizeof(u32) };

    for (u32 kind = 0; kind < GeometryBuffer_Count; kind++)
    {
        for (u32 i = 0; i < g_NumGeometryDirty[kind]; i++)
        {
            GeometryDirtyRange range = g_GeometryDirty[kind][i];
            UpdateGPUBuffer(gpuBuffers[kind],
                            sources[kind] + (size_t)range.begin * strides[kind],
                            (size_t)(range.end - range.begin) * strides[kind],
                            (size_t)range.begin * strides[kind]);
        }
        g_NumGeometryDirty[kind] = 0;
    }
}

void InitBuffers(void)
{
    const size_t animatedVertexSize = sizeof(u32) * 2 * MAX_ANIMATED_VERTEX;
    g_RenderState.skinned.vertexBuffer = CreateBuffer(NULL, MAX_SKINNED_SOURCE_VERTEX * sizeof(ASkinedVertex), BVertexBit | BReadCompute, "CPSkinnedVertexBuffer");
    g_RenderState.surface.vertexBuffer = CreateBuffer(NULL, MAX_VERTEX * sizeof(AVertex), BVertexBit, "CPSurfaceVertexBuffer");
    g_RenderState.indexBuffer          = CreateBuffer(NULL, MAX_INDEX  * sizeof(int)    , SDL_GPU_BUFFERUSAGE_INDEX, "CPIndexBuffer");
    g_RenderState.lineBuffer           = CreateBuffer(NULL, sizeof(ALineVertex) * MAX_LINE_COUNT   , BVertexBit     | BWriteComputeBit, "CPLineVertexBuffer");
    g_RenderState.lineDrawArgsBuffer   = CreateBuffer(NULL, sizeof(u32) * 8                        , BIndirectBit   | BWriteComputeBit, "CPLinedrawArgsBuffer");
    g_RenderState.lightBuffer          = CreateBuffer(NULL, sizeof(LightGPU) * MAX_LIGHT_COUNT     , BReadRasterBit | BReadCompute    , "CPLightBuffer");
    g_RenderState.lightDrawInfoBuffer  = CreateBuffer(NULL, sizeof(LightDrawInfo) * MAX_LIGHT_COUNT, BReadRasterBit | BWriteComputeBit, "CPLightDrawInfoBuffer");
    g_RenderState.lightDrawArgsBuffer  = CreateBuffer(NULL, sizeof(SDL_GPUIndirectDrawCommand)     , BIndirectBit   | BWriteComputeBit, "CPLightDrawArgsBuffer");
    g_RenderState.skinned.animatedVertices = CreateBuffer(NULL, animatedVertexSize, BReadRasterBit | BWriteComputeBit, "CPAnimatedVertices");

    UploadDirtyGeometry();
    g_LightDebugInfo.maxLights = MAX_LIGHT_COUNT;
    UIInit();
}

static void UploadLightBuffer(void)
{
    if (!g_RenderState.lightBuffer || g_RenderState.numLights == 0u)
    {
        g_LightDebugInfo.submittedLights = 0u;
        return;
    }
    UpdateGPUBuffer(g_RenderState.lightBuffer, g_RenderLights, sizeof(LightGPU) * g_RenderState.numLights, 0);
    g_LightDebugInfo.submittedLights = g_RenderState.numLights;
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


static void ResizeWindowFrameTextures(WindowState* winstate, u32 width, u32 height)
{
    QueueWindowFrameTexturesForRelease(winstate);
    CreateWindowBuffers();
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

SDL_GPUDepthStencilTargetInfo MakeDepthTarget(SDL_GPUTexture* texture, SDL_GPULoadOp loadOp, bool cycle)
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

static void UploadRenderSetEntities(RenderSet* set, RenderSetBuffers* buffers)
{
    if (set->numEntities == 0) return;
    UpdateGPUBuffer(buffers->entity, set->entities, set->numEntities * sizeof(Entity), 0ull);
}

void CullScene(SDL_GPUCommandBuffer* cmd, FrustumPlanes planes, mat4x4 viewProj, bool enableHiZ, bool enableSurfaceLOD, u32 forcedLOD)
{
    for (u32 s = 0; s < g_NumActiveScenes; s++)
    {
        Scene* scene = g_ActiveScenes[s];
        DispatchCullDrawArgsCompute(cmd, &scene->skinnedSet, &scene->skinnedBuffers, planes, viewProj, enableHiZ, false, false, true, forcedLOD);
        DispatchCullDrawArgsCompute(cmd, &scene->surfaceSet, &scene->surfaceBuffers, planes, viewProj, enableHiZ, false, false, enableSurfaceLOD, forcedLOD);
    }
}

static void GatherSkinnedAnimationVisibility(SDL_GPUCommandBuffer* cmd, RenderSet* skinnedSet, RenderSetBuffers* skinnedBuffers,
                                             FrustumPlanes cameraFrustum, mat4x4 cameraViewProj, bool enableHiZ,
                                             const ShadowData* pointShadows, const ShadowData* spotShadows)
{
    DispatchCullDrawArgsCompute(cmd, skinnedSet, skinnedBuffers, cameraFrustum, cameraViewProj,
                                enableHiZ, true, true, true, ~0u);

    ShadowCascadeData cascades = GetShadowCascades();
    for (u32 cascade = 0; cascade < SHADOW_CASCADE_COUNT; cascade++)
    {
        mat4x4 shadowViewProj = cascades.lightViewProj[cascade];
        FrustumPlanes shadowFrustum = CreateFrustumPlanes(shadowViewProj);
        DispatchCullDrawArgsCompute(cmd, skinnedSet, skinnedBuffers, shadowFrustum, shadowViewProj,
                                    false, true, false, false, 1u);
    }

    for (u32 shadow = 0; shadow < pointShadows->count; shadow++)
    {
        LightGPU* light = &g_RenderLights[pointShadows->lightIndices[shadow]];
        for (u32 face = 0; face < POINT_SHADOW_FACE_COUNT; face++)
        {
            u32 layer = light->shadowIndex * POINT_SHADOW_FACE_COUNT + face;
            mat4x4 shadowViewProj = pointShadows->lightViewProj[layer];
            DispatchCullDrawArgsCompute(cmd, skinnedSet, skinnedBuffers, CreateFrustumPlanes(shadowViewProj), shadowViewProj,
                                        false, true, false, false, 1u);
        }
    }

    for (u32 shadow = 0; shadow < spotShadows->count; shadow++)
    {
        LightGPU* light = &g_RenderLights[spotShadows->lightIndices[shadow]];
        mat4x4 shadowViewProj = spotShadows->lightViewProj[light->shadowIndex];
        DispatchCullDrawArgsCompute(cmd, skinnedSet, skinnedBuffers, CreateFrustumPlanes(shadowViewProj), shadowViewProj,
                                    false, true, false, false, 1u);
    }
}

static void AnimateSkinned(SDL_GPUCommandBuffer* cmd)
{
    for (u32 s = 0; s < g_NumActiveScenes; s++)
    {
        Scene* scene = g_ActiveScenes[s];
        DispatchAnimationCompute(cmd, &scene->skinnedSet, &scene->skinnedBuffers, &scene->animSystem);
        DispatchAnimateVerticesCompute(cmd, &scene->skinnedSet, &scene->skinnedBuffers, &scene->animSystem);
    }
}

void Render(void)
{
    if (g_NumActiveScenes == 0) return;

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
    SDL_GPUDepthStencilTargetInfo depth_target      = MakeDepthTarget(winstate->tex_depth, SDL_GPU_LOADOP_CLEAR, true);
    SDL_GPUDepthStencilTargetInfo main_depth_target = MakeDepthTarget(winstate->tex_depth, SDL_GPU_LOADOP_LOAD, false);
    SDL_GPUColorTargetInfo        hiz_depth_target  = MakeHiZDepthTarget(winstate);
    SDL_GPUColorTargetInfo        gbuffer_targets[3];
    MakeGBufferTargets(winstate, gbuffer_targets);
    UploadDirtyGeometry();
    for (u32 s = 0; s < g_NumActiveScenes; s++)
    {
        Scene* scene = g_ActiveScenes[s];
        if (scene->renderDataDirty)
        {
            UploadRenderSetStatics(&scene->skinnedSet, &scene->skinnedBuffers);
            UploadRenderSetStatics(&scene->surfaceSet, &scene->surfaceBuffers);
            scene->renderDataDirty = 0;
        }
        UploadRenderSetEntities(&scene->skinnedSet, &scene->skinnedBuffers);
        UploadRenderSetEntities(&scene->surfaceSet, &scene->surfaceBuffers);
    }
    mat4x4 viewProj = M44Multiply(g_Camera.view, g_Camera.projection);
    bool enableHiZ  = g_RenderSettings.enableOcclusion && winstate->hiz_valid;
    mat4x4 hiZViewProj = enableHiZ ? winstate->hiz_view_proj : viewProj;

    FrustumPlanes cameraFrustum = CreateFrustumPlanes(viewProj);
    UpdateLightShadows();
    UploadLightBuffer();
    for (u32 s = 0; s < g_NumActiveScenes; s++)
    {
        Scene* scene = g_ActiveScenes[s];
        GatherSkinnedAnimationVisibility(cmd, &scene->skinnedSet, &scene->skinnedBuffers,
                                         cameraFrustum, hiZViewProj, enableHiZ, &pointShadows, &spotShadows);
    }
    AnimateSkinned(cmd);

    if (g_RenderSettings.enableLocalLights)
    {
        RenderShadows(cmd);
    }

    ShadowCascadeData shadowCascades = CascadedShadowmaps(cmd);
    UploadShadowCascadeBuffer(&shadowCascades);
    CullScene(cmd, cameraFrustum, hiZViewProj, enableHiZ, true, ~0u);

    RenderDepth(cmd, &(DepthPassContext){
        .colorTarget       = &hiz_depth_target,
        .depthTarget       = &depth_target,
        .skinnedPipeline   = g_RenderState.skinned.depthPipeline,
        .surfacePipeline   = g_RenderState.surface.depthPipeline,
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
    DispatchHiZBuildCompute(cmd);
    if (g_RenderSettings.enableLocalLights)
    {
        DispatchCullLightsCompute(cmd, cameraFrustum, viewProj,
                                  g_RenderSettings.enableLightFrustumCulling,
                                  g_RenderSettings.enableOcclusion && g_RenderSettings.enableLightOcclusionCulling,
                                  screenW, screenH);
        RenderDeferredLights(cmd, &color_load_target, viewProj, screenW, screenH);
    }
    RenderLines(cmd, &color_load_target, &main_depth_target, viewProj);

    winstate->hiz_view_proj = viewProj;
    winstate->hiz_valid = true;

    DispatchTonemapCompute(cmd, screenW, screenH, viewProj);
    SDL_GPUTexture* finalTexture = winstate->tex_post;
    if (g_RenderSettings.enableMLAA && winstate->tex_mlaa_output)
    {
        DispatchMLAACompute(cmd, screenW, screenH, g_RenderSettings.mlaaThreshold, g_RenderSettings.showMLAAEdges);
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

void DestroyRenderSetBuffers(RenderSetBuffers* buffers)
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
    if (g_RenderState.skinned.vertexBuffer)     SDL_ReleaseGPUBuffer(g_GPUDevice, g_RenderState.skinned.vertexBuffer);
    if (g_RenderState.skinned.animatedVertices) SDL_ReleaseGPUBuffer(g_GPUDevice, g_RenderState.skinned.animatedVertices);
    if (g_RenderState.surface.vertexBuffer)     SDL_ReleaseGPUBuffer(g_GPUDevice, g_RenderState.surface.vertexBuffer);
    if (g_RenderState.indexBuffer)              SDL_ReleaseGPUBuffer(g_GPUDevice, g_RenderState.indexBuffer);
    if (g_RenderState.lineBuffer)               SDL_ReleaseGPUBuffer(g_GPUDevice, g_RenderState.lineBuffer);
    if (g_RenderState.lineDrawArgsBuffer)       SDL_ReleaseGPUBuffer(g_GPUDevice, g_RenderState.lineDrawArgsBuffer);
    if (g_RenderState.lightBuffer)              SDL_ReleaseGPUBuffer(g_GPUDevice, g_RenderState.lightBuffer);
    if (g_RenderState.pointShadowMatrixBuffer)  SDL_ReleaseGPUBuffer(g_GPUDevice, g_RenderState.pointShadowMatrixBuffer);
    if (g_RenderState.spotShadowMatrixBuffer)   SDL_ReleaseGPUBuffer(g_GPUDevice, g_RenderState.spotShadowMatrixBuffer);
    if (g_RenderState.lightDrawInfoBuffer)      SDL_ReleaseGPUBuffer(g_GPUDevice, g_RenderState.lightDrawInfoBuffer);
    if (g_RenderState.lightDrawArgsBuffer)      SDL_ReleaseGPUBuffer(g_GPUDevice, g_RenderState.lightDrawArgsBuffer);
    if (g_RenderState.uiShapeBuffer)            SDL_ReleaseGPUBuffer(g_GPUDevice, g_RenderState.uiShapeBuffer);
    if (g_RenderState.uiShapeDrawArgsBuffer)    SDL_ReleaseGPUBuffer(g_GPUDevice, g_RenderState.uiShapeDrawArgsBuffer);
    if (g_RenderState.sampler)                  SDL_ReleaseGPUSampler(g_GPUDevice, g_RenderState.sampler);
    if (g_RenderState.hiZSampler)               SDL_ReleaseGPUSampler(g_GPUDevice, g_RenderState.hiZSampler);
    if (g_RenderState.shadowSampler)            SDL_ReleaseGPUSampler(g_GPUDevice, g_RenderState.shadowSampler);
    while (g_NumActiveScenes > 0)
        Scene_Destroy(g_ActiveScenes[g_NumActiveScenes - 1]);
    TextureSystem_DestroyDevice();
    UIDestroy();
    SlugDestroyDemo();
    DestroyRenderPipelines();
    SDL_zero(g_RenderState);
    for (u32 kind = 0; kind < GeometryBuffer_Count; kind++)
        g_NumGeometryDirty[kind] = 0;
    g_GPUDevice = NULL;
}

void Quit(s32 rc)
{
    DestroyPipeline();
    GraphicsDestroy();
    exit(rc);
}
