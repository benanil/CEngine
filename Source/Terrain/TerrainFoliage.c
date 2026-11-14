#include "Include/RenderSet.h"
#include "Include/FileSystem.h"
#include "Include/Platform.h"
#include "Include/Graphics.h"
#include "Include/JobSystem.h"
#include "Include/ParallelFor.h"
#include "Include/TextureSystem.h"
#include "Include/Memory.h"
#include "Include/Scene.h"
#include "Include/Terrain.h"
#include "Include/Random.h"
#include "Include/Algorithm.h"
#include "Source/Terrain/TerrainInternal.h"
#include "Math/Noise.h"
#include "Math/Quaternion.h"
#include "Math/Bitpack.h"
#include "Include/UIWindow.h"
#include <box3d/box3d.h>

// discrete procedural foliage: one entity per placement, placed by worker jobs and
// attached to the chunk that owns it. renders through gFoliage.scene's own render sets
// (not merged into the game scene); colliders attach to the global physics world instead.

#define T_MAX_FOLIAGE_SCENE     64u
#define T_FOLIAGE_MAX_PATH      512
#define T_FOLIAGE_NAME_LEN      64
#define T_MAX_FOLIAGE_GROUPS    64 // WARNING used with bitset if you go above 64 be aware
#define T_FOLIAGE_GROUND_SNAP_OFFSET 0.1f

#define T_MIN_DENSITY 0.25f
#define T_NOISE_AXIS_MAX (int)(T_CHUNK_CELLS * (1.0f / T_MIN_DENSITY)) 

typedef struct tFoliageType_
{
    char  path[T_FOLIAGE_MAX_PATH];
    char  name[T_FOLIAGE_NAME_LEN]; // display label: file name, no directory/extension
    u32   bundleIdx; // in foliage scene
    u32   numEntities;   // num nodes, CountNumEntities
    f32   localBaseY;    // lowest local-space point across all primitive groups
    f32   normalAlign;   // terrain-normal tilt amount; slender meshes stay mostly upright
    b3HullData* hull;    // native-scale convex hull, built lazily on first collider request
    FoliageParams params;
} FoliageType;

extern Graphics gGFX; // cpu mega buffers, declared per translation unit as elsewhere (BVH.c, Physics.c)

typedef struct tFoliagePlacement_
{
    u64 localPos;    // Pack16x4Fixed chunk-relative position
    u64 rotation;    // packed final terrain-aligned rotation
    f32 scale;       // final uniform scale (size * variance), pre-multiplied on the worker
    u32 typeIndex;
} tFoliagePlacement;

typedef struct tFoliageJob_
{
    int3        chunkMin;
    const s8*   density;
    // true for a chunk that has never had foliage decided (see tChunk.foliageBuilt): place
    // every currently-enabled type for it, not just the ones flagged paramsDirty this round.
    bool        placeAllEnabled;
    u32         count;
    tFoliagePlacement placements[T_FOLIAGE_MAX_PER_CHUNK];
} tFoliageJob;

// A resolved placement awaiting batching into RenderSet_AddEntities.
// Transform is computed once per placement; physicsBody attaches only to render group 0.
typedef struct tFoliageBatchItem_
{
    tChunk* chunk;
    v128f   position;
    u64     rotation;
    u64     scale;
} tFoliageBatchItem;

typedef struct tFoliageState_
{
    Scene scene;
    u32   numTypes;
    PCG   pcg;
    u32   seed;
    u64   changedGroups;

    FoliageType types[T_MAX_FOLIAGE_SCENE];
    tFoliageJob  jobs[T_MAX_CHUNKS]; // one slot per resident chunk, all dispatched and
                                     // awaited within a single Foliage_Update call
} tFoliageState;

static tFoliageState gFoliage = {0};
static f32 densityNoiseScale = 1.0f / T_CHUNK_CELLS; 
static f32 densityVary = 0.5f;
static f32 rarityNoiseScale = 1.0f / (T_CHUNK_CELLS * 4.0f);
static f32 rarityVary = 0.1f;

Scene* GetFoliageScene() {
    return &gFoliage.scene;
}

static void VisitFile(const char* path, void* data)
{
    (void)data;
    int pathLen = StringLength(path);
    u8 isMesh = FileHasExtension(path, pathLen, ".fbx") || FileHasExtension(path, pathLen, ".gltf") ||
                FileHasExtension(path, pathLen, ".obj") || FileHasExtension(path, pathLen, ".glb");
    if (!isMesh) return;

    if (gFoliage.numTypes >= T_MAX_FOLIAGE_SCENE || pathLen <= 0 || pathLen >= T_FOLIAGE_MAX_PATH) {
        AX_WARN("foliage path invalid");
        return;
    }

    FoliageType* type = &gFoliage.types[gFoliage.numTypes++];
    MemSet(type, 0, sizeof(*type));
    NormalizePath(path, type->path, T_FOLIAGE_MAX_PATH);
    GetFileNameNoExt(type->path, type->name);
    type->bundleIdx = INVALID_BUNDLE;
    type->params = (FoliageParams){
        .density      = RepeatMinMaxF32(PCGNext(&gFoliage.pcg), 7.5f, 10.0),
        .rarity       = RepeatMinMaxF32(PCGNext(&gFoliage.pcg), 0.8f, 0.9),
        .size         = 1.0f,
        .frequency    = 0.5,
        .groupIndex   = 0u,
        .enabled      = true, // off by default until placement/visuals are verified per type
        .colliderEntity     = false,
        .sizeVariance = true,
    };
}

