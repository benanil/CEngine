#include "../../Include/RenderLimits.h"
#include "../CommonStructs.hlsl"
#include "../Bitpack.hlsl"
#include "../Math.hlsl"
#include "../AnimatedTransform.hlsl"
#include "Shadow.hlsl"
#include "Entity.hlsl"

StructuredBuffer<Entity>              sEntities           : register(t0);
StructuredBuffer<PrimitiveGroup>      sPrimitiveGroups    : register(t1);
StructuredBuffer<uint>                sDrawSparseIndices  : register(t2);
StructuredBuffer<uint>                sBoneMtx            : register(t3);
StructuredBuffer<PointShadowMatrix>   sPointShadowSides   : register(t4);
StructuredBuffer<PrimitiveGroupLOD>   sPrimitiveGroupLODs : register(t5);

cbuffer vs_params : register(b0, space1)
{
    uint uShadowSideIndex;
};

struct VSInput
{
    f16_4_io  aPos          : POSITION0;
    uint      aTangentSpace : TANGENT0;
    f16_2_io  aTexCoords    : TEXCOORD0;
    uint4     aJoints       : BLENDINDICES0;
    uint      aWeights      : BLENDWEIGHT0;
};

float4 vert(VSInput input,
            uint instanceID : SV_InstanceID,
            [[vk::builtin("DrawIndex")]] uint drawID : DRAWINDEX) : SV_Position
{
    uint primitiveIdx = drawID / MESH_LOD_COUNT;
    uint lod = drawID - primitiveIdx * MESH_LOD_COUNT;
    PrimitiveGroup group = sPrimitiveGroups[primitiveIdx];
    uint denseIdx  = sDrawSparseIndices[lod * uint(MAX_ANIM_INSTANCES) + PrimitiveGroup_EntityOffset(group) + instanceID];
    uint sparse = EntityGetSparseID(sEntities[denseIdx]);
    Entity entity = sEntities[denseIdx];
    f16_4 insRot = EntityGetRotation(entity);
    f16_3 insScale = EntityGetScale(entity);

    f16_3x4 animMat = VertexAnimate(sBoneMtx, sparse * MAX_BONES, input.aJoints, input.aWeights);
    float3 pos = float3(mul(animMat, f16_4(input.aPos.xyz, 1.0)));
    float3 finalWorldPos = AnimatedWorldPos(pos, float4(insRot), float3(insScale), entity.position.xyz);

    return MulPointShadowSide(sPointShadowSides[uShadowSideIndex], float4(finalWorldPos, 1.0));
}

float frag(float4 position : SV_Position) : SV_Target0
{
    return position.z;
}
