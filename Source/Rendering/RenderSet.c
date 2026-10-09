
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

#define SPARSE_CLEAR_MASK 0x00000000007FFFFF

extern Graphics gGFX;

void RenderSet_InitSet(RenderSet* set, u32 maxEntities, u32 maxGroups, u32 maxBundles, bool skinned)
{
    MemsetZero(set, sizeof(*set));
    set->maxEntities = maxEntities;
    set->maxGroups   = maxGroups;
    set->maxBundles  = maxBundles;
    set->skinned     = skinned ? 1u : 0u;
    set->numGroups   = 1; // nomesh 

    set->entities         = (Entity*)AllocAligned(maxEntities * sizeof(Entity), 16);
    set->sparseData       = (SparseData*)AllocAligned(maxEntities * sizeof(SparseData), 16);
    set->sparseSlots      = (u64*)AllocZeroTLSF((maxEntities + 63u) >> 6, sizeof(u64));
    set->primitiveGroups  = (PrimitiveGroup*)AllocAligned(maxGroups * sizeof(PrimitiveGroup), 16);
    set->bundlePrimRange  = (Range*)AllocZeroTLSF(maxBundles, sizeof(Range));
    set->bundles          = (const SceneBundle**)AllocTLSF(maxBundles * sizeof(SceneBundle*));
    set->bundleSlots      = (u64*)AllocZeroTLSF((maxBundles + 63u) >> 6, sizeof(u64));
    MemSet(set->entities, 0, maxEntities * sizeof(Entity));
    MemSet(set->primitiveGroups, 0, maxGroups * sizeof(PrimitiveGroup));
    MemSet64((u64*)set->sparseData, SPARSE_CLEAR_MASK, maxEntities);
}

void RenderSet_Destroy(RenderSet* set)
{
    FreeAligned(set->entities);
    FreeAligned(set->sparseData); 
    DeAllocTLSF(set->sparseSlots);
    FreeAligned(set->primitiveGroups);
    DeAllocTLSF(set->bundlePrimRange);
    DeAllocTLSF(set->bundles);
    DeAllocTLSF(set->bundleSlots);
    MemsetZero(set, sizeof(RenderSet));
}

void RenderSet_SetHookScene(RenderSet* set, struct Scene_* scene)
{
    set->hookScene = scene;
}

v128f RenderSet_GroupLocalCenter(const PrimitiveGroup* group) {
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
    if (entityIdx >= group->numEntities) return false;
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
        if (group->meshIndex != meshIndex) continue;
        for (u32 e = 0; e < group->numEntities; e++)
        {
            if (EntityGetSparseID(&set->entities[group->entityOffset + e]) == sparseIdx)
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
        triangles += (u64)(group->lodNumIndices[0] / 3u) * group->numEntities;
    }
    return (u32)Minu64(triangles, 0xFFFFFFFFull);
}

u32 RenderSet_AllocateSparseID(RenderSet* set)
{
    s32 sparseIdx = BitsetFindFirstEmpty(set->sparseSlots, (s32)set->maxEntities);
    if (sparseIdx < 0) {
        AX_WARN("maximum sparse id reached: %d", set->maxEntities);
        return INVALID_ENTITY;
    }

    BitsetSet(set->sparseSlots, sparseIdx);
    return (u32)sparseIdx;
}

u32 RenderSet_AllocateSparseIDRange(RenderSet* set, int count)
{
    bool notEnoughID = (count + set->numEntities) >= set->maxEntities;
    if (notEnoughID || count <= 0) return INVALID_ENTITY;

    s32 sparseIdx = BitsetFindEmptyRange(set->sparseSlots, set->maxEntities, (u32)count);
    if (sparseIdx < 0) {
        AX_WARN("RenderSet_AllocateSparseIDRange: maximum sparse id reached: %d", set->maxEntities);
        return INVALID_ENTITY;
    }

    BitsetSetRange(set->sparseSlots, sparseIdx, count, true);
    return (u32)sparseIdx;
}

void RenderSet_FreeSparseIDRange(RenderSet* set, u32 sparseIdx, u32 count)
{
    if (sparseIdx + count > set->maxEntities) {
        AX_WARN("freeing sparse id range failed!");
        return;
    }

    BitsetSetRange(set->sparseSlots, sparseIdx, count, false);
    for (s32 i = 0; i < count; i++)
    {
        set->sparseData[sparseIdx + i].id = INVALID_ENTITY;
        set->sparseData[sparseIdx + i].gen++;
    }
}

