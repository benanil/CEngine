// Visibility-buffer raster pass.
//
// Replaces the fat G-buffer geometry pass for surface pixels: instead of shading and
// writing material attributes, this writes only per-pixel IDs. VisBufferShade later
// reconstructs attributes from these IDs and writes lit color. The vertex transform here is identical to
// SurfaceDepthOnly.vert / Surface.vert so depth-equal against the prepass is exact.
//
// NOTE: keep the vertex transform and alpha-clip in sync with the surface depth/shadow passes.
#include "TextureSampling.hlsl"
#include "Bitpack.hlsl"
#include "Math.hlsl"

cbuffer vs_params : register(b0, space1)
{
    float4x4 uViewProj;
};

StructuredBuffer<Entity>         sEntities          : register(t0);
StructuredBuffer<PrimitiveGroup> sPrimitiveGroups   : register(t1);
StructuredBuffer<uint>           sDrawSparseIndices : register(t2);
// frag: only needed for alpha-masked materials (punch holes so the prepass depth matches)
StructuredBuffer<MaterialGPU>       sMaterials          : register(t1, space2);
StructuredBuffer<TextureDescriptor> sTextureDescriptors : register(t2, space2);
Texture2DArray<float4> AlbedoPages : register(t0, space2);
SamplerState Sampler               : register(s0, space2);

struct VSInput
{
    uint2    aPos          : POSITION0;
    uint     aTangentSpace : TANGENT0;
    f16_2_io aTexCoords    : TEXCOORD0;
};

struct VSOutput
{
    float4   position   : SV_Position;
    f16_2_io texCoords  : TEXCOORD0;
    nointerpolation uint materialIndex : TEXCOORD1;
    nointerpolation uint drawID        : TEXCOORD2;
    nointerpolation uint instanceID    : TEXCOORD3;
};

VSOutput vert(VSInput input, uint instanceID : SV_InstanceID, [[vk::builtin("DrawIndex")]] uint drawID : DRAWINDEX)
{
    uint primitiveIdx = drawID / MESH_LOD_COUNT;
    uint lod = drawID - primitiveIdx * MESH_LOD_COUNT;
    PrimitiveGroup group = sPrimitiveGroups[primitiveIdx];
    uint denseIdx = sDrawSparseIndices[lod * MAX_ENTITY + group.entityOffset + instanceID];
    Entity entity = sEntities[denseIdx];

    f16_4  insRot   = normalize(UnpackRGBA16Snorm(entity.rotation[0], entity.rotation[1]));
    f16_3  insScale = UnpackRGBA16Unorm(entity.scale).xyz * f16(10.0);
    float3 localPos = group.aabbMin.xyz + UnpackUnorm16x4(input.aPos).xyz * (group.aabbMax.xyz - group.aabbMin.xyz);
    f16_3  worldPos = QMulVec3(insRot, f16_3(localPos) * insScale);
    float3 finalWorldPos = float3(worldPos) + entity.position.xyz;

    VSOutput o;
    o.position     = mul(uViewProj, float4(finalWorldPos, 1.0));
    o.texCoords    = input.aTexCoords;
    o.materialIndex = group.materialIndex;
    o.drawID       = drawID;
    o.instanceID   = instanceID;
    return o;
}

uint2 frag(VSOutput input, uint primitiveID : SV_PrimitiveID) : SV_Target0
{
    MaterialGPU material = sMaterials[input.materialIndex];
    if (MaterialIsAlphaMasked(material.flags))
    {
        TextureDescriptor albedo = sTextureDescriptors[material.albedoDescriptor];
        f16_4 albedoSample = SampleTexturePageRGBA(AlbedoPages, Sampler, albedo, float2(input.texCoords));
        AlphaClipMaterial(material, float(albedoSample.a));
    }
    // 8-byte RG32_UINT visibility buffer:
    //   x = (primitiveIdx:16 << 16) | instanceID:16   (MAX_GROUP=65535, <=65535 instances/group)
    //   y = (triangleID:24    <<  8) | lod:8           (lod 0..2; 0xFF in the spare byte = cleared)
    uint primitiveIdx = input.drawID / MESH_LOD_COUNT;
    uint lod = input.drawID - primitiveIdx * MESH_LOD_COUNT;
    uint x = (primitiveIdx << 16) | (input.instanceID & 0xFFFFu);
    uint y = ((primitiveID & 0xFFFFFFu) << 8) | (lod & 0xFFu);
    return uint2(x, y);
}
