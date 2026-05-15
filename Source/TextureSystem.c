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
enum { TextureDesc_DefaultAlbedo = 1u, TextureDesc_DefaultNormal = 2u, TextureDesc_DefaultMetallicRoughness = 4u };
enum { CompressedPageAlign = 512u, CompressedPageMaxMips = 8u };

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
    if (!gCopyRGBAPipeline)
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
        CHECK_CREATE(gCopyRGBAPipeline, "Texture Page Copy RGBA Pipeline")
    }

    if (!gCopyRGPipeline)
    {
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
        CHECK_CREATE(gCopyRGPipeline, "Texture Page Copy RG Pipeline")
    }
}

static u32 AddDescriptor(u32 pageIndex, float x, float y, float w, float h)
{
    u32 idx = g_RenderState.numTextureDescriptors++;
    if (idx >= MAX_TEXTURE_DESCRIPTORS)
    {
        AX_ERROR("maximum texture descriptors reached");
        return 0;
    }

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
    u32 idx = AddDescriptor(pageIndex, x, y, w, h);
    gTextureDescriptors[idx].flags = flags;
    return idx;
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

static Texture* PageTextureForClass(u32 textureClass)
{
    if (textureClass == TextureClass_Normal) return &g_RenderState.normalPages;
    if (textureClass == TextureClass_MetallicRoughness) return &g_RenderState.metallicRoughnessPages;
    return &g_RenderState.albedoPages;
}

static u32* DescriptorMapForClass(u32 textureClass)
{
    if (textureClass == TextureClass_Normal) return gNormalDescriptor;
    if (textureClass == TextureClass_MetallicRoughness) return gMetallicRoughnessDescriptor;
    return gAlbedoDescriptor;
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
        Texture* dst = PageTextureForClass(req->textureClass);
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
                req->textureClass == TextureClass_MetallicRoughness ? 1u : 0u
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
    SDL_memset(pages, 0, sizeof(*pages));
}

static bool AllocateCompressedPageBuffers(CompressedPageBuffers* pages, SDL_GPUTextureFormat format, u32 mipCount)
{
    SDL_memset(pages, 0, sizeof(*pages));
    pages->format = format;
    pages->mipCount = Mini32((s32)mipCount, CompressedPageMaxMips);
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

static bool CopyCompressedImageToPages(CompressedPageBuffers* pages, const Texture* src, u32 page, u32 x, u32 y)
{
    if (!src->buffer || src->bufferSize == 0) return false;

    BasisuTranscodedImage image;
    if (!BasisuTranscodeImage(src->buffer, src->bufferSize, pages->format, &image)) return false;

    u32 bytesPerBlock = SDL_GPUTextureFormatTexelBlockSize(pages->format);
    u32 mipCount = Mini32((s32)pages->mipCount, (s32)image.mipLevels);
    for (u32 mip = 0; mip < mipCount; mip++)
    {
        BasisuTranscodedLevel* level = &image.levels[mip];
        u32 mipPageSize = Maxu32(TEXTURE_PAGE_SIZE >> mip, 1u);
        u32 dstX = x >> mip;
        u32 dstY = y >> mip;
        if ((dstX & 3u) || (dstY & 3u))
        {
            BasisuFreeTranscodedImage(&image);
            return false;
        }

        u32 srcBlocksX = (level->width + 3u) / 4u;
        u32 srcBlocksY = (level->height + 3u) / 4u;
        u32 dstBlocksX = (mipPageSize + 3u) / 4u;
        u32 dstBlockX = dstX / 4u;
        u32 dstBlockY = dstY / 4u;
        const u8* srcBlocks = (const u8*)image.data + level->offset;
        u8* dstBlocks = pages->data[page][mip];

        for (u32 row = 0; row < srcBlocksY; row++)
        {
            u32 dstOffset = ((dstBlockY + row) * dstBlocksX + dstBlockX) * bytesPerBlock;
            u32 srcOffset = row * srcBlocksX * bytesPerBlock;
            MemCopy(dstBlocks + dstOffset, srcBlocks + srcOffset, srcBlocksX * bytesPerBlock);
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

    u32 maxUploadSize = 0;
    for (u32 mip = 0; mip < pages->mipCount; mip++)
        if (pages->size[mip] > maxUploadSize) maxUploadSize = pages->size[mip];

    SDL_GPUTransferBuffer* transferBuffer = SDL_CreateGPUTransferBuffer(g_GPUDevice, &(SDL_GPUTransferBufferCreateInfo){
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = maxUploadSize
    });

    for (u32 layer = 0; layer < TEXTURE_PAGE_LAYERS; layer++)
    {
        for (u32 mip = 0; mip < pages->mipCount; mip++)
        {
            u8* dst = (u8*)SDL_MapGPUTransferBuffer(g_GPUDevice, transferBuffer, false);
            MemCopy(dst, pages->data[layer][mip], pages->size[mip]);
            SDL_UnmapGPUTransferBuffer(g_GPUDevice, transferBuffer);

            SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(g_GPUDevice);
            SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
            u32 mipSize = Maxu32(TEXTURE_PAGE_SIZE >> mip, 1u);
            SDL_GPUTextureTransferInfo transferInfo = { .transfer_buffer = transferBuffer, .offset = 0 };
            SDL_GPUTextureRegion region = {
                .texture = texture.handle,
                .mip_level = mip,
                .layer = layer,
                .w = mipSize,
                .h = mipSize,
                .d = 1
            };
            SDL_UploadToGPUTexture(copyPass, &transferInfo, &region, false);
            SDL_EndGPUCopyPass(copyPass);
            SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
            SDL_WaitForGPUFences(g_GPUDevice, true, &fence, 1);
            SDL_ReleaseGPUFence(g_GPUDevice, fence);
        }
    }
    SDL_ReleaseGPUTransferBuffer(g_GPUDevice, transferBuffer);
    return texture;
}

static void ReleaseTextureBasisBuffers(Texture* textures)
{
    for (u32 i = 0; i < MAX_SCENE_TEXTURES; i++)
    {
        if (textures[i].buffer && textures[i].bufferSize)
        {
            OSFree(textures[i].buffer, textures[i].bufferSize);
            textures[i].buffer = NULL;
            textures[i].bufferSize = 0;
        }
    }
}

static bool PackClassCompressed(Texture* textures, bool* wanted, u32 textureClass, SDL_GPUTextureFormat format, Texture* outTexture)
{
    if (!IsBlockCompressedFormat(format)) return false;

    u32* descMap = DescriptorMapForClass(textureClass);
    stbrp_rect rects[MAX_SCENE_TEXTURES];
    bool packed[MAX_SCENE_TEXTURES] = {0};
    u32 numRects = 0;
    u32 packedCount = 0;
    u32 classMipCount = CompressedPageMaxMips;

    for (u32 i = 0; i < MAX_SCENE_TEXTURES; i++)
    {
        descMap[i] = textureClass + 1u;
        if (!wanted[i] || !textures[i].buffer || textures[i].bufferSize == 0 || textures[i].width <= 0 || textures[i].height <= 0) continue;
        if (textures[i].mipLevels > 0 && textures[i].mipLevels < classMipCount) classMipCount = textures[i].mipLevels;
        u32 physicalW = AlignUpu32((u32)textures[i].width, CompressedPageAlign);
        u32 physicalH = AlignUpu32((u32)textures[i].height, CompressedPageAlign);
        if (physicalW > TEXTURE_PAGE_SIZE || physicalH > TEXTURE_PAGE_SIZE) continue;
        rects[numRects].id = (int)i;
        rects[numRects].w = (stbrp_coord)(physicalW / CompressedPageAlign);
        rects[numRects].h = (stbrp_coord)(physicalH / CompressedPageAlign);
        rects[numRects].was_packed = 0;
        numRects++;
    }

    CompressedPageBuffers pages;
    if (!AllocateCompressedPageBuffers(&pages, format, classMipCount)) return false;

    u32 remaining = numRects;
    for (u32 page = 0; page < TEXTURE_PAGE_LAYERS && remaining > 0; page++)
    {
        stbrp_rect pageRects[MAX_SCENE_TEXTURES + 1];
        u32 pageRectCount = 0;
        if (page == 0)
        {
            pageRects[pageRectCount].id = -1;
            pageRects[pageRectCount].w = 1;
            pageRects[pageRectCount].h = 1;
            pageRects[pageRectCount].was_packed = 0;
            pageRectCount++;
        }
        for (u32 r = 0; r < numRects; r++)
            if (!packed[r]) pageRects[pageRectCount++] = rects[r];

        stbrp_context ctx;
        stbrp_node nodes[TEXTURE_PAGE_SIZE / CompressedPageAlign];
        stbrp_init_target(&ctx, TEXTURE_PAGE_SIZE / CompressedPageAlign, TEXTURE_PAGE_SIZE / CompressedPageAlign,
                          nodes, TEXTURE_PAGE_SIZE / CompressedPageAlign);
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

            Texture* src = &textures[imageIdx];
            u32 dstX = (u32)pageRects[pr].x * CompressedPageAlign;
            u32 dstY = (u32)pageRects[pr].y * CompressedPageAlign;
            u32 descIdx = AddDescriptor(page, (float)dstX, (float)dstY, (float)src->width, (float)src->height);
            descMap[imageIdx] = descIdx;
            if (!CopyCompressedImageToPages(&pages, src, page, dstX, dstY))
            {
                FreeCompressedPageBuffers(&pages);
                return false;
            }
            packedCount++;
        }
    }

    if (remaining > 0)
    {
        AX_WARN("compressed texture page packer ran out of %d pages for class %d", TEXTURE_PAGE_LAYERS, textureClass);
    }

    u32 uploadedMipCount = pages.mipCount;
    *outTexture = CreateCompressedPageTexture(&pages,
        textureClass == TextureClass_Albedo ? "AlbedoPages" :
        textureClass == TextureClass_Normal ? "NormalPages" : "MetallicRoughnessPages");
    FreeCompressedPageBuffers(&pages);
    if (!outTexture->handle) return false;
    AX_LOG("compressed texture class %d packed %d/%d mips=%d", textureClass, packedCount, numRects, uploadedMipCount);
    return true;
}

static void PackClass(Texture* textures, bool* wanted, u32 textureClass)
{
    enum { AtlasPadding = 0, DefaultReserveSize = 16 };
    u32* descMap = DescriptorMapForClass(textureClass);
    stbrp_rect rects[MAX_SCENE_TEXTURES];
    bool packed[MAX_SCENE_TEXTURES] = {0};
    u32 numRects = 0;
    u32 packedCount = 0;

    for (u32 i = 0; i < MAX_SCENE_TEXTURES; i++)
    {
        descMap[i] = textureClass + 1u;
        if (!wanted[i] || !textures[i].handle || textures[i].width <= 0 || textures[i].height <= 0) continue;
        if (numRects >= MAX_SCENE_TEXTURES) break;
        u32 pad = ((u32)textures[i].width + AtlasPadding * 2u <= TEXTURE_PAGE_SIZE &&
                   (u32)textures[i].height + AtlasPadding * 2u <= TEXTURE_PAGE_SIZE) ? AtlasPadding : 0u;
        rects[numRects].id = (int)i;
        rects[numRects].w = (stbrp_coord)((u32)textures[i].width + pad * 2u);
        rects[numRects].h = (stbrp_coord)((u32)textures[i].height + pad * 2u);
        rects[numRects].was_packed = 0;
        numRects++;
    }

    u32 remaining = numRects;
    for (u32 page = 0; page < TEXTURE_PAGE_LAYERS && remaining > 0; page++)
    {
        stbrp_rect pageRects[MAX_SCENE_TEXTURES + 1];
        u32 pageRectCount = 0;
        if (page == 0)
        {
            pageRects[pageRectCount].id = -1;
            pageRects[pageRectCount].w = DefaultReserveSize;
            pageRects[pageRectCount].h = DefaultReserveSize;
            pageRects[pageRectCount].was_packed = 0;
            pageRectCount++;
        }
        for (u32 r = 0; r < numRects; r++)
        {
            if (!packed[r])
                pageRects[pageRectCount++] = rects[r];
        }

        stbrp_context ctx;
        stbrp_node nodes[TEXTURE_PAGE_SIZE];
        stbrp_init_target(&ctx, TEXTURE_PAGE_SIZE, TEXTURE_PAGE_SIZE, nodes, TEXTURE_PAGE_SIZE);
        stbrp_pack_rects(&ctx, pageRects, (int)pageRectCount);

        for (u32 pr = 0; pr < pageRectCount; pr++)
        {
            if (pageRects[pr].id < 0) continue;
            if (!pageRects[pr].was_packed) continue;
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

            Texture* src = &textures[imageIdx];
            u32 pad = ((u32)src->width + AtlasPadding * 2u <= TEXTURE_PAGE_SIZE &&
                       (u32)src->height + AtlasPadding * 2u <= TEXTURE_PAGE_SIZE) ? AtlasPadding : 0u;
            u32 dstX = (u32)pageRects[pr].x + pad;
            u32 dstY = (u32)pageRects[pr].y + pad;
            u32 descIdx = AddDescriptor(page, (float)dstX, (float)dstY,
                                        (float)src->width, (float)src->height);
            descMap[imageIdx] = descIdx;
            QueuePackedTextureCopy(textureClass, imageIdx, src, page, dstX, dstY, pad);
            packedCount++;
        }
    }

    if (remaining > 0)
        AX_WARN("texture page packer ran out of %d pages for class %d", TEXTURE_PAGE_LAYERS, textureClass);
    AX_LOG("texture class %d packed %d/%d", textureClass, packedCount, numRects);
}

void TextureSystem_BuildPages(SceneBundle** bundles, const u32* imageOffsets, u32 numBundles, Texture* textures)
{
    double startTime = TimeSinceStartup();    
    bool wanted[TextureClass_Count][MAX_SCENE_TEXTURES] = {0};
    g_RenderState.numTextureDescriptors = 0;
    g_RenderState.numMaterials = 0;
    gNumCopyRequests = 0;

    AddDescriptorFlags(0, 0, 0, 16, 16, TextureDesc_DefaultAlbedo); // reserved invalid
    AddDescriptorFlags(0, 0, 0, 16, 16, TextureDesc_DefaultAlbedo); // default albedo
    AddDescriptorFlags(0, 0, 0, 16, 16, TextureDesc_DefaultNormal); // default normal
    AddDescriptorFlags(0, 0, 0, 16, 16, TextureDesc_DefaultMetallicRoughness); // default metallic roughness

    for (u32 b = 0; b < numBundles; b++)
    {
        SceneBundle* bundle = bundles[b];
        bundle->imageOffset = (int)imageOffsets[b];
        bundle->materialOffset = (int)g_RenderState.numMaterials;

        u32 albedoImages = 0, normalImages = 0, mrImages = 0;
        for (s32 imageIdx = 0; imageIdx < bundle->numImages; imageIdx++)
        {
            u32 globalImage = imageOffsets[b] + (u32)imageIdx;
            if (globalImage >= MAX_SCENE_TEXTURES || !textures[globalImage].handle) continue;
            u32 type = textures[globalImage].type;
            if (type & 1u)
            {
                wanted[TextureClass_Normal][globalImage] = true;
                normalImages++;
            }
            else if (type & 2u)
            {
                wanted[TextureClass_MetallicRoughness][globalImage] = true;
                mrImages++;
            }
            else
            {
                wanted[TextureClass_Albedo][globalImage] = true;
                albedoImages++;
            }
        }
        AX_LOG("bundle texture images materials=%d images=%d albedo=%d normal=%d mr=%d offset=%d",
               bundle->numMaterials, bundle->numImages, albedoImages, normalImages, mrImages, imageOffsets[b]);

        for (s32 m = 0; m < bundle->numMaterials; m++)
        {
            AMaterial* src = &bundle->materials[m];
            u32 imageIndex;
            if (ResolveGlobalImageIndex(bundle, imageOffsets[b], src->baseColorTexture.index, &imageIndex))
                wanted[TextureClass_Albedo][imageIndex] = true;
            if (ResolveGlobalImageIndex(bundle, imageOffsets[b], src->textures[0].index, &imageIndex))
                wanted[TextureClass_Normal][imageIndex] = true;
            if (ResolveGlobalImageIndex(bundle, imageOffsets[b], src->metallicRoughnessTexture.index, &imageIndex))
                wanted[TextureClass_MetallicRoughness][imageIndex] = true;
        }
        g_RenderState.numMaterials += (u32)bundle->numMaterials;
    }

    SDL_GPUTextureFormat albedoFormat = SelectAlbedoPageFormat();
    SDL_GPUTextureFormat rgFormat = SelectRGPageFormat();
    bool compressedPages = IsBlockCompressedFormat(albedoFormat) && IsBlockCompressedFormat(rgFormat);

    if (compressedPages)
    {
        compressedPages = PackClassCompressed(textures, wanted[TextureClass_Albedo], TextureClass_Albedo, albedoFormat, &g_RenderState.albedoPages) &&
                          PackClassCompressed(textures, wanted[TextureClass_Normal], TextureClass_Normal, rgFormat, &g_RenderState.normalPages) &&
                          PackClassCompressed(textures, wanted[TextureClass_MetallicRoughness], TextureClass_MetallicRoughness, rgFormat, &g_RenderState.metallicRoughnessPages);
        if (compressedPages)
            AX_LOG("compressed texture pages built %.2fs", TimeSinceStartup() - startTime);
        else
        {
            AX_WARN("compressed texture page build failed, falling back to uncompressed pages");
            if (g_RenderState.albedoPages.handle) SDL_ReleaseGPUTexture(g_GPUDevice, g_RenderState.albedoPages.handle);
            if (g_RenderState.normalPages.handle) SDL_ReleaseGPUTexture(g_GPUDevice, g_RenderState.normalPages.handle);
            if (g_RenderState.metallicRoughnessPages.handle) SDL_ReleaseGPUTexture(g_GPUDevice, g_RenderState.metallicRoughnessPages.handle);
            
            g_RenderState.albedoPages = (Texture){0};
            g_RenderState.normalPages = (Texture){0};
            g_RenderState.metallicRoughnessPages = (Texture){0};
            g_RenderState.numTextureDescriptors = 0;
            gNumCopyRequests = 0;
            AddDescriptorFlags(0, 0, 0, 16, 16, TextureDesc_DefaultAlbedo);
            AddDescriptorFlags(0, 0, 0, 16, 16, TextureDesc_DefaultAlbedo);
            AddDescriptorFlags(0, 0, 0, 16, 16, TextureDesc_DefaultNormal);
            AddDescriptorFlags(0, 0, 0, 16, 16, TextureDesc_DefaultMetallicRoughness);
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
        AX_LOG("uncompressed texture pages created %.2fs", TimeSinceStartup() - startTime);
        PackClass(textures, wanted[TextureClass_Albedo], TextureClass_Albedo);
        AX_LOG("albedo pages packed %.2fs", TimeSinceStartup() - startTime);
        PackClass(textures, wanted[TextureClass_Normal], TextureClass_Normal);
        AX_LOG("normal pages packed %.2fs", TimeSinceStartup() - startTime);
        PackClass(textures, wanted[TextureClass_MetallicRoughness], TextureClass_MetallicRoughness);
        AX_LOG("metallic roughness pages packed %.2fs", TimeSinceStartup() - startTime);
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
            AMaterial* src = &bundle->materials[m];
            MaterialGPU* dst = &gMaterials[g_RenderState.numMaterials++];
            dst->albedoDescriptor = 1;
            dst->normalDescriptor = 2;
            dst->metallicRoughnessDescriptor = 3;
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

    g_RenderState.textureDescriptorBuffer = CreateBuffer(gTextureDescriptors, sizeof(TextureDescriptor) * MAX_TEXTURE_DESCRIPTORS,
        SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ, "TextureDescriptors");
    g_RenderState.materialBuffer = CreateBuffer(gMaterials, sizeof(MaterialGPU) * MAX_GPU_MATERIALS,
        SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ, "Materials");
    ReleaseTextureBasisBuffers(textures);
    AX_LOG("texture system ready descriptors=%d materials=%d %.2fs",
           g_RenderState.numTextureDescriptors, g_RenderState.numMaterials, TimeSinceStartup() - startTime);
}
