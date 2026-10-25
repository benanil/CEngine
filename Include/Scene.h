#ifndef SCENE_H
#define SCENE_H

#include "RenderSet.h"
#include "TextureSystem.h"
#include "Animation.h"
#include "Physics.h"

#define MAX_SCENE_BUNDLES  1024u
#define MAX_SCENE_LIGHTS   256u
#define MAX_THROWN_SPHERES 512u

// one bundle registered in a scene
typedef struct SceneBundleRef_
{
    char*        path;           // bundle cache owned string
    SceneBundle* bundle;
    u32          renderIdx;      // bundle index inside its render set
    u32          materialOffset; // gpu material slot base of the bundle in this scene
    u32          animOffset;     // first animation of the bundle inside the scene's animation system
    u16          skinned;
    u16          isRuntime;
    AnimationBundleAlloc animAlloc;
    u64          cacheKey;       // gBundleCache key (path hash); never store the entry pointer,
                                 // the cache map relocates entries on grow and on swap-with-last erase
} SceneBundleRef;

typedef struct BundleCacheEntry
{
    SceneBundle* bundle;
    u32          refCount;
    // raw geometry heap pointers, needed to free the mega buffer ranges.
    // NULL when the geometry doesn't live in the mega buffers (fbx path)
    void*        vertexHeapPtr;
    void*        indexHeapPtr;
    // blas arrays of every primitive, built after load (BVH.c). gpu shareable layout,
    // primitives point into them through APrimitive.bvhNodeIndex
    void*        bvhNodes;
    void*        bvhTris;
    int          numBvhNodes;
    int          numBvhTris;
} BundleCacheEntry;

// a scene owns one render set for skinned meshes, one for static geometry, their gpu
// buffers, its own texture system and animation system. all gpu resources stay resident,
// activating and deactivating scenes only changes the active list
typedef struct Scene_
{
    RenderSet        skinnedSet;
    RenderSet        surfaceSet;
    RenderSetBuffers skinnedBuffers;
    RenderSetBuffers surfaceBuffers;
    DrawBuffers      transparentDrawBuffers;

    TextureSystem    textureSystem;
    AnimationSystem  animSystem;
    PhysicsSystem    physics;

    bool isFoliageScene;
    f32  ambientBoost; // default 1.0f, max 4.0f

    SceneBundleRef*  bundleRefs;   // fixed MAX_SCENE_BUNDLES allocation. bundle indices are stable
                                   // handles, removing one never shifts the others
    u64*             usedBundleBits;  // MAX_SCENE_BUNDLES bits, 1 means occupied
    u64*             materialSlots;// MAX_GPU_MATERIALS bits, 1 means occupied

    LightGPU*        lights;    // tlsf, MAX_SCENE_LIGHTS, authored lights pushed by Scene_SubmitLights
    u32              numLights;

    u32 numBundles;      // watermark: highest used bundle slot + 1, slots below may be empty
    u32 numMaterials;    // material slot watermark, offsets stay stable while occupied
    u32 renderDataDirty; // static render set buffers need re-upload, consumed by Render
    u32 texturesBaked;   // pages came from a baked atlas, packer state is unusable until a repack
} Scene;

// One staged bundle load: mesh acquire (BundleCacheAcquire) plus cached-texture decode/GPU
// upload (LoadBundleImagesFromCache). Touches only the bundle cache and this stage's own
// buffer, so it's safe off the main thread (ParallelFor); Scene_AddBundleFinalize then
// publishes it serially on the main thread (see Foliage_Init, SceneSerializer_LoadBundles).
// Shared by Scene_AddBundleStage (mesh+textures) and Scene_AddBundleBakedStage (mesh only,
// materialOffset given directly) - skinned/staging unused on the baked path, materialOffset
// unused on the normal path.
typedef struct SceneBundleStage_
{
    SceneBundle* bundle;
    u64          cacheKey;
    u32          materialOffset;  // Scene_AddBundleBaked* path only
    bool         skinned;         // Scene_AddBundle* path only
    bool         loaded;          // false when the load failed; Finalize/Abort are still safe to call
    char*        path;            // bundle cache owned string, stable for the entry's lifetime
    Texture      staging[1024];   // Scene_AddBundle* path only
} SceneBundleStage;

