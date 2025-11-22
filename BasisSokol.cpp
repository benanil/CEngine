
#ifdef __APPLE__
    #include <TargetConditionals.h>
#endif

#if !(TARGET_OS_IPHONE || defined(__EMSCRIPTEN__) || defined(__ANDROID__))
    #define BASISD_SUPPORT_BC7 (0)
#endif

#define BASISD_SUPPORT_PVRTC2 (0)
#define BASISD_SUPPORT_FXT1 (0)
#define BASISD_SUPPORT_ATC (0)
#define BASISD_SUPPORT_KTX2 (0)

#include "Include/Memory.h"

#include "Extern/basis_universal/basisu_transcoder.cpp"
#include "Extern/sokol/sokol_gfx.h"

static basist::transcoder_texture_format BasisTexToTranscoderFormat(basist::basis_tex_format fmt, 
                                                                    bool hasAlpha, bool isNormal, bool isMetalicRoughness)
{
    #ifndef __ANDROID__
    if (isNormal || isMetalicRoughness) return basist::transcoder_texture_format::cTFBC5_RG;
    #endif

    if (fmt == basist::basis_tex_format::cETC1S)
    {
        #ifdef __ANDROID__
        return hasAlpha ? basist::transcoder_texture_format::cTFETC2_RGBA : basist::transcoder_texture_format::cTFETC1_RGB;
        #else
        return hasAlpha ? basist::transcoder_texture_format::cTFBC3_RGBA : basist::transcoder_texture_format::cTFBC1_RGB;
        #endif
    }
    else if (fmt == basist::basis_tex_format::cUASTC4x4)
    {
        #ifdef __ANDROID__
        return basist::transcoder_texture_format::cTFASTC_4x4_RGBA
        #else
        return  basist::transcoder_texture_format::cTFBC7_RGBA;
        #endif
    }
    else
    {
        assert(0);
    }
    return basist::transcoder_texture_format::cTFBC3_RGBA;
}

static sg_pixel_format BasisToSgPixelFormat(basist::transcoder_texture_format fmt) {
    switch (fmt) {
        case basist::transcoder_texture_format::cTFBC1_RGB:   return SG_PIXELFORMAT_BC1_RGBA;
        case basist::transcoder_texture_format::cTFBC3_RGBA:  return SG_PIXELFORMAT_BC3_RGBA;
        case basist::transcoder_texture_format::cTFBC4_R:     return SG_PIXELFORMAT_BC4_R;
        case basist::transcoder_texture_format::cTFBC5_RG:    return SG_PIXELFORMAT_BC5_RG;
        case basist::transcoder_texture_format::cTFBC7_RGBA:  return SG_PIXELFORMAT_BC7_RGBA;

        case basist::transcoder_texture_format::cTFETC2_RGBA: return SG_PIXELFORMAT_ETC2_RGBA8;
        case basist::transcoder_texture_format::cTFETC1_RGB:  return SG_PIXELFORMAT_ETC2_RGB8;
        
        case basist::transcoder_texture_format::cTFRGBA32:    return SG_PIXELFORMAT_RGBA8;

        case basist::transcoder_texture_format::cTFASTC_4x4_RGBA: return SG_PIXELFORMAT_ASTC_4x4_RGBA;

        default: return _SG_PIXELFORMAT_DEFAULT;
    }
}

extern "C"
{

void BasisuSetup(void) {
    basist::basisu_transcoder_init();
}

void BasisuShutdown(void) {
}

sg_image_desc BasisuTranscode(void* basisu_data, uint64_t size, bool isNormal, bool isMetallicRoughness) 
{
    basist::basisu_transcoder transcoder;
    transcoder.start_transcoding(basisu_data, (uint32_t)size);

    basist::basisu_image_info img_info;
    transcoder.get_image_info(basisu_data, (uint32_t)size, img_info, 0);
    basist::basis_tex_format basisTexFmt = transcoder.get_basis_tex_format(basisu_data, (uint32_t)size);
    const basist::transcoder_texture_format fmt = BasisTexToTranscoderFormat(basisTexFmt, img_info.m_alpha_flag, isNormal, isMetallicRoughness);

    sg_image_desc desc = { };
    desc.type = SG_IMAGETYPE_2D;
    desc.width = (int) img_info.m_width;
    desc.height = (int) img_info.m_height;
    desc.num_mipmaps = (int)basisu::minimumu(img_info.m_total_levels, 8u);
    desc.usage.immutable = new int;
    desc.pixel_format = BasisToSgPixelFormat(fmt);
	
    for (int i = 0; i < desc.num_mipmaps; i++) {
        const uint32_t bytes_per_block = basist::basis_get_bytes_per_block_or_pixel(fmt);
        uint32_t orig_width, orig_height, total_blocks;
        transcoder.get_image_level_desc(basisu_data, (uint32_t)size, 0, i, orig_width,
                                        orig_height, total_blocks);
        
        uint32_t required_size = total_blocks * bytes_per_block;
        void* ptr = AllocateTLSFGlobal(required_size);
        desc.data.subimage[0][i].ptr = ptr;
        desc.data.subimage[0][i].size = required_size;
        bool res = transcoder.transcode_image_level(
            basisu_data, (uint32_t)size, 0, i, ptr, required_size / bytes_per_block, fmt, 0);  
        assert(res);
    }
    return desc;
}

void BasisuFree(const sg_image_desc* desc)
{
    assert(desc);
    for (int i = 0; i < desc->num_mipmaps; i++) 
    {
        if (desc->data.subimage[0][i].ptr) 
        {
            DeAllocateTLSFGlobal((void*)desc->data.subimage[0][i].ptr);
        }
    }
}

void BasisuMakeImage(void* basisu_data, uint64_t size, 
                     int* width, int* height, 
                     sg_pixel_format* format, void** buffer, sg_image* handle,
                     bool isNormal, bool isMetallicRoughness) 
{
    sg_image_desc img_desc = BasisuTranscode(basisu_data, size, isNormal, isMetallicRoughness);
    *width = img_desc.width;
    *height = img_desc.height;
    *format = img_desc.pixel_format;
    // *buffer = img_desc.data.subimage[0][0].ptr;
    *handle = sg_make_image(&img_desc);
    BasisuFree(&img_desc);
}

} // extern c