
#include "Include/SceneBundleCache.h"
#include "Include/Scene.h"
#include "Include/Async.h"
#include "Include/Graphics.h"
#include "Include/Memory.h"
#include "Include/Platform.h"
#include "Include/AssetManager.h"
#include "Include/Animation.h"
#include "Include/FileSystem.h"
#include "Include/Rendering.h"
#include "Include/Algorithm.h"
#include "Include/Random.h"
#include "Include/GLTFParser.h"
#include "Include/BVH.h"
#include "Include/DataStructures/HashMap.h"

#include <SDL3/SDL_stdinc.h>

extern Graphics gGFX;

// returns the bundle's vertex/index ranges to the geometry heaps. safe to call
// twice, the pointers are nulled after the free. bundles whose geometry lives
// outside the mega buffers (fbx path) have NULL heap pointers and are skipped
static void Scene_FreeBundleGeometry(SceneBundle* bundle, BundleCacheEntry* cache, bool skinned)
{
    if (cache->vertexHeapPtr)
    {
        GeometryHeapFree(skinned ? GeometryBuffer_SkinnedVertex : GeometryBuffer_SurfaceVertex, cache->vertexHeapPtr);
        if (skinned) gGFX.NumSkinnedVertices -= (u32)bundle->totalVertices;
        else         gGFX.NumSurfaceVertices -= (u32)bundle->totalVertices;
        cache->vertexHeapPtr = NULL;
        bundle->allVertices = NULL;
    }
    if (cache->indexHeapPtr)
    {
        GeometryHeapFree(GeometryBuffer_Index, cache->indexHeapPtr);
        gGFX.NumIndices -= (u32)bundle->totalIndices;
        cache->indexHeapPtr = NULL;
        bundle->allIndices = NULL;
    }
}

/*//////////////////////////////////////////////////////////////////////////*/
/*                              Async                                       */
/*//////////////////////////////////////////////////////////////////////////*/

SceneAsyncRequest* sceneAsyncRequest;

// Acquire a bundle into the cache on the worker thread and HOLD the reference (recording its key on
// the request). The main-thread callback then adds the bundle to the scene as a cache hit instead of
// re-baking it; SceneAsyncUpdate drops these warming references afterwards. out: false on load failure.
static bool SceneAsyncHold(SceneAsyncRequest* request, const char* path)
{
    if (!Scene_AcquireBundlePeek(path)) return false;

    if (request->heldCount == request->heldCap)
    {
        u32 newCap  = request->heldCap ? request->heldCap * 2u : 8u;
        u64* grown  = (u64*)SDL_realloc(request->heldKeys, (size_t)newCap * sizeof(u64));
        if (!grown) { Scene_ReleaseBundlePeek(path); return false; }
        request->heldKeys = grown;
        request->heldCap  = newCap;
    }
    request->heldKeys[request->heldCount++] = StringToHash64(path);
    return true;
}

void StageBundleRange(u32 begin, u32 end, void* userData)
{
    SceneBundleStage* stages = (SceneBundleStage*)userData;
    for (u32 i = begin; i < end; i++) 
        Scene_AddBundleStage(&stages[i], false);
}

static s32 PreloadAllBundlesOfScene(SceneAsyncRequest* request)
{
    AFile file = AFileOpen(request->path, AOpenFlag_ReadBinary);
    if (!AFileExist(file)) return 0;
    
    char line[2048];
    bool bundlesFound = false;
    while (AFileReadLine(line, sizeof(line), file) > 0)
    {
        if (!StrCMP16(line, "bundles")) continue;
        bundlesFound = true;
    }
    if (!bundlesFound) return 0;

    s32 numBundles;
    ParsePositiveNumber(line, &numBundles);
    
    SceneBundleStage* stages = CAllocTLSFArray(SceneBundleStage, numBundles);
    s32 numLoaded = 0, lineLen = 0;
    // bundle 0 0 0 Assets/Meshes/Sphere.gltf
    while (lineLen = AFileReadLine(line, sizeof(line), file))
    {
        if (!StrCMP16(line, "bundle ")) continue;
        const char* ln = line;
        while (*ln && IsWhitespace(*ln) || IsNumber(*ln))
            ln++;
        s32 pathLen = (u32)(u64)(line - ln);
        stages[numLoaded++].path = StringDuplicateN(ln, pathLen);
    }

    if (numLoaded != numBundles)
        AX_LOG("could'nt preload all bundles of scene");

    ParallelFor(numLoaded, 1u, StageBundleRange, stages);
    AFileClose(file);
    return 1;
}

