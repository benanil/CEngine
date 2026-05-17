#include "../Include/RenderLimits.h"
#include "CommonStructs.hlsl"
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

static const uint MatrixNumInt32 = 6;

struct VSInput
{
    f16_4_io  aPos          : POSITION0;
    uint      aTangentSpace : TANGENT0;
    f16_2_io  aTexCoords    : TEXCOORD0;
    uint4     aJoints       : BLENDINDICES0;
    uint      aWeights      : BLENDWEIGHT0;
};

f16_3x4 LoadBone(uint idx)
{
    uint base = idx * MatrixNumInt32;
    f16_3x4 bone;
    bone[0] = f16_4(UnpackHalf2(sBoneMtx[base + 0]), UnpackHalf2(sBoneMtx[base + 1]));
    bone[1] = f16_4(UnpackHalf2(sBoneMtx[base + 2]), UnpackHalf2(sBoneMtx[base + 3]));
    bone[2] = f16_4(UnpackHalf2(sBoneMtx[base + 4]), UnpackHalf2(sBoneMtx[base + 5]));
    return bone;
}

float4 vert(VSInput input, uint instanceID : SV_InstanceID, [[vk::builtin("DrawIndex")]] uint drawID : DRAWINDEX) : SV_Position
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
        f16_3x4 bone = LoadBone(boneStart + input.aJoints[i]);
        animMat[0] = mad(weights[i], bone[0], animMat[0]);
        animMat[1] = mad(weights[i], bone[1], animMat[1]);
        animMat[2] = mad(weights[i], bone[2], animMat[2]);
    }

    f16_4x3 animT = transpose(animMat);
    Entity entity = sEntities[denseIdx];
    f16_4 insRot = normalize(UnpackRGBA16Snorm(entity.rotation[0], entity.rotation[1]));
    f16_3 insScale = UnpackVec3XY11Z10Unorm(entity.scale) * f16(10.0);
    f16_3 worldPos = QMulVec3(insRot, mul(f16_4(input.aPos.xyz, f16(1.0)), animT));
    float3 finalWorldPos = float3(insScale * worldPos) + entity.position.xyz;
    return mul(uViewProj, float4(finalWorldPos, 1.0));
}

void frag() {}