void Foliage_Init()
{
    MemSet(&gFoliage, 0, sizeof(gFoliage));
    gFoliage.pcg = (PCG){ 0x853c49e6748fea9bULL, 0xda3e39cb94b95bdbULL };
    Scene_Init(&gFoliage.scene);
    gFoliage.scene.isFoliageScene = true;
    // foliage objects needs more light for faking outdoor GI
    gFoliage.scene.ambientBoost = 16.0f;

    if (VisitFolder("Assets/Foliage", VisitFile, NULL, true) == 0) {
        AX_WARN("foliage file traverse failed!");
        return;
    }
    if (gFoliage.numTypes == 0) return;

    // Stage every foliage bundle's mesh+texture load across worker threads via ParallelFor.
    // Finalize (RenderSet/TextureSystem/AnimationSystem mutation) still runs serially on the
    // main thread afterwards, same as the rest of Scene_Init's callers.
    SceneBundleStage* stages = CAllocTLSFArray(SceneBundleStage, T_MAX_FOLIAGE_SCENE);
    for (u32 i = 0; i < gFoliage.numTypes; i++)
    {
        stages[i].path      = StringDuplicate(gFoliage.types[i].path);
        stages[i].cacheKey  = StringToHash64(stages[i].path);
        stages[i].skinned   = false;
    }
    ParallelFor(gFoliage.numTypes, 1u, SceneStageRange, &(SceneStageRangeCtx){ stages, false });
    RenderSet* set = &gFoliage.scene.surfaceSet;

    for (u32 i = 0; i < gFoliage.numTypes; i++)
    {
        FoliageType* type = &gFoliage.types[i];
        AX_LOG("foliage: %s", type->path);
        u32 bundleIdx = Scene_AddBundleFinalize(&gFoliage.scene, &stages[i]);
        type->bundleIdx = bundleIdx;
        if (bundleIdx == INVALID_BUNDLE) continue;

        type->params.colliderRadius = 1.0f;
        type->params.colliderType   = b3_shapeTypeCount;
        type->params.colliderEntity = 0;
        type->numEntities = CountNumEntities(set, bundleIdx, 1, NULL, false);
        Range range = set->bundlePrimRange[bundleIdx];
        if (range.count == 0u) continue;
        
        const PrimitiveGroup* firstGroup = &set->primitiveGroups[range.start];
        v128f localBoundsMin = firstGroup->aabbMin;
        v128f localBoundsMax = firstGroup->aabbMax;
        for (u32 g = range.start + 1; g < range.start + range.count; g++)
        {
            const PrimitiveGroup* group = &set->primitiveGroups[g];
            localBoundsMin = VecMin(localBoundsMin, group->aabbMin);
            localBoundsMax = VecMax(localBoundsMax, group->aabbMax);
        }
        type->localBaseY = VecGetY(localBoundsMin);
        f32 height = VecGetY(localBoundsMax) - VecGetY(localBoundsMin);
        f32 widthX = VecGetX(localBoundsMax) - VecGetX(localBoundsMin);
        f32 widthZ = VecGetZ(localBoundsMax) - VecGetZ(localBoundsMin);
        f32 horizontal = Sqrtf(widthX * widthX + widthZ * widthZ);
        f32 aspect = height / Maxf32(horizontal, 1.0e-3f);
        f32 tallness = Maxf32(aspect, 1.0f);
        type->normalAlign = Clampf32(1.0f / (tallness * tallness * tallness), 0.03f, 0.75f);
    }
    gFoliage.changedGroups = ~0ull;
    DeAllocTLSF(stages);
}

void Foliage_Destroy()
{
    for (u32 i = 0; i < gFoliage.numTypes; i++)
        if (gFoliage.types[i].hull) b3DestroyHull(gFoliage.types[i].hull);
    Scene_Destroy(&gFoliage.scene);
    MemSet(&gFoliage, 0, sizeof(gFoliage));
}

void Foliage_SetSeed(u32 seed){
    gFoliage.seed = seed;
}

Scene* Foliage_GetScene(void) { return &gFoliage.scene; }

u32 Foliage_NumTypes(void) { return gFoliage.numTypes; }

const char* Foliage_TypeName(u32 index)
{
    return index < gFoliage.numTypes ? gFoliage.types[index].name : "noname-prop";
}

bool Foliage_GetParams(u32 index, FoliageParams* out)
{
    if (!out || index >= gFoliage.numTypes) return false;
    *out = gFoliage.types[index].params;
    return true;
}

s32 Foliage_BaseTypeOfGroup(s32 group)
{
    // first encounter is base group
    for (s32 i = 0; i < gFoliage.numTypes; i++)
        if (gFoliage.types[i].params.groupIndex == group)
            return i;
    return 0;
}

