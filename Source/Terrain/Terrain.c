// streamed transvoxel terrain. owns its chunk streaming, worker jobs, gpu buffers and
// pipelines, fully independent from the scene render sets: chunks draw as direct indexed
// calls after cpu frustum culling, lod selection is decided here (transition cells must
// match the neighbor lod, the engine's gpu lod picking cannot be used).
//
// streaming: concentric per lod boxes around the camera, snapped to the parent (coarser)
// grid so neighboring chunks never differ by more than one lod level. the finer chunk
// owns the transition cells on faces toward a coarser neighbor.
#include "Include/Terrain.h"
#include "TerrainInternal.h"
#include "Source/Rendering/RenderingInternal.h"
#include "Include/Memory.h"
#include "Include/DataStructures/HashMap.h"

#include "Extern/stb/stb_image.h"
#include "Extern/stb/stb_image_resize2.h"

#if defined(PLATFORM_MACOSX)
#include "Shaders/msl/TerrainVert.msl.h"
#include "Shaders/msl/TerrainFrag.msl.h"
#include "Shaders/msl/TerrainDepthOnlyVert.msl.h"
#include "Shaders/msl/TerrainDepthOnlyFrag.msl.h"
#include "Shaders/msl/TerrainWireFrag.msl.h"

#define Shaders_TerrainVert_spv Shaders_TerrainVert_msl
#define Shaders_TerrainFrag_spv Shaders_TerrainFrag_msl
#define Shaders_TerrainDepthOnlyVert_spv Shaders_TerrainDepthOnlyVert_msl
#define Shaders_TerrainDepthOnlyFrag_spv Shaders_TerrainDepthOnlyFrag_msl
#define Shaders_TerrainWireFrag_spv Shaders_TerrainWireFrag_msl
#elif defined(PLATFORM_WINDOWS)
#include "Shaders/spv/TerrainVert.spv.h"
#include "Shaders/spv/TerrainFrag.spv.h"
#include "Shaders/spv/TerrainDepthOnlyVert.spv.h"
#include "Shaders/spv/TerrainDepthOnlyFrag.spv.h"
#include "Shaders/spv/TerrainWireFrag.spv.h"
#endif

#include <SDL3/SDL_cpuinfo.h>
#include <SDL3/SDL_timer.h>

#define TERRAIN_MAX_CHUNKS      2048u
#define TERRAIN_MAX_JOBS        16u
#define TERRAIN_MAX_WORKERS     8u
#define TERRAIN_RING_RADIUS     2
#define TERRAIN_MAX_VERTICES    (1u << 20)             // 16 MB of TerrainVertex
#define TERRAIN_MAX_INDICES     (4u << 20)             // 16 MB of u32
#define TERRAIN_TRANSFER_BYTES  (8u * 1024u * 1024u)
#define TERRAIN_PENDING_CAP     4096u
#define TERRAIN_DEBUG_HOLES     0    // 1: log a streaming audit + raycast hole probe every 240 frames

// chunks up to this lod keep a cpu copy of their mesh for Terrain_Raycast. the coarse
// rings are most of the resident geometry but gameplay rays rarely need exact hits
// out there, so by default only the near lod 0/1 rings are raycastable. the debug
// hole probe compares the whole drawn set against the density field, it needs all lods
#define TERRAIN_RAYCAST_KEEP_LOD 1u
#define TERRAIN_ALBEDO_SIZE     2048
#define TERRAIN_DETAIL_SIZE     1024                   // normal + arm layers

enum TerrainChunkState_
{
    ChunkState_Queued = 0,  // waiting for a worker
    ChunkState_Generating,  // worker running, nothing to draw yet
    ChunkState_Live,        // mesh resident (or known empty), drawn
};

enum TerrainJobState_
{
    JobState_Free = 0,
    JobState_Queued,   // submitted, waiting for a pool worker
    JobState_Running,
    JobState_Done,
};

typedef struct TerrainChunk_
{
    s32 x, y, z;
    u8  lod;
    u8  state;
    u8  transitionMask; // desired mask, bit per face -x +x -y +y -z +z
    u8  appliedMask;    // mask the resident mesh was built with
    u8  empty;          // no surface crossing, stays in the map so it is not requeried
    u8  dying;          // evicted while a worker still runs, slot freed at job consume
    u8  needRemesh;
    u8  used;
    u8  retiring;       // left the desired set but keeps drawing until replacements are live
    u8  hidden;         // live replacement waiting for the whole split/merge swap to be ready
    u32 gen;            // desired set generation, stale chunks get evicted
    s32 jobSlot;        // -1 when no worker owns this chunk
    u32 vertexOffset, numVertices;
    u32 indexOffset, numIndices;
    float3 aabbMin, aabbMax; // world space, tight bounds from the mesher
    // cpu copy of the resident mesh for Terrain_Raycast, same lifetime as the gpu ranges
    TerrainVertex* cpuVertices;
    u32*           cpuIndices;
} TerrainChunk;

typedef struct TerrainJob_
{
    SDL_AtomicInt state;  // 0 free, 1 running, 2 done
    SDL_AtomicInt cancel;
    s32 x, y, z;
    u32 lod;
    u8  transitionMask;
    u8  empty;
    s32 result;
    u32 chunkSlot;
    s8* density;                 // allocated once per slot, reused
    TerrainMeshScratch scratch;  // allocated once per slot, reused
    TerrainMeshOut mesh;         // capacity persists across jobs
} TerrainJob;

typedef struct TerrainHeapBlock_
{
    u32 offset;
    u32 count;
} TerrainHeapBlock;

typedef struct TerrainHeap_
{
    TerrainHeapBlock* freeBlocks; // sorted by offset
    u32 numFreeBlocks;
    u32 freeBlockCapacity;
    u32 used;
} TerrainHeap;

typedef struct TerrainBox_ { s32 min[3], max[3]; } TerrainBox; // inclusive chunk coords

typedef struct TerrainState_
{
    bool initialized;
    bool enabled;

    TerrainChunk* chunks;     // tlsf, TERRAIN_MAX_CHUNKS
    u32*          freeSlots;
    u32           numFreeSlots;
    HashMap       chunkMap;   // packed (x,y,z,lod) -> chunk slot

    u32* pending;             // chunk slots waiting for a worker, entries can go stale
    u32  numPending;

    TerrainJob jobs[TERRAIN_MAX_JOBS];

    TerrainHeap vertexHeap;
    TerrainHeap indexHeap;

    u32 gen;
    s32 lastCamChunk[3];
    bool desiredValid;
    TerrainBox levelBox[TERRAIN_LOD_COUNT];
    FrustumPlanes cameraFrustum; // updated every frame for load prioritization
    bool frustumValid;
    f32 lodFactor;               // snapshot of g_RenderSettings.terrainLodFactor

    TerrainGenParams genParams;
    bool fixedCenterValid;       // fixedArea captures the camera once, then never moves
    f32  fixedCenter[2];         // world x/z the rings stay centered on

    // editor brush cursor, pushed to the fragment shader for the surface highlight
    float3 brushPos;
    f32   brushRadius;            // 0 while inactive

    SDL_Thread*    workers[TERRAIN_MAX_WORKERS];
    u32            numWorkers;
    SDL_Semaphore* jobSemaphore;
    SDL_AtomicInt  workersQuit;

    u32 numDrawable;          // live chunks with indices
    u32 drawnLastFrame;

    SDL_GPUBuffer*           vertexBuffer;
    SDL_GPUBuffer*           indexBuffer;
    SDL_GPUTransferBuffer*   transferBuffer;
    SDL_GPUGraphicsPipeline* gbufferPipeline;
    SDL_GPUGraphicsPipeline* depthPipeline;
    SDL_GPUGraphicsPipeline* wirePipeline;
    Texture albedoLayers;
    Texture normalLayers;
    Texture armLayers;
} TerrainState;

static TerrainState g_Terrain;

// matches the vs_params cbuffer in Terrain.hlsl / TerrainDepthOnly.hlsl
typedef struct TerrainVSParams_
{
    mat4x4 viewProj;
    f32 chunkOriginSize[4];
    f32 cameraPosition[4];
    f32 cameraForward[4];
} TerrainVSParams;

static f32 TerrainChunkWorldSize(u32 lod)
{
    return (f32)TERRAIN_CHUNK_CELLS * TERRAIN_VOXEL_SIZE * (f32)(1u << lod);
}

static s32 TerrainFloorDiv(s32 a, s32 b)
{
    s32 q = a / b, r = a % b;
    return (r != 0 && ((r < 0) != (b < 0))) ? q - 1 : q;
}

static u64 TerrainChunkKey(s32 x, s32 y, s32 z, u32 lod)
{
    return ((u64)((u32)(x + 0x100000) & 0x1FFFFFu) << 40)
         | ((u64)((u32)(y + 0x8000)   & 0xFFFFu)   << 24)
         | ((u64)((u32)(z + 0x100000) & 0x1FFFFFu) << 3)
         | (u64)lod;
}

