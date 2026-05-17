#include "../Include/RenderLimits.h"
#include "CommonStructs.hlsl"
#include "Bitpack.hlsl"
#include "Math.hlsl"

cbuffer vs_params : register(b0, space1)
{
    // float4x4 uViewProj;
    // float4 uCameraPosition;
};

struct VSInput
{
    uint2 aPos         ;
    uint  aTangentSpace;
    uint  aTexCoords   ;
    uint  aJoints      ;
    uint  aWeights     ;
};

struct AnimatedVert
{
    float4 position;
    uint   tangentSpace;
    uint   texCoords;
    uint2  padd;
};

StructuredBuffer<uint>           sBoneMtx          : register(t0);
StructuredBuffer<Entity>         sEntities         : register(t1);
StructuredBuffer<PrimitiveGroup> sPrimitiveGroups  : register(t2);
StructuredBuffer<uint>           sDrawDenseIndices : register(t3);
StructuredBuffer<VSInput>        sVertexBuffer     : register(t4);
StructuredBuffer<AnimatedVert>   sAnimatedVert     : register(t5);

[numthreads(32, 1, 1)]
void main(uint3 GlobalInvocationID : SV_DispatchThreadID)
{
    // VSInput input, uint instanceID : SV_InstanceID, uint drawID : DRAWINDEX
    PrimitiveGroup group = sPrimitiveGroups[drawID];
    uint denseIdx  = sDrawDenseIndices[group.entityOffset + instanceID];
    uint boneStart = sEntities[denseIdx].sparse * MAX_BONES;

    f16_4 weights;
    weights.xyz = UnpackVec3XY11Z10Unorm(input.aWeights);
    weights.w   = saturate(f16(1.0) - weights.x - weights.y - weights.z);

    f16_3x4 animMat = (f16_3x4)0;
    uint joints = input.aJoints;
    [unroll]
    for (int i = 0; i < 4; i++)
    {
        f16_3x4 bone = LoadBone(sBoneMtx, boneStart + (joints & 0xFF));
        animMat[0] = mad(weights[i], bone[0], animMat[0]);
        animMat[1] = mad(weights[i], bone[1], animMat[1]);
        animMat[2] = mad(weights[i], bone[2], animMat[2]);
        joints >>= 8;
    }

    f16_4x3 animT  = transpose(animMat);
    Entity entity  = sEntities[denseIdx];
    f16_4 insRot   = normalize(UnpackRGBA16Snorm(entity.rotation[0], entity.rotation[1]));
    f16_3 insScale = UnpackVec3XY11Z10Unorm(entity.scale) * f16(10.0);

    f16_3x3 tbn;
    UnpackNormalTangent(input.aTangentSpace, tbn[2], tbn[1]);
    f16 tangentHandedness = UnpackTangentHandedness(input.aTangentSpace);

    tbn[2] = QMulVec3(insRot, mul(f16_4(tbn[2], f16(0.0)), animT));
    tbn[1] = QMulVec3(insRot, mul(f16_4(tbn[1], f16(0.0)), animT));
    tbn[0] = Orthonormalize(tbn[1], tbn[2]);

    f16_3 worldPos = QMulVec3(insRot, mul(f16_4(input.aPos.xyz, f16(1.0)), animT));
    float3 finalWorldPos = float3(insScale * worldPos) + entity.position.xyz;
    AnimatedVert o;
    o.position  = float4(finalWorldPos, 1.0);
    o.texCoords = input.aTexCoords;
    o.tangentSpace = PackNormalTangent(normalize(tbn[2]), normalize(tbn[1]));
    uint outID = 0;
    sAnimatedVert[outID] = o;
}