void RenderSet_FreeSparseID(RenderSet* set, u32 sparseIdx)
{
    if (sparseIdx >= set->maxEntities) {
        AX_WARN("freeing sparse id range failed!");
        return;
    }
    BitsetReset(set->sparseSlots, (s32)sparseIdx);
    set->sparseData[sparseIdx].id = INVALID_ENTITY;
    set->sparseData[sparseIdx].gen++;
}

static s32 GetNumPrimitivesOfSceneBundle(const SceneBundle* sceneBundle)
{
    s32 res = 0;
    for (s32 m = 0; m < sceneBundle->numMeshes; m++)
        res += sceneBundle->meshes[m].numPrimitives;
    return res;
}

u32 RenderSet_AddSceneBundle(RenderSet* set, const SceneBundle* sceneBundle, u32 materialOffset)
{
    u32 numNewGroups = GetNumPrimitivesOfSceneBundle(sceneBundle);
    if (set->numGroups + numNewGroups > set->maxGroups) {
        AX_WARN("maximum primitive group count reached: %d + %d > %d", set->numGroups, numNewGroups, set->maxGroups);
        return INVALID_BUNDLE;
    }

    // bundle slots are stable handles, the lowest free slot is reused so removing a bundle
    // never shifts the indices of the others. numBundles tracks the highest used slot + 1.
    s32 slot = BitsetFindFirstEmpty(set->bundleSlots, (s32)set->maxBundles);
    if (slot < 0) {
        AX_WARN("maximum render bundle count reached: %d", set->maxBundles);
        return INVALID_BUNDLE;
    }

    u32 bundleIdx = (u32)slot;
    BitsetSet(set->bundleSlots, slot);
    if (bundleIdx + 1u > set->numBundles) set->numBundles = bundleIdx + 1u;
    set->bundles[bundleIdx] = sceneBundle;
    u32 primitiveStart = set->numGroups;
    // Skinned primitives deform far outside their bind-pose AABB when they are rigidly bound
    // to a moving bone (a sword in the hand, a helmet on the head, ...). The animation shaders
    // normalize each vertex against PrimitiveGroup.aabbMin/aabbMax, so a per-primitive bind AABB
    // collapses such accessories to a blob once the bone leaves bind pose. Share one skin-wide
    // bound (the union of every primitive) across all groups so accessories normalize against the
    // whole-character reach, matching how the old absolute ANIMATION_MAX_METERS range behaved.
    v128f skinnedMin = VecSet1(FLT_MAX);
    v128f skinnedMax = VecSet1(-FLT_MAX);
    if (set->skinned)
    {
        for (u32 m = 0; m < (u32)sceneBundle->numMeshes; m++)
        {
            const AMesh* mesh = sceneBundle->meshes + m;
            for (u32 p = 0; p < (u32)mesh->numPrimitives; p++)
            {
                const APrimitive* primitive = mesh->primitives + p;
                skinnedMin = VecMin(skinnedMin, VecLoad(primitive->min));
                skinnedMax = VecMax(skinnedMax, VecLoad(primitive->max));
            }
        }

        // Bind-pose bounds do not cover poses that reach past the bind silhouette (an overhead
        // swing, a kick). Inflate symmetrically so those poses stay inside [-1, 1] and avoid
        // clamping; the 13-bit per-axis precision has ample headroom for the slack.
        v128f boundsMargin = VecSetR(2.5f, 1.5f, 2.5f, 0.0f);
        v128f center = VecMulf(VecAdd(skinnedMin, skinnedMax), 0.5f);
        v128f extent = VecMul(VecSub(skinnedMax, skinnedMin), boundsMargin);
        skinnedMin = VecSub(center, extent);
        skinnedMax = VecAdd(center, extent);
    }

    s32 totalPrimitives = 0;
    for (u32 m = 0; m < (u32)sceneBundle->numMeshes; m++)
    {
        const AMesh* mesh = sceneBundle->meshes + m;
        for (u32 p = 0; p < (u32)mesh->numPrimitives; p++)
        {
            const APrimitive* primitive = mesh->primitives + p;
            PrimitiveGroup* group = set->primitiveGroups + primitiveStart + totalPrimitives + (s32)p;
            group->numEntities    = 0;
            group->meshIndex      = (u16)m;
            group->primitiveIndex = (u16)p;
            group->materialIndex  = (u16)(materialOffset + (u32)primitive->material);
            group->bundleIdx      = (u16)INVALID_BUNDLE; // owner-assigned bundle handle, stamped by the caller
            group->entityOffset   = set->numEntities;
            
            v128f aabbMin = set->skinned ? skinnedMin : VecLoad(primitive->min);
            v128f aabbMax = set->skinned ? skinnedMax : VecLoad(primitive->max);
            PrimitiveGroup_SetAABB(group, aabbMin, aabbMax);
            
            for (u32 lod = 0; lod < MESH_LOD_COUNT; lod++)
            {
                group->lodIndexOffset[lod]  = (u32)primitive->lodIndexOffset[lod];
                group->lodNumIndices[lod]   = (u32)primitive->lodNumIndices[lod];
                group->lodVertexOffset[lod] = (u32)primitive->lodVertexOffset[lod];
                group->lodNumVertices[lod]  = (u32)primitive->lodNumVertices[lod];
            }
        }
        totalPrimitives += mesh->numPrimitives;
    }
    set->numGroups += totalPrimitives;
    set->bundlePrimRange[bundleIdx].start = primitiveStart;
    set->bundlePrimRange[bundleIdx].count = set->numGroups - primitiveStart;
    return bundleIdx;
}

