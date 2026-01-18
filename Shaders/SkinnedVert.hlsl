
// shadercross SkinnedVert.hlsl -s HLSL -d SPIRV -t vertex -o SkinnedVert.spv
// bin2c -o SkinnedVert.spv.h SkinnedVert.spv

cbuffer vs_params : register(b0, space1)
{
    float4x4 uViewProj;
};

struct VSInput
{
    float3 aPos       : POSITION0;
    float3 aNormal    : NORMAL0;
    float4 aTangent   : TANGENT0;
    float2 aTexCoords : TEXCOORD0;
    uint4  aJoints    : BLENDINDICES0;
    float4 aWeights   : BLENDWEIGHT0;
};

struct VSOutput
{
    float4 position  : SV_Position;
    float2 texCoords : TEXCOORD0;
    float3 normal    : NORMAL;
};

VSOutput main(VSInput input)
{
    VSOutput o;
    o.texCoords = input.aTexCoords;
    o.position = mul(uViewProj, float4(input.aPos, 1.0));
    o.normal = input.aNormal;
    return o;
}