static s32 SceneAsyncProbe(void* userData)
{
    SceneAsyncRequest* request = (SceneAsyncRequest*)userData;

    if (request->op == SceneAsyncOp_ImportMesh)
        return SceneAsyncHold(request, request->path);

    if (request->op != SceneAsyncOp_OpenScene)
        return PreloadAllBundlesOfScene(request);

    return 1;
}

static void SceneAsyncDone(void* userData, s32 result)
{
    SceneAsyncRequest* request = (SceneAsyncRequest*)userData;
    request->result = result;
    if (!result) AX_WARN("scene async request start failed: %s", request->path);
    SDL_SetAtomicInt(&request->done, 1);
}

bool SceneAsyncBegin(SceneAsyncOp op, const char* path, const char* taskName, SceneAsyncRequestCallback callback)
{
    if (!path || path[0] == '\0') {
        AX_WARN("scene async request invalid path");
        return false;
    }

    if (sceneAsyncRequest) {
        AX_WARN("scene async request already running: %s", sceneAsyncRequest->path);
        return false;
    }

    SceneAsyncRequest* request = (SceneAsyncRequest*)SDL_calloc(1, sizeof(SceneAsyncRequest));
    request->callback = callback;
    request->op = op;

    NormalizePath(path, request->path, StringLength(path)+1);
    if (request->path[0] == '\0') {
        AX_WARN("scene async request invalid path");
        return false;
    }
    AsyncRun(taskName, SceneAsyncProbe, SceneAsyncDone, request);
    sceneAsyncRequest = request;
    return true;
}

// only one async task is not good design
void Scene_AsyncUpdate(void)
{
    SceneAsyncRequest* request = sceneAsyncRequest;
    if (!request || !SDL_GetAtomicInt(&request->done)) return;
    sceneAsyncRequest = NULL;

    if (!request->result) {
        AX_ERROR("scene async request failed: %s", request->path);
    }
    else if (request->callback) {
        request->callback(request);
    }

    // The probe held one warming reference per bundle so the callback's scene/import load hit the
    // cache instead of re-baking. The scene took its own references in the callback, so drop the
    // warming ones now (this also frees the bundles when the request failed and no callback ran).
    for (u32 i = 0; i < request->heldCount; i++)
        BundleCacheReleaseKey(request->heldKeys[i]);
    SDL_free(request->heldKeys);
    SDL_free(request);
}

/*//////////////////////////////////////////////////////////////////////////*/
/*                              Bundle Cache                                */
/*//////////////////////////////////////////////////////////////////////////*/

// resident mesh bundles keyed by path hash, shared between scenes and repeated adds.
// async scene/mesh import touches this from worker threads, so every map operation (and every
// access to an entry) is serialized through g_BundleCacheLock. Entries are addressed by key, never
// by stored pointer, because HashMap relocates values on grow and on swap-with-last erase.
static HashMap gBundleCache;
static SDL_SpinLock g_BundleCacheLock;

// Look up a cached bundle by key under the cache lock. The returned SceneBundle* is stable (each
// bundle is allocated separately and never moves in the map), so it stays valid after the lock is
// released. out: NULL when no entry has that key.
static SceneBundle* BundleCacheFindBundle(u64 key)
{
    SDL_LockSpinlock(&g_BundleCacheLock);
    BundleCacheEntry* entry = (BundleCacheEntry*)HMFind(&gBundleCache, key);
    SceneBundle* bundle = entry ? entry->bundle : NULL;
    SDL_UnlockSpinlock(&g_BundleCacheLock);
    return bundle;
}

// Persists a freshly baked bundle's .abm mesh cache to disk on a worker thread. data is the cache
// key: the bundle is re-found by key and a reference is held for the duration, so its resident
// geometry stays alive while SaveGLTFBinary reads it (the save never mutates it).
typedef struct BundleSaveTask_
{
    u64  cacheKey;
    char abmPath[1024 - sizeof(u64)];
} BundleSaveTask;

