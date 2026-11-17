#include "Include/RenderSet.h"
#include "Include/Bitset.h"
#include "Include/Memory.h"
#include <box3d/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static u32 gAddCb, gRemoveRangeCb, gRemoveGroupsCb, gClearCb, gBodiesCreated, gBodiesDestroyed;

void RenderSet_AddEntitiesCallback(RenderSet* set, u32 groupIdx, u32 localStartIdx, u32 count) { gAddCb++; }
void RenderSet_RemoveRangeCallback(RenderSet* set, u32 groupIdx, u32 localStartIdx, u32 count) { gRemoveRangeCb++; }
void RenderSet_RemoveGroupsCallback(RenderSet* set, u32 firstGroup, u32 groupCount) { gRemoveGroupsCb++; }
void RenderSet_ClearEntitiesCallback(RenderSet* set) { gClearCb++; }
void PhysicsCreateEntityBody(struct Scene_* scene, const Entity* entity, void* data, b3ShapeType shapeType) { gBodiesCreated++; }
void Physics_DestroyEntityBody(struct Scene_* scene, const Entity* entity) { gBodiesDestroyed++; }

static int gChecks, gFailures;

#define CHECK(cond) do { gChecks++; if (!(cond)) { gFailures++; printf("    FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { gChecks++; u64 va_ = (u64)(a), vb_ = (u64)(b); if (va_ != vb_) { gFailures++; printf("    FAIL %s:%d  %s == %s  (%llu vs %llu)\n", __FILE__, __LINE__, #a, #b, (unsigned long long)va_, (unsigned long long)vb_); } } while (0)
#define CHECK_INV(set) CHECK(CheckInvariants(&(set)))

#define TREE_ENTITIES 5u
#define TREE_GROUPS   3u

static SceneBundle* BuildBundle(u32 numMeshes, const u32* primsPerMesh, u32 numNodes, const s32* parents, const s32* meshOfNode, bool skinned)
{
    SceneBundle* sb = calloc(1, sizeof(*sb));
    sb->numMeshes = (s32)numMeshes;
    sb->meshes = calloc(numMeshes, sizeof(*sb->meshes));
    u32 primOffset = 0;
    for (u32 m = 0; m < numMeshes; m++)
    {
        AMesh* mesh = &sb->meshes[m];
        mesh->numPrimitives = (s32)primsPerMesh[m];
        mesh->primitiveOffset = (s32)primOffset;
        mesh->primitives = calloc(primsPerMesh[m], sizeof(*mesh->primitives));
        for (u32 p = 0; p < primsPerMesh[m]; p++)
        {
            APrimitive* prim = &mesh->primitives[p];
            for (u32 k = 0; k < 3; k++) { prim->min[k] = -1.0f - (f32)(primOffset + p); prim->max[k] = 1.0f + (f32)(primOffset + p); }
            prim->material = 0;
            for (u32 lod = 0; lod < MESH_LOD_COUNT; lod++)
            {
                prim->lodIndexOffset[lod]  = 1000u * (primOffset + p) + 100u * lod;
                prim->lodNumIndices[lod]   = 36u / (lod + 1u);
                prim->lodVertexOffset[lod] = 500u * (primOffset + p) + 50u * lod;
                prim->lodNumVertices[lod]  = 24u / (lod + 1u);
            }
        }
        primOffset += primsPerMesh[m];
    }

    sb->numNodes = (s32)numNodes;
    sb->nodes = calloc(numNodes, sizeof(*sb->nodes));
    u32* childCount = calloc(numNodes, sizeof(u32));
    for (u32 n = 0; n < numNodes; n++)
        if (parents[n] >= 0) childCount[parents[n]]++;
    for (u32 n = 0; n < numNodes; n++)
    {
        ANode* node = &sb->nodes[n];
        node->type = 0;
        node->index = meshOfNode[n];
        node->skin = (skinned && meshOfNode[n] >= 0) ? 0 : -1;
        node->parent = parents[n];
        node->rotation[3] = 1.0f;
        node->scale[0] = node->scale[1] = node->scale[2] = 1.0f;
        node->numChildren = (s32)childCount[n];
        node->children = calloc(childCount[n] ? childCount[n] : 1u, sizeof(*node->children));
    }
    u32* fill = calloc(numNodes, sizeof(u32));
    for (u32 n = 0; n < numNodes; n++)
        if (parents[n] >= 0) sb->nodes[parents[n]].children[fill[parents[n]]++] = (s32)n;
    free(fill);
    free(childCount);

    sb->numMaterials = 1;
    sb->materials = calloc(1, sizeof(*sb->materials));
    sb->materials[0].alphaMode = AMaterialAlphaMode_Opaque;
    sb->numSkins = skinned ? 1 : 0;
    return sb;
}

static void FreeBundle(SceneBundle* sb)
{
    for (s32 n = 0; n < sb->numNodes; n++) free(sb->nodes[n].children);
    for (s32 m = 0; m < sb->numMeshes; m++) free(sb->meshes[m].primitives);
    free(sb->nodes);
    free(sb->meshes);
    free(sb->materials);
    free(sb);
}

static SceneBundle* MakeFlat(u32 prims, bool skinned)
{
    u32 primsPerMesh[1] = { prims };
    s32 parents[1] = { -1 };
    s32 meshOfNode[1] = { 0 };
    return BuildBundle(1, primsPerMesh, 1, parents, meshOfNode, skinned);
}

static SceneBundle* MakeTree(void)
{
    u32 primsPerMesh[2] = { 2, 1 };
    s32 parents[4]    = { -1, 0, 0, 1 };
    s32 meshOfNode[4] = { -1, 0, 1, 1 };
    return BuildBundle(2, primsPerMesh, 4, parents, meshOfNode, false);
}