void Foliage_SetParams(u32 index, const FoliageParams* params)
{
    if (!params || index >= gFoliage.numTypes) return;
    FoliageType* type = &gFoliage.types[index];
    type->params = *params;
    u32 group = Foliage_BaseTypeOfGroup(params->groupIndex);
    BitsetSet(&gFoliage.changedGroups, params->groupIndex);
    bool isBaseType = group == params->groupIndex;
    if (!isBaseType) return;

    for (int i = 0; i < gFoliage.numTypes; i++)
    {
        if (gFoliage.types[i].params.groupIndex == params->groupIndex)
        {
            // these are shared properties, not all values has to be shared
            gFoliage.types[i].params.density   = params->density;
            gFoliage.types[i].params.rarity    = params->rarity;
            gFoliage.types[i].params.frequency = params->frequency;
        }
    }
}
#define TERRAIN_FOLIAGE_VERSION 1
void Foliage_Save(const char* path)
{
    AFile file = AFileOpen(path, AOpenFlag_WriteText);
    AFileWriteI32(TERRAIN_FOLIAGE_VERSION, file);
    AFileWrite("numFoliages:", StringLength("numFoliages:"), file, 1);
    AFileWriteI32(gFoliage.numTypes, file);
    AFileWrite("\n", 1, file, 1);

    for (int i = 0; i < gFoliage.numTypes; i++)
    {
        FoliageType type = gFoliage.types[i];
        FoliageParams params = type.params;
        AFileWrite(type.path, StringLength(type.path), file, 1);
        AFileWriteNamedF32("\ndensity:" , params.density   , file);
        AFileWriteNamedF32(" rarity:"    , params.rarity    , file);
        AFileWriteNamedF32(" size:"      , params.size      , file);
        AFileWriteNamedF32(" groupIndex:", params.groupIndex, file);
        AFileWriteNamedF32(" frequency:" , params.frequency , file);
        AFileWriteNamedI32(" enabled:"     , params.enabled, file);
        AFileWriteNamedI32(" collider:"    , params.colliderType, file);
        AFileWriteNamedI32(" sizeVariance:", params.sizeVariance, file);
        AFileWriteNamedI32(" colliderEntity:", (s32)params.colliderEntity, file);
        AFileWriteNamedI32(" colliderRadius:", params.colliderRadius, file);
        AFileWrite("\n", 1, file, 1);
    }
    AFileClose(file);
}

void Foliage_Load(const char* filePath)
{
    char* txt = ReadAllTextAlloc(filePath, NULL, NULL);
    if (!txt) { AX_LOG("terrain foliage save file couldnt found"); return; }
    s32 version = *txt++;
    int numFoliages = 0;
    const char* p = ParsePositiveNumber(txt, &numFoliages);

    for (int i = 0; i < numFoliages; i++)
    {
        char path[256] = { 0 };
        int strLen = 0;
        p = SkipWhiteSpace(p);
        while (*p && !IsWhitespace(*p))
            path[strLen++] = *p++;
        // match with live foliages if exists change its params
        FoliageType* type = NULL;
        FoliageParams* params = NULL;
        for (int i = 0; i < gFoliage.numTypes; i++)
        {
            if (StringEqual(gFoliage.types[i].path, path, strLen))
            {
                type = &gFoliage.types[i];
                params = &type->params;
                break;
            }
        }
        if (!params)
        {
            AX_WARN("foliage couldn't found! %s", path);
            params = &gFoliage.types[T_MAX_FOLIAGE_SCENE - 1].params; // DUMMY
        }
        p = ParseFloat(p, &params->density   );
        p = ParseFloat(p, &params->rarity    );
        p = ParseFloat(p, &params->size      );
        p = ParseFloat(p, &params->groupIndex);
        p = ParseFloat(p, &params->frequency );
        p = ParsePositiveNumber(p, &params->enabled);
        p = ParsePositiveNumber(p, (s32*)&params->colliderType);
        p = ParsePositiveNumber(p, (s32*)&params->sizeVariance);
        if (version == '1') p = ParseFloat(p, &params->colliderEntity);
        if (version == '1') p = ParseFloat(p, &params->colliderRadius);
    }
    FreeAllText(--txt);
}

void Foliage_RandomizeParams(void)
{
    for (u32 i = 0; i < gFoliage.numTypes; i++)
    {
        FoliageType* type = &gFoliage.types[i];
        type->params.density = RepeatMinMaxF32(PCGNext(&gFoliage.pcg), 3.5f, 8.0f);
        type->params.rarity = RepeatMinMaxF32(PCGNext(&gFoliage.pcg), 0.85f, 0.95f);
    }
    gFoliage.changedGroups = ~0ull;
}

// inverse of TerrainDensity_Quantize (>0 air, <0 solid). NOT T_DENSITY_DECODE_SCALE -
// that's negated on purpose for MarchingMesher's own flipped internal convention
#define T_FOLIAGE_DENSITY_DECODE_SCALE (TERRAIN_SDF_CLAMP / 127.0f)

