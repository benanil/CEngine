// scene hierarchy and texture system inspector windows
#include "EditorInternal.h"
#include "Include/Platform.h"
#include "Include/Random.h"
#include "Include/Algorithm.h"
#include "Include/Scene.h"

extern SDL_GPUDevice* g_GPUDevice;

//------------------------------------------------------------------------
// Scene Window

// hierarchy selection, bundle row when node is -1, nothing when bundle is INVALID_BUNDLE
static u32 sceneSelectedBundle = INVALID_BUNDLE;
static s32 sceneSelectedNode   = -1;
static u32 sceneTreeRowBudget;
static bool sceneInfoOpen = true;

// caller owned tree expansion state, node bits start closed,
// bundle flags are stored inverted so bundle rows start open
#define SCENE_TREE_MAX_NODES 16000
static u32 sceneNodeOpenBits[MAX_SCENE_BUNDLES][SCENE_TREE_MAX_NODES / 32];
static u8  sceneBundleClosed[MAX_SCENE_BUNDLES];

static bool SceneNodeIsOpen(u32 bundleIdx, u32 nodeIdx)
{
    if (nodeIdx >= SCENE_TREE_MAX_NODES) return false;
    return (sceneNodeOpenBits[bundleIdx][nodeIdx >> 5u] >> (nodeIdx & 31u)) & 1u;
}

static void SceneNodeFlipOpen(u32 bundleIdx, u32 nodeIdx)
{
    if (nodeIdx >= SCENE_TREE_MAX_NODES) return;
    sceneNodeOpenBits[bundleIdx][nodeIdx >> 5u] ^= 1u << (nodeIdx & 31u);
}

static Clay_String SceneNodeLabel(const ANode* node, s32 nodeIdx)
{
    if (node->name && node->name[0]) return UIStr(node->name);

    char* text = UIFrameStringAlloc(20u);
    if (!text) return CLAY_STRING("Node");
    u32 len = 5u;
    MemCopy(text, "Node ", len);
    len += (u32)IntToString(text + len, (int64_t)nodeIdx, 0);
    text[len] = '\0';
    return (Clay_String) { .isStaticallyAllocated = false, .length = (s32)len, .chars = text };
}

static void SceneNodeTree(const SceneBundle* bundle, u32 bundleIdx, s32 nodeIdx, u32 depth)
{
    if (sceneTreeRowBudget == 0u) return;
    sceneTreeRowBudget--;

    const ANode* node = &bundle->nodes[nodeIdx];
    Clay_ElementId id = Clay_GetElementIdWithIndex(CLAY_STRING("SceneTreeNode"), bundleIdx * 0x10000u + (u32)nodeIdx);

    u32 flags = 0u;
    if (node->numChildren <= 0) flags |= UITreeNodeFlags_Leaf;
    if (bundleIdx == sceneSelectedBundle && nodeIdx == sceneSelectedNode) flags |= UITreeNodeFlags_Selected;

    bool open = SceneNodeIsOpen(bundleIdx, (u32)nodeIdx);
    bool selected = false;
    if (UITreeNode(id, SceneNodeLabel(node, nodeIdx), depth, flags, open, &selected))
    {
        SceneNodeFlipOpen(bundleIdx, (u32)nodeIdx);
        open = !open;
    }
    if (selected)
    {
        sceneSelectedBundle = bundleIdx;
        sceneSelectedNode = nodeIdx;
    }
    if (!open) return;

    for (s32 c = 0; c < node->numChildren; c++)
    {
        s32 child = node->children[c];
        if (child >= 0 && child < bundle->numNodes)
            SceneNodeTree(bundle, bundleIdx, child, depth + 1u);
    }
}

static void SceneBundleTree(const Scene* scene, u32 bundleIdx)
{
    const SceneBundle* bundle = scene->bundles[bundleIdx];
    Clay_ElementId id = Clay_GetElementIdWithIndex(CLAY_STRING("SceneTreeBundle"), bundleIdx);

    u32 flags = 0u;
    if (!bundle || bundle->numNodes <= 0) flags |= UITreeNodeFlags_Leaf;
    if (bundleIdx == sceneSelectedBundle && sceneSelectedNode < 0) flags |= UITreeNodeFlags_Selected;

    bool open = !sceneBundleClosed[bundleIdx];
    bool selected = false;
    if (UITreeNode(id, UIStr(GetFileName(scene->bundlePaths[bundleIdx])), 0u, flags, open, &selected))
    {
        sceneBundleClosed[bundleIdx] ^= 1u;
        open = !open;
    }
    if (selected)
    {
        sceneSelectedBundle = bundleIdx;
        sceneSelectedNode = -1;
    }
    if (!open || !bundle) return;

    // normalized bundles have parent indices built, roots are the unparented nodes
    for (s32 i = 0; i < bundle->numNodes; i++)
        if (bundle->nodes[i].parent < 0)
            SceneNodeTree(bundle, bundleIdx, i, 1u);
}

