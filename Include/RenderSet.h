#ifndef RENDER_SET_H
#define RENDER_SET_H

#include "GLTFParser.h"
#include "SIMD.h"
#include "../Math/Half.h" 
#include "RenderLimits.h"

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

v128f RenderSet_UnpackEntityScale01(u32 packed);
v128f RenderSet_UnpackEntityWorldScale(u32 packed);
u32   RenderSet_PackEntityWorldScale(const f32 scale[3]);

typedef struct PrimitiveGroup_
{
    u32 entityOffset;
    u32 numEntities;
    u32 capacity;
    u32 animatedVertexOffset;
    u32 numIndices;
    u32 indexOffset;
    u32 vertexOffset;
    u32 meshIndex;
    u32 primitiveIndex;
    u32 materialIndex;
    u32 valid;
    u32 numVertices;
    float aabbMin[4];
    float aabbMax[4];
    u32 lodIndexOffset[4];
    u32 lodNumIndices[4];
    u32 lodVertexOffset[4];
    u32 lodNumVertices[4];
    u32 lodAnimatedVertexOffset[4];
} PrimitiveGroup;

v128f RenderSet_GroupLocalCenter(const PrimitiveGroup* group);
v128f RenderSet_EntityBoundsCenter(const PrimitiveGroup* group, const Entity* entity, v128f rotation, v128f worldScale);

typedef struct Range_
{
    u32 start;
    u32 count;
} Range;

typedef struct RenderSet_
{
    Entity*             entities;
    u32*                sparseID; // sparse to dense
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
    u32 nextSparseID;
    u32 skinned;
} RenderSet;

bool RenderSet_ResolveEntity(RenderSet* set, u32 groupIdx, u32 entityIdx,
                             PrimitiveGroup** outGroup, Entity** outEntity);

// spawn order ordinal of a mesh node: static sparse ids are allocated sequentially per mesh node.
s32  RenderSet_NodeSpawnOrdinal(const SceneBundle* bundle, s32 nodeIdx);
bool RenderSet_FindNodeEntity(const RenderSet* set, Range range, u32 meshIndex, u32 sparseIdx,
                              u32* outGroup, u32* outEntity);
u32  RenderSet_CountTriangles(const RenderSet* set);

void RenderSet_InitSet(RenderSet* set, u32 maxEntities, u32 maxGroups, u32 maxBundles, bool skinned);

// materialOffset is the scene's gpu material slot base of the bundle.
// out: groupIdx, ~0u outherwise
u32 RenderSet_AddSceneBundle(RenderSet* set, const SceneBundle* sceneBundle, u32 materialOffset);
// out: entityBegin, entityCount
u32 RenderSet_AddScene(RenderSet* set, u32 bundleIdx, v128f position, v128f rotation, v128f scale, bool wantSkinned);

u32 RenderSet_AddEntity(RenderSet* set, u32 primitiveIdx, const Entity* data);

u32 RenderSet_AddEntities(RenderSet* set, u32 primitiveIdx, u32 numAdded, const Entity* data);

void RenderSet_Clear(RenderSet* set);

// removes all entities, keeps registered bundles and primitive groups
void RenderSet_ClearEntities(RenderSet* set);

u32 RenderSet_RemoveEntity(RenderSet* set, u32 groupIdx, u32 localEntityIdx);

u32 RenderSet_RemoveEntities(RenderSet* set, u32 groupIdx, u32 localStartIdx, u32 count);

u32 RenderSet_RemoveSceneBundle(RenderSet* set, u32 bundleIdx);

void RenderSet_CompactEntities(RenderSet* set);


#endif
