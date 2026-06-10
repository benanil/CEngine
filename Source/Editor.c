#include "Include/Slug.h"
#include "Include/UIRenderer.h"
#include "Include/UIWindow.h"
#include "Include/Platform.h"
#include "Include/Random.h"
#include "Include/Rendering.h"
#include "Include/Algorithm.h"
#include "Include/Scene.h"

extern WindowState g_WindowState;
extern RenderState g_RenderState;
extern SDL_Window* g_SDLWindow;

static bool editorOpen   = true;
static bool sceneOpen    = false;
static bool settingsOpen = false;
static bool filesOpen    = false;
static bool testOpen     = false;

static void ShowFps(void)
{
    static char fpsText[32] = "fps:0";
    static char msText[128] = "ms:0";
    static double lastUpdateTime = 0.0;
    static int fpsLen = 0, msLen = 0;
    double currentTime = TimeSinceStartup();

    if (currentTime - lastUpdateTime >= 0.25)
    {
        lastUpdateTime = currentTime;
        f32 dt = GetDeltaTime();
        int fps = (dt > 1.0e-6f) ? (int)(1.0f / dt) : 0;
        f32 ms = dt * 1000.0f;
        fpsLen = IntToString(fpsText + 4, (int64_t)fps, 0);
        fpsText[4 + fpsLen] = '\0';
        msLen = IntToString(msText + 3, (int64_t)ms, 0);
        msText[3 + msLen] = '\0';
    }

    u32 w = g_WindowState.prev_width;
    float2 fpsSize = SlugCalcTextSizeN(NULL, fpsText, fpsLen + 4, 32.0f);
    float2 msSize = SlugCalcTextSizeN(NULL, msText, msLen + 3, 32.0f);
    SlugAppendText2DN(NULL, fpsText, fpsLen + 4, (float2){w - fpsSize.x, 32.0f }, 32.0f, 0xFFCCCCFF);
    SlugAppendText2DN(NULL, msText, msLen + 3, (float2){w - msSize.x, 68.0f }, 32.0f, 0xFFCCCCFF);
}

static void WindowTestUI(void)
{
    static bool enabled = true;
    static f32 testValue = 0.35f;
    static f32 exposure = 1.0f;
    static u32 customDataIndex;

    if (!UIBeginWindow("Window Test", (float2){ 560.0f, 80.0f }, (float2){ 380.0f, 360.0f }, &testOpen, 0u)) return;

    CLAY_TEXT(CLAY_STRING("Example floating window"), CLAY_TEXT_CONFIG({
        .fontSize = 22,
        .textColor = UIGetClayColor(UIColor_Text)
    }));
    CLAY_TEXT(CLAY_STRING("Drag title bar, resize edges/corners, collapse or close."), CLAY_TEXT_CONFIG({
        .fontSize = 14,
        .textColor = UIGetClayColor(UIColor_SubText)
    }));

    Clay_ElementData windowElement = Clay_GetElementData(CLAY_ID("Window Test"));
    UIDivider(CLAY_ID("WindowTestDivider0"));
    UICheckbox(CLAY_ID("WindowTestEnabled"), CLAY_STRING("Enable test option"), &enabled);
    UISliderFloatValue(CLAY_ID("WindowTestSlider"), CLAY_STRING("Window value"), &testValue, 0.0f, 1.0f, 2);
    UISliderFloatValue(CLAY_ID("WindowTestExposure"), CLAY_STRING("Local exposure"), &exposure, 0.1f, 4.0f, 2);

    static char textBuffer[512] = "Text area in a dockable window.\nTry selecting, typing, and overlapping windows.\0";
    static UITextAreaCustomData textCustomData;
    textCustomData.type = UICustomType_TextArea;
    textCustomData.buffer = textBuffer;
    textCustomData.capacity = sizeof(textBuffer);
    CLAY(CLAY_ID("WindowTestTextArea"), {
        .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(100.0f) } },
        .custom = { .customData = &textCustomData }
    }) {}

    CLAY(CLAY_ID("WindowTestButtons"), {
        .layout = {
            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(38.0f) },
            .childGap = 10,
            .layoutDirection = CLAY_LEFT_TO_RIGHT
        }
    }) {
        if (UIButton(CLAY_ID("WindowTestReset"), CLAY_STRING("Reset"), (Clay_Dimensions){ 96.0f, 34.0f }, false))
        {
            enabled = true;
            testValue = 0.35f;
            exposure = 1.0f;
        }
        UIButton(CLAY_ID("WindowTestButton"), CLAY_STRING("Button"), (Clay_Dimensions){ 96.0f, 34.0f }, false);
    }

    UIEndWindow();
}

