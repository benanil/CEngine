cbuffer TonemapParams : register(b0, space2)
{
    uint2 outputSize;
    float exposure;
    float gamma;
    float2 sunPos;
    float godRayIntensity;
    float time;
    float cloudTime;
    float godRaySamples;
    float2 padding;
    float4x4 invViewProj;
    float4 cameraPosition;
    float4 sunDirection;
};

Texture2D<float4> SourceTexture : register(t0, space0);
Texture2D<float> DepthTexture   : register(t1, space0);
Texture2DArray<float> Noise3DTexture : register(t2, space0);
SamplerState SourceSampler      : register(s0, space0);
SamplerState DepthSampler       : register(s1, space0);
SamplerState NoiseSampler       : register(s2, space0);
[[vk::image_format("rgba8")]] RWTexture2D<float4> OutputTexture : register(u0, space1);

#include "Sky.hlsl" // ComputeSky(float3 rd)
#include "GodRays.hlsl" // ComputeGodRays(float2 uv) 

float3 MulMat3(float3 row0, float3 row1, float3 row2, float3 v)
{
    return float3(dot(row0, v), dot(row1, v), dot(row2, v));
}

float3 AgXDefaultContrastApprox(float3 x)
{
    float3 x2 = x * x;
    float3 x4 = x2 * x2;
    return 15.5f * x4 * x2
         - 40.14f * x4 * x
         + 31.96f * x4
         - 6.868f * x2 * x
         + 0.4298f * x2
         + 0.1191f * x
         - 0.00232f;
}

float3 TonemapACES(float3 color)
{
    float3 v = MulMat3(
        float3(0.59719f, 0.35458f, 0.04823f),
        float3(0.07600f, 0.90834f, 0.01566f),
        float3(0.02840f, 0.13383f, 0.83777f),
        color);

    float3 a = v * (v + 0.0245786f) - 0.000090537f;
    float3 b = v * (0.983729f * v + 0.4329510f) + 0.238081f;
    v = a / b;

    v = MulMat3(
        float3(1.60475f, -0.53108f, -0.07367f),
        float3(-0.10208f, 1.10813f, -0.00605f),
        float3(-0.00327f, -0.07276f, 1.07602f),
        v);

    return saturate(v);
}

float Vignette(float2 uv)
{
    uv *=  1.0 - uv.yx;   //vec2(1.0)- uv.yx; -> 1.-u.yx; Thanks FabriceNeyret !
    float vig = uv.x * uv.y * 15.0; // multiply with sth for intensity
    return pow(vig, 0.2); // change pow for modifying the extend of the  vignette
}

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= outputSize.x || tid.y >= outputSize.y) return;

    float2 uv = (float2(tid.xy) + 0.5f) / float2(outputSize);
    float3 color = SourceTexture.SampleLevel(SourceSampler, uv, 0.0f).rgb;
    float depth = DepthTexture.SampleLevel(DepthSampler, uv, 0.0f);
    if (depth >= 0.9999f)
    {
        color = ComputeSky(SkyRayDirection(uv));
    }
    float godRays = ComputeGodRays(uv);
    color += godRays * float3(1.35f, 1.08f, 0.72f);
    color = TonemapACES(color * exposure);
    color = pow(color, 1.0f / max(gamma, 0.001f));
    color *= max(Vignette(uv), 0.05);
    OutputTexture[tid.xy] = float4(color, 1.0f);
}
