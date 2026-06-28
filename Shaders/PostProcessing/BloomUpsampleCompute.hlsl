cbuffer BloomUpsampleParams : register(b0, space2)
{
    uint2 outputSize;
    float2 lowTexelSize;
    uint lowMip;
    uint highMip;
    float sampleScale;
    float padding0;
};

Texture2D<float4> LowTexture : register(t0, space0);
Texture2D<float4> HighTexture : register(t1, space0);
SamplerState LowSampler : register(s0, space0);
SamplerState HighSampler : register(s1, space0);
[[vk::image_format("rgba16f")]] RWTexture2D<float4> OutputTexture : register(u0, space1);

float3 SampleLow(float2 uv, float2 offset)
{
    return LowTexture.SampleLevel(LowSampler, uv + offset * lowTexelSize * sampleScale, float(lowMip)).rgb;
}

float3 UpsampleTent(float2 uv)
{
    float3 color = 0.0f;
    color += SampleLow(uv, float2(-1.0f,  1.0f)) * 1.0f;
    color += SampleLow(uv, float2( 0.0f,  1.0f)) * 2.0f;
    color += SampleLow(uv, float2( 1.0f,  1.0f)) * 1.0f;
    color += SampleLow(uv, float2(-1.0f,  0.0f)) * 2.0f;
    color += SampleLow(uv, float2( 0.0f,  0.0f)) * 4.0f;
    color += SampleLow(uv, float2( 1.0f,  0.0f)) * 2.0f;
    color += SampleLow(uv, float2(-1.0f, -1.0f)) * 1.0f;
    color += SampleLow(uv, float2( 0.0f, -1.0f)) * 2.0f;
    color += SampleLow(uv, float2( 1.0f, -1.0f)) * 1.0f;
    return color * 0.0625f;
}

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= outputSize.x || tid.y >= outputSize.y) return;

    float2 uv = (float2(tid.xy) + 0.5f) / float2(outputSize);
    float3 high = HighTexture.SampleLevel(HighSampler, uv, float(highMip)).rgb;
    float3 bloom = UpsampleTent(uv) + high;
    OutputTexture[tid.xy] = float4(bloom, 1.0f);
}
