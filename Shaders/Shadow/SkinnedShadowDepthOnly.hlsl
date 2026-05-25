#include "../Include/RenderLimits.h"
#include "../CommonStructs.hlsl"
#include "../Bitpack.hlsl"
#include "../Math.hlsl"
#include "ShadowCascade.hlsl"

cbuffer vs_params : register(b0, space1)
{
    uint uCascadeIndex;
};

StructuredBuffer<Entity>         sEntities         : register(t0);
StructuredBuffer<PrimitiveGroup> sPrimitiveGroups  : register(t1);
StructuredBuffer<uint>           sDrawSparseIndices : register(t2);
StructuredBuffer<AnimatedVert>   sAnimatedVert     : register(t3);
StructuredBuffer<ShadowCascadeBuffer> sShadowCascades : register(t4);

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
            [[vk::builtin("DrawIndex")]] uint drawID : DRAWINDEX,
            uint vertexId : SV_VertexID) : SV_Position
{
    PrimitiveGroup group = sPrimitiveGroups[drawID];
    uint denseIdx  = sDrawSparseIndices[group.entityOffset + instanceID];
    uint localVertex = vertexId - group.vertexOffset;
    uint sparse = sEntities[denseIdx].sparse;
    uint animatedVertex = sparse * uint(MAX_SKINNED_VERTEX_PER_ANIM_INSTANCE) + group.animatedVertexOffset + localVertex;
    AnimatedVert animated = sAnimatedVert[animatedVertex];
    Entity entity = sEntities[denseIdx];
    f16_3 localPos = UnpackAnimatedPosition(uint2(animated.packed0, animated.packed1));
    float3 finalWorldPos = float3(localPos) + entity.position.xyz;
    return MulShadowCascade(sShadowCascades[0], uCascadeIndex, float4(finalWorldPos, 1.0));
}

float frag(float4 position : SV_Position) : SV_Target0
{
    return position.z;
}
