#ifndef TEXTURE_SAMPLING_H
#define TEXTURE_SAMPLING_H

#include "Common.hlsl"
#include "CommonStructs.hlsl"

#ifndef TEXTURE_PAGE_SIZE_F
#define TEXTURE_PAGE_SIZE_F 4096.0f
#endif

float2 TexturePageRepeatUV(TextureDescriptor desc, float2 uv)
{
    float2 halfTexel = float2(0.5f / TEXTURE_PAGE_SIZE_F, 0.5f / TEXTURE_PAGE_SIZE_F);
    return desc.uvBias + lerp(halfTexel, desc.uvScale - halfTexel, frac(uv));
}

f16_4 SampleTexturePageRGBA(Texture2DArray<float4> pages, SamplerState samplerState, TextureDescriptor desc, float2 uv)
{
    float2 atlasUV = TexturePageRepeatUV(desc, uv);
    float2 atlasDdx = ddx(uv) * desc.uvScale;
    float2 atlasDdy = ddy(uv) * desc.uvScale;
    return f16_4(pages.SampleGrad(samplerState, float3(atlasUV, desc.pageIndex), atlasDdx, atlasDdy));
}

f16_3 SRGBToLinear(f16_3 srgb)
{
    return srgb * (srgb * (srgb * f16(0.305306011) + f16(0.682171111)) + f16(0.012522878));
}

f16_2 SampleTexturePageRG(Texture2DArray<float2> pages, SamplerState samplerState, TextureDescriptor desc, float2 uv)
{
    float2 atlasUV = TexturePageRepeatUV(desc, uv);
    float2 atlasDdx = ddx(uv) * desc.uvScale;
    float2 atlasDdy = ddy(uv) * desc.uvScale;
    return f16_2(pages.SampleGrad(samplerState, float3(atlasUV, desc.pageIndex), atlasDdx, atlasDdy));
}

#endif
