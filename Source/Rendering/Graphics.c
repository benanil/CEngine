#ifndef _GRAPHICS_
#define _GRAPHICS_

#include "Include/FileSystem.h"
#include "Include/Platform.h"
#include "Include/Graphics.h"
#include "Include/Algorithm.h"
#include "Include/Memory.h"
#include "Include/BasisBinding.h"
#include "Include/Random.h"

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

Graphics gGFX = {0};

extern void Quit(int rc);

void GraphicsInit(bool msaa)
{
    VkPhysicalDeviceFeatures vk10_features = {
        .shaderInt16 = VK_TRUE,
    };

    VkPhysicalDeviceVulkan12Features vk12_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = NULL,
        .shaderFloat16 = VK_TRUE,
    };

    VkPhysicalDeviceVulkan11Features vk11_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
        .pNext = &vk12_features,
        .shaderDrawParameters = VK_TRUE,
    };

    SDL_GPUVulkanOptions options = {
        .vulkan_api_version = VULKAN_MAKE_API_VERSION(0, 1, 2, 0),
        .feature_list = &vk11_features,
        .vulkan_10_physical_device_features = &vk10_features,
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
    if (!SDL_SetGPUAllowedFramesInFlight(g_GPUDevice, 3u))
    {
        AX_WARN("Failed to set GPU frames in flight: %s", SDL_GetError());
    }
    /* Claim the windows */
    SDL_ClaimWindowForGPUDevice(g_GPUDevice, g_SDLWindow);
    
    (void)msaa;
    
    int drawablew, drawableh;
    /* Set up per-window state */
    WindowState* winstate = &g_WindowState;
    /* create a depth texture for the window */
    SDL_GetWindowSizeInPixels(g_SDLWindow, (int*)&drawablew, (int*)&drawableh);
    winstate->tex_depth   = CreateDepthTexture(drawablew, drawableh);
    winstate->tex_hiz_depth = CreateHiZDepthTexture(drawablew, drawableh);
    winstate->tex_color   = CreateSceneColorTexture(drawablew, drawableh, SDL_GPU_SAMPLECOUNT_1);
    winstate->tex_gbuffer_tangent = CreateGBufferTangentTexture(drawablew, drawableh);
    winstate->tex_gbuffer_albedo_metallic = CreateGBufferAlbedoMetallicTexture(drawablew, drawableh);
    winstate->tex_gbuffer_shadow_roughness = CreateGBufferShadowRoughnessTexture(drawablew, drawableh);
    winstate->tex_post    = CreatePostProcessTexture(drawablew, drawableh);
    winstate->tex_hiz     = CreateHiZTexture(drawablew, drawableh, &winstate->hiz_mip_count);
    winstate->tex_sdsm_bounds = CreateSDSMDepthBoundsTexture(drawablew, drawableh, &winstate->sdsm_mip_count);
    winstate->tex_hbao        = CreateHBAOTexture(Maxu32((u32)drawablew / 2u, 1u), Maxu32((u32)drawableh / 2u, 1u));
    winstate->tex_hbao_blur   = CreateHBAOTexture(Maxu32((u32)drawablew / 2u, 1u), Maxu32((u32)drawableh / 2u, 1u));
    winstate->tex_hbao_normal = CreateHBAONormalTexture(Maxu32((u32)drawablew / 2u, 1u), Maxu32((u32)drawableh / 2u, 1u));
    winstate->tex_mlaa_edge_mask = CreateMLAAEdgeMaskTexture(drawablew, drawableh);
    winstate->tex_mlaa_edge_count = CreateMLAAEdgeCountTexture(drawablew, drawableh);
    winstate->tex_mlaa_output = CreateMLAAOutputTexture(drawablew, drawableh);
    winstate->tex_shadow_depth = CreateShadowDepthTexture(SHADOW_MAP_SIZE);
    winstate->tex_shadow_color = CreateShadowColorTexture(SHADOW_MAP_SIZE, SHADOW_CASCADE_COUNT);
    g_RenderState.skyNoise3D = Create3DNoise3DTexture(64u);
    winstate->hiz_width   = drawablew;
    winstate->hiz_height  = drawableh;
    winstate->hiz_valid   = false;
    winstate->sdsm_valid  = false;
    
    gGFX.SkinnedVertexBuffer = OSAllocAligned(sizeof(ASkinedVertex) * MAX_SKINNED_SOURCE_VERTEX, 4);
    gGFX.SurfaceVertexBuffer = OSAllocAligned(sizeof(AVertex) * MAX_SURFACE_VERTEX, 4);
    gGFX.IndexBuffer         = OSAllocAligned(sizeof(u32) * MAX_INDEX + 16, 4); // 16->give little bit of space for memcpy
    if (!gGFX.SkinnedVertexBuffer || !gGFX.SurfaceVertexBuffer || !gGFX.IndexBuffer)
        AX_ERROR("graphics CPU buffer allocation failed skinned=%p surface=%p index=%p", gGFX.SkinnedVertexBuffer, gGFX.SurfaceVertexBuffer, gGFX.IndexBuffer);
}