// ---------------------------------------------------------------------------------
// gpu heap
// ---------------------------------------------------------------------------------

static void TerrainHeapInit(TerrainHeap* heap, u32 capacity, u32 stride)
{
    (void)stride;
    heap->freeBlockCapacity = TERRAIN_MAX_CHUNKS + 1u;
    heap->freeBlocks = (TerrainHeapBlock*)AllocateTLSFGlobal(heap->freeBlockCapacity * sizeof(TerrainHeapBlock));
    heap->freeBlocks[0] = (TerrainHeapBlock){ 0u, capacity };
    heap->numFreeBlocks = 1u;
    heap->used = 0u;
}

static void TerrainHeapDestroy(TerrainHeap* heap)
{
    if (heap->freeBlocks) DeAllocateTLSFGlobal(heap->freeBlocks);
    SDL_memset(heap, 0, sizeof(*heap));
}

#define TERRAIN_HEAP_FAIL 0xFFFFFFFFu

static u32 TerrainHeapAlloc(TerrainHeap* heap, u32 count)
{
    if (count == 0u) return TERRAIN_HEAP_FAIL;
    for (u32 i = 0; i < heap->numFreeBlocks; i++)
    {
        TerrainHeapBlock* block = &heap->freeBlocks[i];
        if (block->count < count) continue;
        u32 offset = block->offset;
        block->offset += count;
        block->count -= count;
        if (block->count == 0u)
        {
            for (u32 j = i; j + 1u < heap->numFreeBlocks; j++) heap->freeBlocks[j] = heap->freeBlocks[j + 1u];
            heap->numFreeBlocks--;
        }
        heap->used += count;
        return offset;
    }
    return TERRAIN_HEAP_FAIL;
}

static void TerrainHeapFree(TerrainHeap* heap, u32 offset, u32 count)
{
    if (count == 0u) return;
    heap->used -= count;

    u32 i = 0;
    while (i < heap->numFreeBlocks && heap->freeBlocks[i].offset < offset) i++;

    bool mergePrev = i > 0u && heap->freeBlocks[i - 1u].offset + heap->freeBlocks[i - 1u].count == offset;
    bool mergeNext = i < heap->numFreeBlocks && offset + count == heap->freeBlocks[i].offset;
    if (mergePrev && mergeNext)
    {
        heap->freeBlocks[i - 1u].count += count + heap->freeBlocks[i].count;
        for (u32 j = i; j + 1u < heap->numFreeBlocks; j++) heap->freeBlocks[j] = heap->freeBlocks[j + 1u];
        heap->numFreeBlocks--;
    }
    else if (mergePrev) heap->freeBlocks[i - 1u].count += count;
    else if (mergeNext)
    {
        heap->freeBlocks[i].offset = offset;
        heap->freeBlocks[i].count += count;
    }
    else
    {
        ASSERT(heap->numFreeBlocks < heap->freeBlockCapacity);
        for (u32 j = heap->numFreeBlocks; j > i; j--) heap->freeBlocks[j] = heap->freeBlocks[j - 1u];
        heap->freeBlocks[i] = (TerrainHeapBlock){ offset, count };
        heap->numFreeBlocks++;
    }
}

// ---------------------------------------------------------------------------------
// chunk pool
// ---------------------------------------------------------------------------------

static u32 TerrainChunkAlloc(void)
{
    if (g_Terrain.numFreeSlots == 0u) return TERRAIN_HEAP_FAIL;
    return g_Terrain.freeSlots[--g_Terrain.numFreeSlots];
}

static void TerrainChunkRelease(u32 slot)
{
    g_Terrain.chunks[slot].used = 0;
    g_Terrain.freeSlots[g_Terrain.numFreeSlots++] = slot;
}

static void TerrainChunkFreeMesh(TerrainChunk* chunk)
{
    if (chunk->numVertices) TerrainHeapFree(&g_Terrain.vertexHeap, chunk->vertexOffset, chunk->numVertices);
    if (chunk->numIndices)  TerrainHeapFree(&g_Terrain.indexHeap, chunk->indexOffset, chunk->numIndices);
    if (chunk->numIndices && g_Terrain.numDrawable) g_Terrain.numDrawable--;
    chunk->numVertices = chunk->numIndices = 0u;
    SDL_free(chunk->cpuVertices); chunk->cpuVertices = NULL;
    SDL_free(chunk->cpuIndices);  chunk->cpuIndices = NULL;
}

static void TerrainPendingPush(u32 slot)
{
    if (g_Terrain.numPending < TERRAIN_PENDING_CAP)
        g_Terrain.pending[g_Terrain.numPending++] = slot;
    else // a dropped chunk would never mesh, the desired walk has no re-push for queued chunks
        AX_WARN("terrain pending queue full, chunk (%d,%d,%d) lod %u stuck",
                g_Terrain.chunks[slot].x, g_Terrain.chunks[slot].y,
                g_Terrain.chunks[slot].z, g_Terrain.chunks[slot].lod);
}

static void TerrainChunkEvict(u32 slot)
{
    TerrainChunk* chunk = &g_Terrain.chunks[slot];
    HMErase(&g_Terrain.chunkMap, TerrainChunkKey(chunk->x, chunk->y, chunk->z, chunk->lod));
    TerrainChunkFreeMesh(chunk);
    if (chunk->jobSlot >= 0)
    {
        SDL_SetAtomicInt(&g_Terrain.jobs[chunk->jobSlot].cancel, 1);
        chunk->dying = 1; // slot is released when the job is consumed
        return;
    }
    TerrainChunkRelease(slot);
}

// finds a chunk slot in the map, out: chunk pointer or NULL
static TerrainChunk* TerrainFindChunk(s32 x, s32 y, s32 z, u32 lod)
{
    u32* found = (u32*)HMFind(&g_Terrain.chunkMap, TerrainChunkKey(x, y, z, lod));
    return found ? &g_Terrain.chunks[*found] : NULL;
}

// lod changes are not always one level: at ring corners (or under fast movement) a
// retiring chunk's volume gets replaced by a mix of levels, so coverage checks have
// to walk the whole hierarchy instead of only the direct parent/children

// any retiring chunk inside this volume? checked downward through all finer levels
static bool TerrainAnyRetiringDescendant(s32 x, s32 y, s32 z, u32 lod)
{
    if (lod == 0) return false;
    for (u32 i = 0; i < 8u; i++)
    {
        s32 cx = x * 2 + (s32)(i & 1u);
        s32 cy = y * 2 + (s32)((i >> 1) & 1u);
        s32 cz = z * 2 + (s32)((i >> 2) & 1u);
        TerrainChunk* chunk = TerrainFindChunk(cx, cy, cz, lod - 1u);
        if (chunk && chunk->used && chunk->retiring) return true;
        if (TerrainAnyRetiringDescendant(cx, cy, cz, lod - 1u)) return true;
    }
    return false;
}

// true while any retiring chunk still covers this chunk's volume, the chunk stays
// hidden until the whole swap group can switch in the same frame
static bool TerrainCoveredByRetiring(const TerrainChunk* chunk)
{
    s32 px = chunk->x, py = chunk->y, pz = chunk->z;
    for (u32 l = chunk->lod + 1u; l < TERRAIN_LOD_COUNT; l++)
    {
        px >>= 1; py >>= 1; pz >>= 1;
        TerrainChunk* ancestor = TerrainFindChunk(px, py, pz, l);
        if (ancestor && ancestor->used && ancestor->retiring) return true;
    }
    return TerrainAnyRetiringDescendant(chunk->x, chunk->y, chunk->z, chunk->lod);
}

// is this volume fully covered by live (non retiring) chunks at any level? a missing
// octant whose location has no chunk at any level is abandoned space and counts covered
static bool TerrainRegionReady(s32 x, s32 y, s32 z, u32 lod)
{
    TerrainChunk* chunk = TerrainFindChunk(x, y, z, lod);
    if (chunk && chunk->used && !chunk->retiring)
        return chunk->state == ChunkState_Live;

    s32 px = x, py = y, pz = z;
    for (u32 l = lod + 1u; l < TERRAIN_LOD_COUNT; l++)
    {
        px >>= 1; py >>= 1; pz >>= 1;
        TerrainChunk* ancestor = TerrainFindChunk(px, py, pz, l);
        if (ancestor && ancestor->used && !ancestor->retiring)
            return ancestor->state == ChunkState_Live;
    }
    if (lod == 0) return true; // no chunk wants this volume on any level

    for (u32 i = 0; i < 8u; i++)
    {
        if (!TerrainRegionReady(x * 2 + (s32)(i & 1u),
                                y * 2 + (s32)((i >> 1) & 1u),
                                z * 2 + (s32)((i >> 2) & 1u), lod - 1u))
            return false;
    }
    return true;
}

// a retiring chunk may be removed once its whole volume is covered again
static bool TerrainReplacementsReady(const TerrainChunk* chunk)
{
    return TerrainRegionReady(chunk->x, chunk->y, chunk->z, chunk->lod);
}

