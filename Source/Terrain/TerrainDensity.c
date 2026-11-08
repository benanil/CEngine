// procedural density field for the terrain: a 2D fbm carved by a 3D
// noise term so overhangs and caves exist (the transvoxel mesher is fully 3D). pure
// functions of world position and the active gen params, safe to call from worker
// threads: params only change while in flight jobs are already condemned 
// see (Terrain_ApplyGenParams).
#include "TerrainInternal.h"
#include "Include/Terrain.h"
#include "Include/Random.h"
#include "Math/Noise.h"

// TERRAIN_BEDROCK_Y is deliberately NOT a multiple of the lod chunk sizes (16/32/64/128):
// if the floor landed on a chunk boundary the surface would fall on the seam with the
// all-solid chunk below it and mesh degenerately, leaving a hole at the bottom of a deep dig.
// extra mountain height toward the island center (island mode only). elevation above the
// base is scaled by up to (1 + this) at the center, tapering to 1 at the coast.
#define TD_CENTER_GAIN  0.75f

// seed turns into a large noise domain offset, world coords stay near the origin
// so the s8 quantization and chunk keys are unaffected

static f32 td_SeedOffsetX, td_SeedOffsetZ;

static TerrainGenParams td_Params = {
    .seed            = 1.0f,
    .seaLevel        = -45.0f,
    .baseHeight      = -30.0f,
    .landFreq        = 0.43f,
    .carveAmplitude  = 0.3f,
    .carveFreq       = 0.045f,
    .islandRadius    = 260.0f,
    .islandFalloff   = 100.0f,
    .island          = false,
    .offsetX         = 123.0f,
    .offsetZ         = 434.0f
};

TerrainGenParams Terrain_DefaultGenParams(void) {
    TerrainGenParams defaults = {
        .seed           = 1.0f,
        .seaLevel       = -45.0f,
        .baseHeight     = -30.0f,
        .landFreq       = 0.23f,
        .carveAmplitude = 0.3f,
        .carveFreq      = 0.045f,
        .islandRadius   = 260.0f,
        .islandFalloff  = 100.0f,
        .island         = false
    };
    u32 h = VeciGetX(HashKey(VeciSet1(defaults.seed)));
    defaults.offsetX = (f32)(h & 0xFFFFu) * 0.431f;
    defaults.offsetZ = (f32)((h >> 16) & 0xFFFFu) * 0.517f;
    return defaults;
}

void TD_SetParams(const TerrainGenParams* params) {
    td_Params = *params;
    u32 h = VeciGetX(HashKey(VeciSet1(params->seed)));
    td_Params.offsetX = (f32)(h & 0xFFFFu) * 0.431f;
    td_Params.offsetZ = (f32)((h >> 16) & 0xFFFFu) * 0.517f;
}

const TerrainGenParams* TD_GetParams(void) { return &td_Params; }

static inline v128f Ridge(v128f n)
{
    v128f a = VecFmadd(VecNeg(n), n, VecOne());
    return VecMul(a, a);
}

