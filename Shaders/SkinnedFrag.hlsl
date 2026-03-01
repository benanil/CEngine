// shadercross SkinnedFrag.hlsl -s HLSL -d SPIRV -t fragment -o SkinnedFrag.spv
// bin2c -o SkinnedFrag.spv.h SkinnedFrag.spv

struct VSOutput
{
    float4 position  : SV_Position;
    half2  texCoords : TEXCOORD0;
    half3  normal    : NORMAL;
};

Texture2D<float4> Texture : register(t0, space2);
SamplerState Sampler : register(s0, space2);

float4 main(VSOutput input) : SV_Target0
{
    half3 sunDir = half3(-0.5, -0.5, 0.0f);
    half ndl = dot(input.normal, sunDir);
    return Texture.Sample(Sampler, input.texCoords) * max(ndl, 0.1);
}