// Standalone test for SceneSerializer_Load entity reconstruction loop.
// Run from the repo root with:
//   Tests\RunSceneSerializerLoadLoopTest.bat

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Extern/miniperf.h"
#include "Include/RenderSet.h"
#include "Include/Bitset.h"

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

typedef struct TestSceneEntRecord_
{
    u64 rotation;
    u64 scale;
    u32 primGroupIdx;
    u32 sparseIdx;
} TestSceneEntRecord;

typedef struct TestSceneEntitySet_
{
    TestSceneEntRecord* records;
    u32 count;
    u32 capacity;
    u32 maxGroupIdx;
    u32 maxSparseIdx;
} TestSceneEntitySet;

typedef struct TestRestoreScratch_
{
    u32* primitiveCounts;
    u32 primitiveCountCapacity;
} TestRestoreScratch;

typedef struct TestBistroPerfCase_
{
    TestSceneEntitySet sets[2];
    RenderSet renderSets[2];
    TestRestoreScratch scratch[2];
    u32 iterations;
} TestBistroPerfCase;

static void TestRestoreLoop(RenderSet* set, TestRestoreScratch* scratch, const TestSceneEntRecord* records, u32 recordCount)
{
    if (scratch->primitiveCountCapacity < set->numGroups) return;

    u32* primitiveCounts = scratch->primitiveCounts;
    memset(primitiveCounts, 0, set->numGroups * sizeof(u32));
    u32 validEntities = 0;

    for (u32 i = 0; i < recordCount; i++)
    {
        const TestSceneEntRecord* record = &records[i];
        if (record->primGroupIdx >= set->numGroups)
            continue;
        if (record->sparseIdx >= set->maxEntities)
            continue;
        primitiveCounts[record->primGroupIdx]++;
        validEntities++;
    }

    u32 entityOffset = 0;
    for (u32 g = 0; g < set->numGroups; g++)
    {
        PrimitiveGroup* group = &set->primitiveGroups[g];
        group->entityOffset = entityOffset;
        u32 numEntities = primitiveCounts[g];
        group->numEntities = 0;
        group->capacity = numEntities;
        entityOffset += numEntities;
    }
    set->numEntities = validEntities;

    for (u32 i = 0; i < recordCount; i++)
    {
        const TestSceneEntRecord* record = &records[i];
        u32 groupIdx = record->primGroupIdx;
        if (groupIdx >= set->numGroups || record->sparseIdx >= set->maxEntities)
            continue;

        PrimitiveGroup* group = &set->primitiveGroups[groupIdx];
        u32 denseIdx = group->entityOffset + group->numEntities;
        Entity* entity = &set->entities[denseIdx];
        memset(entity, 0, sizeof(*entity));
        group->numEntities++;
        entity->rotation = record->rotation;
        entity->scale = record->scale;
        entity->primitiveIdx = groupIdx;
        entity->sparseIdx = record->sparseIdx;

        BitsetSet(set->sparseSlots, (s32)record->sparseIdx);
        if (set->sparseID[record->sparseIdx] == INVALID_ENTITY || denseIdx < set->sparseID[record->sparseIdx])
            set->sparseID[record->sparseIdx] = denseIdx;
    }
}

static u32 OldAllocateSparseID(RenderSet* set)
{
    s32 sparseIdx = BitsetFindFirstEmpty(set->sparseSlots, (s32)set->maxEntities);
    if (sparseIdx < 0)
        return INVALID_ENTITY;

    BitsetSet(set->sparseSlots, sparseIdx);
    return (u32)sparseIdx;
}

