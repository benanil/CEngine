// Standalone RenderSet unit tests using placeholder SceneBundle data.
// Run from the repo root with:
//   Tests\RunRenderSetTest.bat

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// RenderSet.c includes Platform.h only for logging. Avoid SDL logging/linking in tests.
#define PLATFORM_H
#define AX_LOG(format, ...)  TestLog("INFO", format, ##__VA_ARGS__)
#define AX_WARN(format, ...) TestLog("WARN", format, ##__VA_ARGS__)
#define AX_ERROR(format, ...) TestLog("ERR", format, ##__VA_ARGS__)

#include "Include/Memory.h"
#include "Include/Graphics.h"

Arena GlobalArena;
Graphics gGFX;

static int gChecks;
static int gFailures;

#define CHECK(cond, ...) do { \
    gChecks++; \
    if (!(cond)) { \
        gFailures++; \
        printf("FAIL %s:%d  ", __FUNCTION__, __LINE__); \
        printf(__VA_ARGS__); \
        printf("\n"); \
    } \
} while (0)

static void TestLog(const char* level, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    printf("%s: ", level);
    vprintf(format, args);
    printf("\n");
    va_end(args);
}

void* AllocateTLSFGlobal(size_t size)
{
    return malloc(size);
}

void* ReAllocateTLSFGlobal(void* ptr, size_t size)
{
    return realloc(ptr, size);
}

void DeAllocateTLSFGlobal(void* ptr)
{
    free(ptr);
}

void* AllocZeroTLSFGlobal(size_t count, size_t size)
{
    return calloc(count, size);
}

void* ArenaAllocAlign(Arena* a, size_t size, size_t align)
{
    (void)a;
    (void)align;
    return malloc(size);
}

void ArenaPopAligned(Arena* a, void* ptr, size_t size, size_t align)
{
    (void)a;
    (void)size;
    (void)align;
    free(ptr);
}

void* ArenaAlloc(Arena* a, size_t size) { return ArenaAllocAlign(a, size, DEFAULT_ALIGN); }
void* ArenaAllocZero(Arena* a, size_t size) { void* p = ArenaAlloc(a, size); if (p) memset(p, 0, size); return p; }
ArenaMark ArenaSave(Arena* a) { (void)a; return (ArenaMark){0}; }
void ArenaRestore(Arena* a, ArenaMark mark) { (void)a; (void)mark; }

#include "Source/Rendering/RenderSet.c"

static AVertex gSurfaceVertices[1024];
static ASkinedVertex gSkinnedVertices[1024];
static u32 gIndices[4096];

static APrimitive MakePrimitive(int material, int indexOffset, int numIndices, int vertexOffset, int numVertices)
{
    APrimitive p;
    memset(&p, 0, sizeof(p));
    p.material = (unsigned short)material;
    p.indexOffset = indexOffset;
    p.numIndices = numIndices;
    p.numVertices = numVertices;
    p.min[0] = p.min[1] = p.min[2] = -1.0f;
    p.max[0] = p.max[1] = p.max[2] = 1.0f;
    p.min[3] = 0.0f;
    p.max[3] = 0.0f;
    for (u32 lod = 0; lod < MESH_LOD_COUNT; lod++)
    {
        p.lodIndexOffset[lod] = indexOffset + (int)lod * 100;
        p.lodNumIndices[lod] = lod == 0 ? numIndices : numIndices / 2;
        p.lodVertexOffset[lod] = vertexOffset;
        p.lodNumVertices[lod] = numVertices;
        p.lodAnimatedVertexOffset[lod] = vertexOffset;
    }
    return p;
}

static ANode MakeMeshNode(int meshIndex)
{
    ANode n;
    memset(&n, 0, sizeof(n));
    n.translation[3] = 1.0f;
    n.rotation[3] = 1.0f;
    n.scale[0] = n.scale[1] = n.scale[2] = n.scale[3] = 1.0f;
    n.type = 0;
    n.index = meshIndex;
    n.skin = -1;
    n.parent = -1;
    return n;
}

static SceneBundle MakeBundle(AMesh* meshes, int numMeshes, ANode* nodes, int numNodes, int materialCount, int vertexBase)
{
    SceneBundle b;
    memset(&b, 0, sizeof(b));
    b.meshes = meshes;
    b.numMeshes = numMeshes;
    b.nodes = nodes;
    b.numNodes = numNodes;
    b.numMaterials = materialCount;
    b.allVertices = gSurfaceVertices + vertexBase;
    b.allIndices = gIndices;
    return b;
}

