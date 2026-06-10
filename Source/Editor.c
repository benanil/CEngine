#include "Include/Slug.h"
#include "Include/UIRenderer.h"
#include "Include/UIWindow.h"
#include "Include/Platform.h"
#include "Include/Random.h"
#include "Include/Rendering.h"
#include "Include/Algorithm.h"
#include "EditorInternal.h"

extern WindowState g_WindowState;
extern RenderState g_RenderState;
extern SDL_Window* g_SDLWindow;

static bool editorOpen   = true;
static bool sceneOpen    = false;
static bool texturesOpen = false;
static bool settingsOpen = false;
static bool assetsOpen   = false;
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

    static const char* dropdownOptions[] = { "Alpha", "Beta", "Gamma", "Delta" };
    static u32 dropdownIndex = 1u;
    UIDropdown(CLAY_ID("WindowTestDropdown"), CLAY_STRING("Dropdown test"), dropdownOptions, 4u, &dropdownIndex);

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

static u32 editorQualityIndex = 2u; // High matches the startup defaults

// performance presets, artistic settings (sun, exposure, gamma) stay untouched.
// the lod modifier scales the projected screen size, higher keeps detailed lods longer
static void EditorApplyQualityPreset(RenderSettings* settings, u32 quality)
{
    settings->enableOcclusion           = true;
    settings->enableLightFrustumCulling = true;
    settings->enableLightOcclusionCulling = true;
    switch (quality)
    {
    case 0: // Low
        settings->enableHBAO            = false;
        settings->enableMLAA            = false;
        settings->enableLocalLights     = false;
        settings->lodDistanceModifier   = 0.35f;
        settings->godRaySamples         = 16.0f;
        settings->hbaoDirections        = 4.0f;
        settings->shadowMaxDistance     = SHADOW_MAX_DISTANCE * 0.35f;
        settings->maxVisiblePointShadows = 0.0f;
        settings->maxVisibleSpotShadows  = 0.0f;
        break;
    case 1: // Medium
        settings->enableHBAO            = true;
        settings->enableMLAA            = true;
        settings->enableLocalLights     = true;
        settings->lodDistanceModifier   = 0.55f;
        settings->godRaySamples         = 32.0f;
        settings->hbaoDirections        = 4.0f;
        settings->shadowMaxDistance     = SHADOW_MAX_DISTANCE * 0.6f;
        settings->maxVisiblePointShadows = 4.0f;
        settings->maxVisibleSpotShadows  = 4.0f;
        break;
    case 2: // High, matches the startup defaults
        settings->enableHBAO            = true;
        settings->enableMLAA            = true;
        settings->enableLocalLights     = true;
        settings->lodDistanceModifier   = 0.75f;
        settings->godRaySamples         = 48.0f;
        settings->hbaoDirections        = 8.0f;
        settings->shadowMaxDistance     = SHADOW_MAX_DISTANCE;
        settings->maxVisiblePointShadows = 8.0f;
        settings->maxVisibleSpotShadows  = 8.0f;
        break;
    case 3: // Ultra
        settings->enableHBAO            = true;
        settings->enableMLAA            = true;
        settings->enableLocalLights     = true;
        settings->lodDistanceModifier   = 1.5f;
        settings->godRaySamples         = 64.0f;
        settings->hbaoDirections        = 12.0f;
        settings->shadowMaxDistance     = SHADOW_MAX_DISTANCE;
        settings->maxVisiblePointShadows = (f32)POINT_SHADOW_MAX_LIGHTS;
        settings->maxVisibleSpotShadows  = (f32)SPOT_SHADOW_MAX_LIGHTS;
        break;
    }
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
                static const char* qualityOptions[] = { "Low", "Medium", "High", "Ultra" };
                if (UIDropdown(CLAY_ID("EditorQualityPreset"), CLAY_STRING("Quality"), qualityOptions, 4u, &editorQualityIndex))
                    EditorApplyQualityPreset(settings, editorQualityIndex);
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
                UIEditInt(CLAY_ID("EditorHBAODirections"), CLAY_STRING("Directions"), &settings->hbaoDirections, 2, 16);
            }
            CLAY(CLAY_ID("GraphicsEditorPostBox"), EditorPanelBoxDeclaration) {
                UISectionHeader("Post / AA");
                UISliderFloatValue(CLAY_ID("EditorMLAAThreshold"), CLAY_STRING("MLAA threshold"), &settings->mlaaThreshold  , 0.01f, 0.25f, 3);
                UISliderFloatValue(CLAY_ID("EditorExposure")     , CLAY_STRING("Exposure")      , &settings->exposure       , 0.10f, 4.00f, 2);
                UISliderFloatValue(CLAY_ID("EditorGamma")        , CLAY_STRING("Gamma")         , &settings->gamma          , 1.00f, 3.20f, 2);
                UISliderFloatValue(CLAY_ID("EditorGodRays")      , CLAY_STRING("God rays")      , &settings->godRayIntensity, 0.00f, 8.00f, 2);
                UIEditInt(CLAY_ID("EditorGodRaySamples"), CLAY_STRING("God ray samples"), &settings->godRaySamples, 0, 128);
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
                editorQualityIndex = 2u; // defaults are the High preset
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
                    .godRaySamples = 64.0f,
                    .hbaoDirections = 8.0f,
                    .lodDistanceModifier = 0.75f,
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
        texturesOpen ^= UIButton(CLAY_ID("Textures"), CLAY_STRING("Textures"), (Clay_Dimensions){UIGetFloat(UIFloat_ButtonSize), 25.0f}, false);
        settingsOpen ^= UIButton(CLAY_ID("Settings"), CLAY_STRING("Settings"), (Clay_Dimensions){UIGetFloat(UIFloat_ButtonSize), 25.0f}, false);
        assetsOpen   ^= UIButton(CLAY_ID("Assets")  , CLAY_STRING("Assets")  , (Clay_Dimensions){UIGetFloat(UIFloat_ButtonSize), 25.0f}, false);
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
        DrawSceneWindow(&sceneOpen);
        DrawTexturesWindow(&texturesOpen);
        DrawAssetsWindow(&assetsOpen);
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
