#include "RenderingInternal.h"

typedef struct UIRenderer_
{
    UIShape* shapes;
    u32 count;
    u32 capacity;
} UIRenderer;

#define UI_STACK_SIZE 6u

typedef struct UIContext_
{
    float2 screenSize;
    float2 windowRatio;
    float2 mouse;
    float2 mouseOld;
    f32 uiScale;
    bool wasHovered;
    bool anyElementClicked;
    u64 active;
    u32 nextAutoID;
    u32 colors[UIColor_Count];
    f32 floats[UIFloat_Count];
    u32 colorStack[UIColor_Count][UI_STACK_SIZE];
    f32 floatStack[UIFloat_Count][UI_STACK_SIZE];
    s32 colorStackCount[UIColor_Count];
    s32 floatStackCount[UIFloat_Count];
} UIContext;

typedef struct UIParams_
{
    f32 screenScale[4];
} UIParams;

static UIRenderer g_UIRenderer;
static UIContext g_UI;

static float2 UIResolvePos(float2 p)
{
    return (float2){ p.x * g_UI.windowRatio.x, p.y * g_UI.windowRatio.y };
}

static float2 UIResolveSize(float2 s)
{
    return (float2){ s.x * g_UI.windowRatio.x, s.y * g_UI.windowRatio.y };
}

static f32 UIResolveScalar(f32 v)
{
    return v * g_UI.uiScale;
}

static u64 UIAutoID(const void* ptr)
{
    u64 x = (u64)(uintptr_t)ptr ^ ((u64)g_UI.nextAutoID++ * 0x9E3779B97F4A7C15ull);
    return MurmurHash(x);
}

static f32 UIClamp01(f32 v)
{
    return Minf32(Maxf32(v, 0.0f), 1.0f);
}

static void UIUploadBuffer(SDL_GPUCommandBuffer* cmd, SDL_GPUBuffer* buffer, const void* data, size_t size)
{
    SDL_GPUTransferBufferCreateInfo transferDesc = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size  = size
    };
    SDL_GPUTransferBuffer* transferBuffer = SDL_CreateGPUTransferBuffer(g_GPUDevice, &transferDesc);
    void* dst = SDL_MapGPUTransferBuffer(g_GPUDevice, transferBuffer, false);
    MemCopy(dst, data, size);
    SDL_UnmapGPUTransferBuffer(g_GPUDevice, transferBuffer);

    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
    SDL_UploadToGPUBuffer(copyPass,
        &(SDL_GPUTransferBufferLocation){ .transfer_buffer = transferBuffer, .offset = 0 },
        &(SDL_GPUBufferRegion){ .buffer = buffer, .offset = 0, .size = size },
        true);
    SDL_EndGPUCopyPass(copyPass);
    SDL_ReleaseGPUTransferBuffer(g_GPUDevice, transferBuffer);
}

void UIInit(void)
{
    SDL_zero(g_UIRenderer);
    SDL_zero(g_UI);
    g_UIRenderer.capacity = UI_MAX_SHAPES;
    g_UIRenderer.shapes = (UIShape*)AllocateTLSFGlobal((size_t)g_UIRenderer.capacity * sizeof(UIShape));
    g_RenderState.uiShapeBuffer = CreateBuffer(NULL, (size_t)g_UIRenderer.capacity * sizeof(UIShape), BReadRasterBit, "UIShapeBuffer");
    g_RenderState.uiShapeDrawArgsBuffer = CreateBuffer(NULL, sizeof(SDL_GPUIndirectDrawCommand), BIndirectBit, "UIShapeDrawArgsBuffer");

    for (u32 i = 0; i < UIColor_Count; i++) g_UI.colorStackCount[i] = -1;
    for (u32 i = 0; i < UIFloat_Count; i++) g_UI.floatStackCount[i] = -1;
    g_UI.colors[UIColor_Text] = 0xFFE1E1E1u;
    g_UI.colors[UIColor_Quad] = 0xDD111111u;
    g_UI.colors[UIColor_Hovered] = 0x8CFFFFFFu;
    g_UI.colors[UIColor_Line] = 0xFFDEDEDEu;
    g_UI.colors[UIColor_Border] = 0xFF484848u;
    g_UI.colors[UIColor_CheckboxBG] = 0xFF0B0B0Bu;
    g_UI.colors[UIColor_TextBoxBG] = 0xFF0B0B0Bu;
    g_UI.colors[UIColor_SliderInside] = 0xCF888888u;
    g_UI.colors[UIColor_TextBoxCursor] = 0xFFFFFFFFu;
    g_UI.colors[UIColor_SelectedBorder] = 0xFF008CFAu;

    g_UI.floats[UIFloat_LineThickness] = 1.5f;
    g_UI.floats[UIFloat_ContentStart] = 160.0f;
    g_UI.floats[UIFloat_ButtonSpace] = 12.0f;
    g_UI.floats[UIFloat_TextScale] = 1.0f;
    g_UI.floats[UIFloat_TextBoxWidth] = 175.0f;
    g_UI.floats[UIFloat_SliderHeight] = 18.0f;
    g_UI.floats[UIFloat_Depth] = 0.9f;
    g_UI.floats[UIFloat_FieldWidth] = 98.0f;
    g_UI.floats[UIFloat_TextWrapWidth] = 100.0f;
    g_UI.floats[UIFloat_ScrollWidth] = 16.0f;
}

