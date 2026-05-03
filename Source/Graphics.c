#ifndef _GRAPHICS_
#define _GRAPHICS_

#include "Include/FileSystem.h"
#include "Include/Platform.h"
#include "Include/Graphics.h"
#include "Include/Algorithm.h"
#include "Include/Memory.h"
#include "Include/BasisBinding.h"

#include "Extern/SDL3/src/video/khronos/vulkan/vulkan.h"

#include "Extern/sinfl.h"
#include "Extern/ufbx.h"
#include <SDL3/SDL_hints.h>

#define STBI_NO_BMP
#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_GIF
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM 

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION

// #define STBI_MALLOC(size)           ( AllocateTLSFGlobal(size) )
// #define STBI_FREE(ptr)              ( DeAllocateTLSFGlobal(ptr) )
// #define STBI_REALLOC(ptr, size)     ( ReAllocateTLSFGlobal(ptr, size) )

#define STBIR_MALLOC(size, c)       ( AllocateTLSFGlobal(size) )
#define STBIR_FREE(ptr, c)          ( (void)(c), DeAllocateTLSFGlobal(ptr) )

#define VULKAN_MAKE_API_VERSION(variant, major, minor, patch) \
((((uint32_t)(variant)) << 29U) | (((uint32_t)(major)) << 22U) | (((uint32_t)(minor)) << 12U) | ((uint32_t)(patch)))

#include "Extern/stb/stb_image.h"
#include "Extern/stb/stb_image_resize2.h"

extern SDL_GPUDevice* g_GPUDevice;
extern WindowState    g_WindowState;
extern RenderState    g_RenderState;
extern SDL_Window*    g_SDLWindow;

Graphics gGFX = {};

extern void Quit(int rc);

void GraphicsInit(bool msaa)
{
    VkPhysicalDeviceVulkan12Features vk12_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = NULL,
        .shaderFloat16 = VK_TRUE,
        .shaderSubgroupExtendedTypes = VK_TRUE,
        .subgroupBroadcastDynamicId = VK_TRUE,
    };

    VkPhysicalDeviceVulkan11Features vk11_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
        .pNext = &vk12_features,
        // this is supported on mobile devices and amd latest
        // .storageInputOutput16 = VK_TRUE,
    };

    VkPhysicalDeviceFeatures vk10_features = {
        .shaderInt16 = VK_TRUE,
    };

    SDL_GPUVulkanOptions options = {
        .vulkan_api_version = VULKAN_MAKE_API_VERSION(0, 1, 2, 0),
        .feature_list = &vk11_features,
        .vulkan_10_physical_device_features = &vk10_features
    };

    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetHint(SDL_HINT_GPU_DRIVER, "vulkan");
    SDL_SetPointerProperty(props, SDL_PROP_GPU_DEVICE_CREATE_VULKAN_OPTIONS_POINTER, &options);
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, true);
    SDL_SetStringProperty(props, SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING, "vulkan");
    g_GPUDevice = SDL_CreateGPUDeviceWithProperties(props);
    SDL_DestroyProperties(props);

    CHECK_CREATE(g_GPUDevice, "GPU device");
    /* Claim the windows */
    SDL_ClaimWindowForGPUDevice(g_GPUDevice, g_SDLWindow);
    
    /* Determine which sample count to use */
    g_RenderState.sample_count = SDL_GPU_SAMPLECOUNT_1;
    if (msaa && SDL_GPUTextureSupportsSampleCount(
        g_GPUDevice,
        SDL_GetGPUSwapchainTextureFormat(g_GPUDevice, g_SDLWindow),
        SDL_GPU_SAMPLECOUNT_4)) 
    {
        g_RenderState.sample_count = SDL_GPU_SAMPLECOUNT_4;
    }
    
    int drawablew, drawableh;
    /* Set up per-window state */
    WindowState* winstate = &g_WindowState;
    /* create a depth texture for the window */
    SDL_GetWindowSizeInPixels(g_SDLWindow, (int*)&drawablew, (int*)&drawableh);
    winstate->tex_depth   = CreateDepthTexture(drawablew, drawableh);
    winstate->tex_msaa    = CreateMSAATexture(drawablew, drawableh);
    winstate->tex_resolve = CreateResolveTexture(drawablew, drawableh);
    
    gGFX.VertexBuffer = AllocAligned(sizeof(ASkinedVertex) * MAX_VERTEX, 4);
    gGFX.IndexBuffer  = AllocAligned(sizeof(u32) * MAX_INDEX + 16, 4); // 16->give little bit of space for memcpy
}

