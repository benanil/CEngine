#include "MLAACommon.hlsl"

Texture2D<float4> SourceTexture : register(t0, space0);
Texture2D<uint> EdgeMaskTexture : register(t1, space0);
Texture2D<uint2> EdgeCountTexture : register(t2, space0);
[[vk::image_format("rgba8")]] RWTexture2D<float4> OutputTexture : register(u0, space1);

// OPTIMIZATION: Bypassed SamplerState entirely.
// Using explicit texel loads avoids UV math precision overhead and sampler thrashing.
float4 LoadSource(int2 p)
{
    return SourceTexture.Load(int3(ClampPixel(p), 0));
}

float LoadLuma(int2 p)
{
    return SceneLuma(LoadSource(p).rgb);
}

// Inline helper to quickly get shape flags without deep functional nested calls
uint DetermineShapeFlags(int2 pos, int2 ortho, int negCount, int posCount)
{
    uint shape = 0x00u;
    // Evaluate boundaries sequentially. 
    // Sub-expressions are calculated and immediately compared to reduce active VGPR lifetimes.
    float lumaNeg0 = LoadLuma(pos - ortho * negCount);
    float lumaNeg1 = LoadLuma(pos - ortho * (negCount + 1));
    if (CompareLuma(lumaNeg0, lumaNeg1)) shape |= 0x01u; // risingZ

    float lumaPos0 = LoadLuma(pos + ortho * posCount);
    float lumaPos1 = LoadLuma(pos + ortho * (posCount + 1));
    if (CompareLuma(lumaPos0, lumaPos1)) shape |= 0x02u; // fallingZ
    return shape;
}

void BlendColor(uint count, int2 pos, int2 dir, int2 ortho, bool inverse, inout float3 colorSq)
{
    // Fast Bitwise Check: Early out before diving into decode logic.
    // Masks MLAA_STOP_BIT_POSITION + MLAA_POS_COUNT_SHIFT and NEG_COUNT_SHIFT
    const uint stopMask = (1u << (MLAA_STOP_BIT_POSITION + MLAA_POS_COUNT_SHIFT)) | 
        (1u << (MLAA_STOP_BIT_POSITION + MLAA_NEG_COUNT_SHIFT));
                          
    if ((count & stopMask) == 0u) return;

    uint negCount = DecodeCountNoStopBit(count, MLAA_NEG_COUNT_SHIFT);
    uint posCount = DecodeCountNoStopBit(count, MLAA_POS_COUNT_SHIFT);
    
    // Sample target adjacent color early
    float4 adjacent = LoadSource(pos + dir);
    float3 adjacentSq = adjacent.rgb * adjacent.rgb;

    if ((negCount + posCount) == 0u)
    {
        // 1.0f / 8.0f is a compile-time constant (0.125f)
        colorSq = lerp(colorSq, adjacentSq, 0.125f);
        return;
    }

    if (!IsBitSet(count, MLAA_STOP_BIT_POSITION + MLAA_POS_COUNT_SHIFT)) posCount = MLAA_MAX_EDGE_LENGTH + 1u;
    if (!IsBitSet(count, MLAA_STOP_BIT_POSITION + MLAA_NEG_COUNT_SHIFT)) negCount = MLAA_MAX_EDGE_LENGTH + 1u;

    float length = float(negCount + posCount + 1u);
    float midPoint = length * 0.5f;
    float distance = float(negCount);

    uint shape = DetermineShapeFlags(pos, ortho, int(negCount), int(posCount));

    // Flattened bit conditions to maximize scalar execution on modern GPUs
    bool shapeFalling = (shape == 0x02u); // fallingZ
    bool shapeRising  = (shape == 0x01u); // risingZ
    
    bool shouldBlend = false;
    if (inverse)
    {
        shouldBlend = (shapeFalling && (distance <= midPoint)) ||
            (shapeRising  && (distance >= midPoint)) ||
            (shape == 0x00u); // upperU
    }
    else
    {
        shouldBlend = (shapeFalling && (distance >= midPoint)) ||
            (shapeRising  && (distance <= midPoint)) ||
            (shape == 0x03u); // lowerU
    }

    if (shouldBlend)
    {
        // Optimized area equation: simplified algebraic form minimizes operations
        float invLength = 1.0f / length;
        float h0 = abs((length - distance) * invLength - 0.5f);
        float h1 = abs((length - distance - 1.0f) * invLength - 0.5f);
        float area = 0.5f * (h0 + h1);
        
        colorSq = lerp(colorSq, adjacentSq, area);
    }
}

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= outputSize.x || tid.y >= outputSize.y) return;

    int2 p = int2(tid.xy);
    
    // Debug optimization path
    if (showEdges != 0u)
    {
        uint mask = EdgeMaskTexture.Load(int3(p, 0));
        OutputTexture[tid.xy] = mask != 0u ? float4(1.0f, 0.0f, 0.0f, 1.0f) : LoadSource(p);
        return;
    }

    // Coalesce loading: Fetch target pixels and offset counts efficiently
    uint2 counts = EdgeCountTexture.Load(int3(p, 0));
    uint hcount = counts.x;
    uint vcount = counts.y;
    
    uint hcountUp = EdgeCountTexture.Load(int3(ClampPixel(p + int2(0, 1)), 0)).x;
    uint vcountRight = EdgeCountTexture.Load(int3(ClampPixel(p + int2(-1, 0)), 0)).y;

    // Vector Early Out: If nothing needs blending in this thread, skip entirely
    if ((hcount | vcount | hcountUp | vcountRight) == 0u)
    {
        OutputTexture[tid.xy] = float4(LoadSource(p).rgb, 1.0f);
        return;
    }

    float4 sourceColor = LoadSource(p);
    
    // OPTIMIZATION: Accumulate everything in linear-squared space!
    // This allows us to defer expensive sqrt() conversions to a singular call at the end.
    float3 colorSq = sourceColor.rgb * sourceColor.rgb;

    // Branching hints applied to prevent lockstep execution across empty paths
    if (hcount != 0u)       BlendColor(hcount, p, int2(0, -1), int2(1, 0), false, colorSq);
    if (hcountUp != 0u)     BlendColor(hcountUp, p + int2(0, 1), int2(0, 1), int2(1, 0), true, colorSq);
    if (vcount != 0u)       BlendColor(vcount, p, int2(1, 0), int2(0, -1), false, colorSq);
    if (vcountRight != 0u)  BlendColor(vcountRight, p + int2(-1, 0), int2(-1, 0), int2(0, -1), true, colorSq);

    // Single final conversion back to gamma space
    OutputTexture[tid.xy] = float4(sqrt(colorSq), 1.0f);
}