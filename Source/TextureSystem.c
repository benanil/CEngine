#define STB_RECT_PACK_IMPLEMENTATION
#include "Extern/stb/stb_rect_pack.h"

#include "Include/Graphics.h"
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

static TextureDescriptor gTextureDescriptors[MAX_TEXTURE_DESCRIPTORS];
static MaterialGPU gMaterials[MAX_GPU_MATERIALS];
static u32 gAlbedoDescriptor[MAX_SCENE_TEXTURES];
static u32 gNormalDescriptor[MAX_SCENE_TEXTURES];
static u32 gMetallicRoughnessDescriptor[MAX_SCENE_TEXTURES];

static SDL_GPUComputePipeline* gCopyRGBAPipeline;
static SDL_GPUComputePipeline* gCopyRGPipeline;

typedef struct PageCopyRequest_
{
    u32 textureClass;
    u32 sourceIndex;
    u32 page;
    u32 x, y;
    u32 width, height;
} PageCopyRequest;

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

static PageCopyRequest gCopyRequests[MAX_TEXTURE_DESCRIPTORS];
static u32 gNumCopyRequests;

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
    desc->uvScale[0] = w / (float)TEXTURE_PAGE_SIZE;
    desc->uvScale[1] = h / (float)TEXTURE_PAGE_SIZE;
    desc->uvBias[0] = x / (float)TEXTURE_PAGE_SIZE;
    desc->uvBias[1] = y / (float)TEXTURE_PAGE_SIZE;
    return idx;
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

static void QueuePackedTextureCopy(u32 textureClass, u32 sourceIndex, Texture* src, u32 page, u32 x, u32 y)
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
}

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

        u32 mipCount = MipCountForSize(req->width, req->height);
        if (src->mipLevels > 0 && src->mipLevels < mipCount) mipCount = src->mipLevels;
        for (u32 mip = 0; mip < mipCount; mip++)
        {
            u32 mipWidth = req->width >> mip;
            u32 mipHeight = req->height >> mip;
            if (mipWidth == 0) mipWidth = 1;
            if (mipHeight == 0) mipHeight = 1;
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
            struct { u32 dstOffset[2]; u32 copySize[2]; u32 sourceMip; u32 pad; } params = {
                { req->x >> mip, req->y >> mip },
                { mipWidth, mipHeight },
                mip,
                0
            };
            SDL_PushGPUComputeUniformData(cmd, 0, &params, sizeof(params));
            SDL_DispatchGPUCompute(pass, (mipWidth + 7u) / 8u, (mipHeight + 7u) / 8u, 1);
            SDL_EndGPUComputePass(pass);
        }
    }

    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    SDL_WaitForGPUFences(g_GPUDevice, true, &fence, 1);
    SDL_ReleaseGPUFence(g_GPUDevice, fence);
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
            QueuePackedTextureCopy(textureClass, imageIdx, src, page, dstX, dstY);
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

    AddDescriptor(0, 0, 0, 16, 16); // reserved invalid
    AddDescriptor(0, 0, 0, 16, 16); // default albedo
    AddDescriptor(0, 0, 0, 16, 16); // default normal
    AddDescriptor(0, 0, 0, 16, 16); // default metallic roughness
    
    const SDL_GPUTextureUsageFlags pageFlags = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;

    g_RenderState.albedoPages = rCreateTexture2DArray(TEXTURE_PAGE_SIZE, TEXTURE_PAGE_SIZE, TEXTURE_PAGE_LAYERS, NULL,
                                                      SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, TexFlags_MipMap, pageFlags, "AlbedoPages");
    g_RenderState.normalPages = rCreateTexture2DArray(TEXTURE_PAGE_SIZE, TEXTURE_PAGE_SIZE, TEXTURE_PAGE_LAYERS, NULL,
                                                      SDL_GPU_TEXTUREFORMAT_R8G8_UNORM, TexFlags_MipMap, pageFlags, "NormalPages");
    g_RenderState.metallicRoughnessPages = rCreateTexture2DArray(TEXTURE_PAGE_SIZE, TEXTURE_PAGE_SIZE, TEXTURE_PAGE_LAYERS, NULL,
                                                      SDL_GPU_TEXTUREFORMAT_R8G8_UNORM, TexFlags_MipMap, pageFlags, "MetallicRoughnessPages");

    AX_LOG("texture pages created %.2fs", TimeSinceStartup() - startTime);

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

    PackClass(textures, wanted[TextureClass_Albedo], TextureClass_Albedo);
    AX_LOG("albedo pages packed %.2fs", TimeSinceStartup() - startTime);
    PackClass(textures, wanted[TextureClass_Normal], TextureClass_Normal);
    AX_LOG("normal pages packed %.2fs", TimeSinceStartup() - startTime);
    PackClass(textures, wanted[TextureClass_MetallicRoughness], TextureClass_MetallicRoughness);
    AX_LOG("metallic roughness pages packed %.2fs", TimeSinceStartup() - startTime);
    DispatchPageCopies(textures);
    AX_LOG("texture page gpu copies complete requests=%d %.2fs", gNumCopyRequests, TimeSinceStartup() - startTime);
    UploadDefaultPages();

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
    AX_LOG("texture system ready descriptors=%d materials=%d %.2fs",
           g_RenderState.numTextureDescriptors, g_RenderState.numMaterials, TimeSinceStartup() - startTime);
}