static s32 SaveBundleCacheTask(void* data)
{
    BundleSaveTask* task = (BundleSaveTask*)data;
    SceneBundle* bundle = BundleCacheFindBundle(task->cacheKey);
    if (!bundle) return 0;
    return SaveGLTFBinary(bundle, task->abmPath);
}

static void SaveBundleCacheDone(void* data, s32 result)
{
    BundleSaveTask* task = (BundleSaveTask*)data;
    if (!result) AX_WARN("abm cache save failed: %s", task->abmPath);
    BundleCacheReleaseKey(task->cacheKey); // drop the reference held for the save
    SDL_free(task);
}

// Build + queue (or, on spawn failure, run) the async .abm persist for a just-baked bundle. The
// caller must already hold the reference this releases (incremented under the cache lock at insert).
static void BundleCacheQueueSave(const char* path, u64 key)
{
    BundleSaveTask* task = (BundleSaveTask*)SDL_calloc(1, sizeof(BundleSaveTask));
    if (!task) { BundleCacheReleaseKey(key); return; }

    int pathLen = StringLength(path);
    MemCopy(task->abmPath, path, (size_t)pathLen + 1);
    ChangeExtension(task->abmPath, pathLen, "abm");
    task->cacheKey = key;

    AsyncRun("Save Bundle Cache", SaveBundleCacheTask, SaveBundleCacheDone, task);
}

static void BVHCallback(void* data, s32 result)
{
    (void)data;
    if (result) AX_LOG("bvh creation success");
    else AX_LOG("bvh creation skipped/failed");
}

// Builds the picking BVH off the main thread. data is the cache key (not a pointer): entries move in
// the map, so we re-find by key under the lock to read the bundle and to publish the result.
static s32 CreateBVH(void* data)
{
    u64 key = (u64)(uintptr_t)data;

    SceneBundle* bundle = BundleCacheFindBundle(key);
    if (!bundle) return 0;
    // Build using the stable bundle pointer (the SceneBundle is allocated separately, it never moves).
    // Results land in a local entry; bvhNodes/bvhTris are SDL_malloc'd and stable once built. A second
    // build is prevented by the !entry->bvhNodes check when publishing below (only one task is spawned
    // per bundle anyway, on the fresh insert).
    BundleCacheEntry built;
    MemsetZero(&built, sizeof(built));
    built.bundle = bundle;
    if (!BVH_BuildBundleCached(bundle, &built, bundle->numSkins > 0))
        return 0;

    SDL_LockSpinlock(&g_BundleCacheLock);
    BundleCacheEntry* entry = (BundleCacheEntry*)HMFind(&gBundleCache, key);
    if (entry && !entry->bvhNodes)
    {
        entry->bvhNodes    = built.bvhNodes;
        entry->bvhTris     = built.bvhTris;
        entry->numBvhNodes = built.numBvhNodes;
        entry->numBvhTris  = built.numBvhTris;
        SDL_UnlockSpinlock(&g_BundleCacheLock);
        return 1;
    }
    // entry was released while we built (or another build won the race): drop our copy
    SDL_UnlockSpinlock(&g_BundleCacheLock);
    BVH_FreeBundle(&built);
    return 0;
}

static BundleCacheEntry* AddBundleCacheEntry(SceneBundle* bundle, void* vertexHeapPtr, void* indexHeapPtr, bool baked, u64 key)
{
    BundleCacheEntry value = { bundle, 1u };
    value.vertexHeapPtr = vertexHeapPtr;
    value.indexHeapPtr = indexHeapPtr;
    SDL_LockSpinlock(&g_BundleCacheLock);
    BundleCacheEntry* entry = (BundleCacheEntry*)HMInsert(&gBundleCache, key, &value);
    if (entry && baked) entry->refCount++; // hold a reference for the async cache save
    SDL_UnlockSpinlock(&g_BundleCacheLock);
    return entry;
}

