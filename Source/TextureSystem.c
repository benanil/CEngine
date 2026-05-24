#define STB_RECT_PACK_IMPLEMENTATION
#include "Extern/stb/stb_rect_pack.h"

#include "Include/Graphics.h"
#include "Include/BasisBinding.h"
#include "Include/Memory.h"
#include "Include/Platform.h"
#include "Math/Half.h"
#include "Math/Vector.h"

#if defined(PLATFORM_WINDOWS)
#include "Shaders/spv/TexturePageCopyRGBA.spv.h"
#include "Shaders/spv/TexturePageCopyRG.spv.h"
#endif

extern RenderState g_RenderState;
extern SDL_GPUDevice* g_GPUDevice;

enum { TextureClass_Albedo, TextureClass_Normal, TextureClass_MetallicRoughness, TextureClass_Count };
enum { TextureType_Normal = 1u << 0, TextureType_MetallicRoughness = 1u << 1 };
enum { TextureDesc_Invalid = 0u, TextureDesc_Albedo = 1u, TextureDesc_Normal = 2u, TextureDesc_MetallicRoughness = 3u };
enum { TextureDesc_DefaultAlbedo = 1u, TextureDesc_DefaultNormal = 2u, TextureDesc_DefaultMetallicRoughness = 4u };
enum { CompressedPageAlign = 4u, CompressedPageMaxMips = 8u };

typedef struct TextureClassInfo_
{
    Texture* pageTexture;
    u32* descriptorMap;
    const char* label;
    SDL_GPUTextureFormat uncompressedFormat;
    u32 defaultDescriptor;
    u32 defaultFlag;
    u32 channelMode;
} TextureClassInfo;

typedef struct TexturePlacement_
{
    u32 imageIndex;
    u32 page;
    u32 x, y;
    u32 width, height;
    u32 padding;
} TexturePlacement;

typedef struct TexturePackResult_
{
    TexturePlacement placements[MAX_SCENE_TEXTURES];
    u32 count;
} TexturePackResult;

typedef struct PageCopyRequest_
{
    u32 textureClass;
    u32 sourceIndex;
    u32 page;
    u32 x, y;
    u32 width, height;
    u32 padding;
} PageCopyRequest;

static TextureDescriptor gTextureDescriptors[MAX_TEXTURE_DESCRIPTORS];
static MaterialGPU gMaterials[MAX_GPU_MATERIALS];
static u32 gAlbedoDescriptor[MAX_SCENE_TEXTURES];
static u32 gNormalDescriptor[MAX_SCENE_TEXTURES];
static u32 gMetallicRoughnessDescriptor[MAX_SCENE_TEXTURES];
static PageCopyRequest gCopyRequests[MAX_TEXTURE_DESCRIPTORS];

static SDL_GPUComputePipeline* gCopyRGBAPipeline;
static SDL_GPUComputePipeline* gCopyRGPipeline;
static u32 gNumCopyRequests;

static u32 MipCountForSize(u32 width, u32 height)
{
    u32 levels = 1;
    u32 dim = width > height ? width : height;
    while (dim > 1)
    {
        dim >>= 1;
        levels++;
    }
    return levels;
}

static void InitCopyPipelines(void)
{
    gCopyRGBAPipeline = SDL_CreateGPUComputePipeline(g_GPUDevice, &(SDL_GPUComputePipelineCreateInfo){
        .code = Shaders_TexturePageCopyRGBA_spv,
        .code_size = sizeof(Shaders_TexturePageCopyRGBA_spv),
        .entrypoint = "main",
        .format = SDL_GetGPUShaderFormats(g_GPUDevice),
        .num_samplers = 1,
        .num_readwrite_storage_textures = 1,
        .num_uniform_buffers = 1,
        .threadcount_x = 8,
        .threadcount_y = 8,
        .threadcount_z = 1,
    });
    CHECK_CREATE(gCopyRGBAPipeline, "Texture Page Copy RGBA Pipeline");
    
    gCopyRGPipeline = SDL_CreateGPUComputePipeline(g_GPUDevice, &(SDL_GPUComputePipelineCreateInfo){
        .code = Shaders_TexturePageCopyRG_spv,
        .code_size = sizeof(Shaders_TexturePageCopyRG_spv),
        .entrypoint = "main",
        .format = SDL_GetGPUShaderFormats(g_GPUDevice),
        .num_samplers = 1,
        .num_readwrite_storage_textures = 1,
        .num_uniform_buffers = 1,
        .threadcount_x = 8,
        .threadcount_y = 8,
        .threadcount_z = 1,
    });
    CHECK_CREATE(gCopyRGPipeline, "Texture Page Copy RG Pipeline");
}

static u32 AddDescriptor(u32 pageIndex, float x, float y, float w, float h)
{
    if (g_RenderState.numTextureDescriptors >= MAX_TEXTURE_DESCRIPTORS)
    {
        AX_ERROR("maximum texture descriptors reached");
        return TextureDesc_Invalid;
    }

    u32 idx = g_RenderState.numTextureDescriptors++;
    TextureDescriptor* desc = &gTextureDescriptors[idx];
    desc->pageIndex = pageIndex;
    desc->flags = 0;
    desc->uvScale.x = w / (float)TEXTURE_PAGE_SIZE;
    desc->uvScale.y = h / (float)TEXTURE_PAGE_SIZE;
    desc->uvBias.x = x / (float)TEXTURE_PAGE_SIZE;
    desc->uvBias.y = y / (float)TEXTURE_PAGE_SIZE;
    return idx;
}

