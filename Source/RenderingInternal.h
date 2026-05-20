#ifndef CP_RENDERING_INTERNAL
#define CP_RENDERING_INTERNAL

#include "Include/Rendering.h"
#include "Include/Graphics.h"
#include "Include/Platform.h"
#include "Include/GLTFParser.h"
#include "Include/Animation.h"
#include "Include/AssetManager.h"
#include "Include/Random.h"
#include "Include/Camera.h"
#include "Include/Memory.h"
#include "Include/RenderSet.h"
#include "Math/Half.h"

#define BReadRasterBit   SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ
#define BWriteComputeBit SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE
#define BReadCompute     SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ
#define BIndirectBit     SDL_GPU_BUFFERUSAGE_INDIRECT
#define BVertexBit       SDL_GPU_BUFFERUSAGE_VERTEX

#define SHADOW_MAX_DISTANCE    300.0f
#define SHADOW_ORTHO_SIZE      64.0f
#define SHADOW_NEAR_PLANE      1.0f
#define SHADOW_FAR_PLANE       300.0f
#define SHADOW_CAMERA_DISTANCE 200.0f

typedef struct DepthPassContext_
{
    SDL_GPUColorTargetInfo* colorTarget;
    SDL_GPUDepthStencilTargetInfo* depthTarget;
    const SDL_GPUViewport* viewport;
    const SDL_Rect* scissor;
    SDL_GPUGraphicsPipeline* skinnedPipeline;
    SDL_GPUGraphicsPipeline* surfacePipeline;
    mat4x4 viewProj;
} DepthPassContext;

typedef struct ScenePassContext_
{
    SDL_GPUColorTargetInfo* colorTarget;
    SDL_GPUDepthStencilTargetInfo* depthTarget;
    mat4x4 shadowViewProj;
    mat4x4 viewProj;
} ScenePassContext;

extern SceneBundle*   gPaladin;
extern SceneBundle*   gSponza;
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

void InitRenderPipelines(void);
void DestroyRenderPipelines(void);

mat4x4 GetShadowViewProj(void);

void DispatchCullDrawArgsCompute(SDL_GPUCommandBuffer* cmd, 
                                 RenderSet* renderSet,
                                 RenderSetBuffers* buffers,
                                 FrustumPlanes frustumPlanes,
                                 mat4x4 viewProj,
                                 bool enableHiZ,
                                 bool enableVisibilityOutput);

void DispatchHiZBuildCompute(SDL_GPUCommandBuffer* cmd);
void DispatchTonemapCompute(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* source, SDL_GPUTexture* destination, u32 width, u32 height);
void DispatchAnimationCompute(SDL_GPUCommandBuffer* cmd, RenderSet* renderSet);
void DispatchAnimateVerticesCompute(SDL_GPUCommandBuffer* cmd, RenderSet* renderSet);

void RenderDepth(SDL_GPUCommandBuffer* cmd, const DepthPassContext* ctx);

void RenderScene(SDL_GPUCommandBuffer* cmd, const ScenePassContext* ctx);

#endif
