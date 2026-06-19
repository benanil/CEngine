// terrain authoring window. backend terrain editing is intentionally not owned here;
// this file keeps the editor controls and serializes the high-level .terrain settings.
#include "EditorInternal.h"
#include "Include/Algorithm.h"
#include "Include/FileSystem.h"
#include "Include/Random.h"
#include "Include/Scene.h"
#include "Include/SceneSerializer.h"
#include "Include/Terrain.h"
#include "Include/Platform.h"
#include "Math/Bitpack.h"
#include "Math/Quaternion.h"

extern WindowState g_WindowState;
#define EDITOR_TERRAIN_FOLIAGE_SCENE "Assets/Scenes/TerrainFolliage.scene"
#define EDITOR_TERRAIN_MAX_LAYERS 8u
#define EDITOR_TERRAIN_MAX_FOLIAGE 8u
#define EDITOR_TERRAIN_MAX_FOLIAGE_ASSETS 64u

typedef enum TerrainEditorMode_
{
    TerrainEditorMode_Manipulate,
    TerrainEditorMode_Paint,
    TerrainEditorMode_Foliage,
    TerrainEditorMode_Grass
} TerrainEditorMode;

typedef struct TerrainLayerUI_
{
    bool enabled;
    char albedo[256];
    char normal[256];
} TerrainLayerUI;

typedef struct TerrainFoliageUI_
{
    bool enabled;
    char name[64];
    char meshPath[256];
    f32 density;
    f32 probability;
    f32 scaleMin;
    f32 scaleMax;
    char colorHex[9];
} TerrainFoliageUI;

typedef struct TerrainEditorState_
{
    bool initialized;
    bool created;
    bool deleteConfirmOpen;
    bool fixedChunkSize;
    bool island;
    bool editMode;
    TerrainEditorMode mode;

    char terrainName[128];
    char savePath[512];
    f32 seed;
    f32 seaLevel;
    f32 baseHeight;
    f32 hillAmplitude;
    f32 hillFrequency;
    f32 ridgeAmplitude;
    f32 ridgeFrequency;
    f32 caveAmplitude;
    f32 caveFrequency;
    f32 islandRadius;
    f32 islandFalloff;
    f32 brushRadius;
    f32 brushStrength;
    f32 brushSoftness;

    TerrainLayerUI layers[EDITOR_TERRAIN_MAX_LAYERS];
    u32 selectedLayer;
    TerrainFoliageUI foliage[EDITOR_TERRAIN_MAX_FOLIAGE];
    u32 selectedFoliage;
    f32 grassDensity;
    f32 grassScaleMin;
    f32 grassScaleMax;
    char grassColorHex[9];
    bool lastSaveOk;
    bool foliageReady;
    bool lastFoliageOk;
    bool foliageUseFullScene;
} TerrainEditorState;

static TerrainEditorState terrainUI;
static UITextAreaCustomData terrainTextData[32];
static u32 terrainTextDataCount;
static Scene terrainFoliageScene;
static bool terrainFoliageSceneInit;

typedef struct TerrainFoliageAssetList_
{
    char paths[EDITOR_TERRAIN_MAX_FOLIAGE_ASSETS][512];
    u32 count;
} TerrainFoliageAssetList;

static void TerrainSetString(char* dst, u32 dstSize, const char* src)
{
    u32 len = Minu32((u32)StringLength(src), dstSize - 1u);
    MemCopy(dst, src, len);
    dst[len] = '\0';
}

static void TerrainNormalizePath(const char* src, char* dst, u32 dstSize)
{
    u32 i = 0u;
    for (; src[i] && i + 1u < dstSize; i++)
        dst[i] = src[i] == '\\' ? '/' : src[i];
    dst[i] = '\0';
}

static bool TerrainScenePath(char* dst, u32 dstSize)
{
    const char* scenePath = Scene_GetActivePath();
    if (!scenePath || !scenePath[0]) return false;

    TerrainNormalizePath(scenePath, dst, dstSize);
    u32 len = (u32)StringLength(dst);
    u32 stem = len;
    while (stem > 0u && dst[stem - 1u] != '.' && dst[stem - 1u] != '/' && dst[stem - 1u] != '\\') stem--;
    if (stem > 0u && dst[stem - 1u] == '.') len = stem - 1u;

    static const char ext[] = ".terrain";
    u32 extLen = (u32)sizeof(ext);
    if (len + extLen > dstSize) return false;
    MemCopy(dst + len, ext, extLen);
    return true;
}

static void TerrainNameFromPath(const char* path, char* dst, u32 dstSize)
{
    u32 begin = 0u;
    u32 end = (u32)StringLength(path);
    for (u32 i = 0u; path[i]; i++)
        if (path[i] == '/' || path[i] == '\\') begin = i + 1u;
    for (u32 i = begin; path[i]; i++)
        if (path[i] == '.') { end = i; break; }

    u32 len = Minu32(end - begin, dstSize - 1u);
    MemCopy(dst, path + begin, len);
    dst[len] = '\0';
}

static bool TerrainSyncScenePath(void)
{
    if (!TerrainScenePath(terrainUI.savePath, sizeof(terrainUI.savePath)))
    {
        terrainUI.savePath[0] = '\0';
        return false;
    }
    TerrainNameFromPath(terrainUI.savePath, terrainUI.terrainName, sizeof(terrainUI.terrainName));
    return true;
}

static bool TerrainChunksPath(char* dst, u32 dstSize)
{
    if (!TerrainSyncScenePath()) return false;
    u32 len = Minu32((u32)StringLength(terrainUI.savePath), dstSize - 1u);
    MemCopy(dst, terrainUI.savePath, len);
    dst[len] = '\0';
    ChangeExtension(dst, 512, "chunks");
    return true;
}

static void TerrainCollectFoliageAsset(const char* path, void* data)
{
    TerrainFoliageAssetList* list = (TerrainFoliageAssetList*)data;
    if (list->count >= EDITOR_TERRAIN_MAX_FOLIAGE_ASSETS) return;
    int len = StringLength(path);
    if (!FileHasExtension(path, len, ".gltf") && !FileHasExtension(path, len, ".glb")) return;
    TerrainNormalizePath(path, list->paths[list->count], sizeof(list->paths[list->count]));
    list->count++;
}

static TerrainFoliageAssetList TerrainScanFoliageAssets(void)
{
    TerrainFoliageAssetList list = { 0 };
    VisitFolder("Assets/Folliage", TerrainCollectFoliageAsset, &list, true);
    return list;
}

