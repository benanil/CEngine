
#include "Include/Scene.h"
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
/*                              Bundle Cache                                */
/*//////////////////////////////////////////////////////////////////////////*/

// resident mesh bundles keyed by path hash, shared between scenes and repeated adds
static HashMap gBundleCache;

// mesh data only, builds the .abm and .bdc caches when they are missing or stale.
// staging images are the caller's concern. out: 0 on failure
static s32 LoadBundleMeshCached(const char* path, SceneBundle* bundle, void** outVertexHeapPtr, void** outIndexHeapPtr)
{
    char buffer[1024];
    int pathLen = StringLength(path);
    MemCopy(buffer, path, pathLen + 1);
    int newLen = ChangeExtension(buffer, pathLen, "abm");
    if (IsABMLastVersion(buffer))
    {
        AX_LOG("asset cache hit: %s", buffer);
        return LoadSceneBundleBinary(buffer, bundle, outVertexHeapPtr, outIndexHeapPtr);
    }
    if (!ImportSceneBundle(path, bundle, 1.0f))
    {
        AX_WARN("asset import failed: %s", path);
        return 0;
    }
    AX_LOG("asset cache rebuild: %s -> %s", path, buffer);
    if (!BakeSceneMeshesAndAnimations(bundle, outVertexHeapPtr, outIndexHeapPtr))
    {
        AX_WARN("asset import failed during mesh bake: %s vertices=%d indices=%d", path, bundle->totalVertices, bundle->totalIndices);
        return 0;
    }
    if (!SaveGLTFBinary(bundle, buffer))
    {
        AX_WARN("asset cache save failed: %s", buffer);
        return 0;
    }
    ChangeExtension(buffer, newLen, "bdc");
    SaveSceneImages(bundle, buffer, FileHasExtension(path, pathLen, ".glb"));
    return 1;
}

// out: cache entry with one reference added, NULL on load failure
static BundleCacheEntry* BundleCacheAcquire(const char* path)
{
    if (gBundleCache.valueSize == 0)
        gBundleCache = HMCreate(64u, sizeof(BundleCacheEntry));

    u64 key = StringToHash64(path);
    BundleCacheEntry* entry = (BundleCacheEntry*)HMFind(&gBundleCache, key);
    if (entry)
    {
        entry->refCount++;
        AX_LOG("bundle cache hit: %s refs=%d", path, entry->refCount);
        return entry;
    }

    SceneBundle* bundle = (SceneBundle*)AllocZeroTLSFGlobal(1, sizeof(SceneBundle));
    void* vertexHeapPtr = NULL;
    void* indexHeapPtr = NULL;
    if (!LoadBundleMeshCached(path, bundle, &vertexHeapPtr, &indexHeapPtr))
    {
        DeAllocateTLSFGlobal(bundle);
        return NULL;
    }
    int pathLen = StringLength(path);
    char* pathCopy = (char*)AllocateTLSFGlobal(pathLen + 1);
    MemCopy(pathCopy, path, pathLen + 1);

    BundleCacheEntry value = { bundle, pathCopy, 1u };
    entry = (BundleCacheEntry*)HMInsert(&gBundleCache, key, &value);
    entry->vertexHeapPtr = vertexHeapPtr;
    entry->indexHeapPtr = indexHeapPtr;
    BVH_BuildBundleCached(bundle, entry, bundle->numSkins > 0); // editor picking, failure only disables it
    return entry;
}

static void BundleCacheRelease(const char* path)
{
    u64 key = StringToHash64(path);
    BundleCacheEntry* entry = (BundleCacheEntry*)HMFind(&gBundleCache, key);
    if (!entry || entry->refCount == 0) return;
    if (--entry->refCount > 0) return;

    // geometry returns to the mega buffers. the rest of the bundle cpu data stays
    // allocated, consistent with the engine teardown ownership model
    char* pathCopy = entry->path;
    BVH_FreeBundle(entry);
    Scene_FreeBundleGeometry(entry->bundle, entry, entry->bundle->numSkins > 0);
    HMErase(&gBundleCache, key);
    DeAllocateTLSFGlobal(pathCopy);
}

// doubles the tlsf backed bundle ref array, bundle lists are usually small but can
// fragment heavily between scenes. out: 0 on allocation failure or hard cap
static s32 Scene_EnsureBundleCapacity(Scene* scene, u32 needed)
{
    if (needed <= scene->bundleCapacity) return 1;
    if (needed > MAX_SCENE_BUNDLES)
    {
        AX_WARN("maximum scene bundle count reached: %d", MAX_SCENE_BUNDLES);
        return 0;
    }

    u32 newCapacity = scene->bundleCapacity ? scene->bundleCapacity * 2u : 16u;
    while (newCapacity < needed) newCapacity += newCapacity >> 1;
    if (newCapacity > MAX_SCENE_BUNDLES) newCapacity = MAX_SCENE_BUNDLES;

    SceneBundleRef* refs = (SceneBundleRef*)AllocateTLSFGlobal(newCapacity * sizeof(SceneBundleRef));
    if (!refs) return 0;
    if (scene->bundleRefs)
    {
        MemCopy(refs, scene->bundleRefs, scene->numBundles * sizeof(SceneBundleRef));
        DeAllocateTLSFGlobal(scene->bundleRefs);
    }
    scene->bundleRefs = refs;
    scene->bundleCapacity = newCapacity;
    return 1;
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
}