static u32 FindPrimitiveGroup(RenderSet* set, Range range, u32 meshIndex, u32 primitiveIndex)
{
    for (u32 i = range.start; i < range.start + range.count; i++)
    {
        const PrimitiveGroup* group = set->primitiveGroups + i;
        if (group->meshIndex == meshIndex && group->primitiveIndex == primitiveIndex)
            return i;
    }
    AX_WARN("primitive group couldnt found: %d", primitiveIndex);
    return INVALID_GROUP;
}

// todo(anil) will be removed
static u32 LeaveSpaceForEntities(RenderSet* set, u32 primitiveIdx, u32 numAdded)
{
    PrimitiveGroup* group = &set->primitiveGroups[primitiveIdx];
    const u32 entityStart = group->entityOffset + group->numEntities;
    if (set->numEntities + numAdded > set->maxEntities) {
        AX_WARN("maximum entity reached: %d", set->maxEntities);
        return INVALID_ENTITY;
    }

    for (s32 i = (s32)set->numEntities - 1; i >= (s32)entityStart; i--)
    {
        u32 dst = (u32)i + numAdded;
        set->entities[dst] = set->entities[i];

        u32 sparseIdx = EntityGetSparseID(&set->entities[dst]);
        if (sparseIdx != INVALID_ENTITY && sparseIdx < set->maxEntities && set->sparseData[sparseIdx].id == (u32)i)
            set->sparseData[sparseIdx].id = dst;
    }

    for (u32 i = primitiveIdx + 1u; i < set->numGroups; i++)
        set->primitiveGroups[i].entityOffset = set->primitiveGroups[i].entityOffset + numAdded;

    set->numEntities += numAdded;
    return entityStart;
}

// todo(anil) will be removed
SparseData* RenderSet_AddEntities(RenderSet* set, u32 primitiveIdx, u32 numAdded, const Entity* data)
{
    u32 startIdx = LeaveSpaceForEntities(set, primitiveIdx, numAdded);
    if (startIdx == INVALID_ENTITY) {
        RenderSet_FreeSparseIDRange(set, EntityGetSparseID(&data[0]), numAdded);
        return NULL;
    }
    PrimitiveGroup* group = &set->primitiveGroups[primitiveIdx];
    for (u32 i = 0; i < numAdded; i++)
    {
        u32 denseIdx = startIdx + i;
        set->entities[denseIdx] = data[i];
        u32 sparseIdx = EntityGetSparseID(&set->entities[denseIdx]);
        EntitySetPrimitiveID(&set->entities[denseIdx], primitiveIdx);
        set->sparseData[sparseIdx].id = denseIdx;
    }
    group->numEntities += numAdded;
    RenderSet_AddEntitiesCallback(set, primitiveIdx, group->numEntities - numAdded, numAdded);
    return set->sparseData + EntityGetSparseID(&set->entities[startIdx]);
}

// todo(anil) will be removed
SparseData* RenderSet_AddEntity(RenderSet* set, u32 primitiveIdx, const Entity* data)
{
    return RenderSet_AddEntities(set, primitiveIdx, 1, data);
}

static bool ANodeIsMesh(const ANode* node, bool skinned)
{
    return !(node->type != 0 || node->index < 0 || (skinned && node->skin < 0));
}