void GraphicsDestroy()
{
    WindowState* winstate = &g_WindowState;
    SDL_ReleaseGPUTexture(g_GPUDevice, winstate->tex_depth);
    SDL_ReleaseGPUTexture(g_GPUDevice, winstate->tex_msaa);
    SDL_ReleaseGPUTexture(g_GPUDevice, winstate->tex_resolve);
    SDL_ReleaseWindowFromGPUDevice(g_GPUDevice, g_SDLWindow);

    SDL_DestroyGPUDevice(g_GPUDevice);
}

SDL_GPUBuffer* CreateBuffer(
    const void*          buffer,       // may be NULL
    size_t               bufferSize,
    SDL_GPUBufferUsageFlags bufferUsage,
    const char*          debugName)
{
    // Create the GPU buffer (no initial data)
    SDL_GPUBufferCreateInfo buffer_desc = {
        .usage = bufferUsage,
        .size  = bufferSize,
        .props = SDL_CreateProperties()
    };
    SDL_SetStringProperty(buffer_desc.props, SDL_PROP_GPU_BUFFER_CREATE_NAME_STRING, debugName);
    SDL_GPUBuffer* gpu_buffer = SDL_CreateGPUBuffer(g_GPUDevice, &buffer_desc);
    CHECK_CREATE(gpu_buffer, debugName);
    SDL_DestroyProperties(buffer_desc.props);

    // If no initial data was provided, we're done
    if (buffer == NULL) {
        return gpu_buffer;
    }

    // --- Upload initial data using a transfer buffer ---
    SDL_GPUTransferBufferCreateInfo transfer_desc = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size  = bufferSize,
        .props = SDL_CreateProperties()
    };
    SDL_SetStringProperty(transfer_desc.props, SDL_PROP_GPU_TRANSFERBUFFER_CREATE_NAME_STRING, "Upload Transfer");
    SDL_GPUTransferBuffer* upload_buf = SDL_CreateGPUTransferBuffer(g_GPUDevice, &transfer_desc);
    CHECK_CREATE(upload_buf, "Transfer buffer");
    SDL_DestroyProperties(transfer_desc.props);

    // Map and copy data
    void* map = SDL_MapGPUTransferBuffer(g_GPUDevice, upload_buf, false);
    SDL_memcpy(map, buffer, bufferSize);
    SDL_UnmapGPUTransferBuffer(g_GPUDevice, upload_buf);

    // Copy to GPU buffer
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(g_GPUDevice);
    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation src_location = {
        .transfer_buffer = upload_buf,
        .offset = 0
    };
    SDL_GPUBufferRegion dst_region = {
        .buffer = gpu_buffer,
        .offset = 0,
        .size   = bufferSize
    };
    SDL_UploadToGPUBuffer(copy_pass, &src_location, &dst_region, false);
    SDL_EndGPUCopyPass(copy_pass);
    SDL_SubmitGPUCommandBuffer(cmd);

    SDL_ReleaseGPUTransferBuffer(g_GPUDevice, upload_buf);
    return gpu_buffer;
}

void UpdateGPUBuffer(SDL_GPUBuffer* buffer, const void* data, size_t bufferSize, size_t offset)
{
    SDL_GPUTransferBufferCreateInfo transferBufferCreateInfo;
    transferBufferCreateInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferBufferCreateInfo.size  = bufferSize;
    transferBufferCreateInfo.props = 0;

    SDL_GPUTransferBuffer* boneTransferBuffer;
    SDL_GPUCopyPass* copyPass;
    Uint8* boneTransferPtr;
    SDL_GPUCommandBuffer* uploadCmdBuf;

    uploadCmdBuf = SDL_AcquireGPUCommandBuffer(g_GPUDevice);
    copyPass = SDL_BeginGPUCopyPass(uploadCmdBuf);
    
    boneTransferBuffer = SDL_CreateGPUTransferBuffer(g_GPUDevice, &transferBufferCreateInfo);
    boneTransferPtr = (Uint8*)SDL_MapGPUTransferBuffer(g_GPUDevice, boneTransferBuffer, true);
    MemCopy(boneTransferPtr, data, bufferSize);
    SDL_UnmapGPUTransferBuffer(g_GPUDevice, boneTransferBuffer);

    SDL_UploadToGPUBuffer(copyPass,
                          &(SDL_GPUTransferBufferLocation) { .transfer_buffer = boneTransferBuffer, .offset = 0}, 
                          &(SDL_GPUBufferRegion) 
                          {
                              .buffer = buffer, 
                              .offset = offset, 
                              .size = bufferSize
                          }, 
                          true);
    
    SDL_EndGPUCopyPass(copyPass);
    
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(uploadCmdBuf);
    SDL_WaitForGPUFences(g_GPUDevice, true, &fence, 1);
    SDL_ReleaseGPUFence(g_GPUDevice, fence);
    SDL_ReleaseGPUTransferBuffer(g_GPUDevice, boneTransferBuffer);
}