static void TerrainEditorInit(void)
{
    if (terrainUI.initialized) return;
    terrainUI.initialized = true;
    terrainUI.created = Terrain_GetEnabled();
    terrainUI.fixedChunkSize = false;
    terrainUI.island = false;
    terrainUI.editMode = false;
    terrainUI.mode = TerrainEditorMode_Manipulate;
    TerrainSetString(terrainUI.terrainName, sizeof(terrainUI.terrainName), "NewTerrain");
    TerrainSyncScenePath();
    terrainUI.seed           = 1.0f;
    terrainUI.seaLevel       = 0.0f;
    terrainUI.baseHeight     = -8.0f;
    terrainUI.hillAmplitude  = 1.0f;
    terrainUI.hillFrequency  = 1.0f;
    terrainUI.ridgeAmplitude = 0.5f;
    terrainUI.ridgeFrequency = 0.45f;
    terrainUI.caveAmplitude  = 1.0f;
    terrainUI.caveFrequency  = 0.045f;
    terrainUI.islandRadius   = 250.0f;
    terrainUI.islandFalloff  = 120.0f;
    terrainUI.brushRadius    = 10.0f;
    terrainUI.brushStrength  = 1.0f;
    terrainUI.brushSoftness  = 0.5f;
    // the three layers the engine loads into the terrain texture arrays today,
    // see TerrainLoadLayerArray in Terrain.c. extra slots stay disabled until
    // custom layer loading lands
    static const char* defaultAlbedo[3] = {
        "Assets/Textures/Terrain/brown_mud_leaves_01_diff_2k.png",
        "Assets/Textures/Terrain/rocky_terrain_02_diff_2k.png",
        "Assets/Textures/Terrain/rocky_terrain_diff_2k.png"
    };
    static const char* defaultNormal[3] = {
        "Assets/Textures/Terrain/brown_mud_leaves_01_nor_dx_1k.png",
        "Assets/Textures/Terrain/rocky_terrain_02_nor_dx_1k.png",
        "Assets/Textures/Terrain/rocky_terrain_nor_dx_1k.png"
    };
    for (u32 i = 0; i < 3u; i++)
    {
        terrainUI.layers[i].enabled = true;
        TerrainSetString(terrainUI.layers[i].albedo, sizeof(terrainUI.layers[i].albedo), defaultAlbedo[i]);
        TerrainSetString(terrainUI.layers[i].normal, sizeof(terrainUI.layers[i].normal), defaultNormal[i]);
    }
    TerrainFoliageAssetList foliageAssets = TerrainScanFoliageAssets();
    for (u32 i = 0; i < EDITOR_TERRAIN_MAX_FOLIAGE && i < foliageAssets.count; i++)
    {
        terrainUI.foliage[i].enabled = true;
        TerrainSetString(terrainUI.foliage[i].name, sizeof(terrainUI.foliage[i].name), GetFileName(foliageAssets.paths[i]));
        TerrainSetString(terrainUI.foliage[i].meshPath, sizeof(terrainUI.foliage[i].meshPath), foliageAssets.paths[i]);
        TerrainSetString(terrainUI.foliage[i].colorHex, sizeof(terrainUI.foliage[i].colorHex), "FFFFFFFF");
        terrainUI.foliage[i].density = (i < 2u) ? 0.20f : (i < 4u ? 0.12f : 0.16f);
        terrainUI.foliage[i].probability = 1.0f;
        terrainUI.foliage[i].scaleMin = (i < 2u) ? 0.8f : 0.6f;
        terrainUI.foliage[i].scaleMax = (i < 2u) ? 1.6f : 1.2f;
    }
    terrainUI.grassDensity = 1.0f;
    terrainUI.grassScaleMin = 0.6f;
    terrainUI.grassScaleMax = 1.2f;
    TerrainSetString(terrainUI.grassColorHex, sizeof(terrainUI.grassColorHex), "77AA55FF");
}

static f32 TerrainRandomRange(u32* rng, f32 mn, f32 mx)
{
    return mn + NextFloat01(PCG2Next(rng)) * (mx - mn);
}

static u32 TerrainFoliageRandomUInt(u32* seed)
{
    *seed ^= *seed << 13;
    *seed ^= *seed >> 17;
    *seed ^= *seed << 5;
    return *seed;
}

static f32 TerrainFoliageRandomFloat(u32* seed)
{
    return (f32)(TerrainFoliageRandomUInt(seed) >> 8) / (f32)(1 << 24);
}

static f32 TerrainFoliageFrac(f32 x)
{
    return x - Floorf32(x);
}

static f32 TerrainFoliagePerlin2(f32 x, f32 y)
{
    f32 fx = TerrainFoliageFrac(x);
    f32 fy = TerrainFoliageFrac(y);
    f32 px = Floorf32(x);
    f32 py = Floorf32(y);
    f32 v = px + py * 1000.0f;
    f32 r0 = TerrainFoliageFrac(10000.0f * Sin((0.0f + v) * 0.001f));
    f32 r1 = TerrainFoliageFrac(10000.0f * Sin((1.0f + v) * 0.001f));
    f32 r2 = TerrainFoliageFrac(10000.0f * Sin((1000.0f + v) * 0.001f));
    f32 r3 = TerrainFoliageFrac(10000.0f * Sin((1001.0f + v) * 0.001f));
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);
    f32 a = r0 + (r1 - r0) * fx;
    f32 b = r2 + (r3 - r2) * fx;
    return 2.0f * (a + (b - a) * fy) - 1.0f;
}

static bool TerrainFoliageSceneReset(void)
{
    if (terrainFoliageSceneInit)
        Scene_Destroy(&terrainFoliageScene);
    Scene_Init(&terrainFoliageScene);
    terrainFoliageSceneInit = true;
    return Scene_Activate(&terrainFoliageScene) != 0;
}

static f32 TerrainFoliagePrimitiveNormalizeScale(const PrimitiveGroup* group)
{
    f32 maxDim = Max3(VecSub(group->aabbMax, group->aabbMin));
    if (maxDim <= 0.001f) return 1.0f;
    // Imported foliage packs are often authored in centimeters or arbitrary DCC units.
    // Normalize the largest primitive axis to about 2 meters; UI scale remains a multiplier.
    return Clampf32(2.0f / maxDim, 0.005f, 10.0f);
}