// ---------------------------------------------------------------------------------
// desired set: per lod boxes snapped to the parent grid
// ---------------------------------------------------------------------------------

static bool TerrainBoxContains(const TerrainBox* box, s32 x, s32 y, s32 z)
{
    return x >= box->min[0] && x <= box->max[0]
        && y >= box->min[1] && y <= box->max[1]
        && z >= box->min[2] && z <= box->max[2];
}

// child region of level L in level L units (the snapping makes the division exact)
static TerrainBox TerrainChildBox(u32 level)
{
    const TerrainBox* child = &g_Terrain.levelBox[level - 1u];
    TerrainBox box;
    for (s32 a = 0; a < 3; a++)
    {
        box.min[a] = child->min[a] >> 1;
        box.max[a] = child->max[a] >> 1;
    }
    return box;
}

static u8 TerrainComputeTransitionMask(u32 level, s32 x, s32 y, s32 z)
{
    if (level + 1u >= TERRAIN_LOD_COUNT) return 0;
    static const s32 faceDir[6][3] = {
        { -1, 0, 0 }, { 1, 0, 0 }, { 0, -1, 0 }, { 0, 1, 0 }, { 0, 0, -1 }, { 0, 0, 1 }
    };
    const TerrainBox* box = &g_Terrain.levelBox[level];
    u8 mask = 0;
    for (s32 f = 0; f < 6; f++)
    {
        if (!TerrainBoxContains(box, x + faceDir[f][0], y + faceDir[f][1], z + faceDir[f][2]))
            mask |= (u8)(1 << f);
    }
    return mask;
}

static void TerrainDesireChunk(u32 level, s32 x, s32 y, s32 z)
{
    u64 key = TerrainChunkKey(x, y, z, level);
    u8 mask = TerrainComputeTransitionMask(level, x, y, z);

    u32* found = (u32*)HMFind(&g_Terrain.chunkMap, key);
    if (found)
    {
        TerrainChunk* chunk = &g_Terrain.chunks[*found];
        chunk->gen = g_Terrain.gen;
        chunk->retiring = 0; // came back into the desired set, keep the resident mesh
        if (chunk->transitionMask != mask)
        {
            chunk->transitionMask = mask;
            // empty chunks have no geometry, the mask only matters for meshes
            if (chunk->state == ChunkState_Live && !chunk->empty && !chunk->needRemesh && chunk->jobSlot < 0)
            {
                chunk->needRemesh = 1;
                TerrainPendingPush(*found);
            }
        }
        return;
    }

    u32 slot = TerrainChunkAlloc();
    if (slot == TERRAIN_HEAP_FAIL) { AX_WARN("terrain chunk pool exhausted"); return; }
    TerrainChunk* chunk = &g_Terrain.chunks[slot];
    SDL_memset(chunk, 0, sizeof(*chunk));
    chunk->x = x; chunk->y = y; chunk->z = z;
    chunk->lod = (u8)level;
    chunk->transitionMask = mask;
    chunk->state = ChunkState_Queued;
    chunk->jobSlot = -1;
    chunk->gen = g_Terrain.gen;
    chunk->used = 1;
    HMInsert(&g_Terrain.chunkMap, key, &slot);
    TerrainPendingPush(slot);
}

static void TerrainComputeDesired(const Camera* camera)
{
    g_Terrain.gen++;

    f32 bandMin, bandMax;
    TerrainDensity_GetYRange(&bandMin, &bandMax);

    // fixed area worlds keep their rings where they were created instead of
    // following the camera
    f32 centerX = camera->position.x, centerZ = camera->position.z;
    if (g_Terrain.genParams.fixedArea && g_Terrain.fixedCenterValid)
    {
        centerX = g_Terrain.fixedCenter[0];
        centerZ = g_Terrain.fixedCenter[1];
    }

    // the editor lod factor scales how far every detail ring reaches, the pool warns
    // and degrades gracefully if a huge factor exhausts the chunk budget
    s32 radius = Clamps32((s32)((f32)TERRAIN_RING_RADIUS * g_Terrain.lodFactor + 0.5f), 1, 4);
    for (u32 level = 0; level < TERRAIN_LOD_COUNT; level++)
    {
        f32 size = TerrainChunkWorldSize(level);
        s32 camX = (s32)Floorf32(centerX / size);
        s32 camZ = (s32)Floorf32(centerZ / size);
        TerrainBox* box = &g_Terrain.levelBox[level];
        box->min[0] = (camX - radius) & ~1;
        box->max[0] = (camX + radius) | 1;
        box->min[2] = (camZ - radius) & ~1;
        box->max[2] = (camZ + radius) | 1;
        box->min[1] = ((s32)Floorf32(bandMin / size)) & ~1;
        box->max[1] = ((s32)Floorf32(bandMax / size)) | 1;
    }

    for (u32 level = 0; level < TERRAIN_LOD_COUNT; level++)
    {
        const TerrainBox* box = &g_Terrain.levelBox[level];
        TerrainBox child = { 0 };
        if (level > 0) child = TerrainChildBox(level);
        for (s32 z = box->min[2]; z <= box->max[2]; z++)
        for (s32 y = box->min[1]; y <= box->max[1]; y++)
        for (s32 x = box->min[0]; x <= box->max[0]; x++)
        {
            if (level > 0 && TerrainBoxContains(&child, x, y, z)) continue;
            TerrainDesireChunk(level, x, y, z);
        }
    }

    // chunks that fell out of the desired set: visible geometry keeps drawing as
    // retiring until its replacement lod coverage is fully live (no holes/pops),
    // everything invisible goes away immediately
    for (u32 i = 0; i < TERRAIN_MAX_CHUNKS; i++)
    {
        TerrainChunk* chunk = &g_Terrain.chunks[i];
        if (!chunk->used || chunk->dying || chunk->gen == g_Terrain.gen) continue;
        if (chunk->state == ChunkState_Live && !chunk->hidden && chunk->numIndices > 0u)
            chunk->retiring = 1;
        else
            TerrainChunkEvict(i);
    }
}

// resolves pending split/merge swaps: retiring chunks leave the moment their
// replacements are all live, and the replacements unhide in the same frame
static void TerrainResolveRetiring(void)
{
    for (u32 i = 0; i < TERRAIN_MAX_CHUNKS; i++)
    {
        TerrainChunk* chunk = &g_Terrain.chunks[i];
        if (!chunk->used || !chunk->retiring || chunk->dying) continue;
        if (TerrainReplacementsReady(chunk))
            TerrainChunkEvict(i);
    }
    for (u32 i = 0; i < TERRAIN_MAX_CHUNKS; i++)
    {
        TerrainChunk* chunk = &g_Terrain.chunks[i];
        if (!chunk->used || !chunk->hidden) continue;
        if (!TerrainCoveredByRetiring(chunk))
            chunk->hidden = 0;
    }
}

// ---------------------------------------------------------------------------------
// worker jobs
// ---------------------------------------------------------------------------------

static void TerrainJobRun(TerrainJob* job)
{
    job->result = 1;
    job->empty = 0;

    if (SDL_GetAtomicInt(&job->cancel))
    {
        SDL_SetAtomicInt(&job->state, JobState_Done);
        return;
    }

    TerrainDensity_SampleChunk(job->x, job->y, job->z, job->lod, job->density);

    if (!SDL_GetAtomicInt(&job->cancel))
    {
        bool firstInside = job->density[0] < 0;
        bool mixed = false;
        for (s32 i = 1; i < TERRAIN_SAMPLES_TOTAL; i++)
        {
            if ((job->density[i] < 0) != firstInside) { mixed = true; break; }
        }
        if (!mixed)
            job->empty = 1;
        else
            job->result = Transvoxel_MeshChunk(job->density, job->lod, job->transitionMask,
                                               job->x, job->y, job->z,
                                               &job->mesh, &job->scratch);
    }

    SDL_SetAtomicInt(&job->state, JobState_Done);
}

// persistent pool workers: wake on the semaphore, grab a queued job slot with a CAS
static int TerrainWorkerMain(void* data)
{
    (void)data;
    for (;;)
    {
        SDL_WaitSemaphore(g_Terrain.jobSemaphore);
        if (SDL_GetAtomicInt(&g_Terrain.workersQuit)) return 0;
        for (u32 i = 0; i < TERRAIN_MAX_JOBS; i++)
        {
            TerrainJob* job = &g_Terrain.jobs[i];
            if (SDL_CompareAndSwapAtomicInt(&job->state, JobState_Queued, JobState_Running))
            {
                TerrainJobRun(job);
                break;
            }
        }
    }
}

