#include "../Include/RenderLimits.h"
#include "CommonStructs.hlsl"
#include "TextureSampling.hlsl"
#include "PBR.hlsl"
#include "Bitpack.hlsl"
#include "Math.hlsl"
#include "Shadow.hlsl"

cbuffer vs_params : register(b0, space1)
{
    float4x4 uViewProj;
    float4x4 uLightViewProj[3];
    float4 uCameraPosition;
    float4 uCameraForward;
    float4 uCascadeSplits;
};

StructuredBuffer<Entity>         sEntities         : register(t0);
StructuredBuffer<PrimitiveGroup> sPrimitiveGroups  : register(t1);
StructuredBuffer<uint>           sDrawSparseIndices : register(t2);

struct VSInput
{
    float3   aPos          : POSITION0;
    uint     aTangentSpace : TANGENT0;
    f16_2_io aTexCoords    : TEXCOORD0;
};

struct VSOutput
{
    float4    position  : SV_Position;
    f16_2_io texCoords  : TEXCOORD0;
    f16_3_io normal     : NORMAL;
    f16_3_io tangent    : TANGENT0;
    f16_3_io bitangent  : TEXCOORD1;
    f16_3_io viewDir    : TEXCOORD2;
    float4   shadowPos0 : TEXCOORD3;
    float4   shadowPos1 : TEXCOORD4;
    float4   shadowPos2 : TEXCOORD5;
    float    viewDepth  : TEXCOORD6;
    nointerpolation uint cascadeIndex : TEXCOORD7;
    nointerpolation uint materialIndex : TEXCOORD8;
};

VSOutput vert(VSInput input, uint instanceID : SV_InstanceID, [[vk::builtin("DrawIndex")]] uint drawID : DRAWINDEX)
{
    PrimitiveGroup group = sPrimitiveGroups[drawID];
    uint denseIdx = sDrawSparseIndices[group.entityOffset + instanceID];
    Entity entity = sEntities[denseIdx];

    f16_4 insRot   = normalize(UnpackRGBA16Snorm(entity.rotation[0], entity.rotation[1]));
    f16_3 insScale = UnpackVec3XY11Z10Unorm(entity.scale) * f16(10.0);

    f16_3x3 tbn;
    UnpackNormalTangent(input.aTangentSpace, tbn[2], tbn[1]);
    f16 tangentHandedness = UnpackTangentHandedness(input.aTangentSpace);
    tbn[2] = QMulVec3(insRot, tbn[2]);
    tbn[1] = QMulVec3(insRot, tbn[1]);
    tbn[1] = normalize(Orthonormalize(tbn[1], tbn[2]));
    tbn[0] = normalize(cross(tbn[2], tbn[1])) * tangentHandedness;

    f16_3 worldPos = QMulVec3(insRot, f16_3(input.aPos) * insScale);
    float3 finalWorldPos = float3(worldPos) + entity.position.xyz;

    VSOutput o;
    o.position  = mul(uViewProj, float4(finalWorldPos, 1.0));
    o.texCoords = input.aTexCoords;
    o.normal    = normalize(tbn[2]);
    o.tangent   = tbn[1];
    o.bitangent = tbn[0];
    o.viewDir   = uCameraPosition.xyz - finalWorldPos;
    o.shadowPos0 = mul(uLightViewProj[0], float4(finalWorldPos, 1.0));
    o.shadowPos1 = mul(uLightViewProj[1], float4(finalWorldPos, 1.0));
    o.shadowPos2 = mul(uLightViewProj[2], float4(finalWorldPos, 1.0));
    o.viewDepth = dot(finalWorldPos - uCameraPosition.xyz, uCameraForward.xyz);
    o.cascadeIndex = o.viewDepth > uCascadeSplits.x ? 1u : 0u;
    o.cascadeIndex = o.viewDepth > uCascadeSplits.y ? 2u : o.cascadeIndex;
    o.materialIndex = group.materialIndex;
    return o;
}

Texture2DArray<float4> AlbedoPages            : register(t0, space2);
Texture2DArray<float2> NormalPages            : register(t1, space2);
Texture2DArray<float2> MetallicRoughnessPages : register(t2, space2);
Texture2DArray<float>  ShadowMap              : register(t3, space2);
SamplerState           Sampler                : register(s0, space2);

StructuredBuffer<MaterialGPU>        sMaterials          : register(t4, space2);
StructuredBuffer<TextureDescriptor>  sTextureDescriptors : register(t5, space2);

float4 frag(VSOutput input) : SV_Target0
{
    MaterialGPU material = sMaterials[input.materialIndex];
    TextureDescriptor albedo     = sTextureDescriptors[material.albedoDescriptor];
    TextureDescriptor normalDesc = sTextureDescriptors[material.normalDescriptor];
    TextureDescriptor mrDesc     = sTextureDescriptors[material.metallicRoughnessDescriptor];

    float4 baseFactor = UnpackColor4Uint(material.baseColorFactor);
    f16_3 baseColor  = SRGBToLinear(SampleTexturePageRGBA(AlbedoPages, Sampler, albedo, float2(input.texCoords)).rgb) * f16_3(baseFactor.rgb);
    float3 tangentNormal = DecodeNormalRG(float2(SampleTexturePageRG(NormalPages, Sampler, normalDesc, float2(input.texCoords))));
    f16_2 mr = SampleTexturePageRG(MetallicRoughnessPages, Sampler, mrDesc, float2(input.texCoords));

    float3 N = normalize(tangentNormal.x * normalize(float3(input.tangent)) +
                         tangentNormal.y * normalize(float3(input.bitangent)) +
                         tangentNormal.z * normalize(float3(input.normal)));
    float metallicFactor  = float((material.metallicRoughnessFactor >> 16u) & 0xFFFFu) * (1.0f / 65535.0f);
    float roughnessFactor = float(material.metallicRoughnessFactor & 0xFFFFu) * (1.0f / 65535.0f);
    float metallic  = float(mr.x) * metallicFactor;
    float roughness = float(mr.y) * roughnessFactor;
    
    uint cascadeIndex = input.cascadeIndex;
    float4 shadowPos = cascadeIndex == 0u ? input.shadowPos0 : (cascadeIndex == 1u ? input.shadowPos1 : input.shadowPos2);
    float shadow = SampleShadow(ShadowMap, Sampler, shadowPos, cascadeIndex, N);
    return float4(ApplyPBR(float3(baseColor), N, float3(input.viewDir), metallic, roughness, shadow), baseFactor.a);
}