static void InitTestSet(RenderSet* s, bool skinned, u32 maxEntities, u32 maxGroups, u32 maxBundles)
{
    RenderSet_InitSet(s, maxEntities, maxGroups, maxBundles, skinned);
    RenderSet_SetHookScene(s, NULL);
}

static Entity MakeRoot(f32 x)
{
    Entity e = { 0 };
    EntitySetPositionV(&e, VecSetR(x, 0.0f, 0.0f, 0.0f));
    EntitySetRotation(&e, VecSetR(0.0f, 0.0f, 0.0f, 1.0f));
    EntitySetScaleV(&e, VecSetR(1.0f, 1.0f, 1.0f, 0.0f));
    return e;
}

static SparseData* AddSceneAt(RenderSet* s, u32 bundle, f32 x, bool skinned)
{
    return RenderSet_AddScene(s, bundle, VecSetR(x, 0.0f, 0.0f, 0.0f), VecSetR(0.0f, 0.0f, 0.0f, 1.0f), VecSetR(1.0f, 1.0f, 1.0f, 0.0f), skinned, EntityFlags_None);
}

static u32 CountAtX(const RenderSet* s, f32 x)
{
    u32 n = 0;
    for (u32 e = 0; e < s->numEntities; e++)
    {
        if (EntityGetSparseID(&s->entities[e]) == INVALID_ENTITY) continue;
        if (EntityGetPos(&s->entities[e]).x == x) n++;
    }
    return n;
}

static u32 CountNonZeroSubNodes(const RenderSet* s)
{
    u32 n = 0;
    for (u32 i = 0; i < s->maxEntities; i++) if (s->subNodes[i] != 0) n++;
    return n;
}

static u32 CountLive(const RenderSet* s)
{
    u32 n = 0;
    for (u32 e = 0; e < s->numEntities; e++) if (EntityGetSparseID(&s->entities[e]) != INVALID_ENTITY) n++;
    return n;
}

static EntityID IdOfDense(const RenderSet* s, u32 dense)
{
    u32 sp = EntityGetSparseID(&s->entities[dense]);
    return MakeEntityID(sp, s->sparseData[sp].gen, s->skinned);
}

static bool CheckInvariants(const RenderSet* s)
{
    bool ok = true;
    u32 expected = 0;
    for (u32 g = 0; g < s->numGroups; g++)
    {
        const PrimitiveGroup* grp = &s->primitiveGroups[g];
        if (grp->entityOffset != expected) ok = false;
        expected += grp->numEntities;
    }
    if (expected != s->numEntities) ok = false;

    u32 live = 0;
    for (u32 e = 0; e < s->numEntities; e++)
    {
        const Entity* en = &s->entities[e];
        u32 g = EntityGetPrimitiveID(en);
        if (g >= s->numGroups) { ok = false; continue; }
        const PrimitiveGroup* grp = &s->primitiveGroups[g];
        if (e < grp->entityOffset || e >= grp->entityOffset + grp->numEntities) ok = false;

        u32 sp = EntityGetSparseID(en);
        if (sp == INVALID_ENTITY) continue;
        live++;
        if (sp >= s->maxEntities) { ok = false; continue; }
        if (!BitsetGet(s->sparseSlots, (s32)sp)) ok = false;
        if (!s->skinned && s->sparseData[sp].id != e) ok = false;
        if (s->skinned && s->sparseData[sp].id >= s->numEntities) ok = false;
    }

    if (!s->skinned)
    {
        u32 bits = 0;
        for (u32 i = 0; i < s->maxEntities; i++) if (BitsetGet(s->sparseSlots, (s32)i)) bits++;
        if (bits != live) ok = false;
    }

    for (u32 b = 0; b < s->numBundles; b++)
    {
        if (s->bundles[b] == NULL) continue;
        Range rb = s->bundlePrimRange[b];
        if (rb.start < 1u || rb.start + rb.count > s->numGroups) ok = false;
        for (u32 c = b + 1; c < s->numBundles; c++)
        {
            if (s->bundles[c] == NULL) continue;
            Range rc = s->bundlePrimRange[c];
            if (rb.start < rc.start + rc.count && rc.start < rb.start + rb.count) ok = false;
        }
    }
    return ok && RenderSet_Validate(s, "test");
}

static void T_InitDestroy(void)
{
    RenderSet s;
    InitTestSet(&s, false, 256, 32, 4);
    CHECK_EQ(s.numGroups, 1);
    CHECK_EQ(s.numEntities, 0);
    CHECK_EQ(s.numBundles, 0);
    for (u32 i = 0; i < 256; i++) CHECK_EQ(s.sparseData[i].id, INVALID_ENTITY);
    CHECK_INV(s);
    RenderSet_Destroy(&s);
    CHECK(s.entities == NULL);
    CHECK(s.sparseData == NULL);
}

static void T_SparseAllocFull(void)
{
    RenderSet s;
    InitTestSet(&s, false, 64, 8, 2);
    u32 ids[64];
    for (u32 i = 0; i < 64; i++)
    {
        ids[i] = RenderSet_AllocateSparseID(&s);
        CHECK(ids[i] != INVALID_ENTITY);
        for (u32 j = 0; j < i; j++) CHECK(ids[i] != ids[j]);
    }
    CHECK_EQ(RenderSet_AllocateSparseID(&s), INVALID_ENTITY);
    for (u32 i = 0; i < 64; i++) RenderSet_FreeSparseIDPtr(&s, s.sparseData + ids[i]);
    CHECK_EQ(RenderSet_AllocateSparseIDRange(&s, 64), 0);
    RenderSet_Destroy(&s);
}

