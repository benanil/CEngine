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

// Real atlas descriptors are written with flags == 0 (AddDescriptor); the built-in
// fallback descriptors (no texture bound on the material) carry a non-zero class flag.
// Callers use this to substitute a neutral value instead of sampling the empty page.
bool DescriptorBound(TextureDescriptor desc)
{
    return desc.flags == 0u;
}

f16_4 SampleTexturePageRGBA(Texture2DArray<float4> pages, SamplerState samplerState, TextureDescriptor desc, float2 uv, f16_4 fallback)
{
	if (!DescriptorBound(desc)) return fallback;
    float2 dx = ddx(uv);
    float2 dy = ddy(uv);
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

f16_2 SampleTexturePageRG(Texture2DArray<float2> pages, SamplerState samplerState, TextureDescriptor desc, float2 uv, f16_2 fallback)
{
	if (!DescriptorBound(desc)) return fallback;
    float2 atlasUV = TexturePageRepeatUV(desc, uv);
    float2 atlasDdx = ddx(uv) * desc.uvScale;
    float2 atlasDdy = ddy(uv) * desc.uvScale;
    return f16_2(pages.SampleGrad(samplerState, float3(atlasUV, desc.pageIndex), atlasDdx, atlasDdy));
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