static u32 AddDescriptorFlags(u32 pageIndex, float x, float y, float w, float h, u32 flags)
{
    if (g_RenderState.numTextureDescriptors >= MAX_TEXTURE_DESCRIPTORS)
    {
        AX_ERROR("maximum texture descriptors reached");
        return TextureDesc_Invalid;
    }
    u32 idx = AddDescriptor(pageIndex, x, y, w, h);
    gTextureDescriptors[idx].flags = flags;
    return idx;
}

static u32 PackMaterialFlags(const AMaterial* material)
{
    u32 cutoff = (u32)(Saturatef32(material->alphaCutoff) * 255.0f + 0.5f);
    u32 flags = (cutoff << MATERIAL_ALPHA_CUTOFF_SHIFT) & MATERIAL_ALPHA_CUTOFF_MASK;
    if (material->alphaMode == AMaterialAlphaMode_Mask)
        flags |= MATERIAL_FLAG_ALPHA_MASK;
    return flags;
}

static void MarkWantedTexture(bool wanted[TextureClass_Count][MAX_SCENE_TEXTURES], u32 textureClass, u32 imageIndex)
{
    if (imageIndex >= MAX_SCENE_TEXTURES || textureClass >= TextureClass_Count)
        return;

    for (u32 c = 0; c < TextureClass_Count; c++)
        wanted[c][imageIndex] = false;
    wanted[textureClass][imageIndex] = true;
}

static void CountWantedTextures(bool wanted[TextureClass_Count][MAX_SCENE_TEXTURES], u32* albedo, u32* normal, u32* mr)
{
    *albedo = *normal = *mr = 0;
    for (u32 i = 0; i < MAX_SCENE_TEXTURES; i++)
    {
        *albedo += wanted[TextureClass_Albedo][i];
        *normal += wanted[TextureClass_Normal][i];
        *mr     += wanted[TextureClass_MetallicRoughness][i];
    }
}

static void AddDefaultDescriptors(void)
{
    g_RenderState.numTextureDescriptors = 0;
    AddDescriptorFlags(0, 0, 0, 16, 16, TextureDesc_DefaultAlbedo);            // invalid also samples harmless default
    AddDescriptorFlags(0, 0, 0, 16, 16, TextureDesc_DefaultAlbedo);
    AddDescriptorFlags(0, 0, 0, 16, 16, TextureDesc_DefaultNormal);
    AddDescriptorFlags(0, 0, 0, 16, 16, TextureDesc_DefaultMetallicRoughness);
}

void InitTextureSystem(void)
{
    InitCopyPipelines();
    gNumCopyRequests = 0;
    AddDefaultDescriptors();

    g_RenderState.textureDescriptorBuffer = CreateBuffer(NULL, sizeof(TextureDescriptor) * MAX_TEXTURE_DESCRIPTORS, SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ, "TextureDescriptors");
    g_RenderState.materialBuffer = CreateBuffer(NULL, sizeof(MaterialGPU) * MAX_GPU_MATERIALS, SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ, "Materials");

    for (u32 i = 0; i < MAX_SCENE_TEXTURES; i++)
    {
        gAlbedoDescriptor[i] = TextureDesc_Albedo;
        gNormalDescriptor[i] = TextureDesc_Normal;
        gMetallicRoughnessDescriptor[i] = TextureDesc_MetallicRoughness;
    }
}

static u32 AlignUpu32(u32 value, u32 align)
{
    return (value + align - 1u) & ~(align - 1u);
}

static bool IsBlockCompressedFormat(SDL_GPUTextureFormat format)
{
    return format == SDL_GPU_TEXTUREFORMAT_BC5_RG_UNORM ||
           format == SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM ||
           format == SDL_GPU_TEXTUREFORMAT_ASTC_4x4_UNORM;
}

static u32 CompressedMipBytes(SDL_GPUTextureFormat format, u32 width, u32 height)
{
    u32 blockSize = SDL_GPUTextureFormatTexelBlockSize(format);
    u32 blocksX = (width + 3u) / 4u;
    u32 blocksY = (height + 3u) / 4u;
    return blocksX * blocksY * blockSize;
}

static SDL_GPUTextureFormat SelectAlbedoPageFormat(void)
{
#if defined(__ANDROID__)
    SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_ASTC_4x4_UNORM;
#else
    SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM;
#endif
    return SDL_GPUTextureSupportsFormat(g_GPUDevice, format, SDL_GPU_TEXTURETYPE_2D_ARRAY, SDL_GPU_TEXTUREUSAGE_SAMPLER) ?
           format : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
}

static SDL_GPUTextureFormat SelectRGPageFormat(void)
{
#if defined(__ANDROID__)
    SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_ASTC_4x4_UNORM;
#else
    SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_BC5_RG_UNORM;
#endif
    return SDL_GPUTextureSupportsFormat(g_GPUDevice, format, SDL_GPU_TEXTURETYPE_2D_ARRAY, SDL_GPU_TEXTUREUSAGE_SAMPLER) ?
           format : SDL_GPU_TEXTUREFORMAT_R8G8_UNORM;
}

