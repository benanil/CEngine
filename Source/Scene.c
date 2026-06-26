
#include "Include/Scene.h"
#include "Include/Async.h"
#include "Include/Graphics.h"
#include "Include/Memory.h"
#include "Include/Platform.h"
#include "Include/AssetManager.h"
#include "Include/Animation.h"
#include "Include/FileSystem.h"
#include "Include/Rendering.h"
#include "Include/SceneSerializer.h"
#include "Include/Terrain.h"
#include "Include/Random.h"
#include "Include/Algorithm.h"
#include "Include/Bitset.h"
#include "Include/GLTFParser.h"
#include "Include/BVH.h"
#include "Include/DataStructures/HashMap.h"

#include <SDL3/SDL_stdinc.h>

Scene* g_ActiveScene = NULL;

static Scene g_OwnedActiveScene;
static bool  g_OwnedActiveSceneInit;
static char  g_ActiveScenePath[512];

extern Graphics gGFX;

static void SceneTerrainPath(const char* scenePath, char* out, u32 outSize)
{
    NormalizePath(scenePath, out, outSize);
    ChangeExtension(out, 512, "terrain");
}

static void SceneSaveTerrainSidecar(const char* scenePath)
{
    char terrainPath[512];
    SceneTerrainPath(scenePath, terrainPath, sizeof(terrainPath));

    if (Terrain_GetEnabled())
        Terrain_SaveWorld(terrainPath);
    else if (FileExist(terrainPath))
        RemoveFile(terrainPath);
}

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

static void BundleCacheReleaseKey(u64 key);

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

static s32 SceneAsyncProbe(void* userData)
{
    SceneAsyncRequest* request = (SceneAsyncRequest*)userData;

    if (request->op == SceneAsyncOp_ImportMesh)
        return SceneAsyncHold(request, request->path);

    if (request->op == SceneAsyncOp_OpenScene)
    {
        AFile file = AFileOpen(request->path, AOpenFlag_ReadBinary);
        if (!AFileExist(file)) return 0;

        char line[2048];
        while (AFileReadLine(line, sizeof(line), file) > 0)
        {
            int len = StringLength(line);
            while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r' || line[len - 1] == ' '))
                line[--len] = '\0';

            const char prefix[] = "bundle ";
            if (!StringEqual(line, prefix, StringLength(prefix))) continue;

            const char* path = line + StringLength(prefix);
            for (u32 spaces = 0u; spaces < 3u && *path; path++)
                if (*path == ' ') spaces++;
            while (*path == ' ') path++;
            if (!*path) continue;

            // Bake/load each bundle off the main thread and HOLD it, so the main-thread open hits the
            // cache (no re-bake) and the worker-built picking BVH is reused. SceneAsyncUpdate releases
            // these after the scene has taken its own references. The cache is keyed + lock-guarded,
            // so this is safe against the main thread picking the still-active scene.
            if (!SceneAsyncHold(request, path))
            {
                AFileClose(file);
                return 0;
            }
        }
        AFileClose(file);
        return 1;
    }
    return 1;
}

static void SceneAsyncDone(void* userData, s32 result)
{
    SceneAsyncRequest* request = (SceneAsyncRequest*)userData;
    request->result = result;
    SDL_SetAtomicInt(&request->done, 1);
}

bool SceneAsyncBegin(SceneAsyncOp op, const char* path, const char* taskName, SceneAsyncRequestCallback callback)
{
    if (!path || path[0] == '\0')
    {
        AX_WARN("scene async request invalid path");
        return false;
    }

    if (sceneAsyncRequest)
    {
        AX_WARN("scene async request already running: %s", sceneAsyncRequest->path);
        return false;
    }

    char normalized[512];
    NormalizePath(path, normalized, sizeof(normalized));
    if (normalized[0] == '\0')
    {
        AX_WARN("scene async request invalid path");
        return false;
    }

    SceneAsyncRequest* request = (SceneAsyncRequest*)SDL_calloc(1, sizeof(SceneAsyncRequest));
    if (!request)
    {
        AX_WARN("scene async request allocation failed");
        return false;
    }

    request->callback = callback;
    request->op = op;
    MemCopy(request->path, normalized, StringLength(normalized) + 1);
    if (!AsyncRun(taskName, SceneAsyncProbe, SceneAsyncDone, request))
    {
        SDL_free(request);
        AX_WARN("scene async request start failed: %s", normalized);
        return false;
    }

    sceneAsyncRequest = request;
    return true;
}

