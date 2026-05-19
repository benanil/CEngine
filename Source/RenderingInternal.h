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

#define SHADOW_MAP_SIZE        4096u
#define SHADOW_ORTHO_SIZE      64.0f
#define SHADOW_NEAR_PLANE      1.0f
#define SHADOW_FAR_PLANE       300.0f
#define SHADOW_CAMERA_DISTANCE 200.0f

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

void DispatchCullDrawArgsCompute(SDL_GPUCommandBuffer* cmd, RenderSet* renderSet,
                                 RenderSetBuffers* buffers,
                                 FrustumPlanes frustumPlanes,
                                 mat4x4 viewProj,
                                 SDL_GPUTexture* hiZTexture,
                                 u32 hiZWidth,
                                 u32 hiZHeight,
                                 u32 hiZMipCount,
                                 bool enableHiZ,
                                 bool enableVisibilityOutput);

void DispatchHiZBuildCompute(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* depthTexture, SDL_GPUTexture* hiZTexture,
                             u32 width, u32 height, u32 mipCount, u32 firstMip);
void DispatchTonemapCompute(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* source, SDL_GPUTexture* destination, u32 width, u32 height);
void DispatchAnimationCompute(SDL_GPUCommandBuffer* cmd, RenderSet* renderSet);
void DispatchAnimateVerticesCompute(SDL_GPUCommandBuffer* cmd, RenderSet* renderSet);

void RenderDepthPrepass(SDL_GPUCommandBuffer* cmd,
                        SDL_GPUColorTargetInfo* color_target,
                        SDL_GPUDepthStencilTargetInfo* depth_target,
                        mat4x4 viewProj,
                        SDL_GPUGraphicsPipeline* skinnedPipeline,
                        SDL_GPUGraphicsPipeline* surfacePipeline);
void RenderScene(SDL_GPUCommandBuffer* cmd,
                 SDL_GPUColorTargetInfo* color_target,
                 SDL_GPUDepthStencilTargetInfo* depth_target,
                 mat4x4 viewProj);

#endif
