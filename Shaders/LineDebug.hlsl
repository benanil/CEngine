#include "Bitpack.hlsl"

struct VSInput
{
    float3 pos : POSITION0;
    uint color : COLOR;
};

struct VSOutput
{
    float4 position  : SV_Position;
    f16_3_io color   : COLOR;
};

cbuffer vs_params : register(b0, space1)
{
    float4x4 uViewProj;
};

VSOutput vert(VSInput i)
{
    VSOutput o;
    o.position = mul(uViewProj, float4(i.pos, 1.0));
    o.color = UnpackColor3Uint(i.color);
    return o;
}

float4 frag(VSOutput i) : SV_Target0
{
    return float4(f16_3(i.color), f16(1.0));
}