void SceneAsyncUpdate(void)
{
    SceneAsyncRequest* request = sceneAsyncRequest;
    if (!request || !SDL_GetAtomicInt(&request->done)) return;
    sceneAsyncRequest = NULL;

    if (request->result)
    {
        if (request->callback)
            request->callback(request);
    }
    else
    {
        AX_ERROR("scene async request failed: %s", request->path);
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

// mesh data only. Writes the .bdc image cache synchronously, but on an .abm cache miss it sets
// *outBaked so the caller persists the .abm mesh cache asynchronously (that's the slow part).
// staging images are the caller's concern. out: 0 on failure
static s32 LoadBundleMeshCached(const char* path, SceneBundle* bundle, void** outVertexHeapPtr, void** outIndexHeapPtr, bool* outBaked)
{
    *outBaked = false;
    char buffer[1024];
    int pathLen = StringLength(path);
    MemCopy(buffer, path, pathLen + 1);
    int newLen = ChangeExtension(buffer, pathLen, "abm");
    if (IsABMLastVersion(buffer))
    {
        AX_LOG("asset cache hit: %s", buffer);
        return LoadSceneBundleBinary(buffer, bundle, outVertexHeapPtr, outIndexHeapPtr);
    }
    // Import and bake do all their temporary work through ArenaPushGlobal. When this runs on an
    // async worker thread (editor mesh import), sharing the main thread's GlobalArena would corrupt
    // its LIFO bump pointer, so give this bake its own scratch arena for the duration. It's a small
    // bump buffer (ARENA_SCRATCH_SIZE); allocations larger than it spill to the thread-safe TLSF heap.
    ArenaScratch bakeArena;
    if (!ArenaBeginScratch(&bakeArena, ARENA_SCRATCH_SIZE, "LoadBundleMeshCached"))
    {
        AX_WARN("asset import failed: scratch arena allocation failed: %s", path);
        return 0;
    }

    if (!ImportSceneBundle(path, bundle, 1.0f))
    {
        AX_WARN("asset import failed: %s", path);
        ArenaEndScratch(&bakeArena);
        return 0;
    }
    AX_LOG("asset cache rebuild: %s -> %s", path, buffer);
    if (!BakeSceneMeshesAndAnimations(bundle, outVertexHeapPtr, outIndexHeapPtr))
    {
        AX_WARN("asset import failed during mesh bake: %s vertices=%d indices=%d", path, bundle->totalVertices, bundle->totalIndices);
        ArenaEndScratch(&bakeArena);
        return 0;
    }
    ArenaEndScratch(&bakeArena);
    // The .bdc image cache is written synchronously here (cheap relative to the .abm mesh cache, and
    // fine to block the import). The .abm mesh cache is the slow part and is persisted asynchronously
    // by the caller.
    ChangeExtension(buffer, newLen, "bdc");
    if (!FileExist(buffer))
    {
        ChangeExtension(buffer, newLen, "glb");
        bool deleteRemaining = FileExist(buffer);
        ChangeExtension(buffer, newLen, "bdc");
        SaveSceneImages(bundle, buffer, deleteRemaining);
    }
    *outBaked = true;
    return 1;
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

    if (!AsyncRun("Save Bundle Cache", SaveBundleCacheTask, SaveBundleCacheDone, task))
    {
        // couldn't spawn: persist synchronously so the cache still gets written
        AX_WARN("bundle cache save task failed to start, saving synchronously: %s", task->abmPath);
        s32 ok = SaveBundleCacheTask(task);
        SaveBundleCacheDone(task, ok);
    }
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

// out: cache entry with one reference added, NULL on load failure. The returned pointer is only
// valid until the next map mutation; callers use it transiently and store the key, not the pointer.
static BundleCacheEntry* BundleCacheAcquire(const char* path)
{
    u64 key = StringToHash64(path);

    SDL_LockSpinlock(&g_BundleCacheLock);
    if (gBundleCache.valueSize == 0)
        gBundleCache = HMCreate(64u, sizeof(BundleCacheEntry));
    BundleCacheEntry* entry = (BundleCacheEntry*)HMFind(&gBundleCache, key);
    if (entry)
    {
        entry->refCount++;
        AX_LOG("bundle cache hit: %s refs=%d", path, entry->refCount);
        SDL_UnlockSpinlock(&g_BundleCacheLock);
        return entry;
    }
    SDL_UnlockSpinlock(&g_BundleCacheLock);

    // Load/bake outside the lock (slow). Only one importer touches a given path at a time
    // (the async op guard plus callbacks running after the worker finishes), so no double bake.
    SceneBundle* bundle = (SceneBundle*)AllocZeroTLSFGlobal(1, sizeof(SceneBundle));
    void* vertexHeapPtr = NULL;
    void* indexHeapPtr = NULL;
    bool baked = false;
    if (!LoadBundleMeshCached(path, bundle, &vertexHeapPtr, &indexHeapPtr, &baked))
    {
        DeAllocateTLSFGlobal(bundle);
        return NULL;
    }
    int pathLen = StringLength(path);
    char* pathCopy = (char*)AllocateTLSFGlobal(pathLen + 1);
    MemCopy(pathCopy, path, pathLen + 1);

    BundleCacheEntry value = { bundle, pathCopy, 1u };
    value.vertexHeapPtr = vertexHeapPtr;
    value.indexHeapPtr = indexHeapPtr;
    SDL_LockSpinlock(&g_BundleCacheLock);
    entry = (BundleCacheEntry*)HMInsert(&gBundleCache, key, &value);
    if (entry && baked) entry->refCount++; // hold a reference for the async cache save
    SDL_UnlockSpinlock(&g_BundleCacheLock);

    // Persist the freshly baked .abm/.bdc cache on a worker thread (it holds the reference above and
    // reads the resident geometry read-only). A plain cache hit already has its cache on disk.
    if (baked)
        BundleCacheQueueSave(path, key);

    // BVH builds asynchronously and addresses the entry by key, so it is safe across later inserts.
    if (!AsyncRun("Create BVH", CreateBVH, BVHCallback, (void*)(uintptr_t)key))
        AX_WARN("bvh create task failed! %s", path);
    return entry;
}

static void BundleCacheReleaseKey(u64 key)
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
    DeAllocateTLSFGlobal(freed.path);
}

static void BundleCacheRelease(const char* path)
{
    BundleCacheReleaseKey(StringToHash64(path));
}

// reserves the lowest free bundle slot so indices stay stable across removals. the ref array is a
// fixed MAX_SCENE_BUNDLES allocation. out: bundle slot index, INVALID_BUNDLE when full
static u32 Scene_AllocBundleSlot(Scene* scene)
{
    s32 slot = BitsetFindFirstEmpty(scene->bundleSlots, (s32)MAX_SCENE_BUNDLES);
    if (slot < 0)
    {
        AX_WARN("maximum scene bundle count reached: %d", MAX_SCENE_BUNDLES);
        return INVALID_BUNDLE;
    }
    BitsetSet(scene->bundleSlots, slot);
    if ((u32)slot + 1u > scene->numBundles) scene->numBundles = (u32)slot + 1u;
    return (u32)slot;
}

// frees a bundle slot and pulls the watermark back over any trailing empty slots
static void Scene_FreeBundleSlot(Scene* scene, u32 bundleIdx)
{
    BitsetReset(scene->bundleSlots, (s32)bundleIdx);
    MemsetZero(&scene->bundleRefs[bundleIdx], sizeof(SceneBundleRef));
    while (scene->numBundles > 0 && !BitsetGet(scene->bundleSlots, (s32)(scene->numBundles - 1u)))
        scene->numBundles--;
}

// stamps every primitive group of a render bundle with its owning scene bundle index. both indices
// are stable handles, so the group keeps pointing at the right bundle even after group compaction,
// giving O(1) render-group -> scene-bundle without a reverse map.
static void Scene_StampGroupBundle(RenderSet* set, u32 renderIdx, u32 sceneIdx)
{
    Range range = set->bundlePrimitiveRange[renderIdx];
    for (u32 g = 0; g < range.count; g++)
        set->primitiveGroups[range.start + g].bundleIdx = sceneIdx;
}

void Scene_Init(Scene* scene)
{
    MemsetZero(scene, sizeof(*scene));
    RenderSet_InitSet(&scene->skinnedSet, MAX_ANIM_INSTANCES, MAX_GROUP, MAX_BUNDLES, true);
    RenderSet_InitSet(&scene->surfaceSet, MAX_ENTITY, MAX_GROUP, MAX_BUNDLES, false);
    CreateRenderSetBuffers(&scene->skinnedBuffers, MAX_ANIM_INSTANCES, MAX_GROUP);
    CreateRenderSetBuffers(&scene->surfaceBuffers, MAX_ENTITY, MAX_GROUP);
    TextureSystem_Init(&scene->textureSystem);
    AnimationSystem_Init(&scene->animSystem);
    scene->lights = (LightGPU*)AllocZeroTLSFGlobal(MAX_SCENE_LIGHTS, sizeof(LightGPU));
    scene->materialSlots = (u64*)AllocZeroTLSFGlobal((MAX_GPU_MATERIALS + 63u) >> 6, sizeof(u64));
    scene->bundleSlots   = (u64*)AllocZeroTLSFGlobal((MAX_SCENE_BUNDLES + 63u) >> 6, sizeof(u64));
    scene->bundleRefs    = (SceneBundleRef*)AllocZeroTLSFGlobal(MAX_SCENE_BUNDLES, sizeof(SceneBundleRef));
}

void Scene_Destroy(Scene* scene)
{
    Scene_Deactivate(scene);
    for (u32 i = 0; i < scene->numBundles; i++)
        if (scene->bundleRefs[i].bundle)
            BundleCacheRelease(scene->bundleRefs[i].path);
    DestroyRenderSetBuffers(&scene->skinnedBuffers);
    DestroyRenderSetBuffers(&scene->surfaceBuffers);
    TextureSystem_Destroy(&scene->textureSystem);
    AnimationSystem_Destroy(&scene->animSystem);
    if (scene->bundleRefs) DeAllocateTLSFGlobal(scene->bundleRefs);
    if (scene->lights) DeAllocateTLSFGlobal(scene->lights);
    if (scene->materialSlots) DeAllocateTLSFGlobal(scene->materialSlots);
    if (scene->bundleSlots) DeAllocateTLSFGlobal(scene->bundleSlots);
    scene->bundleRefs = NULL;
    scene->lights = NULL;
    scene->materialSlots = NULL;
    scene->bundleSlots = NULL;
    if (scene == &g_OwnedActiveScene)
    {
        g_OwnedActiveSceneInit = false;
        g_ActiveScenePath[0] = '\0';
    }
    // render set cpu allocations stay, consistent with the rest of the engine teardown
}

Scene* Scene_NewActive(void)
{
    if (g_OwnedActiveSceneInit)
        Scene_Destroy(&g_OwnedActiveScene);
    Scene_Init(&g_OwnedActiveScene);
    g_OwnedActiveSceneInit = true;
    g_ActiveScenePath[0] = '\0';
    Scene_MakeActive(&g_OwnedActiveScene);
    RendererSetLights(NULL, 0u);
    Terrain_DeleteWorld();
    return &g_OwnedActiveScene;
}

Scene* Scene_OpenActive(const char* path)
{
    char normalized[512];
    NormalizePath(path, normalized, sizeof(normalized));

    Scene* scene = Scene_NewActive();
    if (!scene) return NULL;
    if (!SceneSerializer_Load(scene, normalized))
    {
        AX_ERROR("scene load failed: %s", normalized);
        return NULL;
    }
    MemCopy(g_ActiveScenePath, normalized, StringLength(normalized) + 1);

    Terrain_DeleteWorld();
    char terrainPath[512];
    SceneTerrainPath(normalized, terrainPath, sizeof(terrainPath));
    if (FileExist(terrainPath))
        Terrain_LoadWorld(terrainPath);
    return scene;
}

s32 Scene_SaveActive(void)
{
    Scene* scene = Scene_GetActive();
    if (!scene || g_ActiveScenePath[0] == '\0') return 0;
    if (!SceneSerializer_Save(scene, g_ActiveScenePath)) return 0;
    SceneSaveTerrainSidecar(g_ActiveScenePath);
    return 1;
}

s32 Scene_SaveActiveAs(const char* path)
{
    Scene* scene = Scene_GetActive();
    if (!scene || !path || path[0] == '\0') return 0;

    char normalized[512];
    NormalizePath(path, normalized, sizeof(normalized));
    EnsurePath(normalized);
    if (!SceneSerializer_Save(scene, normalized)) return 0;
    MemCopy(g_ActiveScenePath, normalized, StringLength(normalized) + 1);
    SceneSaveTerrainSidecar(g_ActiveScenePath);
    return 1;
}

const char* Scene_GetActivePath(void)
{
    return g_ActiveScenePath;
}

s32 Scene_Activate(Scene* scene)
{
    if (g_ActiveScene == scene) return 1;
    g_ActiveScene = scene;
    scene->renderDataDirty = 1;
    return 1;
}

void Scene_Deactivate(Scene* scene)
{
    if (g_ActiveScene != scene) return;
    g_ActiveScene = NULL;
}

// loads the cached basis images of a gltf into a bundle local staging array
static s32 LoadBundleImagesFromCache(const char* gltfPath, Texture* staging, s32 numImages)
{
    char path[1024];
    int pathLen = StringLength(gltfPath);
    MemCopy(path, gltfPath, pathLen + 1);
    ChangeExtension(path, pathLen, "bdc");
    return LoadSceneImages(path, staging, numImages);
}

// out: scene bundle index of the path, INVALID_BUNDLE when not present
static u32 Scene_FindBundle(const Scene* scene, const char* path)
{
    for (u32 i = 0; i < scene->numBundles; i++)
        if (scene->bundleRefs[i].bundle && StringEqual(scene->bundleRefs[i].path, path, StringLength(path) + 1))
            return i;
    return INVALID_BUNDLE;
}

static void Scene_UpdateMaterialWatermark(Scene* scene)
{
    while (scene->numMaterials > 0 && !BitsetGet(scene->materialSlots, (s32)(scene->numMaterials - 1u)))
        scene->numMaterials--;
    scene->textureSystem.materialWatermark = scene->numMaterials;
}

static s32 Scene_AllocateMaterialSlots(Scene* scene, u32 numMaterials)
{
    if (numMaterials == 0) return 0;
    s32 firstEmpty = BitsetFindEmptyRange(scene->materialSlots, MAX_GPU_MATERIALS, numMaterials);
    if (firstEmpty != -1)
    {
        BitsetSetRange(scene->materialSlots, firstEmpty, numMaterials, true);
        return firstEmpty;
    }
    AX_WARN("maximum GPU materials reached: count=%d", numMaterials);
    return -1;
}

static s32 Scene_ReserveMaterialSlots(Scene* scene, u32 materialOffset, u32 numMaterials)
{
    if (numMaterials == 0) return 1;
    if (materialOffset + numMaterials > MAX_GPU_MATERIALS)
    {
        AX_WARN("maximum GPU materials reached: offset=%d count=%d", materialOffset, numMaterials);
        return 0;
    }

    for (u32 i = 0; i < numMaterials; i++)
    {
        if (BitsetGet(scene->materialSlots, (s32)(materialOffset + i)))
        {
            AX_WARN("material slot range overlaps existing bundle: offset=%d count=%d", materialOffset, numMaterials);
            return 0;
        }
    }

    BitsetSetRange(scene->materialSlots, materialOffset, numMaterials, true);
    return 1;
}

u32 Scene_AddBundle(Scene* scene, const char* path, bool skinned)
{
    u32 existing = Scene_FindBundle(scene, path);
    if (existing != INVALID_BUNDLE)
        return existing;
    if (BitsetFindFirstEmpty(scene->bundleSlots, (s32)MAX_SCENE_BUNDLES) < 0)
    {
        AX_WARN("maximum scene bundle count reached: %d", MAX_SCENE_BUNDLES);
        return INVALID_BUNDLE;
    }
    if (scene->texturesBaked && !Scene_RepackTextures(scene))
        return INVALID_BUNDLE;

    BundleCacheEntry* entry = BundleCacheAcquire(path);
    if (!entry) {
        AX_WARN("gltf scene load failed: %s", path); return INVALID_BUNDLE;
    }

    SceneBundle* bundle    = entry->bundle;
    const char* storedPath = entry->path;
    ArenaMark mark         = ArenaSave(&GlobalArena);
    Texture* staging       = (Texture*)ArenaAllocZero(&GlobalArena, MAX_SCENE_TEXTURES * sizeof(Texture));

    s32 imageResult = LoadBundleImagesFromCache(storedPath, staging, bundle->numImages);
    if (imageResult == 0)// || imageResult == 3)
    {
        char buffer[1024];
        int pathLen = StringLength(storedPath);
        MemCopy(buffer, storedPath, pathLen + 1);
        ChangeExtension(buffer, pathLen, "bdc");
        AX_WARN("scene image cache invalid, rebuilding: %s result=%d", buffer, imageResult);
        SaveSceneImages(bundle, buffer, false);
        imageResult = LoadBundleImagesFromCache(storedPath, staging, bundle->numImages);
    }
    if (imageResult == 0) goto err_arena;

    AnimationBundleAlloc animAlloc;
    MemsetZero(&animAlloc, sizeof(animAlloc));
    bool animAppended = false;
    if (skinned && !AnimationSystem_AppendBundle(&scene->animSystem, bundle, &animAlloc))
        goto err_arena;
    animAppended = skinned;

    s32 allocatedMaterialOffset = Scene_AllocateMaterialSlots(scene, (u32)bundle->numMaterials);
    if (allocatedMaterialOffset < 0) goto err_arena;

    u32 materialOffset = (u32)allocatedMaterialOffset;
    s32 appended       = TextureSystem_AppendBundle(&scene->textureSystem, bundle, staging, materialOffset);
    TextureSystem_ReleaseTextures(staging, (u32)bundle->numImages);
    ArenaRestore(&GlobalArena, mark);

    if (!appended) goto err_materials;

    RenderSet* set  = skinned ? &scene->skinnedSet : &scene->surfaceSet;
    u32 renderIdx   = RenderSet_AddSceneBundle(set, bundle, materialOffset);
    if (renderIdx == INVALID_BUNDLE) goto err_textures;

    u32 bundleIdx           = Scene_AllocBundleSlot(scene);
    if (bundleIdx == INVALID_BUNDLE)
    {
        RenderSet_RemoveSceneBundle(set, renderIdx);
        goto err_textures;
    }
    SceneBundleRef* ref     = &scene->bundleRefs[bundleIdx];
    ref->path               = storedPath;
    ref->bundle             = bundle;
    ref->renderIdx          = renderIdx;
    ref->materialOffset     = materialOffset;
    ref->animOffset         = animAlloc.animOffset;
    ref->animAlloc          = animAlloc;
    ref->skinned            = skinned;
    ref->cacheKey           = StringToHash64(storedPath);
    Scene_StampGroupBundle(set, renderIdx, bundleIdx);

    if (materialOffset + (u32)bundle->numMaterials > scene->numMaterials)
        scene->numMaterials = materialOffset + (u32)bundle->numMaterials;
    scene->renderDataDirty = 1;
    return bundleIdx;

err_textures:
    AX_ERROR("render set bundle registration failed: %s", storedPath);
    TextureSystem_RemoveBundle(&scene->textureSystem, bundle, materialOffset);
err_materials:
    BitsetSetRange(scene->materialSlots, materialOffset, (u32)bundle->numMaterials, false);
    if (animAppended) AnimationSystem_RemoveBundle(&scene->animSystem, animAlloc);
    BundleCacheRelease(storedPath);
    return INVALID_BUNDLE;
err_arena:
    TextureSystem_ReleaseTextures(staging, (u32)bundle->numImages);
    ArenaRestore(&GlobalArena, mark);
    if (animAppended) AnimationSystem_RemoveBundle(&scene->animSystem, animAlloc);
    BundleCacheRelease(storedPath);
    return INVALID_BUNDLE;
}

const SceneBundle* Scene_AcquireBundlePeek(const char* path)
{
    BundleCacheEntry* entry = BundleCacheAcquire(path);
    return entry ? entry->bundle : NULL;
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

u32 Scene_DefaultAnimation(const Scene* scene, u32 bundleIdx)
{
    if (!scene || bundleIdx >= scene->numBundles) return 0u;
    const SceneBundleRef* ref = &scene->bundleRefs[bundleIdx];
    u32 numAnims = ref->bundle ? (u32)ref->bundle->numAnimations : 0u;
    if (numAnims == 0u) return 0u;
    return ref->animOffset + (numAnims > 1u ? 1u : 0u);
}

u32 Scene_FindBundleForRenderGroup(const Scene* scene, bool skinned, u32 groupIdx)
{
    if (!scene) return INVALID_BUNDLE;
    const RenderSet* set = skinned ? &scene->skinnedSet : &scene->surfaceSet;
    if (groupIdx >= set->numGroups) return INVALID_BUNDLE;

    // each group stores its owning scene bundle index directly. it is a stable handle and rides
    // along through group compaction, so this is a plain O(1) field read.
    return set->primitiveGroups[groupIdx].bundleIdx;
}

void Scene_ReleaseBundlePeek(const char* path)
{
    BundleCacheRelease(path);
}

u32 Scene_AddBundleAuto(Scene* scene, const char* path)
{
    // hold a reference while peeking so the bundle loads only once
    BundleCacheEntry* entry = BundleCacheAcquire(path);
    if (!entry)
    {
        AX_ERROR("gltf scene load failed: %s", path);
        return INVALID_BUNDLE;
    }
    bool skinned = entry->bundle->numSkins > 0;
    u32 bundleIdx = Scene_AddBundle(scene, path, skinned);
    BundleCacheRelease(path);
    return bundleIdx;
}

u32 Scene_AddBundleBaked(Scene* scene, const char* path, u32 materialOffset)
{
    if (BitsetFindFirstEmpty(scene->bundleSlots, (s32)MAX_SCENE_BUNDLES) < 0)
    {
        AX_WARN("maximum scene bundle count reached: %d", MAX_SCENE_BUNDLES);
        return INVALID_BUNDLE;
    }

    BundleCacheEntry* entry = BundleCacheAcquire(path);
    if (!entry) {
        AX_ERROR("scene bundle load failed: %s", path);
        return INVALID_BUNDLE;
    }
    SceneBundle* bundle = entry->bundle;
    const char* storedPath = entry->path;
    bool skinned = bundle->numSkins > 0;

    if (!Scene_ReserveMaterialSlots(scene, materialOffset, (u32)bundle->numMaterials))
    {
        BundleCacheRelease(storedPath);
        return INVALID_BUNDLE;
    }

    AnimationBundleAlloc animAlloc;
    MemsetZero(&animAlloc, sizeof(animAlloc));
    bool animAppended = false;
    if (skinned && !AnimationSystem_AppendBundle(&scene->animSystem, bundle, &animAlloc))
    {
        AX_ERROR("scene animation creation failed: %s", storedPath);
        goto err_bundle;
    }
    animAppended = skinned;

    RenderSet* set = skinned ? &scene->skinnedSet : &scene->surfaceSet;
    u32 renderIdx = RenderSet_AddSceneBundle(set, bundle, materialOffset);
    if (renderIdx == INVALID_BUNDLE)
    {
        AX_ERROR("render set bundle registration failed: %s", storedPath);
        goto err_bundle;
    }

    u32 bundleIdx = Scene_AllocBundleSlot(scene);
    if (bundleIdx == INVALID_BUNDLE)
    {
        RenderSet_RemoveSceneBundle(set, renderIdx);
        goto err_bundle;
    }
    SceneBundleRef* ref = &scene->bundleRefs[bundleIdx];
    ref->path           = storedPath;
    ref->bundle         = bundle;
    ref->renderIdx      = renderIdx;
    ref->materialOffset = materialOffset;
    ref->animOffset     = animAlloc.animOffset;
    ref->animAlloc      = animAlloc;
    ref->skinned        = skinned;
    ref->cacheKey       = StringToHash64(storedPath);
    Scene_StampGroupBundle(set, renderIdx, bundleIdx);
    if (materialOffset + (u32)bundle->numMaterials > scene->numMaterials)
        scene->numMaterials = materialOffset + (u32)bundle->numMaterials;
    scene->renderDataDirty = 1;
    return bundleIdx;
err_bundle:
    if (animAppended) AnimationSystem_RemoveBundle(&scene->animSystem, animAlloc);
    BitsetSetRange(scene->materialSlots, materialOffset, (u32)bundle->numMaterials, false);
    BundleCacheRelease(storedPath);
    return INVALID_BUNDLE;
}

u32 Scene_RemoveBundle(Scene* scene, u32 bundleIdx)
{
    if (bundleIdx >= scene->numBundles || !scene->bundleRefs[bundleIdx].bundle) return 0;

    SceneBundleRef* ref = &scene->bundleRefs[bundleIdx];
    bool skinned = ref->skinned != 0;
    RenderSet* set = skinned ? &scene->skinnedSet : &scene->surfaceSet;
    u32 removedRenderIdx = ref->renderIdx;

    u32 removedEntities = RenderSet_RemoveSceneBundle(set, removedRenderIdx);
    TextureSystem_RemoveBundle(&scene->textureSystem, ref->bundle, ref->materialOffset);
    BitsetSetRange(scene->materialSlots, ref->materialOffset, (u32)ref->bundle->numMaterials, false);
    Scene_UpdateMaterialWatermark(scene);
    if (skinned) AnimationSystem_RemoveBundle(&scene->animSystem, ref->animAlloc);
    BundleCacheRelease(ref->path);

    // both the render set and the scene keep stable slot handles now, so removing this bundle
    // leaves every other bundle's index (and its renderIdx) untouched. just free the slot.
    Scene_FreeBundleSlot(scene, bundleIdx);
    scene->renderDataDirty = 1;
    return removedEntities;
}

s32 Scene_RepackTextures(Scene* scene)
{
    AX_LOG("Scene_RepackTextures");
    TextureSystem_ResetPacking(&scene->textureSystem);
    scene->texturesBaked = 0;

    for (u32 b = 0; b < scene->numBundles; b++)
    {
        SceneBundle* bundle = scene->bundleRefs[b].bundle;
        if (!bundle || (bundle->numImages <= 0 && bundle->numMaterials <= 0)) continue;

        ArenaMark mark = ArenaSave(&GlobalArena);
        Texture* staging = (Texture*)ArenaAllocZero(&GlobalArena, MAX_SCENE_TEXTURES * sizeof(Texture));
        if (LoadBundleImagesFromCache(scene->bundleRefs[b].path, staging, bundle->numImages) == 0)
        {
            AX_ERROR("scene image load failed during repack: %s", scene->bundleRefs[b].path);
            ArenaRestore(&GlobalArena, mark);
            return 0;
        }

        s32 appended = TextureSystem_AppendBundle(&scene->textureSystem, bundle, staging, scene->bundleRefs[b].materialOffset);
        TextureSystem_ReleaseTextures(staging, (u32)bundle->numImages);
        ArenaRestore(&GlobalArena, mark);
        if (!appended)
            return 0;
    }
    return 1;
}

u32 Scene_Spawn(Scene* scene, u32 bundleIdx, v128f position, v128f rotation, v128f scale)
{
    if (bundleIdx >= scene->numBundles || !scene->bundleRefs[bundleIdx].bundle) return 0;

    bool skinned = scene->bundleRefs[bundleIdx].skinned != 0;
    RenderSet* set = skinned ? &scene->skinnedSet : &scene->surfaceSet;
    Range range = set->bundlePrimitiveRange[scene->bundleRefs[bundleIdx].renderIdx];
    u32* oldCounts = NULL;
    if (skinned && range.count > 0u)
    {
        oldCounts = (u32*)ArenaAllocGlobal(range.count * sizeof(u32));
        for (u32 i = 0; i < range.count; i++)
            oldCounts[i] = set->primitiveGroups[range.start + i].numEntities;
    }

    u32 added = RenderSet_AddScene(set, scene->bundleRefs[bundleIdx].renderIdx, position, rotation, scale, skinned);
    if (skinned && added && oldCounts)
    {
        GPUAnimationInstance instance = { .animIdx = Scene_DefaultAnimation(scene, bundleIdx), .timeOffset = 0.0f };
        for (u32 i = 0; i < range.count; i++)
        {
            PrimitiveGroup* group = &set->primitiveGroups[range.start + i];
            for (u32 e = oldCounts[i]; e < group->numEntities; e++)
            {
                u32 sparseIdx = set->entities[group->entityOffset + e].sparseIdx;
                AnimationSystem_SetInstance(&scene->animSystem, sparseIdx, instance);
            }
        }
    }
    if (oldCounts) ArenaPopGlobal(range.count * sizeof(u32));

    if (added) scene->renderDataDirty = 1;
    return added;
}

void Scene_ClearEntities(Scene* scene)
{
    RenderSet_ClearEntities(&scene->skinnedSet);
    RenderSet_ClearEntities(&scene->surfaceSet);
    scene->renderDataDirty = 1;
}

void Scene_SubmitLights(void)
{
    Scene* scene = Scene_GetActive();
    if (scene && scene->numLights)
        RendererSetLights(scene->lights, scene->numLights);
}

s32 Scene_MakeActive(Scene* scene)
{
    if (g_ActiveScene) Scene_Deactivate(g_ActiveScene);
    return Scene_Activate(scene);
}

Scene* Scene_GetActive(void)
{
    return g_ActiveScene;
}