static void TerrainDispatchJobs(const Camera* camera)
{
    for (u32 slot = 0; slot < TERRAIN_MAX_JOBS; slot++)
    {
        TerrainJob* job = &g_Terrain.jobs[slot];
        if (SDL_GetAtomicInt(&job->state) != JobState_Free) continue;

        // pick the best pending chunk: nearest first, chunks inside the view frustum
        // are strongly preferred so visible holes fill before anything offscreen
        u32 bestIdx = TERRAIN_HEAP_FAIL;
        f32 bestScore = 0.0f;
        u32 i = 0;
        while (i < g_Terrain.numPending)
        {
            u32 chunkSlot = g_Terrain.pending[i];
            TerrainChunk* chunk = &g_Terrain.chunks[chunkSlot];
            bool wantsJob = chunk->used && !chunk->dying && !chunk->retiring && chunk->jobSlot < 0 &&
                            (chunk->state == ChunkState_Queued || chunk->needRemesh);
            if (!wantsJob)
            {
                g_Terrain.pending[i] = g_Terrain.pending[--g_Terrain.numPending];
                continue;
            }
            f32 size = TerrainChunkWorldSize(chunk->lod);
            f32 ox = (f32)chunk->x * size, oy = (f32)chunk->y * size, oz = (f32)chunk->z * size;
            f32 dx = ox + size * 0.5f - camera->position.x;
            f32 dy = oy + size * 0.5f - camera->position.y;
            f32 dz = oz + size * 0.5f - camera->position.z;
            f32 score = dx * dx + dy * dy + dz * dz;
            if (g_Terrain.frustumValid)
            {
                v128f aabbMin = VecSetR(ox, oy, oz, 0.0f);
                v128f aabbMax = VecSetR(ox + size, oy + size, oz + size, 0.0f);
                // CheckAABBCulled returns true when the box intersects the frustum
                if (!CheckAABBCulled(aabbMin, aabbMax, g_Terrain.cameraFrustum.planes))
                    score *= 16.0f; // offscreen, fill visible terrain first
            }
            if (bestIdx == TERRAIN_HEAP_FAIL || score < bestScore) { bestIdx = i; bestScore = score; }
            i++;
        }
        if (bestIdx == TERRAIN_HEAP_FAIL) return;

        u32 chunkSlot = g_Terrain.pending[bestIdx];
        g_Terrain.pending[bestIdx] = g_Terrain.pending[--g_Terrain.numPending];
        TerrainChunk* chunk = &g_Terrain.chunks[chunkSlot];

        job->x = chunk->x; job->y = chunk->y; job->z = chunk->z;
        job->lod = chunk->lod;
        job->transitionMask = chunk->transitionMask;
        job->chunkSlot = chunkSlot;
        SDL_SetAtomicInt(&job->cancel, 0);

        chunk->needRemesh = 0;
        chunk->jobSlot = (s32)slot;
        if (chunk->state == ChunkState_Queued) chunk->state = ChunkState_Generating;

        SDL_SetAtomicInt(&job->state, JobState_Queued);
        SDL_SignalSemaphore(g_Terrain.jobSemaphore);
    }
}

// ---------------------------------------------------------------------------------
// upload: consumes finished jobs, called from Render() before any render pass
// ---------------------------------------------------------------------------------

typedef struct TerrainCopyRegion_
{
    SDL_GPUBuffer* dst;
    u32 transferOffset;
    u32 dstOffset;
    u32 size;
} TerrainCopyRegion;

void Terrain_GPUFlush(SDL_GPUCommandBuffer* cmd)
{
    if (!g_Terrain.initialized) return;

    TerrainCopyRegion regions[TERRAIN_MAX_JOBS * 2u];
    u32 numRegions = 0;
    u32 cursor = 0;
    u8* mapped = NULL;

    for (u32 slot = 0; slot < TERRAIN_MAX_JOBS; slot++)
    {
        TerrainJob* job = &g_Terrain.jobs[slot];
        if (SDL_GetAtomicInt(&job->state) != JobState_Done) continue;

        TerrainChunk* chunk = &g_Terrain.chunks[job->chunkSlot];
        bool owns = chunk->used && chunk->jobSlot == (s32)slot;

        if (!owns)
        {
            SDL_SetAtomicInt(&job->state, JobState_Free);
            continue;
        }
        if (chunk->dying)
        {
            chunk->jobSlot = -1;
            TerrainChunkRelease(job->chunkSlot);
            SDL_SetAtomicInt(&job->state, JobState_Free);
            continue;
        }
        if (SDL_GetAtomicInt(&job->cancel) || job->result == 0)
        {
            // cancelled but still desired, or mesher out of memory: try again
            chunk->jobSlot = -1;
            chunk->state = ChunkState_Queued;
            TerrainPendingPush(job->chunkSlot);
            SDL_SetAtomicInt(&job->state, JobState_Free);
            continue;
        }

        if (job->empty || job->mesh.numVertices == 0u || job->mesh.numIndices == 0u)
        {
            chunk->jobSlot = -1;
            TerrainChunkFreeMesh(chunk);
            chunk->empty = 1;
            chunk->hidden = 0;
            chunk->appliedMask = job->transitionMask;
            chunk->state = ChunkState_Live;
            SDL_SetAtomicInt(&job->state, JobState_Free);
            continue;
        }

        u32 vertexBytes = job->mesh.numVertices * (u32)sizeof(TerrainVertex);
        u32 indexBytes  = job->mesh.numIndices * (u32)sizeof(u32);
        if (cursor + vertexBytes + indexBytes > TERRAIN_TRANSFER_BYTES)
            break; // out of staging space, the job stays done and retries next frame

        u32 vertexOffset = TerrainHeapAlloc(&g_Terrain.vertexHeap, job->mesh.numVertices);
        u32 indexOffset  = vertexOffset == TERRAIN_HEAP_FAIL ? TERRAIN_HEAP_FAIL
                         : TerrainHeapAlloc(&g_Terrain.indexHeap, job->mesh.numIndices);
        if (indexOffset == TERRAIN_HEAP_FAIL)
        {
            if (vertexOffset != TERRAIN_HEAP_FAIL)
                TerrainHeapFree(&g_Terrain.vertexHeap, vertexOffset, job->mesh.numVertices);
            AX_WARN("terrain geometry heap full, chunk dropped to retry");
            chunk->jobSlot = -1;
            chunk->state = ChunkState_Queued;
            TerrainPendingPush(job->chunkSlot);
            SDL_SetAtomicInt(&job->state, JobState_Free);
            continue;
        }

        if (!mapped)
        {
            mapped = (u8*)SDL_MapGPUTransferBuffer(g_GPUDevice, g_Terrain.transferBuffer, true);
            if (!mapped) { AX_WARN("terrain transfer map failed: %s", SDL_GetError()); break; }
        }

        MemCopy(mapped + cursor, job->mesh.vertices, vertexBytes);
        regions[numRegions++] = (TerrainCopyRegion){
            g_Terrain.vertexBuffer, cursor, vertexOffset * (u32)sizeof(TerrainVertex), vertexBytes };
        cursor += vertexBytes;

        MemCopy(mapped + cursor, job->mesh.indices, indexBytes);
        regions[numRegions++] = (TerrainCopyRegion){
            g_Terrain.indexBuffer, cursor, indexOffset * (u32)sizeof(u32), indexBytes };
        cursor += indexBytes;

        // swap the resident mesh, the freed range can be reused immediately: the copy
        // pass lands before this frame's draws and the draws use the new offsets
        bool wasVisible = chunk->numIndices > 0u;
        TerrainChunkFreeMesh(chunk);
        chunk->vertexOffset = vertexOffset;
        chunk->numVertices  = job->mesh.numVertices;
        chunk->indexOffset  = indexOffset;
        chunk->numIndices   = job->mesh.numIndices;
        if (chunk->lod <= TERRAIN_RAYCAST_KEEP_LOD)
        {
            chunk->cpuVertices = (TerrainVertex*)SDL_malloc(vertexBytes);
            chunk->cpuIndices  = (u32*)SDL_malloc(indexBytes);
            if (chunk->cpuVertices) MemCopy(chunk->cpuVertices, job->mesh.vertices, vertexBytes);
            if (chunk->cpuIndices)  MemCopy(chunk->cpuIndices, job->mesh.indices, indexBytes);
        }
        chunk->empty = 0;
        chunk->appliedMask = job->transitionMask;
        chunk->state = ChunkState_Live;
        chunk->jobSlot = -1;
        // fresh split/merge replacements stay hidden until the whole group is live,
        // remeshes of an already visible chunk swap in place
        if (!wasVisible) chunk->hidden = TerrainCoveredByRetiring(chunk);
        g_Terrain.numDrawable++;

        f32 size = TerrainChunkWorldSize(chunk->lod);
        float3 origin = { (f32)chunk->x * size, (f32)chunk->y * size, (f32)chunk->z * size };
        chunk->aabbMin = F3Add(origin, job->mesh.aabbMin);
        chunk->aabbMax = F3Add(origin, job->mesh.aabbMax);

        // the desired mask moved on or a brush edit landed while the worker was
        // running: remesh with fresh data
        if (chunk->transitionMask != chunk->appliedMask || chunk->needRemesh)
        {
            chunk->needRemesh = 1;
            TerrainPendingPush(job->chunkSlot);
        }
        SDL_SetAtomicInt(&job->state, JobState_Free);
    }

    if (mapped)
    {
        SDL_UnmapGPUTransferBuffer(g_GPUDevice, g_Terrain.transferBuffer);
        SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(cmd);
        for (u32 i = 0; i < numRegions; i++)
        {
            SDL_GPUTransferBufferLocation src = { g_Terrain.transferBuffer, regions[i].transferOffset };
            SDL_GPUBufferRegion dst = { regions[i].dst, regions[i].dstOffset, regions[i].size };
            SDL_UploadToGPUBuffer(pass, &src, &dst, false);
        }
        SDL_EndGPUCopyPass(pass);
    }
}