static void UploadDefaultPages(void)
{
    enum { DefaultSize = 16 };
    u8 albedo[DefaultSize * DefaultSize * 4];
    u8 normal[DefaultSize * DefaultSize * 2];
    u8 mr[DefaultSize * DefaultSize * 2];

    for (u32 i = 0; i < DefaultSize * DefaultSize; i++)
    {
        albedo[i * 4 + 0] = 255;
        albedo[i * 4 + 1] = 255;
        albedo[i * 4 + 2] = 255;
        albedo[i * 4 + 3] = 255;
        normal[i * 2 + 0] = 128;
        normal[i * 2 + 1] = 128;
        mr[i * 2 + 0] = 0;
        mr[i * 2 + 1] = 255;
    }

    UploadTextureRegion(g_RenderState.albedoPages, 0, 0, 0, DefaultSize, DefaultSize, DefaultSize, DefaultSize, albedo);
    UploadTextureRegion(g_RenderState.normalPages, 0, 0, 0, DefaultSize, DefaultSize, DefaultSize, DefaultSize, normal);
    UploadTextureRegion(g_RenderState.metallicRoughnessPages, 0, 0, 0, DefaultSize, DefaultSize, DefaultSize, DefaultSize, mr);
}

static TextureClassInfo GetTextureClassInfo(u32 textureClass)
{
    if (textureClass == TextureClass_Normal)
    {
        return (TextureClassInfo){
            &g_RenderState.normalPages, gNormalDescriptor, "NormalPages",
            SDL_GPU_TEXTUREFORMAT_R8G8_UNORM, TextureDesc_Normal, TextureDesc_DefaultNormal, 0u
        };
    }
    if (textureClass == TextureClass_MetallicRoughness)
    {
        return (TextureClassInfo){
            &g_RenderState.metallicRoughnessPages, gMetallicRoughnessDescriptor, "MetallicRoughnessPages",
            SDL_GPU_TEXTUREFORMAT_R8G8_UNORM, TextureDesc_MetallicRoughness, TextureDesc_DefaultMetallicRoughness, 1u
        };
    }
    return (TextureClassInfo){
        &g_RenderState.albedoPages, gAlbedoDescriptor, "AlbedoPages",
        SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, TextureDesc_Albedo, TextureDesc_DefaultAlbedo, 0u
    };
}

static bool ResolveGlobalImageIndex(const SceneBundle* bundle, u32 imageOffset, u32 textureIndex, u32* outImageIndex)
{
    if (textureIndex >= (u32)bundle->numTextures) return false;
    s32 source = bundle->textures[textureIndex].source;
    if (source < 0 || source >= bundle->numImages) return false;
    u32 imageIndex = imageOffset + (u32)source;
    if (imageIndex >= MAX_SCENE_TEXTURES) return false;
    *outImageIndex = imageIndex;
    return true;
}

static void QueuePackedTextureCopy(u32 textureClass, u32 sourceIndex, Texture* src, u32 page, u32 x, u32 y, u32 padding)
{
    if (!src || !src->handle || src->width <= 0 || src->height <= 0) return;
    if (gNumCopyRequests >= MAX_TEXTURE_DESCRIPTORS)
    {
        AX_WARN("maximum texture page copy requests reached");
        return;
    }
    PageCopyRequest* req = &gCopyRequests[gNumCopyRequests++];
    req->textureClass = textureClass;
    req->sourceIndex = sourceIndex;
    req->page = page;
    req->x = x;
    req->y = y;
    req->width = (u32)src->width;
    req->height = (u32)src->height;
    req->padding = padding;
}

