#include "../Include/RenderLimits.h"
#include "TextureSampling.hlsl"
#include "Bitpack.hlsl"
#include "Math.hlsl"
#include "AnimatedTransform.hlsl"
#include "Entity.hlsl"

cbuffer vs_params : register(b0, space1)
{
    float4x4 uViewProj;
};

StructuredBuffer<Entity>            sEntities           : register(t0);
StructuredBuffer<PrimitiveGroup>    sPrimitiveGroups    : register(t1);
StructuredBuffer<uint>              sDrawSparseIndices  : register(t2);
StructuredBuffer<uint>              sBoneMtx            : register(t3);
StructuredBuffer<PrimitiveGroupLOD> sPrimitiveGroupLODs : register(t4);

StructuredBuffer<MaterialGPU> sMaterials : register(t1, space2);
StructuredBuffer<TextureDescriptor> sTextureDescriptors : register(t2, space2);

Texture2DArray<float4> AlbedoPages : register(t0, space2);
SamplerState           Sampler     : register(s0, space2);

struct VSInput {
    f16_4_io  aPos          : POSITION0;
    uint      aTangentSpace : TANGENT0;
    f16_2_io  aTexCoords    : TEXCOORD0;
    uint4     aJoints       : BLENDINDICES0;
    uint      aWeights      : BLENDWEIGHT0;
};

struct VSOutput {
    float4 position : SV_Position;
    f16_2_io texCoords : TEXCOORD0;
    nointerpolation uint materialIndex : TEXCOORD1;
};

VSOutput vert(VSInput input, uint instanceID : SV_InstanceID, 
            [[vk::builtin("DrawIndex")]] uint drawID : DRAWINDEX,
            uint vertexId : SV_VertexID)
{
    uint primitiveIdx = drawID / MESH_LOD_COUNT;
    uint lod = drawID - primitiveIdx * MESH_LOD_COUNT;
    PrimitiveGroup group = sPrimitiveGroups[primitiveIdx];
    uint denseIdx  = sDrawSparseIndices[lod * uint(MAX_ANIM_INSTANCES) + PrimitiveGroup_EntityOffset(group) + instanceID];
    Entity entity = sEntities[denseIdx];

    f16_4  insRot   = EntityGetRotation(entity);
    f16_3  insScale = EntityGetScale(entity);

    float4x3 animMat = VertexAnimate(sBoneMtx, denseIdx * MAX_BONES, input.aJoints, input.aWeights);
    float3 pos = mul(float4(input.aPos.xyz, 1.0), animMat);
    float3 finalWorldPos = AnimatedWorldPos(pos, float4(insRot), float3(insScale), entity.position.xyz);

    VSOutput o;
    o.position = mul(uViewProj, float4(finalWorldPos, 1.0));
    o.texCoords = input.aTexCoords;
    o.materialIndex = PrimitiveGroup_MaterialIndex(group);
    return o;
}

float frag(VSOutput input) : SV_Target0
{
    MaterialGPU material = sMaterials[input.materialIndex];
	u16 flags = (u16)(material.metallicRoughnessDescriptorAndFlags >> 16);
	if (MaterialIsAlphaMasked(flags))
    {
        TextureDescriptor albedo = sTextureDescriptors[material.AlbedoNormalDescriptor & 0xFFFF];
        f16_4 albedoSample = SampleTexturePageRGBA(AlbedoPages, Sampler, albedo, float2(input.texCoords), f16_4(1.0, 1.0, 1.0, 1.0));
        AlphaClipMaterial(material, float(albedoSample.a));
    }
    return input.position.z;
}