void UIDestroy(void)
{
    if (g_UIRenderer.shapes) DeAllocateTLSFGlobal(g_UIRenderer.shapes);
    g_UIRenderer = (UIRenderer){0};
}

void UIClear(void)
{
    g_UIRenderer.count = 0u;
}

void UIBeginFrame(void)
{
    UIClear();
    g_UI.nextAutoID = 1u;
    g_UI.wasHovered = false;
    g_UI.anyElementClicked = false;
    g_UI.screenSize = (float2){ (f32)Maxu32(g_WindowState.prev_drawablew, 1u), (f32)Maxu32(g_WindowState.prev_drawableh, 1u) };
    g_UI.windowRatio = (float2){ g_UI.screenSize.x / 1920.0f, g_UI.screenSize.y / 1080.0f };
    g_UI.uiScale = (g_UI.windowRatio.x + g_UI.windowRatio.y) * 0.5f;
    if (Absf32(g_UI.windowRatio.x - g_UI.windowRatio.y) > 0.6f) g_UI.uiScale = Minf32(g_UI.windowRatio.x, g_UI.windowRatio.y);
    g_UI.uiScale = Maxf32(g_UI.uiScale, 0.01f);

    f32 mx, my;
    wGetMouseWindowPos(&mx, &my);
    g_UI.mouse = (float2){ mx / Maxf32(g_UI.windowRatio.x, 0.01f), my / Maxf32(g_UI.windowRatio.y, 0.01f) };
    if (!GetMouseDown(MouseButton_Left)) g_UI.active = 0u;
}

void UIEndFrame(SDL_GPUCommandBuffer* cmd, SDL_GPUColorTargetInfo* colorTarget)
{
    UIRender(cmd, colorTarget);
    SlugRender2D(cmd, colorTarget, SlugGetDemoFont());
    g_UI.mouseOld = g_UI.mouse;
}

static bool UIPushShape(float2 pos, float2 size, f32 radius, u32 color, UIShapeType shape)
{
    if (!g_UIRenderer.shapes)
    {
        AX_WARN("UI push skipped, resources not initialized");
        return false;
    }
    if (g_UIRenderer.count >= g_UIRenderer.capacity)
    {
        AX_WARN("UI shape batch full: count=%u capacity=%u", g_UIRenderer.count, g_UIRenderer.capacity);
        return false;
    }

    pos = UIResolvePos(pos);
    size = UIResolveSize(size);
    radius = UIResolveScalar(radius);
    size.x = Maxf32(size.x, 0.0f);
    size.y = Maxf32(size.y, 0.0f);
    UIShape* s = &g_UIRenderer.shapes[g_UIRenderer.count++];
    s->rect[0] = pos.x;
    s->rect[1] = pos.y;
    s->rect[2] = size.x;
    s->rect[3] = size.y;
    s->params[0] = radius;
    s->params[1] = 0.0f;
    s->params[2] = 1.0f;
    s->params[3] = 0.0f;
    s->color = color;
    s->borderColor = color;
    s->shape = (u32)shape;
    s->flags = 0u;
    return true;
}

