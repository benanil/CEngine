#ifndef ENTITY_COMPONENT_SYSTEM
#define ENTITY_COMPONENT_SYSTEM

#include "GLTFParser.h"
#include "SIMD.h"
#include "../Math/Half.h" 

#define MAX_ENTITY UINT16_MAX
#define MAX_GROUP   (MAX_ENTITY >> 1)
#define MAX_BUNDLES (MAX_ENTITY >> 2)

#define INVALID_ENTITY  (~0u)
#define INVALID_GROUP   (~0u)
#define INVALID_BUNDLE  (~0u)

typedef struct Entity_
{
    v128f position;
    u64   rotation;
    u32   scale; // xyz10
    u32   sparseIdx;
} Entity;

typedef struct PrimitiveGroup_
{
    u32 entityOffset;
    u32 numEntities;
    u32 capacity;
    u32 boneStart;
    u32 numIndices;
    u32 indexOffset;
    u32 vertexOffset;
    u32 meshIndex;
    u32 primitiveIndex;
    u32 valid;
    f16 aabbMin[4];
    f16 aabbMax[4];
} PrimitiveGroup;

typedef struct Range_
{
    u32 start;
    u32 count;
} Range;

typedef struct ECS_
{
    Entity*             entities;
    u32*                sparseID;
    u32*                denseToPrimitiveIndex;
    
    PrimitiveGroup*     primitiveGroups;

    Range*              bundleRange;
    const SceneBundle** bundles;
    
    u32 maxEntities;
    u32 maxGroups;
    u32 maxBundles;
    u32 numEntities;
    u32 numGroups;
    u32 numBundles;
} ECS;

extern ECS ecsSkinned;
extern ECS ecsStatic;

void ECS_Init();
void ECS_InitSet(ECS* ecs, u32 maxEntities, u32 maxGroups, u32 maxBundles);

// out: groupIdx, ~0u outherwise
u32 AddSceneBundle(const SceneBundle* sceneBundle);
u32 ECS_AddSceneBundle(ECS* ecs, const SceneBundle* sceneBundle);
// out: entityBegin, entityCount
u32 AddScene(u32 bundleIdx, v128f position, v128f rotation, v128f scale);
u32 ECS_AddScene(ECS* ecs, u32 bundleIdx, v128f position, v128f rotation, v128f scale, bool wantSkinned);

u32 AddEntity(u32 primitiveIdx, const Entity* data);
u32 ECS_AddEntity(ECS* ecs, u32 primitiveIdx, const Entity* data);

u32 AddEntities(u32 primitiveIdx, u32 numAdded, const Entity* data);
u32 ECS_AddEntities(ECS* ecs, u32 primitiveIdx, u32 numAdded, const Entity* data);

u32 RemoveEntity(u32 entityIdx, u32 groupIdx);

u32 RemoveEntities(u32 groupIdx, u32 startIdx, u32 count);

u32 RemoveSceneBundle(u32 bundleIdx);

u32 GetNumPrimitivesInBundle(u32 bundleIdx);

void ECS_CompactEntities();

void ECS_Update(f32 delta_time);

#endif
