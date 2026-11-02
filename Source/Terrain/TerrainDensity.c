// procedural density field for the terrain: a 2D fbm + ridge heightfield carved by a 3D
// noise term so overhangs and caves exist (the transvoxel mesher is fully 3D). pure
// functions of world position and the active gen params, safe to call from worker
// threads: params only change while in flight jobs are already condemned (see
// Terrain_ApplyGenParams). noise functions below are ported from Dodisoft.NewTerrain.
#include "TerrainInternal.h"
#include "Include/Terrain.h"
#include "Math/Noise.h"

#define TD_START_HEIGHT 0.38f
#define TD_WEIGHT       0.5f
#define TD_MULT         0.45f

// TERRAIN_BEDROCK_Y is deliberately NOT a multiple of the lod chunk sizes (16/32/64/128):
// if the floor landed on a chunk boundary the surface would fall on the seam with the
// all-solid chunk below it and mesh degenerately, leaving a hole at the bottom of a deep dig.
// extra mountain height toward the island center (island mode only). elevation above the
// base is scaled by up to (1 + this) at the center, tapering to 1 at the coast.
#define TD_CENTER_GAIN  0.75f
// seed turns into a large noise domain offset, world coords stay near the origin
// so the s8 quantization and chunk keys are unaffected
static f32 td_SeedOffsetX, td_SeedOffsetZ;

static const int plnOctav = 1;
static const int plnPersistency = 0.5f;
static const int plnLacunarity = 2.0f;
static const int plnSeed = 2.0f;

static const int slxOctav = 1;
static const int slxPersistency = 0.5f;
static const int slxLacunarity = 2.0f;

static TerrainGenParams td_Params = {
    .seed           = 1.0f,
    .seaLevel       = -25.0f,
    .baseHeight     = -30.0f,
    .hillAmplitude  = 0.4f,
    .hillFrequency  = 0.5f,
    .ridgeAmplitude = 0.5f,
    .ridgeFrequency = 0.2f,
    .carveAmplitude = 0.3f,
    .carveFrequency = 0.045f,
    .fixedWorldSize = (f32)TERRAIN_FIXED_WORLD_DEFAULT_SIZE,
    .islandRadius   = 260.0f,
    .islandFalloff  = 100.0f,
    .island         = true
};

TerrainGenParams Terrain_DefaultGenParams(void) {
    TerrainGenParams defaults = {
        .seed           = 1.0f,
        .seaLevel       = -25.0f,
        .baseHeight     = -30.0f,
        .hillAmplitude  = 0.4f,
        .hillFrequency  = 0.5f,
        .ridgeAmplitude = 0.5f,
        .ridgeFrequency = 0.2f,
        .carveAmplitude = 0.3f,
        .carveFrequency = 0.045f,
        .fixedWorldSize = (f32)TERRAIN_FIXED_WORLD_DEFAULT_SIZE,
        .islandRadius   = 260.0f,
        .islandFalloff  = 100.0f,
        .island         = false
    };
    return defaults;
}

void TerrainDensity_SetParams(const TerrainGenParams* params) {
    td_Params = *params;
    u32 h = params->seed * 0x9E3779B9u;
    td_SeedOffsetX = (f32)(h & 0xFFFFu) * 0.731f;
    td_SeedOffsetZ = (f32)((h >> 16) & 0xFFFFu) * 0.617f;
}

const TerrainGenParams* TerrainDensity_GetParams(void) { return &td_Params; }

static v128f TerrainNoisePerlin2V(v128f x, v128f y)
{
    return PerlinNoiseFBM(Vec2WFromV(x, y), plnOctav, plnPersistency, plnLacunarity, plnSeed);
}

static v128f TerrainNoiseSimplex2V(v128f x, v128f y)
{
    return SimplexNoiseFBM(Vec2WFromV(x, y), slxOctav, slxPersistency, slxLacunarity);
}

static v128f TerrainNoiseSimplexRidgedSimpleV(v128f x, v128f y)
{
    v128f f = VecMulf(VecFabs(TerrainNoiseSimplex2V(x, y)), 0.5000f);
    v128f ox = x, oy = y;
    x = VecAdd(VecMulf(ox, 1.6f), VecMulf(oy, 1.2f));
    y = VecAdd(VecMulf(ox, -1.2f), VecMulf(oy, 1.6f));
    f = VecFmadd(VecFabs(TerrainNoiseSimplex2V(x, y)), VecSet1(0.2500f), f);
    return VecClamp(VecSub(VecSet1(0.68f), f), VecSet1(0.05f), VecOne());
}

