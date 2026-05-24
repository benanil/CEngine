#include "MLAACommon.hlsl"

Texture2D<float4> SourceTexture : register(t0, space0);
SamplerState SourceSampler : register(s0, space0);
Texture2D<uint> EdgeMaskTexture : register(t1, space0);
Texture2D<uint2> EdgeCountTexture : register(t2, space0);
[[vk::image_format("rgba8")]] RWTexture2D<float4> OutputTexture : register(u0, space1);

float4 LoadSource(int2 p)
{
    int2 cp = ClampPixel(p);
    return SourceTexture.SampleLevel(SourceSampler, (float2(cp) + 0.5f) / float2(outputSize), 0.0f);
}

float LoadLuma(int2 p)
{
    return SceneLuma(LoadSource(p).rgb);
}

void BlendColor(uint count, int2 pos, int2 dir, int2 ortho, bool inverse, inout float4 color)
{
    if (!IsBitSet(count, MLAA_STOP_BIT_POSITION + MLAA_POS_COUNT_SHIFT) &&
        !IsBitSet(count, MLAA_STOP_BIT_POSITION + MLAA_NEG_COUNT_SHIFT))
    {
        return;
    }

    uint negCount = DecodeCountNoStopBit(count, MLAA_NEG_COUNT_SHIFT);
    uint posCount = DecodeCountNoStopBit(count, MLAA_POS_COUNT_SHIFT);
    float4 adjacent = LoadSource(pos + dir);

    if ((negCount + posCount) == 0u)
    {
        float weight = 1.0f / 8.0f;
        color.rgb = sqrt(lerp(color.rgb * color.rgb, adjacent.rgb * adjacent.rgb, weight));
        return;
    }

    if (!IsBitSet(count, MLAA_STOP_BIT_POSITION + MLAA_POS_COUNT_SHIFT)) posCount = MLAA_MAX_EDGE_LENGTH + 1u;
    if (!IsBitSet(count, MLAA_STOP_BIT_POSITION + MLAA_NEG_COUNT_SHIFT)) negCount = MLAA_MAX_EDGE_LENGTH + 1u;

    float length = float(negCount + posCount + 1u);
    float midPoint = length * 0.5f;
    float distance = float(negCount);

    static const uint upperU = 0x00u;
    static const uint risingZ = 0x01u;
    static const uint fallingZ = 0x02u;
    static const uint lowerU = 0x03u;

    uint shape = 0x00u;
    if (CompareLuma(LoadLuma(pos - ortho * int(negCount)), LoadLuma(pos - ortho * int(negCount + 1u)))) shape |= risingZ;
    if (CompareLuma(LoadLuma(pos + ortho * int(posCount)), LoadLuma(pos + ortho * int(posCount + 1u)))) shape |= fallingZ;

    bool shouldBlend =
        (inverse && (((shape == fallingZ) && (float(negCount) <= midPoint)) ||
                     ((shape == risingZ) && (float(negCount) >= midPoint)) ||
                     (shape == upperU))) ||
        (!inverse && (((shape == fallingZ) && (float(negCount) >= midPoint)) ||
                      ((shape == risingZ) && (float(negCount) <= midPoint)) ||
                      (shape == lowerU)));

    if (shouldBlend)
    {
        float h0 = abs((length - distance) / length - 0.5f);
        float h1 = abs((length - distance - 1.0f) / length - 0.5f);
        float area = 0.5f * (h0 + h1);
        color.rgb = sqrt(lerp(color.rgb * color.rgb, adjacent.rgb * adjacent.rgb, area));
    }
}

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= outputSize.x || tid.y >= outputSize.y) return;

    int2 p = int2(tid.xy);
    uint2 counts = EdgeCountTexture.Load(int3(p, 0));
    uint hcount = counts.x;
    uint vcount = counts.y;
    uint hcountUp = EdgeCountTexture.Load(int3(ClampPixel(p + int2(0, 1)), 0)).x;
    uint vcountRight = EdgeCountTexture.Load(int3(ClampPixel(p + int2(-1, 0)), 0)).y;

    float4 color = LoadSource(p);
    if (showEdges != 0u)
    {
        uint mask = EdgeMaskTexture.Load(int3(p, 0));
        OutputTexture[tid.xy] = mask != 0u ? float4(1.0f, 0.0f, 0.0f, 1.0f) : color;
        return;
    }

    if (hcount != 0u) BlendColor(hcount, p, int2(0, -1), int2(1, 0), false, color);
    if (hcountUp != 0u) BlendColor(hcountUp, p + int2(0, 1), int2(0, 1), int2(1, 0), true, color);
    if (vcount != 0u) BlendColor(vcount, p, int2(1, 0), int2(0, -1), false, color);
    if (vcountRight != 0u) BlendColor(vcountRight, p + int2(-1, 0), int2(-1, 0), int2(0, -1), true, color);

    color.a = 1.0f;
    OutputTexture[tid.xy] = color;
}
