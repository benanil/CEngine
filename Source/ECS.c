
#include "Include/ECS.h"
#include "Include/Random.h"
#include "Math/Half.h"
#include "Math/Matrix.h"

ECS ecs;

void ECS_Init()
{
    ecs.numEntities = 0u;
    ecs.numGroups   = 0u;
    ecs.numBundles  = 0u;
    Entity* entities = ecs.entities;
    u32 packedScale = PackXY11Z10UnormToU32(VecSet1(0.1f));

    for (s32 i = 0; i < MAX_ENTITY; i++)
    {
        u64 hash = MurmurHash(i + 123);
        entities[i].position = VecMulf(VecSetR(f1_(i & 63), 0.0f, f1_(i >> 6), 0.0f), 1.5f);
        entities[i].rotation = hash & 0xFFFF0000FFFF0000ull;  // x=0, y=random, z=0, w=random
        entities[i].scale = packedScale;
    }
}

void ECS_Update(float delta_time)
{

}

u32 AddSceneBundle(const SceneBundle* sceneBundle)
{
    if (ecs.numBundles >= MAX_BUNDLES) return ~0;
    u32 bundleIdx = ecs.numBundles++;
    ecs.bundles[bundleIdx] = sceneBundle;
    
    u32 primitiveStart = ecs.numGroups;
    u32 numMeshes = sceneBundle->numMeshes;
    for (u32 m = 0; m < numMeshes; m++)
    {
        const AMesh* mesh = sceneBundle->meshes + m;
        u32 numPrimitives = mesh->numPrimitives;
        
        for (u32 p = 0; p < numPrimitives; p++)
        {
            const APrimitive* primitive = mesh->primitives + p;
            u32 primitiveIdx = ecs.numGroups++;
            PrimitiveGroup* group = ecs.primitiveGroups + primitiveIdx;
            group->valid         = 1;
            group->numEntities   = 0;
            group->capacity      = 0;
            group->boneStart     = sceneBundle->boneOffset;
            group->numIndices    = primitive->numIndices;
            group->indexOffset   = primitive->indexOffset;
            group->entityOffset  = 0;
            Float4ToHalf4V((u64*)group->aabbMin, VecLoad(primitive->min));
            Float4ToHalf4V((u64*)group->aabbMax, VecLoad(primitive->max));
            
            if (primitiveIdx >= ecs.numGroups) ecs.numGroups = primitiveIdx + 1;
        }
    }
    
    ecs.bundleRange[bundleIdx].start = primitiveStart;
    ecs.bundleRange[bundleIdx].count = ecs.numGroups - primitiveStart;
    return bundleIdx;
}

static u32 LeaveSpaceForEntities(u32 primitiveIdx, u32 numAdded)
{
    PrimitiveGroup* group = &ecs.primitiveGroups[primitiveIdx];
    u32 entityStart = group->entityOffset;
    u32 entityEnd = entityStart + group->numEntities;
    
    for (s32 i = ecs.numGroups - 1; i > (s32)primitiveIdx; i--)
    {
        PrimitiveGroup* g = &ecs.primitiveGroups[i];
        u32 srcOffset = g->entityOffset;
        u32 dstOffset = srcOffset + g->numEntities;
        
        for (u32 j = 0; j < numAdded; j++)
        {
            ecs.entities[dstOffset + j] = ecs.entities[srcOffset + j];
        }
        g->entityOffset = dstOffset;
    }
    
    ecs.numEntities += numAdded;
    return entityStart;
}

u32 AddEntities(u32 primitiveIdx, u32 numAdded, const Entity* data)
{
    u32 startIdx = LeaveSpaceForEntities(primitiveIdx, numAdded);
    PrimitiveGroup* group = &ecs.primitiveGroups[primitiveIdx];
    for (u32 i = 0; i < numAdded; i++)
    {
        ecs.entities[group->entityOffset + i] = data[i];
    }
    group->numEntities += numAdded;
    group->capacity = group->entityOffset + group->numEntities;
    return startIdx;
}

u32 AddEntity(u32 primitiveIdx, const Entity* data)
{
    return AddEntities(primitiveIdx, 1, data);
}

static void RecursePushScene(Range range, const SceneBundle* bundle, s32 nodeIndex, v128f position, v128f rotation, v128f scale)
{
    const ANode* node = bundle->nodes + nodeIndex;
    const AMesh* mesh = bundle->meshes + node->index;
    
    if (node->type == 0 && node->index != -1)
    for (s32 j = 0; j < mesh->numPrimitives; ++j)
    {
        Entity entity;
        entity.position = position;
        PackQuaternionS16Norm(rotation, &entity.rotation);
        entity.scale = PackXY11Z10UnormToU32(scale);
        AddEntity(range.start + j, &entity);
    }
    
    for (s32 i = 0; i < node->numChildren; i++)    
    {
        v128f localPos = VecMul(VecLoad(node->translation), scale);
        v128f localRot = VecLoad(node->rotation);
        localPos = QMulVec3V(localPos, rotation);
        localPos = VecAdd(localPos, position);
        localRot = QMul(localRot, rotation);
        v128f localScale = VecLoad(node->scale);
        RecursePushScene(range, bundle, node->children[i], localPos, localRot, localScale);
    }    
}

u32 AddScene(u32 bundleIdx, v128f position, v128f rotation, v128f scale)
{
    Range range = ecs.bundleRange[bundleIdx];
    const SceneBundle* bundle = ecs.bundles[bundleIdx];
    if (bundle->numScenes == 0)
    {
        RecursePushScene(range, bundle, 0, position, rotation, scale);
        return 1;
    }
    AScene defaultScene = bundle->scenes[bundle->defaultSceneIndex];
    for (s32 i = 0; i < defaultScene.numNodes; i++)
        RecursePushScene(range, bundle, defaultScene.nodes[i], position, rotation, scale);
    
    return 1;
}

u32 GetNumPrimitivesInBundle(u32 bundleIdx)
{
    return ecs.bundleRange[bundleIdx].count;
}

void CompactEntities()
{

}


u32 RemoveEntities(u32 groupIdx, u32 startIdx, u32 count)
{
    return 0;
}

u32 RemoveEntity(u32 entityIdx, u32 groupIdx)
{
    return 0;
}

u32 RemoveSceneBundle(u32 bundleIdx)
{
    if (bundleIdx >= MAX_BUNDLES) return 0;
    
    ecs.bundles[bundleIdx] = NULL;
    
    Range range = ecs.bundleRange[bundleIdx];
    u32 totalRemoved = 0;
    
    for (u32 i = range.start; i < range.start + range.count; i++)
    {
        totalRemoved += ecs.primitiveGroups[i].numEntities;
        ecs.primitiveGroups[i].valid = 0;
    }
    
    ecs.numEntities -= totalRemoved;
    return totalRemoved;
}