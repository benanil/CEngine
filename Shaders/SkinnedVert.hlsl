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

StructuredBuffer<uint>   sBoneMtx  : register(t0);
StructuredBuffer<Entity> sEntities : register(t1);

static const uint MaxBonePoses   = 128;
static const uint MatrixNumInt32 = 6;

struct VSInput
{
    fp16_4_io aPos          : POSITION0;
    uint      aTangentSpace : TANGENT0;
    fp16_2_io aTexCoords    : TEXCOORD0;
    uint4     aJoints       : BLENDINDICES0;
    uint      aWeights      : BLENDWEIGHT0;
};

struct VSOutput
{
    float4    position  : SV_Position;
    fp16_2_io texCoords : TEXCOORD0;
    fp16_3_io normal    : NORMAL;
};

VSOutput main(VSInput input, uint instanceID : SV_InstanceID)
{
    uint boneStart = instanceID * MaxBonePoses * MatrixNumInt32;

    fp16_4 weights;
    weights.xyz = UnpackVec3XY11Z10Unorm(input.aWeights);
    weights.w   = saturate(fp16(1.0) - weights.x - weights.y - weights.z);

    fp16_3x4 animMat = (fp16_3x4)0;
    [unroll]
    for (int i = 0; i < 4; i++)
    {
        uint base = input.aJoints[i] * MatrixNumInt32 + boneStart;
        animMat[0] = mad(weights[i], fp16_4(unpackHalf2x16(sBoneMtx[base + 0]), unpackHalf2x16(sBoneMtx[base + 1])), animMat[0]);
        animMat[1] = mad(weights[i], fp16_4(unpackHalf2x16(sBoneMtx[base + 2]), unpackHalf2x16(sBoneMtx[base + 3])), animMat[1]);
        animMat[2] = mad(weights[i], fp16_4(unpackHalf2x16(sBoneMtx[base + 4]), unpackHalf2x16(sBoneMtx[base + 5])), animMat[2]);
    }

    fp16_4x3 animT = transpose(animMat);

    Entity entity   = sEntities[instanceID];
    fp16_4 insRot   = normalize(UnpackRGBA16Snorm(entity.rotation[0], entity.rotation[1]));
    fp16_3 insScale = UnpackVec3XY11Z10Unorm(entity.scale[0]) * fp16(10.0);

    fp16_3x3 tbn;
    UnpackNormalTangent(input.aTangentSpace, tbn[2], tbn[1]);

    tbn[2] = QuaternionRotateVector(insRot, mul(fp16_4(tbn[2], fp16(0.0)), animT));
    tbn[1] = QuaternionRotateVector(insRot, mul(fp16_4(tbn[1], fp16(0.0)), animT));
    tbn[0] = Orthonormalize(tbn[1], tbn[2]);

    fp16_3 worldPos = QuaternionRotateVector(insRot, mul(fp16_4(input.aPos.xyz, fp16(1.0)), animT));

    VSOutput o;
    o.position  = mul(uViewProj, float4(float3(insScale * worldPos) + entity.position.xyz, 1.0));
    o.texCoords = input.aTexCoords;
    o.normal    = normalize(tbn[2]);
    return o;
}