#ifndef ANIMATED_TRANSFORM_HLSL
#define ANIMATED_TRANSFORM_HLSL

#include "Math.hlsl"
#include "Bitpack.hlsl"

// Apply entity rotation + scale to a model-space position (matches the static Surface path, fp32).
float3 AnimatedWorldPos(float3 modelPos, float4 insRot, float3 insScale, float3 entityPos)
{
    return entityPos + QMulVec3F32(insRot, modelPos * insScale);
}

// bone matrices are stored fp16 (12 halves / 6 u32); see WriteBone in AnimationCompute.hlsl.
f16_3x4 LoadBone(StructuredBuffer<uint> boneMtx, uint idx)
{
    uint base = idx * MatrixNumInt32;
    f16_3x4 bone;
    bone[0] = f16_4(UnpackHalf2(boneMtx[base + 0]), UnpackHalf2(boneMtx[base + 1]));
    bone[1] = f16_4(UnpackHalf2(boneMtx[base + 2]), UnpackHalf2(boneMtx[base + 3]));
    bone[2] = f16_4(UnpackHalf2(boneMtx[base + 4]), UnpackHalf2(boneMtx[base + 5]));
    return bone;
}

f16_3x4 VertexAnimate(StructuredBuffer<uint> sBoneMtx, uint boneStart, uint4 aJoints, uint aWeights)
{
    f16_4 weights;
    weights.xyz = UnpackVec3XY11Z10Unorm(aWeights);
    weights.w   = saturate(f16(1.0) - weights.x - weights.y - weights.z);
    
    f16_3x4 animMat = (f16_3x4)0;
    [unroll]
    for (int i = 0; i < 4; i++)
    {
        f16_3x4 bone = LoadBone(sBoneMtx, boneStart + aJoints[i]);
        animMat[0] = mad(weights[i], bone[0], animMat[0]);
        animMat[1] = mad(weights[i], bone[1], animMat[1]);
        animMat[2] = mad(weights[i], bone[2], animMat[2]);
    }
    return animMat;
}

#endif // ANIMATED_TRANSFORM_HLSL