static void T_SparseRangeAndGen(void)
{
    RenderSet s;
    InitTestSet(&s, false, 64, 8, 2);
    u32 start = RenderSet_AllocateSparseIDRange(&s, 8);
    CHECK_EQ(start, 0);
    for (u32 i = 0; i < 8; i++) CHECK(BitsetGet(s.sparseSlots, (s32)(start + i)));
    CHECK_EQ(RenderSet_AllocateSparseIDRange(&s, 0), INVALID_ENTITY);
    CHECK_EQ(RenderSet_AllocateSparseIDRange(&s, -3), INVALID_ENTITY);
    u32 second = RenderSet_AllocateSparseIDRange(&s, 8);
    CHECK_EQ(second, 8);

    u32 gens[8];
    for (u32 i = 0; i < 8; i++) gens[i] = s.sparseData[start + i].gen;
    RenderSet_FreeSparseIDRangePtr(&s, s.sparseData + start, 8);
    for (u32 i = 0; i < 8; i++)
    {
        CHECK(!BitsetGet(s.sparseSlots, (s32)(start + i)));
        CHECK_EQ(s.sparseData[start + i].gen, (gens[i] + 1u) & 0x7Fu);
        CHECK_EQ(s.sparseData[start + i].id, INVALID_ENTITY);
    }

    u32 single = RenderSet_AllocateSparseID(&s);
    CHECK_EQ(single, 0);
    u32 g0 = s.sparseData[single].gen;
    RenderSet_FreeSparseIDPtr(&s, s.sparseData + single);
    CHECK_EQ(s.sparseData[single].gen, (g0 + 1u) & 0x7Fu);
    RenderSet_Destroy(&s);
}

static void T_GenerationMask(void)
{
    RenderSet s;
    InitTestSet(&s, false, 16, 4, 2);
    u32 id = RenderSet_AllocateSparseID(&s);
    s.sparseData[id].id = 0;
    for (u32 gen = 0; gen < 128; gen++)
    {
        s.sparseData[id].gen = gen;
        CHECK(!RenderSet_EntityExpired(&s, MakeEntityID(id, gen, false)));
        CHECK(RenderSet_EntityExpired(&s, MakeEntityID(id, (gen + 1u) & 0x7Fu, false)));
    }
    CHECK(RenderSet_EntityExpired(&s, MakeEntityID(INVALID_ENTITY, 0, false)));
    CHECK(RenderSet_GetEntity(&s, MakeEntityID(INVALID_ENTITY, 0, false)) == NULL);
    RenderSet_Destroy(&s);
}

static void T_AddSceneBundle(void)
{
    SceneBundle* tree = MakeTree();
    SceneBundle* flat = MakeFlat(2, false);
    RenderSet s;
    InitTestSet(&s, false, 64, 4, 2);

    u32 b0 = RenderSet_AddSceneBundle(&s, tree, 5);
    CHECK_EQ(b0, 0);
    CHECK_EQ(s.numGroups, 1 + TREE_GROUPS);
    CHECK_EQ(s.numBundles, 1);
    CHECK_EQ(s.bundlePrimRange[0].start, 1);
    CHECK_EQ(s.bundlePrimRange[0].count, TREE_GROUPS);
    CHECK(s.bundles[0] == tree);
    for (u32 g = 1; g <= TREE_GROUPS; g++)
    {
        const PrimitiveGroup* grp = &s.primitiveGroups[g];
        CHECK_EQ(grp->materialIndex, 5);
        CHECK_EQ(grp->numEntities, 0);
        CHECK_EQ(grp->entityOffset, 0);
        CHECK(grp->lodNumIndices[0] > 0);
        CHECK(grp->lodNumIndices[1] < grp->lodNumIndices[0]);
    }
    CHECK_EQ(s.primitiveGroups[1].meshIndex, 0);
    CHECK_EQ(s.primitiveGroups[2].primitiveIndex, 1);
    CHECK_EQ(s.primitiveGroups[3].meshIndex, 1);

    u32 over = RenderSet_AddSceneBundle(&s, flat, 0);
    CHECK_EQ(over, INVALID_BUNDLE);
    CHECK_EQ(s.numGroups, 1 + TREE_GROUPS);
    CHECK_EQ(s.numBundles, 1);

    RenderSet_Destroy(&s);
    FreeBundle(tree);
    FreeBundle(flat);
}

static void T_BundleSlotLimit(void)
{
    SceneBundle* flat = MakeFlat(1, false);
    RenderSet s;
    InitTestSet(&s, false, 64, 16, 2);
    CHECK_EQ(RenderSet_AddSceneBundle(&s, flat, 0), 0);
    CHECK_EQ(RenderSet_AddSceneBundle(&s, flat, 0), 1);
    CHECK_EQ(RenderSet_AddSceneBundle(&s, flat, 0), INVALID_BUNDLE);
    RenderSet_Destroy(&s);
    FreeBundle(flat);
}

static void T_BundleSlotReuse(void)
{
    SceneBundle* flat = MakeFlat(1, false);
    RenderSet s;
    InitTestSet(&s, false, 64, 16, 4);
    u32 a = RenderSet_AddSceneBundle(&s, flat, 0);
    u32 b = RenderSet_AddSceneBundle(&s, flat, 0);
    u32 c = RenderSet_AddSceneBundle(&s, flat, 0);
    CHECK_EQ(a, 0); CHECK_EQ(b, 1); CHECK_EQ(c, 2);
    CHECK_EQ(s.numBundles, 3);
    RenderSet_RemoveSceneBundle(&s, b);
    CHECK_EQ(s.numBundles, 3);
    CHECK(s.bundles[b] == NULL);
    u32 d = RenderSet_AddSceneBundle(&s, flat, 0);
    CHECK_EQ(d, 1);
    RenderSet_RemoveSceneBundle(&s, c);
    CHECK_EQ(s.numBundles, 2);
    CHECK_INV(s);
    RenderSet_Destroy(&s);
    FreeBundle(flat);
}