bool UIPushRect(float2 pos, float2 size, u32 color)
{
    return UIPushShape(pos, size, 0.0f, color, UIShapeType_Rect);
}

bool UIPushRoundedRect(float2 pos, float2 size, f32 radius, u32 color)
{
    return UIPushShape(pos, size, radius, color, UIShapeType_RoundedRect);
}

bool UIPushCircle(float2 center, f32 radius, u32 color)
{
    float2 pos = { center.x - radius, center.y - radius };
    float2 size = { radius * 2.0f, radius * 2.0f };
    return UIPushShape(pos, size, radius, color, UIShapeType_Circle);
}

bool UIPushCapsule(float2 pos, float2 size, u32 color)
{
    return UIPushShape(pos, size, Minf32(size.x, size.y) * 0.5f, color, UIShapeType_Capsule);
}

bool UIPushBorder(float2 pos, float2 size, f32 thickness, u32 color)
{
    if (!UIPushShape(pos, size, 0.0f, color, UIShapeType_Rect)) return false;
    UIShape* s = &g_UIRenderer.shapes[g_UIRenderer.count - 1u];
    s->color = 0u;
    s->borderColor = color;
    s->params[1] = UIResolveScalar(thickness);
    return true;
}

void UISetColor(UIColor what, u32 color)
{
    if ((u32)what < UIColor_Count) g_UI.colors[what] = color;
}

u32 UIGetColor(UIColor what)
{
    if ((u32)what >= UIColor_Count) return 0xFFFFFFFFu;
    s32 count = g_UI.colorStackCount[what];
    return count >= 0 ? g_UI.colorStack[what][count] : g_UI.colors[what];
}

void UIPushColor(UIColor what, u32 color)
{
    if ((u32)what >= UIColor_Count) return;
    s32 count = g_UI.colorStackCount[what] + 1;
    if ((u32)count >= UI_STACK_SIZE)
    {
        AX_WARN("UI color stack full: %u", (u32)what);
        return;
    }
    g_UI.colorStackCount[what] = count;
    g_UI.colorStack[what][count] = color;
}

void UIPopColor(UIColor what)
{
    if ((u32)what >= UIColor_Count) return;
    if (g_UI.colorStackCount[what] < 0)
    {
        AX_WARN("UI color stack underflow: %u", (u32)what);
        return;
    }
    g_UI.colorStackCount[what]--;
}

void UISetFloat(UIFloat what, f32 value)
{
    if ((u32)what < UIFloat_Count) g_UI.floats[what] = value;
}

f32 UIGetFloat(UIFloat what)
{
    if ((u32)what >= UIFloat_Count) return 0.0f;
    s32 count = g_UI.floatStackCount[what];
    return count >= 0 ? g_UI.floatStack[what][count] : g_UI.floats[what];
}

void UIPushFloat(UIFloat what, f32 value)
{
    if ((u32)what >= UIFloat_Count) return;
    s32 count = g_UI.floatStackCount[what] + 1;
    if ((u32)count >= UI_STACK_SIZE)
    {
        AX_WARN("UI float stack full: %u", (u32)what);
        return;
    }
    g_UI.floatStackCount[what] = count;
    g_UI.floatStack[what][count] = value;
}

void UIPushFloatAdd(UIFloat what, f32 value)
{
    UIPushFloat(what, UIGetFloat(what) + value);
}

void UIPopFloat(UIFloat what)
{
    if ((u32)what >= UIFloat_Count) return;
    if (g_UI.floatStackCount[what] < 0)
    {
        AX_WARN("UI float stack underflow: %u", (u32)what);
        return;
    }
    g_UI.floatStackCount[what]--;
}

