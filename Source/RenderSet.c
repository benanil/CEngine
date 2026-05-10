
#include "Include/RenderSet.h"
#include "Include/Memory.h"
#include "Include/Random.h"
#include "Include/Platform.h"
#include "Math/Half.h"
#include "Math/Matrix.h"
#include "Math/Bitpack.h"

RenderSet skinnedSet;
RenderSet surfaceSet;

void RenderSet_InitSet(RenderSet* set, u32 maxEntities, u32 maxGroups, u32 maxBundles)
{
    MemsetZero(set, sizeof(*set));
    set->maxEntities = maxEntities;
    set->maxGroups   = maxGroups;
    set->maxBundles  = maxBundles;

    set->entities                = (Entity*)AllocZeroTLSFGlobal(maxEntities, sizeof(Entity));
    set->sparseID                = (u32*)AllocateTLSFGlobal(maxEntities * sizeof(u32));
    set->denseToPrimitiveIndex   = (u32*)AllocZeroTLSFGlobal(maxEntities, sizeof(u32));
    set->primitiveGroups         = (PrimitiveGroup*)AllocZeroTLSFGlobal(maxGroups, sizeof(PrimitiveGroup));
    set->bundleRange             = (Range*)AllocZeroTLSFGlobal(maxBundles, sizeof(Range));
    set->bundles                 = (const SceneBundle**)AllocZeroTLSFGlobal(maxBundles, sizeof(SceneBundle*));

    for (u32 i = 0; i < maxEntities; i++)
        set->sparseID[i] = INVALID_ENTITY;
}

void RenderSet_Init()
{
    RenderSet_InitSet(&skinnedSet, MAX_ENTITY, MAX_GROUP, MAX_BUNDLES);
    RenderSet_InitSet(&surfaceSet, MAX_ANIM_INSTANCES, MAX_GROUP, MAX_BUNDLES);
}

u32 RenderSet_AddSceneBundle(RenderSet* set, const SceneBundle* sceneBundle)
{
    if (set->numBundles >= set->maxBundles) return INVALID_BUNDLE;

    u32 bundleIdx = set->numBundles++;
    set->bundles[bundleIdx] = sceneBundle;
    u32 primitiveStart = set->numGroups;

    for (u32 m = 0; m < (u32)sceneBundle->numMeshes; m++)
    {
        const AMesh* mesh = sceneBundle->meshes + m;
        for (u32 p = 0; p < (u32)mesh->numPrimitives; p++)
        {
            if (set->numGroups >= set->maxGroups) return INVALID_BUNDLE;

            const APrimitive* primitive = mesh->primitives + p;
            u32 primitiveIdx = set->numGroups++;
            PrimitiveGroup* group = set->primitiveGroups + primitiveIdx;
            group->valid          = 1;
            group->numEntities    = 0;
            group->capacity       = 0;
            group->boneStart      = 0;
            group->numIndices     = (u32)primitive->numIndices;
            group->indexOffset    = (u32)primitive->indexOffset;
            group->vertexOffset   = 0;
            group->meshIndex      = m;
            group->primitiveIndex = p;
            group->entityOffset   = set->numEntities;
            Float4ToHalf4V((u64*)group->aabbMin, VecLoad(primitive->min));
            Float4ToHalf4V((u64*)group->aabbMax, VecLoad(primitive->max));
        }
    }

    set->bundleRange[bundleIdx].start = primitiveStart;
    set->bundleRange[bundleIdx].count = set->numGroups - primitiveStart;
    return bundleIdx;
}

static u32 FindPrimitiveGroup(RenderSet* set, Range range, u32 meshIndex, u32 primitiveIndex)
{
    for (u32 i = range.start; i < range.start + range.count; i++)
    {
        const PrimitiveGroup* group = set->primitiveGroups + i;
        if (group->valid && group->meshIndex == meshIndex && group->primitiveIndex == primitiveIndex)
            return i;
    }
    AX_WARN("primitive group couldnt found: %d", primitiveIndex);
    return INVALID_GROUP;
}

static void RefreshDenseToPrimitive(RenderSet* set, u32 firstGroup)
{
    for (u32 i = firstGroup; i < set->numGroups; i++)
    {
        const PrimitiveGroup* group = set->primitiveGroups + i;
        for (u32 j = 0; j < group->numEntities; j++)
            set->denseToPrimitiveIndex[group->entityOffset + j] = i;
    }
}

static void RebuildSparseToDense(RenderSet* set)
{
    for (u32 i = 0; i < set->maxEntities; i++)
        set->sparseID[i] = INVALID_ENTITY;

    for (u32 i = 0; i < set->numEntities; i++)
    {
        set->entities[i].sparseIdx = i;
        set->sparseID[i] = i;
    }
}