// ---------------------------------------------------------------------------------
// raycast: cpu mesh copies of the drawn chunk set, used by gameplay and the editor
// ---------------------------------------------------------------------------------

static v128f TerrainDecodePosition(const TerrainVertex* v, v128f chunkOrigin, f32 metersPerStep)
{
    u32 qx = v->posA & 0x1FFFFFu;
    u32 qy = (v->posA >> 21) | ((v->posB & 0x3FFu) << 11);
    u32 qz = (v->posB >> 10) & 0x1FFFFFu;
    v128f local = VecSetR((f32)qx, (f32)qy, (f32)qz, 0.0f);
    return VecAdd(chunkOrigin, VecMulf(local, metersPerStep));
}

s32 Terrain_Raycast(float3 origin, float3 dir, f32 maxDist, u32 maxLod, BVHHit* hit)
{
    if (!g_Terrain.initialized || !g_Terrain.enabled) return 0;

    v128f rayOrigin = VecSetR(origin.x, origin.y, origin.z, 0.0f);
    v128f rayDir    = VecSetR(dir.x, dir.y, dir.z, 0.0f);
    v128f invDir    = VecDiv(VecOne(), rayDir);

    hit->hit.t = maxDist;
    bool anyHit = false;

    for (u32 i = 0; i < TERRAIN_MAX_CHUNKS; i++)
    {
        TerrainChunk* chunk = &g_Terrain.chunks[i];
        // exactly the drawn set: live with geometry, hidden swaps excluded
        if (!chunk->used || chunk->state != ChunkState_Live || chunk->numIndices == 0u || chunk->hidden) continue;
        if (chunk->lod > maxLod) continue;
        if (!chunk->cpuVertices || !chunk->cpuIndices) continue;

        v128f bmin = Vec3Load(&chunk->aabbMin.x);
        v128f bmax = Vec3Load(&chunk->aabbMax.x);
        if (!IntersectAABB(rayOrigin, invDir, bmin, bmax, hit->hit.t)) continue;

        f32 size = TerrainChunkWorldSize(chunk->lod);
        v128f chunkOrigin = VecSetR((f32)chunk->x * size, (f32)chunk->y * size, (f32)chunk->z * size, 0.0f);
        f32 metersPerStep = size / (f32)TERRAIN_POS_MAX;

        for (u32 t = 0; t + 2 < chunk->numIndices; t += 3)
        {
            v128f v0 = TerrainDecodePosition(&chunk->cpuVertices[chunk->cpuIndices[t + 0]], chunkOrigin, metersPerStep);
            v128f v1 = TerrainDecodePosition(&chunk->cpuVertices[chunk->cpuIndices[t + 1]], chunkOrigin, metersPerStep);
            v128f v2 = TerrainDecodePosition(&chunk->cpuVertices[chunk->cpuIndices[t + 2]], chunkOrigin, metersPerStep);
            if (IntersectTriangle(rayOrigin, rayDir, v0, v1, v2, &hit->hit))
            {
                anyHit = true;
                hit->triIndex   = t / 3u;
                hit->entityIdx  = i;            // terrain chunk slot
                hit->groupIdx   = chunk->lod;
                hit->skinnedSet = 0xFFFFFFFFu;
                hit->bundleIdx  = 0xFFFFFFFFu;
            }
        }
    }

    if (!anyHit) return 0;
    return 1;
}

// ---------------------------------------------------------------------------------
// rendering
// ---------------------------------------------------------------------------------

bool Terrain_HasDraws(void)
{
    return g_Terrain.initialized && g_Terrain.enabled && g_Terrain.numDrawable > 0u;
}

static void TerrainFillVSParams(TerrainVSParams* params, const TerrainChunk* chunk, mat4x4 viewProj)
{
    f32 size = TerrainChunkWorldSize(chunk->lod);
    params->viewProj = viewProj;
    params->chunkOriginSize[0] = (f32)chunk->x * size;
    params->chunkOriginSize[1] = (f32)chunk->y * size;
    params->chunkOriginSize[2] = (f32)chunk->z * size;
    params->chunkOriginSize[3] = size;
    params->cameraPosition[0] = g_Camera.position.x;
    params->cameraPosition[1] = g_Camera.position.y;
    params->cameraPosition[2] = g_Camera.position.z;
    params->cameraPosition[3] = 0.0f;
    params->cameraForward[0] = g_Camera.Front.x;
    params->cameraForward[1] = g_Camera.Front.y;
    params->cameraForward[2] = g_Camera.Front.z;
    params->cameraForward[3] = 0.0f;
}

static u32 TerrainDrawChunks(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass, mat4x4 viewProj)
{
    FrustumPlanes frustum = CreateFrustumPlanes(viewProj);
    u32 drawn = 0;
    for (u32 i = 0; i < TERRAIN_MAX_CHUNKS; i++)
    {
        TerrainChunk* chunk = &g_Terrain.chunks[i];
        if (!chunk->used || chunk->state != ChunkState_Live || chunk->numIndices == 0u || chunk->hidden) continue;

        v128f aabbMin = Vec3Load(&chunk->aabbMin.x);
        v128f aabbMax = Vec3Load(&chunk->aabbMax.x);
        if (!CheckAABBCulled(aabbMin, aabbMax, frustum.planes)) continue;

        TerrainVSParams params;
        TerrainFillVSParams(&params, chunk, viewProj);
        SDL_PushGPUVertexUniformData(cmd, 0, &params, sizeof(params));
        SDL_DrawGPUIndexedPrimitives(pass, chunk->numIndices, 1, chunk->indexOffset, (s32)chunk->vertexOffset, 0);
        drawn++;
    }
    return drawn;
}

void Terrain_RenderDepth(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass, mat4x4 viewProj)
{
    if (!Terrain_HasDraws()) return;

    SDL_BindGPUGraphicsPipeline(pass, g_Terrain.depthPipeline);
    SDL_GPUBufferBinding vertexBinding = { g_Terrain.vertexBuffer, 0 };
    SDL_GPUBufferBinding indexBinding  = { g_Terrain.indexBuffer, 0 };
    SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
    SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    TerrainDrawChunks(cmd, pass, viewProj);
}

void Terrain_RenderGBuffer(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass, mat4x4 viewProj)
{
    if (!Terrain_HasDraws()) return;
    if (!g_WindowState.tex_shadow_color || !g_RenderState.shadowCascadeBuffer) return;

    SDL_BindGPUGraphicsPipeline(pass, g_Terrain.gbufferPipeline);
    SDL_GPUBufferBinding vertexBinding = { g_Terrain.vertexBuffer, 0 };
    SDL_GPUBufferBinding indexBinding  = { g_Terrain.indexBuffer, 0 };
    SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
    SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);

    SDL_GPUBuffer* vertexStorage[1] = { g_RenderState.shadowCascadeBuffer };
    SDL_BindGPUVertexStorageBuffers(pass, 0, vertexStorage, 1);

    SDL_GPUTextureSamplerBinding samplers[4] = {
        { .texture = g_Terrain.albedoLayers.handle, .sampler = g_RenderState.sampler },
        { .texture = g_Terrain.normalLayers.handle, .sampler = g_RenderState.sampler },
        { .texture = g_Terrain.armLayers.handle,    .sampler = g_RenderState.sampler },
        { .texture = g_WindowState.tex_shadow_color, .sampler = g_RenderState.shadowSampler }
    };
    SDL_BindGPUFragmentSamplers(pass, 0, samplers, SDL_arraysize(samplers));

    float3 sunDirection = GetRenderSunDirection();
    f32 fragmentParams[8] = {
        sunDirection.x, sunDirection.y, sunDirection.z, 0.0f,
        g_Terrain.brushPos.x, g_Terrain.brushPos.y, g_Terrain.brushPos.z, g_Terrain.brushRadius
    };
    SDL_PushGPUFragmentUniformData(cmd, 0, fragmentParams, sizeof(fragmentParams));

    g_Terrain.drawnLastFrame = TerrainDrawChunks(cmd, pass, viewProj);
}