static u32 OldAddEntity(RenderSet* set, u32 primitiveIdx, const Entity* data)
{
    PrimitiveGroup* group = &set->primitiveGroups[primitiveIdx];
    u32 entityStart = group->entityOffset + group->numEntities;
    if (set->numEntities + 1u > set->maxEntities)
        return INVALID_ENTITY;

    for (s32 i = (s32)set->numEntities - 1; i >= (s32)entityStart; i--)
    {
        u32 dst = (u32)i + 1u;
        set->entities[dst] = set->entities[i];

        u32 sparseIdx = set->entities[dst].sparseIdx;
        if (sparseIdx != INVALID_ENTITY && sparseIdx < set->maxEntities && set->sparseID[sparseIdx] == (u32)i)
            set->sparseID[sparseIdx] = dst;
    }

    for (u32 i = primitiveIdx + 1u; i < set->numGroups; i++)
        set->primitiveGroups[i].entityOffset++;

    set->numEntities++;
    set->entities[entityStart] = *data;
    set->entities[entityStart].primitiveIdx = primitiveIdx;
    if (data->sparseIdx != INVALID_ENTITY && data->sparseIdx < set->maxEntities &&
        (set->sparseID[data->sparseIdx] == INVALID_ENTITY || entityStart < set->sparseID[data->sparseIdx]))
    {
        set->sparseID[data->sparseIdx] = entityStart;
    }
    group->numEntities++;
    group->capacity = group->numEntities;
    return entityStart;
}

static void OldRestoreLoop(RenderSet* set, const TestSceneEntRecord* records, u32 recordCount)
{
    for (u32 i = 0; i < recordCount; i++)
    {
        const TestSceneEntRecord* record = &records[i];
        if (record->primGroupIdx >= set->numGroups)
            continue;

        Entity entity;
        memset(&entity, 0, sizeof(entity));
        entity.rotation = record->rotation;
        entity.scale = record->scale;
        entity.sparseIdx = OldAllocateSparseID(set);
        if (entity.sparseIdx == INVALID_ENTITY)
            continue;
        OldAddEntity(set, record->primGroupIdx, &entity);
    }
}

static void ResetRestoreTarget(RenderSet* set)
{
    set->numEntities = 0;
    memset(set->sparseID, 0xFF, set->maxEntities * sizeof(u32));
    memset(set->sparseSlots, 0, ((set->maxEntities + 63u) >> 6) * sizeof(u64));
}

static void ResetOldRestoreTarget(RenderSet* set)
{
    ResetRestoreTarget(set);
    for (u32 g = 0; g < set->numGroups; g++)
    {
        PrimitiveGroup* group = &set->primitiveGroups[g];
        group->entityOffset = 0;
        group->numEntities = 0;
        group->capacity = 0;
    }
}

static void InitRenderSetStorage(RenderSet* set, u32 maxEntities, u32 numGroups)
{
    memset(set, 0, sizeof(*set));
    set->maxEntities = maxEntities;
    set->maxGroups = numGroups;
    set->numGroups = numGroups;
    set->entities = (Entity*)calloc(maxEntities, sizeof(Entity));
    set->primitiveGroups = (PrimitiveGroup*)calloc(numGroups, sizeof(PrimitiveGroup));
    set->sparseID = (u32*)malloc(maxEntities * sizeof(u32));
    set->sparseSlots = (u64*)calloc((maxEntities + 63u) >> 6, sizeof(u64));
    memset(set->sparseID, 0xFF, maxEntities * sizeof(u32));
}

static void FreeRenderSetStorage(RenderSet* set)
{
    free(set->sparseSlots);
    free(set->sparseID);
    free(set->primitiveGroups);
    free(set->entities);
}

static void InitRestoreScratch(TestRestoreScratch* scratch, u32 numGroups)
{
    scratch->primitiveCounts = (u32*)malloc(numGroups * sizeof(u32));
    scratch->primitiveCountCapacity = numGroups;
    CHECK(scratch->primitiveCounts != NULL, "scratch allocation failed");
}

static void FreeRestoreScratch(TestRestoreScratch* scratch)
{
    free(scratch->primitiveCounts);
    scratch->primitiveCounts = NULL;
    scratch->primitiveCountCapacity = 0;
}

static void PushSceneRecord(TestSceneEntitySet* set, TestSceneEntRecord record)
{
    if (set->count == set->capacity)
    {
        u32 newCapacity = set->capacity ? set->capacity * 2u : 256u;
        TestSceneEntRecord* records = (TestSceneEntRecord*)realloc(set->records, newCapacity * sizeof(TestSceneEntRecord));
        CHECK(records != NULL, "record allocation failed");
        if (!records) return;
        set->records = records;
        set->capacity = newCapacity;
    }

    set->records[set->count++] = record;
    if (record.primGroupIdx > set->maxGroupIdx) set->maxGroupIdx = record.primGroupIdx;
    if (record.sparseIdx > set->maxSparseIdx) set->maxSparseIdx = record.sparseIdx;
}

