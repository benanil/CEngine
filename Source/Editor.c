#include "Include/Slug.h"
#include "Include/UIRenderer.h"
#include "Include/Platform.h"
#include "Include/Random.h"

void ShowFps()
{
    static char fpsText[32] = "fps:0";
    static char msText[128] = "ms:0";
    static double lastUpdateTime = 0.0;
    double currentTime = TimeSinceStartup();

    if (currentTime - lastUpdateTime >= 0.25)
    {
        lastUpdateTime = currentTime;
        f32 dt = GetDeltaTime();
        int fps = (dt > 1.0e-6f) ? (int)(1.0f / dt) : 0;
        f32 ms = dt * 1000.0f;
        int len = IntToString(fpsText + 4, (int64_t)fps, 0);
        fpsText[4 + len] = '\0';
        len = IntToString(msText + 3, (int64_t)ms, 0);
        msText[3 + len] = '\0';
    }

    SlugAppendText2D(NULL, fpsText, (float2){32.0f, 600.0f}, 32.0f, WangHash(78091234));
    SlugAppendText2D(NULL, msText , (float2){32.0f, 650.0f}, 32.0f, WangHash(67894));
}

void UIRenderCallback(void)
{
    static bool enabled = true;
    static f32 slider = 0.62f;
    static char textBox[128] = "edit me";
    static char textArea[512] = "Text area 中文测试 日本語テスト\nArabic: العربية\nGreek: Ελληνικά";
    UIPushRoundedRect((float2){ 32.0f, 32.0f }, (float2){ 760.0f, 500.0f }, 1.5f, UIGetColor(UIColor_Quad));
    UIPushBorder(UIGetFloat(UIFloat_LineThickness) * 2.0f, UIGetColor(UIColor_Border));
    UIPushFloat(UIFloat_TextScale, 0.86f);
    UIText("SDF + Slug Immediate UI", (float2){ 56.0f, 56.0f });
    UIPopFloat(UIFloat_TextScale);
    if (UIButton("Button", (float2) { 56.0f, 94.0f }, (float2) { 160.0f, 44.0f }))
        AX_LOG("button clicked");
    UIRadioButton("Checkbox", (float2){ 56.0f, 150.0f }, &enabled);
    UISliderFloat("Slider", (float2){ 56.0f, 196.0f }, &slider, 180.0f);
    UITextBox("Text Box", (float2){ 56.0f, 242.0f }, textBox, (u32)sizeof(textBox), 260.0f);
    UITextArea("Text Area", (float2){ 56.0f, 292.0f }, textArea, (u32)sizeof(textArea), (float2){ 520.0f, 160.0f });

    ShowFps();
}