static void DrawGraphicsWindow()
{ 
    RenderSettings* settings = &g_RenderSettings;

    Clay_ElementDeclaration EditorPanelBoxDeclaration = 
    {
        .layout = {
            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) },
            .padding = { 14, 14, 12, 12 },
            .childGap = 10,
            .layoutDirection = CLAY_TOP_TO_BOTTOM
        },
        .backgroundColor = { 36, 36, 36, 220 },
        .cornerRadius = CLAY_CORNER_RADIUS(UIGetFloat(UIFloat_CornerRadius)),
        .border = { .color = UIGetClayColor(UIColor_Border), .width = CLAY_BORDER_ALL(UIGetFloat(UIFloat_BorderWidth)) }
    };

    Clay_ElementId windowID = (Clay_ElementId) { .id = StringToHash("Graphics Editor", 5381u) };
    if (UIBeginWindowId(windowID,"Graphics Editor", (float2){ 18.0f, 18.0f }, (float2){ 500.0f, 760.0f }, &editorOpen, 0u))
    {
        CLAY(CLAY_ID("GraphicsEditorHeader"), {
            .layout = {
                .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) },
                .childGap = 4,
                .layoutDirection = CLAY_TOP_TO_BOTTOM
            }
        }) {
            CLAY_TEXT(CLAY_STRING("Graphics Editor"), CLAY_TEXT_CONFIG({
                .fontSize = 24,
                .textColor = UIGetClayColor(UIColor_Text) 
            }));
            CLAY_TEXT(CLAY_STRING("Runtime render controls."), CLAY_TEXT_CONFIG({
                .fontSize = 14,
                .textColor = UIGetClayColor(UIColor_SubText)
            }));
        }
        //UISpacing(CLAY_ID("GraphicsEditorDivider0"), 6.0f);
        // reserve the buttons row (38) plus the content child gap (12)
        CLAY(CLAY_ID("GraphicsEditorScroll"), UIScrollPanelDeclaration(UIWindowRemainingHeight(windowID, CLAY_ID("GraphicsEditorScroll"), 50.0f), 12u)) {
            CLAY(CLAY_ID("GraphicsEditorFeatureBox"), EditorPanelBoxDeclaration) {
                UISectionHeader("Features");
                UICheckbox(CLAY_ID("EditorEnableOcclusion"), CLAY_STRING("Hi-Z occlusion culling"), &settings->enableOcclusion);
                UICheckbox(CLAY_ID("EditorEnableHBAO")     , CLAY_STRING("HBAO ambient occlusion"), &settings->enableHBAO);
                UICheckbox(CLAY_ID("EditorEnableMLAA")     , CLAY_STRING("Anti-aliasing (MLAA)"), &settings->enableMLAA);
                UICheckbox(CLAY_ID("EditorShowMLAAEdges")  , CLAY_STRING("Show MLAA edge mask"), &settings->showMLAAEdges);
                UISliderFloatValue(CLAY_ID("EditorLODDistanceModifier"), CLAY_STRING("LOD distance"), &settings->lodDistanceModifier, 0.05f, 4.0f, 2);
            }
            CLAY(CLAY_ID("GraphicsEditorLightBox"), EditorPanelBoxDeclaration) {
                RenderLightDebugInfo lightInfo = RendererGetLightDebugInfo();
                UISectionHeader("Lights");
                UICheckbox(CLAY_ID("EditorEnableLocalLights"), CLAY_STRING("Local lights"), &settings->enableLocalLights);
                UICheckbox(CLAY_ID("EditorLightFrustumCull"), CLAY_STRING("Light frustum culling"), &settings->enableLightFrustumCulling);
                UICheckbox(CLAY_ID("EditorLightOcclusionCull"), CLAY_STRING("Light occlusion culling"), &settings->enableLightOcclusionCulling);
                UICheckbox(CLAY_ID("EditorShowLightRects"), CLAY_STRING("Show light rects"), &settings->showLightRects);
                UITextU32("Total lights", lightInfo.totalLights);
                UITextU32("Submitted lights", lightInfo.submittedLights);
                UITextU32("Max lights", lightInfo.maxLights);
                UIEditInt(CLAY_ID("EditorMaxVisiblePointShadows"), CLAY_STRING("Point shadow maps"), &settings->maxVisiblePointShadows, 0, POINT_SHADOW_MAX_LIGHTS);
                UIEditInt(CLAY_ID("EditorMaxVisibleSpotShadows"), CLAY_STRING("Spot shadow maps"), &settings->maxVisibleSpotShadows, 0, SPOT_SHADOW_MAX_LIGHTS);
            }
            CLAY(CLAY_ID("GraphicsEditorSunBox"), EditorPanelBoxDeclaration) {
                UISectionHeader("Sun");
                UISliderFloatValue(CLAY_ID("EditorSunYaw")  , CLAY_STRING("Yaw")  , &settings->sunYaw  , -180.0f, 180.0f, 1);
                UISliderFloatValue(CLAY_ID("EditorSunPitch"), CLAY_STRING("Pitch"), &settings->sunPitch, -10.0f, 89.0f, 1);
            }
            CLAY(CLAY_ID("GraphicsEditorShadowBox"), EditorPanelBoxDeclaration) {
                UISectionHeader("Shadows");
                UISliderFloatValue(CLAY_ID("EditorShadowMaxDistance")   , CLAY_STRING("Max distance")   , &settings->shadowMaxDistance      , 25.0f, 1000.0f, 1);
                UISliderFloatValue(CLAY_ID("EditorShadowCameraDistance"), CLAY_STRING("Camera distance"), &settings->shadowCameraDistance   , 10.0f,  500.0f, 1);
                UISliderFloatValue(CLAY_ID("EditorShadowCasterMargin")  , CLAY_STRING("Caster margin")  , &settings->shadowCasterDepthMargin, 10.0f,  500.0f, 1);
                UISliderFloatValue(CLAY_ID("EditorShadowCascadeOverlap"), CLAY_STRING("Cascade overlap"), &settings->shadowCascadeOverlap   ,  0.0f,   80.0f, 1);
                UISliderFloatValue(CLAY_ID("EditorShadowSplitNear")     , CLAY_STRING("Split near")     , &settings->shadowSplitNearDistance,  1.0f,   80.0f, 1);
                UISliderFloatValue(CLAY_ID("EditorShadowPSSM")          , CLAY_STRING("PSSM lambda")    , &settings->shadowPSSMLambda       ,  0.0f,    1.0f, 2);
            }
            CLAY(CLAY_ID("GraphicsEditorHBAOBox"), EditorPanelBoxDeclaration) {
                UISectionHeader("HBAO");
                UISliderFloatValue(CLAY_ID("EditorHBAORadius")   , CLAY_STRING("Radius")   , &settings->hbaoRadius   , 0.05f, 5.0f, 2);
                UISliderFloatValue(CLAY_ID("EditorHBAOBias")     , CLAY_STRING("Bias")     , &settings->hbaoBias     ,  0.0f, 1.0f, 2);
                UISliderFloatValue(CLAY_ID("EditorHBAOIntensity"), CLAY_STRING("Intensity"), &settings->hbaoIntensity,  0.0f, 6.0f, 2);
                UISliderFloatValue(CLAY_ID("EditorHBAOPower")    , CLAY_STRING("Power")    , &settings->hbaoPower    , 0.25f, 6.0f, 2);
            }
            CLAY(CLAY_ID("GraphicsEditorPostBox"), EditorPanelBoxDeclaration) {
                UISectionHeader("Post / AA");
                UISliderFloatValue(CLAY_ID("EditorMLAAThreshold"), CLAY_STRING("MLAA threshold"), &settings->mlaaThreshold  , 0.01f, 0.25f, 3);
                UISliderFloatValue(CLAY_ID("EditorExposure")     , CLAY_STRING("Exposure")      , &settings->exposure       , 0.10f, 4.00f, 2);
                UISliderFloatValue(CLAY_ID("EditorGamma")        , CLAY_STRING("Gamma")         , &settings->gamma          , 1.00f, 3.20f, 2);
                UISliderFloatValue(CLAY_ID("EditorGodRays")      , CLAY_STRING("God rays")      , &settings->godRayIntensity, 0.00f, 8.00f, 2);
            }
        }
        CLAY(CLAY_ID("GraphicsEditorButtons"), {
            .layout = {
                .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(38.0f) },
                .childGap = 10,
                .layoutDirection = CLAY_LEFT_TO_RIGHT
            }
        }) {
            if (UIButton(CLAY_ID("EditorResetGraphics"), CLAY_STRING("Reset"), (Clay_Dimensions){ 100.0f, 34.0f }, false))
            {
                *settings = (RenderSettings){
                    .enableOcclusion = true,
                    .enableHBAO = true,
                    .enableMLAA = true,
                    .showMLAAEdges = false,
                    .enableLocalLights = true,
                    .enableLightFrustumCulling = true,
                    .enableLightOcclusionCulling = true,
                    .showLightRects = false,
                    .hbaoRadius = 1.3f,
                    .hbaoBias = 0.5f,
                    .hbaoIntensity = 2.0f,
                    .hbaoPower = 2.0f,
                    .mlaaThreshold = 0.08f,
                    .exposure = 1.0f,
                    .gamma = 2.2f,
                    .godRayIntensity = 2.5f,
                    .sunYaw = 116.565f,
                    .sunPitch = 63.435f,
                    .shadowMaxDistance = SHADOW_MAX_DISTANCE,
                    .shadowCameraDistance = SHADOW_CAMERA_DISTANCE,
                    .shadowCasterDepthMargin = SHADOW_CASTER_DEPTH_MARGIN,
                    .shadowCascadeOverlap = SHADOW_CASCADE_OVERLAP,
                    .shadowSplitNearDistance = SHADOW_SPLIT_NEAR_DISTANCE,
                    .shadowPSSMLambda = SHADOW_PSSM_LAMBDA,
                    .maxVisiblePointShadows = (f32)POINT_SHADOW_MAX_LIGHTS,
                    .maxVisibleSpotShadows = (f32)SPOT_SHADOW_MAX_LIGHTS
                };
            }
        }
        UIEndWindow();
    }
}

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

