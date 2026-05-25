
#include <SDL3/SDL_gpu.h>
#include "Include/Memory.h"
#include "Include/BasisBinding.h"
#include "Extern/basis_universal/transcoder/basisu_transcoder.h"

static basist::transcoder_texture_format SDLFormatToBasisTranscoderFormat(SDL_GPUTextureFormat format)
{
    switch (format)
    {
        case SDL_GPU_TEXTUREFORMAT_BC5_RG_UNORM:   return basist::transcoder_texture_format::cTFBC5_RG;
        case SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM: return basist::transcoder_texture_format::cTFBC7_RGBA;
        case SDL_GPU_TEXTUREFORMAT_ASTC_4x4_UNORM: return basist::transcoder_texture_format::cTFASTC_4x4_RGBA;
        case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM: return basist::transcoder_texture_format::cTFRGBA32;
        default: return basist::transcoder_texture_format::cTFTotalTextureFormats;
    }
}

static basist::transcoder_texture_format BasisTexToTranscoderFormat(basist::basis_tex_format fmt, 
                                                                    bool hasAlpha, bool isNormal, bool isMetalicRoughness)
{
    if (fmt == basist::basis_tex_format::cETC1S)
    {
        #ifdef __ANDROID__
        return hasAlpha ? basist::transcoder_texture_format::cTFETC2_RGBA : basist::transcoder_texture_format::cTFETC1_RGB;
        #else
        return hasAlpha ? basist::transcoder_texture_format::cTFBC3_RGBA : basist::transcoder_texture_format::cTFBC1_RGB;
        #endif
    }
    else if (fmt == basist::basis_tex_format::cUASTC_LDR_4x4)
    {
        #ifdef __ANDROID__
        return basist::transcoder_texture_format::cTFASTC_4x4_RGBA;
        #else
        if (isNormal || isMetalicRoughness)
            return basist::transcoder_texture_format::cTFBC5_RG;
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

        // case basist::transcoder_texture_format::cTFETC2_RGBA: return SDL_GPU_TEXTUREFORMAT_ETC2_RGB8A8_UNORM;
        // case basist::transcoder_texture_format::cTFETC1_RGB:  return SDL_GPU_TEXTUREFORMAT_ETC2_RGB8_UNORM;
        
        case basist::transcoder_texture_format::cTFRGBA32:    return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

        case basist::transcoder_texture_format::cTFASTC_4x4_RGBA: return SDL_GPU_TEXTUREFORMAT_ASTC_4x4_UNORM;

        default: return SDL_GPU_TEXTUREFORMAT_INVALID;
    }
}

static bool IsBlockCompressedSDLFormat(SDL_GPUTextureFormat format)
{
    switch (format)
    {
        case SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM:
        case SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM:
        case SDL_GPU_TEXTUREFORMAT_BC4_R_UNORM:
        case SDL_GPU_TEXTUREFORMAT_BC5_RG_UNORM:
        case SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM:
        case SDL_GPU_TEXTUREFORMAT_ASTC_4x4_UNORM:
            return true;
        default:
            return false;
    }
}

