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

#include "Shaders/spv/TerrainVert.spv.h"
#include "Shaders/spv/TerrainFrag.spv.h"
#include "Shaders/spv/TerrainDepthOnlyVert.spv.h"
#include "Shaders/spv/TerrainDepthOnlyFrag.spv.h"
#include "Shaders/spv/TerrainWireFrag.spv.h"

#define TERRAIN_MAX_CHUNKS      2048u
#define TERRAIN_MAX_JOBS        16u
#define TERRAIN_MAX_WORKERS     8u
#define TERRAIN_RING_RADIUS     2
#define TERRAIN_MAX_VERTICES    (1u << 20)             // 16 MB of TerrainVertex
#define TERRAIN_MAX_INDICES     (4u << 20)             // 16 MB of u32
#define TERRAIN_TRANSFER_BYTES  (8u * 1024u * 1024u)
#define TERRAIN_PENDING_CAP     4096u
#define TERRAIN_DEBUG_HOLES     0    // 1: log a streaming audit + raycast hole probe every 240 frames
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
    f32 aabbMin[3], aabbMax[3]; // world space, tight bounds from the mesher
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

// first fit free list allocator over the gpu mega buffers, offsets in elements
typedef struct TerrainRange_ { u32 offset, count; } TerrainRange;

typedef struct TerrainHeap_
{
    TerrainRange* freeList; // sorted by offset
    u32 numFree, freeCapacity;
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

static void TerrainHeapInit(TerrainHeap* heap, u32 capacity)
{
    heap->freeCapacity = TERRAIN_MAX_CHUNKS + 1u;
    heap->freeList = (TerrainRange*)AllocateTLSFGlobal(heap->freeCapacity * sizeof(TerrainRange));
    heap->freeList[0] = (TerrainRange){ 0u, capacity };
    heap->numFree = 1u;
    heap->used = 0u;
}

static void TerrainHeapDestroy(TerrainHeap* heap)
{
    if (heap->freeList) DeAllocateTLSFGlobal(heap->freeList);
    SDL_memset(heap, 0, sizeof(*heap));
}

#define TERRAIN_HEAP_FAIL 0xFFFFFFFFu

static u32 TerrainHeapAllocRange(TerrainHeap* heap, u32 count)
{
    for (u32 i = 0; i < heap->numFree; i++)
    {
        TerrainRange* range = &heap->freeList[i];
        if (range->count < count) continue;
        u32 offset = range->offset;
        range->offset += count;
        range->count  -= count;
        if (range->count == 0u)
        {
            for (u32 j = i; j + 1u < heap->numFree; j++) heap->freeList[j] = heap->freeList[j + 1u];
            heap->numFree--;
        }
        heap->used += count;
        return offset;
    }
    return TERRAIN_HEAP_FAIL;
}

static void TerrainHeapFreeRange(TerrainHeap* heap, u32 offset, u32 count)
{
    if (count == 0u) return;
    heap->used -= count;
    u32 i = 0;
    while (i < heap->numFree && heap->freeList[i].offset < offset) i++;

    bool mergePrev = i > 0u && heap->freeList[i - 1u].offset + heap->freeList[i - 1u].count == offset;
    bool mergeNext = i < heap->numFree && offset + count == heap->freeList[i].offset;
    if (mergePrev && mergeNext)
    {
        heap->freeList[i - 1u].count += count + heap->freeList[i].count;
        for (u32 j = i; j + 1u < heap->numFree; j++) heap->freeList[j] = heap->freeList[j + 1u];
        heap->numFree--;
    }
    else if (mergePrev) heap->freeList[i - 1u].count += count;
    else if (mergeNext) { heap->freeList[i].offset = offset; heap->freeList[i].count += count; }
    else
    {
        ASSERT(heap->numFree < heap->freeCapacity);
        for (u32 j = heap->numFree; j > i; j--) heap->freeList[j] = heap->freeList[j - 1u];
        heap->freeList[i] = (TerrainRange){ offset, count };
        heap->numFree++;
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
    if (chunk->numVertices) TerrainHeapFreeRange(&g_Terrain.vertexHeap, chunk->vertexOffset, chunk->numVertices);
    if (chunk->numIndices)  TerrainHeapFreeRange(&g_Terrain.indexHeap, chunk->indexOffset, chunk->numIndices);
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

    // the editor lod factor scales how far every detail ring reaches, the pool warns
    // and degrades gracefully if a huge factor exhausts the chunk budget
    s32 radius = Clamps32((s32)((f32)TERRAIN_RING_RADIUS * g_Terrain.lodFactor + 0.5f), 1, 4);
    for (u32 level = 0; level < TERRAIN_LOD_COUNT; level++)
    {
        f32 size = TerrainChunkWorldSize(level);
        s32 camX = (s32)Floorf32(camera->position.x / size);
        s32 camZ = (s32)Floorf32(camera->position.z / size);
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

        u32 vertexOffset = TerrainHeapAllocRange(&g_Terrain.vertexHeap, job->mesh.numVertices);
        u32 indexOffset  = vertexOffset == TERRAIN_HEAP_FAIL ? TERRAIN_HEAP_FAIL
                         : TerrainHeapAllocRange(&g_Terrain.indexHeap, job->mesh.numIndices);
        if (indexOffset == TERRAIN_HEAP_FAIL)
        {
            if (vertexOffset != TERRAIN_HEAP_FAIL)
                TerrainHeapFreeRange(&g_Terrain.vertexHeap, vertexOffset, job->mesh.numVertices);
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

        SDL_memcpy(mapped + cursor, job->mesh.vertices, vertexBytes);
        regions[numRegions++] = (TerrainCopyRegion){
            g_Terrain.vertexBuffer, cursor, vertexOffset * (u32)sizeof(TerrainVertex), vertexBytes };
        cursor += vertexBytes;

        SDL_memcpy(mapped + cursor, job->mesh.indices, indexBytes);
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
        chunk->cpuVertices  = (TerrainVertex*)SDL_malloc(vertexBytes);
        chunk->cpuIndices   = (u32*)SDL_malloc(indexBytes);
        if (chunk->cpuVertices) SDL_memcpy(chunk->cpuVertices, job->mesh.vertices, vertexBytes);
        if (chunk->cpuIndices)  SDL_memcpy(chunk->cpuIndices, job->mesh.indices, indexBytes);
        chunk->empty = 0;
        chunk->appliedMask = job->transitionMask;
        chunk->state = ChunkState_Live;
        chunk->jobSlot = -1;
        // fresh split/merge replacements stay hidden until the whole group is live,
        // remeshes of an already visible chunk swap in place
        if (!wasVisible) chunk->hidden = TerrainCoveredByRetiring(chunk);
        g_Terrain.numDrawable++;

        f32 size = TerrainChunkWorldSize(chunk->lod);
        for (s32 a = 0; a < 3; a++)
        {
            f32 origin = (a == 0 ? (f32)chunk->x : (a == 1 ? (f32)chunk->y : (f32)chunk->z)) * size;
            chunk->aabbMin[a] = origin + job->mesh.aabbMin[a];
            chunk->aabbMax[a] = origin + job->mesh.aabbMax[a];
        }

        // the desired mask moved on while the worker was running: remesh
        if (chunk->transitionMask != chunk->appliedMask)
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

// moller trumbore, mirrors BVH_IntersectTriangle so terrain and scene hits compare 1:1
static bool TerrainIntersectTriangle(v128f origin, v128f dir, v128f v0, v128f v1, v128f v2,
                                     BVHHit* hit, u32 triIndex)
{
    v128f edge1 = VecSub(v1, v0);
    v128f edge2 = VecSub(v2, v0);
    v128f h = Vec3Cross(dir, edge2);
    f32 a = Vec3DotfV(edge1, h);
    if (a > -1.0e-9f && a < 1.0e-9f) return false;

    f32 f = 1.0f / a;
    v128f s = VecSub(origin, v0);
    f32 u = f * Vec3DotfV(s, h);
    bool fail = (u < 0.0f) | (u > 1.0f);

    v128f q = Vec3Cross(s, edge1);
    f32 v = f * Vec3DotfV(dir, q);
    f32 t = f * Vec3DotfV(edge2, q);
    fail |= (v < 0.0f) | (u + v > 1.0f);

    if (!fail & (t > 0.0001f) & (t < hit->t))
    {
        hit->u = u; hit->v = v; hit->t = t;
        hit->triIndex = triIndex;
        return true;
    }
    return false;
}

static bool TerrainRayHitsAABB(v128f origin, v128f invDir, v128f bmin, v128f bmax, f32 maxT)
{
    v128f t0 = VecMul(VecSub(bmin, origin), invDir);
    v128f t1 = VecMul(VecSub(bmax, origin), invDir);
    v128f tsmall = VecMin(t0, t1);
    v128f tbig   = VecMax(t0, t1);
    f32 tnear = Maxf32(Maxf32(VecGetX(tsmall), VecGetY(tsmall)), VecGetZ(tsmall));
    f32 tfar  = Minf32(Minf32(VecGetX(tbig), VecGetY(tbig)), VecGetZ(tbig));
    return tnear <= tfar && tfar > 0.0f && tnear < maxT;
}

s32 Terrain_Raycast(float3 origin, float3 dir, f32 maxDist, BVHHit* hit)
{
    if (!g_Terrain.initialized || !g_Terrain.enabled) return 0;

    v128f rayOrigin = VecSetR(origin.x, origin.y, origin.z, 0.0f);
    v128f rayDir    = VecSetR(dir.x, dir.y, dir.z, 0.0f);
    v128f invDir    = VecSetR(1.0f / dir.x, 1.0f / dir.y, 1.0f / dir.z, 1.0f);

    hit->t = maxDist;
    bool anyHit = false;

    for (u32 i = 0; i < TERRAIN_MAX_CHUNKS; i++)
    {
        TerrainChunk* chunk = &g_Terrain.chunks[i];
        // exactly the drawn set: live with geometry, hidden swaps excluded
        if (!chunk->used || chunk->state != ChunkState_Live || chunk->numIndices == 0u || chunk->hidden) continue;
        if (!chunk->cpuVertices || !chunk->cpuIndices) continue;

        v128f bmin = VecSetR(chunk->aabbMin[0], chunk->aabbMin[1], chunk->aabbMin[2], 0.0f);
        v128f bmax = VecSetR(chunk->aabbMax[0], chunk->aabbMax[1], chunk->aabbMax[2], 0.0f);
        if (!TerrainRayHitsAABB(rayOrigin, invDir, bmin, bmax, hit->t)) continue;

        f32 size = TerrainChunkWorldSize(chunk->lod);
        v128f chunkOrigin = VecSetR((f32)chunk->x * size, (f32)chunk->y * size, (f32)chunk->z * size, 0.0f);
        f32 metersPerStep = size / (f32)TERRAIN_POS_MAX;

        for (u32 t = 0; t + 2 < chunk->numIndices; t += 3)
        {
            v128f v0 = TerrainDecodePosition(&chunk->cpuVertices[chunk->cpuIndices[t + 0]], chunkOrigin, metersPerStep);
            v128f v1 = TerrainDecodePosition(&chunk->cpuVertices[chunk->cpuIndices[t + 1]], chunkOrigin, metersPerStep);
            v128f v2 = TerrainDecodePosition(&chunk->cpuVertices[chunk->cpuIndices[t + 2]], chunkOrigin, metersPerStep);
            if (TerrainIntersectTriangle(rayOrigin, rayDir, v0, v1, v2, hit, t / 3u))
            {
                anyHit = true;
                hit->entityIdx = i;            // terrain chunk slot
                hit->groupIdx = chunk->lod;
            }
        }
    }

    if (!anyHit) return 0;
    hit->skinnedSet = 0xFFFFFFFFu; // marks a terrain hit, not a render set entity
    hit->bundleIdx  = 0xFFFFFFFFu;
    v128f p = VecAdd(VecSetR(origin.x, origin.y, origin.z, 0.0f), VecMulf(rayDir, hit->t));
    Vec3Store(hit->position, p);
    hit->position[3] = 1.0f;
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

        v128f aabbMin = VecSetR(chunk->aabbMin[0], chunk->aabbMin[1], chunk->aabbMin[2], 0.0f);
        v128f aabbMax = VecSetR(chunk->aabbMax[0], chunk->aabbMax[1], chunk->aabbMax[2], 0.0f);
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
    f32 fragmentParams[4] = { sunDirection.x, sunDirection.y, sunDirection.z, 0.0f };
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
        .format = SDL_GetGPUShaderFormats(g_GPUDevice),
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

    Transvoxel_SelfTest();

    g_Terrain.chunks    = (TerrainChunk*)AllocZeroTLSFGlobal(TERRAIN_MAX_CHUNKS, sizeof(TerrainChunk));
    g_Terrain.freeSlots = (u32*)AllocateTLSFGlobal(TERRAIN_MAX_CHUNKS * sizeof(u32));
    g_Terrain.pending   = (u32*)AllocateTLSFGlobal(TERRAIN_PENDING_CAP * sizeof(u32));
    for (u32 i = 0; i < TERRAIN_MAX_CHUNKS; i++)
        g_Terrain.freeSlots[i] = TERRAIN_MAX_CHUNKS - 1u - i;
    g_Terrain.numFreeSlots = TERRAIN_MAX_CHUNKS;
    g_Terrain.chunkMap = HMCreate(TERRAIN_MAX_CHUNKS, sizeof(u32));

    TerrainHeapInit(&g_Terrain.vertexHeap, TERRAIN_MAX_VERTICES);
    TerrainHeapInit(&g_Terrain.indexHeap, TERRAIN_MAX_INDICES);

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

    g_Terrain.vertexBuffer = CreateBuffer(NULL, TERRAIN_MAX_VERTICES * sizeof(TerrainVertex), BVertexBit, "TerrainVertexBuffer");
    g_Terrain.indexBuffer  = CreateBuffer(NULL, TERRAIN_MAX_INDICES * sizeof(u32), SDL_GPU_BUFFERUSAGE_INDEX, "TerrainIndexBuffer");
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

    f32 size0 = TerrainChunkWorldSize(0);
    s32 cam[3] = {
        (s32)Floorf32(camera->position.x / size0),
        (s32)Floorf32(camera->position.y / size0),
        (s32)Floorf32(camera->position.z / size0)
    };
    if (!g_Terrain.desiredValid || cam[0] != g_Terrain.lastCamChunk[0] || cam[2] != g_Terrain.lastCamChunk[2]
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

#if TERRAIN_DEBUG_HOLES // periodic audit: desired set vs resident chunks, plus a raycast
    {                    // probe that compares the drawn surface against the density field
        static u32 dbgFrame = 0;
        if ((++dbgFrame % 240u) == 0u)
        {
            u32 live = 0, hid = 0, retir = 0, queued = 0, working = 0, emptyN = 0;
            for (u32 i = 0; i < TERRAIN_MAX_CHUNKS; i++)
            {
                TerrainChunk* c = &g_Terrain.chunks[i];
                if (!c->used) continue;
                if (c->state == ChunkState_Live)
                {
                    live++;
                    if (c->hidden) { hid++; AX_LOG("  hidden lod%u (%d,%d,%d)\n", c->lod, c->x, c->y, c->z); }
                    if (c->retiring) retir++;
                    if (c->empty) emptyN++;
                }
                else if (c->state == ChunkState_Queued) queued++;
                else working++;
            }
            // chunks the player can currently see that still have no mesh: the user
            // facing metric, stays near zero when dispatch prioritization works
            u32 visWaiting = 0;
            for (u32 i = 0; i < TERRAIN_MAX_CHUNKS; i++)
            {
                TerrainChunk* c = &g_Terrain.chunks[i];
                if (!c->used || c->state == ChunkState_Live) continue;
                f32 size = TerrainChunkWorldSize(c->lod);
                f32 ox = (f32)c->x * size, oy = (f32)c->y * size, oz = (f32)c->z * size;
                v128f bmin = VecSetR(ox, oy, oz, 0.0f);
                v128f bmax = VecSetR(ox + size, oy + size, oz + size, 0.0f);
                if (CheckAABBCulled(bmin, bmax, g_Terrain.cameraFrustum.planes)) visWaiting++;
            }
            u32 missing = 0;
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
                    if (!HMFind(&g_Terrain.chunkMap, TerrainChunkKey(x, y, z, level)))
                    {
                        missing++;
                        AX_LOG("  missing lod%u (%d,%d,%d)\n", level, x, y, z);
                    }
                }
            }
            // raycast probe: downward rays across the lod0..lod2 rings. anywhere the
            // density field has a top surface, the rendered chunk set must hit too
            u32 probeMiss = 0, probeDev = 0, probeHits = 0;
            const f32 probeTop = 100.0f;
            for (s32 gz = -10; gz <= 10; gz++)
            for (s32 gx = -10; gx <= 10; gx++)
            {
                f32 wx = camera->position.x + (f32)gx * 15.0f;
                f32 wz = camera->position.z + (f32)gz * 15.0f;

                // top surface of the density field: coarse march down, then bisect
                f32 expectedY = 0.0f;
                bool hasSurface = false;
                f32 prev = TerrainDensity_SDF(wx, probeTop, wz);
                for (f32 y = probeTop - 2.0f; y >= -60.0f; y -= 2.0f)
                {
                    f32 d = TerrainDensity_SDF(wx, y, wz);
                    if (prev > 0.0f && d <= 0.0f)
                    {
                        f32 lo = y, hi = y + 2.0f;
                        for (s32 it = 0; it < 24; it++)
                        {
                            f32 mid = (lo + hi) * 0.5f;
                            if (TerrainDensity_SDF(wx, mid, wz) <= 0.0f) lo = mid; else hi = mid;
                        }
                        expectedY = lo;
                        hasSurface = true;
                        break;
                    }
                    prev = d;
                }
                if (!hasSurface) continue;

                BVHHit hit;
                if (!Terrain_Raycast((float3){ wx, probeTop, wz }, (float3){ 0.0f, -1.0f, 0.0f }, 200.0f, &hit))
                {
                    probeMiss++;
                    AX_WARN("terrain probe MISS at (%.0f, %.0f) expected y=%.1f", wx, wz, expectedY);
                    for (u32 level = 0; level < TERRAIN_LOD_COUNT; level++)
                    {
                        f32 size = TerrainChunkWorldSize(level);
                        s32 chx = (s32)Floorf32(wx / size), chy = (s32)Floorf32(expectedY / size), chz = (s32)Floorf32(wz / size);
                        u32* slot = (u32*)HMFind(&g_Terrain.chunkMap, TerrainChunkKey(chx, chy, chz, level));
                        if (!slot) { AX_WARN("  lod%u (%d,%d,%d): not resident", level, chx, chy, chz); continue; }
                        TerrainChunk* c = &g_Terrain.chunks[*slot];
                        AX_WARN("  lod%u (%d,%d,%d): state=%u empty=%u hidden=%u retiring=%u indices=%u",
                                level, chx, chy, chz, c->state, c->empty, c->hidden, c->retiring, c->numIndices);
                    }
                }
                else
                {
                    probeHits++;
                    f32 hitY = probeTop - hit.t;
                    // coarse lods legitimately smooth the surface, tolerance scales with cell size
                    f32 tolerance = 2.0f + 1.5f * TERRAIN_VOXEL_SIZE * (f32)(1 << hit.groupIdx);
                    if (Absf32(hitY - expectedY) > tolerance)
                    {
                        probeDev++;
                        AX_WARN("terrain probe DEVIATION at (%.0f, %.0f): hit y=%.1f lod%u expected y=%.1f",
                                wx, wz, hitY, hit.groupIdx, expectedY);
                    }
                }
            }
            AX_LOG("terrain dbg: live=%u empty=%u hidden=%u retiring=%u queued=%u working=%u visWaiting=%u missing=%u pending=%u vheap=%u/%u iheap=%u/%u probe hit/miss/dev=%u/%u/%u\n",
                   live, emptyN, hid, retir, queued, working, visWaiting, missing, g_Terrain.numPending,
                   g_Terrain.vertexHeap.used, TERRAIN_MAX_VERTICES,
                   g_Terrain.indexHeap.used, TERRAIN_MAX_INDICES, probeHits, probeMiss, probeDev);
        }
    }
#endif
}

void Terrain_SetEnabled(bool enabled) { g_Terrain.enabled = enabled; }
bool Terrain_GetEnabled(void)         { return g_Terrain.enabled; }

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
