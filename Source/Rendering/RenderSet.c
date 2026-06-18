
#include "Include/RenderSet.h"
#include "Include/Graphics.h"
#include "Include/Memory.h"
#include "Include/Algorithm.h"
#include "Include/Random.h"
#include "Include/Platform.h"
#include "Include/Bitset.h"
#include "Math/Half.h"
#include "Math/Matrix.h"
#include "Math/Bitpack.h"

extern Graphics gGFX;

void RenderSet_InitSet(RenderSet* set, u32 maxEntities, u32 maxGroups, u32 maxBundles, bool skinned)
{
    MemsetZero(set, sizeof(*set));
    set->maxEntities = maxEntities;
    set->maxGroups   = maxGroups;
    set->maxBundles  = maxBundles;
    set->skinned     = skinned ? 1u : 0u;

    set->entities                = (Entity*)AllocZeroTLSFGlobal(maxEntities, sizeof(Entity));
    set->sparseID                = (u32*)AllocateTLSFGlobal(maxEntities * sizeof(u32));
    set->sparseSlots             = (u64*)AllocZeroTLSFGlobal((maxEntities + 63u) >> 6, sizeof(u64));
    set->denseToPrimitiveIndex   = (u32*)AllocZeroTLSFGlobal(maxEntities, sizeof(u32));
    set->primitiveGroups         = (PrimitiveGroup*)AllocZeroTLSFGlobal(maxGroups, sizeof(PrimitiveGroup));
    set->bundleRange             = (Range*)AllocZeroTLSFGlobal(maxBundles, sizeof(Range));
    set->bundles                 = (const SceneBundle**)AllocZeroTLSFGlobal(maxBundles, sizeof(SceneBundle*));

    for (u32 i = 0; i < maxEntities; i++)
        set->sparseID[i] = INVALID_ENTITY;
}

v128f RenderSet_UnpackEntityScale01(u32 packed)
{
    v128u i = VeciSrl(VeciSet1(packed), VeciSetR(0, 11, 22, 31));
    i = VeciAnd(i, VeciSetR(0x7FF, 0x7FF, 0x3FF, 0));
    return VecMul(VecI32ToF32(i), VecSetR(1.0f / 2047.0f, 1.0f / 2047.0f, 1.0f / 1023.0f, 0.0f));
}

v128f RenderSet_UnpackEntityWorldScale(u32 packed)
{
    v128f scale = VecMulf(RenderSet_UnpackEntityScale01(packed), 10.0f);
    VecSetW(scale, 1.0f);
    return scale;
}

u32 RenderSet_PackEntityWorldScale(v128f scale)
{
    return PackXY11Z10UnormToU32(VecClamp(VecMulf(scale, 0.1f), VecSet1(0.0001f), VecOne()));
}

u32 RenderSet_PackEntityUniformWorldScale(f32 scale)
{
    f32 packedScale = Clampf32(scale * 0.1f, 0.0001f, 1.0f);
    return PackXY11Z10UnormToU32(VecSet1(packedScale));
}

v128f RenderSet_GroupLocalCenter(const PrimitiveGroup* group)
{
    return VecMulf(VecAdd(group->aabbMin, group->aabbMax), 0.5f);
}

v128f RenderSet_EntityBoundsCenter(const PrimitiveGroup* group, const Entity* entity, v128f rotation, v128f worldScale)
{
    return VecAdd(QMulVec3V(VecMul(RenderSet_GroupLocalCenter(group), worldScale), rotation), entity->position);
}

bool RenderSet_ResolveEntity(RenderSet* set, u32 groupIdx, u32 entityIdx, PrimitiveGroup** outGroup, Entity** outEntity)
{
    if (!set || groupIdx >= set->numGroups) return false;
    PrimitiveGroup* group = &set->primitiveGroups[groupIdx];
    if (!group->valid || entityIdx >= group->numEntities) return false;
    if (outGroup) *outGroup = group;
    if (outEntity) *outEntity = &set->entities[group->entityOffset + entityIdx];
    return true;
}

