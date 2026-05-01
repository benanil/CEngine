#include "Common.hlsl"

// shadercross SkinnedFrag.hlsl -s HLSL -d SPIRV -t fragment -o SkinnedFrag.spv
// bin2c -o SkinnedFrag.spv.h SkinnedFrag.spv

struct VSOutput
{
    float4   position  : SV_Position;
    f16_2_io texCoords : TEXCOORD0;
    f16_3_io normal    : NORMAL;
};

Texture2D<float4> Texture : register(t0, space2);
SamplerState Sampler : register(s0, space2);

float4 main(VSOutput input) : SV_Target0
{
    f16_3 sunDir = f16_3(-0.5, -0.5, 0.0f);
    f16_io ndl = dot(input.normal, sunDir);
    return Texture.Sample(Sampler, input.texCoords) * max(ndl, 0.1);
}