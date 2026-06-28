#define BLOOM_EPSILON 1e-5f

cbuffer BloomDownsampleParams : register(b0, space2)
{
    uint2 outputSize;
    float2 sourceTexelSize;
    uint sourceMip;
    uint prefilter;
    float threshold;
    float knee;
    float clampValue;
    float padding0;
};

Texture2D<float4> SourceTexture : register(t0, space0);
SamplerState SourceSampler : register(s0, space0);
[[vk::image_format("rgba16f")]] RWTexture2D<float4> OutputTexture : register(u0, space1);

float3 SafeHDR(float3 color)
{
    return min(max(color, 0.0f), clampValue);
}

float3 QuadraticThreshold(float3 color)
{
    float brightness = max(max(color.r, color.g), color.b);
    float soft = brightness - threshold + knee;
    soft = clamp(soft, 0.0f, knee * 2.0f);
    soft = soft * soft / max(knee * 4.0f, BLOOM_EPSILON);
    float contribution = max(soft, brightness - threshold) / max(brightness, BLOOM_EPSILON);
    return color * saturate(contribution);
}

float3 SampleSource(float2 uv, float2 offset)
{
    return SourceTexture.SampleLevel(SourceSampler, uv + offset * sourceTexelSize, float(sourceMip)).rgb;
}

float3 DownsampleBox13Tap(float2 uv)
{
    float3 a = SampleSource(uv, float2(-2.0f,  2.0f));
    float3 b = SampleSource(uv, float2( 0.0f,  2.0f));
    float3 c = SampleSource(uv, float2( 2.0f,  2.0f));
    float3 d = SampleSource(uv, float2(-2.0f,  0.0f));
    float3 e = SampleSource(uv, float2( 0.0f,  0.0f));
    float3 f = SampleSource(uv, float2( 2.0f,  0.0f));
    float3 g = SampleSource(uv, float2(-2.0f, -2.0f));
    float3 h = SampleSource(uv, float2( 0.0f, -2.0f));
    float3 i = SampleSource(uv, float2( 2.0f, -2.0f));
    float3 j = SampleSource(uv, float2(-1.0f,  1.0f));
    float3 k = SampleSource(uv, float2( 1.0f,  1.0f));
    float3 l = SampleSource(uv, float2(-1.0f, -1.0f));
    float3 m = SampleSource(uv, float2( 1.0f, -1.0f));

    return e * 0.125f
         + (a + c + g + i) * 0.03125f
         + (b + d + f + h) * 0.0625f
         + (j + k + l + m) * 0.125f;
}

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= outputSize.x || tid.y >= outputSize.y) return;

    float2 uv = (float2(tid.xy) + 0.5f) / float2(outputSize);
    float3 color = SafeHDR(DownsampleBox13Tap(uv));
    if (prefilter != 0u)
        color = QuadraticThreshold(color);

    OutputTexture[tid.xy] = float4(color, 1.0f);
}
