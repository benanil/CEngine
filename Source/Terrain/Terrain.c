#include "Include/Terrain.h"
#include "Source/Terrain/TerrainInternal.h"
#include "Include/Graphics.h"
#include "Include/FileSystem.h"
#include "Include/Algorithm.h"
#include "Include/BasisCompressWrapper.h"

#define T_ALBEDO_SIZE  2048
#define T_DETAIL_SIZE  1024
#define T_ALBEDO_BASIS "Assets/Textures/Terrain/TerrainAlbedo.basis"
#define T_NORMAL_BASIS "Assets/Textures/Terrain/TerrainNormal.basis"
#define T_ARM_BASIS    "Assets/Textures/Terrain/TerrainARM.basis"

typedef struct TerrainState_
{
    bool initialized;
    bool enabled;
    TerrainGenParams genParams;
    TerrainAuthoring authoring;
    Texture albedoLayers;
    Texture normalLayers;
    Texture armLayers;
} TerrainState;

static TerrainState tp;

const char* const tAlbedoPaths[T_LAYER_COUNT] = {
    "Assets/Textures/Terrain/rocky_terrain_02_diff_2k.png",
    "Assets/Textures/Terrain/brown_mud_leaves_01_diff_2k.png",
    "Assets/Textures/Terrain/rocky_terrain_diff_2k.png",
    "Assets/Textures/Terrain/sandydrysoil-albedo2b.png"
};

const char* const tNormalPaths[T_LAYER_COUNT] = {
    "Assets/Textures/Terrain/rocky_terrain_02_nor_dx_1k.png",
    "Assets/Textures/Terrain/brown_mud_leaves_01_nor_dx_1k.png",
    "Assets/Textures/Terrain/rocky_terrain_nor_dx_1k.png",
    "Assets/Textures/Terrain/sandydrysoil-normal.png"
};

const char* const tMetallicRoughnessPaths[T_LAYER_COUNT] = {
    "Assets/Textures/Terrain/rocky_terrain_02_arm_1k.png",
    "Assets/Textures/Terrain/brown_mud_leaves_01_arm_2k.png",
    "Assets/Textures/Terrain/rocky_terrain_arm_1k.png",
    "Assets/Textures/Terrain/brown_mud_leaves_01_arm_2k.png"
};

void TerrainInitMaterialTextures(void) {
    if (tp.albedoLayers.handle && tp.normalLayers.handle && tp.armLayers.handle) return;
    tp.albedoLayers = basis_load_or_build_texture_array(tAlbedoPaths, T_LAYER_COUNT, T_ALBEDO_SIZE, true,
                                                        T_ALBEDO_BASIS, BASIS_FORMAT_UASTC,
                                                        "TerrainAlbedo");
    tp.normalLayers = basis_load_or_build_texture_array(tNormalPaths, T_LAYER_COUNT, T_DETAIL_SIZE, false,
                                                        T_NORMAL_BASIS, BASIS_FORMAT_UASTC | BASIS_FLAG_LINEAR,
                                                        "TerrainNormal");
    tp.armLayers = basis_load_or_build_texture_array(tMetallicRoughnessPaths, T_LAYER_COUNT, T_DETAIL_SIZE, false,
                                                     T_ARM_BASIS, BASIS_FORMAT_UASTC | BASIS_FLAG_LINEAR,
                                                     "TerrainARM");
}

static void TerrainAuthoringDefaults(TerrainAuthoring* authoring) {
    MemSet(authoring, 0, sizeof(*authoring));
    for (u32 i = 0; i < T_LAYER_COUNT; i++)
    {
        authoring->layers[i].enabled = true;
        StringCopy(tAlbedoPaths[i], authoring->layers[i].albedo, sizeof(authoring->layers[i].albedo));
        StringCopy(tNormalPaths[i], authoring->layers[i].normal, sizeof(authoring->layers[i].normal));
        StringCopy(tMetallicRoughnessPaths[i], authoring->layers[i].metallicRoughness, sizeof(authoring->layers[i].metallicRoughness));
    }
}

TerrainAuthoring* Terrain_GetAuthoring(void){
    if (!tp.initialized) tInit();
    return &tp.authoring;
}

void tInit(void)
{
    if (tp.initialized) return;
    Foliage_Init();
    tp.genParams = Terrain_DefaultGenParams();
    TerrainAuthoringDefaults(&tp.authoring);
    TerrainDensity_SetParams(&tp.genParams);
    TerrainEdit_Init();
    TerrainInitMaterialTextures();
    tp.initialized = true;
}

void tDestroy(void) {
    if (!tp.initialized) return;
    ReleaseTexture(&tp.albedoLayers);
    ReleaseTexture(&tp.normalLayers);
    ReleaseTexture(&tp.armLayers);
    TerrainEdit_Destroy();
    SDL_memset(&tp, 0, sizeof(tp));
}