extern "C"
{
extern SDL_GPUDevice* g_GPUDevice;

void BasisuSetup(void)
{
    basist::basisu_transcoder_init();
}

void BasisuShutdown(void) {

}

bool BasisuTranscodeImage(
    const void* basisu_data,
    uint64_t size,
    SDL_GPUTextureFormat targetFormat,
    BasisuTranscodedImage* outImage)
{
    if (!basisu_data || !size || !outImage) return false;

    SDL_memset(outImage, 0, sizeof(*outImage));

    basist::transcoder_texture_format fmt = SDLFormatToBasisTranscoderFormat(targetFormat);
    if (fmt == basist::transcoder_texture_format::cTFTotalTextureFormats) return false;

    basist::basisu_transcoder transcoder;
    basist::basisu_image_info img_info;
    if (!transcoder.start_transcoding(basisu_data, (uint32_t)size)) return false;
    if (!transcoder.get_image_info(basisu_data, (uint32_t)size, img_info, 0)) return false;

    uint32_t mipLevels = basisu::minimum(img_info.m_total_levels, (uint32_t)BASISU_MAX_TRANSCODED_MIPS);
    uint32_t bytesPerBlock = basist::basis_get_bytes_per_block_or_pixel(fmt);
    uint32_t dataSize = 0;

    for (uint32_t i = 0; i < mipLevels; i++)
    {
        BasisuTranscodedLevel* level = &outImage->levels[i];
        transcoder.get_image_level_desc(basisu_data, (uint32_t)size, 0, i,
                                        level->width, level->height, level->blocks);
        level->offset = dataSize;
        level->size = level->blocks * bytesPerBlock;
        dataSize += level->size;
    }

    void* data = AllocateTLSFGlobal(dataSize);
    if (!data) return false;

    uint8_t* ptr = (uint8_t*)data;
    for (uint32_t i = 0; i < mipLevels; i++)
    {
        BasisuTranscodedLevel* level = &outImage->levels[i];
        bool ok = transcoder.transcode_image_level(basisu_data, (uint32_t)size, 0, i,
                                                   ptr + level->offset,
                                                   level->size / bytesPerBlock,
                                                   fmt, 0);
        if (!ok)
        {
            DeAllocateTLSFGlobal(data);
            SDL_memset(outImage, 0, sizeof(*outImage));
            return false;
        }
    }

    outImage->data = data;
    outImage->dataSize = dataSize;
    outImage->width = img_info.m_width;
    outImage->height = img_info.m_height;
    outImage->mipLevels = mipLevels;
    outImage->bytesPerBlock = bytesPerBlock;
    outImage->format = targetFormat;
    return true;
}

void BasisuFreeTranscodedImage(BasisuTranscodedImage* image)
{
    if (!image) return;
    if (image->data) DeAllocateTLSFGlobal(image->data);
    SDL_memset(image, 0, sizeof(*image));
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
    uint32_t* mipLevels,
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
    SDL_GPUDevice* gpuDevice = g_GPUDevice;
    
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
    fmt = BasisTexToTranscoderFormat(basisTexFmt, img_info.m_alpha_flag, isNormal, isMetallicRoughness);
    bytes_per_block = basist::basis_get_bytes_per_block_or_pixel(fmt);
    
    imageDataLength = 0;
    for (int i = 0; i < img_info.m_total_levels; i++)
    {
        levelDesc = &imageLevelDescriptions[i];
        transcoder.get_image_level_desc(basisu_data, (uint32_t)size, 0, i,
                                        levelDesc->width, levelDesc->height, levelDesc->blocks);
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
    textureTransferPtr = (Uint8*)SDL_MapGPUTransferBuffer(gpuDevice, textureTransferBuffer, false);
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
        uint32_t mipWidth = basisu::maximum(texDesc.width >> i, 1u);
        uint32_t mipHeight = basisu::maximum(texDesc.height >> i, 1u);
        
        textureRegion.texture = result;
        textureRegion.w = IsBlockCompressedSDLFormat(texDesc.format) ? mipWidth : levelDesc->width;
        textureRegion.h = IsBlockCompressedSDLFormat(texDesc.format) ? mipHeight : levelDesc->height;
        textureRegion.d = 1;
        textureRegion.mip_level = i;
        
        SDL_UploadToGPUTexture(copyPass, &textureTransferInfo, &textureRegion, false);
        imageDataLength += levelDesc->blocks * bytes_per_block;
    }
    
    SDL_EndGPUCopyPass(copyPass);
    fence = SDL_SubmitGPUCommandBufferAndAcquireFence(uploadCmdBuf);
    
    SDL_WaitForGPUFences(gpuDevice, true, &fence, 1);
    SDL_ReleaseGPUFence(gpuDevice, fence);
    SDL_ReleaseGPUTransferBuffer(gpuDevice, textureTransferBuffer);
    
    *width  = img_info.m_width;
    *height = img_info.m_height;
    *format = texDesc.format;
    if (mipLevels) *mipLevels = texDesc.num_levels;
    return result;
}

void* BasisuDecodeImageRGBA(
    const void* basisu_data,
    uint64_t size,
    int* width,
    int* height)
{
    basist::basisu_transcoder transcoder;
    basist::basisu_image_info img_info;
    transcoder.start_transcoding(basisu_data, (uint32_t)size);
    transcoder.get_image_info(basisu_data, (uint32_t)size, img_info, 0);

    uint32_t levelWidth = 0, levelHeight = 0, levelBlocks = 0;
    transcoder.get_image_level_desc(basisu_data, (uint32_t)size, 0, 0, levelWidth, levelHeight, levelBlocks);

    size_t outputSize = (size_t)levelWidth * (size_t)levelHeight * 4u;
    void* output = AllocateTLSFGlobal(outputSize);
    if (!output)
        return nullptr;

    bool ok = transcoder.transcode_image_level(basisu_data, (uint32_t)size, 0, 0, output,
                                               (uint32_t)(outputSize / 4u),
                                               basist::transcoder_texture_format::cTFRGBA32, 0);
    if (!ok)
        return nullptr;

    *width = (int)levelWidth;
    *height = (int)levelHeight;
    return output;
}

} // extern c
