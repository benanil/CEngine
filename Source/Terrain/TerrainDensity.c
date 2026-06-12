// procedural density field for the terrain: a 2D fbm + ridge heightfield carved by a 3D
// noise term so overhangs and caves exist (the transvoxel mesher is fully 3D). pure
// functions of world position, safe to call from worker threads: stb_perlin only reads
// static tables. this is the only translation unit defining the stb_perlin implementation
#include "TerrainInternal.h"

#define STB_PERLIN_IMPLEMENTATION
#include "Extern/stb/stb_perlin.h"

// all tunables in one place
#define TD_BASE_HEIGHT     -8.0f
#define TD_HILL_AMP        14.0f
#define TD_HILL_FREQ       0.012f
#define TD_RIDGE_AMP       46.0f
#define TD_RIDGE_FREQ      0.0021f
#define TD_CARVE_AMP       7.0f
#define TD_CARVE_FREQ      0.045f

static f32 TerrainDensity_Height(f32 x, f32 z)
{
    f32 hills = stb_perlin_fbm_noise3(x * TD_HILL_FREQ, 0.0f, z * TD_HILL_FREQ, 2.0f, 0.5f, 5);
    f32 ridge = stb_perlin_ridge_noise3(x * TD_RIDGE_FREQ, 7.7f, z * TD_RIDGE_FREQ, 2.0f, 0.5f, 1.0f, 5);
    return TD_BASE_HEIGHT + TD_HILL_AMP * hills + TD_RIDGE_AMP * ridge;
}

static f32 TerrainDensity_Carve(f32 x, f32 y, f32 z)
{
    return TD_CARVE_AMP * stb_perlin_noise3(x * TD_CARVE_FREQ, y * TD_CARVE_FREQ, z * TD_CARVE_FREQ, 0, 0, 0);
}

f32 TerrainDensity_SDF(f32 x, f32 y, f32 z)
{
    return (y - TerrainDensity_Height(x, z)) + TerrainDensity_Carve(x, y, z);
}

void TerrainDensity_GetYRange(f32* outMin, f32* outMax)
{
    // height range plus the carve amplitude, fbm/ridge outputs stay within ~[-1.1, 1.2]
    *outMin = TD_BASE_HEIGHT - TD_HILL_AMP * 1.2f - TD_CARVE_AMP - 2.0f;
    *outMax = TD_BASE_HEIGHT + TD_HILL_AMP * 1.2f + TD_RIDGE_AMP * 1.2f + TD_CARVE_AMP + 2.0f;
}

// quantize a signed distance in meters to s8. the scale is world fixed so a coarse
// sample bit-matches the fine sample at the same position (see TERRAIN_SDF_CLAMP)
static s8 TerrainDensity_Quantize(f32 sdf)
{
    f32 q = sdf * (127.0f / TERRAIN_SDF_CLAMP);
    if (q >  127.0f) q =  127.0f;
    if (q < -127.0f) q = -127.0f;
    return (s8)(q >= 0.0f ? q + 0.5f : q - 0.5f);
}

void TerrainDensity_SampleChunk(s32 cx, s32 cy, s32 cz, u32 lod, s8* out)
{
    const f32 voxel = TERRAIN_VOXEL_SIZE * (f32)(1 << lod);
    const f32 ox = (f32)cx * (TERRAIN_CHUNK_CELLS * voxel);
    const f32 oy = (f32)cy * (TERRAIN_CHUNK_CELLS * voxel);
    const f32 oz = (f32)cz * (TERRAIN_CHUNK_CELLS * voxel);

    // the heightfield part only depends on the column, evaluate it once per (x,z)
    f32 heights[TERRAIN_SAMPLES_AXIS * TERRAIN_SAMPLES_AXIS];
    for (s32 z = 0; z < TERRAIN_SAMPLES_AXIS; z++)
    for (s32 x = 0; x < TERRAIN_SAMPLES_AXIS; x++)
    {
        f32 wx = ox + (f32)(x - 1) * voxel;
        f32 wz = oz + (f32)(z - 1) * voxel;
        heights[z * TERRAIN_SAMPLES_AXIS + x] = TerrainDensity_Height(wx, wz);
    }

    s8* dst = out;
    for (s32 z = 0; z < TERRAIN_SAMPLES_AXIS; z++)
    for (s32 y = 0; y < TERRAIN_SAMPLES_AXIS; y++)
    for (s32 x = 0; x < TERRAIN_SAMPLES_AXIS; x++)
    {
        f32 wx = ox + (f32)(x - 1) * voxel;
        f32 wy = oy + (f32)(y - 1) * voxel;
        f32 wz = oz + (f32)(z - 1) * voxel;
        f32 sdf = wy - heights[z * TERRAIN_SAMPLES_AXIS + x];
        // the carve term only matters near the surface, skip the 3D noise when the
        // column distance alone saturates the s8 range
        if (sdf > -(TERRAIN_SDF_CLAMP + TD_CARVE_AMP) && sdf < (TERRAIN_SDF_CLAMP + TD_CARVE_AMP))
            sdf += TerrainDensity_Carve(wx, wy, wz);
        *dst++ = TerrainDensity_Quantize(sdf);
    }
}
