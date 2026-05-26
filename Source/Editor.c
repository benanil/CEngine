#include "Include/Slug.h"
#include "Include/UIRenderer.h"
#include "Include/Platform.h"
#include "Include/Random.h"
#include "Include/Rendering.h"

extern WindowState g_WindowState;

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

static void EditorSliderFloat(Clay_ElementId id, const char* label, f32* value, f32 minValue, f32 maxValue, int decimals)
{
    Clay_String labelString = { .isStaticallyAllocated = true, .length = (s32)StringLength(label), .chars = label };
    UISliderFloatValue(id, labelString, value, minValue, maxValue, decimals);
}

static void EditorDrawScrollBar(Clay_ElementId id)
{
    static struct { u64 id; f32 dragOffsetY; } drag;

    Clay_ElementData element = Clay_GetElementData(id);
    Clay_ScrollContainerData scroll = Clay_GetScrollContainerData(id);
    u64 scrollId = (u64)id.id;

    if (!GetMouseDown(MouseButton_Left) && drag.id == scrollId) drag.id = 0u;
    if (!element.found || !scroll.found || !scroll.scrollPosition) return;

    f32 containerH = Maxf32(scroll.scrollContainerDimensions.height, 1.0f);
    f32 contentH   = Maxf32(scroll.contentDimensions.height, containerH);
    f32 maxScroll  = contentH - containerH;
    if (maxScroll <= 1.0f) return;

    f32 trackW = 6.0f;
    f32 trackX = element.boundingBox.x + element.boundingBox.width - trackW;
    f32 trackY = element.boundingBox.y;
    f32 thumbH = Minf32(Maxf32(containerH * (containerH / contentH), 28.0f), containerH);
    f32 t      = Saturatef32(-scroll.scrollPosition->y / maxScroll);
    f32 thumbY = trackY + t * (containerH - thumbH);

    Clay_PointerData pointer = Clay_GetPointerState();
    float2 mouse     = { pointer.position.x, pointer.position.y };
    float2 thumbPos  = { trackX - 4.0f, thumbY };
    float2 thumbSize = { trackW + 8.0f, thumbH };
    float2 trackPos  = { trackX - 4.0f, trackY };
    float2 trackSize = { trackW + 8.0f, containerH };

    bool thumbHovered = RectPointIntersect(thumbPos, thumbSize, mouse) != 0u;
    bool trackHovered = RectPointIntersect(trackPos, trackSize, mouse) != 0u;

    if (GetMouseDown(MouseButton_Left) && (thumbHovered || trackHovered))
    {
        drag.id          = scrollId;
        drag.dragOffsetY = thumbHovered ? mouse.y - thumbY : thumbH * 0.5f;
    }
    if (drag.id == scrollId && GetMouseDown(MouseButton_Left))
    {
        f32 scrollRange          = Maxf32(containerH - thumbH, 1.0f);
        f32 newT                 = Saturatef32((mouse.y - trackY - drag.dragOffsetY) / scrollRange);
        scroll.scrollPosition->y = -newT * maxScroll;
        t                        = newT;
        thumbY                   = trackY + t * (containerH - thumbH);
    }

    UIPushRoundedRect((float2){ trackX, trackY }, (float2){ trackW, containerH }, 3.0f, 0x33404040u);
    u32 thumbColor = (drag.id == scrollId || thumbHovered) ? 0xFFE8A400u : 0xAA808080u;
    UIPushRoundedRect((float2){ trackX, thumbY }, (float2){ trackW, thumbH }, 3.0f, thumbColor);
}

static void EditorSectionHeader(const char* title)
{
    Clay_String titleString = { .isStaticallyAllocated = true, .length = (s32)StringLength(title), .chars = title };
    CLAY_TEXT(titleString, CLAY_TEXT_CONFIG({
        .fontSize = 16,
        .textColor = { 232, 164, 0, 255 }
    }));
}

static void EditorDivider(Clay_ElementId id)
{
    CLAY(id, {
        .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1.0f) } },
        .backgroundColor = { 55, 55, 55, 160 }
    }) {}
}

static void EditorSpacing(Clay_ElementId id, float pixels)
{
    CLAY(id, {
        .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(pixels) } },
        .backgroundColor = { 55, 55, 55, 160 }
    }) {}
}

static Clay_ElementDeclaration EditorScrollPanelDeclaration(f32 height)
{
    Clay_ElementDeclaration declaration = {
        .layout = {
            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(height) },
            .padding = { 0, 20, 0, 0 },
            .childGap = 12,
            .layoutDirection = CLAY_TOP_TO_BOTTOM
        },
        .clip = { .vertical = true, .childOffset = Clay_GetScrollOffset() }
    };
    return declaration;
}