static bool PrimitiveIsTransparent(const SceneBundle* bundle, const APrimitive* primitive)
{
    AMaterialAlphaMode alphaMode = AMaterialAlphaMode_Opaque;
    if (primitive->material < (u32)bundle->numMaterials)
        alphaMode = bundle->materials[primitive->material].alphaMode;
    return alphaMode == AMaterialAlphaMode_Blend;
}

// totalEntityAdded = SumU32(primitiveCounts, numPrimitives)
void BatchLeaveSpacePrimitives(RenderSet* set, u32 primitiveStart, u32 numPrimitives, u32* primitiveCounts, u32 totalEntityAdded)
{
    u32 insertedBefore = totalEntityAdded; 
    if (insertedBefore == 0) return;
    for (s32 g = (s32)set->numGroups - 1; g >= (s32)primitiveStart; g--)
    {
        PrimitiveGroup* group = &set->primitiveGroups[g];
        insertedBefore -= primitiveCounts[g];

        u32 oldOffset = group->entityOffset;
        u32 newOffset = oldOffset + insertedBefore;
        for (s32 e = (s32)group->numEntities - 1; e >= 0; e--)
        {
            u32 dst = newOffset + (u32)e;
            set->entities[dst] = set->entities[oldOffset + (u32)e];
            EntitySetPrimitiveID(&set->entities[dst], (u32)g);

            u32 sparseIdx = EntityGetSparseID(&set->entities[dst]);
            if (sparseIdx != INVALID_ENTITY && sparseIdx < set->maxEntities)
                set->sparseData[sparseIdx].id = dst;
        }

        group->entityOffset = newOffset;
    }

    set->numEntities += totalEntityAdded;
}

// make group0 for non meshes
// todo(anil) count no meshes as well
u32 CountNumEntities(const RenderSet* set, u32 bundleIdx, u32 numScenes, u32* primitiveCounts, bool wantSkinned)
{
    const SceneBundle* bundle = set->bundles[bundleIdx];
    const Range range = set->bundlePrimRange[bundleIdx];
    u32 meshNodeCount = 0, totalPrimAdded = 0;
    // count num nodesfor allocations
    for (u32 m = 0; m < (u32)bundle->numNodes; m++)
    {
        const ANode* node = bundle->nodes + m;
        if (!ANodeIsMesh(node, wantSkinned))
        {
            primitiveCounts[0] += numScenes;
            totalPrimAdded += numScenes;
            continue;
        }

        const AMesh* mesh = bundle->meshes + node->index;
        meshNodeCount += numScenes;

        for (u32 p = 0; p < (u32)mesh->numPrimitives; p++)
        {
            primitiveCounts[range.start + (u32)mesh->primitiveOffset + p] += numScenes;
            totalPrimAdded += numScenes;
        }
    }

    return set->skinned ? meshNodeCount : totalPrimAdded;
}
// when an entity removed: 
// - look at the entity on the left:
//      left.hasPrim = hasPrim
//      left.hasSibling = left.hasSibling && hasPrim
// - to determine parent child id:
//      if (parent.child == id)
//      {
//          if (hasPrim || hasSibling) { parent.child = id + 1; }
//          else parent.child = 0
//      }