static Entity MakeEntity(u32 sparseIdx)
{
    Entity e;
    memset(&e, 0, sizeof(e));
    e.position = VecZero();
    PackQuaternionS16Norm(QIdentity(), &e.rotation);
    e.scale = RenderSet_PackEntityUniformWorldScale(1.0f);
    e.sparseIdx = sparseIdx;
    return e;
}

static void TestBundleRegistration(void)
{
    RenderSet set;
    RenderSet_InitSet(&set, 16, 8, 4, false);

    APrimitive primA[2] = { MakePrimitive(0, 10, 30, 0, 8), MakePrimitive(1, 40, 18, 8, 6) };
    AMesh meshesA[1] = { { "A", primA, 2, 0, NULL } };
    SceneBundle bundleA = MakeBundle(meshesA, 1, NULL, 0, 2, 100);

    APrimitive primB[1] = { MakePrimitive(0, 100, 12, 0, 4) };
    AMesh meshesB[1] = { { "B", primB, 1, 0, NULL } };
    SceneBundle bundleB = MakeBundle(meshesB, 1, NULL, 0, 1, 200);

    u32 a = RenderSet_AddSceneBundle(&set, &bundleA, 10);
    u32 b = RenderSet_AddSceneBundle(&set, &bundleB, 20);

    CHECK(a == 0, "first bundle index=%u", a);
    CHECK(b == 1, "second bundle index=%u", b);
    CHECK(set.numGroups == 3, "numGroups=%u", set.numGroups);
    CHECK(set.bundleRange[0].start == 0 && set.bundleRange[0].count == 2, "bundle A range %u+%u", set.bundleRange[0].start, set.bundleRange[0].count);
    CHECK(set.bundleRange[1].start == 2 && set.bundleRange[1].count == 1, "bundle B range %u+%u", set.bundleRange[1].start, set.bundleRange[1].count);
    CHECK(set.primitiveGroups[0].materialIndex == 10, "group0 material=%u", set.primitiveGroups[0].materialIndex);
    CHECK(set.primitiveGroups[1].materialIndex == 11, "group1 material=%u", set.primitiveGroups[1].materialIndex);
    CHECK(set.primitiveGroups[2].materialIndex == 20, "group2 material=%u", set.primitiveGroups[2].materialIndex);
    CHECK(set.primitiveGroups[0].vertexOffset == 100, "group0 vertexOffset=%u", set.primitiveGroups[0].vertexOffset);
    CHECK(set.primitiveGroups[1].vertexOffset == 108, "group1 vertexOffset=%u", set.primitiveGroups[1].vertexOffset);
    CHECK(set.primitiveGroups[2].vertexOffset == 200, "group2 vertexOffset=%u", set.primitiveGroups[2].vertexOffset);
    CHECK(RenderSet_Validate(&set, "registration"), "validation failed");
}

static void TestMiddleInsertionKeepsMappings(void)
{
    RenderSet set;
    RenderSet_InitSet(&set, 16, 8, 4, false);

    APrimitive primA[2] = { MakePrimitive(0, 0, 9, 0, 3), MakePrimitive(0, 9, 9, 3, 3) };
    AMesh meshesA[1] = { { "A", primA, 2, 0, NULL } };
    SceneBundle bundleA = MakeBundle(meshesA, 1, NULL, 0, 1, 0);

    APrimitive primB[2] = { MakePrimitive(0, 18, 9, 0, 3), MakePrimitive(0, 27, 9, 3, 3) };
    AMesh meshesB[1] = { { "B", primB, 2, 0, NULL } };
    SceneBundle bundleB = MakeBundle(meshesB, 1, NULL, 0, 1, 10);

    RenderSet_AddSceneBundle(&set, &bundleA, 0);
    RenderSet_AddSceneBundle(&set, &bundleB, 1);

    Entity e = MakeEntity(INVALID_ENTITY);
    CHECK(RenderSet_AddEntity(&set, 0, &e) != INVALID_ENTITY, "add group 0");
    CHECK(RenderSet_AddEntity(&set, 2, &e) != INVALID_ENTITY, "add group 2 after middle shift");
    CHECK(RenderSet_AddEntity(&set, 1, &e) != INVALID_ENTITY, "add group 1 after group 2 has entity");

    CHECK(set.numEntities == 3, "numEntities=%u", set.numEntities);
    CHECK(set.primitiveGroups[0].entityOffset == 0 && set.primitiveGroups[0].numEntities == 1, "group0 range %u+%u", set.primitiveGroups[0].entityOffset, set.primitiveGroups[0].numEntities);
    CHECK(set.primitiveGroups[1].entityOffset == 1 && set.primitiveGroups[1].numEntities == 1, "group1 range %u+%u", set.primitiveGroups[1].entityOffset, set.primitiveGroups[1].numEntities);
    CHECK(set.primitiveGroups[2].entityOffset == 2 && set.primitiveGroups[2].numEntities == 1, "group2 range %u+%u", set.primitiveGroups[2].entityOffset, set.primitiveGroups[2].numEntities);
    CHECK(set.primitiveGroups[3].entityOffset == 3 && set.primitiveGroups[3].numEntities == 0, "group3 offset=%u", set.primitiveGroups[3].entityOffset);
    for (u32 i = 0; i < set.numEntities; i++)
        CHECK(set.denseToPrimitiveIndex[i] == i, "dense %u primitive=%u", i, set.denseToPrimitiveIndex[i]);
    CHECK(RenderSet_Validate(&set, "middle insertion"), "validation failed");
}