bool UIClickCheck(float2 pos, float2 size, UIClickOpt flags)
{
    if (flags & UIClickOpt_BigCollision)
    {
        f32 grow = Minf32(size.x, size.y) * 0.5f;
        pos.x -= grow;
        pos.y -= grow;
        size.x += grow * 2.0f;
        size.y += grow * 2.0f;
    }
    g_UI.wasHovered = RectPointIntersect(pos, size, g_UI.mouse) != 0u;
    bool released = GetMouseReleased(MouseButton_Left) != 0u;
    bool pressed = GetMousePressed(MouseButton_Left) != 0u;
    g_UI.anyElementClicked |= g_UI.wasHovered && released;
    if ((flags & UIClickOpt_WhileMouseDown) && GetMouseDown(MouseButton_Left)) return g_UI.wasHovered;
    return g_UI.wasHovered && (released || pressed);
}

bool UIIsHovered(void)
{
    return g_UI.wasHovered;
}

float2 UITextSize(const char* text)
{
    f32 size = 32.0f * UIGetFloat(UIFloat_TextScale) * g_UI.uiScale;
    float2 px = SlugCalcTextSize(SlugGetDemoFont(), text, size);
    return (float2){ px.x / Maxf32(g_UI.windowRatio.x, 0.01f), px.y / Maxf32(g_UI.windowRatio.y, 0.01f) };
}

void UIText(const char* text, float2 pos)
{
    if (!text) return;
    f32 size = 32.0f * UIGetFloat(UIFloat_TextScale) * g_UI.uiScale;
    SlugAppendText2D(SlugGetDemoFont(), text, UIResolvePos(pos), size, UIGetColor(UIColor_Text));
}

bool UIButton(const char* text, float2 pos, float2 size)
{
    if (size.x + size.y < MATH_Epsilon)
    {
        f32 buttonSpace = UIGetFloat(UIFloat_ButtonSpace);
        float2 textSize = UITextSize(text);
        pos.x -= buttonSpace * 2.0f;
        pos.y += buttonSpace;
        size = (float2){ textSize.x + buttonSpace * 2.0f, textSize.y + buttonSpace };
    }

    bool clicked = UIClickCheck(pos, size, UIClickOpt_None);
    u32 color = g_UI.wasHovered ? UIGetColor(UIColor_Hovered) : UIGetColor(UIColor_Quad);
    UIPushRoundedRect(pos, size, 6.0f, color);
    UIPushBorder(pos, size, UIGetFloat(UIFloat_LineThickness), UIGetColor(UIColor_Border));

    if (text)
    {
        float2 textSize = UITextSize(text);
        float2 textPos = { pos.x + (size.x - textSize.x) * 0.5f, pos.y + (size.y - textSize.y) * 0.5f };
        UIText(text, textPos);
    }
    return clicked;
}

bool UICheckbox(const char* text, float2 pos, bool* enabled)
{
    float2 textSize = UITextSize(text);
    UIText(text, pos);
    f32 box = Maxf32(textSize.y * 0.75f, 16.0f);
    f32 controlX = Maxf32(pos.x + UIGetFloat(UIFloat_ContentStart) - box, pos.x + textSize.x + UIGetFloat(UIFloat_ButtonSpace));
    float2 boxPos = { controlX, pos.y + (textSize.y - box) * 0.5f };
    bool clicked = UIClickCheck(boxPos, (float2){ box, box }, UIClickOpt_BigCollision);
    if (clicked && enabled) *enabled = !*enabled;

    UIPushRoundedRect(boxPos, (float2){ box, box }, 3.0f, UIGetColor(UIColor_CheckboxBG));
    UIPushBorder(boxPos, (float2){ box, box }, UIGetFloat(UIFloat_LineThickness), UIGetColor(UIColor_Border));
    if (enabled && *enabled)
    {
        f32 pad = box * 0.22f;
        UIPushRoundedRect((float2){ boxPos.x + pad, boxPos.y + pad }, (float2){ box - pad * 2.0f, box - pad * 2.0f }, 2.0f, UIGetColor(UIColor_SliderInside));
    }
    return clicked;
}