static bool TerrainFoliagePickPosition(u32* rng, f32 spawnRadius, u32 seed, f32 probability, float3* outPos)
{
    f32 rarity = Maxf32(0.15f, 1.0f - Clampf32(probability, 0.0f, 1.0f) * 0.75f);
    f32 localThreshold = 0.4f * rarity;
    f32 globalThreshold = 0.5f * rarity;
    f32 localSize = 0.12f * rarity;
    f32 globalSize = 0.05f * rarity;
    f32 offsetX = Sin((f32)seed * 16.3f) * 1024.0f;
    f32 offsetZ = Cos(-(f32)seed * 32.7f) * 1024.0f;
    for (u32 tries = 0u; tries < 40u; tries++)
    {
        f32 angle = TerrainRandomRange(rng, 0.0f, MATH_TwoPI);
        f32 dist = Sqrtf(NextFloat01(PCG2Next(rng))) * spawnRadius;
        f32 jitterX = (TerrainFoliageRandomFloat(rng) * 2.0f - 1.0f) * 1.333333f;
        f32 jitterZ = (TerrainFoliageRandomFloat(rng) * 2.0f - 1.0f) * 1.333333f;
        f32 x = Cos(angle) * dist + jitterX;
        f32 z = Sin(angle) * dist + jitterZ;
        f32 global = Clampf32(TerrainFoliagePerlin2(x * globalSize + offsetX, z * globalSize + offsetZ), 0.0f, 1.0f);
        f32 local = Clampf32(TerrainFoliagePerlin2(z * localSize - offsetX * 1.3f, x * localSize - offsetZ * 1.3f), 0.0f, 1.0f);
        if (global >= globalThreshold || local >= localThreshold) continue;
        BVHHit hit;
        float3 origin = (float3){ x, terrainUI.seaLevel + 320.0f, z };
        float3 dir = (float3){ 0.0f, -1.0f, 0.0f };
        if (!Terrain_RaycastField(origin, dir, 700.0f, &hit))
            continue;
        float3 pos = BVH_HitPositionF(origin, dir, &hit);
        if (terrainUI.island && pos.y <= terrainUI.seaLevel + 0.15f)
            continue;
        *outPos = pos;
        return true;
    }
    return false;
}

static bool TerrainGenerateFoliageScene(void)
{
    if (!Terrain_GetEnabled()) return false;
    if (!TerrainFoliageSceneReset()) return false;

    TerrainFoliageAssetList foliageAssets = TerrainScanFoliageAssets();
    if (foliageAssets.count == 0u)
    {
        Scene_Deactivate(&terrainFoliageScene);
        return false;
    }

    u32 bundleIdx[EDITOR_TERRAIN_MAX_FOLIAGE_ASSETS];
    bool loadedAny = false;
    for (u32 i = 0u; i < foliageAssets.count; i++)
    {
        bundleIdx[i] = Scene_AddBundleAuto(&terrainFoliageScene, foliageAssets.paths[i]);
        loadedAny |= bundleIdx[i] != INVALID_BUNDLE;
    }
    if (!loadedAny)
    {
        Scene_Deactivate(&terrainFoliageScene);
        return false;
    }

    f32 spawnRadius = terrainUI.island ? terrainUI.islandRadius + terrainUI.islandFalloff * 0.35f : 500.0f;
    spawnRadius = Maxf32(spawnRadius, 32.0f);
    u32 rng = ((u32)terrainUI.seed ^ 0xA53C39u) | 1u;
    u32 spawnedTotal = 0u;

    for (u32 i = 0u; i < foliageAssets.count; i++)
    {
        if (bundleIdx[i] == INVALID_BUNDLE) continue;
        SceneBundleRef* ref = &terrainFoliageScene.bundleRefs[bundleIdx[i]];
        if (ref->skinned) continue;
        RenderSet* set = &terrainFoliageScene.surfaceSet;
        Range range = set->bundleRange[ref->renderIdx];
        f32 density = i < EDITOR_TERRAIN_MAX_FOLIAGE ? terrainUI.foliage[i].density : 0.14f;
        f32 probability = i < EDITOR_TERRAIN_MAX_FOLIAGE ? terrainUI.foliage[i].probability : 1.0f;
        f32 scaleMin = i < EDITOR_TERRAIN_MAX_FOLIAGE ? terrainUI.foliage[i].scaleMin : 0.7f;
        f32 scaleMax = i < EDITOR_TERRAIN_MAX_FOLIAGE ? terrainUI.foliage[i].scaleMax : 1.3f;
        bool enabled = i >= EDITOR_TERRAIN_MAX_FOLIAGE || terrainUI.foliage[i].enabled;
        if (!enabled) continue;

        if (terrainUI.foliageUseFullScene)
        {
            u32 targetCount = (u32)Clampf32(density * 120.0f, 0.0f, 500.0f);
            for (u32 n = 0u; n < targetCount; n++)
            {
                float3 pos;
                u32 noiseSeed = ((u32)terrainUI.seed ^ (i * 73856093u));
                if (!TerrainFoliagePickPosition(&rng, spawnRadius, noiseSeed, probability, &pos)) continue;
                f32 yaw = TerrainRandomRange(&rng, -MATH_PI, MATH_PI);
                f32 scale = TerrainRandomRange(&rng, Minf32(scaleMin, scaleMax), Maxf32(scaleMin, scaleMax));
                spawnedTotal += Scene_Spawn(&terrainFoliageScene, bundleIdx[i], VecSetR(pos.x, pos.y, pos.z, 1.0f), QFromEuler(0.0f, yaw, 0.0f), VecSet1(scale));
            }
        }
        else
        {
            for (u32 g = range.start; g < range.start + range.count; g++)
            {
                PrimitiveGroup* group = &set->primitiveGroups[g];
                if (!group->valid) continue;
                f32 normalizeScale = TerrainFoliagePrimitiveNormalizeScale(group);
                u32 targetCount = (u32)Clampf32(density * 120.0f, 0.0f, 500.0f);
                for (u32 n = 0u; n < targetCount; n++)
                {
                    float3 pos;
                    u32 noiseSeed = ((u32)terrainUI.seed ^ (i * 73856093u) ^ (g * 19349663u));
                    if (!TerrainFoliagePickPosition(&rng, spawnRadius, noiseSeed, probability, &pos)) continue;
                    f32 yaw = TerrainRandomRange(&rng, -MATH_PI, MATH_PI);
                    f32 scale = TerrainRandomRange(&rng, Minf32(scaleMin, scaleMax), Maxf32(scaleMin, scaleMax)) * normalizeScale;
                    Entity entity;
                    entity.position = VecSetR(pos.x, pos.y, pos.z, 1.0f);
                    PackQuaternionS16Norm(VecNorm(QFromEuler(0.0f, yaw, 0.0f)), &entity.rotation);
                    entity.scale = RenderSet_PackEntityUniformWorldScale(scale);
                    entity.sparseIdx = INVALID_ENTITY;
                    spawnedTotal += RenderSet_AddEntity(set, g, &entity) != INVALID_ENTITY;
                }
            }
        }
    }
    if (spawnedTotal == 0u)
    {
        Scene_Deactivate(&terrainFoliageScene);
        return false;
    }

    EnsurePath(EDITOR_TERRAIN_FOLIAGE_SCENE);
    terrainFoliageScene.renderDataDirty = 1;
    terrainUI.foliageReady = true;
    return SceneSerializer_Save(&terrainFoliageScene, EDITOR_TERRAIN_FOLIAGE_SCENE) != 0;
}

