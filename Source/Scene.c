
#include "Include/Scene.h"
#include "Include/Graphics.h"
#include "Include/Memory.h"
#include "Include/Platform.h"
#include "Include/AssetManager.h"
#include "Include/Animation.h"
#include "Include/FileSystem.h"
#include "Include/Rendering.h"
#include "Include/Random.h"
#include "Include/Algorithm.h"
#include "Include/GLTFParser.h"
#include "Include/BVH.h"
#include "Include/DataStructures/HashMap.h"

Scene* g_ActiveScenes[MAX_ACTIVE_SCENES];
u32    g_NumActiveScenes;

extern Graphics gGFX;

// returns the bundle's vertex/index ranges to the geometry heaps. safe to call
// twice, the pointers are nulled after the free. bundles whose geometry lives
// outside the mega buffers (fbx path) have NULL heap pointers and are skipped
static void Scene_FreeBundleGeometry(SceneBundle* bundle, bool skinned)
{
    if (bundle->vertexHeapPtr)
    {
        GeometryHeapFree(skinned ? GeometryBuffer_SkinnedVertex : GeometryBuffer_SurfaceVertex, bundle->vertexHeapPtr);
        if (skinned) gGFX.NumSkinnedVertices -= (u32)bundle->totalVertices;
        else         gGFX.NumSurfaceVertices -= (u32)bundle->totalVertices;
        bundle->vertexHeapPtr = NULL;
        bundle->allVertices = NULL;
    }
    if (bundle->indexHeapPtr)
    {
        GeometryHeapFree(GeometryBuffer_Index, bundle->indexHeapPtr);
        gGFX.NumIndices -= (u32)bundle->totalIndices;
        bundle->indexHeapPtr = NULL;
        bundle->allIndices = NULL;
    }
}

/*//////////////////////////////////////////////////////////////////////////*/
/*                              Bundle Cache                                */
/*//////////////////////////////////////////////////////////////////////////*/

typedef struct BundleCacheEntry_
{
    SceneBundle* bundle;
    char*        path;     // cache owned copy, scenes reference it in bundlePaths
    u32          refCount;
} BundleCacheEntry;

// resident mesh bundles keyed by path hash, shared between scenes and repeated adds
static HashMap gBundleCache;

// mesh data only, builds the .abm and .bdc caches when they are missing or stale.
// staging images are the caller's concern. out: 0 on failure
static s32 LoadBundleMeshCached(const char* path, SceneBundle* bundle)
{
    char buffer[1024];
    int pathLen = StringLength(path);
    MemCopy(buffer, path, pathLen + 1);
    int newLen = ChangeExtension(buffer, pathLen, "abm");
    if (IsABMLastVersion(buffer))
    {
        AX_LOG("asset cache hit: %s", buffer);
        return LoadSceneBundleBinary(buffer, bundle);
    }
    if (!ParseGLTF(path, bundle, 1.0f))
    {
        AX_WARN("asset import failed: %s", path);
        return 0;
    }
    AX_LOG("asset cache rebuild: %s -> %s", path, buffer);
    if (!BakeSceneMeshesAndAnimations(bundle))
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
    if (!LoadBundleMeshCached(path, bundle))
    {
        DeAllocateTLSFGlobal(bundle);
        return NULL;
    }
    BVH_BuildBundle(bundle, bundle->numSkins > 0); // editor picking, failure only disables it

    int pathLen = StringLength(path);
    char* pathCopy = (char*)AllocateTLSFGlobal(pathLen + 1);
    MemCopy(pathCopy, path, pathLen + 1);

    BundleCacheEntry value = { bundle, pathCopy, 1u };
    return (BundleCacheEntry*)HMInsert(&gBundleCache, key, &value);
}

static void BundleCacheRelease(const char* path)
{
    u64 key = StringToHash64(path);
    BundleCacheEntry* entry = (BundleCacheEntry*)HMFind(&gBundleCache, key);
    if (!entry || entry->refCount == 0) return;
    if (--entry->refCount > 0) return;

    // geometry returns to the mega buffers. the rest of the bundle cpu data stays
    // allocated, consistent with the engine teardown (skinned bundles registered
    // animation data that is not reclaimed yet)
    char* pathCopy = entry->path;
    BVH_FreeBundle(entry->bundle);
    Scene_FreeBundleGeometry(entry->bundle, entry->bundle->numSkins > 0);
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
    while (newCapacity < needed) newCapacity *= 2u;
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
    scene->bundleRefs = NULL;
    scene->bundleCapacity = 0;
    scene->lights = NULL;
    // render set cpu allocations stay, consistent with the rest of the engine teardown
}

s32 Scene_Activate(Scene* scene)
{
    for (u32 i = 0; i < g_NumActiveScenes; i++)
        if (g_ActiveScenes[i] == scene) return 1;

    if (g_NumActiveScenes >= MAX_ACTIVE_SCENES)
    {
        AX_WARN("maximum active scene count reached: %d", MAX_ACTIVE_SCENES);
        return 0;
    }

    // skinned animation instances and their gpu pools are indexed by sparse id,
    // two active scenes with skinned entities would collide in those pools
    if (scene->skinnedSet.numEntities > 0)
        for (u32 i = 0; i < g_NumActiveScenes; i++)
            if (g_ActiveScenes[i]->skinnedSet.numEntities > 0)
                AX_WARN("multiple active scenes with skinned entities share animation instance slots");

    g_ActiveScenes[g_NumActiveScenes++] = scene;
    scene->renderDataDirty = 1;
    return 1;
}