static void T_AddSceneTree(void)
{
    SceneBundle* tree = MakeTree();
    RenderSet s;
    InitTestSet(&s, false, 64, 8, 2);
    u32 b = RenderSet_AddSceneBundle(&s, tree, 0);
    gBodiesCreated = 0;
    SparseData* root = AddSceneAt(&s, b, 100.0f, false);
    CHECK(root != NULL);
    CHECK_EQ(SparseIDFromSparseData(&s, root), 0);
    CHECK_EQ(s.numEntities, TREE_ENTITIES);
    CHECK_EQ(s.primitiveGroups[0].numEntities, 1);
    CHECK_EQ(s.primitiveGroups[1].numEntities, 1);
    CHECK_EQ(s.primitiveGroups[2].numEntities, 1);
    CHECK_EQ(s.primitiveGroups[3].numEntities, 2);
    CHECK_EQ(gBodiesCreated, 4);
    CHECK_EQ(CountAtX(&s, 100.0f), TREE_ENTITIES);
    CHECK((EntityGetFlags(&s.entities[0]) & EntityFlags_NoMesh) != 0);
    CHECK_INV(s);

    CHECK_EQ(s.sparseData[0].subNodeCount, 2);
    CHECK_EQ(s.sparseData[0].parent, 0);
    CHECK_EQ(s.sparseData[1].subNodeCount, 1);
    CHECK_EQ(s.sparseData[1].parent, 1);
    CHECK_EQ(s.sparseData[1].hasPrim, 1);
    CHECK_EQ(s.sparseData[2].hasPrim, 0);
    CHECK_EQ(s.sparseData[3].parent, 3);
    CHECK_EQ(s.sparseData[4].parent, 3);

    for (u32 sp = 1; sp < TREE_ENTITIES; sp++)
    {
        const SparseData* child = &s.sparseData[sp];
        if (!child->parent) continue;
        const SparseData* parent = child - child->parent;
        bool found = false;
        for (u32 i = 0; i < parent->subNodeCount; i++)
            if (parent + s.subNodes[parent->subNodeOffset + i] == child) found = true;
        CHECK(found);
    }

    EntityID rootID = MakeEntityID(0, root->gen, false);
    CHECK(!RenderSet_EntityExpired(&s, rootID));
    CHECK(RenderSet_GetEntity(&s, rootID) == &s.entities[0]);

    RenderSet_Destroy(&s);
    FreeBundle(tree);
}

static void T_MultiScene(void)
{
    SceneBundle* tree = MakeTree();
    RenderSet s;
    InitTestSet(&s, false, 128, 8, 2);
    u32 b = RenderSet_AddSceneBundle(&s, tree, 0);
    Entity roots[3] = { MakeRoot(100.0f), MakeRoot(200.0f), MakeRoot(300.0f) };
    SparseData* first = RenderSet_AddSceneArray(&s, b, roots, 3, false);
    CHECK(first != NULL);
    CHECK_EQ(s.numEntities, 3u * TREE_ENTITIES);
    CHECK_EQ(CountAtX(&s, 100.0f), TREE_ENTITIES);
    CHECK_EQ(CountAtX(&s, 200.0f), TREE_ENTITIES);
    CHECK_EQ(CountAtX(&s, 300.0f), TREE_ENTITIES);
    CHECK_EQ(CountLive(&s), 3u * TREE_ENTITIES);
    CHECK_INV(s);

    for (u32 scene = 1; scene < 3; scene++)
    {
        u32 base = scene * TREE_ENTITIES;
        CHECK_EQ(s.sparseData[base].subNodeCount, 2);
        CHECK_EQ(s.sparseData[base + 3].parent, 3);
    }
    RenderSet_Destroy(&s);
    FreeBundle(tree);
}

static void T_SecondBundleKeepsFirst(void)
{
    SceneBundle* tree = MakeTree();
    SceneBundle* flat = MakeFlat(2, false);
    RenderSet s;
    InitTestSet(&s, false, 128, 16, 4);
    u32 a = RenderSet_AddSceneBundle(&s, tree, 0);
    u32 f = RenderSet_AddSceneBundle(&s, flat, 0);
    u32 c = RenderSet_AddSceneBundle(&s, tree, 0);

    AddSceneAt(&s, a, 100.0f, false);
    CHECK_INV(s);
    AddSceneAt(&s, f, 200.0f, false);
    CHECK_INV(s);
    AddSceneAt(&s, c, 300.0f, false);
    CHECK_INV(s);
    AddSceneAt(&s, a, 400.0f, false);
    CHECK_INV(s);

    CHECK_EQ(CountAtX(&s, 100.0f), TREE_ENTITIES);
    CHECK_EQ(CountAtX(&s, 200.0f), 2);
    CHECK_EQ(CountAtX(&s, 300.0f), TREE_ENTITIES);
    CHECK_EQ(CountAtX(&s, 400.0f), TREE_ENTITIES);
    CHECK_EQ(s.numEntities, 3u * TREE_ENTITIES + 2u);
    CHECK_EQ(s.primitiveGroups[0].numEntities, 3);
    RenderSet_Destroy(&s);
    FreeBundle(tree);
    FreeBundle(flat);
}

static void T_SkinnedMultiPrim(void)
{
    SceneBundle* skinned = MakeFlat(2, true);
    RenderSet s;
    InitTestSet(&s, true, 64, 8, 2);
    u32 b = RenderSet_AddSceneBundle(&s, skinned, 0);
    CHECK_EQ(b, 0);
    for (u32 g = 1; g <= 2; g++)
    {
        CHECK(VecGetX(s.primitiveGroups[g].aabbMin) == VecGetX(s.primitiveGroups[1].aabbMin));
        CHECK(VecGetX(s.primitiveGroups[g].aabbMax) == VecGetX(s.primitiveGroups[1].aabbMax));
    }
    Entity roots[2] = { MakeRoot(100.0f), MakeRoot(200.0f) };
    SparseData* r = RenderSet_AddSceneArray(&s, b, roots, 2, true);
    CHECK(r != NULL);
    CHECK_EQ(s.numEntities, 4);
    CHECK_EQ(s.primitiveGroups[1].numEntities, 2);
    CHECK_EQ(s.primitiveGroups[2].numEntities, 2);
    CHECK_INV(s);
    RenderSet_Destroy(&s);
    FreeBundle(skinned);
}

