#include "../Include/RenderLimits.h"
#include "CommonStructs.hlsl"
#include "TextureSampling.hlsl"
#define PBR_DEBUG_OUTPUT 0
#include "PBR.hlsl"
#include "Bitpack.hlsl"
#include "Math.hlsl"

cbuffer vs_params : register(b0, space1)
{
    float4x4 uViewProj;
    float4 uCameraPosition;
};

StructuredBuffer<uint>           sBoneMtx          : register(t0);
StructuredBuffer<Entity>         sEntities         : register(t1);
StructuredBuffer<PrimitiveGroup> sPrimitiveGroups  : register(t2);
StructuredBuffer<uint>           sDrawDenseIndices : register(t3);

struct VSInput
{
    f16_4_io  aPos          : POSITION0;
    uint      aTangentSpace : TANGENT0;
    f16_2_io  aTexCoords    : TEXCOORD0;
    uint4     aJoints       : BLENDINDICES0;
    uint      aWeights      : BLENDWEIGHT0;
};

struct VSOutput
{
    float4   position  : SV_Position;
    f16_2_io texCoords : TEXCOORD0;
    f16_3_io normal    : NORMAL;
    f16_3_io tangent   : TANGENT0;
    f16_3_io bitangent : TEXCOORD1;
    f16_3_io viewDir   : TEXCOORD2;
    nointerpolation uint materialIndex : TEXCOORD3;
};

VSOutput vert(VSInput input, uint instanceID : SV_InstanceID, [[vk::builtin("DrawIndex")]] uint drawID : DRAWINDEX)
{
    PrimitiveGroup group = sPrimitiveGroups[drawID];
    uint denseIdx  = sDrawDenseIndices[group.entityOffset + instanceID];
    uint boneStart = sEntities[denseIdx].sparse * MAX_BONES;

    f16_4 weights;
    weights.xyz = UnpackVec3XY11Z10Unorm(input.aWeights);
    weights.w   = saturate(f16(1.0) - weights.x - weights.y - weights.z);

    f16_3x4 animMat = (f16_3x4)0;
    [unroll]
    for (int i = 0; i < 4; i++)
    {
        f16_3x4 bone = LoadBone(sBoneMtx, boneStart + input.aJoints[i]);
        animMat[0] = mad(weights[i], bone[0], animMat[0]);
        animMat[1] = mad(weights[i], bone[1], animMat[1]);
        animMat[2] = mad(weights[i], bone[2], animMat[2]);
    }

    f16_4x3 animT  = transpose(animMat);
    Entity entity  = sEntities[denseIdx];
    f16_4 insRot   = normalize(UnpackRGBA16Snorm(entity.rotation[0], entity.rotation[1]));
    f16_3 insScale = UnpackVec3XY11Z10Unorm(entity.scale) * f16(10.0);

    f16_3x3 tbn;
    UnpackNormalTangent(input.aTangentSpace, tbn[2], tbn[1]);
    f16 tangentHandedness = UnpackTangentHandedness(input.aTangentSpace);

    tbn[2] = QMulVec3(insRot, mul(f16_4(tbn[2], f16(0.0)), animT));
    tbn[1] = QMulVec3(insRot, mul(f16_4(tbn[1], f16(0.0)), animT));
    tbn[0] = Orthonormalize(tbn[1], tbn[2]);

    f16_3 worldPos = QMulVec3(insRot, mul(f16_4(input.aPos.xyz, f16(1.0)), animT));
    float3 finalWorldPos = float3(insScale * worldPos) + entity.position.xyz;
    VSOutput o;
    o.position  = mul(uViewProj, float4(finalWorldPos, 1.0));
    o.texCoords = input.aTexCoords;
    o.normal    = normalize(tbn[2]);
    o.tangent   = normalize(tbn[1]);
    o.bitangent = normalize(cross(tbn[2], tbn[1])) * tangentHandedness;
    o.viewDir   = uCameraPosition.xyz - finalWorldPos;
    o.materialIndex = group.materialIndex;
    return o;
}