void GraphicsDestroy()
{
    WindowState* winstate = &g_WindowState;
    SDL_ReleaseGPUTexture(g_GPUDevice, winstate->tex_depth);
    SDL_ReleaseGPUTexture(g_GPUDevice, winstate->tex_hiz_depth);
    SDL_ReleaseGPUTexture(g_GPUDevice, winstate->tex_color);
    SDL_ReleaseGPUTexture(g_GPUDevice, winstate->tex_gbuffer_tangent);
    SDL_ReleaseGPUTexture(g_GPUDevice, winstate->tex_gbuffer_albedo_metallic);
    SDL_ReleaseGPUTexture(g_GPUDevice, winstate->tex_gbuffer_shadow_roughness);
    SDL_ReleaseGPUTexture(g_GPUDevice, winstate->tex_post);
    SDL_ReleaseGPUTexture(g_GPUDevice, winstate->tex_hiz);
    SDL_ReleaseGPUTexture(g_GPUDevice, winstate->tex_sdsm_bounds);
    SDL_ReleaseGPUTexture(g_GPUDevice, winstate->tex_hbao);
    SDL_ReleaseGPUTexture(g_GPUDevice, winstate->tex_hbao_blur);
    SDL_ReleaseGPUTexture(g_GPUDevice, winstate->tex_hbao_normal);
    SDL_ReleaseGPUTexture(g_GPUDevice, winstate->tex_mlaa_edge_mask);
    SDL_ReleaseGPUTexture(g_GPUDevice, winstate->tex_mlaa_edge_count);
    SDL_ReleaseGPUTexture(g_GPUDevice, winstate->tex_mlaa_output);
    SDL_ReleaseGPUTexture(g_GPUDevice, winstate->tex_shadow_depth);
    SDL_ReleaseGPUTexture(g_GPUDevice, winstate->tex_shadow_color);
    SDL_ReleaseGPUTexture(g_GPUDevice, g_RenderState.skyNoise3D);
    SDL_ReleaseWindowFromGPUDevice(g_GPUDevice, g_SDLWindow);

    SDL_DestroyGPUDevice(g_GPUDevice);
    OSFreeAligned(gGFX.SkinnedVertexBuffer, sizeof(ASkinedVertex) * MAX_SKINNED_SOURCE_VERTEX);
    OSFreeAligned(gGFX.SurfaceVertexBuffer, sizeof(AVertex) * MAX_SURFACE_VERTEX);
    OSFreeAligned(gGFX.IndexBuffer        , sizeof(u32) * MAX_INDEX + 16);
    gGFX.SkinnedVertexBuffer = NULL;
    gGFX.SurfaceVertexBuffer = NULL;
    gGFX.IndexBuffer = NULL;
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
    if (buffer == NULL) 
        return gpu_buffer;

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
    SDL_GPUTransferBufferLocation src_location = { .transfer_buffer = upload_buf, .offset = 0 };
    SDL_GPUBufferRegion dst_region = { .buffer = gpu_buffer, .offset = 0, .size  = bufferSize };
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
                          &(SDL_GPUBufferRegion) { .buffer = buffer,  .offset = offset,  .size = bufferSize }, 
                          true);
    
    SDL_EndGPUCopyPass(copyPass);
    
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(uploadCmdBuf);
    SDL_WaitForGPUFences(g_GPUDevice, true, &fence, 1);
    SDL_ReleaseGPUFence(g_GPUDevice, fence);
    SDL_ReleaseGPUTransferBuffer(g_GPUDevice, boneTransferBuffer);
}

static u32 GetMipCount(u32 width, u32 height)
{
    u32 levels = 1;
    u32 size = width > height ? width : height;
    while (size > 16) {
        size >>= 1u;
        levels++;
    }
    return levels;
}