static v128f TerrainNoisePerlinTerrainV(v128f x, v128f y, int freq, f32 h, f32 w, f32 m)
{
    v128f hv = VecSet1(h);
    for (int i = 0; i < freq; i++)
    {
        hv = VecFmadd(TerrainNoisePerlin2V(VecMulf(x, m), VecMulf(y, m)), VecSet1(w), hv);
        w *= 0.5f;
        m *= 2.0f;
    }
    return VecClamp(VecAddf(VecMulf(hv, 0.68f), 0.1f), VecSet1(0.04f), VecOne());
}

static v128f TerrainNoiseDensity01V(v128f x, v128f z)
{
    f32 noiseScale    = Clampf32(td_Params.hillFrequency , 0.05f, 4.0f);
    f32 lowlandScale  = Clampf32(td_Params.hillAmplitude , 0.25f, 8.0f);
    f32 mountainScale = Clampf32(td_Params.ridgeFrequency, 0.05f, 2.0f);
    v128f cx = VecMulf(VecAddf(x, td_SeedOffsetX), noiseScale / (f32)T_CHUNK_CELLS);
    v128f cz = VecMulf(VecAddf(z, td_SeedOffsetZ), noiseScale / (f32)T_CHUNK_CELLS);
    v128f mountain = VecMul(TerrainNoiseSimplexRidgedSimpleV(VecMulf(cx, mountainScale), VecMulf(cz, mountainScale)),
                            TerrainNoisePerlinTerrainV(VecMulf(cx, lowlandScale * 4.0f), VecMulf(cz, lowlandScale * 4.0f), 2, TD_START_HEIGHT, TD_WEIGHT, TD_MULT));
    v128f lowland = TerrainNoisePerlinTerrainV(VecMulf(cx, lowlandScale), VecMulf(cz, lowlandScale), 4, TD_START_HEIGHT, TD_WEIGHT, TD_MULT);
    return VecFmadd(VecSub(mountain, lowland), VecSet1(0.64f), lowland);
}

v128f TerrainDensity_IslandFadeV(v128f x, v128f z)
{
    if (!td_Params.island) return VecZero();
    v128f dist = VecSqrt(VecAdd(VecMul(x, x), VecMul(z, z)));
    v128f t = VecClamp(VecDivf(VecSubf(dist, td_Params.islandRadius), Maxf32(td_Params.islandFalloff, 1.0f)), VecZero(), VecOne());
    return VecMul(VecMul(t, t), VecSub(VecSet1(3.0f), VecMulf(t, 2.0f)));
}

v128f TerrainDensity_HeightV(v128f x, v128f z)
{
    v128f density = TerrainNoiseDensity01V(x, z);
    f32 heightScale = Clampf32(td_Params.ridgeAmplitude, 0.25f, 2.0f);
    v128f elev = VecMulf(VecAddf(VecMulf(VecSubf(VecMulf(density, 255.0f), 7.0f), 0.3333333f), 9.666667f), heightScale);

    if (td_Params.island)
    {
        v128f t = TerrainDensity_IslandFadeV(x, z);
        elev = VecMul(elev, VecAddf(VecMulf(VecSub(VecOne(), t), TD_CENTER_GAIN), 1.0f));
        v128f height = VecAddf(elev, td_Params.baseHeight);
        return VecFmadd(VecSub(VecSet1(td_Params.seaLevel), height), t, height);
    }
    return VecAddf(elev, td_Params.baseHeight);
}

static v128f TerrainDensity_CarveV(v128f x, v128f y, v128f z)
{
    f32 freq = td_Params.carveFrequency;
    v128f nx = VecAdd(VecMulf(VecAddf(x, td_SeedOffsetX), freq), VecMulf(y, freq * 0.37f));
    v128f nz = VecSub(VecMulf(VecAddf(z, td_SeedOffsetZ), freq), VecMulf(y, freq * 0.19f));
    return VecMulf(TerrainNoiseSimplex2V(nx, nz), td_Params.carveAmplitude);
}