// one serialized render set entity, packed transform forms round trip exactly
typedef struct SceneEntRecord_
{
    u64   rotation;
    u64   scale;
    float position[3];
    u32   primGroupIdx;
    u32   sparseIdx;
    u32   flags;
    u32   parent;
    u32   subNodeOffset;
    u32   subNodeCount;
} SceneEntRecord;

// internal no need to know its content
// lifetime ends after scene load
typedef struct SceneFileData_
{
    f32 sunYaw, sunPitch;
    u32 atlasLayers[TextureClass_Count];
    char atlasPath[TextureClass_Count][1024];
    SceneBundleStage* stages;
    bool              baked;

    u32           numBundles;
    char*         bundlePaths;       // numBundles * 1024
    u32*          bundleSkinned;
    u32*          bundleMaterialOff;
    SceneBundle** runtimeBundles;

    TextureDescriptor* descriptors;
    u32 numDescriptors;

    MaterialGPU* materials;
    u32 materialWatermark;

    LightGPU* lights;
    u32 numLights;

    SceneEntRecord* entities[2]; // 0 surface, 1 skinned
    u32 numEntities[2];

    ScenePhysicsRecord* physics; // surface entities that override the default collider
    u32 numPhysics;
    u16* subNodes[2];
} SceneFileData;

typedef enum SceneAsyncOp_
{
    SceneAsyncOp_None = 0,
    SceneAsyncOp_ImportMesh,
    SceneAsyncOp_OpenScene
} SceneAsyncOp;

typedef struct SceneAsyncRequest_ SceneAsyncRequest;

typedef void (*SceneAsyncRequestCallback)(SceneAsyncRequest* request);

struct SceneAsyncRequest_
{
    SceneAsyncOp op;
    SDL_AtomicInt done;
    Scene* scene;
    SceneFileData* sceneFileData;
    SceneBundleStage* sceneBundleStage;
    SceneAsyncRequestCallback callback;
    s32    result;
    u32    bundleIdx;
    v128f  position;
    v128f  rotation;
    v128f  scale;
    char   path[512];
};
typedef struct SceneStageRangeCtx_
{
    SceneBundleStage* stages;
    bool baked;
} SceneStageRangeCtx;

// scenes the renderer draws each frame, in activation order
extern Scene* g_ActiveScene;

void Scene_Init(Scene* scene);
void Scene_Destroy(Scene* scene);

// engine-owned active scene helpers. These are usable without editor code and keep
// the active .scene path in Scene.c.
Scene* Scene_NewActive(void);
// data: null or call SceneSerializer_LoadStage
Scene* Scene_OpenActive(const char* path, SceneFileData* data);
s32    Scene_SaveActive(void);
s32    Scene_SaveActiveAs(const char* path);
const char* Scene_GetActivePath(void);

// adds the scene to the rendered scenes. out: 0 when the active list is full.
// note: the animated vertex pool is shared and indexed by sparse id, only one
// active scene should contain skinned entities at a time
s32 Scene_Activate(Scene* scene);

void Scene_Deactivate(Scene* scene);

// makes this the only rendered scene. out: 0 on failure
s32 Scene_MakeActive(Scene* scene);

// out: the first active scene, NULL when none
Scene* GetActiveScene(void);

const char* GetActiveScenePath();

bool Entity_IsTransparent(const Entity* entity);

// per-frame scene tick: pumps async loads and steps physics for the active scene
void Scene_Update(float deltaTime);

// returns stable index no need to wory about index will be invalid
u32 Scene_AddBundle(Scene* scene, SceneBundle* bundle, const char* name);
// if bundle is already added this will not add again
// most of the time use this otherwise if you want to duplicate use Scene_AddBundle
u32 Scene_AddBundleCached(Scene* scene, SceneBundle* bundle, const char* name);
// returns index of bundle if exists otherwise ~0
u32 Scene_BundleIdx(Scene* scene, SceneBundle* bundle);
u32 Scene_BundleFindFromPath(Scene* scene, const char* path);
// loads a gltf bundle, packs its textures into the scene's texture system and registers
// its primitives to the matching render set. bundles are shared through a global cache
// keyed by path, repeated adds of the same path reuse the resident mesh data.
// out: scene bundle index, INVALID_BUNDLE otherwise
u32 Scene_AddBundleFromPath(Scene* scene, const char* path);