static void T_CountTriangles(void)
{
    SceneBundle* flat = MakeFlat(2, false);
    RenderSet s;
    InitTestSet(&s, false, 64, 8, 2);
    u32 b = RenderSet_AddSceneBundle(&s, flat, 0);
    CHECK_EQ(RenderSet_CountTriangles(&s), 0);
    Entity roots[3] = { MakeRoot(1.0f), MakeRoot(2.0f), MakeRoot(3.0f) };
    RenderSet_AddSceneArray(&s, b, roots, 3, false);
    CHECK_EQ(RenderSet_CountTriangles(&s), 3u * 2u * (36u / 3u));
    RenderSet_RemoveEntities(&s, 1, 0, 1);
    CHECK_EQ(RenderSet_CountTriangles(&s), 5u * (36u / 3u));
    RenderSet_Destroy(&s);
    FreeBundle(flat);
}

static void T_RemoveEntitiesGeneration(void)
{
    SceneBundle* flat = MakeFlat(2, false);
    RenderSet s;
    InitTestSet(&s, false, 64, 8, 2);
    u32 b = RenderSet_AddSceneBundle(&s, flat, 0);
    Entity roots[4] = { MakeRoot(1.0f), MakeRoot(2.0f), MakeRoot(3.0f), MakeRoot(4.0f) };
    RenderSet_AddSceneArray(&s, b, roots, 4, false);
    CHECK_EQ(s.primitiveGroups[1].numEntities, 4);
    CHECK_EQ(s.primitiveGroups[2].numEntities, 4);

    EntityID ids[4];
    for (u32 i = 0; i < 4; i++) ids[i] = IdOfDense(&s, s.primitiveGroups[1].entityOffset + i);
    u32 g2Before = s.primitiveGroups[2].entityOffset;
    EntityID otherGroup = IdOfDense(&s, g2Before);

    gRemoveRangeCb = 0;
    u32 removed = RenderSet_RemoveEntities(&s, 1, 1, 1);
    CHECK_EQ(removed, 1);
    CHECK_EQ(gRemoveRangeCb, 1);
    CHECK(RenderSet_EntityExpired(&s, ids[1]));
    CHECK(!RenderSet_EntityExpired(&s, ids[0]));
    CHECK(!RenderSet_EntityExpired(&s, ids[2]));
    CHECK(!RenderSet_EntityExpired(&s, ids[3]));
    CHECK(!RenderSet_EntityExpired(&s, otherGroup));
    CHECK_EQ(s.primitiveGroups[1].numEntities, 3);
    CHECK_EQ(s.numEntities, 7);
    CHECK_INV(s);

    EntityID surv[3];
    for (u32 i = 0; i < 3; i++) surv[i] = IdOfDense(&s, s.primitiveGroups[1].entityOffset + i);
    removed = RenderSet_RemoveEntities(&s, 1, 1, 100);
    CHECK_EQ(removed, 2);
    CHECK(!RenderSet_EntityExpired(&s, surv[0]));
    CHECK(RenderSet_EntityExpired(&s, surv[1]));
    CHECK(RenderSet_EntityExpired(&s, surv[2]));
    CHECK_EQ(RenderSet_RemoveEntities(&s, 1, 50, 1), 0);
    CHECK_EQ(RenderSet_RemoveEntities(&s, 99, 0, 1), 0);
    CHECK_EQ(RenderSet_RemoveEntities(&s, 1, 0, 0), 0);
    CHECK_INV(s);
    RenderSet_Destroy(&s);
    FreeBundle(flat);
}

static void T_RemoveSceneBundleMiddle(void)
{
    SceneBundle* tree = MakeTree();
    RenderSet s;
    InitTestSet(&s, false, 128, 16, 4);
    u32 a = RenderSet_AddSceneBundle(&s, tree, 0);
    u32 b = RenderSet_AddSceneBundle(&s, tree, 0);
    u32 c = RenderSet_AddSceneBundle(&s, tree, 0);
    AddSceneAt(&s, a, 100.0f, false);
    SparseData* rootB = AddSceneAt(&s, b, 200.0f, false);
    AddSceneAt(&s, c, 300.0f, false);
    CHECK_EQ(s.numEntities, 3u * TREE_ENTITIES);
    EntityID oldB = MakeEntityID(SparseIDFromSparseData(&s, rootB), rootB->gen, false);

    gRemoveGroupsCb = 0;
    u32 removed = RenderSet_RemoveSceneBundle(&s, b);
    CHECK_EQ(gRemoveGroupsCb, 1);
    CHECK_EQ(removed, TREE_ENTITIES);
    CHECK_EQ(s.numEntities, 2u * TREE_ENTITIES);
    CHECK_EQ(s.numGroups, 1 + 2 * TREE_GROUPS);
    CHECK_EQ(s.primitiveGroups[0].numEntities, 2);
    CHECK_EQ(CountAtX(&s, 100.0f), TREE_ENTITIES);
    CHECK_EQ(CountAtX(&s, 200.0f), 0);
    CHECK_EQ(CountAtX(&s, 300.0f), TREE_ENTITIES);
    CHECK_EQ(s.bundlePrimRange[c].start, 1 + TREE_GROUPS);
    CHECK_EQ(s.bundlePrimRange[a].start, 1);
    CHECK(s.bundles[b] == NULL);
    CHECK_EQ(s.bundlePrimRange[b].count, 0);
    CHECK(RenderSet_EntityExpired(&s, oldB));
    CHECK_INV(s);

    AddSceneAt(&s, a, 400.0f, false);
    CHECK(RenderSet_EntityExpired(&s, oldB));
    CHECK_EQ(CountAtX(&s, 400.0f), TREE_ENTITIES);
    CHECK_INV(s);
    RenderSet_Destroy(&s);
    FreeBundle(tree);
}

