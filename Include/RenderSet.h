///////////////////////////////////////////////////////////
//  sparse dense set optimized for rendering
//  todo typedef for entity id's
#ifndef RENDER_SET_H
#define RENDER_SET_H

#include "GLTFParser.h"
#include "SIMD.h"
#include "../Math/Half.h" 
#include "RenderLimits.h"

#define INVALID_ENTITY  (~0u)
#define INVALID_GROUP   (~0u)
#define INVALID_BUNDLE  (~0u)


#define ENTITY_MAX_SCALE 10.0f

typedef enum EntityFlags_
{
    EntityFlags_None            = 0,
    EntityFlags_ColliderEnabled = 1 << 0,
    EntityFlags_Transparent     = 1 << 1,
    EntityFlags_NoMesh          = 1 << 2,
    EntityFlags_Hidden          = 1 << 3
} EntityFlags;


// sparseId | (generation << 24)
typedef u32 EntityID;

typedef struct Entity_
{
    v128f position;     // last 32bit unused
    u64   rotation;     
    u64   scale;        // xyz16-last16 bit unused
    u32   primitiveIdx; // todo make it 16 bit
    u32   sparseIdx;
    // 24 bit parent sparseIdx, last byte generation
    u32   parentIdx;
    u16   material;
    u16   flags; // EntityFlags
} Entity;

typedef struct Range_
{
    u32 start;
    u32 count;
} Range;

typedef struct PrimitiveGroup_ PrimitiveGroup;
typedef struct PrimitiveGroupGPU_ PrimitiveGroupGPU;
typedef struct PrimitiveGroupLOD_ PrimitiveGroupLOD;

struct Scene_;

typedef struct RenderSet_
{
    Entity*             entities;
    u32*                sparseID; // sparse to dense
    u64*                sparseSlots; // bitset for used sparse id's
    
    PrimitiveGroup*     primitiveGroups;
    Range*              bundlePrimRange;
    const SceneBundle** bundles;
    u64*                bundleSlots; // bitset for used bundle slots, 1 means occupied

    u32 maxEntities;
    u32 maxGroups;
    u32 maxBundles;
    u32 numEntities;
    u32 numGroups;
    u32 numBundles; // watermark: highest used bundle slot + 1, slots below may be empty
    u32 skinned;
    struct Scene_* hookScene;
} RenderSet;

struct PrimitiveGroup_
{
    v128f aabbMin;
    v128f aabbMax;
    u32 lodIndexOffset[3];
    u16 entityOffset, numEntities;
    u32 lodNumIndices[3];
    u16 capacity, meshIndex;
    u32 lodVertexOffset[3];
    u16 primitiveIndex, materialIndex; 
    u32 lodNumVertices[3];
    u16 bundleIdx, padding0;
};

STATIC_ASSERT(sizeof(PrimitiveGroup) == 96, "PrimitiveGroup CPU/GPU stride mismatch");

struct PrimitiveGroupGPU_
{
    u32 aabbMinEntity[4]; // xyz float bits, w entityOffset | (numEntities << 16)
    u32 aabbMaxMaterial[4]; // xyz float bits, w materialIndex
};

STATIC_ASSERT(sizeof(PrimitiveGroupGPU) == 32, "PrimitiveGroupGPU must stay 32 bytes");

struct PrimitiveGroupLOD_
{
    u32 lodIndexOffset[4];
    u32 lodNumIndices[4];
    u32 lodVertexOffset[4];
    u32 lodNumVertices[4];
};

STATIC_ASSERT(sizeof(PrimitiveGroupLOD) == 64, "PrimitiveGroupLOD must stay 64 bytes");

static inline void PrimitiveGroup_SetAABB(PrimitiveGroup* group, v128f aabbMin, v128f aabbMax)
{
    group->aabbMin = aabbMin;
    group->aabbMax = aabbMax;
}

static inline EntityID MakeEntityID(u32 sparse, u32 gen) {
    return sparse | (gen << 24);
}

static inline u32 GetEntityGen(const Entity* entity) {
    return entity->parentIdx >> 24;
}

static inline u32 GetEntityID(const Entity* entity) {
    return entity->sparseIdx | (entity->parentIdx & 0xFF000000u);
}

static inline void SetEntityGen(Entity* entity, u32 gen) {
    entity->parentIdx &= 0x00ffffffu;
    entity->parentIdx |= (u32)((u8)(gen)) << 24;
}

