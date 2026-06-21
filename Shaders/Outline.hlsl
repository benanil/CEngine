// editor selection outline, ported from the old engine's OutlineVert/OutlineFrag:
// the mesh grows along its normals in the vertex shader and draws flat green.
// rendered as an inverted hull (front face culling) with depth test instead of the
// old stencil masking, the scene depth hides the interior
#include "Bitpack.hlsl"
#include "Math.hlsl"

cbuffer vs_params : register(b0, space1)
{
    float4x4 uViewProj;
    float4 uPosition;  // entity world position
    float4 uRotation;  // entity rotation quaternion
    float4 uScaleBias; // xyz entity scale, w local space outline thickness
    float4 uAABBMin;   // primitive AABB, to de-quantize the unorm16 position
    float4 uAABBMax;
};

struct VSInput
{
    uint2    aPos          : POSITION0;
    uint     aTangentSpace : TANGENT0;
    f16_2_io aTexCoords    : TEXCOORD0;
};

float4 vert(VSInput input) : SV_Position
{
    f16_3 normal, tangent;
    UnpackNormalTangent(input.aTangentSpace, normal, tangent);

    // grow the mesh along the normal like the old engine's outline shader
    float3 decodedPos = uAABBMin.xyz + UnpackUnorm16x4(input.aPos).xyz * (uAABBMax.xyz - uAABBMin.xyz);
    float3 localPos = decodedPos + float3(normal) * uScaleBias.w;
    float3 worldPos = QMulVec3F32(uRotation, localPos * uScaleBias.xyz) + uPosition.xyz;
    return mul(uViewProj, float4(worldPos, 1.0));
}

float4 frag() : SV_Target0
{
    return float4(0.22, 0.77, 0.22, 1.0);
}