// trilinear sample of the cached 19^3 density grid at a chunk-local point (0..T_CHUNK_CELLS).
// GetDensityFromTerrainPoint, cheap lookup instead of an analytic field eval. sample
// (ix,iy,iz) = world corner (ix-1,iy-1,iz-1), same layout tMesherDensityAt reads
static f32 tFoliageSampleDensity(const s8* density, float3 localPos)
{
    f32 px = Clampf32(localPos.x, 0.0f, (f32)T_CHUNK_CELLS);
    f32 py = Clampf32(localPos.y, 0.0f, (f32)T_CHUNK_CELLS);
    f32 pz = Clampf32(localPos.z, 0.0f, (f32)T_CHUNK_CELLS);

    f32 fx = Floorf32(px), fy = Floorf32(py), fz = Floorf32(pz);
    s32 x0 = (s32)fx + 1, x1 = x0 + 1;
    s32 y0 = (s32)fy + 1, y1 = y0 + 1;
    s32 z0 = (s32)fz + 1, z1 = z0 + 1;
    f32 xd = px - fx, yd = py - fy, zd = pz - fz;

    const s32 axis = T_SAMPLES_AXIS;
    // reads tChunk.density directly (see MarchingTerrain.c's tBuildDensity / IntegrateFinishedBuilds),
    // which is already transposed to [x][y][z] for the mesher - no separate raw copy exists anymore.
    #define TFOL_AT(ix, iy, iz) ((f32)density[(ix) * axis * axis + (iy) * axis + (iz)] * T_FOLIAGE_DENSITY_DECODE_SCALE)
    f32 c000 = TFOL_AT(x0, y0, z0), c100 = TFOL_AT(x1, y0, z0);
    f32 c010 = TFOL_AT(x0, y1, z0), c110 = TFOL_AT(x1, y1, z0);
    f32 c001 = TFOL_AT(x0, y0, z1), c101 = TFOL_AT(x1, y0, z1);
    f32 c011 = TFOL_AT(x0, y1, z1), c111 = TFOL_AT(x1, y1, z1);
    #undef TFOL_AT

    f32 c00 = c000 * (1.0f - xd) + c100 * xd;
    f32 c10 = c010 * (1.0f - xd) + c110 * xd;
    f32 c01 = c001 * (1.0f - xd) + c101 * xd;
    f32 c11 = c011 * (1.0f - xd) + c111 * xd;
    f32 c0 = c00 * (1.0f - yd) + c10 * yd;
    f32 c1 = c01 * (1.0f - yd) + c11 * yd;
    return c0 * (1.0f - zd) + c1 * zd;
}

// GetNearestSurfaceY: March top-down for the first air-to-solid crossing.
// Prevents latching onto cave ceilings/overhangs (fixing floating foliage).
// Rejects buried chunks, thin shells, and steep slopes.
// Out: false if no valid surface found.
static bool tFoliageFindSurfaceY(const s8* density, f32 x, f32 z, f32* outY, float3* outNormal)
{
    const f32 step = 0.5f, fineStep = 0.1f, maxY = (f32)T_CHUNK_CELLS;
    float3 p = { x, maxY, z };
    f32 d = tFoliageSampleDensity(density, p);
    if (d < 0.0f) return false; // ceiling already solid: buried, real surface is above

    f32 y = maxY;
    while (d >= 0.0f)
    {
        y -= step;
        if (y < 0.0f) return false; // no ground here, real surface is in a lower chunk
        p.y = y;
        d = tFoliageSampleDensity(density, p);
    }
    while (d < 0.0f && y < maxY) 
    {
        y += fineStep;
        p.y = y;
        d = tFoliageSampleDensity(density, p);
    }
    y = Clampf32(y - fineStep * 0.5f, 0.0f, maxY);

    if (tFoliageSampleDensity(density, (float3){ x, y - 0.75f, z }) >= 0.0f) return false; // thin/floating shell

    const f32 e = 0.5f;
    float3 grad = {
        tFoliageSampleDensity(density, (float3){ x + e, y, z }) - tFoliageSampleDensity(density, (float3){ x - e, y, z }),
        tFoliageSampleDensity(density, (float3){ x, y + e, z }) - tFoliageSampleDensity(density, (float3){ x, y - e, z }),
        tFoliageSampleDensity(density, (float3){ x, y, z + e }) - tFoliageSampleDensity(density, (float3){ x, y, z - e })
    };
    float3 normal = F3NormSafe(grad);
    if (normal.y < 0.6f) return false; // too steep, mirrors grass's slope cutoff

    *outY = y;
    *outNormal = normal;
    return true;
}

// fills rnd[0..count) with a chained WangHashx4 stream, 4 lanes/store. count must be a
// multiple of 4 (noiseAxis is forced to one, and noiseAxis*noiseAxis*4 stays one too).
static void GetRandom(u32 s, s32 count, f32* rnd)
{
    v128u seed = VeciMul(VeciSet1(s), VeciSetR(1, 234097, 12345, 314159265));
    for (s32 i = 0; i < count; i += 4) {
        seed = WangHashx4(seed);
        VecStore(rnd + i, VecNextFloat(seed));
    }
}

static void GetExistance(s32 chunkX, s32 chunkZ, s32 axis, f32 frequency, u32 seed, f32* cx)
{
    u32 hash = WangHash(seed ^ 0xA511E9B3U);
    f32 offsetX = (f32)(hash & 0xFFFFu) * 0.481632f;
    f32 offsetZ = (f32)(hash >> 16) * 0.34579f;
    for (s32 x = 0; x < axis; x++)
    {
        v128f worldX = VecSet1(chunkX + x + offsetX);
        for (s32 z = 0; z < axis; z += 4)
        {
            v128f worldZ = VecAdd(VecSet1(chunkZ + z + offsetZ), Vec0123());
            Vec2W worldXZ = (Vec2W){ worldX, worldZ };
            v128f p = PerlinDistinct2FBM(worldXZ, frequency, 0.0f, seed ^ 0xFDA548Eu);
            VecStore(cx + x * axis + z, p);
        }
    }
}

