#include "PBR.hlsl"
#include "Bitpack.hlsl"
#include "CommonStructs.hlsl"
#include "Shadow/Shadow.hlsl"

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
Texture2DArray<float> PointShadowTexture : register(t5, space2);
Texture2DArray<float> SpotShadowTexture  : register(t6, space2);
SamplerState      Sampler                : register(s0, space2);
SamplerState      PointShadowSampler     : register(s5, space2);
SamplerState      SpotShadowSampler      : register(s6, space2);

StructuredBuffer<LightGPU>      PS_Lights    : register(t7, space2);
StructuredBuffer<LightDrawInfo> PS_DrawInfos : register(t8, space2);
StructuredBuffer<PointShadowMatrix> PS_PointShadowMatrices : register(t9, space2);
StructuredBuffer<PointShadowMatrix> PS_SpotShadowMatrices  : register(t10, space2);

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
    output.position  = float4(output.uv.x * 2.0f - 1.0f, 1.0f - output.uv.y * 2.0f, 0.0f, 1.0f);
    output.drawIndex = instanceID;
    return output;
}

uint PointShadowFace(float3 lightToWorld)
{
    float3 a = abs(lightToWorld);
    if (a.x >= a.y && a.x >= a.z)
        return lightToWorld.x >= 0.0f ? 0u : 1u;
    if (a.y >= a.z)
        return lightToWorld.y >= 0.0f ? 2u : 3u;
    return lightToWorld.z >= 0.0f ? 4u : 5u;
}

float SamplePointShadow(LightGPU light, float3 worldPos, float3 normal, float3 lightDir)
{
    if ((light.flags & LIGHT_FLAG_SHADOWED) == 0u || light.shadowIndex >= POINT_SHADOW_MAX_LIGHTS)
        return 1.0f;

    float3 lightToWorld = worldPos - light.positionRadius.xyz;
    uint face = PointShadowFace(lightToWorld);
    uint matrixIndex = light.shadowIndex * POINT_SHADOW_FACE_COUNT + face;
    float4 shadowPos = MulPointShadowSide(PS_PointShadowMatrices[matrixIndex], float4(worldPos, 1.0f));
    float3 proj = shadowPos.xyz / max(abs(shadowPos.w), 0.00001f);
    float2 uv = proj.xy * float2(0.5f, -0.5f) + 0.5f;
    float depth = proj.z;
    if (shadowPos.w <= 0.0f || any(uv < 0.0f) || any(uv > 1.0f) || depth < 0.0f || depth > 1.0f)
        return 1.0f;

    uint width, height, layers;
    PointShadowTexture.GetDimensions(width, height, layers);
    if (light.shadowIndex >= layers)
        return 1.0f;

    float ndotl = saturate(dot(normal, lightDir));
    float bias = max(0.0025f * (1.0f - ndotl), 0.0008f);
    uint faceWidth = max(width / POINT_SHADOW_FACE_COUNT, 1u);
    float2 texel = 1.0f / float2(faceWidth, max(height, 1u));
    float shadow = 0.0f;

    [unroll]
    for (int i = 0; i < 8; i++)
    {
        float2 sampleUV = clamp(uv + ShadowKernel[i] * texel * 1.5f, texel * 0.5f, 1.0f - texel * 0.5f);
        float2 atlasUV = float2((sampleUV.x + float(face)) / float(POINT_SHADOW_FACE_COUNT), sampleUV.y);
        float mapDepth = PointShadowTexture.SampleLevel(PointShadowSampler, float3(atlasUV, float(light.shadowIndex)), 0.0f);
        shadow += float(mapDepth >= depth - bias);
    }
    return max(shadow * 0.125f, 0.15f);
}

float SampleSpotShadow(LightGPU light, float3 worldPos, float3 normal, float3 lightDir)
{
    if ((light.flags & LIGHT_FLAG_SHADOWED) == 0u || light.shadowIndex >= SPOT_SHADOW_MAX_LIGHTS)
        return 1.0f;

    uint width, height, layers;
    SpotShadowTexture.GetDimensions(width, height, layers);
    if (light.shadowIndex >= layers)
        return 1.0f;

    float4 shadowPos = MulPointShadowSide(PS_SpotShadowMatrices[light.shadowIndex], float4(worldPos, 1.0f));
    float3 proj = shadowPos.xyz / max(abs(shadowPos.w), 0.00001f);
    float2 uv = proj.xy * float2(0.5f, -0.5f) + 0.5f;
    float depth = proj.z;
    if (shadowPos.w <= 0.0f || any(uv < 0.0f) || any(uv > 1.0f) || depth < 0.0f || depth > 1.0f)
        return 1.0f;

    float ndotl = saturate(dot(normal, lightDir));
    float bias = max(0.0015f * (1.0f - ndotl), 0.0005f);
    float2 texel = 1.0f / float2(max(width, 1u), max(height, 1u));
    float shadow = 0.0f;

    [unroll]
    for (int i = 0; i < 8; i++)
    {
        float2 sampleUV = uv + ShadowKernel[i] * texel * 1.25f;
        float mapDepth = SpotShadowTexture.SampleLevel(SpotShadowSampler, float3(sampleUV, float(light.shadowIndex)), 0.0f);
        shadow += float(mapDepth >= depth - bias);
    }
    return max(shadow * (1.0f / 8.0f), 0.15f);
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

    float shadow = light.type == LIGHT_TYPE_POINT ? SamplePointShadow(light, worldPos, normal, lightDir) :
                   (light.type == LIGHT_TYPE_SPOT ? SampleSpotShadow(light, worldPos, normal, lightDir) : 1.0f);
    float3 radiance = light.colorIntensity.rgb * light.colorIntensity.w * attenuation * shadow;
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

    float4 albedoMetallic  = AlbedoMetallicTexture.Load(int3(pixel, 0));
    float2 shadowRoughness = ShadowRoughnessTexture.Load(int3(pixel, 0));
    float ao = AmbientOcclusion.SampleLevel(Sampler, input.uv, 0.0f);
    float3 worldPos = ReconstructWorldPosition(input.uv, depth);
    float3 viewDir  = cameraPosition.xyz - worldPos;
    
    LightDrawInfo info  = PS_DrawInfos[input.drawIndex];
    LightGPU      light = PS_Lights[info.lightIndex];
    float3 color = ApplyLocalLight(albedoMetallic.rgb, normal, viewDir, albedoMetallic.a, shadowRoughness.g, light, worldPos, ao);
    return float4(color, 1.0f);
}
