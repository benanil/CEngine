#ifndef CP_RENDERING_INTERNAL
#define CP_RENDERING_INTERNAL

#include "Include/Rendering.h"
#include "Include/Graphics.h"
#include "Include/Platform.h"
#include "Include/Camera.h"
#include "Include/RenderSet.h"
#include "Include/Scene.h"

typedef struct ShadowCascadeData_
{
    mat4x4 lightViewProj[SHADOW_CASCADE_COUNT];
    float  splitDistances[SHADOW_CASCADE_COUNT];
} ShadowCascadeData;

typedef struct ShadowData_
{
    mat4x4 lightViewProj[POINT_SHADOW_LAYER_COUNT];
    u32 lightIndices[POINT_SHADOW_MAX_LIGHTS];
    u32 count;
} ShadowData;

typedef struct DepthPassContext_
{
    SDL_GPUColorTargetInfo* colorTarget;
    SDL_GPUDepthStencilTargetInfo* depthTarget;
    const SDL_GPUViewport* viewport;
    const SDL_Rect* scissor;
    SDL_GPUGraphicsPipeline* skinnedPipeline;
    SDL_GPUGraphicsPipeline* surfacePipeline;
    mat4x4 viewProj;
    u32 cascadeIndex;
    bool useShadowCascades;
    bool usePointShadowSides;
    bool useSpotShadowSides;
    bool alphaClip;
    bool enableLOD;
} DepthPassContext;

typedef enum CullDrawFlags_
{
    CullDrawFlag_None                = 0,
    CullDrawFlag_EnableHiZ           = 1u << 0,
    CullDrawFlag_VisibilityOutput    = 1u << 1,
    CullDrawFlag_ResetVisibility     = 1u << 2,
    CullDrawFlag_EnableLODSelection  = 1u << 3,
    CullDrawFlag_CullSphere          = 1u << 4,
    CullDrawFlag_NoFrustum           = 1u << 5
} CullDrawFlags;

typedef struct ScenePassContext_
{
    SDL_GPUColorTargetInfo* colorTargets;
    u32 numColorTargets;
    SDL_GPUDepthStencilTargetInfo* depthTarget;
    ShadowCascadeData shadowCascades;
    mat4x4 viewProj;
} ScenePassContext;

extern WindowState    g_WindowState;
extern RenderState    g_RenderState;
extern SDL_GPUDevice* g_GPUDevice;
extern SDL_Window*    g_SDLWindow;
extern Camera         g_Camera;
extern Graphics       gGFX;
extern LightGPU       g_RenderLights[MAX_LIGHT_COUNT];
extern ShadowData pointShadows;
extern ShadowData spotShadows;

// Per-light visibility read back from the previous frame's light cull
// (1 = light passed frustum + occlusion culling). When g_LightVisibilityGate is
// true, shadow slot assignment skips lights whose entry is 0 so occluded lights
// don't render shadow maps. One frame of latency by design (no GPU stall).
extern u32  g_LightVisiblePrev[MAX_LIGHT_COUNT];
extern bool g_LightVisibilityGate;

extern SDL_GPUComputePipeline* g_AnimComputePipeline;
extern SDL_GPUComputePipeline* g_AnimVerticesPipeline;
extern SDL_GPUComputePipeline* g_CullDrawArgsComputePipeline;
extern SDL_GPUComputePipeline* g_CullLightsComputePipeline;
extern SDL_GPUComputePipeline* g_TonemapComputePipeline;
extern SDL_GPUComputePipeline* g_HiZBuildComputePipeline;
extern SDL_GPUComputePipeline* g_HiZDownscaleComputePipeline;
extern SDL_GPUComputePipeline* g_HBAOComputePipeline;
extern SDL_GPUComputePipeline* g_HBAOBlurComputePipeline;
extern SDL_GPUComputePipeline* g_ExtractNormalComputePipeline;
extern SDL_GPUComputePipeline* g_DeferredLightingComputePipeline;
extern SDL_GPUComputePipeline* g_MLAAEdgeMaskComputePipeline;
extern SDL_GPUComputePipeline* g_MLAALineLengthComputePipeline;
extern SDL_GPUComputePipeline* g_MLAABlendComputePipeline;