u32 AddBundleAsScene(RenderSet* set, u32 bundleIdx, u32 sparseStart, const Entity* root, bool wantSkinned)
{
    const Range range = set->bundlePrimRange[bundleIdx];
    const SceneBundle* bundle = set->bundles[bundleIdx];
    const ANode* nodes = bundle->nodes;
    const int numNodes = bundle->numNodes;

    // zeroed: flags is never set below, was left as garbage
    Entity* nodeEntities = ArenaAllocGlobal(((u32)numNodes + 1u) * sizeof(Entity));
    MemsetZero(nodeEntities, ((u32)numNodes + 1u) * sizeof(Entity));
    nodeEntities++;
    // world: staging entities -1 is root
    nodeEntities[-1] = root[0];
    EntityFlags flags = EntityGetFlags(&root[0]);
    // transform entities
    for (u32 n = 0; n < (u32)numNodes; n++)
    {
        const ANode* node = nodes + n;
        v128f localPos   = VecLoad(node->translation);
        v128f localRot   = VecNorm(VecLoad(node->rotation));
        v128f localScale = VecLoad(node->scale);
        const Entity* parent = nodeEntities + node->parent;
        Entity* added = nodeEntities + n;
        v128f parentRot = EntityGetRotation(parent);
        v128f parentScale = EntityGetScaleV(parent);
        EntitySetPositionV(added, VecAdd(QMulVec3V(VecMul(localPos, parentScale), parentRot), parent->position));
        EntitySetRotation(added, VecNorm(QMul(localRot, parentRot)));
        EntitySetScaleV(added, VecMul(localScale, parentScale));
        EntitySetFlags(added, flags);
    }

    u32 sparseCursor = 0;
    for (u32 n = 0; n < (u32)numNodes; n++)
    {
        const ANode* node = nodes + n;
        Entity* added = nodeEntities + n;
        u16 parent = Maxs32(node->parent, 0);
        
        if (!ANodeIsMesh(node, wantSkinned))
        {
            ASSERTR(set->primitiveGroups[0].numEntities < UINT16_MAX, continue);
            u32 denseId = set->primitiveGroups[0].numEntities++;
            u32 sparseId = sparseStart + sparseCursor++;
            EntityAddFlags(added, EntityFlags_NoMesh);
            EntitySetPrimitiveID(added, 0);
            EntitySetSparseID(added, sparseId);
            set->entities[denseId] = *added;
            SparseData* sparseData = &set->sparseData[sparseId];
            sparseData->parent = n - parent;
            sparseData->id = denseId;
            continue;
        }

        const AMesh* mesh = bundle->meshes + node->index;
        u32 firstGroupIdx = range.start + (u32)mesh->primitiveOffset;
        u32 nodeSparseIdx = INVALID_ENTITY;
        if (set->skinned)
            nodeSparseIdx = sparseStart + sparseCursor++;
    
        for (u32 p = 0; p < (u32)mesh->numPrimitives; p++)
        {
            const APrimitive* primitive = mesh->primitives + p;
        
            Entity prim = *added;
            EntitySetPrimitiveID(&prim, firstGroupIdx + p);
            u32 sparseId = set->skinned ? nodeSparseIdx : sparseStart + sparseCursor++;
            EntitySetSparseID(&prim, sparseId);
            EntityAddFlags(&prim, PrimitiveIsTransparent(bundle, primitive) * EntityFlags_Transparent);
            PrimitiveGroup* group = &set->primitiveGroups[firstGroupIdx + p];
            u32 localIdx = (u32)group->numEntities;
            u32 denseIdx = group->entityOffset + localIdx;
            ASSERTR(localIdx < UINT16_MAX, continue);
            ASSERTR(denseIdx < MAX_ENTITY, continue);
            group->numEntities++;
            set->entities[denseIdx] = prim;

            SparseData* sparseData = &set->sparseData[sparseId];
            sparseData->parent = n - parent;
            sparseData->id = denseIdx;
            sparseData->hasSibling = p < mesh->numPrimitives - 1;
            // todo(anil) move this outside of loop (optimization)
            Scene_PhysicsCreateEntityBody(set->hookScene, &set->entities[denseIdx]);
            // RenderSet_AddEntitiesCallback(set, firstGroupIdx + p, localIdx, 1u);
        }
    }
    nodeEntities--;
    ArenaPopGlobal(((u32)numNodes + 1u) * sizeof(Entity)); // nodeEntities
    return sparseCursor;
}

SparseData* RenderSet_AddScene(RenderSet* set, u32 bundleIdx, v128f position, v128f rotation, v128f scale, bool wantSkinned, EntityFlags flag)
{
    Entity e = { 0 };
    EntitySetPositionV(&e, position);
    EntitySetRotation(&e, rotation);
    EntitySetScaleV(&e, scale);
    EntitySetFlags(&e, flag);
    return RenderSet_AddSceneArray(set, bundleIdx, &e, 1, wantSkinned);
}

SparseData* RenderSet_AddSceneArray(RenderSet* set, u32 bundleIdx, const Entity* transforms, u32 numScenes, bool wantSkinned)
{
    if (bundleIdx >= set->numBundles || set->bundles[bundleIdx] == NULL) {
        AX_WARN("add scene bundle bounds check failed!");
        return NULL;
    }

    const Range primRange = set->bundlePrimRange[bundleIdx];
    if (set->bundles[bundleIdx]->numNodes <= 0 || primRange.count == 0u) {
        AX_WARN("no nodes in bundle to add!");
        return NULL;
    }
 
    u32* primitiveCounts = ArenaAllocGlobal(set->numGroups * sizeof(u32));
    MemSet(primitiveCounts, 0, set->numGroups * sizeof(u32));

    u32 sparseCount = CountNumEntities(set, bundleIdx, numScenes, primitiveCounts, wantSkinned);
    u32 sparseStart = RenderSet_AllocateSparseIDRange(set, sparseCount);
    if (sparseStart == INVALID_ENTITY || sparseCount == 0u)
    {
        AX_WARN("maximum entity reached: %d", set->maxEntities);
        ArenaPopGlobal(set->numGroups * sizeof(u32));
        return NULL;
    }

    BatchLeaveSpacePrimitives(set, primRange.start, primRange.count, primitiveCounts, sparseCount);

    for (s32 i = 0; i < numScenes; i++)
        AddBundleAsScene(set, bundleIdx, sparseStart, transforms + i, wantSkinned);
    
    ArenaPopGlobal(set->numGroups * sizeof(u32));
    return set->sparseData + sparseStart;
}