s32 RenderSet_NodeSpawnOrdinal(const SceneBundle* bundle, s32 nodeIdx)
{
    s32 ordinal = 0;
    for (s32 i = 0; i < bundle->numNodes; i++)
    {
        const ANode* node = &bundle->nodes[i];
        if (node->type != 0 || node->index < 0) continue;
        if (i == nodeIdx) return ordinal;
        ordinal++;
    }
    return -1;
}

bool RenderSet_FindNodeEntity(const RenderSet* set, Range range, u32 meshIndex, u32 sparseIdx,
                              u32* outGroup, u32* outEntity)
{
    for (u32 g = range.start; g < range.start + range.count; g++)
    {
        const PrimitiveGroup* group = &set->primitiveGroups[g];
        if (!group->valid || group->meshIndex != meshIndex) continue;
        for (u32 e = 0; e < group->numEntities; e++)
        {
            if (set->entities[group->entityOffset + e].sparseIdx == sparseIdx)
            {
                *outGroup = g;
                *outEntity = e;
                return true;
            }
        }
    }
    return false;
}

u32 RenderSet_CountTriangles(const RenderSet* set)
{
    u64 triangles = 0u;
    for (u32 i = 0u; i < set->numGroups; i++)
    {
        const PrimitiveGroup* group = &set->primitiveGroups[i];
        if (!group->valid) continue;
        triangles += (u64)(group->numIndices / 3u) * group->numEntities;
    }
    return (u32)Minu64(triangles, 0xFFFFFFFFull);
}

u32 RenderSet_AllocateSparseID(RenderSet* set)
{
    s32 sparseIdx = BitsetFindFirstEmpty(set->sparseSlots, (s32)set->maxEntities);
    if (sparseIdx < 0)
    {
        AX_WARN("maximum sparse id reached: %d", set->maxEntities);
        return INVALID_ENTITY;
    }

    BitsetSet(set->sparseSlots, sparseIdx);
    return (u32)sparseIdx;
}

void RenderSet_FreeSparseID(RenderSet* set, u32 sparseIdx)
{
    if (sparseIdx >= set->maxEntities) return;
    BitsetReset(set->sparseSlots, (s32)sparseIdx);
    set->sparseID[sparseIdx] = INVALID_ENTITY;
}

static bool HasFreeSparseIDs(const RenderSet* set, u32 count)
{
    if (count == 0)
        return true;
    return BitsetHasAtLeastEmptyBits(set->sparseSlots, (s32)set->maxEntities, count);
}