static SDL_GPUTexture* CreateTexture2D(u32 width, u32 height,
                                       SDL_GPUTextureFormat format,
                                       SDL_GPUTextureUsageFlags usage,
                                       SDL_GPUSampleCount sampleCount,
                                       u32 mipLevels,
                                       const char* label)
{
    SDL_GPUTextureCreateInfo createinfo = {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = format,
        .usage = usage,
        .width = width,
        .height = height,
        .layer_count_or_depth = 1,
        .num_levels = mipLevels,
        .sample_count = sampleCount,
        .props = 0
    };

    SDL_GPUTexture* result = SDL_CreateGPUTexture(g_GPUDevice, &createinfo);
    CHECK_CREATE(result, label)
    return result;
}

static SDL_GPUTexture* CreateTexture2DArray(u32 width, u32 height, u32 layers,
                                            SDL_GPUTextureFormat format,
                                            SDL_GPUTextureUsageFlags usage,
                                            const char* label)
{
    SDL_GPUTextureCreateInfo createinfo = {
        .type = SDL_GPU_TEXTURETYPE_2D_ARRAY,
        .format = format,
        .usage = usage,
        .width = width,
        .height = height,
        .layer_count_or_depth = layers,
        .num_levels = 1,
        .sample_count = SDL_GPU_SAMPLECOUNT_1,
        .props = 0
    };

    SDL_GPUTexture* result = SDL_CreateGPUTexture(g_GPUDevice, &createinfo);
    CHECK_CREATE(result, label)
    return result;
}

SDL_GPUTexture* Create3DNoise3DTexture(u32 size)
{
    SDL_GPUTextureCreateInfo createinfo = {
        .type = SDL_GPU_TEXTURETYPE_2D_ARRAY,
        .format = SDL_GPU_TEXTUREFORMAT_R8_UNORM,
        .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
        .width = size,
        .height = size,
        .layer_count_or_depth = size,
        .num_levels = 1,
        .sample_count = SDL_GPU_SAMPLECOUNT_1,
        .props = 0
    };

    SDL_GPUTexture* texture = SDL_CreateGPUTexture(g_GPUDevice, &createinfo);
    CHECK_CREATE(texture, "Sky 3D Noise Texture");

    u32 voxelCount = size * size * size;
    u64* data = (u64*)ArenaPushGlobal(voxelCount);
    for (u32 z = 0; z < size * size * (size / sizeof(u64)); z++)
        data[z] = MurmurHash(z);

    SDL_GPUTransferBuffer* transferBuffer = SDL_CreateGPUTransferBuffer(g_GPUDevice, &(SDL_GPUTransferBufferCreateInfo){
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = voxelCount
    });
    CHECK_CREATE(transferBuffer, "Sky 3D Noise Transfer Buffer");
    u8* mapped = (u8*)SDL_MapGPUTransferBuffer(g_GPUDevice, transferBuffer, false);
    MemCopy(mapped, data, voxelCount);
    SDL_UnmapGPUTransferBuffer(g_GPUDevice, transferBuffer);
    ArenaPopGlobal(voxelCount);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(g_GPUDevice);
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
    for (u32 layer = 0; layer < size; layer++)
    {
        SDL_UploadToGPUTexture(copyPass,
            &(SDL_GPUTextureTransferInfo){ .transfer_buffer = transferBuffer, .offset = layer * size * size, .pixels_per_row = size, .rows_per_layer = size },
            &(SDL_GPUTextureRegion){ .texture = texture, .mip_level = 0, .layer = layer, .x = 0, .y = 0, .z = 0, .w = size, .h = size, .d = 1 },
            false);
    }
    SDL_EndGPUCopyPass(copyPass);
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    SDL_WaitForGPUFences(g_GPUDevice, true, &fence, 1);
    SDL_ReleaseGPUFence(g_GPUDevice, fence);
    SDL_ReleaseGPUTransferBuffer(g_GPUDevice, transferBuffer);
    return texture;
}

SDL_GPUTexture* CreateDepthTexture(u32 drawablew, u32 drawableh)
{
    return CreateTexture2D(drawablew, drawableh, SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
                           SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
                           SDL_GPU_SAMPLECOUNT_1, 1, "Depth Texture");
}