void Scene_Destroy(Scene* scene)
{
    Scene_Deactivate(scene);
    for (u32 i = 0; i < scene->numBundles; i++)
        BundleCacheRelease(scene->bundleRefs[i].path);
    DestroyRenderSetBuffers(&scene->skinnedBuffers);
    DestroyRenderSetBuffers(&scene->surfaceBuffers);
    TextureSystem_Destroy(&scene->textureSystem);
    AnimationSystem_Destroy(&scene->animSystem);
    if (scene->bundleRefs) DeAllocateTLSFGlobal(scene->bundleRefs);
    if (scene->lights) DeAllocateTLSFGlobal(scene->lights);
    if (scene->materialSlots) DeAllocateTLSFGlobal(scene->materialSlots);
    scene->bundleRefs = NULL;
    scene->bundleCapacity = 0;
    scene->lights = NULL;
    scene->materialSlots = NULL;
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
        if (StringEqual(scene->bundleRefs[i].path, path, StringLength(path) + 1))
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
    if (!Scene_EnsureBundleCapacity(scene, scene->numBundles + 1))
        return INVALID_BUNDLE;
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
    if (imageResult == 0 || imageResult == 3)
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

    u32 bundleIdx           = scene->numBundles++;
    SceneBundleRef* ref     = &scene->bundleRefs[bundleIdx];
    ref->path               = storedPath;
    ref->bundle             = bundle;
    ref->renderIdx          = renderIdx;
    ref->materialOffset     = materialOffset;
    ref->animOffset         = animAlloc.animOffset;
    ref->animAlloc          = animAlloc;
    ref->skinned            = skinned;
    ref->cache              = entry;

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

const BundleCacheEntry* FindCacheForRenderBundle(const Scene* scene, bool skinned, u32 renderIdx)
{
    for (u32 i = 0; i < scene->numBundles; i++)
    {
        const SceneBundleRef* ref = &scene->bundleRefs[i];
        if ((ref->skinned != 0) == skinned && ref->renderIdx == renderIdx)
            return ref->cache;
    }
    return NULL;
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
    for (u32 i = 0; i < scene->numBundles; i++)
    {
        const SceneBundleRef* ref = &scene->bundleRefs[i];
        if ((ref->skinned != 0) != skinned) continue;

        const RenderSet* set = skinned ? &scene->skinnedSet : &scene->surfaceSet;
        Range range = set->bundlePrimitiveRange[ref->renderIdx];
        if (groupIdx >= range.start && groupIdx < range.start + range.count)
            return i;
    }
    return INVALID_BUNDLE;
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
    if (!Scene_EnsureBundleCapacity(scene, scene->numBundles + 1))
        return INVALID_BUNDLE;

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

    u32 bundleIdx = scene->numBundles++;
    SceneBundleRef* ref = &scene->bundleRefs[bundleIdx];
    ref->path           = storedPath;
    ref->bundle         = bundle;
    ref->renderIdx      = renderIdx;
    ref->materialOffset = materialOffset;
    ref->animOffset     = animAlloc.animOffset;
    ref->animAlloc      = animAlloc;
    ref->skinned        = skinned;
    ref->cache          = entry;
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
    if (bundleIdx >= scene->numBundles) return 0;

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

    // the render set compacts its bundle list, later indices of the same set shift down
    for (u32 i = 0; i < scene->numBundles; i++)
    {
        if (i == bundleIdx) continue;
        if ((scene->bundleRefs[i].skinned != 0) == skinned && scene->bundleRefs[i].renderIdx > removedRenderIdx)
            scene->bundleRefs[i].renderIdx--;
    }

    for (u32 i = bundleIdx + 1; i < scene->numBundles; i++)
        scene->bundleRefs[i - 1] = scene->bundleRefs[i];
    scene->numBundles--;
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
        if (bundle->numImages <= 0 && bundle->numMaterials <= 0) continue;

        ArenaMark mark = ArenaSave(&GlobalArena);
        Texture* staging = (Texture*)ArenaAllocZero(&GlobalArena, MAX_SCENE_TEXTURES * sizeof(Texture));
        if (!LoadBundleImagesFromCache(scene->bundleRefs[b].path, staging, bundle->numImages))
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
    if (bundleIdx >= scene->numBundles) return 0;

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
