// terrain depth prepass: decodes the compact chunk relative vertex and writes linear-z
// to the hi-z color target like SurfaceDepthOnly. terrain is always opaque, no alpha clip
#include "Common.hlsl"

cbuffer vs_params : register(b0, space1)
{
    float4x4 uViewProj;
    float4   uChunkOriginSize;
    float4   uCameraPosition;
    float4   uCameraForward;
};

struct VSInput
{
    uint4 data : TEXCOORD0;
};

struct VSOutput
{
    float4 position : SV_Position;
};

VSOutput vert(VSInput input)
{
    uint qx = input.data.x & 0x1FFFFFu;
    uint qy = (input.data.x >> 21) | ((input.data.y & 0x3FFu) << 11);
    uint qz = (input.data.y >> 10) & 0x1FFFFFu;
    float3 worldPos = float3(qx, qy, qz) * (uChunkOriginSize.w / 524288.0) + uChunkOriginSize.xyz;
    VSOutput o;
    o.position = mul(uViewProj, float4(worldPos, 1.0));
    return o;
}

float frag(VSOutput input) : SV_Target0
{
    return input.position.z;
}

// wireframe overlay fragment: same vertex shader, line fill pipeline, draws after
// lighting into the hdr color target
float4 wireFrag(VSOutput input) : SV_Target0
{
    return float4(0.12, 0.95, 0.35, 1.0);
}
