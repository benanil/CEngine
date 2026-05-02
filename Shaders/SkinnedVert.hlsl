#include "Bitpack.hlsl"
#include "Math.hlsl"

cbuffer vs_params : register(b0, space1)
{
    float4x4 uViewProj;
};

struct Entity
{
    float4   position;
    uint32_t rotation[2];
    uint32_t scale[2];
};

StructuredBuffer<uint>         sBoneMtx  : register(t0);
StructuredBuffer<Entity>       sEntities : register(t1);

static const uint MatrixNumInt32 = 6;

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
    float4    position  : SV_Position;
    f16_2_io texCoords : TEXCOORD0;
    f16_3_io normal    : NORMAL;
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

VSOutput main(VSInput input, uint instanceID : SV_InstanceID)
{
    uint boneStart = instanceID * MAX_BONES; // * MatrixNumInt32;

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

    Entity entity   = sEntities[instanceID];
    f16_4 insRot   = normalize(UnpackRGBA16Snorm(entity.rotation[0], entity.rotation[1]));
    f16_3 insScale = UnpackVec3XY11Z10Unorm(entity.scale[0]) * f16(10.0);

    f16_3x3 tbn;
    UnpackNormalTangent(input.aTangentSpace, tbn[2], tbn[1]);

    tbn[2] = QMulVec3(insRot, mul(f16_4(tbn[2], f16(0.0)), animT));
    tbn[1] = QMulVec3(insRot, mul(f16_4(tbn[1], f16(0.0)), animT));
    tbn[0] = Orthonormalize(tbn[1], tbn[2]);

    f16_3 worldPos = QMulVec3(insRot, mul(f16_4(input.aPos.xyz, f16(1.0)), animT));

    VSOutput o;
    o.position  = mul(uViewProj, float4(float3(insScale * worldPos) + entity.position.xyz, 1.0));
    o.texCoords = input.aTexCoords;
    o.normal    = normalize(tbn[2]);
    return o;
}