static void T_RemoveSceneBundleZeroesTail(void)
{
    SceneBundle* wide = MakeFlat(6, false);
    RenderSet s;
    InitTestSet(&s, false, 64, 8, 2);
    u32 b = RenderSet_AddSceneBundle(&s, wide, 0);
    CHECK_EQ(b, 0);
    AddSceneAt(&s, b, 1.0f, false);
    CHECK_EQ(RenderSet_RemoveSceneBundle(&s, b), 6);
    CHECK_EQ(s.numGroups, 1);
    CHECK_EQ(s.numBundles, 0);
    CHECK_EQ(s.numEntities, 0);
    for (u32 g = 1; g < 8; g++)
    {
        CHECK_EQ(s.primitiveGroups[g].numEntities, 0);
        CHECK_EQ(s.primitiveGroups[g].lodNumIndices[0], 0);
    }
    CHECK_INV(s);
    RenderSet_Destroy(&s);
    FreeBundle(wide);
}

static void T_StagedRemoveAndCompact(void)
{
    SceneBundle* tree = MakeTree();
    RenderSet s;
    InitTestSet(&s, false, 128, 8, 2);
    u32 b = RenderSet_AddSceneBundle(&s, tree, 0);
    SparseData* r1 = AddSceneAt(&s, b, 100.0f, false);
    SparseData* r2 = AddSceneAt(&s, b, 200.0f, false);
    u32 start1 = SparseIDFromSparseData(&s, r1);
    u32 start2 = SparseIDFromSparseData(&s, r2);
    CHECK_EQ(start1, 0);
    CHECK_EQ(start2, TREE_ENTITIES);
    EntityID id1 = MakeEntityID(start1, r1->gen, false);
    CHECK_EQ(CountNonZeroSubNodes(&s), 6);

    gBodiesDestroyed = 0;
    RenderSet_RemoveEntityRangeStaged(&s, (Range){ start1, TREE_ENTITIES });
    CHECK_EQ(gBodiesDestroyed, TREE_ENTITIES);
    CHECK_EQ(s.numEntities, 2u * TREE_ENTITIES);
    CHECK_EQ(CountLive(&s), TREE_ENTITIES);
    CHECK(RenderSet_EntityExpired(&s, id1));
    CHECK_EQ(CountNonZeroSubNodes(&s), 3);
    for (u32 i = 0; i < TREE_ENTITIES; i++) CHECK(!BitsetGet(s.sparseSlots, (s32)(start1 + i)));

    RenderSet_CompactEntities(&s);
    CHECK_EQ(s.numEntities, TREE_ENTITIES);
    CHECK_EQ(CountAtX(&s, 100.0f), 0);
    CHECK_EQ(CountAtX(&s, 200.0f), TREE_ENTITIES);
    CHECK_INV(s);

    SparseData* r3 = AddSceneAt(&s, b, 300.0f, false);
    CHECK_EQ(SparseIDFromSparseData(&s, r3), start1);
    CHECK(RenderSet_EntityExpired(&s, id1));
    CHECK_EQ(CountAtX(&s, 300.0f), TREE_ENTITIES);
    CHECK_INV(s);
    RenderSet_Destroy(&s);
    FreeBundle(tree);
}

static void T_SingleStagedRemove(void)
{
    SceneBundle* tree = MakeTree();
    RenderSet s;
    InitTestSet(&s, false, 64, 8, 2);
    u32 b = RenderSet_AddSceneBundle(&s, tree, 0);
    AddSceneAt(&s, b, 100.0f, false);
    RenderSet_RemoveEntityStaged(&s, 3);
    CHECK_EQ(s.sparseData[3].parent, 0);
    CHECK_EQ(s.sparseData[0].subNodeCount, 1);
    CHECK(!BitsetGet(s.sparseSlots, 3));
    CHECK_EQ(CountLive(&s), TREE_ENTITIES - 1);
    RenderSet_CompactEntities(&s);
    CHECK_EQ(s.numEntities, TREE_ENTITIES - 1);
    CHECK_INV(s);
    RenderSet_Destroy(&s);
    FreeBundle(tree);
}

static void T_RemoveFromParent(void)
{
    SceneBundle* tree = MakeTree();
    RenderSet s;
    InitTestSet(&s, false, 64, 8, 2);
    u32 b = RenderSet_AddSceneBundle(&s, tree, 0);
    AddSceneAt(&s, b, 100.0f, false);
    SparseData* parent = &s.sparseData[0];
    u32 off = parent->subNodeOffset;
    CHECK_EQ(parent->subNodeCount, 2);
    SparseDataRemoveFromParent(&s, &s.sparseData[3]);
    CHECK_EQ(parent->subNodeCount, 1);
    CHECK_EQ(s.sparseData[3].parent, 0);
    CHECK_EQ(s.subNodes[off + 1], 0);
    SparseDataRemoveFromParent(&s, &s.sparseData[3]);
    CHECK_EQ(parent->subNodeCount, 1);
    RenderSet_Destroy(&s);
    FreeBundle(tree);
}

static void T_FreeSingleReleasesSubNodes(void)
{
    SceneBundle* tree = MakeTree();
    RenderSet s;
    InitTestSet(&s, false, 64, 8, 2);
    u32 b = RenderSet_AddSceneBundle(&s, tree, 0);
    AddSceneAt(&s, b, 100.0f, false);
    CHECK_EQ(CountNonZeroSubNodes(&s), 3);
    RenderSet_FreeSparseIDPtr(&s, &s.sparseData[0]);
    CHECK_EQ(s.sparseData[0].subNodeCount, 0);
    CHECK_EQ(CountNonZeroSubNodes(&s), 1);
    RenderSet_Destroy(&s);
    FreeBundle(tree);
}

