#include "../../Include/RenderLimits.h"

#define TILE_HEAT_DEBUG 0

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
    float bloomIntensity;
    uint bloomEnabled;
    uint tilesX;
    uint tileHeatEnabled;
    uint2 bloomPadding;
    float4x4 invViewProj;
    float4 cameraPosition;
    float4 sunDirection;
    float4 fogColorDensity;   // rgb = fog color, w = overall density scale
    float4 fogParams;         // x = base height, y = height falloff, z = sun scatter amt, w = enable (0/1)
};

Texture2D<float4> SourceTexture : register(t0, space0);
Texture2D<float> DepthTexture   : register(t1, space0);
Texture2DArray<float> Noise3DTexture : register(t2, space0);
Texture2D<float4> BloomTexture : register(t3, space0);
StructuredBuffer<uint2> sLightGrid : register(t4, space0);
SamplerState SourceSampler      : register(s0, space0);
SamplerState DepthSampler       : register(s1, space0);
SamplerState NoiseSampler       : register(s2, space0);
SamplerState BloomSampler       : register(s3, space0);
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

float3 ReconstructWorldPosition(float2 uv, float depth)
{
    float4 clip = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, depth, 1.0f);
    float4 world = mul(invViewProj, clip);
    return world.xyz / max(abs(world.w), 0.00001f);
}

// warm tint applied to fog when looking toward the sun
float3 FogSunTint(float3 fogCol, float3 rayDir)
{
    // sunDirection points toward the sun (same convention as SunDisk in Sky.hlsl)
    float sun = saturate(dot(rayDir, normalize(sunDirection.xyz)));
    return lerp(fogCol, fogCol * float3(1.35f, 1.08f, 0.72f), pow(sun, 3.0f) * fogParams.z);
}

// analytic exponential height fog integrated along the view ray
float3 ApplyHeightFog(float3 color, float3 rayDir, float rayLen)
{
    // slider density (0..1) is a coarse knob; the geometry extinction coefficient
    // it maps to must be tiny because the integral divides by falloff and accumulates
    // over hundreds of meters. 0.01 keeps even high slider values gentle.
    float density = fogColorDensity.w * 0.01f;
    float baseH   = fogParams.x;
    float falloff = max(fogParams.y, 1e-4f);
    float camRel  = cameraPosition.y - baseH;
    float fogInt;
    if (abs(rayDir.y) < 1e-4f)
    {
        // near-horizontal ray: constant density slab
        fogInt = density * rayLen * exp(-falloff * camRel);
    }
    else
    {
        fogInt = density * exp(-falloff * camRel)
               * (1.0f - exp(-rayLen * rayDir.y * falloff)) / (rayDir.y * falloff);
    }
    float fogAmt = saturate(fogInt);
    float3 fogCol = FogSunTint(fogColorDensity.rgb, rayDir);
    return lerp(color, fogCol, fogAmt);
}

float Vignette(float2 uv)
{
    uv *=  1.0 - uv.yx;   //vec2(1.0)- uv.yx; -> 1.-u.yx; Thanks FabriceNeyret !
    float vig = uv.x * uv.y * 15.0; // multiply with sth for intensity
    return pow(vig, 0.2); // change pow for modifying the extend of the  vignette
}

float3 ApplyTileHeatDebug(float3 color, uint2 pixel)
{
#if TILE_HEAT_DEBUG
    if (tileHeatEnabled == 0u || tilesX == 0u)
        return color;

    uint2 tile = pixel / FORWARD_TILE_SIZE;
    uint lightCount = sLightGrid[tile.y * tilesX + tile.x].y;
    if (lightCount == 0u)
        return color;

    float heat = sqrt(saturate(float(lightCount) / float(MAX_LIGHTS_PER_TILE)));
    color = lerp(color, float3(1.0f, 0.0f, 0.0f), heat);
#endif
    return color;
}

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= outputSize.x || tid.y >= outputSize.y) return;

    float2 uv = (float2(tid.xy) + 0.5f) / float2(outputSize);
    float3 color = SourceTexture.SampleLevel(SourceSampler, uv, 0.0f).rgb;
    float depth = DepthTexture.SampleLevel(DepthSampler, uv, 0.0f);
    if (bloomEnabled != 0u)
        color += BloomTexture.SampleLevel(BloomSampler, uv, 0.0f).rgb * bloomIntensity;

    bool fogEnabled = fogParams.w > 0.5f;
    if (depth >= 0.9999f)
    {
        float3 skyDir = SkyRayDirection(uv);
        color = ComputeSky(skyDir);
        if (fogEnabled)
        {
            // fade the sky toward fog color near the horizon
            float horizon = saturate(1.0f - skyDir.y); // 1 at/below horizon, 0 at zenith
            horizon *= horizon;                        // concentrate the haze near the horizon
            float3 fogCol = FogSunTint(fogColorDensity.rgb, skyDir);
            color = lerp(color, fogCol, saturate(horizon * fogColorDensity.w));
        }
    }
    else if (fogEnabled)
    {
        float3 worldPos = ReconstructWorldPosition(uv, depth);
        float3 rayDir = worldPos - cameraPosition.xyz;
        float rayLen = length(rayDir);
        rayDir /= max(rayLen, 1e-5f);
        color = ApplyHeightFog(color, rayDir, rayLen);
    }
    float godRays = ComputeGodRays(uv);
    color += godRays * float3(1.35f, 1.08f, 0.72f);
    color = TonemapACES(color * exposure);
    color = pow(color, 1.0f / max(gamma, 0.001f));
    color *= max(Vignette(uv), 0.05);
    color = ApplyTileHeatDebug(color, tid.xy);
    OutputTexture[tid.xy] = float4(color, 1.0f);
}