static void GraphicsEditorUI(void)
{
    RenderSettings* settings = &g_RenderSettings;
    Clay_BeginLayout();
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
    CLAY(CLAY_ID("GraphicsEditorRoot"), {
        .layout = {
            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
            .padding = { 18, 18, 18, 18 },
            .childAlignment = { .x = CLAY_ALIGN_X_LEFT, .y = CLAY_ALIGN_Y_TOP },
        }
    }) {
        ShowFps();
        CLAY(CLAY_ID("GraphicsEditorPanel"), {
            .layout = {
                .sizing = { CLAY_SIZING_FIXED(500.0f), CLAY_SIZING_FIT(0) },
                .padding = { 18, 18, 18, 18 },
                .childGap = 12,
                .layoutDirection = CLAY_TOP_TO_BOTTOM
            },
            .backgroundColor = { 26, 26, 26, 245 },
            .cornerRadius = CLAY_CORNER_RADIUS(UIGetFloat(UIFloat_CornerRadius)),
            .border = { .color = UIGetClayColor(UIColor_Border), .width = CLAY_BORDER_ALL(UIGetFloat(UIFloat_BorderWidth)) }
        }) {
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
            //EditorSpacing(CLAY_ID("GraphicsEditorDivider0"), 6.0f);
            CLAY(CLAY_ID("GraphicsEditorScroll"), EditorScrollPanelDeclaration(590.0f)) {
                CLAY(CLAY_ID("GraphicsEditorFeatureBox"), EditorPanelBoxDeclaration) {
                    EditorSectionHeader("Features");
                    UICheckbox(CLAY_ID("EditorEnableOcclusion"), CLAY_STRING("Hi-Z occlusion culling"), &settings->enableOcclusion);
                    UICheckbox(CLAY_ID("EditorEnableHBAO")     , CLAY_STRING("HBAO ambient occlusion"), &settings->enableHBAO);
                    UICheckbox(CLAY_ID("EditorEnableMLAA")     , CLAY_STRING("Anti-aliasing (MLAA)"), &settings->enableMLAA);
                    UICheckbox(CLAY_ID("EditorShowMLAAEdges")  , CLAY_STRING("Show MLAA edge mask"), &settings->showMLAAEdges);
                }
                CLAY(CLAY_ID("GraphicsEditorSunBox"), EditorPanelBoxDeclaration) {
                    EditorSectionHeader("Sun");
                    EditorSliderFloat(CLAY_ID("EditorSunYaw")  , "Yaw"  , &settings->sunYaw  , -180.0f, 180.0f, 1);
                    EditorSliderFloat(CLAY_ID("EditorSunPitch"), "Pitch", &settings->sunPitch, -10.0f, 89.0f, 1);
                }
                CLAY(CLAY_ID("GraphicsEditorShadowBox"), EditorPanelBoxDeclaration) {
                    EditorSectionHeader("Shadows");
                    UICheckbox(CLAY_ID("EditorEnableSDSM"), CLAY_STRING("Sample distribution shadow maps"), &settings->enableSDSM);
                    EditorSliderFloat(CLAY_ID("EditorShadowMaxDistance")   , "Max distance"   , &settings->shadowMaxDistance      , 25.0f, 1000.0f, 1);
                    EditorSliderFloat(CLAY_ID("EditorShadowCameraDistance"), "Camera distance", &settings->shadowCameraDistance   , 10.0f,  500.0f, 1);
                    EditorSliderFloat(CLAY_ID("EditorShadowCasterMargin")  , "Caster margin"  , &settings->shadowCasterDepthMargin, 10.0f,  500.0f, 1);
                    EditorSliderFloat(CLAY_ID("EditorShadowCascadeOverlap"), "Cascade overlap", &settings->shadowCascadeOverlap   ,  0.0f,   80.0f, 1);
                    EditorSliderFloat(CLAY_ID("EditorShadowSplitNear")     , "Split near"     , &settings->shadowSplitNearDistance,  1.0f,   80.0f, 1);
                    EditorSliderFloat(CLAY_ID("EditorShadowPSSM")          , "PSSM lambda"    , &settings->shadowPSSMLambda       ,  0.0f,    1.0f, 2);
                }
                CLAY(CLAY_ID("GraphicsEditorHBAOBox"), EditorPanelBoxDeclaration) {
                    EditorSectionHeader("HBAO");
                    EditorSliderFloat(CLAY_ID("EditorHBAORadius")   , "Radius"   , &settings->hbaoRadius   , 0.05f, 5.0f, 2);
                    EditorSliderFloat(CLAY_ID("EditorHBAOBias")     , "Bias"     , &settings->hbaoBias     ,  0.0f, 1.0f, 2);
                    EditorSliderFloat(CLAY_ID("EditorHBAOIntensity"), "Intensity", &settings->hbaoIntensity,  0.0f, 6.0f, 2);
                    EditorSliderFloat(CLAY_ID("EditorHBAOPower")    , "Power"    , &settings->hbaoPower    , 0.25f, 6.0f, 2);
                }
                CLAY(CLAY_ID("GraphicsEditorPostBox"), EditorPanelBoxDeclaration) {
                    EditorSectionHeader("Post / AA");
                    EditorSliderFloat(CLAY_ID("EditorMLAAThreshold"), "MLAA threshold", &settings->mlaaThreshold  , 0.01f, 0.25f, 3);
                    EditorSliderFloat(CLAY_ID("EditorExposure")     , "Exposure"      , &settings->exposure       , 0.10f, 4.00f, 2);
                    EditorSliderFloat(CLAY_ID("EditorGamma")        , "Gamma"         , &settings->gamma          , 1.00f, 3.20f, 2);
                    EditorSliderFloat(CLAY_ID("EditorGodRays")      , "God rays"      , &settings->godRayIntensity, 0.00f, 8.00f, 2);
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
                        .enableSDSM = false,
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
                        .shadowPSSMLambda = SHADOW_PSSM_LAMBDA
                    };
                }
            }
        }
    }
    Clay_RenderCommandArray commands = UIEndLayout();
    UIRenderCommands(&commands);
    EditorDrawScrollBar(CLAY_ID("GraphicsEditorScroll"));
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