static void GetDensityPerlin(s32 chunkX, s32 chunkZ, v128f density, s32 axis, u32 seed, f32* dpx, f32* dpz)
{
    v128f densityVaryV = VecMulf(density, densityVary);
    for (s32 i = 0; i < axis; i += 4)
    {
        v128f worldX = VecMulf(VecAdd(VecSet1(chunkX + i), Vec0123()), densityNoiseScale);
        v128f worldZ = VecMulf(VecAdd(VecSet1(chunkZ + i), Vec0123()), densityNoiseScale);
        v128f px = Perlin1(worldX, VeciSet1(seed));
        v128f pz = Perlin1(worldZ, VeciSet1(seed + 123u));
        px = VecClampf(VecFmadd(px, densityVaryV, density), T_MIN_DENSITY, T_CHUNK_CELLS-1.0f);
        pz = VecClampf(VecFmadd(pz, densityVaryV, density), T_MIN_DENSITY, T_CHUNK_CELLS-1.0f);
        VecStore(dpx + i, VecLerp(px, pz, 0.25f)); // make x,z densities a bit closer
        VecStore(dpz + i, VecLerp(pz, px, 0.25f));
    }
}

static void GetRarityGate(s32 chunkX, s32 chunkZ, s32 axis, f32 rarity, f32* r, u32 seed)
{
    for (s32 x = 0; x < axis; x++)
    {
        v128f worldX = VecMulf(VecSet1(chunkX + x), rarityNoiseScale);
        for (s32 z = 0; z < axis; z += 4)
        {
            v128f worldZ = VecMulf(VecAdd(VecSet1(chunkZ + z), Vec0123()), rarityNoiseScale);
            v128f sr = Perlin2FBM(Vec2WFromV(worldX, worldZ), 2, 0.5f, 2.0f, seed);
            sr = VecFmadd(sr, VecSet1(rarityVary), VecSet1(rarity));
            VecStore(r + x * axis + z, VecClamp01(sr));
        }
    }
}

static Quaternion CreateFoliageRotation(float3 normal, f32 yaw, f32 normalAlign)
{
    float3 up = F3Up();
    float3 axis = F3Cross(up, normal);
    f32 upDot = Clampf32(F3Dot(up, normal), -1.0f, 1.0f);
    f32 axisLength = F3Dot(axis, axis);
    Quaternion tilt = QIdentity();
    if (axisLength > 1.0e-6f)
        tilt = QFromAxisAngle(F3NormSafe(axis), Minf32(ACos(upDot) * normalAlign, MATH_PI * 0.25f));

    Quaternion aroundUp = QFromAxisAngle(up, yaw);
    return QNormEst(QMul(aroundUp, tilt));
}

static u8 GetFoliageGroups(u8 group, u8 foliages[T_MAX_FOLIAGE_SCENE])
{
    u8 activeGroups = 0;
    for (u32 t = 0; t < gFoliage.numTypes; t++)
    {
        const FoliageType* type = &gFoliage.types[t];
        if (type->params.groupIndex == group && type->bundleIdx != INVALID_BUNDLE && type->params.enabled)
            foliages[activeGroups++] = t; 
    }
    return activeGroups;
}

// worker thread: pure placement, touches only this job slot's output and the read-only
// foliage type table. no RenderSet/physics access happens here (main-thread only)
static void RunFoliageJob(void* userData)
{
    tFoliageJob* job = (tFoliageJob*)userData;
    job->count = 0u;
    ALIGNSIMD f32 rnd[T_NOISE_AXIS_MAX * T_NOISE_AXIS_MAX * 6];
    ALIGNSIMD f32 existance[T_NOISE_AXIS_MAX * T_NOISE_AXIS_MAX];
    ALIGNSIMD f32 rarity[T_NOISE_AXIS_MAX * T_NOISE_AXIS_MAX];
    ALIGNSIMD f32 densityX[T_NOISE_AXIS_MAX];
    ALIGNSIMD f32 densityY[T_NOISE_AXIS_MAX];
    
    for (u8 g = 0; g < T_MAX_FOLIAGE_GROUPS; g++)
    {
        u8 group = g;
        u8 foliages[T_MAX_FOLIAGE_SCENE] = {0};
        u8 numFoliages = GetFoliageGroups(group, foliages);
        bool changed = BitsetGet(&gFoliage.changedGroups, g) || job->placeAllEnabled;
        if (numFoliages == 0 || !changed) continue;
        
        const FoliageType* baseType = &gFoliage.types[foliages[0]];
        u32 seed = WangHash(gFoliage.seed + (u32)job->chunkMin.x * 73856093u ^ (u32)job->chunkMin.z * 19349663u ^ (group * 83492791u + 1u));
        f32 density = Maxf32(baseType->params.density, T_MIN_DENSITY);
        f32 rarityGate = Clampf32(baseType->params.rarity, 0.05f, 1.0f);
        u32 typeNoiseSeed = WangHash((group + 1u) * 0x9e3779b9u + gFoliage.seed);
        f32 noiseFrequency = baseType->params.frequency * 0.05f;
        
        s32 noiseAxis = (s32)((f32)T_CHUNK_CELLS / density);
        noiseAxis = Clamps32((noiseAxis + 3) & ~3, 4, T_NOISE_AXIS_MAX);

        GetRandom(seed, noiseAxis * noiseAxis * 6, rnd);
        GetExistance(job->chunkMin.x, job->chunkMin.z, noiseAxis, noiseFrequency, typeNoiseSeed, existance);
        GetDensityPerlin(job->chunkMin.x, job->chunkMin.z, VecSet1(density), noiseAxis, typeNoiseSeed, densityX, densityY);
        GetRarityGate(job->chunkMin.x, job->chunkMin.z, noiseAxis, rarityGate, rarity, typeNoiseSeed);
        s32 rId = 0, ix = 0, iz = 0;
        for (f32 x = density * 0.5f; x < T_CHUNK_CELLS && ix < noiseAxis; x += densityX[ix], ix++)
        {
            iz = 0;
            for (f32 z = density * 0.5f; z < T_CHUNK_CELLS && iz < noiseAxis; z += densityY[iz], iz++)
            {
                u32 noiseIdx = ix * noiseAxis + iz;
                if (job->count >= T_FOLIAGE_MAX_PER_CHUNK) return;
                if (existance[noiseIdx] < rarity[noiseIdx]) continue;
                u32 foliageIndex = (u32)Clampf32((rnd[rId] + 1.0f) * numFoliages * 0.5f, 0.0f, numFoliages-1.0f); 
                foliageIndex = foliages[foliageIndex];
                const FoliageType* type = &gFoliage.types[foliageIndex];
                
                f32 localX = Clampf32(rnd[rId++] * density * 0.2f + x, 0.0f, T_CHUNK_CELLS);
                f32 localZ = Clampf32(rnd[rId++] * density * 0.2f + z, 0.0f, T_CHUNK_CELLS);
                f32 localY; float3 normal;
                if (!tFoliageFindSurfaceY(job->density, localX, localZ, &localY, &normal)) continue;
                
                float3 localPos = F3Sub((float3){ localX, localY, localZ }, F3MulF(normal, T_FOLIAGE_GROUND_SNAP_OFFSET));
                tFoliagePlacement* placement = &job->placements[job->count++];
                placement->localPos = Pack16x4Fixed(Vec3Load(&localPos.x), (f32)T_CHUNK_CELLS);
                placement->rotation = PackQuaternionS16NormRet(CreateFoliageRotation(normal, rnd[rId++] * MATH_PI, type->normalAlign));
                placement->scale = 1.0f + (type->params.sizeVariance * rnd[rId++] * 0.25f);
                placement->typeIndex = foliageIndex;
            }
        }
    }
}

