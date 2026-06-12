#ifndef TERRAIN_H
#define TERRAIN_H

#include "Common.h"
#include "Camera.h"
#include "BVH.h"
#include <SDL3/SDL_gpu.h>

#if defined(__cplusplus)
extern "C" {
#endif

// streamed voxel terrain meshed with the Transvoxel algorithm (https://transvoxel.org/).
// chunks generate on worker threads from a procedural density field and render through
// a dedicated pipeline, independent from the scene render sets

typedef struct TerrainStats_
{
    u32 liveChunks;     // chunks holding a mesh or known empty
    u32 emptyChunks;    // chunks with no surface crossing
    u32 queuedChunks;   // waiting for a worker
    u32 jobsInFlight;
    u32 drawnChunks;    // after frustum culling, last g-buffer pass
    u32 numVertices;    // resident in the terrain vertex heap
    u32 numIndices;
} TerrainStats;

void Terrain_Init(void);     // after RendererInit + InitBuffers, gpu device must exist
void Terrain_Destroy(void);

// streams chunks around the camera and consumes finished worker jobs, call once per
// frame from the main loop before Render()
void Terrain_Update(const Camera* camera);

void Terrain_SetEnabled(bool enabled);
bool Terrain_GetEnabled(void);

// casts a world space ray against the resident terrain meshes (the same set that
// draws: live, not hidden, retiring included). dir must be normalized, hits beyond
// maxDist are rejected. fills the scene BVHHit: t/u/v/position/triIndex are valid,
// entityIdx holds the terrain chunk slot, groupIdx the chunk lod, and
// skinnedSet/bundleIdx are 0xFFFFFFFF to mark a terrain hit. out: 1 when hit
s32 Terrain_Raycast(float3 origin, float3 dir, f32 maxDist, BVHHit* hit);
bool Terrain_HasDraws(void); // any live chunk with geometry while enabled
TerrainStats Terrain_GetStats(void);

// renderer hooks, implemented in Terrain.c and called by Rendering.c/RenderingDraw.c.
// flush records copy passes for pending chunk meshes on the frame command buffer and
// must run before any render pass uses the terrain buffers
void Terrain_GPUFlush(SDL_GPUCommandBuffer* cmd);
void Terrain_RenderDepth(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass, mat4x4 viewProj);
void Terrain_RenderGBuffer(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass, mat4x4 viewProj);
// line overlay over the lit scene, enabled by g_RenderSettings.terrainWireframe
void Terrain_RenderWireframe(SDL_GPUCommandBuffer* cmd, SDL_GPUColorTargetInfo* colorTarget,
                             SDL_GPUDepthStencilTargetInfo* depthTarget, mat4x4 viewProj);

#if defined(__cplusplus)
}
#endif

#endif // TERRAIN_H