// Async and ParallelFor compatible
// userdata: SceneStageRangeCtx
void SceneStageRange(u32 begin, u32 end, void* userData);

SceneBundleStage* Scene_AddBundleFromPathStage(Scene* scene, const char* path);

// caller sets stage->storedPath (+ stage->skinned) before calling; result lands in
// stage->loaded (false on failure, Finalize/Abort still safe to call then). void*, single
// argument so callers fan this out with ParallelFor via a small per-index range wrapper.
void Scene_AddBundleStage(void* stage, bool baked);
void StageBundleRange(u32 begin, u32 end, void* stages);

// Main-thread only: publishes a staged load into the scene (GPU texture packing, material/
// animation bookkeeping, render set registration) - the part that mutates shared scene state
// and so can't run off-thread. Releases the cache reference Scene_AddBundleStage took either
// way. out: scene bundle index, INVALID_BUNDLE on failure or when the stage itself had failed
u32 Scene_AddBundleFinalize(Scene* scene, SceneBundleStage* stage);

u32 Scene_AddBundleBakedFinalize(Scene* scene, SceneBundleStage* stage);
// Drops a staged load without adding it to any scene (e.g. caller decided not to use it).
// No-op when the stage failed to load. Safe from any thread.
void Scene_AddBundleStageAbort(SceneBundleStage* stage);

// Scene_AddBundle with the skinned flag detected from the bundle's skin data
u32 Scene_AddBundleAuto(Scene* scene, const char* path);

void Scene_ReleaseBundlePeek(const char* path);

// registers an already loaded bundle without touching the texture system, used by the
// baked scene load path where the pages are restored separately.
// out: scene bundle index, INVALID_BUNDLE otherwise
u32 Scene_AddBundleBaked(Scene* scene, const char* path, u32 materialOffset);

void Scene_AddBundleBakedStageAbort(SceneBundleStage* stage);

u32 Scene_DefaultAnimation(const Scene* scene, u32 bundleIdx);
u32 Scene_FindBundleForRenderGroup(const Scene* scene, bool skinned, u32 groupIdx);

// pushes the active scene's authored lights to the renderer, call once per frame
void Scene_SubmitLights(void);

// removes the bundle's entities and primitive groups from the render set and clears its
// material slots. page space leaks until Scene_RepackTextures. scene bundle indices after
// bundleIdx shift down by one. out: number of entities removed
u32 Scene_RemoveBundle(Scene* scene, u32 bundleIdx);

// rebuilds the texture pages from the remaining bundles, reclaiming the space of removed
// ones. material offsets are preserved so baked group indices stay valid. stalls on io
// and transcode, do not call mid frame. out: 0 on failure
s32 Scene_RepackTextures(Scene* scene);

// instances the bundle node hierarchy with the given transform. out: number of entities added
SparseData* Scene_Spawn(Scene* scene, u32 bundleIdx, v128f position, v128f rotation, v128f scale, EntityFlags flags);

void Scene_ClearEntities(Scene* scene);

// Copies the resident cache entry for a scene render bundle into *out (looked up by key under the
// cache lock). out: false when the bundle is not resident. The copied bvhNodes/bvhTris pointers stay
// valid while the bundle is referenced; never hold the entry pointer itself, the map relocates it.
bool FindCacheForSceneBundle(const Scene* scene, u32 bundleIdx, BundleCacheEntry* out);

bool SceneAsyncBegin(SceneAsyncOp op, const char* path, const char* taskName, SceneAsyncRequestCallback callback);

static inline bool Scene_SparseDataIsSkinned(const Scene* scene, const SparseData* sparse)
{
    SparseData* skinnedSparse = scene->skinnedSet.sparseData;
    return (sparse >= skinnedSparse) && 
           (sparse < (skinnedSparse + scene->skinnedSet.maxEntities));
}

static inline bool Scene_EntityIsSkinned(const Scene* scene, const Entity* entity)
{
    Entity* entityStart = scene->skinnedSet.entities;
    return (entity >= entityStart) && 
        (entity < (entityStart + scene->skinnedSet.maxEntities));
}

