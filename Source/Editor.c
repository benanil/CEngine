#include "Include/Slug.h"
#include "Include/UIRenderer.h"
#include "Include/Platform.h"
#include "Include/Random.h"

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

static void ClayTestUI(void)
{
    static u32 selectedButton = UINT32_MAX;
    static bool checkboxValue = true;
    static f32 sliderValue = 0.35f;
    static char clayTextEdit[512] = "Editable multiline text\ninside a Clay layout slot.";
    static bool imageLoadAttempted = false;
    static Texture testTexture;
    static UIImageData testImage;

    if (!imageLoadAttempted)
    {
        imageLoadAttempted = true;
        testTexture = rImportTexture("Assets/Textures/Test.jpg", TexFlags_MipMap, "ClayTestImage");
        testImage = UIImageFromTexture(&testTexture);
    }

    Clay_BeginLayout();
    Clay_ElementId textEditId = CLAY_ID("ClayTextEditSlot");

    CLAY(CLAY_ID("ClayTestRoot"), {
        .layout = {
            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
            .padding = { 0, 0, 0, 0 },
            .childAlignment = { .x = CLAY_ALIGN_X_LEFT, .y = CLAY_ALIGN_Y_CENTER },
        }
    }) {
        ShowFps();

        CLAY(CLAY_ID("ClayTestPanel"), {
            .layout = {
                .sizing = { CLAY_SIZING_FIXED(420.0f), CLAY_SIZING_GROW(0) },
                .padding = { 22, 22, 22, 22 },
                .childGap = 14,
                .layoutDirection = CLAY_TOP_TO_BOTTOM
            },
            .backgroundColor = { 14, 17, 27, 248 },
            .cornerRadius = CLAY_CORNER_RADIUS(0.0f),
            .border = { .color = { 55, 78, 120, 180 }, .width = CLAY_BORDER_ALL(4) }
        }) {
            CLAY(CLAY_ID("ClayTestHeader"), {
                .layout = {
                    .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) },
                    .childGap = 6,
                    .layoutDirection = CLAY_TOP_TO_BOTTOM
                }
            }) {
                CLAY_TEXT(CLAY_STRING("Clay Layout Test"), CLAY_TEXT_CONFIG({
                    .fontSize = 26,
                    .textColor = { 220, 232, 255, 255 }
                }));
                CLAY_TEXT(CLAY_STRING("Left-docked full-height panel."), CLAY_TEXT_CONFIG({
                    .fontSize = 14,
                    .textColor = { 110, 130, 170, 255 }
                }));
                CLAY_TEXT(CLAY_STRING("Multiline Clay text line 1\nline 2: clipped, measured, and rendered by Slug"), CLAY_TEXT_CONFIG({
                    .fontSize = 14,
                    .lineHeight = 18,
                    .textColor = { 155, 176, 220, 255 }
                }));
            }

            CLAY(CLAY_ID("ClayTestDivider"), {
                .layout = {
                    .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1.0f) },
                },
                .backgroundColor = { 45, 60, 95, 140 }
            }) {}

            CLAY(CLAY_ID("ClayTestControls"), {
                .layout = {
                    .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) },
                    .padding = { 16, 16, 14, 14 },
                    .childGap = 14,
                    .layoutDirection = CLAY_TOP_TO_BOTTOM
                },
                .backgroundColor = { 18, 22, 34, 200 },
                .cornerRadius = CLAY_CORNER_RADIUS(10.0f),
                .border = { .color = { 40, 56, 88, 120 }, .width = CLAY_BORDER_ALL(1) }
            }) {
                UICheckbox(CLAY_ID("ClayTestCheckbox"), CLAY_STRING("Enable feature"), &checkboxValue);
                UISliderFloat(CLAY_ID("ClayTestSlider"), CLAY_STRING("Intensity"), &sliderValue, 0.0f, 1.0f);
                UIProgressBar(CLAY_ID("ClayTestProgress"), CLAY_STRING("Progress"), sliderValue);
            }

            CLAY(CLAY_ID("ClayTestRow"), {
                .layout = {
                    .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(40.0f) },
                    .childGap = 10,
                    .layoutDirection = CLAY_LEFT_TO_RIGHT,
                    .childAlignment = { .y = CLAY_ALIGN_Y_CENTER }
                }
            }) {
                const Clay_String buttonNames[3] = {
                    CLAY_STRING_CONST("Alpha"),
                    CLAY_STRING_CONST("Beta"),
                    CLAY_STRING_CONST("Gamma")
                };
                for (u32 i = 0; i < 3u; i++)
                {
                    Clay_String label = i == selectedButton ? CLAY_STRING("Selected") : buttonNames[i];
                    if (UIButton(CLAY_IDI("ClayTestPill", i), label, (Clay_Dimensions){ 96.0f, 34.0f }, i == selectedButton)) selectedButton = i;
                }
            }

            CLAY(textEditId, {
                .layout = {
                    .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(96.0f) }
                }
            }) {}

            CLAY(CLAY_ID("ClayScrollTest"), {
                .layout = {
                    .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
                    .padding = { 10, 10, 10, 10 },
                    .childGap = 8,
                    .layoutDirection = CLAY_TOP_TO_BOTTOM
                },
                .backgroundColor = { 9, 11, 18, 255 },
                .cornerRadius = CLAY_CORNER_RADIUS(10.0f),
                .border = { .color = { 30, 42, 68, 120 }, .width = CLAY_BORDER_ALL(1) },
                .clip = { .vertical = true, .childOffset = Clay_GetScrollOffset() }
            }) {
                for (u32 i = 0; i < 8u; i++)
                {
                    f32 t = (f32)i / 7.0f;
                    CLAY(CLAY_IDI("ClayScrollBlock", i), {
                        .layout = {
                            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(30.0f) },
                            .padding = { 12, 12, 6, 6 },
                            .childAlignment = { .y = CLAY_ALIGN_Y_CENTER }
                        },
                        .backgroundColor = {
                            28.0f + t * 20.0f,
                            42.0f + t * 38.0f,
                            100.0f + t * 80.0f,
                            255.0f
                        },
                        .cornerRadius = CLAY_CORNER_RADIUS(6.0f)
                    }) {
                        CLAY_TEXT(CLAY_STRING("This Slug text is clipped by Clay scissor"), CLAY_TEXT_CONFIG({
                            .fontSize = 14,
                            .wrapMode = CLAY_TEXT_WRAP_NONE,
                            .textColor = { 200, 215, 245, 230 }
                        }));
                    }
                }
            }
        }

        CLAY(CLAY_ID("ClayImageGridPanel"), {
            .layout = {
                .sizing = { CLAY_SIZING_FIXED(500.0f), CLAY_SIZING_FIXED(380.0f) },
                .padding = { 16, 16, 16, 16 },
                .childGap = 12,
                .layoutDirection = CLAY_TOP_TO_BOTTOM
            },
            .floating = {
                .attachTo = CLAY_ATTACH_TO_PARENT,
                .attachPoints = { CLAY_ATTACH_POINT_RIGHT_BOTTOM, CLAY_ATTACH_POINT_RIGHT_BOTTOM },
                .offset = { -24.0f, -24.0f },
                .zIndex = 10
            },
            .backgroundColor = { 12, 15, 24, 245 },
            .cornerRadius = CLAY_CORNER_RADIUS(18.0f),
            .border = { .color = { 72, 96, 150, 180 }, .width = CLAY_BORDER_ALL(2) }
        }) {
            CLAY(CLAY_ID("ClayImageGridHeader"), {
                .layout = {
                    .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) },
                    .childGap = 4,
                    .layoutDirection = CLAY_TOP_TO_BOTTOM
                }
            }) {
                CLAY_TEXT(CLAY_STRING("Scrollable Image Grid"), CLAY_TEXT_CONFIG({
                    .fontSize = 20,
                    .textColor = { 225, 235, 255, 255 }
                }));
                CLAY_TEXT(CLAY_STRING("Assets/Textures/Test.jpg repeated in a clipped 2D grid"), CLAY_TEXT_CONFIG({
                    .fontSize = 13,
                    .textColor = { 135, 154, 196, 255 }
                }));
            }

            CLAY(CLAY_ID("ClayImageGridScroll"), {
                .layout = {
                    .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
                    .padding = { 10, 10, 10, 10 },
                    .childGap = 12,
                    .layoutDirection = CLAY_TOP_TO_BOTTOM
                },
                .backgroundColor = { 6, 8, 14, 255 },
                .cornerRadius = CLAY_CORNER_RADIUS(12.0f),
                .border = { .color = { 36, 50, 84, 160 }, .width = CLAY_BORDER_ALL(1) },
                .clip = { .vertical = true, .childOffset = Clay_GetScrollOffset() }
            }) {
                for (u32 y = 0; y < 16u; y++)
                {
                    CLAY(CLAY_IDI("ClayImageGridRow", y), {
                        .layout = {
                            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(96.0f) },
                            .childGap = 12,
                            .layoutDirection = CLAY_LEFT_TO_RIGHT
                        }
                    }) {
                        for (u32 x = 0; x < 4u; x++)
                        {
                            u32 index = y * 4u + x;
                            CLAY(CLAY_IDI("ClayImageGridCell", index), {
                                .layout = {
                                    .sizing = { CLAY_SIZING_FIXED(96.0f), CLAY_SIZING_FIXED(96.0f) }
                                },
                                .cornerRadius = CLAY_CORNER_RADIUS(10.0f),
                                .image = { .imageData = testImage.texture ? &testImage : NULL }
                            }) {}
                        }
                    }
                }
            }
        }
    }

    Clay_RenderCommandArray commands = UIEndLayout();
    UIRenderCommands(&commands);

    Clay_ElementData textEditData = Clay_GetElementData(textEditId);
    if (textEditData.found)
    {
        UITextArea(NULL,
                   (float2){ textEditData.boundingBox.x, textEditData.boundingBox.y },
                   clayTextEdit,
                   (u32)sizeof(clayTextEdit),
                   (float2){ textEditData.boundingBox.width, textEditData.boundingBox.height });
    }
}

void UIRenderCallback(void)
{
    // static bool enabled = true;
    // static f32 slider = 0.62f;
    // static char textBox[128] = "edit me";
    // static char textArea[512] = "Text area 中文测试 日本語テスト\nArabic: العربية\nGreek: Ελληνικά";
    // UIPushRoundedRect((float2){ 32.0f, 32.0f }, (float2){ 760.0f, 500.0f }, 1.5f, UIGetColor(UIColor_Quad));
    // UIPushBorder(UIGetFloat(UIFloat_LineThickness) * 2.0f, UIGetColor(UIColor_Border));
    // UIPushFloat(UIFloat_TextScale, 0.86f);
    // UIText("SDF + Slug Immediate UI", (float2){ 56.0f, 56.0f });
    // UIPopFloat(UIFloat_TextScale);
    // UITextArea("Text Area", (float2){ 56.0f, 292.0f }, textArea, (u32)sizeof(textArea), (float2){ 520.0f, 160.0f });
    if (GetKeyPressed('c'))
    {
        Clay_SetDebugModeEnabled(!Clay_IsDebugModeEnabled());
    }
    ClayTestUI();
}
