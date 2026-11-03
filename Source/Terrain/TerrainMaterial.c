#include "TerrainInternal.h"
#include "Include/Terrain.h"
#include "Math/Color.h"

#define T_GRASS_LAYER 0u
#define T_DIRT_LAYER  1u
#define T_ROCK_LAYER  2u
#define T_SAND_LAYER  3u
#define T_SAND_TOP    2.0f
#define T_SAND_FADE   2.0f
#define T_GRASS_START 3.0f
#define T_GRASS_FADE  8.0f

static void CalculateProceduralMaterial(Vec3W worldPos, Vec3W normal, v128f weights[4], v128f seaLevel)
{
    v128f slope = normal.y;
    v128f steepRock = Vec1Minus(VecClamp01(VecMulf(VecSubf(slope, 0.55f), 4.0f)));
    v128f highBlend = VecClamp01(VecMulf(VecSubf(worldPos.y, 34.0f), 0.08f));
    v128f rockBlend = VecMax(steepRock, highBlend);
    v128f grassHeight = VecClamp01(VecMulf(VecSubf(VecSub(worldPos.y, seaLevel), T_GRASS_START), 1.0f / T_GRASS_FADE));
    v128f grassAmount = VecMul(Vec1Minus(rockBlend), grassHeight);
    
    M44TransposeV(
        VecMul(grassAmount, Vec1Minus(rockBlend)),
        VecMul(Vec1Minus(grassAmount), Vec1Minus(rockBlend)),
        rockBlend,
        VecZero(),
        weights
    );
}

static MaterialBlendW SelectDominantLayers(const v128f weights[4])
{
    MaterialBlendW o = {};
    for (s32 i = 0; i < 4; i++)
    {
        u32 primary = T_GRASS_LAYER;
        u32 secondary = T_DIRT_LAYER;
        const f32* w = (const f32*)&weights[i];
        for (u32 layer = T_DIRT_LAYER; layer <= T_ROCK_LAYER; layer++)
        {
            if (w[layer] > w[primary])
            {
                secondary = primary;
                primary = layer;
            }
            else if (layer != primary && w[layer] > w[secondary])
            {
                secondary = layer;
            }
        }
        f32 total = w[primary] + w[secondary];
        u32 primaryWeight = (u32)Clampf32(w[primary] / Maxf32(total, 1.0e-4f) * 255.0f + 0.5f, 0.0f, 255.0f);
        VeciSetN(&o.primary, i, primary);
        VeciSetN(&o.secondary, i, secondary);
        VeciSetN(&o.primaryWeight, i, primaryWeight);
    }
    return o; 
}

static v128u ApplyMaterialOverrides(Vec3W worldPosSoa, v128f seaLevel, MaterialBlendW* blend)
{
    v128f sandBlend = VecSub(VecAddf(seaLevel, T_SAND_TOP), worldPosSoa.y);
    sandBlend = VecClamp01(VecMulf(sandBlend, (1.0f / T_SAND_FADE)));
    // mat4x4 worldPos = M44TransposeV(worldPosSoa.x, worldPosSoa.y, worldPosSoa.z, VecZero());
    v128u isSand     = VecF32ToU32(VecCmpGt(sandBlend, VecSet1(0.002f)));
    blend->primary   = VeciBlend(blend->primary  , VeciSet1(T_SAND_LAYER), isSand); 
    blend->secondary = VeciBlend(blend->secondary, VeciSet1(T_DIRT_LAYER), isSand);
    blend->primaryWeight = VeciBlend(blend->primaryWeight, VecF32ToU32(VecMulf(sandBlend, 255.0f)), isSand);
    
    return VeciOr(VeciOr(blend->primary, VeciSll32(blend->secondary, 8)), VeciSll32(blend->primaryWeight, 16));
    // TerrainEdit_MaterialWeights(Vec3Get(worldPos.r[i]), matIndex, matWeight);
}

v128u VCALL tTerrainMaterial(Vec3W worldPos, Vec3W normal, v128f oreNoise, v128f seaLevelNoise)
{
    const TerrainGenParams* params = Terrain_GetGenParams();
    v128f weights[4];
    v128f seaLevel = VecFmadd(seaLevelNoise, VecSet1(15.0f), VecSet1(params->seaLevel)); 
    CalculateProceduralMaterial(worldPos, normal, weights, seaLevel);
    MaterialBlendW material = SelectDominantLayers(weights);
    return ApplyMaterialOverrides(worldPos, seaLevel, &material);
}
