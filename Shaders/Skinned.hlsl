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

StructuredBuffer<Entity>         sEntities         : register(t0);
StructuredBuffer<PrimitiveGroup> sPrimitiveGroups  : register(t1);
StructuredBuffer<uint>           sDrawDenseIndices : register(t2);
StructuredBuffer<AnimatedVert>   sAnimatedVert     : register(t3);

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

VSOutput vert(VSInput input, uint instanceID : SV_InstanceID, [[vk::builtin("DrawIndex")]] uint drawID : DRAWINDEX, uint vertexID : SV_VertexID)
{
    PrimitiveGroup group = sPrimitiveGroups[drawID];
    uint denseIdx  = sDrawDenseIndices[group.entityOffset + instanceID];
    uint localVertex = vertexID - group.vertexOffset;
    uint sparse = sEntities[denseIdx].sparse;
    uint animatedVertex = sparse * uint(MAX_SKINNED_VERTEX_PER_ANIM_INSTANCE) + group.animatedVertexOffset + localVertex;
    AnimatedVert animated = sAnimatedVert[animatedVertex];
    Entity entity = sEntities[denseIdx];
    uint2 packedAnimated = uint2(animated.packed0, animated.packed1);
    f16_3 localPos = UnpackAnimatedPosition(packedAnimated);
    float3 finalWorldPos = float3(localPos) + entity.position.xyz;

    f16_3x3 tbn;
    f16 tangentHandedness;
    UnpackAnimatedTangentSpace(packedAnimated, tbn[2], tbn[1], tangentHandedness);

    VSOutput o;
    o.position  = mul(uViewProj, float4(finalWorldPos, 1.0));
    o.texCoords = input.aTexCoords;
    o.normal    = tbn[2];
    o.tangent   = tbn[1];
    o.bitangent = cross(tbn[2], tbn[1]) * tangentHandedness;
    o.viewDir   = uCameraPosition.xyz - finalWorldPos;
    o.materialIndex = group.materialIndex;
    return o;
}

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

    float4 baseFactor = float4(1.0, 1.0, 1.0, 1.0); // UnpackColor4Uint(material.baseColorFactor);
    f16_3 baseColor = SRGBToLinear(SampleTexturePageRGBA(AlbedoPages, Sampler, albedo, float2(input.texCoords)).rgb) * f16_3(baseFactor.rgb);
    float3 tangentNormal = DecodeNormalRG(float2(SampleTexturePageRG(NormalPages, Sampler, normalDesc, float2(input.texCoords))));
    f16_2 mr = SampleTexturePageRG(MetallicRoughnessPages, Sampler, mrDesc, float2(input.texCoords));

    float3 N = normalize(tangentNormal.x * normalize(float3(input.tangent)) +
                         tangentNormal.y * normalize(float3(input.bitangent)) +
                         tangentNormal.z * normalize(float3(input.normal)));
    float metallicFactor  = float((material.metallicRoughnessFactor >> 16u) & 0xFFFFu) * (1.0f / 65535.0f);
    float roughnessFactor = float(material.metallicRoughnessFactor & 0xFFFFu) * (1.0f / 65535.0f);
    float metallic  = float(mr.x) * metallicFactor;
    float roughness = float(mr.y) * roughnessFactor;
    
    #if PBR_DEBUG_OUTPUT == 4
        return float4(normalize(float3(input.normal)) * 0.5f + 0.5f, baseFactor.a);
    #elif PBR_DEBUG_OUTPUT == 5
        return float4(normalize(float3(input.tangent)) * 0.5f + 0.5f, baseFactor.a);
    #elif PBR_DEBUG_OUTPUT == 6
        return float4(normalize(float3(input.bitangent)) * 0.5f + 0.5f, baseFactor.a);
    #elif PBR_DEBUG_OUTPUT == 7
        return float4(tangentNormal * 0.5f + 0.5f, baseFactor.a);
    #elif PBR_DEBUG_OUTPUT == 8
        return float4(metallicFactor, roughnessFactor, f16(0.0), baseFactor.a);
    #endif
    return float4(ApplyPBR(float3(baseColor), N, float3(input.viewDir), metallic, roughness), baseFactor.a);
}