// instanced triangle count at lod 0
static u32 SceneCountTriangles(const RenderSet* set)
{
    u64 triangles = 0u;
    for (u32 i = 0u; i < set->numGroups; i++)
    {
        const PrimitiveGroup* group = &set->primitiveGroups[i];
        if (!group->valid) continue;
        triangles += (u64)(group->numIndices / 3u) * group->numEntities;
    }
    return (u32)Minu64(triangles, 0xFFFFFFFFull);
}

void DrawSceneWindow(bool* open)
{
    Clay_ElementId windowID = (Clay_ElementId) { .id = StringToHash("SceneWindow", 5381u) };
    if (!UIBeginWindowId(windowID, "Scene", (float2) { 18.0f, 18.0f }, (float2) { 500.0f, 760.0f }, open, 0u)) return;

    Scene* scene = Scene_GetActive();
    if (!scene)
    {
        CLAY_TEXT(CLAY_STRING("No active scene."), CLAY_TEXT_CONFIG({
            .fontSize = 14,
            .textColor = UIGetClayColor(UIColor_SubText)
        }));
        UIEndWindow();
        return;
    }

    sceneInfoOpen ^= UICollapsingHeader(CLAY_ID("SceneInfoHeader"), CLAY_STRING("Active Scene"), sceneInfoOpen);
    if (sceneInfoOpen)
    {
        CLAY(CLAY_ID("SceneWindowInfo"), {
            .layout = {
                .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) },
                .padding = { 6, 0, 0, 0 },
                .childGap = 4,
                .layoutDirection = CLAY_TOP_TO_BOTTOM
            }
        }) {
            UITextU32("Active scenes", g_NumActiveScenes);
            UITextU32("Bundles", scene->numBundles);
            UITextU32("Materials", scene->numMaterials);
            UITextU32("Static entities", scene->surfaceSet.numEntities);
            UITextU32("Skinned entities", scene->skinnedSet.numEntities);
            UITextU32("Primitive groups", scene->surfaceSet.numGroups + scene->skinnedSet.numGroups);
            UITextU32("Triangles", SceneCountTriangles(&scene->surfaceSet) + SceneCountTriangles(&scene->skinnedSet));
        }
    }
    UIDivider(CLAY_ID("SceneWindowDivider"));

    sceneTreeRowBudget = SCENE_TREE_MAX_NODES;
    CLAY(CLAY_ID("SceneWindowTree"), UIScrollPanelDeclaration(UIWindowRemainingHeight(windowID, CLAY_ID("SceneWindowTree"), 0.0f), 2u)) {
        for (u32 b = 0u; b < scene->numBundles; b++)
            SceneBundleTree(scene, b);
        if (sceneTreeRowBudget == 0u)
        {
            CLAY_TEXT(CLAY_STRING("... node limit reached, collapse some rows"), CLAY_TEXT_CONFIG({
                .fontSize = 13,
                .textColor = UIGetClayColor(UIColor_SubText)
            }));
        }
    }
    UIEndWindow();
}

//------------------------------------------------------------------------
// Textures Window

// texture page inspector, blits the selected 4k page layer into a small preview each frame
#define TEX_PREVIEW_SIZE 1024

static bool texInfoOpen = true;
static u32 texInspectClass = TextureClass_Albedo;
static u32 texInspectLayer;
static SDL_GPUTexture* texPagePreview;
static UIImageData texPagePreviewImage;

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
    blit.filter = SDL_GPU_FILTER_LINEAR;
    SDL_BlitGPUTexture(cmd, &blit);
    SDL_SubmitGPUCommandBuffer(cmd);
}

void DrawTexturesWindow(bool* open)
{
    Clay_ElementId windowID = (Clay_ElementId) { .id = StringToHash("TexturesWindow", 5381u) };
    if (!UIBeginWindowId(windowID, "Textures", (float2) { 540.0f, 18.0f }, (float2) { 520.0f, 760.0f }, open, 0u)) return;

    Scene* scene = Scene_GetActive();
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
        if (UIButton(CLAY_ID("TexClassAlbedo"), CLAY_STRING("Albedo")    , (Clay_Dimensions){ 110.0f, 28.0f }, texInspectClass == TextureClass_Albedo))            texInspectClass = TextureClass_Albedo;
        if (UIButton(CLAY_ID("TexClassNormal"), CLAY_STRING("Normal")    , (Clay_Dimensions){ 110.0f, 28.0f }, texInspectClass == TextureClass_Normal))            texInspectClass = TextureClass_Normal;
        if (UIButton(CLAY_ID("TexClassMR")    , CLAY_STRING("MetalRough"), (Clay_Dimensions){ 110.0f, 28.0f }, texInspectClass == TextureClass_MetallicRoughness)) texInspectClass = TextureClass_MetallicRoughness;
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
            char buffer[16] = { 0 };
            IntToString(buffer, (s64)i, 0);
            if (UIButton(id, UIStr(buffer), (Clay_Dimensions){ 30.0f, 26.0f }, i == texInspectLayer)) texInspectLayer = i;
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