static void T_SparseReuseClearsMetadata(void)
{
    SceneBundle* tree = MakeTree();
    SceneBundle* flat = MakeFlat(2, false);
    RenderSet s;
    InitTestSet(&s, false, 64, 16, 4);
    u32 t = RenderSet_AddSceneBundle(&s, tree, 0);
    u32 f = RenderSet_AddSceneBundle(&s, flat, 0);
    SparseData* r = AddSceneAt(&s, f, 100.0f, false);
    CHECK_EQ(SparseIDFromSparseData(&s, r), 0);
    CHECK_EQ(s.sparseData[0].hasPrim, 1);
    RenderSet_RemoveEntityRangeStaged(&s, (Range){ 0, 2 });
    RenderSet_CompactEntities(&s);
    SparseData* r2 = AddSceneAt(&s, t, 200.0f, false);
    CHECK_EQ(SparseIDFromSparseData(&s, r2), 0);
    CHECK_EQ(s.sparseData[0].hasPrim, 0);
    CHECK_EQ(s.sparseData[1].hasPrim, 1);
    CHECK_INV(s);
    RenderSet_Destroy(&s);
    FreeBundle(tree);
    FreeBundle(flat);
}

static void T_ClearEntities(void)
{
    SceneBundle* tree = MakeTree();
    RenderSet s;
    InitTestSet(&s, false, 64, 8, 2);
    u32 b = RenderSet_AddSceneBundle(&s, tree, 0);
    SparseData* r = AddSceneAt(&s, b, 100.0f, false);
    EntityID old = MakeEntityID(SparseIDFromSparseData(&s, r), r->gen, false);

    gClearCb = 0;
    RenderSet_ClearEntities(&s);
    CHECK_EQ(gClearCb, 1);
    CHECK_EQ(s.numEntities, 0);
    CHECK_EQ(s.numGroups, 1 + TREE_GROUPS);
    CHECK_EQ(s.numBundles, 1);
    CHECK(s.bundles[0] == tree);
    CHECK(RenderSet_EntityExpired(&s, old));
    CHECK_EQ(CountNonZeroSubNodes(&s), 0);
    CHECK_INV(s);

    AddSceneAt(&s, b, 200.0f, false);
    CHECK_EQ(s.numEntities, TREE_ENTITIES);
    CHECK_EQ(CountAtX(&s, 200.0f), TREE_ENTITIES);
    CHECK(RenderSet_EntityExpired(&s, old));
    CHECK_INV(s);
    RenderSet_Destroy(&s);
    FreeBundle(tree);
}

static void T_ClearReservesGroupZero(void)
{
    SceneBundle* tree = MakeTree();
    RenderSet s;
    InitTestSet(&s, false, 64, 8, 2);
    u32 b = RenderSet_AddSceneBundle(&s, tree, 0);
    AddSceneAt(&s, b, 100.0f, false);
    RenderSet_Clear(&s);
    CHECK_EQ(s.numGroups, 1);
    CHECK_EQ(s.numEntities, 0);
    CHECK_EQ(s.numBundles, 0);
    u32 b2 = RenderSet_AddSceneBundle(&s, tree, 0);
    CHECK_EQ(b2, 0);
    CHECK_EQ(s.bundlePrimRange[b2].start, 1);
    AddSceneAt(&s, b2, 100.0f, false);
    CHECK_EQ(s.primitiveGroups[0].numEntities, 1);
    CHECK_INV(s);
    RenderSet_Destroy(&s);
    FreeBundle(tree);
}

static void T_LimitsAreExact(void)
{
    SceneBundle* flat = MakeFlat(1, false);
    RenderSet s;
    InitTestSet(&s, false, 8, 4, 2);
    u32 b = RenderSet_AddSceneBundle(&s, flat, 0);
    Entity roots[9];
    for (u32 i = 0; i < 9; i++) roots[i] = MakeRoot((f32)i);
    CHECK(RenderSet_AddSceneArray(&s, b, roots, 9, false) == NULL);
    CHECK_EQ(s.numEntities, 0);
    CHECK_EQ(CountLive(&s), 0);
    CHECK(RenderSet_AddSceneArray(&s, b, roots, 8, false) != NULL);
    CHECK_EQ(s.numEntities, 8);
    CHECK(RenderSet_AddSceneArray(&s, b, roots, 1, false) == NULL);
    CHECK_EQ(s.numEntities, 8);
    CHECK_INV(s);
    RenderSet_Destroy(&s);
    FreeBundle(flat);
}

static void T_InvalidBundleArguments(void)
{
    SceneBundle* flat = MakeFlat(1, false);
    RenderSet s;
    InitTestSet(&s, false, 16, 4, 4);
    Entity root = MakeRoot(0.0f);
    CHECK(RenderSet_AddSceneArray(&s, 0, &root, 1, false) == NULL);
    CHECK(RenderSet_AddSceneArray(&s, 3, &root, 1, false) == NULL);
    u32 b = RenderSet_AddSceneBundle(&s, flat, 0);
    CHECK(RenderSet_AddSceneArray(&s, b, &root, 0, false) == NULL);
    CHECK_EQ(RenderSet_RemoveSceneBundle(&s, 3), 0);
    CHECK_EQ(RenderSet_RemoveSceneBundle(&s, b), 0);
    CHECK_EQ(RenderSet_RemoveSceneBundle(&s, b), 0);
    CHECK_INV(s);
    RenderSet_Destroy(&s);
    FreeBundle(flat);
}

#ifdef RS_TEST_CRASHERS
static void T_RemoveEntitiesOverTombstone(void)
{
    SceneBundle* flat = MakeFlat(1, false);
    RenderSet s;
    InitTestSet(&s, false, 16, 4, 2);
    u32 b = RenderSet_AddSceneBundle(&s, flat, 0);
    Entity roots[3] = { MakeRoot(1.0f), MakeRoot(2.0f), MakeRoot(3.0f) };
    RenderSet_AddSceneArray(&s, b, roots, 3, false);
    RenderSet_RemoveEntityStaged(&s, 1);
    CHECK_EQ(RenderSet_RemoveEntities(&s, 1, 0, 3), 3);
    CHECK_EQ(s.numEntities, 0);
    CHECK_INV(s);
    RenderSet_Destroy(&s);
    FreeBundle(flat);
}

