#include "../../Include/RenderLimits.h"
#include "../CommonStructs.hlsl"
#include "../Bitpack.hlsl"
#include "../Math.hlsl"
#include "../LOD.hlsl"

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
    uint2 viewportSize;
    uint shadowLOD;
    float lodDistanceModifier;
    float3 padding;
    float4x4 viewProjection;
}

StructuredBuffer<uint>               sBoneMtx           : register(t0);
StructuredBuffer<Entity>             sEntities          : register(t1);
StructuredBuffer<PrimitiveGroup>     sPrimitiveGroups   : register(t2);
StructuredBuffer<uint>               sDrawSparseIndices : register(t3);
StructuredBuffer<SkinnedVertex>      sVertexBuffer      : register(t4);
StructuredBuffer<IndexedDrawCommand> sDrawArgs          : register(t5); // unused; kept for binding layout

RWStructuredBuffer<AnimatedVert> sAnimatedVert : register(u0, space1);

[numthreads(1, 32, 1)]
void main(uint3 globalID : SV_DispatchThreadID, uint3 groupID : SV_GroupID, uint3 groupThreadID : SV_GroupThreadID)
{
    uint drawIdx = globalID.x;
    if (drawIdx >= numPrimitiveGroups)
        return;

    uint primitiveIdx = drawIdx / MESH_LOD_COUNT;
    uint lod = drawIdx - primitiveIdx * MESH_LOD_COUNT;
    PrimitiveGroup group = sPrimitiveGroups[primitiveIdx];

    uint instanceSlot = groupID.y * 32u + groupThreadID.y;
    if (instanceSlot >= group.numEntities)
        return;

    uint vertexBase = groupID.z * 32u;
    uint animatedBase = group.lodAnimatedVertexOffset[lod];
    uint numVertices = group.lodNumVertices[lod];
    uint sourceVertexBase = group.lodVertexOffset[lod];

    uint denseIdx = group.entityOffset + instanceSlot;
    Entity entity = sEntities[denseIdx];
    uint sparse = entity.sparse;

    uint cameraLOD = SelectEntityLOD(entity, group, viewProjection, viewportSize, lodDistanceModifier);
    uint requiredShadowLOD = min(shadowLOD, MESH_LOD_COUNT - 1u);
    if (lod != cameraLOD && lod != requiredShadowLOD)
        return;

    uint boneStart = sparse * MAX_BONES;
    f16_4 insRot = normalize(UnpackRGBA16Snorm(entity.rotation[0], entity.rotation[1]));
    f16_3 insScale = UnpackVec3XY11Z10Unorm(entity.scale) * f16(10.0);

    [loop]
    for (uint vertexOffset = 0; vertexOffset < 32u; vertexOffset++)
    {
        uint localVertex = vertexBase + vertexOffset;
        if (localVertex >= numVertices)
            break; 

        uint animatedLocalVertex = animatedBase + localVertex;
        uint outVertex = sparse * uint(MAX_SKINNED_VERTEX_PER_ANIM_INSTANCE) + animatedLocalVertex;
        if (animatedLocalVertex >= uint(MAX_SKINNED_VERTEX_PER_ANIM_INSTANCE) || outVertex >= uint(MAX_ANIMATED_VERTEX))
            break;

        uint sourceVertex = sourceVertexBase + localVertex;

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
            uint shift = i * 8;
            f16_3x4 bone = LoadBone(sBoneMtx, boneStart + ((joints >> shift) & 0xFFu));
            animMat[0] = mad(weights[i], bone[0], animMat[0]);
            animMat[1] = mad(weights[i], bone[1], animMat[1]);
            animMat[2] = mad(weights[i], bone[2], animMat[2]);
        }

        // mul(Vector, Transpose(Matrix)) is equivalent to mul(Matrix, Vector) in HLSL.
        f16_3 localPos = insScale * QMulVec3(insRot, mul(animMat, inputPos));

        f16_3x3 tbn;
        UnpackNormalTangent(input.tangentSpace, tbn[2], tbn[1]);
        f16 tangentHandedness = UnpackTangentHandedness(input.tangentSpace);

        tbn[2] = QMulVec3(insRot, mul(animMat, f16_4(tbn[2], f16(0.0))));
        tbn[1] = QMulVec3(insRot, mul(animMat, f16_4(tbn[1], f16(0.0))));
        tbn[0] = Orthonormalize(tbn[1], tbn[2]);

        AnimatedVert o;
        uint2 packedAnimated = PackAnimatedVertex(localPos, tbn[2], tbn[1], tangentHandedness);
        o.packed0 = packedAnimated.x;
        o.packed1 = packedAnimated.y;
        sAnimatedVert[outVertex] = o;
    }
}