static int ParseSceneEntitySets(const char* path, TestSceneEntitySet sets[2])
{
    FILE* file = fopen(path, "rb");
    CHECK(file != NULL, "failed to open scene file: %s", path);
    if (!file) return 0;

    char line[2048];
    int activeSet = -1;
    u32 remaining = 0;
    while (fgets(line, sizeof(line), file))
    {
        u32 setIdx = 0;
        u32 count = 0;
        if (sscanf(line, "entities %u %u", &setIdx, &count) == 2)
        {
            CHECK(setIdx < 2u, "entity set index out of range: %u", setIdx);
            activeSet = setIdx < 2u ? (int)setIdx : -1;
            remaining = count;
            continue;
        }

        if (activeSet >= 0 && remaining > 0u)
        {
            TestSceneEntRecord record;
            float x, y, z;
            u32 rotLo, rotHi;
            unsigned long long scale;
            if (sscanf(line, "ent %u %f %f %f %u %u %llu %u",
                       &record.primGroupIdx, &x, &y, &z, &rotLo, &rotHi, &scale, &record.sparseIdx) == 8)
            {
                record.rotation = (u64)rotLo | ((u64)rotHi << 32u);
                record.scale = (u64)scale;
                PushSceneRecord(&sets[activeSet], record);
                remaining--;
            }
        }
    }

    fclose(file);
    return 1;
}

static void Test_DenseLayoutAndSparseMap(void)
{
    RenderSet set;
    InitRenderSetStorage(&set, 16, 4);
    TestRestoreScratch scratch;
    InitRestoreScratch(&scratch, set.numGroups);

    const TestSceneEntRecord records[] = {
        { .rotation = 10, .scale = 100, .primGroupIdx = 2, .sparseIdx = 7 },
        { .rotation = 11, .scale = 101, .primGroupIdx = 0, .sparseIdx = 3 },
        { .rotation = 12, .scale = 102, .primGroupIdx = 2, .sparseIdx = 5 },
        { .rotation = 13, .scale = 103, .primGroupIdx = 1, .sparseIdx = 9 },
        { .rotation = 14, .scale = 104, .primGroupIdx = 3, .sparseIdx = 5 },
        { .rotation = 99, .scale = 199, .primGroupIdx = 8, .sparseIdx = 2 },
        { .rotation = 98, .scale = 198, .primGroupIdx = 1, .sparseIdx = 32 },
    };

    TestRestoreLoop(&set, &scratch, records, (u32)(sizeof(records) / sizeof(records[0])));

    CHECK(set.numEntities == 5, "numEntities=%u expected=5", set.numEntities);
    CHECK(set.primitiveGroups[0].entityOffset == 0 && set.primitiveGroups[0].numEntities == 1 && set.primitiveGroups[0].capacity == 1,
          "group 0 range/cap invalid");
    CHECK(set.primitiveGroups[1].entityOffset == 1 && set.primitiveGroups[1].numEntities == 1 && set.primitiveGroups[1].capacity == 1,
          "group 1 range/cap invalid");
    CHECK(set.primitiveGroups[2].entityOffset == 2 && set.primitiveGroups[2].numEntities == 2 && set.primitiveGroups[2].capacity == 2,
          "group 2 range/cap invalid");
    CHECK(set.primitiveGroups[3].entityOffset == 4 && set.primitiveGroups[3].numEntities == 1 && set.primitiveGroups[3].capacity == 1,
          "group 3 range/cap invalid");

    const u32 expectedPrimitive[] = { 0, 1, 2, 2, 3 };
    const u32 expectedSparse[] = { 3, 9, 7, 5, 5 };
    for (u32 i = 0; i < set.numEntities; i++)
    {
        CHECK(set.entities[i].primitiveIdx == expectedPrimitive[i],
              "dense %u primitive=%u expected=%u", i, set.entities[i].primitiveIdx, expectedPrimitive[i]);
        CHECK(set.entities[i].sparseIdx == expectedSparse[i],
              "dense %u sparse=%u expected=%u", i, set.entities[i].sparseIdx, expectedSparse[i]);
    }

    CHECK(set.sparseID[3] == 0, "sparse 3 maps to %u expected=0", set.sparseID[3]);
    CHECK(set.sparseID[9] == 1, "sparse 9 maps to %u expected=1", set.sparseID[9]);
    CHECK(set.sparseID[7] == 2, "sparse 7 maps to %u expected=2", set.sparseID[7]);
    CHECK(set.sparseID[5] == 3, "duplicate sparse 5 maps to first dense %u expected=3", set.sparseID[5]);
    CHECK(BitsetGet(set.sparseSlots, 3), "sparse slot 3 not set");
    CHECK(BitsetGet(set.sparseSlots, 5), "sparse slot 5 not set");
    CHECK(BitsetGet(set.sparseSlots, 7), "sparse slot 7 not set");
    CHECK(BitsetGet(set.sparseSlots, 9), "sparse slot 9 not set");

    FreeRestoreScratch(&scratch);
    FreeRenderSetStorage(&set);
}

