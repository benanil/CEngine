#ifndef SCENE_H
#define SCENE_H

#include "RenderSet.h"
#include "TextureSystem.h"
#include "Animation.h"

#define MAX_SCENE_BUNDLES 1024u
#define MAX_ACTIVE_SCENES 2u

// a scene owns one render set for skinned meshes, one for static geometry, their gpu
// buffers, its own texture system and animation system. all gpu resources stay resident,
// activating and deactivating scenes only changes the active list
typedef struct Scene_
{
    RenderSet        skinnedSet;
    RenderSet        surfaceSet;
    RenderSetBuffers skinnedBuffers;
    RenderSetBuffers surfaceBuffers;
    TextureSystem    textureSystem;
    AnimationSystem  animSystem;

    const char*  bundlePaths[MAX_SCENE_BUNDLES];
    SceneBundle* bundles[MAX_SCENE_BUNDLES];
    u32          bundleRenderIdx[MAX_SCENE_BUNDLES]; // bundle index inside its render set
    u8           bundleSkinned[MAX_SCENE_BUNDLES];

    u32 numBundles;
    u32 numMaterials;    // material slot watermark, slots are stable and leak on removal
    u32 renderDataDirty; // static render set buffers need re-upload, consumed by Render
} Scene;

// scenes the renderer draws each frame, in activation order
extern Scene* g_ActiveScenes[MAX_ACTIVE_SCENES];
extern u32    g_NumActiveScenes;

void Scene_Init(Scene* scene);
void Scene_Destroy(Scene* scene);

// adds the scene to the rendered scenes. out: 0 when the active list is full.
// note: the animated vertex pool is shared and indexed by sparse id, only one
// active scene should contain skinned entities at a time
s32 Scene_Activate(Scene* scene);

void Scene_Deactivate(Scene* scene);

// loads a gltf bundle, packs its textures into the scene's texture system and registers
// its primitives to the matching render set. out: scene bundle index, INVALID_BUNDLE otherwise
u32 Scene_AddBundle(Scene* scene, const char* path, bool skinned);

// removes the bundle's entities and primitive groups from the render set and clears its
// material slots. page space leaks until Scene_RepackTextures. scene bundle indices after
// bundleIdx shift down by one. out: number of entities removed
u32 Scene_RemoveBundle(Scene* scene, u32 bundleIdx);

// rebuilds the texture pages from the remaining bundles, reclaiming the space of removed
// ones. material offsets are preserved so baked group indices stay valid. stalls on io
// and transcode, do not call mid frame. out: 0 on failure
s32 Scene_RepackTextures(Scene* scene);

// instances the bundle node hierarchy with the given transform. out: number of entities added
u32 Scene_Spawn(Scene* scene, u32 bundleIdx, v128f position, v128f rotation, v128f scale);

void Scene_ClearEntities(Scene* scene);

// makes this the only rendered scene. out: 0 on failure
s32 Scene_MakeActive(Scene* scene);

// out: the first active scene, NULL when none
Scene* Scene_GetActive(void);

#endif // SCENE_H