static TerrainGenParams TerrainEditorBuildParams(void)
{
    TerrainGenParams params = Terrain_DefaultGenParams();
    params.seed           = (u32)terrainUI.seed;
    params.seaLevel       = terrainUI.seaLevel;
    params.baseHeight     = terrainUI.baseHeight;
    params.hillAmplitude  = Clampf32(terrainUI.hillAmplitude, 0.1f, 4.0f);
    params.hillFrequency  = Clampf32(terrainUI.hillFrequency, 0.05f, 4.0f);
    params.ridgeAmplitude = Clampf32(terrainUI.ridgeAmplitude, 0.25f, 2.0f);
    params.ridgeFrequency = Clampf32(terrainUI.ridgeFrequency, 0.05f, 2.0f);
    params.carveAmplitude = Clampf32(terrainUI.caveAmplitude, 0.0f, 32.0f);
    params.carveFrequency = Clampf32(terrainUI.caveFrequency, 0.0001f, 0.2f);
    params.island         = terrainUI.island;
    params.islandRadius   = terrainUI.islandRadius;
    params.islandFalloff  = terrainUI.islandFalloff;
    params.fixedArea      = terrainUI.fixedChunkSize;
    return params;
}

static void TerrainEditorApplyParams(const TerrainGenParams* params)
{
    terrainUI.seed           = (f32)params->seed;
    terrainUI.seaLevel       = params->seaLevel;
    terrainUI.baseHeight     = params->baseHeight;
    terrainUI.hillAmplitude  = params->hillAmplitude;
    terrainUI.hillFrequency  = params->hillFrequency;
    terrainUI.ridgeAmplitude = params->ridgeAmplitude;
    terrainUI.ridgeFrequency = params->ridgeFrequency;
    terrainUI.caveAmplitude  = params->carveAmplitude;
    terrainUI.caveFrequency  = params->carveFrequency;
    terrainUI.island         = params->island;
    terrainUI.islandRadius   = params->islandRadius;
    terrainUI.islandFalloff  = params->islandFalloff;
    terrainUI.fixedChunkSize = params->fixedArea;
}

// per frame brush interaction, runs from the main loop before gizmo/picking and
// consumes the mouse while terrain edit mode is active over the scene
bool TerrainEditorUpdate(Camera* camera)
{
    bool wantsBrush = terrainUI.initialized && terrainUI.editMode && terrainUI.created &&
                      Terrain_GetEnabled() && EditorSceneInteractAllowed();
    if (!wantsBrush)
    {
        Terrain_SetBrushCursor(F3Zero(), 0.0f, false);
        return false;
    }

    RayV ray = ScreenPointToRay(camera, EditorSceneMouse());
    float3 origin = Vec3Get(ray.origin);
    float3 dir = F3Norm(Vec3Get(ray.dir));
    BVHHit hit;
    // the mesh raycast only covers the near lod rings (cpu copies), distant terrain
    // falls back to tracing the analytic density field so everywhere stays editable
    if (!Terrain_Raycast(origin, dir, 600.0f, 1u, &hit) &&
        !Terrain_RaycastField(origin, dir, 600.0f, &hit))
    {
        Terrain_SetBrushCursor(F3Zero(), 0.0f, false);
        return false;
    }

    float3 hitPos = Vec3Get(BVH_HitPositionV(ray.origin, ray.dir, &hit));
    Terrain_SetBrushCursor(hitPos, terrainUI.brushRadius, true);

    if (GetMouseDown(MouseButton_Left))
    {
        switch (terrainUI.mode)
        {
        case TerrainEditorMode_Manipulate:
        {
            // strength is meters per second, shift inverts to dig
            f32 strength = terrainUI.brushStrength * 18.0f * PlatformCtx.DeltaTime;
            if (GetKeyDown(SDLK_LSHIFT)) strength = -strength;
            Terrain_SculptSphere(hitPos, terrainUI.brushRadius, strength, terrainUI.brushSoftness);
            break;
        }
        case TerrainEditorMode_Paint:
            // strength slider scales how fast the blend weight saturates. only
            // enabled layers paint, disabled list entries do nothing
            if (terrainUI.layers[terrainUI.selectedLayer].enabled)
                Terrain_PaintSphere(hitPos, terrainUI.brushRadius, terrainUI.selectedLayer,
                                    Absf32(terrainUI.brushStrength) * 520.0f * PlatformCtx.DeltaTime,
                                    terrainUI.brushSoftness);
            break;
        default: // foliage and grass placement land with their systems
            break;
        }
        return true;
    }
    return false;
}

static char* TerrainWriteString(char* p, const char* s)
{
    u32 len = (u32)StringLength(s);
    MemCopy(p, s, len);
    return p + len;
}

static char* TerrainWriteLine(char* p, const char* key, const char* value)
{
    p = TerrainWriteString(p, key);
    *p++ = ' ';
    p = TerrainWriteString(p, value);
    *p++ = '\n';
    return p;
}

static char* TerrainWriteF32(char* p, const char* key, f32 value, int decimals)
{
    p = TerrainWriteString(p, key);
    *p++ = ' ';
    p += FloatToString(p, value, decimals);
    *p++ = '\n';
    return p;
}

static char* TerrainWriteBool(char* p, const char* key, bool value)
{
    p = TerrainWriteString(p, key);
    *p++ = ' ';
    *p++ = value ? '1' : '0';
    *p++ = '\n';
    return p;
}

