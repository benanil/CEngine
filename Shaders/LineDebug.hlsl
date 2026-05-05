struct VSInput
{
    float3 pos : POSITION0;
    uint color : COLOR;
};

struct VSOutput
{
    float4 position  : SV_Position;
    float3 color     : COLOR;
};

cbuffer vs_params : register(b0, space1)
{
    float4x4 uViewProj;
};

float3 UnpackColor3Uint(uint color)
{
    const float tof1 = 1.0 / 255.0;
    return float3(
        (float)((color >> 0)  & 0xFF),
        (float)((color >> 8)  & 0xFF),
        (float)((color >> 16) & 0xFF)
    ) * tof1;
}

VSOutput vert(VSInput i)
{
    VSOutput o;
    o.position = mul(uViewProj, float4(i.pos, 1.0));
    o.color = UnpackColor3Uint(i.color);
    return o;
}

float4 frag(VSOutput i) : SV_Target0
{
    return float4(i.color, 1.0);
}