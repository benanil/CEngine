#ifndef ANIMATED_TRANSFORM_HLSL
#define ANIMATED_TRANSFORM_HLSL

#include "Math.hlsl"
#include "Bitpack.hlsl"

// Apply entity rotation + scale to a model-space position (matches the static Surface path, fp32).
float3 AnimatedWorldPos(float3 modelPos, float4 insRot, float3 insScale, float3 entityPos)
{
    return entityPos + QMulVec3F32(insRot, modelPos * insScale);
}

float4x3 SkinPosition(StructuredBuffer<uint> sBoneMtx, uint boneIdx)
{
    uint base = boneIdx * MatrixNumInt32;
    float2 a0 = UnpackHalf2_f32(sBoneMtx[base + 0]);
    float2 a1 = UnpackHalf2_f32(sBoneMtx[base + 1]);
    float2 a2 = UnpackHalf2_f32(sBoneMtx[base + 2]);
    float2 a3 = UnpackHalf2_f32(sBoneMtx[base + 3]);
    float2 a4 = UnpackHalf2_f32(sBoneMtx[base + 4]);
    return float4x3(
        float3(a0.x, a0.y, a1.x),
        float3(a1.y, a2.x, a2.y),
        float3(a3.x, a3.y, a4.x),
        float3(asfloat(sBoneMtx[base + 5]), asfloat(sBoneMtx[base + 6]), asfloat(sBoneMtx[base + 7]))
    );
}

float4x3 VertexAnimate(StructuredBuffer<uint> sBoneMtx, uint boneStart, uint4 aJoints, uint aWeights)
{
    float4 weights;
    weights.xyz = UnpackVec3XY11Z10Unormf32(aWeights);
    weights.w   = saturate(f16(1.0) - weights.x - weights.y - weights.z);
    
    float4x3 animMat = (float4x3)0;
    [unroll]
    for (int i = 0; i < 4; i++)
    {
        float4x3 bone = SkinPosition(sBoneMtx, boneStart + aJoints[i]);
        animMat[0] = mad(weights[i], bone[0], animMat[0]);
        animMat[1] = mad(weights[i], bone[1], animMat[1]);
        animMat[2] = mad(weights[i], bone[2], animMat[2]);
        animMat[3] = mad(weights[i], bone[3], animMat[3]);
    }
    return animMat;
}

f16_4x3 SkinPositionf16(StructuredBuffer<uint> sBoneMtx, uint boneIdx)
{
    uint base = boneIdx * MatrixNumInt32;
    f16_2 a0 = UnpackHalf2(sBoneMtx[base + 0]);
    f16_2 a1 = UnpackHalf2(sBoneMtx[base + 1]);
    f16_2 a2 = UnpackHalf2(sBoneMtx[base + 2]);
    f16_2 a3 = UnpackHalf2(sBoneMtx[base + 3]);
    f16_2 a4 = UnpackHalf2(sBoneMtx[base + 4]);
    return float4x3(
        float3(a0.x, a0.y, a1.x),
        float3(a1.y, a2.x, a2.y),
        float3(a3.x, a3.y, a4.x),
        float3(f16(asfloat(sBoneMtx[base + 5])), f16(asfloat(sBoneMtx[base + 6])), f16(asfloat(sBoneMtx[base + 7])))
    );
}

f16_4x3 VertexAnimatef16(StructuredBuffer<uint> sBoneMtx, uint boneStart, uint4 aJoints, uint aWeights)
{
    f16_4 weights;
    weights.xyz = UnpackVec3XY11Z10Unorm(aWeights);
    weights.w   = saturate(f16(1.0) - weights.x - weights.y - weights.z);
    
    f16_4x3 animMat = (f16_4x3)0;
    [unroll]
    for (int i = 0; i < 4; i++)
    {
        f16_4x3 bone = SkinPositionf16(sBoneMtx, boneStart + aJoints[i]);
        animMat[0] = mad(weights[i], bone[0], animMat[0]);
        animMat[1] = mad(weights[i], bone[1], animMat[1]);
        animMat[2] = mad(weights[i], bone[2], animMat[2]);
        animMat[3] = mad(weights[i], bone[3], animMat[3]);
    }
    return animMat;
}

#endif // ANIMATED_TRANSFORM_HLSL
