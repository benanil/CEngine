//------------------------------------------------------------------------
// Textures Window
#include "Include/Scene.h"
#include "Include/Random.h"
#include "Include/TextureSystem.h"
#include "Include/UIWindow.h"
#include "Include/Algorithm.h"
#include "Include/Graphics.h"

// texture page inspector, blits the selected 4k page layer into a small preview each frame
#define TEX_PREVIEW_SIZE 1024

static bool texInfoOpen = true;
static u32 texInspectClass = TextureClass_Albedo;
static u32 texInspectLayer;
static SDL_GPUTexture* texPagePreview;
static UIImageData texPagePreviewImage;

extern SDL_GPUDevice* g_GPUDevice;

static u32 TexturePageMemoryMB(const TextureSystem* ts)
{
    u64 bytes = 0u;
    for (u32 i = 0u; i < TextureClass_Count; i++)
    {
        u64 texelBytes = ts->compressed ? 1u : (i == TextureClass_Albedo ? 4u : 2u);
        bytes += (u64)TEXTURE_PAGE_SIZE * TEXTURE_PAGE_SIZE * ts->classes[i].layerCount * texelBytes;
    }
    return (u32)((bytes * 4u / 3u) >> 20u); // mip chain adds ~1/3
}

static void TexturePageBlitPreview(const TexturePageClass* cls)
{
    if (!texPagePreview)
    {
        texPagePreview = CreateTexture2D(TEX_PREVIEW_SIZE, TEX_PREVIEW_SIZE, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                                         SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
                                         SDL_GPU_SAMPLECOUNT_1, 1u, "TexturePagePreview");
        texPagePreviewImage = (UIImageData){ .texture = texPagePreview };
    }
    if (!texPagePreview) return;

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(g_GPUDevice);
    if (!cmd) return;

    SDL_GPUBlitInfo blit;
    SDL_zero(blit);
    blit.source.texture = cls->pages.handle;
    blit.source.layer_or_depth_plane = texInspectLayer;
    blit.source.w = (u32)cls->pages.width;
    blit.source.h = (u32)cls->pages.height;
    blit.destination.texture = texPagePreview;
    blit.destination.w = TEX_PREVIEW_SIZE;
    blit.destination.h = TEX_PREVIEW_SIZE;
    blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
    blit.filter  = SDL_GPU_FILTER_LINEAR;
    SDL_BlitGPUTexture(cmd, &blit);
    SDL_SubmitGPUCommandBuffer(cmd);
}

void DrawTexturesWindow(bool* open)
{
    Clay_ElementId windowID = (Clay_ElementId) { .id = StringToHash("TexturesWindow", 5381u) };
    if (!UIBeginWindowId(windowID, "Textures", (float2) { 540.0f, 18.0f }, (float2) { 520.0f, 760.0f }, open, 0u)) return;

    Scene* scene = GetActiveScene();
    if (!scene)
    {
        CLAY_TEXT(CLAY_STRING("No active scene."), CLAY_TEXT_CONFIG({
            .fontSize = 14,
            .textColor = UIGetClayColor(UIColor_SubText)
        }));
        UIEndWindow();
        return;
    }

    TextureSystem* ts = &scene->textureSystem;

    texInfoOpen ^= UICollapsingHeader(CLAY_ID("TextureInfoHeader"), CLAY_STRING("Texture System"), texInfoOpen);
    if (texInfoOpen)
    {
        CLAY(CLAY_ID("TexturesWindowInfo"), {
            .layout = {
                .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) },
                .padding = { 6, 0, 0, 0 },
                .childGap = 4,
                .layoutDirection = CLAY_TOP_TO_BOTTOM
            }
        }) {
            UITextU32("Descriptors", ts->numDescriptors);
            UITextU32("Material watermark", ts->materialWatermark);
            UITextU32("Compressed pages", ts->compressed);
            UITextU32("Page size", TEXTURE_PAGE_SIZE);
            UITextU32("Albedo layers", ts->classes[TextureClass_Albedo].layerCount);
            UITextU32("Normal layers", ts->classes[TextureClass_Normal].layerCount);
            UITextU32("Metallic-roughness layers", ts->classes[TextureClass_MetallicRoughness].layerCount);
            UITextU32("Page memory (MB)", TexturePageMemoryMB(ts));
        }
    }
    UIDivider(CLAY_ID("TexturesWindowDivider"));

    CLAY(CLAY_ID("TextureClassButtons"), {
        .layout = {
            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(30.0f) },
            .childGap = 8,
            .layoutDirection = CLAY_LEFT_TO_RIGHT
        }
    }) {
        UIPushFloatAdd(UIFloat_TextScale, -0.15f);
        if (UIButton("Albedo"    , (Clay_Dimensions){ 110.0f, 28.0f }, texInspectClass == TextureClass_Albedo))            texInspectClass = TextureClass_Albedo;
        if (UIButton("Normal"    , (Clay_Dimensions){ 110.0f, 28.0f }, texInspectClass == TextureClass_Normal))            texInspectClass = TextureClass_Normal;
        if (UIButton("MetalRough", (Clay_Dimensions){ 110.0f, 28.0f }, texInspectClass == TextureClass_MetallicRoughness)) texInspectClass = TextureClass_MetallicRoughness;
        UIPopFloat(UIFloat_TextScale);
    }

    const TexturePageClass* cls = &ts->classes[texInspectClass];
    if (!cls->pages.handle || cls->layerCount == 0u)
    {
        CLAY_TEXT(CLAY_STRING("No pages for this class."), CLAY_TEXT_CONFIG({
            .fontSize = 14,
            .textColor = UIGetClayColor(UIColor_SubText)
        }));
        UIEndWindow();
        return;
    }
    if (texInspectLayer >= cls->layerCount) texInspectLayer = 0u;

    CLAY(CLAY_ID("TextureLayerButtons"), {
        .layout = {
            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(28.0f) },
            .childGap = 6,
            .layoutDirection = CLAY_LEFT_TO_RIGHT
        }
    }) {
        for (u32 i = 0u; i < cls->layerCount && i < TEXTURE_PAGE_LAYERS; i++)
        {
            Clay_ElementId id = Clay_GetElementIdWithIndex(CLAY_STRING("TexLayerButton"), i);
            char* buffer = UIFrameStringAlloc(16);
            IntToString(buffer, (s64)i, 0);
            if (UIButtonID(UIStr(buffer).chars, id.id, (Clay_Dimensions){ 30.0f, 26.0f }, i == texInspectLayer)) texInspectLayer = i;
        }
    }

    TexturePageBlitPreview(cls);

    UIWindow* window = UIGetWindow(windowID);
    f32 side = window ? window->scale.x - 48.0f : 256.0f;
    side = Maxf32(Minf32(side, UIWindowRemainingHeight(windowID, CLAY_ID("TexturePagePreview"), 0.0f)), 64.0f);
    CLAY(CLAY_ID("TexturePagePreview"), {
        .layout = { .sizing = { CLAY_SIZING_FIXED(side), CLAY_SIZING_FIXED(side) } },
        .image = { .imageData = &texPagePreviewImage },
        .border = { .color = UIGetClayColor(UIColor_Border), .width = CLAY_BORDER_ALL(1) }
    }) {}

    UIEndWindow();
}