#if 0
StructuredBuffer<AnimatedVert> sAnimatedVert : register(t5);
VSOutput vertNew(VSInput input, uint instanceID : SV_InstanceID, [[vk::builtin("DrawIndex")]] uint drawID : DRAWINDEX)
{
    PrimitiveGroup group = sPrimitiveGroups[drawID];
    uint denseIdx  = sDrawDenseIndices[group.entityOffset + instanceID];
    uint boneStart = sEntities[denseIdx].sparse * MAX_BONES;
    Entity entity  = sEntities[denseIdx];
    uint outID = 0;
    AnimatedVert input = sAnimatedVert[outID];
    f16_3x3 tbn;
    UnpackNormalTangent(input.tangentSpace, tbn[2], tbn[1]);
    f16 tangentHandedness = UnpackTangentHandedness(input.tangentSpace);

    VSOutput o;
    o.position  = mul(uViewProj, input.position);
    o.texCoords = input.aTexCoords;
    o.normal    = normalize(tbn[2]);
    o.tangent   = normalize(tbn[1]);
    o.bitangent = normalize(cross(tbn[2], tbn[1])) * tangentHandedness;
    o.viewDir   = uCameraPosition.xyz - finalWorldPos;
    o.materialIndex = group.materialIndex;
    return o;
}
#endif
Texture2DArray<float4> AlbedoPages            : register(t0, space2);
Texture2DArray<float2> NormalPages            : register(t1, space2);
Texture2DArray<float2> MetallicRoughnessPages : register(t2, space2);
SamplerState Sampler                          : register(s0, space2);

StructuredBuffer<MaterialGPU>        sMaterials          : register(t3, space2);
StructuredBuffer<TextureDescriptor>  sTextureDescriptors : register(t4, space2);

float4 frag(VSOutput input) : SV_Target0
{
    MaterialGPU material = sMaterials[input.materialIndex];
    TextureDescriptor albedo = sTextureDescriptors[material.albedoDescriptor];
    TextureDescriptor normalDesc = sTextureDescriptors[material.normalDescriptor];
    TextureDescriptor mrDesc = sTextureDescriptors[material.metallicRoughnessDescriptor];

    float4 baseFactor = VecOne(); // UnpackColor4Uint(material.baseColorFactor);
    float3 baseColor = SRGBToLinear(SampleTexturePageRGBA(AlbedoPages, Sampler, albedo, float2(input.texCoords)).rgb) * baseFactor.rgb;
    float3 tangentNormal = DecodeNormalRG(SampleTexturePageRG(NormalPages, Sampler, normalDesc, float2(input.texCoords)));
    float2 mr = SampleTexturePageRG(MetallicRoughnessPages, Sampler, mrDesc, float2(input.texCoords));

    float3 N = normalize(tangentNormal.x * normalize(input.tangent) +
                         tangentNormal.y * normalize(input.bitangent) +
                         tangentNormal.z * normalize(input.normal));
    float metallicFactor  = float((material.metallicRoughnessFactor >> 16u) & 0xFFFFu) * (1.0f / 65535.0f);
    float roughnessFactor = float(material.metallicRoughnessFactor & 0xFFFFu) * (1.0f / 65535.0f);
    float metallic  = mr.x * metallicFactor;
    float roughness = mr.y * roughnessFactor;
    
    #if PBR_DEBUG_OUTPUT == 4
        return float4(normalize(input.normal) * 0.5f + 0.5f, baseFactor.a);
    #elif PBR_DEBUG_OUTPUT == 5
        return float4(normalize(input.tangent) * 0.5f + 0.5f, baseFactor.a);
    #elif PBR_DEBUG_OUTPUT == 6
        return float4(normalize(input.bitangent) * 0.5f + 0.5f, baseFactor.a);
    #elif PBR_DEBUG_OUTPUT == 7
        return float4(tangentNormal * 0.5f + 0.5f, baseFactor.a);
    #elif PBR_DEBUG_OUTPUT == 8
        return float4(metallicFactor, roughnessFactor, 0.0f, baseFactor.a);
    #endif
    return float4(ApplyPBR(baseColor, N, input.viewDir, metallic, roughness), baseFactor.a);
}
