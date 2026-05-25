#include "TextureSampling.hlsl"
#include "PBR.hlsl"
#include "Bitpack.hlsl"
#include "Math.hlsl"
#include "Shadow.hlsl"

#define CSM_DEBUG_CASCADES 0

cbuffer vs_params : register(b0, space1)
{
    float4x4 uViewProj;
    float4x4 uLightViewProj[3];
    float4 uCameraPosition;
    float4 uCameraForward;
    float4 uCascadeSplits;
};

cbuffer ps_params : register(b0, space3)
{
    float4 uViewportSize;
    float4 uSunDirection;
};

StructuredBuffer<Entity>         sEntities          : register(t0);
StructuredBuffer<PrimitiveGroup> sPrimitiveGroups   : register(t1);
StructuredBuffer<uint>           sDrawSparseIndices : register(t2);

Texture2DArray<float4> AlbedoPages            : register(t0, space2);
Texture2DArray<float2> NormalPages            : register(t1, space2);
Texture2DArray<float2> MetallicRoughnessPages : register(t2, space2);
Texture2DArray<float>  ShadowMap              : register(t3, space2);
SamplerState           Sampler                : register(s0, space2);
SamplerState           ShadowSampler          : register(s3, space2);

StructuredBuffer<MaterialGPU>        sMaterials          : register(t4, space2);
StructuredBuffer<TextureDescriptor>  sTextureDescriptors : register(t5, space2);

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
    nointerpolation float2 cascadeSplits : TEXCOORD7;
    nointerpolation uint materialIndex : TEXCOORD8;
    nointerpolation float handedness : TEXCOORD9;
};

struct GBufferOutput
{
    uint   tangentFrame    : SV_Target0;
    f16_4_io albedoMetallic  : SV_Target1;
    f16_2_io shadowRoughness : SV_Target2;
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
    o.cascadeSplits = uCascadeSplits.xy;
    o.materialIndex = group.materialIndex;
    o.handedness = float(tangentHandedness);
    return o;
}

GBufferOutput frag(VSOutput input)
{
    MaterialGPU material = sMaterials[input.materialIndex];
    TextureDescriptor albedo     = sTextureDescriptors[material.albedoDescriptor];
    TextureDescriptor normalDesc = sTextureDescriptors[material.normalDescriptor];
    TextureDescriptor mrDesc     = sTextureDescriptors[material.metallicRoughnessDescriptor];

    f16_4 albedoSample = SampleTexturePageRGBA(AlbedoPages, Sampler, albedo, float2(input.texCoords));
    float4 baseFactor  = UnpackColor4Uint(material.baseColorFactor);
    AlphaClipMaterial(material, float(albedoSample.a));
    f16_3 baseColor = SRGBToLinear(albedoSample.rgb) * f16_3(baseFactor.rgb);
    float3 tangentNormal = DecodeNormalRG(float2(SampleTexturePageRG(NormalPages, Sampler, normalDesc, float2(input.texCoords))));
    f16_2 mr = SampleTexturePageRG(MetallicRoughnessPages, Sampler, mrDesc, float2(input.texCoords));

    float3 N = normalize(tangentNormal.x * normalize(float3(input.tangent)) +
                         tangentNormal.y * normalize(float3(input.bitangent)) +
                         tangentNormal.z * normalize(float3(input.normal)));

    float metallic  = float(mr.x);
    float roughness = float(mr.y);

    uint cascadeIndex = input.viewDepth > input.cascadeSplits.x ? 1u : 0u;
    cascadeIndex = input.viewDepth > input.cascadeSplits.y ? 2u : cascadeIndex;
    float4 shadowPos = cascadeIndex == 0u ? input.shadowPos0 : (cascadeIndex == 1u ? input.shadowPos1 : input.shadowPos2);
    float shadow = SampleShadow(ShadowMap, ShadowSampler, shadowPos, cascadeIndex, N, uSunDirection.xyz);

    #if CSM_DEBUG_CASCADES
    f16_3 cascadeColor = cascadeIndex == 0u ? f16_3(1.0f, 0.0f, 0.0f) : (cascadeIndex == 1u ? f16_3(0.0f, 1.0f, 0.0f) : f16_3(0.0f, 0.0f, 1.0f));
    baseColor  = lerp(cascadeColor * f16(0.12f), cascadeColor, f16(saturate(shadow)));
    metallic   = 0.0f; roughness  = 1.0f;
    #endif

    roughness = SpecularAntiAliasing(roughness, ddx(N), ddy(N));
    float3 T = normalize(float3(input.tangent) - dot(float3(input.tangent), N) * N);
    GBufferOutput output;
    output.tangentFrame    = PackNormalTangent(f16_3(N), f16_4(f16_3(T), f16(input.handedness)));
    output.albedoMetallic  = f16_4_io(float4(float3(baseColor), saturate(metallic)));
    output.shadowRoughness = f16_2_io(float2(saturate(shadow), saturate(roughness)));
    return output;
}