static void InitBistroPerfCase(TestBistroPerfCase* perf)
{
    memset(perf, 0, sizeof(*perf));
    if (!ParseSceneEntitySets("Assets/Test/Scenes/BistroGood.scene", perf->sets))
        return;

    for (u32 s = 0; s < 2u; s++)
    {
        TestSceneEntitySet* src = &perf->sets[s];
        u32 numGroups = src->maxGroupIdx + 1u;
        u32 maxEntities = src->maxSparseIdx + 1u;
        if (maxEntities < src->count) maxEntities = src->count;
        InitRenderSetStorage(&perf->renderSets[s], maxEntities + 1u, numGroups);
        InitRestoreScratch(&perf->scratch[s], numGroups);
    }
    perf->iterations = 1000;
}

static void FreeBistroPerfCase(TestBistroPerfCase* perf)
{
    for (u32 s = 0; s < 2u; s++)
    {
        FreeRestoreScratch(&perf->scratch[s]);
        FreeRenderSetStorage(&perf->renderSets[s]);
        free(perf->sets[s].records);
    }
}

static void Perf_BistroRestoreLoop(void* arg)
{
    TestBistroPerfCase* perf = (TestBistroPerfCase*)arg;
    for (u32 i = 0; i < perf->iterations; i++)
    {
        for (u32 s = 0; s < 2u; s++)
        {
            ResetRestoreTarget(&perf->renderSets[s]);
            TestRestoreLoop(&perf->renderSets[s], &perf->scratch[s], perf->sets[s].records, perf->sets[s].count);
        }
    }
}

static void Perf_BistroOldRestoreLoop(void* arg)
{
    TestBistroPerfCase* perf = (TestBistroPerfCase*)arg;
    for (u32 i = 0; i < perf->iterations; i++)
    {
        for (u32 s = 0; s < 2u; s++)
        {
            ResetOldRestoreTarget(&perf->renderSets[s]);
            OldRestoreLoop(&perf->renderSets[s], perf->sets[s].records, perf->sets[s].count);
        }
    }
}

static void PrintMiniPerfResult(const char* label, MiniPerfResult result, u32 iterations)
{
    printf("PERF %s: %.6f sec total, %.3f us/iter, ctxsw=%llu\n",
           label,
           result.ElapsedTime,
           (result.ElapsedTime * 1000000.0) / (double)iterations,
           (unsigned long long)result.ContextSwitches);
    printf("PERF %s: cycles=%llu instructions=%llu branchMiss=%llu branch=%llu dataMiss=%llu dataAccess=%llu\n",
           label,
           (unsigned long long)result.Counters[MP_CycleCount],
           (unsigned long long)result.Counters[MP_Instructions],
           (unsigned long long)result.Counters[MP_BranchMisses],
           (unsigned long long)result.Counters[MP_BranchCount],
           (unsigned long long)result.Counters[MP_DataMisses],
           (unsigned long long)result.Counters[MP_DataAccess]);
}