void tSetEnabled(bool enabled) {
    if (!tp.initialized) tInit();
    tp.enabled = enabled;
}

bool tGetEnabled(void) {
    return tp.initialized && tp.enabled;
}

void Terrain_ApplyGenParams(const TerrainGenParams* params) {
    if (!params) return;
    if (!tp.initialized) tInit();
    tp.genParams = *params;
    tp.genParams.fixedWorldSize = (u32)Clamps32((s32)tp.genParams.fixedWorldSize, TERRAIN_FIXED_WORLD_MIN_SIZE, TERRAIN_FIXED_WORLD_MAX_SIZE);
    Foliage_SetSeed(tp.genParams.seed);
    TerrainDensity_SetParams(&tp.genParams);
    tInvalidateAll();
}

const TerrainGenParams* Terrain_GetGenParams(void) {
    if (!tp.initialized) tInit();
    return &tp.genParams;
}

void Terrain_CreateWorld(const TerrainGenParams* params) {
    Terrain_ApplyGenParams(params);
    tp.enabled = true;
}

void Terrain_DeleteWorld(void) {
    tp.enabled = false;
    TerrainEdit_Clear();
    tInvalidateAll();
}

void Terrain_SetBrushCursor(float3 position, f32 radius, bool active) {
    tSetBrushCursor(position, radius, active);
}

void Terrain_SculptSphere(float3 center, f32 radius, f32 strength, f32 softness) {
    if (!tGetEnabled()) return;
    float3 mn, mx;
    TerrainEdit_SculptSphere(center, radius, strength, softness, &mn, &mx);
    tInvalidateRegion(mn, mx);
}

void Terrain_PaintSphere(float3 center, f32 radius, u32 layer, f32 strength, f32 softness) {
    if (!tGetEnabled()) return;
    float3 mn, mx;
    TerrainEdit_PaintSphere(center, radius, (u8)Clamps32((s32)layer + 1, 1, 15), strength, softness, &mn, &mx);
    tInvalidateRegion(mn, mx);
}

// todo physics raycast
s32 tRaycast(float3 origin, float3 dir, f32 maxDist, u32 maxLod, BVHHit* hit) {
    (void)origin; (void)dir; (void)maxDist; (void)maxLod; (void)hit;
    return 0;
}

s32 tRaycastField(float3 origin, float3 dir, f32 maxDist, BVHHit* hit)
{
    if (!tGetEnabled()) return 0;
    f32 t = 0.0f;
    f32 lastT = 0.0f;
    for (u32 step = 0; step < 256u && t < maxDist; step++)
    {
        f32 px = origin.x + dir.x * t;
        f32 py = origin.y + dir.y * t;
        f32 pz = origin.z + dir.z * t;
        f32 sdf = TerrainDensity_At(px, py, pz);
        if (sdf < 0.0f)
        {
            f32 lo = lastT;
            f32 hi = t;
            for (u32 i = 0; i < 16u; i++)
            {
                f32 mid = (lo + hi) * 0.5f;
                f32 mx = origin.x + dir.x * mid;
                f32 my = origin.y + dir.y * mid;
                f32 mz = origin.z + dir.z * mid;
                if (TerrainDensity_At(mx, my, mz) < 0.0f) hi = mid; else lo = mid;
            }
            hit->hit.t = lo;
            hit->hit.u = 0.0f;
            hit->hit.v = 0.0f;
            hit->triIndex = 0u;
            hit->entityIdx = 0xFFFFFFFFu;
            hit->groupIdx = 0u;
            hit->skinnedSet = 0xFFFFFFFFu;
            hit->bundleIdx = 0xFFFFFFFFu;
            return 1;
        }
        lastT = t;
        t += Maxf32(sdf * 0.5f, 0.3f);
    }
    return 0;
}

TerrainStats tGetStats(void) {
    return (TerrainStats){0};
}

void RenderTerrainWireframe(SDL_GPUCommandBuffer* cmd, SDL_GPUColorTargetInfo* colorTarget, SDL_GPUDepthStencilTargetInfo* depthTarget, mat4x4 viewProj)
{
    (void)cmd; (void)colorTarget; (void)depthTarget; (void)viewProj;
}

bool tGetMaterialTextures(SDL_GPUTexture** albedo, SDL_GPUTexture** normal, SDL_GPUTexture** arm)
{
    if (!tp.initialized) tInit();
    TerrainInitMaterialTextures();
    if (albedo) *albedo = tp.albedoLayers.handle;
    if (normal) *normal = tp.normalLayers.handle;
    if (arm) *arm = tp.armLayers.handle;
    return tp.albedoLayers.handle && tp.normalLayers.handle && tp.armLayers.handle;
}

u32 Terrain_NumEditedRegions(void) {
    return TerrainEdit_NumChunks();
}

bool Terrain_SaveEditChunks(const char* path) {
    return tp.initialized && TerrainEdit_SaveChunks(path);
}