SDL_GPUTexture* CreateDepthTexture(Uint32 drawablew, Uint32 drawableh)
{
    SDL_GPUTextureCreateInfo createinfo;
    SDL_GPUTexture* result;
    
    createinfo.type = SDL_GPU_TEXTURETYPE_2D;
    createinfo.format = SDL_GPU_TEXTUREFORMAT_D24_UNORM;
    createinfo.width = drawablew;
    createinfo.height = drawableh;
    createinfo.layer_count_or_depth = 1;
    createinfo.num_levels = 1;
    createinfo.sample_count = g_RenderState.sample_count;
    createinfo.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    createinfo.props = 0;
    
    result = SDL_CreateGPUTexture(g_GPUDevice, &createinfo);
    CHECK_CREATE(result, "Depth Texture")
    return result;
}

SDL_GPUTexture* CreateMSAATexture(Uint32 drawablew, Uint32 drawableh)
{
    SDL_GPUTextureCreateInfo createinfo;
    SDL_GPUTexture* result;
    
    if (g_RenderState.sample_count == SDL_GPU_SAMPLECOUNT_1) {
    	return NULL;
    }
    
    createinfo.type = SDL_GPU_TEXTURETYPE_2D;
    createinfo.format = SDL_GetGPUSwapchainTextureFormat(g_GPUDevice, g_SDLWindow);
    createinfo.width = drawablew;
    createinfo.height = drawableh;
    createinfo.layer_count_or_depth = 1;
    createinfo.num_levels = 1;
    createinfo.sample_count = g_RenderState.sample_count;
    createinfo.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    createinfo.props = 0;
    
    result = SDL_CreateGPUTexture(g_GPUDevice, &createinfo);
    CHECK_CREATE(result, "MSAA Texture")
    return result;
}

SDL_GPUTexture* CreateResolveTexture(Uint32 drawablew, Uint32 drawableh)
{
    SDL_GPUTextureCreateInfo createinfo;
    SDL_GPUTexture* result;
    
    if (g_RenderState.sample_count == SDL_GPU_SAMPLECOUNT_1) {
    	return NULL;
    }
    
    createinfo.type = SDL_GPU_TEXTURETYPE_2D;
    createinfo.format = SDL_GetGPUSwapchainTextureFormat(g_GPUDevice, g_SDLWindow);
    createinfo.width = drawablew;
    createinfo.height = drawableh;
    createinfo.layer_count_or_depth = 1;
    createinfo.num_levels = 1;
    createinfo.sample_count = SDL_GPU_SAMPLECOUNT_1;
    createinfo.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    createinfo.props = 0;
    
    result = SDL_CreateGPUTexture(g_GPUDevice, &createinfo);
    CHECK_CREATE(result, "Resolve Texture")
    return result;
}

static void MakeRGBAFromRGB(const unsigned char* RESTRICT from, unsigned char* rgba, int numPixels)
{
    for (int i = 0; i < numPixels; i++)
    {
        MemsetZero(rgba, 4 * sizeof(char));
        rgba[0] = from[0];
        rgba[1] = from[1];
        rgba[2] = from[2];
        rgba[3] = 255;

        from += 3;
        rgba += 4;
    }
}


Texture rImportTexture(const char* path, TexFlags flags, const char* label)
{
    int width, height, channels;
    unsigned char* image = NULL;
    Texture defTexture;
    defTexture.width  = 32;
    defTexture.height = 32;
    defTexture.handle = 0;

    if (!FileExist(path)) {
        AX_ERROR("image is not exist, using default texture! %s", path);
        return defTexture;
    }

    AFile asset = AFileOpen(path, AOpenFlag_ReadBinary);
    u64 size = AFileSize(asset);

    void* textureLoadBuffer = ArenaPushGlobal(size);
    
    AFileRead(textureLoadBuffer, size, asset, 1);
    image = stbi_load_from_memory(textureLoadBuffer, (int)size, &width, &height, &channels, 4);
    ArenaPopGlobal(size);

    AFileClose(asset);
    
    if (image == NULL) {
        AX_ERROR("image load failed! %s", path);
        return defTexture;
    }
    
    const SDL_GPUTextureFormat numCompToFormat[5] = 
    {
        0, 
        SDL_GPU_TEXTUREFORMAT_R8_UNORM, 
        SDL_GPU_TEXTUREFORMAT_R8G8_UNORM,
        SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, 
        SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, 
    };
    
    if (channels == 3) // shouldn't happen since stb gives us 4 channel
    {
        size_t numBytes = width * height * 4;
        textureLoadBuffer = ArenaPushGlobal(size);
        MakeRGBAFromRGB(image, textureLoadBuffer, numBytes);
        STBI_REALLOC(image, numBytes);
        MemCopy(image, textureLoadBuffer, numBytes);
        ArenaPopGlobal(numBytes);
    }
    Texture texture = rCreateTexture(width, height, image, numCompToFormat[channels], flags, label); 
    
    bool delBuff = (flags & TexFlags_DontDeleteCPUBuffer) == 0;
    if (delBuff)
    {
        stbi_image_free(image);
        texture.buffer = NULL;
    }
    return texture;
}