static bool TerrainEditorSave(void)
{
    if (!TerrainSyncScenePath())
    {
        AX_WARN("terrain save skipped: active scene has no saved .scene path");
        return false;
    }
    EnsurePath(terrainUI.savePath);

    u32 capacity = 16384u;
    char* text = (char*)SDL_malloc(capacity);
    if (!text) return false;
    char* p = text;
    p = TerrainWriteString(p, "terrain 1\n");
    p = TerrainWriteLine(p, "name", terrainUI.terrainName);
    p = TerrainWriteBool(p, "fixed_chunk_size", terrainUI.fixedChunkSize);
    p = TerrainWriteBool(p, "island", terrainUI.island);
    p = TerrainWriteF32(p, "seed", terrainUI.seed, 0);
    p = TerrainWriteF32(p, "sea_level", terrainUI.seaLevel, 3);
    p = TerrainWriteF32(p, "base_height", terrainUI.baseHeight, 3);
    p = TerrainWriteF32(p, "hill_amplitude", terrainUI.hillAmplitude, 3);
    p = TerrainWriteF32(p, "hill_frequency", terrainUI.hillFrequency, 6);
    p = TerrainWriteF32(p, "ridge_amplitude", terrainUI.ridgeAmplitude, 3);
    p = TerrainWriteF32(p, "ridge_frequency", terrainUI.ridgeFrequency, 6);
    p = TerrainWriteF32(p, "cave_amplitude", terrainUI.caveAmplitude, 3);
    p = TerrainWriteF32(p, "cave_frequency", terrainUI.caveFrequency, 6);
    p = TerrainWriteF32(p, "island_radius", terrainUI.islandRadius, 3);
    p = TerrainWriteF32(p, "island_falloff", terrainUI.islandFalloff, 3);

    for (u32 i = 0u; i < EDITOR_TERRAIN_MAX_LAYERS; i++)
    {
        TerrainLayerUI* layer = &terrainUI.layers[i];
        if (!layer->enabled || !layer->albedo[0]) continue;
        p = TerrainWriteString(p, "layer ");
        p += IntToString(p, (s64)i, 0);
        *p++ = ' ';
        p = TerrainWriteString(p, layer->albedo);
        *p++ = ' ';
        p = TerrainWriteString(p, layer->normal[0] ? layer->normal : "none");
        *p++ = '\n';
    }

    p = TerrainWriteString(p, "grass ");
    p += FloatToString(p, terrainUI.grassDensity, 3); *p++ = ' ';
    p += FloatToString(p, terrainUI.grassScaleMin, 3); *p++ = ' ';
    p += FloatToString(p, terrainUI.grassScaleMax, 3); *p++ = ' ';
    p = TerrainWriteString(p, terrainUI.grassColorHex);
    *p++ = '\n';

    for (u32 i = 0u; i < EDITOR_TERRAIN_MAX_FOLIAGE; i++)
    {
        TerrainFoliageUI* f = &terrainUI.foliage[i];
        if (!f->enabled || !f->meshPath[0]) continue;
        p = TerrainWriteString(p, "foliage ");
        p += IntToString(p, (s64)(i + 1u), 0);
        *p++ = ' ';
        p = TerrainWriteString(p, f->name[0] ? f->name : "unnamed"); *p++ = ' ';
        p = TerrainWriteString(p, f->meshPath); *p++ = ' ';
        p += FloatToString(p, f->density, 3); *p++ = ' ';
        p += FloatToString(p, f->probability, 3); *p++ = ' ';
        p += FloatToString(p, f->scaleMin, 3); *p++ = ' ';
        p += FloatToString(p, f->scaleMax, 3); *p++ = ' ';
        p = TerrainWriteString(p, f->colorHex[0] ? f->colorHex : "FFFFFFFF");
        *p++ = '\n';
    }

    WriteAllBytes(terrainUI.savePath, text, (unsigned long)(p - text));
    SDL_free(text);

    char chunksPath[512];
    if (!TerrainChunksPath(chunksPath, sizeof(chunksPath))) return false;
    EnsurePath(chunksPath);
    return FileExist(terrainUI.savePath) && Terrain_SaveEditChunks(chunksPath);
}

static bool TerrainKeyIs(const char* line, const char* key, const char** value)
{
    u32 len = (u32)StringLength(key);
    for (u32 i = 0; i < len; i++)
        if (line[i] != key[i]) return false;
    if (line[len] != ' ') return false;
    *value = line + len + 1;
    return true;
}

static bool TerrainEditorLoad(void)
{
    if (!TerrainSyncScenePath())
    {
        AX_WARN("terrain load skipped: active scene has no saved .scene path");
        return false;
    }
    char* text = ReadAllFileAlloc(terrainUI.savePath);
    if (!text) return false;

    char chunksPath[512];
    bool chunksPathOk = TerrainChunksPath(chunksPath, sizeof(chunksPath));

    const char* value;
    char* line = text;
    while (line && *line)
    {
        char* next = line;
        while (*next && *next != '\n') next++;
        bool hadNewline = *next == '\n';
        *next = '\0';

        if      (TerrainKeyIs(line, "name", &value)) TerrainSetString(terrainUI.terrainName, sizeof(terrainUI.terrainName), value);
        else if (TerrainKeyIs(line, "fixed_chunk_size", &value)) terrainUI.fixedChunkSize = value[0] == '1';
        else if (TerrainKeyIs(line, "island", &value)) terrainUI.island = value[0] == '1';
        else if (TerrainKeyIs(line, "seed", &value)) ParseFloat(value, &terrainUI.seed);
        else if (TerrainKeyIs(line, "sea_level", &value)) ParseFloat(value, &terrainUI.seaLevel);
        else if (TerrainKeyIs(line, "base_height", &value)) ParseFloat(value, &terrainUI.baseHeight);
        else if (TerrainKeyIs(line, "hill_amplitude", &value)) ParseFloat(value, &terrainUI.hillAmplitude);
        else if (TerrainKeyIs(line, "hill_frequency", &value)) ParseFloat(value, &terrainUI.hillFrequency);
        else if (TerrainKeyIs(line, "ridge_amplitude", &value)) ParseFloat(value, &terrainUI.ridgeAmplitude);
        else if (TerrainKeyIs(line, "ridge_frequency", &value)) ParseFloat(value, &terrainUI.ridgeFrequency);
        else if (TerrainKeyIs(line, "cave_amplitude", &value)) ParseFloat(value, &terrainUI.caveAmplitude);
        else if (TerrainKeyIs(line, "cave_frequency", &value)) ParseFloat(value, &terrainUI.caveFrequency);
        else if (TerrainKeyIs(line, "island_radius", &value)) ParseFloat(value, &terrainUI.islandRadius);
        else if (TerrainKeyIs(line, "island_falloff", &value)) ParseFloat(value, &terrainUI.islandFalloff);
        else if (TerrainKeyIs(line, "grass", &value))
        {
            value = ParseFloat(value, &terrainUI.grassDensity);
            value = ParseFloat(value, &terrainUI.grassScaleMin);
            value = ParseFloat(value, &terrainUI.grassScaleMax);
            while (*value == ' ') value++;
            TerrainSetString(terrainUI.grassColorHex, sizeof(terrainUI.grassColorHex), value);
        }
        else if (TerrainKeyIs(line, "layer", &value))
        {
            s64 idx64 = 0;
            value = ParseNumberI64(value, &idx64);
            int idx = (int)idx64;
            if (idx >= 0 && idx < (int)EDITOR_TERRAIN_MAX_LAYERS)
            {
                TerrainLayerUI* layer = &terrainUI.layers[idx];
                layer->enabled = true;
                while (*value == ' ') value++;
                u32 n = 0;
                while (value[n] && value[n] != ' ') n++;
                u32 albedoLen = Minu32(n, (u32)sizeof(layer->albedo) - 1u);
                MemCopy(layer->albedo, value, albedoLen); layer->albedo[albedoLen] = '\0';
                value += n;
                while (*value == ' ') value++;
                TerrainSetString(layer->normal, sizeof(layer->normal), value);
            }
        }
        else if (TerrainKeyIs(line, "foliage", &value))
        {
            s64 idx64 = 0;
            value = ParseNumberI64(value, &idx64);
            int idx = (int)idx64;
            idx -= 1; // file indices start at 1, 0 is grass
            if (idx >= 0 && idx < (int)EDITOR_TERRAIN_MAX_FOLIAGE)
            {
                TerrainFoliageUI* f = &terrainUI.foliage[idx];
                f->enabled = true;
                while (*value == ' ') value++;
                u32 n = 0;
                while (value[n] && value[n] != ' ') n++;
                u32 nameLen = Minu32(n, (u32)sizeof(f->name) - 1u);
                MemCopy(f->name, value, nameLen); f->name[nameLen] = '\0';
                value += n;
                while (*value == ' ') value++;
                n = 0;
                while (value[n] && value[n] != ' ') n++;
                u32 meshLen = Minu32(n, (u32)sizeof(f->meshPath) - 1u);
                MemCopy(f->meshPath, value, meshLen); f->meshPath[meshLen] = '\0';
                value += n;
                value = ParseFloat(value, &f->density);
                value = ParseFloat(value, &f->probability);
                value = ParseFloat(value, &f->scaleMin);
                value = ParseFloat(value, &f->scaleMax);
                while (*value == ' ') value++;
                TerrainSetString(f->colorHex, sizeof(f->colorHex), value);
            }
        }
        line = hadNewline ? next + 1 : NULL;
    }
    FreeAllText(text);

    if (!chunksPathOk || !Terrain_LoadEditChunks(chunksPath)) return false;

    // regenerate with the loaded settings, every chunk picks up the loaded edit chunks
    TerrainGenParams params = TerrainEditorBuildParams();
    Terrain_CreateWorld(&params);
    terrainUI.created = true;
    return true;
}