// non compressed
static void DispatchPageCopies(Texture* textures)
{
    if (gNumCopyRequests == 0) return;
    InitCopyPipelines();

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(g_GPUDevice);
    for (u32 i = 0; i < gNumCopyRequests; i++)
    {
        PageCopyRequest* req = &gCopyRequests[i];
        Texture* src = &textures[req->sourceIndex];
        TextureClassInfo info = GetTextureClassInfo(req->textureClass);
        Texture* dst = info.pageTexture;
        if (!src->handle || !dst->handle) continue;

        u32 sourceMipCount = MipCountForSize(req->width, req->height);
        if (src->mipLevels > 0 && src->mipLevels < sourceMipCount) sourceMipCount = src->mipLevels;
        u32 pageMipCount = MipCountForSize(TEXTURE_PAGE_SIZE, TEXTURE_PAGE_SIZE);
        for (u32 mip = 0; mip < pageMipCount; mip++)
        {
            u32 sourceMip = mip < sourceMipCount ? mip : sourceMipCount - 1u;
            u32 mipWidth  = Maxu32(req->width >> mip, 1u);
            u32 mipHeight = Maxu32(req->height >> mip, 1u);
            u32 sourceWidth  = Maxu32(req->width >> sourceMip, 1u);
            u32 sourceHeight = Maxu32(req->height >> sourceMip, 1u);
            u32 pageMipSize = TEXTURE_PAGE_SIZE >> mip;
            if (pageMipSize == 0) pageMipSize = 1;
            u32 innerX = req->x >> mip;
            u32 innerY = req->y >> mip;
            u32 gutter = req->padding && innerX > 0 && innerY > 0 &&
                         (innerX + mipWidth) < pageMipSize &&
                         (innerY + mipHeight) < pageMipSize ? 1u : 0u;
            u32 copyWidth = mipWidth + gutter * 2u;
            u32 copyHeight = mipHeight + gutter * 2u;
            SDL_GPUStorageTextureReadWriteBinding rwTexture = {
                .texture = dst->handle,
                .mip_level = mip,
                .layer = req->page,
                .cycle = false
            };
            SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(cmd, &rwTexture, 1, NULL, 0);
            SDL_BindGPUComputePipeline(pass, req->textureClass == TextureClass_Albedo ? gCopyRGBAPipeline : gCopyRGPipeline);
            SDL_BindGPUComputeSamplers(pass, 0, &(SDL_GPUTextureSamplerBinding){
                .texture = src->handle,
                .sampler = g_RenderState.sampler
            }, 1);
            struct { u32 dstOffset[2]; u32 copySize[2]; u32 sourceSize[2]; u32 sourceMip; u32 gutter; u32 channelMode; } params = {
                { innerX - gutter, innerY - gutter },
                { copyWidth, copyHeight },
                { sourceWidth, sourceHeight },
                sourceMip,
                gutter,
                info.channelMode
            };
            SDL_PushGPUComputeUniformData(cmd, 0, &params, sizeof(params));
            SDL_DispatchGPUCompute(pass, (copyWidth + 7u) / 8u, (copyHeight + 7u) / 8u, 1);
            SDL_EndGPUComputePass(pass);
        }
    }

    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    SDL_WaitForGPUFences(g_GPUDevice, true, &fence, 1);
    SDL_ReleaseGPUFence(g_GPUDevice, fence);
}

typedef struct CompressedPageBuffers_
{
    SDL_GPUTextureFormat format;
    u32 mipCount;
    u8* data[TEXTURE_PAGE_LAYERS][CompressedPageMaxMips];
    u32 size[CompressedPageMaxMips];
} CompressedPageBuffers;

static void FreeCompressedPageBuffers(CompressedPageBuffers* pages)
{
    for (u32 layer = 0; layer < TEXTURE_PAGE_LAYERS; layer++)
        for (u32 mip = 0; mip < pages->mipCount; mip++)
            if (pages->data[layer][mip]) DeAllocateTLSFGlobal(pages->data[layer][mip]);
    SmallMemSet(pages, 0, sizeof(*pages));
}

static bool AllocateCompressedPageBuffers(CompressedPageBuffers* pages, SDL_GPUTextureFormat format, u32 mipCount)
{
    SmallMemSet(pages, 0, sizeof(*pages));
    pages->format = format;
    pages->mipCount = Mins32((s32)mipCount, CompressedPageMaxMips);
    if (pages->mipCount == 0) pages->mipCount = 1;
    for (u32 mip = 0; mip < pages->mipCount; mip++)
    {
        u32 mipSize = Maxu32(TEXTURE_PAGE_SIZE >> mip, 1u);
        pages->size[mip] = CompressedMipBytes(format, mipSize, mipSize);
        for (u32 layer = 0; layer < TEXTURE_PAGE_LAYERS; layer++)
        {
            pages->data[layer][mip] = (u8*)AllocZeroTLSFGlobal(1, pages->size[mip]);
            if (!pages->data[layer][mip])
            {
                FreeCompressedPageBuffers(pages);
                return false;
            }
        }
    }
    return true;
}

static void WriteBC4ConstantBlock(u8* dst, u8 value)
{
    dst[0] = value;
    dst[1] = value;
    for (u32 i = 2; i < 8; i++) dst[i] = 0;
}

static void FillCompressedDefaultBlocks(CompressedPageBuffers* pages, u32 textureClass)
{
    if (pages->format != SDL_GPU_TEXTUREFORMAT_BC5_RG_UNORM) return;
    if (textureClass != TextureClass_Normal && textureClass != TextureClass_MetallicRoughness) return;

    u8 x = textureClass == TextureClass_Normal ? 128 : 0;
    u8 y = textureClass == TextureClass_Normal ? 128 : 255;
    for (u32 mip = 0; mip < pages->mipCount; mip++)
    {
        u32 pageMipSize = Maxu32(TEXTURE_PAGE_SIZE >> mip, 1u);
        u32 defaultMipSize = Maxu32(16u >> mip, 1u);
        u32 blocksX = (pageMipSize + 3u) / 4u;
        u32 defaultBlocks = (defaultMipSize + 3u) / 4u;
        for (u32 row = 0; row < defaultBlocks; row++)
        {
            for (u32 col = 0; col < defaultBlocks; col++)
            {
                u8* block = pages->data[0][mip] + (row * blocksX + col) * 16u;
                WriteBC4ConstantBlock(block, x);
                WriteBC4ConstantBlock(block + 8, y);
            }
        }
    }
}

