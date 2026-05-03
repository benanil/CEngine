
#include "Include/ECS.h"
#include "Include/Memory.h"
#include "Include/Random.h"
#include "Math/Half.h"
#include "Math/Matrix.h"
#include "Math/Bitpack.h"

ECS ecsSkinned;
ECS ecsStatic;

void ECS_InitSet(ECS* ecs, u32 maxEntities, u32 maxGroups, u32 maxBundles)
{
    MemsetZero(ecs, sizeof(*ecs));
    ecs->maxEntities = maxEntities;
    ecs->maxGroups   = maxGroups;
    ecs->maxBundles  = maxBundles;

    ecs->entities                = (Entity*)AllocZeroTLSFGlobal(maxEntities, sizeof(Entity));
    ecs->sparseID                = (u32*)AllocateTLSFGlobal(maxEntities * sizeof(u32));
    ecs->denseToPrimitiveIndex   = (u32*)AllocZeroTLSFGlobal(maxEntities, sizeof(u32));
    ecs->primitiveGroups         = (PrimitiveGroup*)AllocZeroTLSFGlobal(maxGroups, sizeof(PrimitiveGroup));
    ecs->bundleRange             = (Range*)AllocZeroTLSFGlobal(maxBundles, sizeof(Range));
    ecs->bundles                 = (const SceneBundle**)AllocZeroTLSFGlobal(maxBundles, sizeof(SceneBundle*));

    for (u32 i = 0; i < maxEntities; i++)
        ecs->sparseID[i] = INVALID_ENTITY;
}

void ECS_Init()
{
    ECS_InitSet(&ecsSkinned, MAX_ENTITY, MAX_GROUP, MAX_BUNDLES);
    ECS_InitSet(&ecsStatic,  MAX_ENTITY, MAX_GROUP, MAX_BUNDLES);
}

u32 ECS_AddSceneBundle(ECS* ecs, const SceneBundle* sceneBundle)
{
    if (ecs->numBundles >= ecs->maxBundles) return INVALID_BUNDLE;

    u32 bundleIdx = ecs->numBundles++;
    ecs->bundles[bundleIdx] = sceneBundle;
    u32 primitiveStart = ecs->numGroups;

    for (u32 m = 0; m < (u32)sceneBundle->numMeshes; m++)
    {
        const AMesh* mesh = sceneBundle->meshes + m;
        for (u32 p = 0; p < (u32)mesh->numPrimitives; p++)
        {
            if (ecs->numGroups >= ecs->maxGroups) return INVALID_BUNDLE;

            const APrimitive* primitive = mesh->primitives + p;
            u32 primitiveIdx = ecs->numGroups++;
            PrimitiveGroup* group = ecs->primitiveGroups + primitiveIdx;
            group->valid          = 1;
            group->numEntities    = 0;
            group->capacity       = 0;
            group->boneStart      = 0;
            group->numIndices     = (u32)primitive->numIndices;
            group->indexOffset    = (u32)primitive->indexOffset;
            group->vertexOffset   = 0;
            group->meshIndex      = m;
            group->primitiveIndex = p;
            group->entityOffset   = ecs->numEntities;
            Float4ToHalf4V((u64*)group->aabbMin, VecLoad(primitive->min));
            Float4ToHalf4V((u64*)group->aabbMax, VecLoad(primitive->max));
        }
    }

    ecs->bundleRange[bundleIdx].start = primitiveStart;
    ecs->bundleRange[bundleIdx].count = ecs->numGroups - primitiveStart;
    return bundleIdx;
}

u32 AddSceneBundle(const SceneBundle* sceneBundle)
{
    return ECS_AddSceneBundle(&ecsSkinned, sceneBundle);
}

static u32 FindPrimitiveGroup(ECS* ecs, Range range, u32 meshIndex, u32 primitiveIndex)
{
    for (u32 i = range.start; i < range.start + range.count; i++)
    {
        const PrimitiveGroup* group = ecs->primitiveGroups + i;
        if (group->valid && group->meshIndex == meshIndex && group->primitiveIndex == primitiveIndex)
            return i;
    }
    return INVALID_GROUP;
}

static void RefreshDenseToPrimitive(ECS* ecs, u32 firstGroup)
{
    for (u32 i = firstGroup; i < ecs->numGroups; i++)
    {
        const PrimitiveGroup* group = ecs->primitiveGroups + i;
        for (u32 j = 0; j < group->numEntities; j++)
            ecs->denseToPrimitiveIndex[group->entityOffset + j] = i;
    }
}

static u32 LeaveSpaceForEntities(ECS* ecs, u32 primitiveIdx, u32 numAdded)
{
    PrimitiveGroup* group = &ecs->primitiveGroups[primitiveIdx];
    const u32 entityStart = group->entityOffset + group->numEntities;
    if (ecs->numEntities + numAdded > ecs->maxEntities) return INVALID_ENTITY;

    for (s32 i = (s32)ecs->numGroups - 1; i > (s32)primitiveIdx; i--)
    {
        PrimitiveGroup* g = &ecs->primitiveGroups[i];
        u32 srcOffset = g->entityOffset;
        u32 dstOffset = srcOffset + numAdded;

        for (s32 j = (s32)g->numEntities - 1; j >= 0; j--)
        {
            u32 src = srcOffset + (u32)j;
            u32 dst = dstOffset + (u32)j;
            ecs->entities[dst] = ecs->entities[src];
            if (ecs->entities[dst].sparseIdx != INVALID_ENTITY)
                ecs->sparseID[ecs->entities[dst].sparseIdx] = dst;
        }
        g->entityOffset = dstOffset;
    }

    ecs->numEntities += numAdded;
    RefreshDenseToPrimitive(ecs, primitiveIdx + 1);
    return entityStart;
}

