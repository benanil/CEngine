#ifndef TEXTURE_SAMPLING_H
#define TEXTURE_SAMPLING_H

#include "CommonStructs.hlsl"

#ifndef TEXTURE_PAGE_SIZE_F
#define TEXTURE_PAGE_SIZE_F 4096.0f
#endif

float2 TexturePageRepeatUV(TextureDescriptor desc, float2 uv)
{
    float2 halfTexel = desc.uvScale * (0.5f / TEXTURE_PAGE_SIZE_F);
    return desc.uvBias + lerp(halfTexel, desc.uvScale - halfTexel, frac(uv));
}

float2 TexturePageRepeatUV_Aggressive(TextureDescriptor desc, float2 uv)
{
    // A strict 1.5 to 2-texel safe inset. Since BC7 artifacts live in 4x4 blocks,
    // stepping inside by 1.5 texels covers the trilinear blend region perfectly.
    float2 safePadding = desc.uvScale * (1.5f / TEXTURE_PAGE_SIZE_F);
    return desc.uvBias + lerp(safePadding, desc.uvScale - safePadding, frac(uv));
}

// Gradient-explicit variants. The vis-buffer materialize compute pass has no screen-space
// derivatives, so it computes analytic uv gradients and passes them here. The ddx/ddy-based
// wrappers below delegate to these so both paths share the exact same sampling math.
f16_4 SampleTexturePageRGBAGrad(Texture2DArray<float4> pages, SamplerState samplerState, TextureDescriptor desc, float2 uv, float2 duvdx, float2 duvdy)
{
    float2 dx = duvdx;
    float2 dy = duvdy;
    dx = dx - round(dx);
    dy = dy - round(dy);
    float2 atlasUV = TexturePageRepeatUV_Aggressive(desc, uv);
    // HACK: Artificially sharpen the texture at grazing angles by crushing the derivatives.
    // This tricks the hardware into pulling from a sharper mip level where your
    // safe padding is still wide enough to prevent background bleeding.
    float2 atlasDdx = dx * desc.uvScale * 0.3f;
    float2 atlasDdy = dy * desc.uvScale * 0.3f;
    return f16_4(pages.SampleGrad(samplerState, float3(atlasUV, desc.pageIndex), atlasDdx, atlasDdy));
}

f16_2 SampleTexturePageRGGrad(Texture2DArray<float2> pages, SamplerState samplerState, TextureDescriptor desc, float2 uv, float2 duvdx, float2 duvdy)
{
    float2 atlasUV = TexturePageRepeatUV(desc, uv);
    float2 atlasDdx = duvdx * desc.uvScale;
    float2 atlasDdy = duvdy * desc.uvScale;
    return f16_2(pages.SampleGrad(samplerState, float3(atlasUV, desc.pageIndex), atlasDdx, atlasDdy));
}

f16_4 SampleTexturePageRGBA(Texture2DArray<float4> pages, SamplerState samplerState, TextureDescriptor desc, float2 uv)
{
    return SampleTexturePageRGBAGrad(pages, samplerState, desc, uv, ddx(uv), ddy(uv));
}

f16_2 SampleTexturePageRG(Texture2DArray<float2> pages, SamplerState samplerState, TextureDescriptor desc, float2 uv)
{
    return SampleTexturePageRGGrad(pages, samplerState, desc, uv, ddx(uv), ddy(uv));
}

f16_3 SRGBToLinear(f16_3 srgb)
{
    // return srgb * (srgb * (srgb * f16(0.305306011) + f16(0.682171111)) + f16(0.012522878));
    float3 s = float3(srgb);
    float3 lo = s / 12.92f;
    float3 hi = pow((s + 0.055f) / 1.055f, 2.4f);
    return f16_3(select(s <= 0.04045f, lo, hi));
}

#endif