// debug line overlay over the lit scene, toggled from the graphics settings panel
void Terrain_RenderWireframe(SDL_GPUCommandBuffer* cmd, SDL_GPUColorTargetInfo* colorTarget,
                             SDL_GPUDepthStencilTargetInfo* depthTarget, mat4x4 viewProj)
{
    if (!g_RenderSettings.terrainWireframe || !Terrain_HasDraws()) return;

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, colorTarget, 1, depthTarget);
    SDL_BindGPUGraphicsPipeline(pass, g_Terrain.wirePipeline);
    SDL_GPUBufferBinding vertexBinding = { g_Terrain.vertexBuffer, 0 };
    SDL_GPUBufferBinding indexBinding  = { g_Terrain.indexBuffer, 0 };
    SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
    SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    TerrainDrawChunks(cmd, pass, viewProj);
    SDL_EndGPURenderPass(pass);
}

// ---------------------------------------------------------------------------------
// init: pipelines, buffers, triplanar texture arrays
// ---------------------------------------------------------------------------------

static SDL_GPUShader* TerrainCreateShader(const u8* code, size_t codeSize, SDL_GPUShaderStage stage,
                                          const char* entry, u32 numUniforms, u32 numSamplers, u32 numStorage)
{
    SDL_GPUShader* shader = SDL_CreateGPUShader(g_GPUDevice, &(SDL_GPUShaderCreateInfo){
        .code = code, .code_size = codeSize,
        .format = AX_GPU_SHADER_FORMAT,
        .stage = stage,
        .entrypoint = entry,
        .num_uniform_buffers = numUniforms,
        .num_samplers = numSamplers,
        .num_storage_buffers = numStorage
    });
    CHECK_CREATE(shader, "Terrain Shader");
    return shader;
}