static u64 CalcMipBytes(u32 width, u32 height, u32 bpp, u32 mipLevels)
{
    u64 total = 0;
    u64 w = width;
    u64 h = height;

    for (u32 i = 0; i < mipLevels; ++i)
    {
        total += w * h * bpp;
        w = w > 1 ? (w >> 1) : 1;
        h = h > 1 ? (h >> 1) : 1;
    }
    return total;
}

Texture rCreateTexture(int width, int height, void* data, SDL_GPUTextureFormat format, TexFlags flags, const char* label)
{
    SDL_GPUTextureCreateInfo texDesc;
    texDesc.type                 = SDL_GPU_TEXTURETYPE_2D;
    texDesc.format               = format;
    texDesc.width                = width;
    texDesc.height               = height;
    texDesc.layer_count_or_depth = 1;
    texDesc.sample_count         = SDL_GPU_SAMPLECOUNT_1;
    texDesc.usage                = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    texDesc.props                = 0;
    
    if (!!(flags & TexFlags_MipMap))
        texDesc.num_levels = Clampi32((Log2u32((unsigned)width) >> 1)-1, 1, 8);
    else
        texDesc.num_levels = 1;
        
    Texture res; 
    res.width = width;
    res.height = height;
    res.handle = SDL_CreateGPUTexture(g_GPUDevice, &texDesc);
    res.format = format;
    res.buffer = data;

    if (data)
    {
        SDL_GPUCommandBuffer* uploadCmdBuf = SDL_AcquireGPUCommandBuffer(g_GPUDevice);
        SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(uploadCmdBuf);
     
        SDL_GPUTransferBufferCreateInfo transferBufferCreateInfo = {
            .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
            .size  = SDL_GPUTextureFormatTexelBlockSize(format) * width * height
        };

        SDL_GPUTransferBuffer* textureTransferBuffer = SDL_CreateGPUTransferBuffer(g_GPUDevice, &transferBufferCreateInfo);
        Uint8* textureTransferPtr = (Uint8*)SDL_MapGPUTransferBuffer(g_GPUDevice, textureTransferBuffer, false);
        MemCopy(textureTransferPtr, data, transferBufferCreateInfo.size);
        SDL_UnmapGPUTransferBuffer(g_GPUDevice, textureTransferBuffer);
        
        SDL_GPUTextureRegion textureRegion = {
            .texture = res.handle,
            .mip_level = 0,
            .w = width,
            .h = height,
            .d = 1
        };

        SDL_GPUTextureTransferInfo textureTransferInfo = {
            .transfer_buffer = textureTransferBuffer,
            .offset = 0
        };

        SDL_UploadToGPUTexture(copyPass, &textureTransferInfo, &textureRegion, false);
        SDL_EndGPUCopyPass(copyPass);
        
        SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(uploadCmdBuf);
        SDL_WaitForGPUFences(g_GPUDevice, true, &fence, 1);
        SDL_ReleaseGPUFence(g_GPUDevice, fence);
        SDL_ReleaseGPUTransferBuffer(g_GPUDevice, textureTransferBuffer);
    }

    if (data && !!(flags & TexFlags_MipMap))
    {
        SDL_GPUCommandBuffer* mipmapCmdBuf = SDL_AcquireGPUCommandBuffer(g_GPUDevice);
        SDL_GenerateMipmapsForGPUTexture(mipmapCmdBuf, res.handle);
        SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(mipmapCmdBuf);
        SDL_WaitForGPUFences(g_GPUDevice, true, &fence, 1);
        SDL_ReleaseGPUFence(g_GPUDevice, fence); 
    }

    return res;
}

void rUpdateTexture(Texture texture, void* data)
{

}

void rDeleteTexture(Texture texture)
{

}


#endif