SDL_GPUTexture* CreateHiZDepthTexture(u32 drawablew, u32 drawableh)
{
    return CreateTexture2D(drawablew, drawableh, SDL_GPU_TEXTUREFORMAT_R32_FLOAT,
                           SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
                           SDL_GPU_SAMPLECOUNT_1, 1, "Hi-Z Resolved Depth Texture");
}

SDL_GPUTexture* CreateHiZTexture(u32 drawablew, u32 drawableh, u32* mipCount)
{
    u32 levels = GetMipCount(drawablew, drawableh);
    if (mipCount) *mipCount = levels;
    return CreateTexture2D(drawablew, drawableh, SDL_GPU_TEXTUREFORMAT_R32_FLOAT,
                           SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE,
                           SDL_GPU_SAMPLECOUNT_1, levels, "Hi-Z Texture");
}

SDL_GPUTexture* CreateSceneColorTexture(u32 drawablew, u32 drawableh, SDL_GPUSampleCount sampleCount)
{
    SDL_GPUTextureUsageFlags usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;
    if (sampleCount == SDL_GPU_SAMPLECOUNT_1) usage |= SDL_GPU_TEXTUREUSAGE_SAMPLER;
    return CreateTexture2D(drawablew, drawableh, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,
                            usage, sampleCount, 1, "Scene Color Texture");
}

SDL_GPUTexture* CreateSDSMDepthBoundsTexture(u32 drawablew, u32 drawableh, u32* mipCount)
{
    u32 width = Maxu32((drawablew + 1u) / 2u, 1u);
    u32 height = Maxu32((drawableh + 1u) / 2u, 1u);
    u32 levels = GetMipCount(width, height);
    if (mipCount) *mipCount = levels;
    return CreateTexture2D(width, height, SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT,
                           SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE,
                           SDL_GPU_SAMPLECOUNT_1, levels, "SDSM Depth Bounds Texture");
}

SDL_GPUTexture* CreateGBufferTangentTexture(u32 drawablew, u32 drawableh)
{
    return CreateTexture2D(drawablew, drawableh, SDL_GPU_TEXTUREFORMAT_R32_UINT,
                           SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
                           SDL_GPU_SAMPLECOUNT_1, 1, "GBuffer Tangent Texture");
}

SDL_GPUTexture* CreateGBufferAlbedoMetallicTexture(u32 drawablew, u32 drawableh)
{
    return CreateTexture2D(drawablew, drawableh, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                           SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
                           SDL_GPU_SAMPLECOUNT_1, 1, "GBuffer Albedo Metallic Texture");
}

SDL_GPUTexture* CreateGBufferShadowRoughnessTexture(u32 drawablew, u32 drawableh)
{
    return CreateTexture2D(drawablew, drawableh, SDL_GPU_TEXTUREFORMAT_R8G8_UNORM,
                           SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
                           SDL_GPU_SAMPLECOUNT_1, 1, "GBuffer Shadow Roughness Texture");
}

SDL_GPUTexture* CreatePostProcessTexture(u32 drawablew, u32 drawableh)
{
    return CreateTexture2D(drawablew, drawableh, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                           SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE,
                           SDL_GPU_SAMPLECOUNT_1, 1, "Post Process Texture");
}

SDL_GPUTexture* CreateHBAOTexture(u32 drawablew, u32 drawableh)
{
    return CreateTexture2D(drawablew, drawableh, SDL_GPU_TEXTUREFORMAT_R8_UNORM,
                           SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE,
                           SDL_GPU_SAMPLECOUNT_1, 1, "HBAO Texture");
}

SDL_GPUTexture* CreateHBAONormalTexture(u32 drawablew, u32 drawableh)
{
    return CreateTexture2D(drawablew, drawableh, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                            SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE,
                            SDL_GPU_SAMPLECOUNT_1, 1, "HBAO Normal Texture");
}

SDL_GPUTexture* CreateMLAAEdgeMaskTexture(u32 drawablew, u32 drawableh)
{
    return CreateTexture2D(drawablew, drawableh, SDL_GPU_TEXTUREFORMAT_R32_UINT,
                           SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE,
                           SDL_GPU_SAMPLECOUNT_1, 1, "MLAA Edge Mask Texture");
}

SDL_GPUTexture* CreateMLAAEdgeCountTexture(u32 drawablew, u32 drawableh)
{
    return CreateTexture2D(drawablew, drawableh, SDL_GPU_TEXTUREFORMAT_R32G32_UINT,
                           SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE,
                           SDL_GPU_SAMPLECOUNT_1, 1, "MLAA Edge Count Texture");
}