void Scene_Deactivate(Scene* scene)
{
    for (u32 i = 0; i < g_NumActiveScenes; i++)
    {
        if (g_ActiveScenes[i] != scene) continue;
        for (u32 j = i + 1; j < g_NumActiveScenes; j++)
            g_ActiveScenes[j - 1] = g_ActiveScenes[j];
        g_NumActiveScenes--;
        g_ActiveScenes[g_NumActiveScenes] = NULL;
        return;
    }
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

u32 Scene_AddBundle(Scene* scene, const char* path, bool skinned)
{
    // repeated adds of the same path reuse the scene bundle, spawn more instances instead
    u32 existing = Scene_FindBundle(scene, path);
    if (existing != INVALID_BUNDLE)
        return existing;

    if (!Scene_EnsureBundleCapacity(scene, scene->numBundles + 1))
        return INVALID_BUNDLE;
    
    // baked pages have no live packer state, rebuild it before the first append
    if (scene->texturesBaked && !Scene_RepackTextures(scene))
        return INVALID_BUNDLE;

    BundleCacheEntry* entry = BundleCacheAcquire(path);
    if (!entry)
    {
        AX_ERROR("gltf scene load failed: %s", path);
        return INVALID_BUNDLE;
    }
    SceneBundle* bundle = entry->bundle;
    const char* storedPath = entry->path;

    ArenaMark mark = ArenaSave(&GlobalArena);
    Texture* staging = (Texture*)ArenaAllocZero(&GlobalArena, MAX_SCENE_TEXTURES * sizeof(Texture));

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
    if (imageResult == 0)
    {
        AX_ERROR("scene image load failed: %s", storedPath);
        ArenaRestore(&GlobalArena, mark);
        BundleCacheRelease(storedPath);
        return INVALID_BUNDLE;
    }

    u32 animOffset = scene->animSystem.numAnimations;
    if (skinned && !AnimationSystem_AppendBundle(&scene->animSystem, bundle))
    {
        AX_ERROR("scene animation creation failed: %s", storedPath);
        TextureSystem_ReleaseTextures(staging, (u32)bundle->numImages);
        ArenaRestore(&GlobalArena, mark);
        BundleCacheRelease(storedPath);
        return INVALID_BUNDLE;
    }

    // staging is bundle local, material slots are stable for the bundle's lifetime
    u32 materialOffset = scene->numMaterials;
    s32 appended = TextureSystem_AppendBundle(&scene->textureSystem, bundle, staging, materialOffset);
    TextureSystem_ReleaseTextures(staging, (u32)bundle->numImages);
    ArenaRestore(&GlobalArena, mark);
    if (!appended)
    {
        BundleCacheRelease(storedPath);
        return INVALID_BUNDLE;
    }

    RenderSet* set = skinned ? &scene->skinnedSet : &scene->surfaceSet;
    u32 renderIdx = RenderSet_AddSceneBundle(set, bundle, materialOffset);
    if (renderIdx == INVALID_BUNDLE)
    {
        AX_ERROR("render set bundle registration failed: %s", storedPath);
        BundleCacheRelease(storedPath);
        return INVALID_BUNDLE;
    }

    u32 bundleIdx = scene->numBundles++;
    SceneBundleRef* ref = &scene->bundleRefs[bundleIdx];
    ref->path           = storedPath;
    ref->bundle         = bundle;
    ref->renderIdx      = renderIdx;
    ref->materialOffset = materialOffset;
    ref->animOffset     = animOffset;
    ref->skinned        = skinned;
    scene->numMaterials += (u32)bundle->numMaterials;
    return bundleIdx;
}

const SceneBundle* Scene_AcquireBundlePeek(const char* path)
{
    BundleCacheEntry* entry = BundleCacheAcquire(path);
    return entry ? entry->bundle : NULL;
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
    if (!entry)
    {
        AX_ERROR("scene bundle load failed: %s", path);
        return INVALID_BUNDLE;
    }
    SceneBundle* bundle = entry->bundle;
    const char* storedPath = entry->path;
    bool skinned = bundle->numSkins > 0;

    u32 animOffset = scene->animSystem.numAnimations;
    if (skinned && !AnimationSystem_AppendBundle(&scene->animSystem, bundle))
    {
        AX_ERROR("scene animation creation failed: %s", storedPath);
        BundleCacheRelease(storedPath);
        return INVALID_BUNDLE;
    }

    RenderSet* set = skinned ? &scene->skinnedSet : &scene->surfaceSet;
    u32 renderIdx = RenderSet_AddSceneBundle(set, bundle, materialOffset);
    if (renderIdx == INVALID_BUNDLE)
    {
        AX_ERROR("render set bundle registration failed: %s", storedPath);
        BundleCacheRelease(storedPath);
        return INVALID_BUNDLE;
    }

    u32 bundleIdx = scene->numBundles++;
    SceneBundleRef* ref = &scene->bundleRefs[bundleIdx];
    ref->path           = storedPath;
    ref->bundle         = bundle;
    ref->renderIdx      = renderIdx;
    ref->materialOffset = materialOffset;
    ref->animOffset     = animOffset;
    ref->skinned        = skinned;
    if (materialOffset + (u32)bundle->numMaterials > scene->numMaterials)
        scene->numMaterials = materialOffset + (u32)bundle->numMaterials;
    return bundleIdx;
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
    u32 added = RenderSet_AddScene(set, scene->bundleRefs[bundleIdx].renderIdx, position, rotation, scale, skinned);
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
    while (g_NumActiveScenes > 0)
        Scene_Deactivate(g_ActiveScenes[g_NumActiveScenes - 1]);
    return Scene_Activate(scene);
}

Scene* Scene_GetActive(void)
{
    return g_NumActiveScenes > 0 ? g_ActiveScenes[0] : NULL;
}
