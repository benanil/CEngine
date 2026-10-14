#ifndef ANIMATED_TRANSFORM_HLSL
#define ANIMATED_TRANSFORM_HLSL

#include "Math.hlsl"
#include "Bitpack.hlsl"

// The animated vertex cache (sAnimatedVert) stores ONLY a bounds-normalized skinned position,
// packed as 16/16/16 unorm in 2x u32 (see PackUnorm16x4 / UnpackUnorm16x4).
//
// Unlike the old format, neither entity rotation nor scale is baked in: the compute pass writes
// the skinned position in *model space* (exactly like a static mesh quantizes against its AABB),
// and every vertex shader applies the entity rotation + scale on read. Keeping the stored value in
// model space makes the bounds normalization exact (no rotation mixing axes -> no clamping) and
// lets all depth/shadow passes share one position-only buffer.
//
// Everything on the read side stays fp32: the model position is meter-scale, so casting it to fp16
// would re-introduce ~1-2 mm of frame-to-frame jitter. PackAnimatedCenterRel (compute) and
// UnpackAnimatedModelPos (consumers) are exact inverses.

float3 AnimatedBoundsCenter(float3 aabbMin, float3 aabbMax) { return (aabbMin + aabbMax) * 0.5f; }

// guard against a zero-extent axis (planar mesh) so the normalize divide can't produce NaN
float3 AnimatedBoundsExtent(float3 aabbMin, float3 aabbMax) { return max((aabbMax - aabbMin) * 0.5f, 1.0e-4f); }

// Compute pass: centerRel = skinnedModelPos - boundsCenter (the skinning accumulates in this
// bounds-local frame so values stay near 0). extent is the AABB half-size. Maps
// [-extent, extent] -> [0, 1] and packs to 16/16/16. Kept in fp32 so the quantization is the only
// precision loss.
uint2 PackAnimatedCenterRel(float3 centerRel, float3 extent)
{
    float3 norm01 = centerRel / (2.0f * extent) + 0.5f;
    return PackUnorm16x4(float4(norm01, 0.0f));
}

// Vertex shaders: rebuild the model-space skinned position from the packed unorm (fp32).
float3 UnpackAnimatedModelPos(uint2 packed, float3 aabbMin, float3 aabbMax)
{
    float3 norm01 = UnpackUnorm16x4(packed).xyz;
    return aabbMin + norm01 * (aabbMax - aabbMin);
}

// Apply entity rotation + scale to a model-space position (matches the static Surface path, fp32).
float3 AnimatedWorldPos(float3 modelPos, float4 insRot, float3 insScale, float3 entityPos)
{
    return entityPos + QMulVec3F32(insRot, modelPos * insScale);
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