static bool CopyCompressedImageToPages(CompressedPageBuffers* pages, const Texture* src, u32 page, u32 x, u32 y)
{
    if (!src->buffer || src->bufferSize == 0) return false;

    BasisuTranscodedImage image;
    if (!BasisuTranscodeImage(src->buffer, src->bufferSize, pages->format, &image)) return false;

    u32 bytesPerBlock = SDL_GPUTextureFormatTexelBlockSize(pages->format);
    if (image.mipLevels == 0)
    {
        BasisuFreeTranscodedImage(&image);
        return false;
    }

    for (u32 mip = 0; mip < pages->mipCount; mip++)
    {
        u32 sourceMip = mip < image.mipLevels ? mip : image.mipLevels - 1u;
        BasisuTranscodedLevel* level = &image.levels[sourceMip];
        u32 mipPageSize = Maxu32(TEXTURE_PAGE_SIZE >> mip, 1u);
        u32 dstX = x >> mip;
        u32 dstY = y >> mip;
        u32 srcBlocksX = (level->width + 3u) / 4u;
        u32 srcBlocksY = (level->height + 3u) / 4u;
        u32 dstCopyWidth = Maxu32(src->width >> mip, 1u);
        u32 dstCopyHeight = Maxu32(src->height >> mip, 1u);
        u32 dstCopyBlocksX = (dstCopyWidth + 3u) / 4u;
        u32 dstCopyBlocksY = (dstCopyHeight + 3u) / 4u;
        u32 dstBlocksX = (mipPageSize + 3u) / 4u;
        u32 dstBlockX = dstX / 4u;
        u32 dstBlockY = dstY / 4u;
        const u8* srcBlocks = (const u8*)image.data + level->offset;
        u8* dstBlocks = pages->data[page][mip];

        for (u32 row = 0; row < dstCopyBlocksY; row++)
        {
            u32 srcRow = (row * srcBlocksY) / dstCopyBlocksY;
            for (u32 col = 0; col < dstCopyBlocksX; col++)
            {
                u32 srcCol = (col * srcBlocksX) / dstCopyBlocksX;
                u32 dstOffset = ((dstBlockY + row) * dstBlocksX + dstBlockX + col) * bytesPerBlock;
                u32 srcOffset = (srcRow * srcBlocksX + srcCol) * bytesPerBlock;
                MemCopy(dstBlocks + dstOffset, srcBlocks + srcOffset, bytesPerBlock);
            }
        }
    }

    BasisuFreeTranscodedImage(&image);
    return true;
}

static Texture CreateCompressedPageTexture(CompressedPageBuffers* pages, const char* label)
{
    Texture texture = {0};
    texture.width = TEXTURE_PAGE_SIZE;
    texture.height = TEXTURE_PAGE_SIZE;
    texture.format = pages->format;
    texture.mipLevels = pages->mipCount;

    SDL_GPUTextureCreateInfo texDesc = {
        .type = SDL_GPU_TEXTURETYPE_2D_ARRAY,
        .format = pages->format,
        .width = TEXTURE_PAGE_SIZE,
        .height = TEXTURE_PAGE_SIZE,
        .layer_count_or_depth = TEXTURE_PAGE_LAYERS,
        .num_levels = pages->mipCount,
        .sample_count = SDL_GPU_SAMPLECOUNT_1,
        .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
        .props = SDL_CreateProperties()
    };
    if (label) SDL_SetStringProperty(texDesc.props, SDL_PROP_GPU_TEXTURE_CREATE_NAME_STRING, label);
    texture.handle = SDL_CreateGPUTexture(g_GPUDevice, &texDesc);
    SDL_DestroyProperties(texDesc.props);
    if (!texture.handle) return texture;

    u32 totalUploadSize = 0;
    for (u32 layer = 0; layer < TEXTURE_PAGE_LAYERS; layer++)
        for (u32 mip = 0; mip < pages->mipCount; mip++)
            totalUploadSize += pages->size[mip];

    SDL_GPUTransferBuffer* transferBuffer = SDL_CreateGPUTransferBuffer(g_GPUDevice, &(SDL_GPUTransferBufferCreateInfo){
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = totalUploadSize
    });

    u8* dst = (u8*)SDL_MapGPUTransferBuffer(g_GPUDevice, transferBuffer, false);
    u32 offset = 0;
    for (u32 layer = 0; layer < TEXTURE_PAGE_LAYERS; layer++)
    {
        for (u32 mip = 0; mip < pages->mipCount; mip++)
        {
            MemCopy(dst + offset, pages->data[layer][mip], pages->size[mip]);
            offset += pages->size[mip];
        }
    }
    SDL_UnmapGPUTransferBuffer(g_GPUDevice, transferBuffer);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(g_GPUDevice);
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
    offset = 0;
    for (u32 layer = 0; layer < TEXTURE_PAGE_LAYERS; layer++)
    {
        for (u32 mip = 0; mip < pages->mipCount; mip++)
        {
            u32 mipSize = Maxu32(TEXTURE_PAGE_SIZE >> mip, 1u);
            SDL_GPUTextureTransferInfo transferInfo = { .transfer_buffer = transferBuffer, .offset = offset };
            SDL_GPUTextureRegion region = {
                .texture = texture.handle,
                .mip_level = mip,
                .layer = layer,
                .w = mipSize,
                .h = mipSize,
                .d = 1
            };
            SDL_UploadToGPUTexture(copyPass, &transferInfo, &region, false);
            offset += pages->size[mip];
        }
    }
    SDL_EndGPUCopyPass(copyPass);
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    SDL_WaitForGPUFences(g_GPUDevice, true, &fence, 1);
    SDL_ReleaseGPUFence(g_GPUDevice, fence);
    SDL_ReleaseGPUTransferBuffer(g_GPUDevice, transferBuffer);
    return texture;
}