u32 ECS_AddEntities(ECS* ecs, u32 primitiveIdx, u32 numAdded, const Entity* data)
{
    if (primitiveIdx >= ecs->numGroups || numAdded == 0) return INVALID_ENTITY;

    u32 startIdx = LeaveSpaceForEntities(ecs, primitiveIdx, numAdded);
    if (startIdx == INVALID_ENTITY) return INVALID_ENTITY;

    PrimitiveGroup* group = &ecs->primitiveGroups[primitiveIdx];
    for (u32 i = 0; i < numAdded; i++)
    {
        u32 denseIdx = startIdx + i;
        ecs->entities[denseIdx] = data[i];
        ecs->entities[denseIdx].sparseIdx = denseIdx;
        ecs->sparseID[denseIdx] = denseIdx;
        ecs->denseToPrimitiveIndex[denseIdx] = primitiveIdx;
    }
    group->numEntities += numAdded;
    group->capacity = group->numEntities;
    return startIdx;
}

u32 AddEntities(u32 primitiveIdx, u32 numAdded, const Entity* data)
{
    return ECS_AddEntities(&ecsSkinned, primitiveIdx, numAdded, data);
}

u32 ECS_AddEntity(ECS* ecs, u32 primitiveIdx, const Entity* data)
{
    return ECS_AddEntities(ecs, primitiveIdx, 1, data);
}

u32 AddEntity(u32 primitiveIdx, const Entity* data)
{
    return ECS_AddEntity(&ecsSkinned, primitiveIdx, data);
}

static u32 AddNodeEntity(ECS* ecs, Range range, const SceneBundle* bundle, u32 meshIndex, v128f position, v128f rotation, v128f scale)
{
    const AMesh* mesh = bundle->meshes + meshIndex;
    u32 added = 0;
    for (u32 j = 0; j < (u32)mesh->numPrimitives; ++j)
    {
        u32 groupIdx = FindPrimitiveGroup(ecs, range, meshIndex, j);
        if (groupIdx == INVALID_GROUP) continue;

        Entity entity;
        entity.position = position;
        PackQuaternionS16Norm(VecNorm(rotation), &entity.rotation);
        entity.scale = PackXY11Z10UnormToU32(scale);
        entity.sparseIdx = INVALID_ENTITY;
        if (ECS_AddEntity(ecs, groupIdx, &entity) != INVALID_ENTITY)
            added++;
    }
    return added;
}

u32 ECS_AddScene(ECS* ecs, u32 bundleIdx, v128f position, v128f rotation, v128f scale, bool wantSkinned)
{
    if (bundleIdx >= ecs->numBundles || ecs->bundles[bundleIdx] == NULL) return 0;

    Range range = ecs->bundleRange[bundleIdx];
    const SceneBundle* bundle = ecs->bundles[bundleIdx];
    if (bundle->numNodes <= 0) return 0;

    typedef struct NodeTransform_
    {
        v128f position;
        v128f rotation;
        v128f scale;
    } NodeTransform;

    NodeTransform* world = (NodeTransform*)AllocateTLSFGlobal(sizeof(NodeTransform) * (u32)bundle->numNodes);
    rotation = VecNorm(rotation);
    for (u32 i = 0; i < (u32)bundle->numNodes; i++)
    {
        const ANode* node = bundle->nodes + i;
        v128f localPos   = VecLoad(node->translation);
        v128f localRot   = VecNorm(VecLoad(node->rotation));
        v128f localScale = VecLoad(node->scale);

        if (node->parent >= 0)
        {
            NodeTransform parent = world[node->parent];
            world[i].position = VecAdd(QMulVec3V(VecMul(localPos, parent.scale), parent.rotation), parent.position);
            world[i].rotation = VecNorm(QMul(localRot, parent.rotation));
            world[i].scale = VecMul(localScale, parent.scale);
        }
        else
        {
            world[i].position = VecAdd(QMulVec3V(VecMul(localPos, scale), rotation), position);
            world[i].rotation = VecNorm(QMul(localRot, rotation));
            world[i].scale = VecMul(localScale, scale);
        }
    }

    u32 added = 0;
    for (u32 i = 0; i < (u32)bundle->numNodes; i++)
    {
        const ANode* node = bundle->nodes + i;
        if (node->type != 0 || node->index < 0) continue;
        if ((node->skin >= 0) != wantSkinned) continue;

        added += AddNodeEntity(ecs, range, bundle, (u32)node->index, world[i].position, world[i].rotation, world[i].scale);
    }

    DeAllocateTLSFGlobal(world);
    return added;
}

u32 AddScene(u32 bundleIdx, v128f position, v128f rotation, v128f scale)
{
    return ECS_AddScene(&ecsSkinned, bundleIdx, position, rotation, scale, true);
}

u32 GetNumPrimitivesInBundle(u32 bundleIdx)
{
    if (bundleIdx >= ecsSkinned.numBundles) return 0;
    return ecsSkinned.bundleRange[bundleIdx].count;
}

void ECS_CompactEntities()
{
}

void CompactEntities()
{
}

u32 RemoveEntities(u32 groupIdx, u32 startIdx, u32 count)
{
    (void)groupIdx; (void)startIdx; (void)count;
    return 0;
}

u32 RemoveEntity(u32 entityIdx, u32 groupIdx)
{
    (void)entityIdx; (void)groupIdx;
    return 0;
}

u32 RemoveSceneBundle(u32 bundleIdx)
{
    if (bundleIdx >= ecsSkinned.numBundles) return 0;
    ecsSkinned.bundles[bundleIdx] = NULL;
    return 0;
}

void ECS_Update(float delta_time)
{
    (void)delta_time;
}