static void DrawSceneWindow()
{
    Clay_ElementId windowID = (Clay_ElementId) { .id = StringToHash("SceneWindow", 5381u) };
    if (!UIBeginWindowId(windowID, "Scene", (float2) { 18.0f, 18.0f }, (float2) { 500.0f, 760.0f }, &sceneOpen, 0u)) return;

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

static void DrawSettingsWindow()
{
    Clay_ElementId windowID = (Clay_ElementId) { .id = StringToHash("SettingsWindow", 5381u) };
    if (UIBeginWindowId(windowID, "Settings", (float2) { 18.0f, 18.0f }, (float2) { 500.0f, 760.0f }, &settingsOpen, 0u))
    {
        UIEndWindow();
    }
}

static void GraphicsEditorUI(void)
{
    Clay_BeginLayout();

    int screenWidth, screenHeight;
    SDL_GetWindowSize(g_SDLWindow, &screenWidth, &screenHeight);
    u16 borderWidth = (u16)UIGetFloat(UIFloat_BorderWidth);

    CLAY(CLAY_ID("TabBar"), {
        .layout = {
            .sizing = { CLAY_SIZING_FIXED(screenWidth), CLAY_SIZING_FIXED(40.0f) },
            .childAlignment = { .x = CLAY_ALIGN_X_LEFT, .y = CLAY_ALIGN_Y_CENTER },
            .childGap = 15,
            .padding = { 20, 0 },
            .layoutDirection = CLAY_LEFT_TO_RIGHT
        },
        .backgroundColor = UIPanelColor(),
        .border = { .color = UIGetClayColor(UIColor_Border),  .width = { borderWidth, borderWidth, borderWidth, borderWidth, 0} }
    }) {
        UIPushFloatAdd(UIFloat_TextScale, -0.15f);
        UIPushFloat(UIFloat_CornerRadius, 2.5f);
        editorOpen   ^= UIButton(CLAY_ID("Graphics"), CLAY_STRING("Graphics"), (Clay_Dimensions){UIGetFloat(UIFloat_ButtonSize), 25.0f}, false);
        sceneOpen    ^= UIButton(CLAY_ID("Scene")   , CLAY_STRING("Scene")   , (Clay_Dimensions){UIGetFloat(UIFloat_ButtonSize), 25.0f}, false);
        settingsOpen ^= UIButton(CLAY_ID("Settings"), CLAY_STRING("Settings"), (Clay_Dimensions){UIGetFloat(UIFloat_ButtonSize), 25.0f}, false);
        filesOpen    ^= UIButton(CLAY_ID("Files")   , CLAY_STRING("Files")   , (Clay_Dimensions){UIGetFloat(UIFloat_ButtonSize), 25.0f}, false);
        testOpen     ^= UIButton(CLAY_ID("Test")    , CLAY_STRING("Test")    , (Clay_Dimensions){UIGetFloat(UIFloat_ButtonSize), 25.0f}, false);
        UIPopFloat(UIFloat_CornerRadius);
        UIPopFloat(UIFloat_TextScale);
    }

    CLAY(CLAY_ID("GraphicsEditorRoot"), {
        .layout = {
            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
        }
    }) {

        ShowFps();
        WindowTestUI();
        DrawSettingsWindow();
        DrawSceneWindow();
        DrawGraphicsWindow();
    }
    Clay_RenderCommandArray commands = UIEndLayout();
    UIRenderCommands(&commands);
}

void UIRenderCallback(void)
{
    // static char textArea[512] = "Text area 中文测试 日本語テスト\nArabic: العربية\nGreek: Ελληνικά";
    // UIText("SDF + Slug Immediate UI", (float2){ 56.0f, 56.0f });
    // UITextArea("Text Area", (float2){ 56.0f, 292.0f }, textArea, (u32)sizeof(textArea), (float2){ 520.0f, 160.0f });
    if (GetKeyPressed('c'))
    {
        Clay_SetDebugModeEnabled(!Clay_IsDebugModeEnabled());
    }
    GraphicsEditorUI();
}