static void ReleaseTextureBasisBuffers(Texture* textures)
{
    for (u32 i = 0; i < MAX_SCENE_TEXTURES; i++)
    {
        if (textures[i].buffer && textures[i].bufferSize)
        {
            SDL_free(textures[i].buffer);
            textures[i].buffer = NULL;
            textures[i].bufferSize = 0;
        }
    }
}

static void ReleasePageTexture(Texture* texture)
{
    if (texture->handle) SDL_ReleaseGPUTexture(g_GPUDevice, texture->handle);
    *texture = (Texture){0};
}

static u32 PackTextureRects(Texture* textures, const bool* wanted, u32 alignment, u32 reserveSize, bool requireBasis, TexturePackResult* out)
{
    stbrp_rect* rects = (stbrp_rect*)ArenaPushGlobal(MAX_SCENE_TEXTURES * sizeof(stbrp_rect));
    bool packed[MAX_SCENE_TEXTURES] = {0};
    u32 numRects = 0;
    out->count = 0;

    for (u32 i = 0; i < MAX_SCENE_TEXTURES; i++)
    {
        Texture* tex = &textures[i];
        if (!wanted[i] || tex->width <= 0 || tex->height <= 0) continue;
        if (requireBasis)
        {
            if (!tex->buffer || tex->bufferSize == 0) continue;
        }
        else if (!tex->handle) continue;

        u32 physicalW = AlignUpu32((u32)tex->width, alignment);
        u32 physicalH = AlignUpu32((u32)tex->height, alignment);
        if (physicalW > TEXTURE_PAGE_SIZE || physicalH > TEXTURE_PAGE_SIZE) continue;
        if (numRects >= MAX_SCENE_TEXTURES) break;
        rects[numRects].id = (int)i;
        rects[numRects].w = (stbrp_coord)physicalW;
        rects[numRects].h = (stbrp_coord)physicalH;
        rects[numRects].was_packed = 0;
        numRects++;
    }

    u32 remaining = numRects;
    stbrp_rect* pageRects = ArenaPushGlobal((MAX_SCENE_TEXTURES + 1) * sizeof(stbrp_rect));
    stbrp_node* nodes = AllocateTLSFGlobal(TEXTURE_PAGE_SIZE * sizeof(stbrp_node));
    for (u32 page = 0; page < TEXTURE_PAGE_LAYERS && remaining > 0; page++)
    {
        u32 pageRectCount = 0;
        if (page == 0 && reserveSize > 0)
        {
            pageRects[pageRectCount].id = -1;
            pageRects[pageRectCount].w = (stbrp_coord)AlignUpu32(reserveSize, alignment);
            pageRects[pageRectCount].h = (stbrp_coord)AlignUpu32(reserveSize, alignment);
            pageRects[pageRectCount].was_packed = 0;
            pageRectCount++;
        }
        for (u32 r = 0; r < numRects; r++)
            if (!packed[r]) pageRects[pageRectCount++] = rects[r];

        stbrp_context ctx;
        stbrp_init_target(&ctx, TEXTURE_PAGE_SIZE, TEXTURE_PAGE_SIZE, nodes, TEXTURE_PAGE_SIZE);
        stbrp_pack_rects(&ctx, pageRects, (int)pageRectCount);

        for (u32 pr = 0; pr < pageRectCount; pr++)
        {
            if (pageRects[pr].id < 0 || !pageRects[pr].was_packed) continue;
            u32 imageIdx = (u32)pageRects[pr].id;
            for (u32 r = 0; r < numRects; r++)
            {
                if ((u32)rects[r].id == imageIdx)
                {
                    packed[r] = true;
                    remaining--;
                    break;
                }
            }
            if (out->count >= MAX_SCENE_TEXTURES) break;
            Texture* tex = &textures[imageIdx];
            out->placements[out->count++] = (TexturePlacement){
                imageIdx, page, (u32)pageRects[pr].x, (u32)pageRects[pr].y,
                (u32)tex->width, (u32)tex->height, 0u
            };
        }
    }
    DeAllocateTLSFGlobal(nodes);
    ArenaPopGlobal((MAX_SCENE_TEXTURES + 1) * sizeof(stbrp_rect));
    ArenaPopGlobal(MAX_SCENE_TEXTURES * sizeof(stbrp_rect));
    if (remaining > 0)
        AX_WARN("texture page packer ran out of %d pages", TEXTURE_PAGE_LAYERS);
    return numRects;
}