static void RunBistroPerfTest(void)
{
    TestBistroPerfCase perf;
    InitBistroPerfCase(&perf);
    if (perf.iterations == 0) return;

    MiniPerfResult newResult = MiniPerf(Perf_BistroRestoreLoop, &perf);
    PrintMiniPerfResult("SceneSerializer_BistroRestoreLoop_New", newResult, perf.iterations);

    MiniPerfResult oldResult = MiniPerf(Perf_BistroOldRestoreLoop, &perf);
    PrintMiniPerfResult("SceneSerializer_BistroRestoreLoop_Old", oldResult, perf.iterations);
    if (newResult.ElapsedTime > 0.0)
        printf("PERF SceneSerializer_BistroRestoreLoop speedup: %.2fx\n", oldResult.ElapsedTime / newResult.ElapsedTime);
    FreeBistroPerfCase(&perf);
}

static void Test_BistroGoodEntityLoop(void)
{
    TestSceneEntitySet sets[2];
    memset(sets, 0, sizeof(sets));
    if (!ParseSceneEntitySets("Assets/Test/Scenes/BistroGood.scene", sets))
        return;

    CHECK(sets[0].count > 0u, "BistroGood surface entity set is empty");
    CHECK(sets[1].count > 0u, "BistroGood skinned entity set is empty");

    for (u32 s = 0; s < 2u; s++)
    {
        TestSceneEntitySet* src = &sets[s];
        RenderSet set;
        u32 numGroups = src->maxGroupIdx + 1u;
        u32 maxEntities = src->maxSparseIdx + 1u;
        if (maxEntities < src->count) maxEntities = src->count;
        InitRenderSetStorage(&set, maxEntities + 1u, numGroups);
        TestRestoreScratch scratch;
        InitRestoreScratch(&scratch, numGroups);

        TestRestoreLoop(&set, &scratch, src->records, src->count);

        CHECK(set.numEntities == src->count, "set %u numEntities=%u expected=%u", s, set.numEntities, src->count);

        u32 counted = 0;
        for (u32 g = 0; g < set.numGroups; g++)
        {
            PrimitiveGroup* group = &set.primitiveGroups[g];
            CHECK(group->entityOffset == counted, "set %u group %u offset=%u expected=%u", s, g, group->entityOffset, counted);
            CHECK(group->capacity == group->numEntities, "set %u group %u cap=%u count=%u", s, g, group->capacity, group->numEntities);
            for (u32 e = 0; e < group->numEntities; e++)
            {
                Entity* entity = &set.entities[group->entityOffset + e];
                CHECK(entity->primitiveIdx == g, "set %u dense entity primitive=%u expected=%u", s, entity->primitiveIdx, g);
                CHECK(entity->sparseIdx < set.maxEntities, "set %u sparse out of range: %u", s, entity->sparseIdx);
                CHECK(BitsetGet(set.sparseSlots, (s32)entity->sparseIdx), "set %u sparse slot missing: %u", s, entity->sparseIdx);
                CHECK(set.sparseID[entity->sparseIdx] != INVALID_ENTITY, "set %u sparse map missing: %u", s, entity->sparseIdx);
            }
            counted += group->numEntities;
        }
        CHECK(counted == set.numEntities, "set %u counted=%u expected=%u", s, counted, set.numEntities);

        for (u32 i = 0; i < set.numEntities; i++)
        {
            u32 sparseIdx = set.entities[i].sparseIdx;
            CHECK(set.sparseID[sparseIdx] <= i, "set %u sparse %u maps forward to %u from dense %u", s, sparseIdx, set.sparseID[sparseIdx], i);
        }

        FreeRestoreScratch(&scratch);
        FreeRenderSetStorage(&set);
    }

    free(sets[0].records);
    free(sets[1].records);
}

int main(void)
{
    Test_DenseLayoutAndSparseMap();
    Test_BistroGoodEntityLoop();
    RunBistroPerfTest();
    printf("SceneSerializerLoadLoopTest: %d checks, %d failures\n", gChecks, gFailures);
    return gFailures ? 1 : 0;
}
