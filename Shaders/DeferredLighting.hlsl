#include "PBR.hlsl"
#include "Bitpack.hlsl"

cbuffer DeferredParams : register(b0, space2)
{
    uint2 outputSize;
    float2 padding0;
    float4x4 invViewProj;
    float4 cameraPosition;
    float4 sunDirection;
};

Texture2D<uint>   TangentFrameTexture    : register(t0, space0);
Texture2D<float4> AlbedoMetallicTexture  : register(t1, space0);
Texture2D<float2> ShadowRoughnessTexture : register(t2, space0);
Texture2D<float>  DepthTexture           : register(t3, space0);
Texture2D<float>  AmbientOcclusion       : register(t4, space0);
SamplerState      Sampler                : register(s0, space0);
[[vk::image_format("rgba16f")]] RWTexture2D<float4> OutputTexture : register(u0, space1);

float3 ReconstructWorldPosition(float2 uv, float depth)
{
    float4 clip = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, depth, 1.0f);
    float4 world = mul(invViewProj, clip);
    return world.xyz / max(abs(world.w), 0.00001f);
}

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= outputSize.x || tid.y >= outputSize.y) return;

    uint2 pixel = tid.xy;
    float2 uv = (float2(pixel) + 0.5f) / float2(outputSize);
    float depth = DepthTexture.Load(int3(pixel, 0));
    if (depth > 0.9992f)
    {
        OutputTexture[pixel] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }

    uint tangentFrame = TangentFrameTexture.Load(int3(pixel, 0));
    f16_3 packedNormal;
    f16_3 packedTangent;
    UnpackNormalTangent(tangentFrame, packedNormal, packedTangent);

    float4 albedoMetallic = AlbedoMetallicTexture.Load(int3(pixel, 0));
    float2 shadowRoughness = ShadowRoughnessTexture.Load(int3(pixel, 0));
    float ao = AmbientOcclusion.SampleLevel(Sampler, uv, 0.0f);

    float3 worldPos = ReconstructWorldPosition(uv, depth);
    float3 viewDir = cameraPosition.xyz - worldPos;
    float3 color = ApplyPBR(albedoMetallic.rgb,
                            float3(packedNormal),
                            viewDir,
                            albedoMetallic.a,
                            shadowRoughness.g,
                            shadowRoughness.r,
                            ao,
                            sunDirection.xyz);
    OutputTexture[pixel] = float4(color, 1.0f);
}