void TerrainEditorSceneChanged(bool loadSidecar)
{
    (void)loadSidecar;
    TerrainEditorInit();
    terrainUI.created = Terrain_GetEnabled();
    terrainUI.editMode = false;
    terrainUI.lastSaveOk = false;
    TerrainSyncScenePath();
    if (terrainUI.created)
        TerrainEditorApplyParams(Terrain_GetGenParams());
}

static void TerrainTextEdit(Clay_ElementId id, char* buffer, u32 capacity, f32 height)
{
    if (terrainTextDataCount >= (u32)(sizeof(terrainTextData) / sizeof(terrainTextData[0]))) return;
    UITextAreaCustomData* textData = &terrainTextData[terrainTextDataCount++];
    textData->type = UICustomType_TextArea;
    textData->buffer = buffer;
    textData->capacity = capacity;
    textData->flags = UITextAreaFlags_CenterY | UITextAreaFlags_NoWrap | UITextAreaFlags_Clip;
    textData->edited = 0u;
    CLAY(id, {
        .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(height) } },
        .custom = { .customData = textData }
    }) {}
}

static void TerrainTextLabel(const char* label)
{
    CLAY_TEXT(UIStr(label), CLAY_TEXT_CONFIG({
        .fontSize = 13,
        .textColor = UIGetClayColor(UIColor_SubText)
    }));
}

static void TerrainLabeledText(Clay_ElementId id, const char* label, char* buffer, u32 capacity)
{
    TerrainTextLabel(label);
    TerrainTextEdit(id, buffer, capacity, 26.0f);
}

static void TerrainToolbar(void)
{
    CLAY(CLAY_ID("TerrainToolbar"), {
        .layout = {
            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(30.0f) },
            .childGap = 8,
            .layoutDirection = CLAY_LEFT_TO_RIGHT
        }
    }) {
        UIPushFloatAdd(UIFloat_TextScale, -0.15f);
        if (UIButton(CLAY_ID("TerrainCreate"), CLAY_STRING("Create"), (Clay_Dimensions){ 78.0f, 26.0f }, false))
        {
            TerrainSyncScenePath();
            // applies the current noise settings, also regenerates an existing world
            TerrainGenParams params = TerrainEditorBuildParams();
            Terrain_CreateWorld(&params);
            terrainUI.created = true;
        }
        if (UIButton(CLAY_ID("TerrainDelete"), CLAY_STRING("Delete"), (Clay_Dimensions){ 78.0f, 26.0f }, false))
            terrainUI.deleteConfirmOpen = true;
        if (UIButton(CLAY_ID("TerrainSave"), CLAY_STRING("Save"), (Clay_Dimensions){ 78.0f, 26.0f }, false))
            terrainUI.lastSaveOk = TerrainEditorSave();
        if (UIButton(CLAY_ID("TerrainLoad"), CLAY_STRING("Load"), (Clay_Dimensions){ 78.0f, 26.0f }, false))
            terrainUI.lastSaveOk = TerrainEditorLoad();
        UIPopFloat(UIFloat_TextScale);
    }
}

