
#include <SDL3/SDL_gpu.h>
#include "Include/Memory.h"
#include "Extern/basis_universal/basisu_transcoder.h"

static basist::transcoder_texture_format BasisTexToTranscoderFormat(basist::basis_tex_format fmt, 
                                                                    bool hasAlpha, bool isNormal, bool isMetalicRoughness)
{
    #ifndef __ANDROID__
    if (isNormal || isMetalicRoughness) 
        return basist::transcoder_texture_format::cTFBC5_RG;
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

static SDL_GPUTextureFormat BasisToSDLPixelFormat(basist::transcoder_texture_format fmt) 
{
    switch (fmt)
    {
        case basist::transcoder_texture_format::cTFBC1_RGB:   return SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM;
        case basist::transcoder_texture_format::cTFBC3_RGBA:  return SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM;
        case basist::transcoder_texture_format::cTFBC4_R:     return SDL_GPU_TEXTUREFORMAT_BC4_R_UNORM;
        case basist::transcoder_texture_format::cTFBC5_RG:    return SDL_GPU_TEXTUREFORMAT_BC5_RG_UNORM;
        case basist::transcoder_texture_format::cTFBC7_RGBA:  return SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM;

        case basist::transcoder_texture_format::cTFETC2_RGBA: return SDL_GPU_TEXTUREFORMAT_ETC2_RGB8A8_UNORM;
        case basist::transcoder_texture_format::cTFETC1_RGB:  return SDL_GPU_TEXTUREFORMAT_ETC2_RGB8_UNORM;
        
        case basist::transcoder_texture_format::cTFRGBA32:    return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

        case basist::transcoder_texture_format::cTFASTC_4x4_RGBA: return SDL_GPU_TEXTUREFORMAT_ASTC_4x4_UNORM;

        default: return SDL_GPU_TEXTUREFORMAT_INVALID;
    }
}

extern "C"
{

void BasisuSetup(void)
{
    basist::basisu_transcoder_init();
}

void BasisuShutdown(void) {

}


typedef struct ImageLevelDesc_
{
    uint32_t width, height, blocks;
} ImageLevelDesc;

SDL_GPUTexture* BasisuMakeImage(
    const void* basisu_data, 
    uint64_t size, 
    int* width,
    int* height, 
    SDL_GPUTextureFormat* format,
    SDL_GPUDevice* gpuDevice,
    bool isNormal, 
    bool isMetallicRoughness) 
{
    basist::basisu_transcoder         transcoder;
    basist::basisu_image_info         img_info;
    basist::basis_tex_format          basisTexFmt;
    basist::transcoder_texture_format fmt;

    SDL_GPUCommandBuffer*           uploadCmdBuf;
    SDL_GPUCopyPass*                copyPass;
    SDL_GPUTexture*                 result;
    SDL_GPUTransferBuffer*          textureTransferBuffer;
    SDL_GPUFence*                   fence;
    SDL_GPUTextureCreateInfo        texDesc;
    SDL_GPUTransferBufferCreateInfo transferBufferCreateInfo;
    SDL_GPUTextureTransferInfo      textureTransferInfo;
    SDL_GPUTextureRegion            textureRegion;
    
    Uint8* textureTransferPtr;
    Uint8* ptr;
    
    uint32_t required_size;
    uint32_t bytes_per_block;
    size_t  imageDataLength;

    ImageLevelDesc* levelDesc;
    ImageLevelDesc imageLevelDescriptions[16];
    bool res;
    
    SDL_memset(&transferBufferCreateInfo, 0, sizeof(SDL_GPUTransferBufferCreateInfo));
    SDL_memset(&textureTransferInfo     , 0, sizeof(SDL_GPUTextureTransferInfo));
    SDL_memset(&textureRegion           , 0, sizeof(SDL_GPUTextureRegion));
    SDL_memset(imageLevelDescriptions   , 0xCD, sizeof(imageLevelDescriptions));

    transcoder.start_transcoding(basisu_data, (uint32_t)size);
    transcoder.get_image_info(basisu_data, (uint32_t)size, img_info, 0);
    
    basisTexFmt = transcoder.get_basis_tex_format(basisu_data, (uint32_t)size);
    fmt         = BasisTexToTranscoderFormat(basisTexFmt, img_info.m_alpha_flag, isNormal, isMetallicRoughness);
    bytes_per_block = basist::basis_get_bytes_per_block_or_pixel(fmt);
    
    imageDataLength = 0;
    for (int i = 0; i < img_info.m_total_levels; i++)
    {
        levelDesc = &imageLevelDescriptions[i];
        transcoder.get_image_level_desc(basisu_data, (uint32_t)size, 0, i, levelDesc->width, levelDesc->height, levelDesc->blocks);
        imageDataLength += levelDesc->blocks * bytes_per_block;
    }
    
    uploadCmdBuf = SDL_AcquireGPUCommandBuffer(gpuDevice);
    copyPass     = SDL_BeginGPUCopyPass(uploadCmdBuf);
    
    texDesc.type                 = SDL_GPU_TEXTURETYPE_2D;
    texDesc.format               = BasisToSDLPixelFormat(fmt);
    texDesc.width                = img_info.m_width;
    texDesc.height               = img_info.m_height;
    texDesc.layer_count_or_depth = 1;
    texDesc.num_levels           = (int)basisu::minimum(img_info.m_total_levels, 8u);
    texDesc.sample_count         = SDL_GPU_SAMPLECOUNT_1;
    texDesc.usage                = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    texDesc.props                = 0;
    
    result = SDL_CreateGPUTexture(gpuDevice, &texDesc);
    // CHECK_CREATE(result, "Compressed Texture");
    
    transferBufferCreateInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferBufferCreateInfo.size  = imageDataLength;
    
    textureTransferBuffer = SDL_CreateGPUTransferBuffer(gpuDevice, &transferBufferCreateInfo);
    textureTransferPtr    = (Uint8*)SDL_MapGPUTransferBuffer(gpuDevice, textureTransferBuffer, false);
    ptr = textureTransferPtr;
    
    for (int i = 0; i < texDesc.num_levels; i++) 
    {
        levelDesc = &imageLevelDescriptions[i];
        required_size = levelDesc->blocks * bytes_per_block;
        res = transcoder.transcode_image_level(basisu_data, (uint32_t)size, 0, i, ptr, required_size / bytes_per_block, fmt, 0);  
        assert(res);
        ptr += required_size;
    }
    
    SDL_UnmapGPUTransferBuffer(gpuDevice, textureTransferBuffer);
    imageDataLength = 0;
    
    for (int i = 0; i < texDesc.num_levels; i++)
    {
        levelDesc = &imageLevelDescriptions[i];
        
        textureTransferInfo.transfer_buffer = textureTransferBuffer;
        textureTransferInfo.offset = imageDataLength;
        
        textureRegion.texture = result;
        textureRegion.w = levelDesc->width;
        textureRegion.h = levelDesc->height;
        textureRegion.d = 1;
        textureRegion.mip_level = i;
        
        SDL_UploadToGPUTexture(copyPass, &textureTransferInfo, &textureRegion, false);
        imageDataLength += levelDesc->blocks * bytes_per_block;
    }
    
    SDL_ReleaseGPUTransferBuffer(gpuDevice, textureTransferBuffer);
    SDL_EndGPUCopyPass(copyPass);
    fence = SDL_SubmitGPUCommandBufferAndAcquireFence(uploadCmdBuf);
    
    SDL_WaitForGPUFences(gpuDevice, true, &fence, 1);
    SDL_ReleaseGPUFence(gpuDevice, fence);
    
    *width  = img_info.m_width;
    *height = img_info.m_height;
    *format = texDesc.format;
    
    return result;
}

} // extern c