// (optimization) we might do group boundary swaps instead of moving entire entities
void RenderSet_CompactEntities(RenderSet* set)
{
    u32 writeEntity = 0;
    for (u32 groupIdx = 0; groupIdx < set->numGroups; groupIdx++)
    {
        PrimitiveGroup* group = &set->primitiveGroups[groupIdx];
        u32 oldOffset = group->entityOffset;
        u32 oldCount = group->numEntities;

        group->entityOffset = writeEntity;
        for (u32 i = 0; i < oldCount; i++)
        {
            Entity entity = set->entities[oldOffset + i];
            u32 sparse = EntityGetSparseID(&entity);
            if (sparse == INVALID_ENTITY)
                continue;
            set->sparseData[sparse].id = writeEntity;
            set->entities[writeEntity++] = entity;
        }
        group->numEntities = writeEntity - group->entityOffset;
    }
    set->numEntities = writeEntity;
}

void RenderSet_RemoveEntityStaged(RenderSet* set, u32 sparseId)
{
    Entity* entity = &set->entities[set->sparseData[sparseId].id];
    EntitySetSparseID(entity, INVALID_ENTITY);
    RenderSet_FreeSparseID(set, sparseId);
}

void RenderSet_RemoveEntityRangeStaged(RenderSet* set, Range range)
{
    for (u32 i = 0; i < range.count; i++)
    {
        Entity* entity = &set->entities[set->sparseData[range.start + i].id];
        EntitySetSparseID(entity, INVALID_ENTITY);
    }
    RenderSet_FreeSparseIDRange(set, range.start, range.count);
}

static void ShiftEntitiesLeft(RenderSet* set, u32 firstRemoved, u32 count)
{
    if (count == 0) return;

    u32 oldNumEntities = set->numEntities;
    u32 endRemoved = firstRemoved + count;
    for (u32 i = firstRemoved; i < endRemoved && i < oldNumEntities; i++)
    {
        u32 sparseIdx = EntityGetSparseID(&set->entities[i]);
        bool validSparse = sparseIdx != INVALID_ENTITY && sparseIdx < set->maxEntities;
        bool sparseMapsToRemoved = validSparse &&
                                   set->sparseData[sparseIdx].id >= firstRemoved &&
                                   set->sparseData[sparseIdx].id < endRemoved;
        if (sparseMapsToRemoved)
        {
            set->sparseData[sparseIdx].id = INVALID_ENTITY;
            BitsetReset(set->sparseSlots, (s32)sparseIdx);
        }
    }

    for (u32 i = endRemoved; i < oldNumEntities; i++)
    {
        u32 dst = i - count;
        set->entities[dst] = set->entities[i];

        u32 sparseIdx = EntityGetSparseID(&set->entities[dst]);
        bool validSparse = sparseIdx != INVALID_ENTITY && sparseIdx < set->maxEntities;
        bool sparseMapsToMovedSource = validSparse && set->sparseData[sparseIdx].id == i;
        bool sparseNeedsReplacement = validSparse && set->sparseData[sparseIdx].id == INVALID_ENTITY;
        if (sparseMapsToMovedSource || sparseNeedsReplacement)
        {
            set->sparseData[sparseIdx].id = dst;
            BitsetSet(set->sparseSlots, (s32)sparseIdx);
        }
    }

    set->numEntities -= count;
}