static void TerrainNoiseUI(void)
{
    UISectionHeader("Noise Settings");
    bool hasScenePath = TerrainSyncScenePath();
    TerrainTextLabel("Terrain file (from active scene)");
    if (hasScenePath)
    {
        CLAY_TEXT(UIStr(terrainUI.savePath), CLAY_TEXT_CONFIG({
            .fontSize = 13,
            .textColor = UIGetClayColor(UIColor_Text)
        }));
    }
    else
    {
        CLAY_TEXT(CLAY_STRING("Save the scene first to create ScenePathAndName.terrain"), CLAY_TEXT_CONFIG({
            .fontSize = 13,
            .textColor = UIGetClayColor(UIColor_SubText)
        }));
    }
    bool edited = false;
    edited |= UICheckbox(CLAY_ID("TerrainFixedChunks"), CLAY_STRING("Fixed chunk size, do not stream with movement"), &terrainUI.fixedChunkSize);
    edited |= UICheckbox(CLAY_ID("TerrainIsland"), CLAY_STRING("Island mask, center area above sea level"), &terrainUI.island);
    edited |= UIEditFloat(CLAY_ID("TerrainSeed"), CLAY_STRING("Seed"), &terrainUI.seed, 0.0f, 999999.0f, 1.0f, 0);
    edited |= UIEditFloat(CLAY_ID("TerrainSeaLevel"), CLAY_STRING("Sea level"), &terrainUI.seaLevel, -200.0f, 200.0f, 1.0f, 2);
    edited |= UIEditFloat(CLAY_ID("TerrainBaseHeight"), CLAY_STRING("Base height"), &terrainUI.baseHeight, -200.0f, 200.0f, 1.0f, 2);
    edited |= UIEditFloat(CLAY_ID("TerrainHillAmp"), CLAY_STRING("Lowland scale"), &terrainUI.hillAmplitude, 0.25f, 8.0f, 0.1f, 2);
    edited |= UIEditFloat(CLAY_ID("TerrainHillFreq"), CLAY_STRING("Noise scale"), &terrainUI.hillFrequency, 0.05f, 4.0f, 0.05f, 2);
    edited |= UIEditFloat(CLAY_ID("TerrainRidgeAmp"), CLAY_STRING("Height scale"), &terrainUI.ridgeAmplitude, 0.25f, 2.0f, 0.05f, 2);
    edited |= UIEditFloat(CLAY_ID("TerrainRidgeFreq"), CLAY_STRING("Mountain scale"), &terrainUI.ridgeFrequency, 0.05f, 2.0f, 0.05f, 2);
    edited |= UIEditFloat(CLAY_ID("TerrainCaveAmp"), CLAY_STRING("Carve amplitude"), &terrainUI.caveAmplitude, 0.0f, 32.0f, 0.5f, 2);
    edited |= UIEditFloat(CLAY_ID("TerrainCaveFreq"), CLAY_STRING("Carve frequency"), &terrainUI.caveFrequency, 0.0001f, 0.2f, 0.001f, 5);
    if (edited)
    {
        AX_LOG("terrain edited");
        // applies the current noise settings, also regenerates an existing world
        TerrainGenParams params = TerrainEditorBuildParams();
        Terrain_CreateWorld(&params);
        terrainUI.created = true;
    }
    if (terrainUI.island)
    {
        UIEditFloat(CLAY_ID("TerrainIslandRadius"), CLAY_STRING("Island radius"), &terrainUI.islandRadius, 1.0f, 10000.0f, 10.0f, 1);
        UIEditFloat(CLAY_ID("TerrainIslandFalloff"), CLAY_STRING("Island falloff"), &terrainUI.islandFalloff, 1.0f, 5000.0f, 10.0f, 1);
    }
}

static void TerrainEditModeUI(void)
{
    UISectionHeader("Edit Mode");
    UICheckbox(CLAY_ID("TerrainEditMode"), CLAY_STRING("Terrain edit mode"), &terrainUI.editMode);
    CLAY(CLAY_ID("TerrainModeButtons"), {
        .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(30.0f) }, .childGap = 6, .layoutDirection = CLAY_LEFT_TO_RIGHT }
    }) {
        if (UIButtonFlags(CLAY_ID("TerrainModeManipulate"), CLAY_STRING("Manipulate"), (Clay_Dimensions){ 72.0f, 26.0f }, terrainUI.mode == TerrainEditorMode_Manipulate, UIButtonFlag_FitText)) terrainUI.mode = TerrainEditorMode_Manipulate;
        if (UIButtonFlags(CLAY_ID("TerrainModePaint"), CLAY_STRING("Paint"), (Clay_Dimensions){ 58.0f, 26.0f }, terrainUI.mode == TerrainEditorMode_Paint, UIButtonFlag_FitText)) terrainUI.mode = TerrainEditorMode_Paint;
        if (UIButtonFlags(CLAY_ID("TerrainModeFoliage"), CLAY_STRING("Foliage"), (Clay_Dimensions){ 64.0f, 26.0f }, terrainUI.mode == TerrainEditorMode_Foliage, UIButtonFlag_FitText)) terrainUI.mode = TerrainEditorMode_Foliage;
        if (UIButtonFlags(CLAY_ID("TerrainModeGrass"), CLAY_STRING("Grass"), (Clay_Dimensions){ 58.0f, 26.0f }, terrainUI.mode == TerrainEditorMode_Grass, UIButtonFlag_FitText)) terrainUI.mode = TerrainEditorMode_Grass;
    }
    UIEditFloat(CLAY_ID("TerrainBrushRadius"), CLAY_STRING("Brush radius"), &terrainUI.brushRadius, 0.1f, 200.0f, 1.0f, 2);
    UIEditFloat(CLAY_ID("TerrainBrushStrength"), CLAY_STRING("Brush strength"), &terrainUI.brushStrength, -10.0f, 10.0f, 0.1f, 2);
    UIEditFloat(CLAY_ID("TerrainBrushSoftness"), CLAY_STRING("Brush softness"), &terrainUI.brushSoftness, 0.0f, 1.0f, 0.05f, 2);
    CLAY_TEXT(CLAY_STRING("Cursor preview: terrain hit point whitens while edit mode is active."), CLAY_TEXT_CONFIG({
        .fontSize = 13,
        .textColor = UIGetClayColor(UIColor_SubText)
    }));
}

static void TerrainLayersUI(void)
{
    UISectionHeader("Paint Layers");
    static const char* layerOptions[] = { "Layer 0", "Layer 1", "Layer 2", "Layer 3", "Layer 4", "Layer 5", "Layer 6", "Layer 7" };
    UIDropdown(CLAY_ID("TerrainLayerSelect"), CLAY_STRING("Selected layer"), layerOptions, EDITOR_TERRAIN_MAX_LAYERS, &terrainUI.selectedLayer);
    TerrainLayerUI* layer = &terrainUI.layers[terrainUI.selectedLayer];
    UICheckbox(CLAY_ID("TerrainLayerEnabled"), CLAY_STRING("Layer enabled"), &layer->enabled);
    TerrainLabeledText(CLAY_ID("TerrainLayerAlbedo"), "Albedo texture path", layer->albedo, sizeof(layer->albedo));
    TerrainLabeledText(CLAY_ID("TerrainLayerNormal"), "Normal texture path", layer->normal, sizeof(layer->normal));
}