// raw analytic field: terrain heightfield + 3D carve, no bedrock floor, no sculpt. the
// floor is a min applied by the callers as their final step so sculpt can never defeat it.
static v128f VCALL TerrainDensity_RawV(v128f x, v128f y, v128f z)
{
    v128f islandFade = TerrainDensity_IslandFadeV(x, z);
    v128f carve = VecMul(TerrainDensity_CarveV(x, y, z), VecSub(VecOne(), islandFade));
    return VecAdd(VecSub(y, TerrainDensity_HeightV(x, z)), carve);
}

v128f TerrainDensity_SDF(v128f x, v128f y, v128f z)
{
    // solid bedrock floor: min with (y - surface) keeps integer samples at/below
    // TERRAIN_BEDROCK_Y negative (solid), so the world has no bottom to fall through
    return VecMax(TerrainDensity_RawV(x, y, z), VecSubf(y, TERRAIN_BEDROCK_SURFACE_Y));
}

f32 TerrainDensity_At(f32 x, f32 y, f32 z)
{
    f32 density = VecGetX(TerrainDensity_RawV(VecSet1(x), VecSet1(y), VecSet1(z)));
    // sculpt edits raise/lower the field so grass follows painted/sculpted terrain, added
    // BEFORE the floor min so a dig can never punch through the bedrock (floor always wins).
    // skip the map probe when nothing is edited.
    if (TerrainEdit_NumChunks() != 0u)
        density += TerrainEdit_DeltaAt((s32)Floorf32(x + 0.5f), (s32)Floorf32(y + 0.5f), (s32)Floorf32(z + 0.5f));
    return Minf32(density, y - TERRAIN_BEDROCK_SURFACE_Y);
}

f32 TerrainDensity_SurfaceY(f32 x, f32 z, f32 startY, float3* outNormal)
{
    // the sdf is ~signed distance in meters and d(sdf)/dy is ~1, so y -= sdf is a Newton
    // step that converges in a couple of iterations from the analytic height seed
    f32 y = startY;
    for (s32 i = 0; i < 6; i++)
        y -= TerrainDensity_At(x, y, z);

    if (outNormal)
    {
        const f32 e = 0.5f;
        float3 grad = {
            TerrainDensity_At(x + e, y, z) - TerrainDensity_At(x - e, y, z),
            TerrainDensity_At(x, y + e, z) - TerrainDensity_At(x, y - e, z),
            TerrainDensity_At(x, y, z + e) - TerrainDensity_At(x, y, z - e)
        };
        // the sdf increases toward air, so its gradient is the outward (up-facing) normal
        *outNormal = F3NormSafe(grad);
    }
    return y;
}

void TerrainDensity_GetYRange(f32* outMin, f32* outMax)
{
    // Ported algorithm maps density 0.04..1.0 through the original y formula.
    f32 heightScale = Clampf32(td_Params.ridgeAmplitude, 0.25f, 2.0f);
    f32 minElev = ((0.04f * 255.0f - 7.0f) * 0.3333333f + 9.666667f) * heightScale;
    f32 maxElev = ((1.00f * 255.0f - 7.0f) * 0.3333333f + 9.666667f) * heightScale;
    if (td_Params.island) maxElev *= 1.0f + TD_CENTER_GAIN; // center peaks are the tallest
    f32 lo = td_Params.baseHeight + minElev - td_Params.carveAmplitude - 2.0f;
    f32 hi = td_Params.baseHeight + maxElev + td_Params.carveAmplitude + 2.0f;
    // stream a chunk past the bedrock floor so the solid floor itself always meshes
    lo = Minf32(lo, TERRAIN_BEDROCK_Y - (f32)T_CHUNK_CELLS);
    if (td_Params.island)
    {
        lo = Minf32(lo, td_Params.seaLevel - td_Params.carveAmplitude - 2.0f);
        hi = Maxf32(hi, td_Params.seaLevel + 2.0f);
    }
    *outMin = lo;
    *outMax = hi;
}