static void TestAddScenePlaceholderHierarchy(void)
{
    RenderSet set;
    RenderSet_InitSet(&set, 16, 8, 4, false);

    APrimitive prim0[2] = { MakePrimitive(0, 0, 9, 0, 3), MakePrimitive(0, 9, 9, 3, 3) };
    APrimitive prim1[1] = { MakePrimitive(1, 18, 12, 6, 4) };
    AMesh meshes[2] = {
        { "M0", prim0, 2, 0, NULL },
        { "M1", prim1, 1, 0, NULL }
    };
    ANode nodes[2] = { MakeMeshNode(0), MakeMeshNode(1) };
    SceneBundle bundle = MakeBundle(meshes, 2, nodes, 2, 2, 32);

    u32 bundleIdx = RenderSet_AddSceneBundle(&set, &bundle, 5);
    u32 added = RenderSet_AddScene(&set, bundleIdx, VecZero(), QIdentity(), VecSet1(1.0f), false);

    CHECK(added == 3, "added=%u", added);
    CHECK(set.numEntities == 3, "numEntities=%u", set.numEntities);
    CHECK(set.nextSparseID == 2, "nextSparseID=%u (per mesh node, not primitive)", set.nextSparseID);
    CHECK(set.entities[0].sparseIdx == set.entities[1].sparseIdx, "mesh0 primitives should share sparse id");
    CHECK(set.entities[2].sparseIdx != set.entities[0].sparseIdx, "mesh1 should get another sparse id");
    CHECK(RenderSet_Validate(&set, "add scene"), "validation failed");
}

static void TestSparseCapacityFailureDoesNotMutate(void)
{
    RenderSet set;
    RenderSet_InitSet(&set, 1, 2, 1, false);

    APrimitive prim[1] = { MakePrimitive(0, 0, 9, 0, 3) };
    AMesh meshes[1] = { { "A", prim, 1, 0, NULL } };
    SceneBundle bundle = MakeBundle(meshes, 1, NULL, 0, 1, 0);
    RenderSet_AddSceneBundle(&set, &bundle, 0);

    Entity two[2] = { MakeEntity(INVALID_ENTITY), MakeEntity(INVALID_ENTITY) };
    u32 result = RenderSet_AddEntities(&set, 0, 2, two);

    CHECK(result == INVALID_ENTITY, "result=%u", result);
    CHECK(set.numEntities == 0, "numEntities mutated to %u", set.numEntities);
    CHECK(set.primitiveGroups[0].numEntities == 0, "group count mutated to %u", set.primitiveGroups[0].numEntities);
    CHECK(set.nextSparseID == 0, "nextSparseID mutated to %u", set.nextSparseID);
    CHECK(RenderSet_Validate(&set, "sparse failure"), "validation failed");
}

int main(void)
{
    gGFX.SurfaceVertexBuffer = gSurfaceVertices;
    gGFX.SkinnedVertexBuffer = gSkinnedVertices;
    gGFX.IndexBuffer = gIndices;

    TestBundleRegistration();
    TestMiddleInsertionKeepsMappings();
    TestAddScenePlaceholderHierarchy();
    TestSparseCapacityFailureDoesNotMutate();

    printf("RenderSetTest: %d checks, %d failures\n", gChecks, gFailures);
    return gFailures ? 1 : 0;
}
