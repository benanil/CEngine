#include "../Include/RenderLimits.h"
#include "CommonStructs.hlsl"
#include "Bitpack.hlsl"
#include "Math.hlsl"

struct SkinnedVertex
{
    uint positionXY;
    uint positionZW;
    uint tangentSpace;
    uint texCoords;
    uint joints;
    uint weights;
};

cbuffer params : register(b0, space2)
{
    uint numPrimitiveGroups;
    uint maxAnimatedVertices;
    uint2 padding;
};

StructuredBuffer<uint>           sBoneMtx          : register(t0);
StructuredBuffer<Entity>         sEntities         : register(t1);
StructuredBuffer<PrimitiveGroup> sPrimitiveGroups  : register(t2);
StructuredBuffer<uint>           sDrawDenseIndices : register(t3);
StructuredBuffer<SkinnedVertex>  sVertexBuffer     : register(t4);
StructuredBuffer<IndexedDrawCommand> sDrawArgs     : register(t5);

RWStructuredBuffer<AnimatedVert> sAnimatedVert     : register(u0, space1);

void AnimateOneVertex(uint primitiveIdx, uint instanceSlot, uint localVertex)
{
    if (primitiveIdx >= numPrimitiveGroups)
        return;

    PrimitiveGroup group = sPrimitiveGroups[primitiveIdx];
    if (instanceSlot >= sDrawArgs[primitiveIdx].numInstances || localVertex >= group.numVertices)
        return;

    uint denseIdx  = sDrawDenseIndices[group.entityOffset + instanceSlot];
    uint sparse = sEntities[denseIdx].sparse;
    if (group.animatedVertexOffset + localVertex >= uint(MAX_SKINNED_VERTEX_PER_ANIM_INSTANCE))
        return;

    uint boneStart = sEntities[denseIdx].sparse * MAX_BONES;
    uint sourceVertex = group.vertexOffset + localVertex;
    uint outVertex = sparse * uint(MAX_SKINNED_VERTEX_PER_ANIM_INSTANCE) + group.animatedVertexOffset + localVertex;

    SkinnedVertex input = sVertexBuffer[sourceVertex];
    f16_4 packedPos = f16_4(UnpackHalf2(input.positionXY), UnpackHalf2(input.positionZW));
    f16_4 inputPos = f16_4(packedPos.xyz, f16(1.0));

    f16_4 weights;
    weights.xyz = UnpackVec3XY11Z10Unorm(input.weights);
    weights.w   = saturate(f16(1.0) - weights.x - weights.y - weights.z);

    f16_3x4 animMat = (f16_3x4)0;
    uint joints = input.joints;
    [unroll]
    for (int i = 0; i < 4; i++)
    {
        f16_3x4 bone = LoadBone(sBoneMtx, boneStart + (joints & 0xFFu));
        animMat[0] = mad(weights[i], bone[0], animMat[0]);
        animMat[1] = mad(weights[i], bone[1], animMat[1]);
        animMat[2] = mad(weights[i], bone[2], animMat[2]);
        joints >>= 8;
    }

    f16_4x3 animT  = transpose(animMat);
    Entity entity = sEntities[denseIdx];
    f16_4 insRot = normalize(UnpackRGBA16Snorm(entity.rotation[0], entity.rotation[1]));
    f16_3 insScale = UnpackVec3XY11Z10Unorm(entity.scale) * f16(10.0);

    f16_3x3 tbn;
    UnpackNormalTangent(input.tangentSpace, tbn[2], tbn[1]);
    f16 tangentHandedness = UnpackTangentHandedness(input.tangentSpace);

    tbn[2] = QMulVec3(insRot, mul(f16_4(tbn[2], f16(0.0)), animT));
    tbn[1] = QMulVec3(insRot, mul(f16_4(tbn[1], f16(0.0)), animT));
    tbn[0] = Orthonormalize(tbn[1], tbn[2]);

    f16_3 localPos = insScale * QMulVec3(insRot, mul(inputPos, animT));

    AnimatedVert o;
    o.positionXY = PackHalf2(localPos.xy);
    o.positionZTangent = PackHalf(localPos.z) | (PackAnimatedTangentSpace16(tbn[2], tbn[1], tangentHandedness) << 16);
    sAnimatedVert[outVertex] = o;
}

[numthreads(32, 1, 1)]
void main(uint3 globalID : SV_DispatchThreadID, uint3 groupID : SV_GroupID)
{
    uint primitiveIdx = globalID.x;
    uint instanceBase = groupID.y * 32u;
    uint vertexBase = groupID.z * 32u;

    [loop]
    for (uint instanceOffset = 0; instanceOffset < 32u; instanceOffset++)
    {
        [loop]
        for (uint vertexOffset = 0; vertexOffset < 32u; vertexOffset++)
        {
            AnimateOneVertex(primitiveIdx, instanceBase + instanceOffset, vertexBase + vertexOffset);
        }
    }
}