static u32 LeaveSpaceForEntities(RenderSet* set, u32 primitiveIdx, u32 numAdded)
{
    PrimitiveGroup* group = &set->primitiveGroups[primitiveIdx];
    const u32 entityStart = group->entityOffset + group->numEntities;
    if (set->numEntities + numAdded > set->maxEntities)
    {
        AX_WARN("maximum entity reached: %d", set->maxEntities);
        return INVALID_ENTITY;
    }

    for (s32 i = (s32)set->numGroups - 1; i > (s32)primitiveIdx; i--)
    {
        PrimitiveGroup* g = &set->primitiveGroups[i];
        u32 srcOffset = g->entityOffset;
        u32 dstOffset = srcOffset + numAdded;

        for (s32 j = (s32)g->numEntities - 1; j >= 0; j--)
        {
            u32 src = srcOffset + (u32)j;
            u32 dst = dstOffset + (u32)j;
            set->entities[dst] = set->entities[src];
            if (set->entities[dst].sparseIdx != INVALID_ENTITY)
                set->sparseID[set->entities[dst].sparseIdx] = dst;
        }
        g->entityOffset = dstOffset;
    }

    set->numEntities += numAdded;
    RefreshDenseToPrimitive(set, primitiveIdx + 1);
    return entityStart;
}

u32 RenderSet_AddEntities(RenderSet* set, u32 primitiveIdx, u32 numAdded, const Entity* data)
{
    if (primitiveIdx >= set->numGroups || numAdded == 0) return INVALID_ENTITY;

    u32 startIdx = LeaveSpaceForEntities(set, primitiveIdx, numAdded);
    if (startIdx == INVALID_ENTITY) return INVALID_ENTITY;

    PrimitiveGroup* group = &set->primitiveGroups[primitiveIdx];
    for (u32 i = 0; i < numAdded; i++)
    {
        u32 denseIdx = startIdx + i;
        set->entities[denseIdx] = data[i];
        set->entities[denseIdx].sparseIdx = denseIdx;
        set->sparseID[denseIdx] = denseIdx;
        set->denseToPrimitiveIndex[denseIdx] = primitiveIdx;
    }
    group->numEntities += numAdded;
    group->capacity = group->numEntities;
    RebuildSparseToDense(set);
    return startIdx;
}

u32 RenderSet_AddEntity(RenderSet* set, u32 primitiveIdx, const Entity* data)
{
    return RenderSet_AddEntities(set, primitiveIdx, 1, data);
}

static u32 AddNodeEntity(RenderSet* set, Range range, const SceneBundle* bundle, u32 meshIndex, v128f position, v128f rotation, v128f scale)
{
    const AMesh* mesh = bundle->meshes + meshIndex;
    u32 added = 0;
    for (u32 j = 0; j < (u32)mesh->numPrimitives; ++j)
    {
        u32 groupIdx = FindPrimitiveGroup(set, range, meshIndex, j);
        if (groupIdx == INVALID_GROUP) continue;

        Entity entity;
        entity.position = position;
        PackQuaternionS16Norm(VecNorm(rotation), &entity.rotation);
        entity.scale = PackXY11Z10UnormToU32(scale);
        entity.sparseIdx = INVALID_ENTITY;
        if (RenderSet_AddEntity(set, groupIdx, &entity) != INVALID_ENTITY)
            added++;
    }
    return added;
}

u32 RenderSet_AddScene(RenderSet* set, u32 bundleIdx, v128f position, v128f rotation, v128f scale, bool wantSkinned)
{
    if (bundleIdx >= set->numBundles || set->bundles[bundleIdx] == NULL)
    {
        AX_WARN("add scene bundle bounds check failed!");
        return 0;
    }

    Range range = set->bundleRange[bundleIdx];
    const SceneBundle* bundle = set->bundles[bundleIdx];
    if (bundle->numNodes <= 0)
    {
        AX_WARN("no nodes in bundle to add!");
        return 0;
    }

    typedef struct NodeTransform_
    {
        v128f position;
        v128f rotation;
        v128f scale;
    } NodeTransform;

    rotation = VecNorm(rotation);
    NodeTransform sceneTransform = { position, rotation, scale };
    NodeTransform* world = (NodeTransform*)ArenaPushGlobal(sizeof(NodeTransform) * (u32)bundle->numNodes);
    for (u32 i = 0; i < (u32)bundle->numNodes; i++)
    {
        const ANode* node = bundle->nodes + i;
        v128f localPos   = VecLoad(node->translation);
        v128f localRot   = VecNorm(VecLoad(node->rotation));
        v128f localScale = VecLoad(node->scale);

        NodeTransform parent = node->parent >= 0 ? world[node->parent] : sceneTransform;
        world[i].position = VecAdd(QMulVec3V(VecMul(localPos, parent.scale), parent.rotation), parent.position);
        world[i].rotation = VecNorm(QMul(localRot, parent.rotation));
        world[i].scale = VecMul(localScale, parent.scale);
    }

    u32 added = 0;
    for (u32 i = 0; i < (u32)bundle->numNodes; i++)
    {
        const ANode* node = bundle->nodes + i;
        if (node->type != 0 || node->index < 0) continue;
        if ((node->skin >= 0) != wantSkinned) continue;

        added += AddNodeEntity(set, range, bundle, (u32)node->index, world[i].position, world[i].rotation, world[i].scale);
    }

    ArenaPopGlobal(sizeof(NodeTransform) * (u32)bundle->numNodes);
    return added;
}

u32 GetNumPrimitivesInBundle(u32 bundleIdx)
{
    if (bundleIdx >= skinnedSet.numBundles) return 0;
    return skinnedSet.bundleRange[bundleIdx].count;
}

void RenderSet_CompactEntities()
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
    if (bundleIdx >= skinnedSet.numBundles) return 0;
    skinnedSet.bundles[bundleIdx] = NULL;
    return 0;
}

void RenderSet_Update(float delta_time)
{
    (void)delta_time;
}
