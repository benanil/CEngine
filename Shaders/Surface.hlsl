#include "../Include/RenderLimits.h"
#include "CommonStructs.hlsl"
#include "Bitpack.hlsl"
#include "Math.hlsl"

cbuffer vs_params : register(b0, space1)
{
    float4x4 uViewProj;
};

StructuredBuffer<Entity>         sEntities         : register(t0);
StructuredBuffer<PrimitiveGroup> sPrimitiveGroups  : register(t1);
StructuredBuffer<uint>           sDrawDenseIndices : register(t2);

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
};

VSOutput vert(VSInput input, uint instanceID : SV_InstanceID, [[vk::builtin("DrawIndex")]] uint drawID : DRAWINDEX)
{
    PrimitiveGroup group = sPrimitiveGroups[drawID];
    uint denseIdx = sDrawDenseIndices[group.entityOffset + instanceID];
    Entity entity = sEntities[denseIdx];

    f16_4 insRot   = normalize(UnpackRGBA16Snorm(entity.rotation[0], entity.rotation[1]));
    f16_3 insScale = UnpackVec3XY11Z10Unorm(entity.scale) * f16(10.0);

    f16_3x3 tbn;
    UnpackNormalTangent(input.aTangentSpace, tbn[2], tbn[1]);
    tbn[2] = QMulVec3(insRot, tbn[2]);

    f16_3 worldPos = QMulVec3(insRot, f16_3(input.aPos) * insScale);

    VSOutput o;
    o.position  = mul(uViewProj, float4(float3(worldPos) + entity.position.xyz, 1.0));
    o.texCoords = input.aTexCoords;
    o.normal    = normalize(tbn[2]);
    return o;
}

Texture2D<float4> Texture : register(t0, space2);
SamplerState Sampler      : register(s0, space2);

float4 frag(VSOutput input) : SV_Target0
{
    f16_3 sunDir = f16_3(0.5, -0.5, 0.0f);
    f16_io ndl = dot(input.normal, -sunDir);
    return Texture.Sample(Sampler, input.texCoords) * max(ndl, 0.1);
}