// Safe because group entries remain contiguous and never reorder.
static void ScheduleFoliageJob(JobSystem* js, tFoliageJob* job, const tChunk* chunk, bool placeAllEnabled)
{
	if (!chunk->density) return; // chunk had no cached density grid yet, nothing to sample
    job->chunkMin = chunk->min;
    job->density = chunk->density;
    job->placeAllEnabled = placeAllEnabled;
    job->count = 0u;
    JobSystem_Execute(js, RunFoliageJob, job);
}

void Foliage_ClearEntities()
{
    RenderSet_ClearEntities(&gFoliage.scene.surfaceSet);
}

void Foliage_DestroyChunkFoliage(tChunk* chunk)
{
    if (!PTR_VALID(chunk)) return;

    RenderSet* set = &gFoliage.scene.surfaceSet;
    for (s32 j = 0; j < chunk->foliageCount; j++) 
    {
        FoliageEntity foliage = chunk->foliageEntities[j];
        RenderSet_RemoveEntityRangeStaged(set, foliage.range);
    }
    
    chunk->foliageCount = 0u;
}

static void SetFolliageCollider(const FoliageType* type, u32 numEntityAdded, RenderSet* set, u32 sparseCursor, float rise)
{
    if (type->params.colliderEntity >= numEntityAdded ||
        type->params.colliderType == b3_compoundShape ||
        type->params.colliderType == b3_heightShape   ||
        type->params.colliderType == b3_shapeTypeCount)
        return;
    u32 colliderEntity = type->params.colliderEntity;
    Entity* rootEntity = &set->entities[set->sparseData[sparseCursor + colliderEntity].id];
    EntityAddFlags(rootEntity, EntityFlags_ColliderEnabled);

    const PrimitiveGroup* group = &set->primitiveGroups[EntityGetPrimitiveID(rootEntity)];
    v128f scaleV = EntityGetScaleV(rootEntity);
    v128f localMin = VecMul(group->aabbMin, scaleV);
    v128f localMax = VecMul(group->aabbMax, scaleV);
    v128f center = VecMulf(VecAdd(localMin, localMax), 0.5f);
    v128f half   = VecMax(VecMulf(VecSub(localMax, localMin), 0.5f), VecSet1(0.01f));
    f32 hx = VecGetX(half), hy = VecGetY(half), hz = VecGetZ(half);
    
    b3Capsule capsule = {
        .center1 = { 0.0f, rise, 0.0f },
        .center2 = { 0.0f, rise + (hy * 2.0f), 0.0f },
        .radius  = type->params.colliderRadius
    };
    b3Sphere sphere = { .center = v128fToB3Vec3(center), .radius = type->params.colliderRadius };
    b3BoxHull box;
    if (type->params.colliderType == b3_hullShape)
        box = b3MakeOffsetBoxHull(hx, hy, hz, v128fToB3Vec3(center));

    void* data;
    switch (type->params.colliderType)
    {
        case b3_capsuleShape: data = &capsule; break;
        case b3_hullShape:    data = &box;    break; 
        case b3_sphereShape:  data = &sphere; break;
        default: data = NULL;
    }

    Entity* firstEntity = &set->entities[set->sparseData[sparseCursor].id];
    EntitySetSparseID(firstEntity, sparseCursor + colliderEntity);
    EntitySetSparseID(rootEntity, sparseCursor);
    // swap first entity with root
    XSWAP(SparseData, set->sparseData[sparseCursor + colliderEntity], set->sparseData[sparseCursor]);
    SparseData* rootSparse  = &set->sparseData[sparseCursor];
    
    // unparent all entities
    for (s32 i = 1; i < numEntityAdded; i++)
    {
        SparseData* sparse = &set->sparseData[sparseCursor + i];
        SparseDataRemoveFromParent(set, sparse);
        sparse->parent = i; // set to root
    }

    rootSparse->subNodeOffset = U16SetGetEmptyRange(set->subNodes, set->maxEntities, numEntityAdded - 1);
    rootSparse->subNodeCount = numEntityAdded - 1;
    if (rootSparse->subNodeOffset == UINT32_MAX) { AX_WARN("subnode allocation failed foliage"); }
    else for (s32 i = 1; i < numEntityAdded; i++)
    {
        set->subNodes[rootSparse->subNodeOffset + i - 1] = i;
    }
    PhysicsCreateEntityBody(set->hookScene, rootEntity, data, type->params.colliderType);
}

