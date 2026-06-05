#include "PBR.hlsl"
#include "Bitpack.hlsl"
#include "CommonStructs.hlsl"

cbuffer DeferredLightParams : register(b0, space3)
{
    uint2 outputSize;
    float2 padding0;
    float4x4 invViewProj;
    float4 cameraPosition;
};

StructuredBuffer<LightDrawInfo> VS_DrawInfos : register(t0);

Texture2D<uint>   TangentFrameTexture    : register(t0, space2);
Texture2D<float4> AlbedoMetallicTexture  : register(t1, space2);
Texture2D<float2> ShadowRoughnessTexture : register(t2, space2);
Texture2D<float>  DepthTexture           : register(t3, space2);
Texture2D<float>  AmbientOcclusion       : register(t4, space2);
SamplerState      Sampler                : register(s0, space2);
StructuredBuffer<LightGPU> PS_Lights : register(t5, space2);
StructuredBuffer<LightDrawInfo> PS_DrawInfos : register(t6, space2);

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
    uint drawIndex  : TEXCOORD1;
};

float3 ReconstructWorldPosition(float2 uv, float depth)
{
    float4 clip = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, depth, 1.0f);
    float4 world = mul(invViewProj, clip);
    return world.xyz / max(abs(world.w), 0.00001f);
}

VSOutput vert(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID)
{
    LightDrawInfo info = VS_DrawInfos[instanceID];
    float2 corners[6] = {
        float2(info.uvRect.x, info.uvRect.y),
        float2(info.uvRect.z, info.uvRect.y),
        float2(info.uvRect.x, info.uvRect.w),
        float2(info.uvRect.x, info.uvRect.w),
        float2(info.uvRect.z, info.uvRect.y),
        float2(info.uvRect.z, info.uvRect.w)
    };

    VSOutput output;
    output.uv = corners[vertexID];
    output.position = float4(output.uv.x * 2.0f - 1.0f, 1.0f - output.uv.y * 2.0f, 0.0f, 1.0f);
    output.drawIndex = instanceID;
    return output;
}

float3 ApplyLocalLight(float3 albedo, float3 normal, float3 viewDir, float metallic, float perceptualRoughness, LightGPU light, float3 worldPos, float ao)
{
    float3 lightVector = light.positionRadius.xyz - worldPos;
    float distanceSq = dot(lightVector, lightVector);
    float radius = max(light.positionRadius.w, 0.001f);
    if (distanceSq >= radius * radius)
        discard;

    float distanceToLight = sqrt(max(distanceSq, 0.00001f));
    float3 lightDir = lightVector / distanceToLight;
    float attenuation = saturate(1.0f - distanceToLight / radius);
    attenuation *= attenuation;

    if (light.type == LIGHT_TYPE_SPOT)
    {
        float coneCos = light.directionCone.w;
        float spotCos = dot(normalize(-light.directionCone.xyz), lightDir);
        float spot = saturate((spotCos - coneCos) / max(1.0f - coneCos, 0.0001f));
        attenuation *= spot * spot;
    }

    float3 radiance = light.colorIntensity.rgb * light.colorIntensity.w * attenuation;
    return ApplyPBRLight(albedo, normal, viewDir, metallic, perceptualRoughness, radiance, lightDir) * saturate(ao);
}

float4 frag(VSOutput input) : SV_Target0
{
    uint2 pixel = uint2(input.position.xy);
    if (pixel.x >= outputSize.x || pixel.y >= outputSize.y)
        discard;

    float depth = DepthTexture.Load(int3(pixel, 0));
    if (depth >= 0.9999f)
        discard;

    uint tangentFrame = TangentFrameTexture.Load(int3(pixel, 0));
    f16_3 packedNormal;
    f16_3 packedTangent;
    UnpackNormalTangent(tangentFrame, packedNormal, packedTangent);
    float3 normal = normalize(float3(packedNormal));

    float4 albedoMetallic = AlbedoMetallicTexture.Load(int3(pixel, 0));
    float2 shadowRoughness = ShadowRoughnessTexture.Load(int3(pixel, 0));
    float ao = AmbientOcclusion.SampleLevel(Sampler, input.uv, 0.0f);
    float3 worldPos = ReconstructWorldPosition(input.uv, depth);
    float3 viewDir = cameraPosition.xyz - worldPos;

    LightDrawInfo info = PS_DrawInfos[input.drawIndex];
    LightGPU light = PS_Lights[info.lightIndex];
    float3 color = ApplyLocalLight(albedoMetallic.rgb, normal, viewDir, albedoMetallic.a, shadowRoughness.g, light, worldPos, ao);
    return float4(color, 1.0f);
}
