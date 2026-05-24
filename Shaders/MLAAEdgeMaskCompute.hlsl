#include "MLAACommon.hlsl"

Texture2D<float4> SourceTexture : register(t0, space0);
SamplerState SourceSampler : register(s0, space0);
[[vk::image_format("r32ui")]] RWTexture2D<uint> EdgeMaskTexture : register(u0, space1);

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= outputSize.x || tid.y >= outputSize.y) return;

    int2 p = int2(tid.xy);
    float2 invSize = 1.0f / float2(outputSize);
    float center = SceneLuma(SourceTexture.SampleLevel(SourceSampler, (float2(p) + 0.5f) * invSize, 0.0f).rgb);
    float up = SceneLuma(SourceTexture.SampleLevel(SourceSampler, (float2(ClampPixel(p + int2(0, -1))) + 0.5f) * invSize, 0.0f).rgb);
    float right = SceneLuma(SourceTexture.SampleLevel(SourceSampler, (float2(ClampPixel(p + int2(1, 0))) + 0.5f) * invSize, 0.0f).rgb);

    uint mask = 0u;
    if (CompareLuma(center, up)) mask |= MLAA_UPPER_MASK;
    if (CompareLuma(center, right)) mask |= MLAA_RIGHT_MASK;
    EdgeMaskTexture[tid.xy] = mask;
}