// quantize a signed distance in meters to s8. the scale is world fixed so a coarse
// sample bit-matches the fine sample at the same position (see TERRAIN_SDF_CLAMP)
static s8 TerrainDensity_Quantize(f32 sdf)
{
    f32 q = Clampf32(sdf * (127.0f / TERRAIN_SDF_CLAMP), -127.0f, 127.0f);
    s32 v = (s32)(q >= 0.0f ? q + 0.5f : q - 0.5f);
    return v == 0 ? (s8)Signf(q) : (s8)v;
}

void TerrainDensity_SampleChunk(s32 cx, s32 cy, s32 cz, s8* out)
{
    const f32 voxel = T_VOXEL_SIZE;
    const f32 ox = (f32)cx * (T_CHUNK_CELLS * voxel);
    const f32 oy = (f32)cy * (T_CHUNK_CELLS * voxel);
    const f32 oz = (f32)cz * (T_CHUNK_CELLS * voxel);
    const f32 carveSkip = TERRAIN_SDF_CLAMP + td_Params.carveAmplitude;

    // the heightfield part only depends on the column, evaluate it once per (x,z)
    f32 heights[T_SAMPLES_AXIS * T_SAMPLES_AXIS];
    for (s32 z = 0; z < T_SAMPLES_AXIS; z++)
    for (s32 x = 0; x < T_SAMPLES_AXIS; x += 4)
    {
        v128f wx = VecAdd(VecSet1(ox + (f32)(x - 1) * voxel), VecMulf(Vec0123(), voxel));
        v128f wz = VecSet1(oz + (f32)(z - 1) * voxel);
        f32 h[4];
        VecStore(h, TerrainDensity_HeightV(wx, wz));
        u32 n = Minu32(4u, (u32)(T_SAMPLES_AXIS - x));
        for (u32 i = 0; i < n; i++) heights[z * T_SAMPLES_AXIS + x + i] = h[i];
    }

    s8* dst = out;
    for (s32 z = 0; z < T_SAMPLES_AXIS; z++)
    for (s32 y = 0; y < T_SAMPLES_AXIS; y++)
    for (s32 x = 0; x < T_SAMPLES_AXIS; x += 4)
    {
        v128f wx = VecAdd(VecSet1(ox + (f32)(x - 1) * voxel), VecMulf(Vec0123(), voxel));
        v128f wy = VecSet1(oy + (f32)(y - 1) * voxel);
        v128f wz = VecSet1(oz + (f32)(z - 1) * voxel);
        f32 h[4] = {0};
        u32 n = Minu32(4u, (u32)(T_SAMPLES_AXIS - x));
        for (u32 i = 0; i < n; i++) h[i] = heights[z * T_SAMPLES_AXIS + x + i];
        v128f sdf = VecSub(wy, VecLoad(h));
        // the carve term only matters near the surface, skip the 3D noise when the
        // column distance alone saturates the s8 range
        v128f carveMask = VecAnd(VecCmpGt(sdf, VecSet1(-carveSkip)), VecCmpLt(sdf, VecSet1(carveSkip)));
        if (VecMovemask(carveMask))
        {
            v128f carve = VecMul(TerrainDensity_CarveV(wx, wy, wz), VecSub(VecOne(), TerrainDensity_IslandFadeV(wx, wz)));
            sdf = VecAdd(sdf, VecSelect(VecZero(), carve, carveMask));
        }
        f32 s[4];
        VecStore(s, sdf);
        for (u32 i = 0; i < n; i++) *dst++ = TerrainDensity_Quantize(s[i]);
    }

    TerrainEdit_OverlayChunk(cx, cy, cz, out);

    // bedrock floor, applied last so sculpt can never dig through it. each y-plane clamps
    // its samples toward solid by the quantized (worldY - floor): above the floor that
    // threshold saturates to +127 and the min is a no-op, below it drives samples solid.
    for (s32 y = 0; y < T_SAMPLES_AXIS; y++)
    {
        s32 floorQ = TerrainDensity_Quantize(oy + (f32)(y - 1) * voxel - TERRAIN_BEDROCK_SURFACE_Y);
        for (s32 z = 0; z < T_SAMPLES_AXIS; z++)
        for (s32 x = 0; x < T_SAMPLES_AXIS; x++)
        {
            s8* p = &out[(z * T_SAMPLES_AXIS + y) * T_SAMPLES_AXIS + x];
            *p = (s8)Mins32(*p, floorQ);
        }
    }
}