// https://www.shadertoy.com/view/sXVSWt
static v128f NoiseDensity01V(v128f x, v128f z)
{
    const TerrainGenParams* p = &td_Params;
    const f32 f = p->landFreq * 0.1f;
    Vec2W pos = (Vec2W) {
        VecMulf(VecAddf(x, p->offsetX), f),
        VecMulf(VecAddf(z, p->offsetZ), f)
    };

    const v128u base = VeciSet1(p->seed ^ 0x687453u);
    const v128u s4 = HashKey(VeciAdd(base, VeciSetR(12, 34, 56, 78)));
    const v128u s5 = HashKey(s4);

    Vec2W qc = Vec2WMulf(pos, 0.01f);
    Vec2W pc = (Vec2W) {
        VecFmadd(Perlin2(qc, VeciSplatY(s4)), VecSet1(14.0f), pos.x),
        VecFmadd(Perlin2(qc, VeciSplatX(s4)), VecSet1(14.0f), pos.y)
    };

    v128f inland = SmoothstepV(Perlin2(Vec2WMulf(pc, 0.016f), VeciSplatX(s5)), -0.3f, 0.4f);
    v128f ridgeLine = Vec1Minus(VecFabs(Perlin2(Vec2WMulf(pc, 0.011f), VeciSplatY(s5))));
    v128f chain = VecMul(SmoothstepV(ridgeLine, 0.72f, 0.94f), inland);

    Vec2W q = Vec2WMulf(pos, 0.025f);
    pos.x = VecFmadd(Perlin2(q, VeciSplatX(s4)), VecSet1(3.5f), pos.x);
    pos.y = VecFmadd(Perlin2(q, VeciSplatY(s4)), VecSet1(3.5f), pos.y);

    v128f r    = Perlin2(Vec2WMulf(pos, 0.035f), VeciSplatZ(s4));
    v128f t    = VecMul(SmoothstepV(r, -0.4f, 0.0f), inland);
    v128f mask = VecMul(SmoothstepV(r, -0.4f, 0.3f), chain);

    v128f fine  = VecFmadd(Perlin2(Vec2WMulf(pos, 0.25f), VeciSplatX(s4)), VecSet1(0.4f), VecSet1(0.6f));
    v128f plain = VecFmadd(Perlin2(Vec2WMulf(pos, 0.07f), VeciSplatY(s4)), VecSet1(0.275f), VecSet1(0.275f));
    v128f peaks = Ridge(Perlin2FBM(Vec2WMulf(pos, 0.08f), 2, 0.5f, 2.0f, VeciGetZ(s5)));

    v128f land = VecLerpV(plain, fine, t);
    v128f low  = VecMulf(land, 0.5f);
    v128f mt   = VecFmadd(land, VecSet1(0.15f), VecMulf(peaks, 0.85f));
    v128f high = VecFmadd(mt, VecSet1(0.5f), VecSet1(0.5f));

    return VecLerpV(low, high, mask);
}

v128f TD_IslandFadeV(v128f x, v128f z)
{   
    if (!td_Params.island) return VecZero();
    v128f dist = VecSqrt(VecAdd(VecMul(x, x), VecMul(z, z)));
    v128f t = VecClamp(VecDivf(VecSubf(dist, td_Params.islandRadius), Maxf32(td_Params.islandFalloff, 1.0f)), VecZero(), VecOne());
    return VecMul(VecMul(t, t), VecSub(VecSet1(3.0f), VecMulf(t, 2.0f)));
}

v128f TD_HeightV(v128f x, v128f z)
{
    v128f density = NoiseDensity01V(x, z);
    v128f elev = VecAddf(VecMulf(VecSubf(VecMulf(density, 255.0f), 7.0f), 0.3333333f), 9.666667f);

    if (td_Params.island)
    {
        v128f t = TD_IslandFadeV(x, z);
        elev = VecMul(elev, VecAddf(VecMulf(VecSub(VecOne(), t), TD_CENTER_GAIN), 1.0f));
        v128f height = VecAddf(elev, td_Params.baseHeight);
        return VecFmadd(VecSub(VecSet1(td_Params.seaLevel), height), t, height);
    }
    return VecAddf(elev, td_Params.baseHeight);
}

static v128f CarveV(v128f x, v128f y, v128f z)
{
    f32 freq = td_Params.carveFreq;
    v128f nx = VecAdd(VecMulf(VecAddf(x, td_SeedOffsetX), freq), VecMulf(y, freq * 0.37f));
    v128f ny = VecAdd(VecMulf(VecAddf(y, td_SeedOffsetX), freq), VecMulf(y, freq * 0.37f));
    v128f nz = VecSub(VecMulf(VecAddf(z, td_SeedOffsetZ), freq), VecMulf(y, freq * 0.19f));
    return Perlin3FBM(Vec3WFromV(nx, ny, nz), 2, td_Params.carveAmplitude, 2.0f, td_Params.seed * 4u);
}

// raw analytic field: terrain heightfield + 3D carve, no bedrock floor, no sculpt. the
// floor is a min applied by the callers as their final step so sculpt can never defeat it.
static v128f VCALL RawV(v128f x, v128f y, v128f z)
{
    v128f islandFade = TD_IslandFadeV(x, z);
    v128f carve = VecMul(CarveV(x, y, z), Vec1Minus(islandFade));
    return VecAdd(VecSub(y, TD_HeightV(x, z)), carve);
}