static inline RenderSet* Scene_GetEntityRenderSet(Scene* scene, EntityID id)
{
    return EntityIDIsSkinned(id) ? &scene->skinnedSet : &scene->surfaceSet;
}

static inline EntityID EntityIDFromSparseData(const Scene* scene, const SparseData* sparse)
{
    bool isSkinned = Scene_SparseDataIsSkinned(scene, sparse);
    const RenderSet* rs = isSkinned ? &scene->skinnedSet : &scene->surfaceSet;
    return (u32)(sparse - rs->sparseData) | (sparse->gen << 24) | ((u32)isSkinned << 31);
}

static inline Entity* Scene_GetEntity(const Scene* scene, EntityID id)
{
    const RenderSet* rs = (id >> 31) ? &scene->skinnedSet : &scene->surfaceSet;
    return RenderSet_GetEntity(rs, id);
}

static inline void Scene_RemoveEntityRec(Scene* scene, RenderSet* set, EntityID id)
{
    u32 sparseId = EntityIDSparse(id);
    const SparseData* sparse = set->sparseData + sparseId;
    for (s32 c = 0; c < sparse->subNodeCount; c++)
    {
        s32 child = sparseId + set->subNodes[sparse->subNodeOffset + c];
        Scene_RemoveEntityRec(scene, set, EntityIDFromSparseData(scene, &set->sparseData[child]));
    }
    RenderSet_RemoveEntityStaged(set, sparseId);
}

static inline void Scene_RemoveEntity(Scene* scene, EntityID id)
{
    RenderSet* set = Scene_GetEntityRenderSet(scene, id);
    Scene_RemoveEntityRec(scene, set, id);
    RenderSet_CompactEntities(set);
}

static inline void Scene_AddPosRec(Scene* scene, Entity* entity, v128f offset)
{
    bool skinned = Scene_EntityIsSkinned(scene, entity);
    RenderSet* set = skinned ? &scene->skinnedSet : &scene->surfaceSet;
    u32 sparseId = EntityGetSparseID(entity);
    const SparseData* sparse = set->sparseData + sparseId;
    EntityAddPositionV(&set->entities[sparse->id], offset);
    if (!skinned) Entity_SyncPhysicsBody(scene, entity);
    
    for (s32 c = 0; c < sparse->subNodeCount; c++)
    {
        s32 child = sparseId + set->subNodes[sparse->subNodeOffset + c];
        Entity* entity = &set->entities[set->sparseData[child].id];
        Scene_AddPosRec(scene, entity, offset);
    }
}

static inline void Scene_RotateRec(Scene* scene, Entity* entity, Quaternion rotation)
{
    bool skinned = Scene_EntityIsSkinned(scene, entity);
    u32 sparseId = EntityGetSparseID(entity);
    RenderSet* set = skinned ? &scene->skinnedSet : &scene->surfaceSet;
    EntityAddRotation(entity, rotation);
    if (!skinned) Entity_SyncPhysicsBody(scene, entity);
    
    const SparseData* sparse = set->sparseData + sparseId;
    for (s32 c = 0; c < sparse->subNodeCount; c++)
    {
        s32 child = sparseId + set->subNodes[sparse->subNodeOffset + c];
        Entity* entity = &set->entities[set->sparseData[child].id];
        Scene_RotateRec(scene, entity, rotation);
    }
}

static inline void Scene_ScaleRec(Scene* scene, Entity* entity, v128f scale, bool root)
{
    bool skinned = Scene_EntityIsSkinned(scene, entity);
    u32 sparseId = EntityGetSparseID(entity);
    RenderSet* set = skinned ? &scene->skinnedSet : &scene->surfaceSet;
    if (!root) EntityMulScaleV(entity, scale);
    if (!skinned) Entity_SyncPhysicsBody(scene, entity);
    
    const SparseData* sparse = set->sparseData + sparseId;
    for (s32 c = 0; c < sparse->subNodeCount; c++)
    {
        s32 child = sparseId + set->subNodes[sparse->subNodeOffset + c];
        Entity* entity = &set->entities[set->sparseData[child].id];
        Scene_ScaleRec(scene, entity, scale, false);
    }
}

#endif // SCENE_H