#ifndef CP_RENDERING_INTERNAL
#define CP_RENDERING_INTERNAL

#include "Include/Rendering.h"
#include "Include/Graphics.h"
#include "Include/Platform.h"
#include "Include/Camera.h"
#include "Include/RenderSet.h"


typedef struct ShadowCascadeData_
{
    mat4x4 lightViewProj[SHADOW_CASCADE_COUNT];
    float  splitDistances[SHADOW_CASCADE_COUNT];
} ShadowCascadeData;

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
    bool alphaClip;
    bool enableLOD;
} DepthPassContext;

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
extern RenderSet      skinnedSet;
extern RenderSet      surfaceSet;

extern SDL_GPUComputePipeline* g_AnimComputePipeline;
extern SDL_GPUComputePipeline* g_AnimVerticesPipeline;
extern SDL_GPUComputePipeline* g_CullDrawArgsComputePipeline;
extern SDL_GPUComputePipeline* g_TonemapComputePipeline;
extern SDL_GPUComputePipeline* g_HiZBuildComputePipeline;
extern SDL_GPUComputePipeline* g_HiZDownscaleComputePipeline;
extern SDL_GPUComputePipeline* g_SDSMDepthBoundsInitialPipeline;
extern SDL_GPUComputePipeline* g_SDSMDepthBoundsReducePipeline;
extern SDL_GPUComputePipeline* g_SDSMSetupShadowsPipeline;
extern SDL_GPUComputePipeline* g_HBAOComputePipeline;
extern SDL_GPUComputePipeline* g_HBAOBlurComputePipeline;
extern SDL_GPUComputePipeline* g_ExtractNormalComputePipeline;
extern SDL_GPUComputePipeline* g_DeferredLightingComputePipeline;
extern SDL_GPUComputePipeline* g_MLAAEdgeMaskComputePipeline;
extern SDL_GPUComputePipeline* g_MLAALineLengthComputePipeline;
extern SDL_GPUComputePipeline* g_MLAABlendComputePipeline;

void InitRenderPipelines(void);
void DestroyRenderPipelines(void);

ShadowCascadeData GetShadowCascades(void);
float3 GetRenderSunDirection(void);

void DispatchCullDrawArgsCompute(SDL_GPUCommandBuffer* cmd, 
                                 RenderSet* renderSet,
                                 RenderSetBuffers* buffers,
                                 FrustumPlanes frustumPlanes,
                                  mat4x4 viewProj,
                                  bool enableHiZ,
                                  bool enableVisibilityOutput,
                                  bool enableLOD,
                                  u32 forcedLOD);

void DispatchHiZBuildCompute(SDL_GPUCommandBuffer* cmd);
void DispatchSDSMSetupShadowsCompute(SDL_GPUCommandBuffer* cmd, mat4x4 viewProj);
void DispatchSDSMDepthBoundsCompute(SDL_GPUCommandBuffer* cmd);
void DispatchHBAOCompute(SDL_GPUCommandBuffer* cmd, bool enabled, u32 width, u32 height);
void DispatchDeferredLightingCompute(SDL_GPUCommandBuffer* cmd, u32 width, u32 height, mat4x4 viewProj);
void DispatchTonemapCompute(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* source, SDL_GPUTexture* depth, SDL_GPUTexture* destination, u32 width, u32 height, mat4x4 viewProj);
void DispatchMLAACompute(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* source, SDL_GPUTexture* destination, u32 width, u32 height, f32 threshold, bool showEdges);
void DispatchAnimationCompute(SDL_GPUCommandBuffer* cmd, RenderSet* renderSet);
void DispatchAnimateVerticesCompute(SDL_GPUCommandBuffer* cmd, RenderSet* renderSet);

void RenderDepth(SDL_GPUCommandBuffer* cmd, const DepthPassContext* ctx);

void RenderScene(SDL_GPUCommandBuffer* cmd, const ScenePassContext* ctx);
void RenderLines(SDL_GPUCommandBuffer* cmd, SDL_GPUColorTargetInfo* colorTarget, SDL_GPUDepthStencilTargetInfo* depthTarget, mat4x4 viewProj);

#endif