bool UISliderFloat(const char* label, float2 pos, f32* value, f32 width)
{
    float2 labelSize = UITextSize(label);
    UIText(label, pos);
    float2 barSize = { width, UIGetFloat(UIFloat_SliderHeight) };
    f32 controlX = Maxf32(pos.x + UIGetFloat(UIFloat_ContentStart) - width, pos.x + labelSize.x + UIGetFloat(UIFloat_ButtonSpace));
    float2 barPos = { controlX, pos.y + (labelSize.y - barSize.y) * 0.5f };
    u64 id = UIAutoID(value);
    bool hovered = RectPointIntersect(barPos, barSize, g_UI.mouse) != 0u;
    if (hovered && GetMousePressed(MouseButton_Left)) g_UI.active = id;

    bool edited = false;
    if (g_UI.active == id && GetMouseDown(MouseButton_Left))
    {
        *value = UIClamp01((g_UI.mouse.x - barPos.x) / Maxf32(barSize.x, 0.001f));
        edited = true;
    }

    g_UI.wasHovered = hovered;
    UIPushRoundedRect(barPos, barSize, barSize.y * 0.5f, UIGetColor(UIColor_TextBoxBG));
    UIPushBorder(barPos, barSize, UIGetFloat(UIFloat_LineThickness), UIGetColor(UIColor_Border));
    if (*value > 0.0f)
    {
        float2 fillSize = { barSize.x * UIClamp01(*value), barSize.y };
        UIPushCapsule(barPos, fillSize, UIGetColor(UIColor_SliderInside));
    }
    return edited;
}

void UIRender(SDL_GPUCommandBuffer* cmd, SDL_GPUColorTargetInfo* colorTarget)
{
    if (g_UIRenderer.count == 0u) return;
    if (!g_RenderState.uiShapePipeline || !g_RenderState.uiShapeBuffer || !g_RenderState.uiShapeDrawArgsBuffer)
    {
        AX_WARN("UI render skipped, resources not initialized");
        UIClear();
        return;
    }

    SDL_GPUIndirectDrawCommand draw = {
        .num_vertices = 6u,
        .num_instances = g_UIRenderer.count,
        .first_vertex = 0u,
        .first_instance = 0u
    };
    UIUploadBuffer(cmd, g_RenderState.uiShapeBuffer, g_UIRenderer.shapes, (size_t)g_UIRenderer.count * sizeof(UIShape));
    UIUploadBuffer(cmd, g_RenderState.uiShapeDrawArgsBuffer, &draw, sizeof(draw));

    UIParams params = {0};
    params.screenScale[0] = (f32)Maxu32(g_WindowState.prev_drawablew, 1u);
    params.screenScale[1] = (f32)Maxu32(g_WindowState.prev_drawableh, 1u);
    params.screenScale[2] = 1.0f;
    params.screenScale[3] = 0.0f;

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, colorTarget, 1, NULL);
    SDL_BindGPUGraphicsPipeline(pass, g_RenderState.uiShapePipeline);
    SDL_BindGPUVertexStorageBuffers(pass, 0, &g_RenderState.uiShapeBuffer, 1);
    SDL_PushGPUVertexUniformData(cmd, 0, &params, sizeof(params));
    SDL_PushGPUFragmentUniformData(cmd, 0, &params, sizeof(params));
    SDL_DrawGPUPrimitivesIndirect(pass, g_RenderState.uiShapeDrawArgsBuffer, 0, 1);
    SDL_EndGPURenderPass(pass);
    UIClear();
}

void UIRenderDemo(void)
{
    static bool enabled = true;
    static f32 slider = 0.62f;
    UIPushRoundedRect((float2){ 32.0f, 32.0f }, (float2){ 760.0f, 315.0f }, 18.0f, 0xCC202028u);
    UIPushBorder((float2){ 32.0f, 32.0f }, (float2){ 760.0f, 315.0f }, 1.5f, UIGetColor(UIColor_Border));
    UIPushFloat(UIFloat_TextScale, 0.86f);
    UIText("SDF + Slug Immediate UI", (float2){ 56.0f, 56.0f });
    UIPopFloat(UIFloat_TextScale);
    UIButton("Button", (float2){ 56.0f, 94.0f }, (float2){ 160.0f, 44.0f });
    UICheckbox("Checkbox", (float2){ 56.0f, 150.0f }, &enabled);
    UISliderFloat("Slider", (float2){ 56.0f, 196.0f }, &slider, 180.0f);
    UIText("Unicode: 中文测试  日本語テスト  العربية  Ελληνικά", (float2){ 56.0f, 252.0f });
}
