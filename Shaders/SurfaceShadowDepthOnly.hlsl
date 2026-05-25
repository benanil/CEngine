#include "CommonStructs.hlsl"
#include "Bitpack.hlsl"
#include "Math.hlsl"
#include "ShadowCascade.hlsl"

cbuffer vs_params : register(b0, space1)
{
    uint uCascadeIndex;
};

StructuredBuffer<Entity>         sEntities         : register(t0);
StructuredBuffer<PrimitiveGroup> sPrimitiveGroups  : register(t1);
StructuredBuffer<uint>           sDrawSparseIndices : register(t2);
StructuredBuffer<ShadowCascadeBuffer> sShadowCascades : register(t3);

struct VSInput
{
    float3   aPos          : POSITION0;
    uint     aTangentSpace : TANGENT0;
    f16_2_io aTexCoords    : TEXCOORD0;
};

float4 vert(VSInput input, uint instanceID : SV_InstanceID, [[vk::builtin("DrawIndex")]] uint drawID : DRAWINDEX) : SV_Position
{
    PrimitiveGroup group = sPrimitiveGroups[drawID];
    uint denseIdx = sDrawSparseIndices[group.entityOffset + instanceID];
    Entity entity = sEntities[denseIdx];

    f16_4 insRot   = normalize(UnpackRGBA16Snorm(entity.rotation[0], entity.rotation[1]));
    f16_3 insScale = UnpackVec3XY11Z10Unorm(entity.scale) * f16(10.0);
    f16_3 worldPos = QMulVec3(insRot, f16_3(input.aPos) * insScale);
    float3 finalWorldPos = float3(worldPos) + entity.position.xyz;
    return MulShadowCascade(sShadowCascades[0], uCascadeIndex, float4(finalWorldPos, 1.0));
}

float frag(float4 position : SV_Position) : SV_Target0
{
    return position.z;
}