// out: cache entry with one reference added, NULL on load failure. The returned pointer is only
// valid until the next map mutation; callers use it transiently and store the key, not the pointer.
BundleCacheEntry* BundleCacheAcquire(SceneBundleStage* stage)
{
    SDL_LockSpinlock(&g_BundleCacheLock);
    if (gBundleCache.valueSize == 0)
        gBundleCache = HMCreate(64u, sizeof(BundleCacheEntry));
    BundleCacheEntry* entry = (BundleCacheEntry*)HMFind(&gBundleCache, stage->cacheKey);
    if (entry)
    {
        entry->refCount++;
        AX_LOG("bundle cache hit: %s refs=%d", stage->path, entry->refCount);
        SDL_UnlockSpinlock(&g_BundleCacheLock);
        return entry;
    }
    SDL_UnlockSpinlock(&g_BundleCacheLock);

    // Load/bake outside the lock (slow). Only one importer touches a given path at a time
    // (the async op guard plus callbacks running after the worker finishes), so no double bake.
    SceneBundle* bundle = stage->bundle ? stage->bundle : (SceneBundle*)AllocTLSF(sizeof(SceneBundle));
    stage->bundle = bundle;
    void* vertexHeapPtr = NULL;
    void* indexHeapPtr = NULL;
    bool baked = false;
    if (!stage->isRuntime && !LoadBundleMeshCached(stage->path, bundle, &vertexHeapPtr, &indexHeapPtr, &baked))
    {
        DeAllocTLSF(bundle);
        return NULL;
    }

    if (stage->isRuntime && !BakeSceneMeshesAndAnimations(bundle, &vertexHeapPtr, &indexHeapPtr))
    {
        AX_WARN("asset import failed during mesh bake: %s vertices=%d indices=%d", stage->path, bundle->totalVertices, bundle->totalIndices);
        return NULL;
    }
    entry = AddBundleCacheEntry(bundle, vertexHeapPtr, indexHeapPtr, baked, stage->cacheKey);

    // Persist the freshly baked .abm/.bdc cache on a worker thread (it holds the reference above and
    // reads the resident geometry read-only). A plain cache hit already has its cache on disk.
    if (baked)
        BundleCacheQueueSave(stage->path, stage->cacheKey);

    // BVH builds asynchronously and addresses the entry by key, so it is safe across later inserts.
    AsyncRun("Create BVH", CreateBVH, BVHCallback, (void*)(uintptr_t)stage->cacheKey);
    return entry;
}

void BundleCacheReleaseKey(u64 key)
{
    SDL_LockSpinlock(&g_BundleCacheLock);
    BundleCacheEntry* entry = (BundleCacheEntry*)HMFind(&gBundleCache, key);
    if (!entry || entry->refCount == 0) { SDL_UnlockSpinlock(&g_BundleCacheLock); return; }
    if (--entry->refCount > 0) { SDL_UnlockSpinlock(&g_BundleCacheLock); return; }

    // Snapshot what we need to free, erase the entry, then free outside the lock. Freeing through the
    // snapshot is safe: bvh arrays and geometry heap blocks are stable heap allocations, not in the map.
    BundleCacheEntry freed = *entry;
    HMErase(&gBundleCache, key);
    SDL_UnlockSpinlock(&g_BundleCacheLock);

    // geometry returns to the mega buffers. the rest of the bundle cpu data stays
    // allocated, consistent with the engine teardown ownership model
    BVH_FreeBundle(&freed);
    Scene_FreeBundleGeometry(freed.bundle, &freed, freed.bundle->numSkins > 0);
}

void BundleCacheRelease(const char* path)
{
    BundleCacheReleaseKey(StringToHash64(path));
}

bool FindCacheForSceneBundle(const Scene* scene, u32 bundleIdx, BundleCacheEntry* out)
{
    if (bundleIdx >= scene->numBundles || !scene->bundleRefs[bundleIdx].bundle) return false;
    const SceneBundleRef* ref = &scene->bundleRefs[bundleIdx];

    // Copy the entry out under the lock, addressed by key: gBundleCache relocates entries on grow
    // and on swap-with-last erase, so a stored/returned pointer could dangle. The copied
    // bvhNodes/bvhTris are stable heap allocations that live while the bundle is referenced.
    SDL_LockSpinlock(&g_BundleCacheLock);
    const BundleCacheEntry* entry = (const BundleCacheEntry*)HMFind(&gBundleCache, ref->cacheKey);
    if (entry) *out = *entry;
    SDL_UnlockSpinlock(&g_BundleCacheLock);
    return entry != NULL;
}