u32 RenderSet_RemoveEntities(RenderSet* set, u32 groupIdx, u32 localStartIdx, u32 count)
{
    if (groupIdx >= set->numGroups || count == 0) return 0;

    PrimitiveGroup* group = &set->primitiveGroups[groupIdx];
    if (localStartIdx >= group->numEntities) return 0;

    if (localStartIdx + count > group->numEntities)
        count = group->numEntities - localStartIdx;

    RenderSet_RemoveRangeCallback(set, groupIdx, localStartIdx, count);
    u32 cnt = group->numEntities - localStartIdx;
    for (u32 i = 0; i < cnt; i++)
    {
        Entity* entity = &set->entities[group->entityOffset + localStartIdx + i];
        set->sparseData[EntityGetSparseID(entity)].gen++;
    }

    u32 firstRemoved = group->entityOffset + localStartIdx;
    ShiftEntitiesLeft(set, firstRemoved, count);

    group->numEntities -= count;

    for (u32 i = groupIdx + 1; i < set->numGroups; i++)
        set->primitiveGroups[i].entityOffset = set->primitiveGroups[i].entityOffset - count;

    return count;
}

u32 RenderSet_RemoveEntity(RenderSet* set, u32 groupIdx, u32 localEntityIdx)
{
    return RenderSet_RemoveEntities(set, groupIdx, localEntityIdx, 1);
}

u32 RenderSet_RemoveSceneBundle(RenderSet* set, u32 bundleIdx)
{
    if (bundleIdx >= set->numBundles) return 0;

    Range range = set->bundlePrimRange[bundleIdx];
    if (range.count == 0) return 0;

    u32 firstGroup = range.start;
    u32 lastGroup = firstGroup + range.count - 1;
    if (firstGroup >= set->numGroups || lastGroup >= set->numGroups) return 0;

    RenderSet_RemoveGroupsCallback(set, firstGroup, range.count);

    u32 firstEntity = set->primitiveGroups[firstGroup].entityOffset;
    PrimitiveGroup* last = &set->primitiveGroups[lastGroup];
    u32 entityCount = (last->entityOffset + last->numEntities) - firstEntity;

    ShiftEntitiesLeft(set, firstEntity, entityCount);

    u32 noMeshRemoved = 0;
    for (s32 e = (s32)set->numEntities - 1; e >= 0; e--)
    {
        Entity* entity = &set->entities[e];
        bool noMesh = (EntityGetFlags(entity) & EntityFlags_NoMesh) != 0u;
        u32 primitiveID = EntityGetPrimitiveID(entity);
        if (noMesh && primitiveID >= firstGroup && primitiveID <= lastGroup)
        {
            ShiftEntitiesLeft(set, (u32)e, 1u);
            noMeshRemoved++;
        }
    }

    u32 groupCount = range.count;
    for (u32 i = firstGroup + groupCount; i < set->numGroups; i++)
    {
        PrimitiveGroup moved = set->primitiveGroups[i];
        moved.entityOffset = moved.entityOffset - entityCount;
        set->primitiveGroups[i - groupCount] = moved;
        for (u32 e = 0; e < moved.numEntities; e++)
            EntitySetPrimitiveID(&set->entities[moved.entityOffset + e], i - groupCount);
    }
    set->numGroups -= groupCount;

    u32 zeroedGroups = Minu32(groupCount, set->maxGroups - groupCount);
    MemSet(&set->primitiveGroups[set->numGroups], 0, zeroedGroups * sizeof(PrimitiveGroup));

    // free the slot without shifting the others; the primitive groups compacted above, so any
    // live bundle whose range started after the removed one slides down by groupCount.
    BitsetReset(set->bundleSlots, (s32)bundleIdx);
    set->bundles[bundleIdx] = NULL;
    set->bundlePrimRange[bundleIdx] = (Range){0};

    for (u32 i = 0; i < set->numBundles; i++)
    {
        if (i == bundleIdx || set->bundles[i] == NULL) continue;
        if (set->bundlePrimRange[i].start > firstGroup)
            set->bundlePrimRange[i].start -= groupCount;
    }

    while (set->numBundles > 0 && !BitsetGet(set->bundleSlots, (s32)(set->numBundles - 1u)))
        set->numBundles--;

    return entityCount + noMeshRemoved;
}

void RenderSet_ClearEntities(RenderSet* set)
{
    RenderSet_ClearEntitiesCallback(set);
    MemsetZero(set->entities, set->maxEntities * sizeof(Entity));
    MemSet64((u64*)set->sparseData, SPARSE_CLEAR_MASK, set->maxEntities);
    MemSet64(set->sparseSlots, 0, ((set->maxEntities + 63u) >> 6));

    for (u32 g = 0; g < set->numGroups; g++)
    {
        PrimitiveGroup* group = set->primitiveGroups + g;
        group->entityOffset = 0;
        group->numEntities = 0;
    }

    set->numEntities = 0;
}