// Runs once per Foliage_Update burst after JobSystem_Wait.
// Bulk-adds dirty placements via RenderSet_AddEntities per (type, render group).
static void IntegrateFinishedFoliage(u32 scheduledCount)
{
    if (scheduledCount == 0u) return;

    RenderSet* set = &gFoliage.scene.surfaceSet;
    tChunk** resolvedChunks = ArenaAllocGlobal(scheduledCount * sizeof(tChunk*));
    u32* primitiveCounts = ArenaAllocGlobal(set->numGroups * sizeof(u32));
    u32 totalEntityAdded = 0;
    u32 totalEntityRemoved = 0;
    MemSet32((s32*)primitiveCounts, 0, set->numGroups);

    for (u32 i = 0; i < scheduledCount; i++)
    {
        tFoliageJob* job = &gFoliage.jobs[i];
        tChunk* chunk = tFindChunkByMin(job->chunkMin);
        resolvedChunks[i] = chunk;
        if (!chunk) continue;
        // remove invalidated foliages only
        for (s32 j = 0; j < chunk->foliageCount; j++)
        {
            FoliageEntity foliage = chunk->foliageEntities[j];
            FoliageType* type = &gFoliage.types[foliage.bundleId];
            bool removed = type->params.enabled == false;
            if (removed || BitsetGet(&gFoliage.changedGroups, type->params.groupIndex))
            {
                chunk->foliageEntities[j--] = chunk->foliageEntities[--chunk->foliageCount]; // unordered remove
                RenderSet_RemoveEntityRangeStaged(set, foliage.range);
                totalEntityRemoved += foliage.range.count;
            }
        }

        for (u32 p = 0; p < job->count; p++)
        {
            FoliageType* type = &gFoliage.types[job->placements[p].typeIndex];
            totalEntityAdded += CountNumEntities(set, type->bundleIdx, 1, primitiveCounts, false);
        }
    }

    if (totalEntityRemoved)
    {
        RenderSet_CompactEntities(set);
        gFoliage.scene.renderDataDirty = true;
    }

    if (!BitsetHasAtLeastEmptyBits(set->sparseSlots, set->maxEntities, totalEntityAdded)) {
        AX_WARN("popcount: %d", BitsetPopCount(set->sparseSlots, set->maxEntities));
        AX_WARN("terrain sparse id alloc failed!");
        ArenaPopGlobal(set->numGroups * sizeof(u32)); /* primitiveCounts */ 
        ArenaPopGlobal(scheduledCount * sizeof(tChunk*)); /* resolvedChunks */ 
        return;
    }
  
    BatchLeaveSpacePrimitives(set, 0, set->numGroups, primitiveCounts, totalEntityAdded);
    
    for (u32 i = 0; i < scheduledCount; i++)
    {
        tChunk* chunk = resolvedChunks[i];
        if (!chunk) continue;
        tFoliageJob* job = &gFoliage.jobs[i];

        for (u32 p = 0; p < job->count; p++)
        {
            const tFoliagePlacement* placement = &job->placements[p];
            FoliageType* type = &gFoliage.types[placement->typeIndex];

            float3 worldPos = F3Add(ToFloat3(job->chunkMin), Vec3Get(Unpack16x4Fixed(placement->localPos, (f32)T_CHUNK_CELLS)));
            f32 finalScale = type->params.size * placement->scale;
            f32 rise = type->localBaseY * finalScale;
            worldPos.y -= rise;

            Entity e = {0};
            e.position = VecSetR(worldPos.x, worldPos.y, worldPos.z, 0.0f);
            e.rotation = placement->rotation;
            e.scale    = EntityPackUniformWorldScale(finalScale);
            // RenderSet_AddSceneArray(set, type->bundleIdx, &e, 1, false);
            u32 sparseCursor = RenderSet_AllocateSparseIDRange(set, type->numEntities);
            if (AX_UNLIKELY(sparseCursor == INVALID_ENTITY)) break;
            u32 numEntityAdded = AddBundleAsScene(set, type->bundleIdx, sparseCursor, &e, false);
            chunk->foliageEntities[chunk->foliageCount++] = (FoliageEntity){
                (Range){ sparseCursor, numEntityAdded},
                type->bundleIdx
            };

            SetFolliageCollider(type, numEntityAdded, set, sparseCursor, rise);
        }
    }
    ArenaPopGlobal(set->numGroups * sizeof(u32)); // primitiveCounts
    ArenaPopGlobal(scheduledCount * sizeof(tChunk*)); /* resolvedChunks */ 
    RenderSet_Validate(set, "omassey");
    gFoliage.scene.renderDataDirty = true;
}