SDL_GPUTexture* CreateMLAAOutputTexture(u32 drawablew, u32 drawableh)
{
    return CreateTexture2D(drawablew, drawableh, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                           SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE,
                           SDL_GPU_SAMPLECOUNT_1, 1, "MLAA Output Texture");
}

SDL_GPUTexture* CreateShadowDepthTexture(u32 size)
{
    return CreateTexture2D(size, size, SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
                           SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
                           SDL_GPU_SAMPLECOUNT_1, SHADOW_CASCADE_COUNT, "Shadow Depth Texture");
}

SDL_GPUTexture* CreateShadowColorTexture(u32 size, u32 layers)
{
    (void)layers;
    return CreateTexture2D(size, size, SDL_GPU_TEXTUREFORMAT_R32_FLOAT,
                           SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
                           SDL_GPU_SAMPLECOUNT_1, SHADOW_CASCADE_COUNT, "Shadow Color Texture");
}

Texture rImportTexture(const char* path, TexFlags flags, const char* label)
{
    int width, height, channels;
    unsigned char* image = NULL;
    Texture defTexture;
    defTexture.width  = 32;
    defTexture.height = 32;
    defTexture.handle = 0;
    defTexture.type = 0;
    defTexture.mipLevels = 1;

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
    
    (void)channels;
    SDL_GPUTextureUsageFlags usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    if (flags & TexFlags_MipMap) usage |= SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    Texture texture = rCreateTexture(width, height, image, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, flags, usage, label); 
    texture.type = 0;
    
    bool delBuff = (flags & TexFlags_DontDeleteCPUBuffer) == 0;
    if (delBuff)
    {
        stbi_image_free(image);
        texture.buffer = NULL;
    }
    return texture;
}

static Texture rCreateTextureEx(
    int width,
    int height,
    int layers,
    void* data,
    SDL_GPUTextureFormat format,
    SDL_GPUTextureUsageFlags usage,
    TexFlags flags,
    const char* label)
{
    bool isArray = layers > 1;

    u32 mipLevels = 1;
    if (flags & TexFlags_MipMap)
    {
        u32 maxDim = (u32)(width > height ? width : height);
        mipLevels = Log2u32(maxDim) + 1;
    }
    
    SDL_GPUTextureCreateInfo texDesc = {
        .type                 = isArray ? SDL_GPU_TEXTURETYPE_2D_ARRAY : SDL_GPU_TEXTURETYPE_2D,
        .format               = format,
        .width                = (u32)width,
        .height               = (u32)height,
        .layer_count_or_depth = (u32)layers,
        .num_levels           = mipLevels,
        .sample_count         = SDL_GPU_SAMPLECOUNT_1,
        .usage                = usage,
        .props                = SDL_CreateProperties()
    };

    if (label)
        SDL_SetStringProperty(texDesc.props, SDL_PROP_GPU_TEXTURE_CREATE_NAME_STRING, label);

    Texture res = {0};
    res.width  = width;
    res.height = height;
    res.format = format;
    res.buffer = data;
    res.mipLevels = mipLevels;
    res.handle = SDL_CreateGPUTexture(g_GPUDevice, &texDesc);

    SDL_DestroyProperties(texDesc.props);

    if (!res.handle)
    {
        AX_WARN("SDL_CreateGPUTexture failed!");
        return res;
    }

    if (data)
    {
        u32 blockSize  = SDL_GPUTextureFormatTexelBlockSize(format);
        u32 layerSize  = blockSize * (u32)width * (u32)height;
        u32 uploadSize = layerSize * (u32)layers;

        SDL_GPUTransferBufferCreateInfo transferDesc = {
            .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
            .size  = uploadSize
        };

        SDL_GPUTransferBuffer* transferBuffer = SDL_CreateGPUTransferBuffer(g_GPUDevice, &transferDesc);

        Uint8* dst = (Uint8*)SDL_MapGPUTransferBuffer(g_GPUDevice, transferBuffer, false);

        MemCopy(dst, data, uploadSize);

        SDL_UnmapGPUTransferBuffer(g_GPUDevice, transferBuffer);

        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(g_GPUDevice);
        SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);

        SDL_GPUTextureTransferInfo transferInfo = {
            .transfer_buffer = transferBuffer,
            .offset          = 0,
            .pixels_per_row  = (u32)width,
            .rows_per_layer  = (u32)height
        };

        SDL_GPUTextureRegion region = {
            .texture   = res.handle,
            .mip_level = 0,
            .layer     = 0,
            .x         = 0,
            .y         = 0,
            .z         = 0,
            .w         = (u32)width,
            .h         = (u32)height,
            .d         = isArray ? 1 : (u32)layers
        };

        if (isArray)
        {
            for (u32 layer = 0; layer < (u32)layers; layer++)
            {
                transferInfo.offset = layerSize * layer;
                region.layer = layer;
                region.d = 1;
                SDL_UploadToGPUTexture(copyPass, &transferInfo, &region, false);
            }
        }
        else
        {
            SDL_UploadToGPUTexture(copyPass, &transferInfo, &region, false);
        }

        SDL_EndGPUCopyPass(copyPass);

        SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
        SDL_WaitForGPUFences(g_GPUDevice, true, &fence, 1);
        SDL_ReleaseGPUFence(g_GPUDevice, fence);

        SDL_ReleaseGPUTransferBuffer(g_GPUDevice, transferBuffer);
    }

    if (data && (flags & TexFlags_MipMap))
    {
        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(g_GPUDevice);

        SDL_GenerateMipmapsForGPUTexture(cmd, res.handle);

        SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
        SDL_WaitForGPUFences(g_GPUDevice, true, &fence, 1);
        SDL_ReleaseGPUFence(g_GPUDevice, fence);
    }

    return res;
}