static void TerrainInitPipelines(void)
{
    const SDL_GPUVertexAttribute vertexAttributes[1] = {
        { .location = 0, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_UINT4, .offset = 0 }
    };
    SDL_GPUVertexInputState vertexInput = {
        .vertex_buffer_descriptions = &(SDL_GPUVertexBufferDescription){
            0, sizeof(TerrainVertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0
        },
        .num_vertex_buffers = 1,
        .vertex_attributes = vertexAttributes,
        .num_vertex_attributes = 1
    };

    // g-buffer pass, formats and depth state match InitSurfacePipeline
    {
        SDL_GPUShader* vert = TerrainCreateShader(Shaders_TerrainVert_spv, sizeof(Shaders_TerrainVert_spv),
                                                  SDL_GPU_SHADERSTAGE_VERTEX, "vert", 1, 0, 1);
        SDL_GPUShader* frag = TerrainCreateShader(Shaders_TerrainFrag_spv, sizeof(Shaders_TerrainFrag_spv),
                                                  SDL_GPU_SHADERSTAGE_FRAGMENT, "frag", 1, 4, 0);
        const SDL_GPUColorTargetDescription gbufferTargets[3] = {
            { .format = SDL_GPU_TEXTUREFORMAT_R32_UINT       },
            { .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM },
            { .format = SDL_GPU_TEXTUREFORMAT_R8G8_UNORM     }
        };
        g_Terrain.gbufferPipeline = SDL_CreateGPUGraphicsPipeline(g_GPUDevice, &(SDL_GPUGraphicsPipelineCreateInfo){
            .vertex_shader   = vert,
            .fragment_shader = frag,
            .primitive_type  = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
            .target_info     = (SDL_GPUGraphicsPipelineTargetInfo){
                .num_color_targets         = 3,
                .color_target_descriptions = gbufferTargets,
                .depth_stencil_format      = SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
                .has_depth_stencil_target  = true
            },
            .depth_stencil_state = (SDL_GPUDepthStencilState){
                .enable_depth_test  = true,
                .enable_depth_write = false,
                .compare_op         = SDL_GPU_COMPAREOP_LESS_OR_EQUAL
            },
            .multisample_state  = (SDL_GPUMultisampleState){ .sample_count = SDL_GPU_SAMPLECOUNT_1 },
            .vertex_input_state = vertexInput
        });
        CHECK_CREATE(g_Terrain.gbufferPipeline, "Terrain GBuffer Pipeline");
        SDL_ReleaseGPUShader(g_GPUDevice, vert);
        SDL_ReleaseGPUShader(g_GPUDevice, frag);
    }

    // depth prepass, formats match InitDepthOnlyPipelines
    {
        SDL_GPUShader* vert = TerrainCreateShader(Shaders_TerrainDepthOnlyVert_spv, sizeof(Shaders_TerrainDepthOnlyVert_spv),
                                                  SDL_GPU_SHADERSTAGE_VERTEX, "vert", 1, 0, 0);
        SDL_GPUShader* frag = TerrainCreateShader(Shaders_TerrainDepthOnlyFrag_spv, sizeof(Shaders_TerrainDepthOnlyFrag_spv),
                                                  SDL_GPU_SHADERSTAGE_FRAGMENT, "frag", 0, 0, 0);
        g_Terrain.depthPipeline = SDL_CreateGPUGraphicsPipeline(g_GPUDevice, &(SDL_GPUGraphicsPipelineCreateInfo){
            .vertex_shader   = vert,
            .fragment_shader = frag,
            .primitive_type  = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
            .target_info     = (SDL_GPUGraphicsPipelineTargetInfo){
                .num_color_targets         = 1,
                .color_target_descriptions = &(SDL_GPUColorTargetDescription){ .format = SDL_GPU_TEXTUREFORMAT_R32_FLOAT },
                .depth_stencil_format      = SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
                .has_depth_stencil_target  = true
            },
            .depth_stencil_state = (SDL_GPUDepthStencilState){
                .enable_depth_test  = true,
                .enable_depth_write = true,
                .compare_op         = SDL_GPU_COMPAREOP_LESS_OR_EQUAL
            },
            .multisample_state  = (SDL_GPUMultisampleState){ .sample_count = SDL_GPU_SAMPLECOUNT_1 },
            .vertex_input_state = vertexInput
        });
        CHECK_CREATE(g_Terrain.depthPipeline, "Terrain Depth Pipeline");
        SDL_ReleaseGPUShader(g_GPUDevice, vert);
        SDL_ReleaseGPUShader(g_GPUDevice, frag);
    }

    // wireframe overlay: line fill over the lit hdr color target, depth tested against
    // the scene with a small bias so the lines win against their own triangles
    {
        SDL_GPUShader* vert = TerrainCreateShader(Shaders_TerrainDepthOnlyVert_spv, sizeof(Shaders_TerrainDepthOnlyVert_spv),
                                                  SDL_GPU_SHADERSTAGE_VERTEX, "vert", 1, 0, 0);
        SDL_GPUShader* frag = TerrainCreateShader(Shaders_TerrainWireFrag_spv, sizeof(Shaders_TerrainWireFrag_spv),
                                                  SDL_GPU_SHADERSTAGE_FRAGMENT, "wireFrag", 0, 0, 0);
        g_Terrain.wirePipeline = SDL_CreateGPUGraphicsPipeline(g_GPUDevice, &(SDL_GPUGraphicsPipelineCreateInfo){
            .vertex_shader   = vert,
            .fragment_shader = frag,
            .primitive_type  = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
            .target_info     = (SDL_GPUGraphicsPipelineTargetInfo){
                .num_color_targets         = 1,
                .color_target_descriptions = &(SDL_GPUColorTargetDescription){ .format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT },
                .depth_stencil_format      = SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
                .has_depth_stencil_target  = true
            },
            .rasterizer_state = (SDL_GPURasterizerState){
                .fill_mode = SDL_GPU_FILLMODE_LINE,
                .enable_depth_bias = true,
                .depth_bias_constant_factor = -1.0f,
                .depth_bias_slope_factor    = -1.0f
            },
            .depth_stencil_state = (SDL_GPUDepthStencilState){
                .enable_depth_test  = true,
                .enable_depth_write = false,
                .compare_op         = SDL_GPU_COMPAREOP_LESS_OR_EQUAL
            },
            .multisample_state  = (SDL_GPUMultisampleState){ .sample_count = SDL_GPU_SAMPLECOUNT_1 },
            .vertex_input_state = vertexInput
        });
        CHECK_CREATE(g_Terrain.wirePipeline, "Terrain Wireframe Pipeline");
        SDL_ReleaseGPUShader(g_GPUDevice, vert);
        SDL_ReleaseGPUShader(g_GPUDevice, frag);
    }
}

// loads three pngs into one rgba8 texture array layer by layer, resizing to the target
// size when needed. srgb only affects the resize filter, the formats stay unorm and the
// shader converts srgb to linear like the surface shader does
static Texture TerrainLoadLayerArray(const char* const paths[3], s32 size, bool srgb, const char* label)
{
    Texture tex = rCreateTexture2DArray(size, size, 3, NULL, TEX_FMT_8UNORM4,
                                        TexFlags_MipMap, TEX_SAMPLER | TEX_COLOR_TARGET, label);
    for (s32 layer = 0; layer < 3; layer++)
    {
        int w, h, channels;
        u8* image = stbi_load(paths[layer], &w, &h, &channels, 4);
        if (!image)
        {
            AX_ERROR("terrain texture missing: %s", paths[layer]);
            continue;
        }
        u8* upload = image;
        u8* resized = NULL;
        if (w != size || h != size)
        {
            resized = (u8*)SDL_malloc((size_t)size * size * 4u);
            if (srgb) stbir_resize_uint8_srgb(image, w, h, 0, resized, size, size, 0, STBIR_RGBA);
            else      stbir_resize_uint8_linear(image, w, h, 0, resized, size, size, 0, STBIR_RGBA);
            upload = resized;
        }
        UploadTextureRegion(tex, (u32)layer, 0, 0, (u32)size, (u32)size, (u32)size, (u32)size, upload);
        if (resized) SDL_free(resized);
        stbi_image_free(image);
    }
    GenerateTextureMips(tex);
    return tex;
}

static void TerrainInitTextures(void)
{
    static const char* const albedoPaths[3] = {
        "Assets/Textures/Terrain/brown_mud_leaves_01_diff_2k.png",
        "Assets/Textures/Terrain/rocky_terrain_02_diff_2k.png",
        "Assets/Textures/Terrain/rocky_terrain_diff_2k.png"
    };
    static const char* const normalPaths[3] = {
        "Assets/Textures/Terrain/brown_mud_leaves_01_nor_dx_1k.png",
        "Assets/Textures/Terrain/rocky_terrain_02_nor_dx_1k.png",
        "Assets/Textures/Terrain/rocky_terrain_nor_dx_1k.png"
    };
    static const char* const armPaths[3] = {
        "Assets/Textures/Terrain/brown_mud_leaves_01_arm_2k.png",
        "Assets/Textures/Terrain/rocky_terrain_02_arm_1k.png",
        "Assets/Textures/Terrain/rocky_terrain_arm_1k.png"
    };

    u64 start = SDL_GetTicks();
    g_Terrain.albedoLayers = TerrainLoadLayerArray(albedoPaths, TERRAIN_ALBEDO_SIZE, true, "TerrainAlbedo");
    g_Terrain.normalLayers = TerrainLoadLayerArray(normalPaths, TERRAIN_DETAIL_SIZE, false, "TerrainNormal");
    g_Terrain.armLayers    = TerrainLoadLayerArray(armPaths, TERRAIN_DETAIL_SIZE, false, "TerrainARM");
    AX_LOG("terrain textures loaded in %llu ms (png decode, consider baking)", (unsigned long long)(SDL_GetTicks() - start));
}

void Terrain_Init(void)
{
    if (g_Terrain.initialized) return;
    SDL_memset(&g_Terrain, 0, sizeof(g_Terrain));

    g_Terrain.genParams = Terrain_DefaultGenParams();
    TerrainDensity_SetParams(&g_Terrain.genParams);
    TerrainEdit_Init();
    // Transvoxel_SelfTest();

    g_Terrain.chunks    = (TerrainChunk*)AllocZeroTLSFGlobal(TERRAIN_MAX_CHUNKS, sizeof(TerrainChunk));
    g_Terrain.freeSlots = (u32*)AllocateTLSFGlobal(TERRAIN_MAX_CHUNKS * sizeof(u32));
    g_Terrain.pending   = (u32*)AllocateTLSFGlobal(TERRAIN_PENDING_CAP * sizeof(u32));
    for (u32 i = 0; i < TERRAIN_MAX_CHUNKS; i++)
        g_Terrain.freeSlots[i] = TERRAIN_MAX_CHUNKS - 1u - i;
    g_Terrain.numFreeSlots = TERRAIN_MAX_CHUNKS;
    g_Terrain.chunkMap = HMCreate(TERRAIN_MAX_CHUNKS, sizeof(u32));

    TerrainHeapInit(&g_Terrain.vertexHeap, TERRAIN_MAX_VERTICES, (u32)sizeof(TerrainVertex));
    TerrainHeapInit(&g_Terrain.indexHeap, TERRAIN_MAX_INDICES, (u32)sizeof(u32));

    for (u32 i = 0; i < TERRAIN_MAX_JOBS; i++)
    {
        TerrainJob* job = &g_Terrain.jobs[i];
        job->density = (s8*)SDL_malloc(TERRAIN_SAMPLES_TOTAL);
        Transvoxel_ScratchInit(&job->scratch);
        SDL_SetAtomicInt(&job->state, JobState_Free);
    }

    // persistent worker pool, sized to the machine but capped: terrain meshing should
    // never starve the rest of the engine
    SDL_SetAtomicInt(&g_Terrain.workersQuit, 0);
    g_Terrain.jobSemaphore = SDL_CreateSemaphore(0);
    CHECK_CREATE(g_Terrain.jobSemaphore, "Terrain Job Semaphore");
    s32 cores = SDL_GetNumLogicalCPUCores();
    g_Terrain.numWorkers = (u32)Clamps32(cores - 2, 2, TERRAIN_MAX_WORKERS);
    for (u32 i = 0; i < g_Terrain.numWorkers; i++)
    {
        g_Terrain.workers[i] = SDL_CreateThread(TerrainWorkerMain, "terrain_worker", NULL);
        CHECK_CREATE(g_Terrain.workers[i], "Terrain Worker Thread");
    }

    g_Terrain.vertexBuffer   = CreateBuffer(NULL, TERRAIN_MAX_VERTICES * sizeof(TerrainVertex), BVertexBit, "TerrainVertexBuffer");
    g_Terrain.indexBuffer    = CreateBuffer(NULL, TERRAIN_MAX_INDICES * sizeof(u32), SDL_GPU_BUFFERUSAGE_INDEX, "TerrainIndexBuffer");
    g_Terrain.transferBuffer = SDL_CreateGPUTransferBuffer(g_GPUDevice, &(SDL_GPUTransferBufferCreateInfo){
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size  = TERRAIN_TRANSFER_BYTES
    });
    CHECK_CREATE(g_Terrain.transferBuffer, "Terrain Transfer Buffer");

    TerrainInitPipelines();
    TerrainInitTextures();

    g_Terrain.enabled = true;
    g_Terrain.initialized = true;
}

void Terrain_Destroy(void)
{
    if (!g_Terrain.initialized) return;

    // stop the pool: workers drain their current job, then exit on the quit signal
    for (u32 i = 0; i < TERRAIN_MAX_JOBS; i++)
        SDL_SetAtomicInt(&g_Terrain.jobs[i].cancel, 1);
    SDL_SetAtomicInt(&g_Terrain.workersQuit, 1);
    for (u32 i = 0; i < g_Terrain.numWorkers; i++)
        SDL_SignalSemaphore(g_Terrain.jobSemaphore);
    for (u32 i = 0; i < g_Terrain.numWorkers; i++)
        SDL_WaitThread(g_Terrain.workers[i], NULL);
    SDL_DestroySemaphore(g_Terrain.jobSemaphore);

    for (u32 i = 0; i < TERRAIN_MAX_JOBS; i++)
    {
        TerrainJob* job = &g_Terrain.jobs[i];
        SDL_free(job->density);
        Transvoxel_ScratchDestroy(&job->scratch);
        Transvoxel_MeshOutDestroy(&job->mesh);
    }

    if (g_Terrain.gbufferPipeline) SDL_ReleaseGPUGraphicsPipeline(g_GPUDevice, g_Terrain.gbufferPipeline);
    if (g_Terrain.depthPipeline)   SDL_ReleaseGPUGraphicsPipeline(g_GPUDevice, g_Terrain.depthPipeline);
    if (g_Terrain.wirePipeline)    SDL_ReleaseGPUGraphicsPipeline(g_GPUDevice, g_Terrain.wirePipeline);
    if (g_Terrain.vertexBuffer)    SDL_ReleaseGPUBuffer(g_GPUDevice, g_Terrain.vertexBuffer);
    if (g_Terrain.indexBuffer)     SDL_ReleaseGPUBuffer(g_GPUDevice, g_Terrain.indexBuffer);
    if (g_Terrain.transferBuffer)  SDL_ReleaseGPUTransferBuffer(g_GPUDevice, g_Terrain.transferBuffer);
    ReleaseTexture(&g_Terrain.albedoLayers);
    ReleaseTexture(&g_Terrain.normalLayers);
    ReleaseTexture(&g_Terrain.armLayers);

    for (u32 i = 0; i < TERRAIN_MAX_CHUNKS; i++)
    {
        SDL_free(g_Terrain.chunks[i].cpuVertices);
        SDL_free(g_Terrain.chunks[i].cpuIndices);
    }

    HMDestroy(&g_Terrain.chunkMap);
    TerrainHeapDestroy(&g_Terrain.vertexHeap);
    TerrainHeapDestroy(&g_Terrain.indexHeap);
    DeAllocateTLSFGlobal(g_Terrain.chunks);
    DeAllocateTLSFGlobal(g_Terrain.freeSlots);
    DeAllocateTLSFGlobal(g_Terrain.pending);
    TerrainEdit_Destroy();
    SDL_memset(&g_Terrain, 0, sizeof(g_Terrain));
}

// ---------------------------------------------------------------------------------
// per frame update
// ---------------------------------------------------------------------------------

void Terrain_Update(const Camera* camera)
{
    if (!g_Terrain.initialized || !g_Terrain.enabled) return;

    g_Terrain.cameraFrustum = CreateFrustumPlanes(M44Multiply(camera->view, camera->projection));
    g_Terrain.frustumValid = true;

    if (g_Terrain.genParams.fixedArea && !g_Terrain.fixedCenterValid)
    {
        g_Terrain.fixedCenter[0] = camera->position.x;
        g_Terrain.fixedCenter[1] = camera->position.z;
        g_Terrain.fixedCenterValid = true;
    }

    f32 size0 = TerrainChunkWorldSize(0);
    s32 cam[3] = {
        (s32)Floorf32(camera->position.x / size0),
        (s32)Floorf32(camera->position.y / size0),
        (s32)Floorf32(camera->position.z / size0)
    };
    bool camMoved = cam[0] != g_Terrain.lastCamChunk[0] || cam[2] != g_Terrain.lastCamChunk[2];
    if (g_Terrain.genParams.fixedArea) camMoved = false; // rings stay put
    if (!g_Terrain.desiredValid || camMoved
        || g_RenderSettings.terrainLodFactor != g_Terrain.lodFactor)
    {
        g_Terrain.lastCamChunk[0] = cam[0];
        g_Terrain.lastCamChunk[1] = cam[1];
        g_Terrain.lastCamChunk[2] = cam[2];
        g_Terrain.lodFactor = g_RenderSettings.terrainLodFactor;
        g_Terrain.desiredValid = true;
        TerrainComputeDesired(camera);
    }

    TerrainResolveRetiring();
    TerrainDispatchJobs(camera);
}

void Terrain_SetEnabled(bool enabled) { g_Terrain.enabled = enabled; }
bool Terrain_GetEnabled(void)         { return g_Terrain.enabled; }

// frees every resident chunk. in flight worker jobs are marked dying and release
// when their result is consumed, nothing of theirs uploads
static void TerrainEvictAll(void)
{
    for (u32 i = 0; i < TERRAIN_MAX_CHUNKS; i++)
        if (g_Terrain.chunks[i].used && !g_Terrain.chunks[i].dying)
            TerrainChunkEvict(i);
    g_Terrain.numPending = 0;
    g_Terrain.desiredValid = false;
}

void Terrain_ApplyGenParams(const TerrainGenParams* params)
{
    g_Terrain.genParams = *params;
    g_Terrain.fixedCenterValid = false; // recapture at the next update
    // jobs sampling while the params swap produce torn results, but every live and
    // generating chunk is evicted right after, so nothing stale survives
    TerrainDensity_SetParams(params);
    TerrainEvictAll();
}

const TerrainGenParams* Terrain_GetGenParams(void)
{
    return &g_Terrain.genParams;
}

void Terrain_CreateWorld(const TerrainGenParams* params)
{
    Terrain_ApplyGenParams(params);
    g_Terrain.enabled = true;
}

void Terrain_DeleteWorld(void)
{
    g_Terrain.enabled = false;
    g_Terrain.brushRadius = 0.0f;
    TerrainEvictAll();
    TerrainEdit_Clear();
}

// ---------------------------------------------------------------------------------
// editor brush
// ---------------------------------------------------------------------------------

void Terrain_SetBrushCursor(float3 position, f32 radius, bool active)
{
    g_Terrain.brushPos = position;
    g_Terrain.brushRadius = active ? radius : 0.0f;
}

// queues a remesh for every chunk whose sample grid can see the edited region.
// the pad ring feeds gradients, so the box grows by two voxels of each chunk's lod
static void TerrainRemeshRegion(float3 mn, float3 mx)
{
    for (u32 i = 0; i < TERRAIN_MAX_CHUNKS; i++)
    {
        TerrainChunk* chunk = &g_Terrain.chunks[i];
        if (!chunk->used || chunk->dying || chunk->retiring) continue;
        f32 size = TerrainChunkWorldSize(chunk->lod);
        f32 pad = 2.0f * TERRAIN_VOXEL_SIZE * (f32)(1u << chunk->lod);
        f32 ox = (f32)chunk->x * size, oy = (f32)chunk->y * size, oz = (f32)chunk->z * size;
        if (mx.x < ox - pad || mn.x > ox + size + pad ||
            mx.y < oy - pad || mn.y > oy + size + pad ||
            mx.z < oz - pad || mn.z > oz + size + pad) continue;
        if (chunk->needRemesh) continue;
        chunk->needRemesh = 1;
        // queued chunks are already pending, generating ones requeue at consume
        if (chunk->jobSlot < 0 && chunk->state == ChunkState_Live)
            TerrainPendingPush(i);
    }
}

void Terrain_SculptSphere(float3 center, f32 radius, f32 strength, f32 softness)
{
    float3 mn, mx;
    TerrainEdit_SculptSphere(center, radius, strength, softness, &mn, &mx);
    TerrainRemeshRegion(mn, mx);
}

void Terrain_PaintSphere(float3 center, f32 radius, u32 layer, f32 strength, f32 softness)
{
    float3 mn, mx;
    TerrainEdit_PaintSphere(center, radius, (u8)Clamps32((s32)layer + 1, 1, 15), strength, softness, &mn, &mx);
    TerrainRemeshRegion(mn, mx);
}

// sphere-traces the density field (noise + sculpt edits) instead of the meshes, so
// it works at any distance regardless of which lods keep cpu copies. the hit is the
// analytic surface, within ~half a coarse cell of the rendered mesh
s32 Terrain_RaycastField(float3 origin, float3 dir, f32 maxDist, BVHHit* hit)
{
    if (!g_Terrain.initialized || !g_Terrain.enabled) return 0;

    f32 t = 0.0f, lastT = 0.0f;
    for (u32 step = 0; step < 256u && t < maxDist; step++)
    {
        f32 px = origin.x + dir.x * t, py = origin.y + dir.y * t, pz = origin.z + dir.z * t;
        f32 sdf = TerrainDensity_SDF(px, py, pz) +
                  TerrainEdit_DeltaAt((s32)Floorf32(px), (s32)Floorf32(py), (s32)Floorf32(pz));
        if (sdf < 0.0f)
        {
            // bisect between the last outside sample and this inside one
            f32 lo = lastT, hi = t;
            for (u32 i = 0; i < 16u; i++)
            {
                f32 mid = (lo + hi) * 0.5f;
                f32 mx = origin.x + dir.x * mid, my = origin.y + dir.y * mid, mz = origin.z + dir.z * mid;
                f32 d = TerrainDensity_SDF(mx, my, mz) +
                        TerrainEdit_DeltaAt((s32)Floorf32(mx), (s32)Floorf32(my), (s32)Floorf32(mz));
                if (d < 0.0f) hi = mid; else lo = mid;
            }
            hit->hit.t = lo;
            hit->hit.u = hit->hit.v = 0.0f;
            hit->triIndex   = 0u;
            hit->entityIdx  = 0xFFFFFFFFu;
            hit->groupIdx   = 0u;
            hit->skinnedSet = 0xFFFFFFFFu;
            hit->bundleIdx  = 0xFFFFFFFFu;
            return 1;
        }
        lastT = t;
        // the field is not a true distance (heightfield slopes exceed 1), step conservatively
        t += Maxf32(sdf * 0.5f, 0.3f);
    }
    return 0;
}

u32 Terrain_NumEditedRegions(void)                  { return TerrainEdit_NumChunks(); }
bool Terrain_SaveEditChunks(const char* path)       { return TerrainEdit_SaveChunks(path); }
bool Terrain_LoadEditChunks(const char* path)       { return TerrainEdit_LoadChunks(path); }

TerrainStats Terrain_GetStats(void)
{
    TerrainStats stats = {0};
    if (!g_Terrain.initialized) return stats;
    for (u32 i = 0; i < TERRAIN_MAX_CHUNKS; i++)
    {
        TerrainChunk* chunk = &g_Terrain.chunks[i];
        if (!chunk->used) continue;
        if (chunk->state == ChunkState_Live) { stats.liveChunks++; if (chunk->empty) stats.emptyChunks++; }
        else stats.queuedChunks++;
    }
    for (u32 i = 0; i < TERRAIN_MAX_JOBS; i++)
    {
        s32 jobState = SDL_GetAtomicInt((SDL_AtomicInt*)&g_Terrain.jobs[i].state);
        if (jobState == JobState_Queued || jobState == JobState_Running) stats.jobsInFlight++;
    }
    stats.drawnChunks = g_Terrain.drawnLastFrame;
    stats.numVertices = g_Terrain.vertexHeap.used;
    stats.numIndices  = g_Terrain.indexHeap.used;
    return stats;
}