static void NoiseUI()
{
    static bool open = true;
    static SDL_GPUTexture* noiseTex = NULL;
    static float noiseScale = 0.002f;

    UIImageData noisePreviewImage = (UIImageData){ .texture = noiseTex };

    Clay_ElementId windowID = CLAY_ID("Noise");
    static bool updated = false;
    
    if (!noiseTex || updated)
    {
        updated = false;
        #define TexSize 512
        if (!noiseTex)
            noiseTex = CreateTexture2D(TexSize, TexSize, SDL_GPU_TEXTUREFORMAT_R8_UNORM,
                                       SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
                                       SDL_GPU_SAMPLECOUNT_1, 1u, "TextureNoisePreview");
        Texture texture;
        texture.handle = noiseTex;
        texture.width  = texture.height = TexSize;
        texture.format = SDL_GPU_TEXTUREFORMAT_R8_UNORM;
        static f32 noise[TexSize * TexSize];
        static f32 rarity[TexSize * TexSize];
        
        GetExistance(0, 0, TexSize, 0.005f, Seed32(), noise);
        GetRarityGate(0, 0, TexSize, 0.6f, rarity, 325u);
        
        u8 result[TexSize * TexSize];
        for (s32 i = 0; i < TexSize * TexSize; i++)
        {
            f32 r = rarity[i] * 0.2f + 0.5f;
            result[i] = (u8)(SmoothStep(r-0.05f, r+0.05, noise[i]) * 128.0f);
        }
        UploadTextureRegion(texture, 0, 0, 0, TexSize, TexSize, TexSize, TexSize, result);
        #undef TexSize
    }

    if (UIBeginWindowId(windowID, "Noise",
                        (float2) { 800.0f, 40.0f }, (float2) { 500.0f, 260.0f }, 
                        &open, UIWindowFlags_NoClose))
    {
        UIWindow* window = UIGetWindow(windowID);
        f32 side = window ? window->scale.x - 48.0f : 256.0f;

        if (noisePreviewImage.texture)
        {
            CLAY(CLAY_ID("TexturePagePreview"), {
                .layout = { .sizing = { CLAY_SIZING_FIXED(side), CLAY_SIZING_FIXED(side) } },
                .image  = { .imageData = &noisePreviewImage },
                .border = { .color = UIGetClayColor(UIColor_Border), .width = CLAY_BORDER_ALL(1) }
            }) {}
        }
        
        updated = UIEditFloat("noiseScale", &noiseScale, 0.005f, 1.0f, 0.001f, 4);
        
        bool noiseChanged = UIEditFloat("densityNoise", &densityNoiseScale, 0.000f, 4.0f, 0.001f, 4);
        noiseChanged |= UIEditFloat("densityVary", &densityVary, 0.000f, 1.0f, 0.001f, 4);
        noiseChanged |= UIEditFloat("rarityNoise", &rarityNoiseScale, 0.000f, 1.0f, 0.001f, 4);
        noiseChanged |= UIEditFloat("rarityVary", &rarityVary, 0.000f, 1.0f, 0.001f, 4);

        if (noiseChanged)
        {
            gFoliage.changedGroups = ~0ull;
            Scene_ClearEntities(&gFoliage.scene);
            tChunk* allChunks = tGetAllChunks();
            for (s32 c = 0; c < T_MAX_CHUNKS; c++)
                allChunks[c].foliageCount = 0;
        }

        UIEndWindow();
    }
}

void Foliage_Update(void)
{
    JobSystem* js = tGetTerrainJobSystem();
    if (!js) return;
    // Always walk residents (ignoring anyDirty) so streamed-in chunks get an initial foliage pass.
    // Uses the occupied-chunks bitset instead of trusting chunkCount density.
    const u64* occupied = tGetOccupiedChunksBitset();
    u32 scheduledCount = 0u;
    bool anyDirty = gFoliage.changedGroups > 0ull;
    IterateSetBits(occupied, T_CHUNK_BITSET_WORDS * 64,
    {
        tChunk* chunk = tGetChunkByIndex(bitId);
        if (!chunk || chunk->buildState != CHUNK_READY || !chunk->mesh.vertices.heapPtr) continue;
        if (!chunk->density) continue; // no cached density grid yet, nothing to sample
    
        bool needsInitialBuild = !chunk->foliageBuilt;
        if (!anyDirty && !needsInitialBuild) continue; // nothing to do for this chunk right now
    
        ScheduleFoliageJob(js, &gFoliage.jobs[scheduledCount], chunk, needsInitialBuild);
        chunk->foliageBuilt = true; // decided (even a zero-placement result counts)
        scheduledCount++;
    });

    if (scheduledCount > 0u)
    {
        JobSystem_Wait(js);
        IntegrateFinishedFoliage(scheduledCount);
    }

    gFoliage.changedGroups = 0ull;
    NoiseUI();
}