static bool PackClassCompressed(Texture* textures, bool* wanted, u32 textureClass, SDL_GPUTextureFormat format, Texture* outTexture)
{
    if (!IsBlockCompressedFormat(format)) return false;

    TextureClassInfo info = GetTextureClassInfo(textureClass);
    for (u32 i = 0; i < MAX_SCENE_TEXTURES; i++) info.descriptorMap[i] = info.defaultDescriptor;

    TexturePackResult pack;
    u32 numRects = PackTextureRects(textures, wanted, CompressedPageAlign, 16, true, &pack);

    CompressedPageBuffers pages;
    if (!AllocateCompressedPageBuffers(&pages, format, Mins32((s32)MipCountForSize(TEXTURE_PAGE_SIZE, TEXTURE_PAGE_SIZE), CompressedPageMaxMips))) return false;
    FillCompressedDefaultBlocks(&pages, textureClass);

    for (u32 i = 0; i < pack.count; i++)
    {
        TexturePlacement* placement = &pack.placements[i];
        Texture* src = &textures[placement->imageIndex];
        u32 descIdx = AddDescriptor(placement->page, (float)placement->x, (float)placement->y, (float)src->width, (float)src->height);
        info.descriptorMap[placement->imageIndex] = descIdx;
        if (!CopyCompressedImageToPages(&pages, src, placement->page, placement->x, placement->y))
        {
            FreeCompressedPageBuffers(&pages);
            return false;
        }
    }

    u32 uploadedMipCount = pages.mipCount;
    *outTexture = CreateCompressedPageTexture(&pages, info.label);
    FreeCompressedPageBuffers(&pages);
    if (!outTexture->handle) return false;
    AX_LOG("compressed texture class %d packed %d/%d mips=%d", textureClass, pack.count, numRects, uploadedMipCount);
    return true;
}

static bool TryBuildCompressedPages(Texture* textures, bool wanted[TextureClass_Count][MAX_SCENE_TEXTURES], SDL_GPUTextureFormat albedoFormat, SDL_GPUTextureFormat rgFormat)
{
    if (!IsBlockCompressedFormat(albedoFormat) || !IsBlockCompressedFormat(rgFormat)) return false;

    Texture albedo = {0};
    Texture normal = {0};
    Texture mr = {0};
    u32 oldDescriptorCount = g_RenderState.numTextureDescriptors;

    if (!PackClassCompressed(textures, wanted[TextureClass_Albedo], TextureClass_Albedo, albedoFormat, &albedo)) goto fail;
    if (!PackClassCompressed(textures, wanted[TextureClass_Normal], TextureClass_Normal, rgFormat, &normal)) goto fail;
    if (!PackClassCompressed(textures, wanted[TextureClass_MetallicRoughness], TextureClass_MetallicRoughness, rgFormat, &mr)) goto fail;

    g_RenderState.albedoPages = albedo;
    g_RenderState.normalPages = normal;
    g_RenderState.metallicRoughnessPages = mr;
    return true;

fail:
    ReleasePageTexture(&albedo);
    ReleasePageTexture(&normal);
    ReleasePageTexture(&mr);
    g_RenderState.numTextureDescriptors = oldDescriptorCount;
    return false;
}

static void PackClass(Texture* textures, bool* wanted, u32 textureClass)
{
    TextureClassInfo info = GetTextureClassInfo(textureClass);
    for (u32 i = 0; i < MAX_SCENE_TEXTURES; i++) info.descriptorMap[i] = info.defaultDescriptor;

    TexturePackResult pack;
    u32 numRects = PackTextureRects(textures, wanted, 1u, 16u, false, &pack);
    for (u32 i = 0; i < pack.count; i++)
    {
        TexturePlacement* placement = &pack.placements[i];
        Texture* src = &textures[placement->imageIndex];
        u32 descIdx = AddDescriptor(placement->page, (float)placement->x, (float)placement->y,
                                    (float)src->width, (float)src->height);
        info.descriptorMap[placement->imageIndex] = descIdx;
        QueuePackedTextureCopy(textureClass, placement->imageIndex, src, placement->page, placement->x, placement->y, placement->padding);
    }
    AX_LOG("texture class %d packed %d/%d", textureClass, pack.count, numRects);
}