bool Terrain_LoadEditChunks(const char* path) {
    if (!tp.initialized) tInit();
    return TerrainEdit_LoadChunks(path);
}

bool Terrain_SaveWorld(const char* path) {
    if (!path || !path[0] || !tGetEnabled()) return false;
    EnsurePath(path);

    char* text = (char*)AllocTLSF(4096u);
    if (!text) return false;

    TerrainGenParams* params = &tp.genParams;
    TerrainAuthoring* authoring = &tp.authoring;
    char* p = text;
    p = WStr(p, "terrain 1\n");
    p = ParseWriteF32(p, "fixed_world_size", (f32)params->fixedWorldSize, 0);
    p = ParseWriteBool(p, "island", params->island);
    p = ParseWriteF32(p, "seed", (f32)params->seed, 0);
    p = ParseWriteF32(p, "sea_level", params->seaLevel, 3);
    p = ParseWriteF32(p, "base_height"    , params->baseHeight, 3);
    p = ParseWriteF32(p, "hill_amplitude" , params->hillAmplitude, 3);
    p = ParseWriteF32(p, "hill_frequency" , params->hillFrequency, 6);
    p = ParseWriteF32(p, "ridge_amplitude", params->ridgeAmplitude, 3);
    p = ParseWriteF32(p, "ridge_frequency", params->ridgeFrequency, 6);
    p = ParseWriteF32(p, "cave_amplitude" , params->carveAmplitude, 3);
    p = ParseWriteF32(p, "cave_frequency" , params->carveFrequency, 6);
    p = ParseWriteF32(p, "island_radius"  , params->islandRadius, 3);
    p = ParseWriteF32(p, "island_falloff" , params->islandFalloff, 3);

    WriteAllBytes(path, text, (unsigned long)(p - text));
    DeAllocTLSF(text);

    char chunksPath[512];
    StringCopy(path, chunksPath, sizeof(chunksPath));
    ChangeExtension(chunksPath, StringLength(chunksPath), "chunks");
    EnsurePath(chunksPath);
    bool success = FileExist(path) && Terrain_SaveEditChunks(chunksPath);
    ChangeExtension(chunksPath, StringLength(chunksPath), "foliage");
    Foliage_Save(chunksPath);
    return success;
}

void Terrain_LoadWorld(const char* path) {
    if (!path || !path[0]) return;
    if (!tp.initialized) tInit();

    char* text = ReadAllFileAlloc(path);
    if (!text) return;

    TerrainGenParams params = Terrain_DefaultGenParams();
    TerrainAuthoringDefaults(&tp.authoring);
    const char* value;
    char* line = text;
    while (line && *line) {
        char* next = line;
        while (*next && *next != '\n') next++;
        bool hadNewline = *next == '\n';
        *next = '\0';

        if      (ParseKeyIs(line, "fixed_world_size", &value)) { f32 f; ParseFloat(value, &f); params.fixedWorldSize = (u32)Clamps32((s32)f, TERRAIN_FIXED_WORLD_MIN_SIZE, TERRAIN_FIXED_WORLD_MAX_SIZE); }
        else if (ParseKeyIs(line, "island"          , &value)) params.island = value[0] == '1';
        else if (ParseKeyIs(line, "seed"            , &value)) { f32 f; ParseFloat(value, &f); params.seed = (u32)f; }
        else if (ParseKeyIs(line, "sea_level"       , &value)) ParseFloat(value, &params.seaLevel);
        else if (ParseKeyIs(line, "base_height"     , &value)) ParseFloat(value, &params.baseHeight);
        else if (ParseKeyIs(line, "hill_amplitude"  , &value)) ParseFloat(value, &params.hillAmplitude);
        else if (ParseKeyIs(line, "hill_frequency"  , &value)) ParseFloat(value, &params.hillFrequency);
        else if (ParseKeyIs(line, "ridge_amplitude" , &value)) ParseFloat(value, &params.ridgeAmplitude);
        else if (ParseKeyIs(line, "ridge_frequency" , &value)) ParseFloat(value, &params.ridgeFrequency);
        else if (ParseKeyIs(line, "cave_amplitude"  , &value)) ParseFloat(value, &params.carveAmplitude);
        else if (ParseKeyIs(line, "cave_frequency"  , &value)) ParseFloat(value, &params.carveFrequency);
        else if (ParseKeyIs(line, "island_radius"   , &value)) ParseFloat(value, &params.islandRadius);
        else if (ParseKeyIs(line, "island_falloff"  , &value)) ParseFloat(value, &params.islandFalloff);

        line = hadNewline ? next + 1 : NULL;
    }
    FreeAllText(text);

    char chunksPath[512];
    StringCopy(path, chunksPath, sizeof(chunksPath));
    Terrain_LoadEditChunks(chunksPath);
    ChangeExtension(chunksPath, StringLength(chunksPath), "foliage");
    Foliage_Load(chunksPath);
    Terrain_CreateWorld(&params);
}