void RenderSet_Clear(RenderSet* set)
{
    RenderSet_RemoveGroupsCallback(set, 0u, set->numGroups);
    set->numEntities = 0;
    set->numGroups   = 0;
    set->numBundles  = 0;
    
    MemSet64((u64*)set->sparseData, SPARSE_CLEAR_MASK, set->maxEntities);
    MemSet64(set->sparseSlots, 0, ((set->maxEntities + 63u) >> 6));
    MemSet(set->entities, 0, set->maxEntities * sizeof(Entity));
    MemsetZero(set->primitiveGroups, set->maxGroups * sizeof(PrimitiveGroup));
    MemsetZero(set->bundlePrimRange, set->maxBundles * sizeof(Range));
    MemsetZero(set->bundles, set->maxBundles * sizeof(SceneBundle*));
    MemsetZero(set->bundleSlots, ((set->maxBundles + 63u) >> 6) * sizeof(u64));
}

bool RenderSet_Validate(const RenderSet* set, const char* label)
{
    if (!set) return false;

    bool ok = true;
    u32 countedEntities = 0;
    u32 prevEnd = 0;

    for (u32 b = 0; b < set->numBundles; b++)
    {
        if (set->bundles[b] == NULL) continue;
        Range range = set->bundlePrimRange[b];
        if (range.start + range.count > set->numGroups) {
            AX_WARN("RenderSet invalid %s: bundle %d range start=%d count=%d groups=%d",
                    label ? label : "", b, range.start, range.count, set->numGroups);
            ok = false;
        }
    }

    for (u32 g = 0; g < set->numGroups; g++)
    {
        const PrimitiveGroup* group = &set->primitiveGroups[g];
        u32 end = group->entityOffset + group->numEntities;
        if (group->entityOffset < prevEnd || end > set->numEntities) {
            AX_WARN("RenderSet invalid %s: group %d entity range offset=%d count=%d prevEnd=%d entities=%d mesh=%d prim=%d mat=%d idx=%d/%d",
                    label ? label : "", g, group->entityOffset, group->numEntities, prevEnd, set->numEntities,
                    group->meshIndex, group->primitiveIndex, group->materialIndex, group->lodIndexOffset[0], group->lodNumIndices[0]);
            ok = false;
        }
        prevEnd = Maxu32(prevEnd, end);
        countedEntities += group->numEntities;

        if (g > 0 && group->numEntities > 0 && group->lodNumIndices[0] == 0) {
            AX_WARN("RenderSet invalid %s: group %d has entities but no lod0 indices mesh=%d prim=%d",
                    label ? label : "", g, group->meshIndex, group->primitiveIndex);
            ok = false;
        }
    }

    if (countedEntities > set->numEntities) {
        AX_WARN("RenderSet invalid %s: counted drawable entities=%d set entities=%d groups=%d bundles=%d",
                label ? label : "", countedEntities, set->numEntities, set->numGroups, set->numBundles);
        ok = false;
    }

    for (u32 e = 0; e < set->numEntities; e++)
    {
        Entity* entity = &set->entities[e];
        u32 groupIdx = EntityGetPrimitiveID(entity);

        if (groupIdx >= set->numGroups) {
            AX_WARN("RenderSet invalid %s: dense %d primitive=%d groups=%d", label ? label : "", e, groupIdx, set->numGroups);
            ok = false;
            continue;
        }

        const PrimitiveGroup* group = &set->primitiveGroups[groupIdx];
        if (e < group->entityOffset || e >= group->entityOffset + group->numEntities) {
            // abbaov
            AX_WARN("RenderSet invalid %s: dense %d points group %d range=%d..%d",
                    label ? label : "", e, groupIdx, group->entityOffset, group->entityOffset + group->numEntities);
            ok = false;
        }

        u32 sparseIdx = EntityGetSparseID(entity);
        if (sparseIdx == INVALID_ENTITY) {
            AX_WARN("RenderSet invalid sparse id: %d", e);
            continue;
        }

        if (sparseIdx >= set->maxEntities || set->sparseData[sparseIdx].id == INVALID_ENTITY 
            || set->sparseData[sparseIdx].id >= set->maxEntities) {
            AX_WARN("RenderSet invalid %s: dense %d sparse %d missing sparseToDense", label ? label : "", e, sparseIdx);
            ok = false;
        }
    }

    return ok;
}