Texture rCreateTexture(int width, int height, void* data, SDL_GPUTextureFormat format,
                       TexFlags flags, SDL_GPUTextureUsageFlags usage, const char* label)
{
    return rCreateTextureEx(width, height, 1, data, format, usage, flags, label);
}

Texture rCreateTexture2DArray(int width, int height, int layers, void* data, SDL_GPUTextureFormat format, 
                              TexFlags flags, SDL_GPUTextureUsageFlags usage, const char* label)
{
    return rCreateTextureEx(width, height, layers, data, format, usage, flags, label);
}

void UploadTextureRegion(Texture texture, u32 layer, u32 x, u32 y, u32 width, u32 height, u32 srcWidth, u32 srcHeight, const void* data)
{
    if (!texture.handle || !data || width == 0 || height == 0) return;

    u32 blockSize = SDL_GPUTextureFormatTexelBlockSize(texture.format);
    SDL_GPUTransferBufferCreateInfo transferDesc = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size  = (u32)(srcWidth * srcHeight * blockSize)
    };
    SDL_GPUTransferBuffer* transferBuffer = SDL_CreateGPUTransferBuffer(g_GPUDevice, &transferDesc);
    Uint8* dst = (Uint8*)SDL_MapGPUTransferBuffer(g_GPUDevice, transferBuffer, false);
    MemCopy(dst, data, transferDesc.size);
    SDL_UnmapGPUTransferBuffer(g_GPUDevice, transferBuffer);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(g_GPUDevice);
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureTransferInfo transferInfo = {
        .transfer_buffer = transferBuffer,
        .offset = 0,
        .pixels_per_row = srcWidth,
        .rows_per_layer = srcHeight
    };
    SDL_GPUTextureRegion region = {
        .texture = texture.handle,
        .mip_level = 0,
        .layer = layer,
        .x = x,
        .y = y,
        .z = 0,
        .w = width,
        .h = height,
        .d = 1
    };
    SDL_UploadToGPUTexture(copyPass, &transferInfo, &region, false);
    SDL_EndGPUCopyPass(copyPass);
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    SDL_WaitForGPUFences(g_GPUDevice, true, &fence, 1);
    SDL_ReleaseGPUFence(g_GPUDevice, fence);
    SDL_ReleaseGPUTransferBuffer(g_GPUDevice, transferBuffer);
}

void GenerateTextureMips(Texture texture)
{
    if (!texture.handle) return;
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(g_GPUDevice);
    SDL_GenerateMipmapsForGPUTexture(cmd, texture.handle);
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    SDL_WaitForGPUFences(g_GPUDevice, true, &fence, 1);
    SDL_ReleaseGPUFence(g_GPUDevice, fence);
}

void ReleaseTexture(Texture* texture)
{
    SDL_ReleaseGPUTexture(g_GPUDevice, texture->handle);
    texture->handle = NULL;
    texture->width = texture->height = 0;
}

void rDeleteTexture(Texture texture)
{

}


#endif
