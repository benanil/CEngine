#ifndef TEXTURE_SAMPLING_H
#define TEXTURE_SAMPLING_H

#include "CommonStructs.hlsl"

#ifndef TEXTURE_PAGE_SIZE_F
#define TEXTURE_PAGE_SIZE_F 4096.0f
#endif

float2 TexturePageRepeatUV(TextureDescriptor desc, float2 uv)
{
    float2 halfTexel = float2(0.5f / TEXTURE_PAGE_SIZE_F, 0.5f / TEXTURE_PAGE_SIZE_F);
    return desc.uvBias + lerp(halfTexel, desc.uvScale - halfTexel, frac(uv));
}

float4 SampleTexturePageRGBA(Texture2DArray<float4> pages, SamplerState samplerState, TextureDescriptor desc, float2 uv)
{
    float2 atlasUV = TexturePageRepeatUV(desc, uv);
    float2 atlasDdx = ddx(uv) * desc.uvScale;
    float2 atlasDdy = ddy(uv) * desc.uvScale;
    return pages.SampleGrad(samplerState, float3(atlasUV, desc.pageIndex), atlasDdx, atlasDdy);
}

float3 SRGBToLinear(float3 srgb)
{
    return srgb * (srgb * (srgb * 0.305306011f + 0.682171111f) + 0.012522878f);
}

float2 SampleTexturePageRG(Texture2DArray<float2> pages, SamplerState samplerState, TextureDescriptor desc, float2 uv)
{
    float2 atlasUV = TexturePageRepeatUV(desc, uv);
    float2 atlasDdx = ddx(uv) * desc.uvScale;
    float2 atlasDdy = ddy(uv) * desc.uvScale;
    return pages.SampleGrad(samplerState, float3(atlasUV, desc.pageIndex), atlasDdx, atlasDdy);
}

#endif