u32 RenderSet_AddSceneBundle(RenderSet* set, const SceneBundle* sceneBundle, u32 materialOffset)
{
    if (set->numBundles >= set->maxBundles)
    {
        AX_WARN("maximum render bundle count reached: %d", set->maxBundles);
        return INVALID_BUNDLE;
    }

    u32 numNewGroups = 0;
    for (u32 m = 0; m < (u32)sceneBundle->numMeshes; m++)
        numNewGroups += (u32)sceneBundle->meshes[m].numPrimitives;
    if (set->numGroups + numNewGroups > set->maxGroups)
    {
        AX_WARN("maximum primitive group count reached: %d + %d > %d", set->numGroups, numNewGroups, set->maxGroups);
        return INVALID_BUNDLE;
    }

    u32 bundleIdx = set->numBundles++;
    set->bundles[bundleIdx] = sceneBundle;
    u32 primitiveStart = set->numGroups;
    u32 vertexBase = 0;
    u32 localVertexOffset = 0;
    if (set->skinned)
        vertexBase = (u32)((const ASkinedVertex*)sceneBundle->allVertices - gGFX.SkinnedVertexBuffer);
    else
        vertexBase = (u32)((const AVertex*)sceneBundle->allVertices - gGFX.SurfaceVertexBuffer);

    for (u32 m = 0; m < (u32)sceneBundle->numMeshes; m++)
    {
        const AMesh* mesh = sceneBundle->meshes + m;
        for (u32 p = 0; p < (u32)mesh->numPrimitives; p++)
        {
            const APrimitive* primitive = mesh->primitives + p;
            u32 primitiveIdx = set->numGroups++;
            PrimitiveGroup* group = set->primitiveGroups + primitiveIdx;
            group->valid          = 1;
            group->numEntities    = 0;
            group->capacity       = 0;
            group->animatedVertexOffset = set->skinned ? (u32)primitive->lodAnimatedVertexOffset[0] : localVertexOffset;
            group->numIndices     = (u32)primitive->numIndices;
            group->indexOffset    = (u32)primitive->indexOffset;
            group->vertexOffset   = set->skinned ? (u32)primitive->lodVertexOffset[0] : vertexBase + localVertexOffset;
            group->meshIndex      = m;
            group->primitiveIndex = p;
            group->materialIndex  = materialOffset + (u32)primitive->material;
            group->numVertices    = (u32)primitive->numVertices;
            group->entityOffset   = set->numEntities;
            group->aabbMin = VecLoad(primitive->min);
            group->aabbMax = VecLoad(primitive->max);
            for (u32 lod = 0; lod < MESH_LOD_COUNT; lod++)
            {
                group->lodIndexOffset[lod] = (u32)primitive->lodIndexOffset[lod];
                group->lodNumIndices[lod]  = (u32)primitive->lodNumIndices[lod];
                group->lodVertexOffset[lod] = (u32)primitive->lodVertexOffset[lod];
                group->lodNumVertices[lod] = (u32)primitive->lodNumVertices[lod];
                group->lodAnimatedVertexOffset[lod] = (u32)primitive->lodAnimatedVertexOffset[lod];
            }
            localVertexOffset += (u32)primitive->numVertices;
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
    for (u32 g = firstGroup; g < set->numGroups; g++)
    {
        const PrimitiveGroup* group = set->primitiveGroups + g;
        for (u32 e = 0; e < group->numEntities; e++)
            set->denseToPrimitiveIndex[group->entityOffset + e] = g;
    }
}

static void RebuildSparseToDense(RenderSet* set)
{
    MemSet(set->sparseID, 0xFF, set->maxEntities * sizeof(u32));
    MemSet(set->sparseSlots, 0, ((set->maxEntities + 63u) >> 6) * sizeof(u64));

    for (u32 e = 0; e < set->numEntities; e++)
    {
        u32 sparseIdx = set->entities[e].sparseIdx;
        if (sparseIdx != INVALID_ENTITY && sparseIdx < set->maxEntities)
        {
            BitsetSet(set->sparseSlots, (s32)sparseIdx);
            if (set->sparseID[sparseIdx] == INVALID_ENTITY)
                set->sparseID[sparseIdx] = e;
        }
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

    if (!HasFreeSparseIDs(set, numAdded))
    {
        AX_WARN("maximum sparse id reached: %d", set->maxEntities);
        return INVALID_ENTITY;
    }

    u32 startIdx = LeaveSpaceForEntities(set, primitiveIdx, numAdded);
    if (startIdx == INVALID_ENTITY) return INVALID_ENTITY;

    PrimitiveGroup* group = &set->primitiveGroups[primitiveIdx];
    for (u32 i = 0; i < numAdded; i++)
    {
        u32 denseIdx = startIdx + i;
        set->entities[denseIdx] = data[i];
        if (set->entities[denseIdx].sparseIdx == INVALID_ENTITY)
            set->entities[denseIdx].sparseIdx = RenderSet_AllocateSparseID(set);
        set->denseToPrimitiveIndex[denseIdx] = primitiveIdx;
    }
    group->numEntities += numAdded;
    group->capacity = group->numEntities;
    RefreshDenseToPrimitive(set, 0);
    RebuildSparseToDense(set);
    return startIdx;
}

u32 RenderSet_AddEntity(RenderSet* set, u32 primitiveIdx, const Entity* data)
{
    return RenderSet_AddEntities(set, primitiveIdx, 1, data);
}

static u32 AddNodeEntity(RenderSet* set, Range range, const SceneBundle* bundle, u32 meshIndex, u32 sparseIdx, v128f position, v128f rotation, v128f scale)
{
    const AMesh* mesh = bundle->meshes + meshIndex;
    if (sparseIdx == INVALID_ENTITY) return 0;

    u32 added = 0;
    for (u32 j = 0; j < (u32)mesh->numPrimitives; ++j)
    {
        u32 groupIdx = FindPrimitiveGroup(set, range, meshIndex, j);
        if (groupIdx == INVALID_GROUP) continue;

        Entity entity;
        entity.position = position;
        PackQuaternionS16Norm(VecNorm(rotation), &entity.rotation);
        entity.scale = PackXY11Z10UnormToU32(scale);
        entity.sparseIdx = sparseIdx;
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
    NodeTransform* world = (NodeTransform*)ArenaAllocAlign(&GlobalArena, sizeof(NodeTransform) * (u32)bundle->numNodes, _Alignof(v128f));
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
    u32 sceneSparseIdx = wantSkinned ? RenderSet_AllocateSparseID(set) : INVALID_ENTITY;
    if (wantSkinned && sceneSparseIdx == INVALID_ENTITY)
    {
        ArenaPopAligned(&GlobalArena, world, sizeof(NodeTransform) * (u32)bundle->numNodes, _Alignof(v128f));
        return 0;
    }

    for (u32 i = 0; i < (u32)bundle->numNodes; i++)
    {
        const ANode* node = bundle->nodes + i;
        if (node->type != 0 || node->index < 0) continue;
        if (wantSkinned && node->skin < 0) continue;

        u32 sparseIdx = wantSkinned ? sceneSparseIdx : RenderSet_AllocateSparseID(set);
        if (sparseIdx == INVALID_ENTITY) continue;
        u32 nodeAdded = AddNodeEntity(set, range, bundle, (u32)node->index, sparseIdx, world[i].position, world[i].rotation, world[i].scale);
        if (!wantSkinned && nodeAdded == 0) RenderSet_FreeSparseID(set, sparseIdx);
        added += nodeAdded;
    }

    if (wantSkinned && added == 0) RenderSet_FreeSparseID(set, sceneSparseIdx);
    RebuildSparseToDense(set);

    ArenaPopAligned(&GlobalArena, world, sizeof(NodeTransform) * (u32)bundle->numNodes, _Alignof(v128f));
    return added;
}

void RenderSet_CompactEntities(RenderSet* set)
{
    u32 writeEntity = 0;
    u32 oldNumEntities = set->numEntities;

    for (u32 groupIdx = 0; groupIdx < set->numGroups; groupIdx++)
    {
        PrimitiveGroup* group = &set->primitiveGroups[groupIdx];
        u32 oldOffset = group->entityOffset;
        u32 oldCount = group->numEntities;
        u32 newCount = 0;

        group->entityOffset = writeEntity;
        for (u32 i = 0; i < oldCount; i++)
        {
            Entity entity = set->entities[oldOffset + i];
            if (entity.sparseIdx == INVALID_ENTITY)
                continue;

            set->entities[writeEntity] = entity;
            set->denseToPrimitiveIndex[writeEntity] = groupIdx;
            writeEntity++;
            newCount++;
        }

        group->numEntities = newCount;
        group->capacity = newCount;
    }

    for (u32 i = writeEntity; i < oldNumEntities; i++)
    {
        MemsetZero(&set->entities[i], sizeof(Entity));
        set->denseToPrimitiveIndex[i] = 0;
    }

    set->numEntities = writeEntity;
    RebuildSparseToDense(set);
}

void RenderSet_ClearEntities(RenderSet* set)
{
    MemsetZero(set->entities, set->maxEntities * sizeof(Entity));
    MemsetZero(set->denseToPrimitiveIndex, set->maxEntities * sizeof(u32));
    for (u32 i = 0; i < set->maxEntities; i++)
        set->sparseID[i] = INVALID_ENTITY;
    MemsetZero(set->sparseSlots, ((set->maxEntities + 63u) >> 6) * sizeof(u64));

    for (u32 g = 0; g < set->numGroups; g++)
    {
        PrimitiveGroup* group = set->primitiveGroups + g;
        group->entityOffset = 0;
        group->numEntities = 0;
        group->capacity = 0;
    }

    set->numEntities = 0;
}

void RenderSet_Clear(RenderSet* set)
{
    set->numEntities = 0;
    set->numGroups = 0;
    set->numBundles = 0;

    for (u32 i = 0; i < set->maxEntities; i++)
        set->sparseID[i] = INVALID_ENTITY;
    MemsetZero(set->sparseSlots, ((set->maxEntities + 63u) >> 6) * sizeof(u64));
    MemsetZero(set->entities, set->maxEntities * sizeof(Entity));
    MemsetZero(set->denseToPrimitiveIndex, set->maxEntities * sizeof(u32));
    MemsetZero(set->primitiveGroups, set->maxGroups * sizeof(PrimitiveGroup));
    MemsetZero(set->bundleRange, set->maxBundles * sizeof(Range));
    MemsetZero(set->bundles, set->maxBundles * sizeof(SceneBundle*));
}

static void ShiftEntitiesLeft(RenderSet* set, u32 firstRemoved, u32 count)
{
    if (count == 0) return;

    u32 endRemoved = firstRemoved + count;
    for (u32 i = endRemoved; i < set->numEntities; i++)
    {
        set->entities[i - count] = set->entities[i];
        set->denseToPrimitiveIndex[i - count] = set->denseToPrimitiveIndex[i];
    }

    set->numEntities -= count;
}

u32 RenderSet_RemoveEntities(RenderSet* set, u32 groupIdx, u32 localStartIdx, u32 count)
{
    if (groupIdx >= set->numGroups || count == 0) return 0;

    PrimitiveGroup* group = &set->primitiveGroups[groupIdx];
    if (!group->valid || localStartIdx >= group->numEntities) return 0;

    if (localStartIdx + count > group->numEntities)
        count = group->numEntities - localStartIdx;

    u32 firstRemoved = group->entityOffset + localStartIdx;
    ShiftEntitiesLeft(set, firstRemoved, count);

    group->numEntities -= count;
    group->capacity = group->numEntities;

    for (u32 i = groupIdx + 1; i < set->numGroups; i++)
        set->primitiveGroups[i].entityOffset -= count;

    RefreshDenseToPrimitive(set, groupIdx);
    RebuildSparseToDense(set);
    return count;
}

u32 RenderSet_RemoveEntity(RenderSet* set, u32 groupIdx, u32 localEntityIdx)
{
    return RenderSet_RemoveEntities(set, groupIdx, localEntityIdx, 1);
}

u32 RenderSet_RemoveSceneBundle(RenderSet* set, u32 bundleIdx)
{
    if (bundleIdx >= set->numBundles) return 0;

    Range range = set->bundleRange[bundleIdx];
    if (range.count == 0) return 0;

    u32 firstGroup = range.start;
    u32 lastGroup = firstGroup + range.count - 1;
    if (firstGroup >= set->numGroups || lastGroup >= set->numGroups) return 0;

    u32 firstEntity = set->primitiveGroups[firstGroup].entityOffset;
    PrimitiveGroup* last = &set->primitiveGroups[lastGroup];
    u32 entityCount = (last->entityOffset + last->numEntities) - firstEntity;

    ShiftEntitiesLeft(set, firstEntity, entityCount);

    u32 groupCount = range.count;
    for (u32 i = firstGroup + groupCount; i < set->numGroups; i++)
    {
        PrimitiveGroup moved = set->primitiveGroups[i];
        moved.entityOffset -= entityCount;
        set->primitiveGroups[i - groupCount] = moved;
    }
    set->numGroups -= groupCount;

    for (u32 i = set->numGroups; i < set->numGroups + groupCount && i < set->maxGroups; i++)
        MemsetZero(&set->primitiveGroups[i], sizeof(PrimitiveGroup));

    for (u32 i = bundleIdx + 1; i < set->numBundles; i++)
    {
        set->bundles[i - 1] = set->bundles[i];
        set->bundleRange[i - 1] = set->bundleRange[i];
        if (set->bundleRange[i - 1].start > firstGroup)
            set->bundleRange[i - 1].start -= groupCount;
    }
    set->numBundles--;
    set->bundles[set->numBundles] = NULL;
    set->bundleRange[set->numBundles] = (Range){0};

    RefreshDenseToPrimitive(set, firstGroup);
    RebuildSparseToDense(set);
    return entityCount;
}

bool RenderSet_Validate(const RenderSet* set, const char* label)
{
    if (!set) return false;

    bool ok = true;
    u32 countedEntities = 0;
    u32 prevEnd = 0;

    for (u32 b = 0; b < set->numBundles; b++)
    {
        Range range = set->bundleRange[b];
        if (range.start + range.count > set->numGroups)
        {
            AX_WARN("RenderSet invalid %s: bundle %d range start=%d count=%d groups=%d",
                    label ? label : "", b, range.start, range.count, set->numGroups);
            ok = false;
        }
    }

    for (u32 g = 0; g < set->numGroups; g++)
    {
        const PrimitiveGroup* group = &set->primitiveGroups[g];
        if (!group->valid) continue;

        u32 end = group->entityOffset + group->numEntities;
        if (group->entityOffset < prevEnd || end > set->numEntities)
        {
            AX_WARN("RenderSet invalid %s: group %d entity range offset=%d count=%d prevEnd=%d entities=%d mesh=%d prim=%d mat=%d idx=%d/%d",
                    label ? label : "", g, group->entityOffset, group->numEntities, prevEnd, set->numEntities,
                    group->meshIndex, group->primitiveIndex, group->materialIndex, group->lodIndexOffset[0], group->lodNumIndices[0]);
            ok = false;
        }
        prevEnd = Maxu32(prevEnd, end);
        countedEntities += group->numEntities;

        if (group->numEntities > 0 && group->lodNumIndices[0] == 0)
        {
            AX_WARN("RenderSet invalid %s: group %d has entities but no lod0 indices mesh=%d prim=%d",
                    label ? label : "", g, group->meshIndex, group->primitiveIndex);
            ok = false;
        }
    }

    if (countedEntities != set->numEntities)
    {
        AX_WARN("RenderSet invalid %s: counted entities=%d set entities=%d groups=%d bundles=%d",
                label ? label : "", countedEntities, set->numEntities, set->numGroups, set->numBundles);
        ok = false;
    }

    for (u32 e = 0; e < set->numEntities; e++)
    {
        u32 groupIdx = set->denseToPrimitiveIndex[e];
        if (groupIdx >= set->numGroups)
        {
            AX_WARN("RenderSet invalid %s: dense %d primitive=%d groups=%d", label ? label : "", e, groupIdx, set->numGroups);
            ok = false;
            continue;
        }

        const PrimitiveGroup* group = &set->primitiveGroups[groupIdx];
        if (e < group->entityOffset || e >= group->entityOffset + group->numEntities)
        {
            AX_WARN("RenderSet invalid %s: dense %d points group %d range=%d..%d",
                    label ? label : "", e, groupIdx, group->entityOffset, group->entityOffset + group->numEntities);
            ok = false;
        }

        u32 sparseIdx = set->entities[e].sparseIdx;
        if (sparseIdx != INVALID_ENTITY && sparseIdx < set->maxEntities && set->sparseID[sparseIdx] == INVALID_ENTITY)
        {
            AX_WARN("RenderSet invalid %s: dense %d sparse %d missing sparseToDense", label ? label : "", e, sparseIdx);
            ok = false;
        }
    }

    return ok;
}