extern SDL_GPUGraphicsPipeline* g_OutlinePipeline;
extern SDL_GPUGraphicsPipeline* g_GizmoLinePipeline;
extern ALineVertex g_GizmoVertices[MAX_GIZMO_VERTICES];
extern u32         g_NumGizmoVertices;

extern OutlineTarget g_OutlineTargets[MAX_OUTLINE_TARGETS];
extern u32           g_NumOutlineTargets;

void InitRenderPipelines(void);
void DestroyRenderPipelines(void);

ShadowCascadeData GetShadowCascades(void);
float3 GetRenderSunDirection(void);

void DispatchCullDrawArgsCompute(SDL_GPUCommandBuffer* cmd,
                                 RenderSet*          renderSet,
                                 RenderSetBuffers*   buffers,
                                 FrustumPlanes       frustumPlanes,
                                 mat4x4              viewProj,
                                 CullDrawFlags       flags,
                                 u32                 forcedLOD,
                                 u32                 instanceMultiplier,
                                 const f32           cullSphere[4]);

void DispatchHiZBuildCompute(SDL_GPUCommandBuffer* cmd);
void DispatchHBAOCompute(SDL_GPUCommandBuffer* cmd, bool enabled, u32 width, u32 height);
void DispatchDeferredLightingCompute(SDL_GPUCommandBuffer* cmd, u32 width, u32 height, mat4x4 viewProj);
void DispatchCullLightsCompute(SDL_GPUCommandBuffer* cmd, FrustumPlanes frustumPlanes, mat4x4 viewProj, bool enableFrustum, bool enableHiZ, u32 width, u32 height);
void DispatchTonemapCompute(SDL_GPUCommandBuffer* cmd, u32 width, u32 height, mat4x4 viewProj);
void DispatchMLAACompute(SDL_GPUCommandBuffer* cmd, u32 width, u32 height, f32 threshold, bool showEdges);
void DispatchAnimationCompute(SDL_GPUCommandBuffer* cmd, RenderSet* renderSet, RenderSetBuffers* buffers, AnimationSystem* anims);
void DispatchAnimateVerticesCompute(SDL_GPUCommandBuffer* cmd, RenderSet* renderSet, RenderSetBuffers* buffers, AnimationSystem* anims);

void RenderDepth(SDL_GPUCommandBuffer* cmd, const DepthPassContext* ctx);

void RenderScene(SDL_GPUCommandBuffer* cmd, const ScenePassContext* ctx);
void RenderDeferredLights(SDL_GPUCommandBuffer* cmd, SDL_GPUColorTargetInfo* colorTarget, mat4x4 viewProj, u32 width, u32 height);
void RenderShadows(SDL_GPUCommandBuffer* cmd);
void RenderLines(SDL_GPUCommandBuffer* cmd, SDL_GPUColorTargetInfo* colorTarget, SDL_GPUDepthStencilTargetInfo* depthTarget, mat4x4 viewProj);
void RenderOutline(SDL_GPUCommandBuffer* cmd, SDL_GPUColorTargetInfo* colorTarget, SDL_GPUDepthStencilTargetInfo* depthTarget, mat4x4 viewProj);
void RenderGizmo(SDL_GPUCommandBuffer* cmd, SDL_GPUColorTargetInfo* colorTarget, mat4x4 viewProj);

SDL_GPUDepthStencilTargetInfo MakeDepthTarget(SDL_GPUTexture* texture, SDL_GPULoadOp loadOp, bool cycle);
void UploadShadowCascadeBuffer(const ShadowCascadeData* cascades);
void UpdateLightShadows(void);
void CullScene(SDL_GPUCommandBuffer* cmd, FrustumPlanes planes, mat4x4 viewProj, CullDrawFlags flags, u32 forcedLOD);

#endif