v128f TD_SDF(v128f x, v128f y, v128f z)
{
    // solid bedrock floor: min with (y - surface) keeps integer samples at/below
    // TERRAIN_BEDROCK_Y negative (solid), so the world has no bottom to fall through
    return VecMax(RawV(x, y, z), VecSubf(y, TERRAIN_BEDROCK_SURFACE_Y));
}

f32 TD_At(f32 x, f32 y, f32 z)
{
    f32 density = VecGetX(RawV(VecSet1(x), VecSet1(y), VecSet1(z)));
    // sculpt edits raise/lower the field so grass follows painted/sculpted terrain, added
    // BEFORE the floor min so a dig can never punch through the bedrock (floor always wins).
    // skip the map probe when nothing is edited.
    if (TerrainEdit_NumChunks() != 0u)
        density += TerrainEdit_DeltaAt((s32)Floorf32(x + 0.5f), (s32)Floorf32(y + 0.5f), (s32)Floorf32(z + 0.5f));
    return Minf32(density, y - TERRAIN_BEDROCK_SURFACE_Y);
}

f32 TD_SurfaceY(f32 x, f32 z, f32 startY, float3* outNormal)
{
    // the sdf is ~signed distance in meters and d(sdf)/dy is ~1, so y -= sdf is a Newton
    // step that converges in a couple of iterations from the analytic height seed
    f32 y = startY;
    for (s32 i = 0; i < 6; i++)
        y -= TD_At(x, y, z);

    if (outNormal)
    {
        const f32 e = 0.5f;
        float3 grad = {
            TD_At(x + e, y, z) - TD_At(x - e, y, z),
            TD_At(x, y + e, z) - TD_At(x, y - e, z),
            TD_At(x, y, z + e) - TD_At(x, y, z - e)
        };
        // the sdf increases toward air, so its gradient is the outward (up-facing) normal
        *outNormal = F3NormSafe(grad);
    }
    return y;
}

void TD_GetYRange(f32* outMin, f32* outMax)
{
    // Ported algorithm maps density 0.04..1.0 through the original y formula.
    f32 minElev = ((0.04f * 255.0f - 7.0f) * 0.3333333f + 9.666667f);
    f32 maxElev = ((1.00f * 255.0f - 7.0f) * 0.3333333f + 9.666667f);
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
static s8 TD_Quantize(f32 sdf)
{
    f32 q = Clampf32(sdf * (127.0f / TERRAIN_SDF_CLAMP), -127.0f, 127.0f);
    s32 v = (s32)(q >= 0.0f ? q + 0.5f : q - 0.5f);
    return v == 0 ? (s8)Signf(q) : (s8)v;
}

void TD_SampleChunk(s32 cx, s32 cy, s32 cz, s8* out)
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
        VecStore(h, TD_HeightV(wx, wz));
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
            v128f carve = VecMul(CarveV(wx, wy, wz), VecSub(VecOne(), TD_IslandFadeV(wx, wz)));
            sdf = VecAdd(sdf, VecSelect(VecZero(), carve, carveMask));
        }
        f32 s[4];
        VecStore(s, sdf);
        for (u32 i = 0; i < n; i++) *dst++ = TD_Quantize(s[i]);
    }

    TerrainEdit_OverlayChunk(cx, cy, cz, out);

    // bedrock floor, applied last so sculpt can never dig through it. each y-plane clamps
    // its samples toward solid by the quantized (worldY - floor): above the floor that
    // threshold saturates to +127 and the min is a no-op, below it drives samples solid.
    for (s32 y = 0; y < T_SAMPLES_AXIS; y++)
    {
        s32 floorQ = TD_Quantize(oy + (f32)(y - 1) * voxel - TERRAIN_BEDROCK_SURFACE_Y);
        for (s32 z = 0; z < T_SAMPLES_AXIS; z++)
        for (s32 x = 0; x < T_SAMPLES_AXIS; x++)
        {
            s8* p = &out[(z * T_SAMPLES_AXIS + y) * T_SAMPLES_AXIS + x];
            *p = (s8)Mins32(*p, floorQ);
        }
    }
}
