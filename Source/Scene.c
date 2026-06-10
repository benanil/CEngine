
#include "Include/Scene.h"
#include "Include/Graphics.h"
#include "Include/Memory.h"
#include "Include/Platform.h"
#include "Include/AssetManager.h"
#include "Include/Animation.h"
#include "Include/FileSystem.h"

Scene* g_ActiveScenes[MAX_ACTIVE_SCENES];
u32    g_NumActiveScenes;

void Scene_Init(Scene* scene)
{
    MemsetZero(scene, sizeof(*scene));
    RenderSet_InitSet(&scene->skinnedSet, MAX_ANIM_INSTANCES, MAX_GROUP, MAX_BUNDLES, true);
    RenderSet_InitSet(&scene->surfaceSet, MAX_ENTITY, MAX_GROUP, MAX_BUNDLES, false);
    CreateRenderSetBuffers(&scene->skinnedBuffers, MAX_ANIM_INSTANCES, MAX_GROUP);
    CreateRenderSetBuffers(&scene->surfaceBuffers, MAX_ENTITY, MAX_GROUP);
    TextureSystem_Init(&scene->textureSystem);
}

void Scene_Destroy(Scene* scene)
{
    Scene_Deactivate(scene);
    DestroyRenderSetBuffers(&scene->skinnedBuffers);
    DestroyRenderSetBuffers(&scene->surfaceBuffers);
    TextureSystem_Destroy(&scene->textureSystem);
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

u32 Scene_AddBundle(Scene* scene, const char* path, bool skinned)
{
    if (scene->numBundles >= MAX_SCENE_BUNDLES)
    {
        AX_WARN("maximum scene bundle count reached: %d", MAX_SCENE_BUNDLES);
        return INVALID_BUNDLE;
    }

    SceneBundle* bundle = (SceneBundle*)AllocZeroTLSFGlobal(1, sizeof(SceneBundle));
    ArenaMark mark = ArenaSave(&GlobalArena);
    Texture* staging = (Texture*)ArenaAllocZero(&GlobalArena, MAX_SCENE_TEXTURES * sizeof(Texture));

    if (!LoadGLTFCached(path, bundle, staging))
    {
        AX_ERROR("gltf scene load failed: %s", path);
        ArenaRestore(&GlobalArena, mark);
        DeAllocateTLSFGlobal(bundle);
        return INVALID_BUNDLE;
    }

    if (skinned && !SceneBundleCreateAnimations(bundle))
    {
        AX_ERROR("scene animation creation failed: %s", path);
        TextureSystem_ReleaseTextures(staging, (u32)bundle->numImages);
        ArenaRestore(&GlobalArena, mark);
        return INVALID_BUNDLE;
    }

    // staging is bundle local, material slots are stable for the bundle's lifetime
    bundle->imageOffset    = 0;
    bundle->materialOffset = (int)scene->numMaterials;

    s32 appended = TextureSystem_AppendBundle(&scene->textureSystem, bundle, staging);
    TextureSystem_ReleaseTextures(staging, (u32)bundle->numImages);
    ArenaRestore(&GlobalArena, mark);
    if (!appended)
        return INVALID_BUNDLE;

    RenderSet* set = skinned ? &scene->skinnedSet : &scene->surfaceSet;
    u32 renderIdx = RenderSet_AddSceneBundle(set, bundle);
    if (renderIdx == INVALID_BUNDLE)
    {
        AX_ERROR("render set bundle registration failed: %s", path);
        return INVALID_BUNDLE;
    }

    u32 bundleIdx = scene->numBundles++;
    scene->bundlePaths[bundleIdx]     = path;
    scene->bundles[bundleIdx]         = bundle;
    scene->bundleRenderIdx[bundleIdx] = renderIdx;
    scene->bundleSkinned[bundleIdx]   = skinned;
    scene->numMaterials += (u32)bundle->numMaterials;
    return bundleIdx;
}

u32 Scene_RemoveBundle(Scene* scene, u32 bundleIdx)
{
    if (bundleIdx >= scene->numBundles) return 0;

    SceneBundle* bundle = scene->bundles[bundleIdx];
    bool skinned = scene->bundleSkinned[bundleIdx] != 0;
    RenderSet* set = skinned ? &scene->skinnedSet : &scene->surfaceSet;
    u32 removedRenderIdx = scene->bundleRenderIdx[bundleIdx];

    u32 removedEntities = RenderSet_RemoveSceneBundle(set, removedRenderIdx);
    TextureSystem_RemoveBundle(&scene->textureSystem, bundle);

    // the render set compacts its bundle list, later indices of the same set shift down
    for (u32 i = 0; i < scene->numBundles; i++)
    {
        if (i == bundleIdx) continue;
        if ((scene->bundleSkinned[i] != 0) == skinned && scene->bundleRenderIdx[i] > removedRenderIdx)
            scene->bundleRenderIdx[i]--;
    }

    // the bundle data itself stays allocated: vertices live in the shared mega buffers
    // and skinned bundles registered animation data that is not reclaimed yet
    for (u32 i = bundleIdx + 1; i < scene->numBundles; i++)
    {
        scene->bundlePaths[i - 1]     = scene->bundlePaths[i];
        scene->bundles[i - 1]         = scene->bundles[i];
        scene->bundleRenderIdx[i - 1] = scene->bundleRenderIdx[i];
        scene->bundleSkinned[i - 1]   = scene->bundleSkinned[i];
    }
    scene->numBundles--;
    scene->renderDataDirty = 1;
    return removedEntities;
}

s32 Scene_RepackTextures(Scene* scene)
{
    TextureSystem_ResetPacking(&scene->textureSystem);

    for (u32 b = 0; b < scene->numBundles; b++)
    {
        SceneBundle* bundle = scene->bundles[b];
        if (bundle->numImages <= 0 && bundle->numMaterials <= 0) continue;

        ArenaMark mark = ArenaSave(&GlobalArena);
        Texture* staging = (Texture*)ArenaAllocZero(&GlobalArena, MAX_SCENE_TEXTURES * sizeof(Texture));
        if (!LoadBundleImagesFromCache(scene->bundlePaths[b], staging, bundle->numImages))
        {
            AX_ERROR("scene image load failed during repack: %s", scene->bundlePaths[b]);
            ArenaRestore(&GlobalArena, mark);
            return 0;
        }

        s32 appended = TextureSystem_AppendBundle(&scene->textureSystem, bundle, staging);
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

    bool skinned = scene->bundleSkinned[bundleIdx] != 0;
    RenderSet* set = skinned ? &scene->skinnedSet : &scene->surfaceSet;
    u32 added = RenderSet_AddScene(set, scene->bundleRenderIdx[bundleIdx], position, rotation, scale, skinned);
    if (added) scene->renderDataDirty = 1;
    return added;
}

void Scene_ClearEntities(Scene* scene)
{
    RenderSet_ClearEntities(&scene->skinnedSet);
    RenderSet_ClearEntities(&scene->surfaceSet);
    scene->renderDataDirty = 1;
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