static inline void NextEntityGen(Entity* entity) {
    SetEntityGen(entity, GetEntityGen(entity) + 1);
}

static inline Entity* RenderSet_GetEntity(RenderSet* rs, EntityID entityID)
{
    if (AX_UNLIKELY(entityID == INVALID_ENTITY)) return NULL;
    u32 denseIdx = rs->sparseID[entityID & 0x00FFFFFFu];
    if (denseIdx == INVALID_ENTITY) return NULL; // unmapped entity likely deleted
    Entity* entity = &rs->entities[denseIdx];
    bool expired = GetEntityGen(entity) != (entityID >> 24);
    return expired ? NULL : entity;
}

v128f EntityUnpackScale01(u64 packed);
v128f EntityUnpackWorldScale(u64 packed);
u64   EntityPackWorldScale(v128f scale);
u64   EntityPackUniformWorldScale(f32 scale);

v128f RenderSet_GroupLocalCenter(const PrimitiveGroup* group);
v128f RenderSet_EntityBoundsCenter(const PrimitiveGroup* group, const Entity* entity, v128f rotation, v128f worldScale);

bool  RenderSet_ResolveEntity(RenderSet* set, u32 groupIdx, u32 entityIdx,
                             PrimitiveGroup** outGroup, Entity** outEntity);

// spawn order ordinal of a mesh node: static sparse ids are allocated sequentially per mesh node.
s32   RenderSet_NodeSpawnOrdinal(const SceneBundle* bundle, s32 nodeIdx);
bool  RenderSet_FindNodeEntity(const RenderSet* set, Range range, u32 meshIndex, u32 sparseIdx,
                               u32* outGroup, u32* outEntity);
u32   RenderSet_AllocateSparseID(RenderSet* set);
u32   RenderSet_AllocateSparseIDRange(RenderSet* set, int count);
void  RenderSet_FreeSparseID(RenderSet* set, u32 sparseIdx);
void  RenderSet_FreeSparseIDRange(RenderSet* set, u32 sparseIdx, u32 count);
u32   RenderSet_CountTriangles(const RenderSet* set);

// debug validation for insertion/upload invariants. out: false when corruption is found.
bool  RenderSet_Validate(const RenderSet* set, const char* label);

void  RenderSet_Destroy(RenderSet* set);
void  RenderSet_InitSet(RenderSet* set, u32 maxEntities, u32 maxGroups, u32 maxBundles, bool skinned);
void  RenderSet_SetHookScene(RenderSet* set, struct Scene_* scene);

// materialOffset is the scene's gpu material slot base of the bundle.
// out: groupIdx, ~0u outherwise
u32   RenderSet_AddSceneBundle(RenderSet* set, const SceneBundle* sceneBundle, u32 materialOffset);
// returns: root node, first entity that is added: sparseID | (generation << 24)
//    since always parentID < childID look at: SceneNormalize.c EmitRemappedNode
EntityID RenderSet_AddScene(RenderSet* set, u32 bundleIdx, v128f position, v128f rotation, v128f scale, bool wantSkinned);

EntityID RenderSet_AddEntity(RenderSet* set, u32 primitiveIdx, const Entity* data);

EntityID RenderSet_AddEntities(RenderSet* set, u32 primitiveIdx, u32 numAdded, const Entity* data);

void  RenderSet_Clear(RenderSet* set);

// removes all entities, keeps registered bundles and primitive groups
void  RenderSet_ClearEntities(RenderSet* set);

u32   RenderSet_RemoveEntity(RenderSet* set, u32 groupIdx, u32 localEntityIdx);

u32   RenderSet_RemoveEntities(RenderSet* set, u32 groupIdx, u32 localStartIdx, u32 count);

u32   RenderSet_RemoveSceneBundle(RenderSet* set, u32 bundleIdx);

void  RenderSet_CompactEntities(RenderSet* set);

// define these somewhere
void RenderSet_AddEntitiesCallback(RenderSet* set, u32 groupIdx, u32 localStartIdx, u32 count);
void RenderSet_RemoveRangeCallback(RenderSet* set, u32 groupIdx, u32 localStartIdx, u32 count);
void RenderSet_RemoveGroupsCallback(RenderSet* set, u32 firstGroup, u32 groupCount);
void RenderSet_ClearEntitiesCallback(RenderSet* set);


#endif