void TextureSystem_BuildPages(SceneBundle** bundles, const u32* imageOffsets, u32 numBundles, Texture* textures)
{
    double startTime = TimeSinceStartup();    
    bool wanted[TextureClass_Count][MAX_SCENE_TEXTURES] = {0};
    g_RenderState.numMaterials = 0;
    InitTextureSystem();

    for (u32 b = 0; b < numBundles; b++)
    {
        SceneBundle* bundle = bundles[b];
        bundle->imageOffset = (int)imageOffsets[b];
        bundle->materialOffset = (int)g_RenderState.numMaterials;

        for (s32 m = 0; m < bundle->numMaterials; m++)
        {
            AMaterial* src = &bundle->materials[m];
            u32 imageIndex;
            if (ResolveGlobalImageIndex(bundle, imageOffsets[b], src->textures[0].index, &imageIndex))
                MarkWantedTexture(wanted, TextureClass_Normal, imageIndex);
            if (ResolveGlobalImageIndex(bundle, imageOffsets[b], src->metallicRoughnessTexture.index, &imageIndex))
                MarkWantedTexture(wanted, TextureClass_MetallicRoughness, imageIndex);
            // Base color wins conflicts because it is visible albedo, while MR/normal can fall back safely.
            if (ResolveGlobalImageIndex(bundle, imageOffsets[b], src->baseColorTexture.index, &imageIndex))
                MarkWantedTexture(wanted, TextureClass_Albedo, imageIndex);
        }

        u32 totalAlbedo = 0, totalNormal = 0, totalMR = 0;
        CountWantedTextures(wanted, &totalAlbedo, &totalNormal, &totalMR);
        AX_LOG("bundle material textures materials=%d images=%d wantedTotals albedo=%d normal=%d mr=%d offset=%d",
               bundle->numMaterials, bundle->numImages, totalAlbedo, totalNormal, totalMR, imageOffsets[b]);
        if (g_RenderState.numMaterials + (u32)bundle->numMaterials > MAX_GPU_MATERIALS)
        {
            AX_ERROR("maximum GPU materials reached");
            ReleaseTextureBasisBuffers(textures);
            return;
        }
        g_RenderState.numMaterials += (u32)bundle->numMaterials;
    }

    SDL_GPUTextureFormat albedoFormat = SelectAlbedoPageFormat();
    SDL_GPUTextureFormat rgFormat = SelectRGPageFormat();
    bool compressedPages = false;

    if (IsBlockCompressedFormat(albedoFormat) && IsBlockCompressedFormat(rgFormat))
    {
        compressedPages = TryBuildCompressedPages(textures, wanted, albedoFormat, rgFormat);
        if (compressedPages)
            AX_LOG("compressed texture pages built %.2fs", TimeSinceStartup() - startTime);
        else
        {
            AX_WARN("compressed texture page build failed, falling back to uncompressed pages");
            ReleasePageTexture(&g_RenderState.albedoPages);
            ReleasePageTexture(&g_RenderState.normalPages);
            ReleasePageTexture(&g_RenderState.metallicRoughnessPages);
            gNumCopyRequests = 0;
            AddDefaultDescriptors();
        }
    }

    if (!compressedPages)
    {
        const SDL_GPUTextureUsageFlags pageFlags = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;
        g_RenderState.albedoPages = rCreateTexture2DArray(TEXTURE_PAGE_SIZE, TEXTURE_PAGE_SIZE, TEXTURE_PAGE_LAYERS, NULL,
                                                          SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, TexFlags_MipMap, pageFlags, "AlbedoPages");
        g_RenderState.normalPages = rCreateTexture2DArray(TEXTURE_PAGE_SIZE, TEXTURE_PAGE_SIZE, TEXTURE_PAGE_LAYERS, NULL,
                                                          SDL_GPU_TEXTUREFORMAT_R8G8_UNORM, TexFlags_MipMap, pageFlags, "NormalPages");
        g_RenderState.metallicRoughnessPages = rCreateTexture2DArray(TEXTURE_PAGE_SIZE, TEXTURE_PAGE_SIZE, TEXTURE_PAGE_LAYERS, NULL,
                                                          SDL_GPU_TEXTUREFORMAT_R8G8_UNORM, TexFlags_MipMap, pageFlags, "MetallicRoughnessPages");
        PackClass(textures, wanted[TextureClass_Albedo], TextureClass_Albedo);
        PackClass(textures, wanted[TextureClass_Normal], TextureClass_Normal);
        PackClass(textures, wanted[TextureClass_MetallicRoughness], TextureClass_MetallicRoughness);
        DispatchPageCopies(textures);
        AX_LOG("texture page gpu copies complete requests=%d %.2fs", gNumCopyRequests, TimeSinceStartup() - startTime);
        UploadDefaultPages();
    }

    g_RenderState.numMaterials = 0;
    for (u32 b = 0; b < numBundles; b++)
    {
        SceneBundle* bundle = bundles[b];
        for (s32 m = 0; m < bundle->numMaterials; m++)
        {
            if (g_RenderState.numMaterials >= MAX_GPU_MATERIALS)
            {
                AX_ERROR("maximum GPU materials reached");
                break;
            }
            AMaterial* src = &bundle->materials[m];
            MaterialGPU* dst = &gMaterials[g_RenderState.numMaterials++];
            dst->albedoDescriptor = 1;
            dst->normalDescriptor = 2;
            dst->metallicRoughnessDescriptor = 3;
            dst->flags = PackMaterialFlags(src);
            dst->baseColorFactor = src->baseColorFactor;
            dst->metallicRoughnessFactor = ((u32)src->metallicFactor << 16u) | src->roughnessFactor;
            u32 imageIndex;

            if (ResolveGlobalImageIndex(bundle, imageOffsets[b], src->baseColorTexture.index, &imageIndex))
                dst->albedoDescriptor = gAlbedoDescriptor[imageIndex];
            if (ResolveGlobalImageIndex(bundle, imageOffsets[b], src->textures[0].index, &imageIndex))
                dst->normalDescriptor = gNormalDescriptor[imageIndex];
            if (ResolveGlobalImageIndex(bundle, imageOffsets[b], src->metallicRoughnessTexture.index, &imageIndex))
                dst->metallicRoughnessDescriptor = gMetallicRoughnessDescriptor[imageIndex];
        }
    }
    size_t descriptorOffset = 0;
    size_t materialOffset = 0;
    UpdateGPUBuffer(g_RenderState.textureDescriptorBuffer,  gTextureDescriptors, sizeof(TextureDescriptor) * MAX_TEXTURE_DESCRIPTORS, descriptorOffset);
    UpdateGPUBuffer(g_RenderState.materialBuffer, gMaterials, sizeof(MaterialGPU) * MAX_GPU_MATERIALS, descriptorOffset);
    ReleaseTextureBasisBuffers(textures);
    AX_LOG("texture system ready descriptors=%d materials=%d %.2fs", g_RenderState.numTextureDescriptors, g_RenderState.numMaterials, TimeSinceStartup() - startTime);
}