static void T_DoubleStagedRemove(void)
{
    SceneBundle* flat = MakeFlat(1, false);
    RenderSet s;
    InitTestSet(&s, false, 16, 4, 2);
    u32 b = RenderSet_AddSceneBundle(&s, flat, 0);
    Entity root = MakeRoot(1.0f);
    RenderSet_AddSceneArray(&s, b, &root, 1, false);
    RenderSet_RemoveEntityStaged(&s, 0);
    RenderSet_RemoveEntityStaged(&s, 0);
    CHECK_INV(s);
    RenderSet_Destroy(&s);
    FreeBundle(flat);
}
#endif

typedef struct FuzzScene_ { u32 bundle; u32 start; u32 count; bool alive; } FuzzScene;

static u32 gRng = 0xC0FFEEu;
static u32 Rand(u32 n) { gRng = gRng * 1664525u + 1013904223u; return (gRng >> 8) % n; }

static void T_Fuzz(void)
{
    SceneBundle* tree = MakeTree();
    SceneBundle* flat = MakeFlat(2, false);
    SceneBundle* bundles[3] = { tree, flat, tree };
    u32 perScene[3] = { TREE_ENTITIES, 2, TREE_ENTITIES };

    RenderSet s;
    InitTestSet(&s, false, 512, 32, 6);
    u32 slotKind[6];
    for (u32 i = 0; i < 3; i++) slotKind[RenderSet_AddSceneBundle(&s, bundles[i], 0)] = i;

    FuzzScene scenes[1024];
    u32 numScenes = 0;
    bool stop = false;

    for (u32 step = 0; step < 600 && !stop; step++)
    {
        u32 op = Rand(12);
        if (op < 6)
        {
            u32 slot = Rand(s.numBundles);
            if (!BitsetGet(s.bundleSlots, (s32)slot)) continue;
            u32 n = 1 + Rand(3);
            Entity roots[3] = { MakeRoot((f32)step), MakeRoot((f32)step), MakeRoot((f32)step) };
            SparseData* r = RenderSet_AddSceneArray(&s, slot, roots, n, false);
            if (r && numScenes < 1024)
                scenes[numScenes++] = (FuzzScene){ slot, SparseIDFromSparseData(&s, r), n * perScene[slotKind[slot]], true };
        }
        else if (op < 9)
        {
            if (numScenes == 0) continue;
            FuzzScene* sc = &scenes[Rand(numScenes)];
            if (!sc->alive) continue;
            RenderSet_RemoveEntityRangeStaged(&s, (Range){ sc->start, sc->count });
            sc->alive = false;
        }
        else if (op < 11)
        {
            RenderSet_CompactEntities(&s);
        }
        else
        {
            u32 slot = Rand(s.numBundles);
            if (!BitsetGet(s.bundleSlots, (s32)slot)) continue;
            u32 kind = slotKind[slot];
            for (u32 i = 0; i < numScenes; i++) if (scenes[i].bundle == slot) scenes[i].alive = false;
            RenderSet_RemoveSceneBundle(&s, slot);
            slotKind[RenderSet_AddSceneBundle(&s, bundles[kind], 0)] = kind;
        }

        if (!CheckInvariants(&s))
        {
            printf("    fuzz invariant broken at step %u (op %u)\n", step, op);
            gFailures++;
            stop = true;
        }
        gChecks++;
    }
    RenderSet_Destroy(&s);
    FreeBundle(tree);
    FreeBundle(flat);
}

typedef struct TestCase_ { const char* name; void (*fn)(void); } TestCase;

#define TEST(fn) { #fn, fn }

int main(void)
{
    InitGlobalArena();
    TestCase tests[] = {
        TEST(T_InitDestroy),
        TEST(T_SparseAllocFull),
        TEST(T_SparseRangeAndGen),
        TEST(T_GenerationMask),
        TEST(T_AddSceneBundle),
        TEST(T_BundleSlotLimit),
        TEST(T_BundleSlotReuse),
        TEST(T_AddSceneTree),
        TEST(T_MultiScene),
        TEST(T_SecondBundleKeepsFirst),
        TEST(T_SkinnedMultiPrim),
        TEST(T_CountTriangles),
        TEST(T_RemoveEntitiesGeneration),
        TEST(T_RemoveSceneBundleMiddle),
        TEST(T_RemoveSceneBundleZeroesTail),
        TEST(T_StagedRemoveAndCompact),
        TEST(T_SingleStagedRemove),
        TEST(T_RemoveFromParent),
        TEST(T_FreeSingleReleasesSubNodes),
        TEST(T_SparseReuseClearsMetadata),
        TEST(T_ClearEntities),
        TEST(T_ClearReservesGroupZero),
        TEST(T_LimitsAreExact),
        TEST(T_InvalidBundleArguments),
        #ifdef RS_TEST_CRASHERS
        TEST(T_RemoveEntitiesOverTombstone),
        TEST(T_DoubleStagedRemove),
        #endif
        TEST(T_Fuzz),
    };

    int failedTests = 0;
    for (u32 i = 0; i < sizeof(tests) / sizeof(tests[0]); i++)
    {
        int before = gFailures;
        printf("[ RUN  ] %s\n", tests[i].name);
        tests[i].fn();
        if (gFailures != before) { failedTests++; printf("[ FAIL ] %s\n", tests[i].name); }
        else printf("[  OK  ] %s\n", tests[i].name);
    }
    printf("\n%d checks, %d failed checks, %d failed tests\n", gChecks, gFailures, failedTests);
    return failedTests != 0;
}