static void TerrainFoliageControlsUI(void)
{
    UISectionHeader("Foliage Render Sets");
    static const char* foliageOptions[] = { "Foliage 1", "Foliage 2", "Foliage 3", "Foliage 4", "Foliage 5", "Foliage 6", "Foliage 7", "Foliage 8" };
    UIDropdown(CLAY_ID("TerrainFoliageSelect"), CLAY_STRING("Selected foliage"), foliageOptions, EDITOR_TERRAIN_MAX_FOLIAGE, &terrainUI.selectedFoliage);
    TerrainFoliageUI* f = &terrainUI.foliage[terrainUI.selectedFoliage];
    UICheckbox(CLAY_ID("TerrainFoliageUseFullScene"), CLAY_STRING("Use full scene bundles"), &terrainUI.foliageUseFullScene);
    UICheckbox(CLAY_ID("TerrainFoliageEnabled"), CLAY_STRING("Foliage enabled"), &f->enabled);
    TerrainLabeledText(CLAY_ID("TerrainFoliageName"), "Name", f->name, sizeof(f->name));
    TerrainLabeledText(CLAY_ID("TerrainFoliageMesh"), "Mesh path", f->meshPath, sizeof(f->meshPath));
    UIEditFloat(CLAY_ID("TerrainFoliageDensity"), CLAY_STRING("Density"), &f->density, 0.0f, 100.0f, 0.1f, 3);
    UIEditFloat(CLAY_ID("TerrainFoliageProbability"), CLAY_STRING("Probability"), &f->probability, 0.0f, 1.0f, 0.05f, 3);
    UIEditFloat(CLAY_ID("TerrainFoliageScaleMin"), CLAY_STRING("Scale min"), &f->scaleMin, 0.01f, 100.0f, 0.1f, 3);
    UIEditFloat(CLAY_ID("TerrainFoliageScaleMax"), CLAY_STRING("Scale max"), &f->scaleMax, 0.01f, 100.0f, 0.1f, 3);
    TerrainLabeledText(CLAY_ID("TerrainFoliageColor"), "Instance color hex AABBGGRR", f->colorHex, sizeof(f->colorHex));
    if (UIButtonFlags(CLAY_ID("TerrainFoliageGenerate"), CLAY_STRING("Generate Folliage Scene"), (Clay_Dimensions){ 120.0f, 28.0f }, false, UIButtonFlag_FitText))
        terrainUI.lastFoliageOk = TerrainGenerateFoliageScene();
    CLAY_TEXT(terrainUI.foliageReady ?
        (terrainUI.lastFoliageOk ? CLAY_STRING("TerrainFolliage.scene saved and active") : CLAY_STRING("Folliage generation failed")) :
        CLAY_STRING("Folliage scene not generated"), CLAY_TEXT_CONFIG({
            .fontSize = 13,
            .textColor = UIGetClayColor(UIColor_SubText)
        }));
}

static void TerrainGrassUI(void)
{
    UISectionHeader("Grass Blades");
    UIEditFloat(CLAY_ID("TerrainGrassDensity"), CLAY_STRING("Density"), &terrainUI.grassDensity, 0.0f, 1000.0f, 1.0f, 3);
    UIEditFloat(CLAY_ID("TerrainGrassScaleMin"), CLAY_STRING("Scale min"), &terrainUI.grassScaleMin, 0.01f, 10.0f, 0.1f, 3);
    UIEditFloat(CLAY_ID("TerrainGrassScaleMax"), CLAY_STRING("Scale max"), &terrainUI.grassScaleMax, 0.01f, 10.0f, 0.1f, 3);
    TerrainLabeledText(CLAY_ID("TerrainGrassColor"), "Blade color hex AABBGGRR", terrainUI.grassColorHex, sizeof(terrainUI.grassColorHex));
    UIButtonFlags(CLAY_ID("TerrainGrassGenerate"), CLAY_STRING("Generate grass"), (Clay_Dimensions){ 110.0f, 28.0f }, false, UIButtonFlag_FitText);
}

static void TerrainStatsUI(void)
{
    UISectionHeader("Runtime Stats");
    TerrainStats stats = Terrain_GetStats();
    UITextU32("Live chunks", stats.liveChunks);
    UITextU32("Empty chunks", stats.emptyChunks);
    UITextU32("Queued chunks", stats.queuedChunks);
    UITextU32("Jobs in flight", stats.jobsInFlight);
    UITextU32("Drawn chunks", stats.drawnChunks);
    UITextU32("Vertices", stats.numVertices);
    UITextU32("Indices", stats.numIndices);
}

static void TerrainDeletePopup(void)
{
    if (!terrainUI.deleteConfirmOpen) return;
    float2 center = { g_WindowState.prev_width * 0.5f - 220.0f, g_WindowState.prev_height * 0.5f - 85.0f };
    if (!UIBeginWindow("Delete Terrain?", center, (float2){ 440.0f, 170.0f }, &terrainUI.deleteConfirmOpen, UIWindowFlags_NoResize)) return;

    CLAY_TEXT(CLAY_STRING("Delete the active terrain from the scene?"), CLAY_TEXT_CONFIG({
        .fontSize = 15,
        .textColor = UIGetClayColor(UIColor_Text)
    }));
    CLAY_TEXT(CLAY_STRING("Saved .terrain files are not removed."), CLAY_TEXT_CONFIG({
        .fontSize = 13,
        .textColor = UIGetClayColor(UIColor_SubText)
    }));
    CLAY(CLAY_ID("TerrainDeleteButtons"), {
        .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(34.0f) }, .childGap = 10, .layoutDirection = CLAY_LEFT_TO_RIGHT }
    }) {
        if (UIButton(CLAY_ID("TerrainDeleteYes"), CLAY_STRING("Delete"), (Clay_Dimensions){ 96.0f, 30.0f }, false) || GetKeyPressed('\r'))
        {
            terrainUI.created = false;
            terrainUI.editMode = false;
            Terrain_DeleteWorld();
            terrainUI.deleteConfirmOpen = false;
        }
        if (UIButton(CLAY_ID("TerrainDeleteNo"), CLAY_STRING("Cancel"), (Clay_Dimensions){ 96.0f, 30.0f }, false))
            terrainUI.deleteConfirmOpen = false;
    }
    UIEndWindow();
}

void DrawTerrainWindow(bool* open)
{
    TerrainEditorInit();
    terrainTextDataCount = 0u;
    terrainUI.created = terrainUI.created || Terrain_GetEnabled();

    Clay_ElementId windowID = CLAY_ID("TerrainWindow");
    if (UIBeginWindowId(windowID, "Terrain", (float2){ 540.0f, 80.0f }, (float2){ 520.0f, 760.0f }, open, 0u))
    {
        TerrainToolbar();
        CLAY(CLAY_ID("TerrainEditorScroll"), UIScrollPanelDeclaration(UIWindowRemainingHeight(windowID, CLAY_ID("TerrainEditorScroll"), 0.0f), 12u)) {
            TerrainNoiseUI();
            UIDivider(CLAY_ID("TerrainNoiseDivider"));
            TerrainEditModeUI();
            UIDivider(CLAY_ID("TerrainEditDivider"));
            TerrainLayersUI();
            UIDivider(CLAY_ID("TerrainLayerDivider"));
            TerrainFoliageControlsUI();
            UIDivider(CLAY_ID("TerrainFoliageDivider"));
            TerrainGrassUI();
            UIDivider(CLAY_ID("TerrainGrassDivider"));
            TerrainStatsUI();
            CLAY_TEXT(terrainUI.lastSaveOk ? CLAY_STRING("Last save: ok") : CLAY_STRING("Last save: pending"), CLAY_TEXT_CONFIG({
                .fontSize = 13,
                .textColor = UIGetClayColor(UIColor_SubText)
            }));
        }
        UIEndWindow();
    }
    TerrainDeletePopup();
}
