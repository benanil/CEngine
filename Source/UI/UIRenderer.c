#define CLAY_IMPLEMENTATION
#include "UI_Internal.h"
#include "Include/Memory.h"
#include "Include/Platform.h" // PlatformContext.lastTime for milisecond text
#include "Include/Random.h"
#include "Include/String.h"
#include "Include/Algorithm.h"
#include "Include/Rendering.h"
#include "Extern/kb/kb_text_shape.h"

UIRenderer g_UIRenderer;
UIContext g_UI;
UILayoutContext g_UILayout;

extern WindowState  g_WindowState;
extern RenderState  g_RenderState;

static char g_UISliderValueLabels[64][96];
static u32  g_UISliderValueLabelIndex;

static void UIRenderLayoutImage(const Clay_RenderCommand* command);

static void UILayoutHandleError(Clay_ErrorData errorData)
{
    AX_WARN("Clay error: %.*s", errorData.errorText.length, errorData.errorText.chars);
}

static Clay_Dimensions UIMeasureText(Clay_StringSlice text, Clay_TextElementConfig* config, void* userData)
{
    (void)userData;
    f32 size = (f32)Maxu32(config && config->fontSize ? config->fontSize : 16u, 1u);
    float2 measured = SlugCalcTextSizeN(SlugGetDemoFont(), text.chars, (u32)Maxs32(text.length, 0), size);
    if (config && config->lineHeight > 0u) measured.y = Maxf32(measured.y, (f32)config->lineHeight);
    return (Clay_Dimensions){ measured.x, measured.y };
}

static void UILayoutInit(void)
{
    if (g_UILayout.initialized) return;

    u32 memorySize = Clay_MinMemorySize();
    g_UILayout.memory = AllocateTLSFGlobal(memorySize);
    if (!g_UILayout.memory)
    {
        AX_WARN("Clay init skipped, allocation failed: %u bytes", memorySize);
        return;
    }

    g_UILayout.memorySize = memorySize;
    Clay_Arena arena = Clay_CreateArenaWithCapacityAndMemory(memorySize, g_UILayout.memory);
    Clay_Initialize(arena, (Clay_Dimensions){ (f32)Maxu32(g_WindowState.prev_width, 1u), (f32)Maxu32(g_WindowState.prev_height, 1u) },
                           (Clay_ErrorHandler){ UILayoutHandleError });
    Clay_SetMeasureTextFunction(UIMeasureText, NULL);
    g_UILayout.initialized = true;
}

void UIInit(void)
{
    MemsetZero(&g_UIRenderer, sizeof(UIRenderer));
    MemsetZero(&g_UI, sizeof(UIContext));
    g_UIRenderer.capacity = UI_MAX_SHAPES;
    g_UIRenderer.imageCapacity = UI_MAX_IMAGES;
    g_UIRenderer.shapes = (UIShape*)AllocateTLSFGlobal((size_t)g_UIRenderer.capacity * sizeof(UIShape));
    g_UIRenderer.images = (UIImageCommand*)AllocateTLSFGlobal((size_t)g_UIRenderer.imageCapacity * sizeof(UIImageCommand));
    g_RenderState.uiShapeBuffer = CreateBuffer(NULL, (size_t)g_UIRenderer.capacity * sizeof(UIShape), BReadRasterBit, "UIShapeBuffer");
    g_RenderState.uiShapeDrawArgsBuffer = CreateBuffer(NULL, sizeof(SDL_GPUIndirectDrawCommand), BIndirectBit, "UIShapeDrawArgsBuffer");

    for (u32 i = 0; i < UIColor_Count; i++) g_UI.colorStackCount[i] = -1;
    for (u32 i = 0; i < UIFloat_Count; i++) g_UI.floatStackCount[i] = -1;
    g_UI.colors[UIColor_Text]           = 0xFFE1E1E1u;
    g_UI.colors[UIColor_Quad]           = 0xEE111111u;
    g_UI.colors[UIColor_Hovered]        = 0x8CFFFFFFu;
    g_UI.colors[UIColor_Line]           = 0xFFDEDEDEu;
    g_UI.colors[UIColor_Border]         = 0xFF484848u;
    g_UI.colors[UIColor_CheckboxBG]     = 0xFF0B0B0Bu;
    g_UI.colors[UIColor_TextBoxBG]      = 0xFF0B0B0Bu;
    g_UI.colors[UIColor_SliderInside]   = 0xCF888888u;
    g_UI.colors[UIColor_TextBoxCursor]  = 0xFF11FF11u;
    g_UI.colors[UIColor_SelectedBorder] = 0xFF008CFAu;

    g_UI.floats[UIFloat_LineThickness]  = 1.5f;
    g_UI.floats[UIFloat_ContentStart]   = 160.0f;
    g_UI.floats[UIFloat_ButtonSpace]    = 12.0f;
    g_UI.floats[UIFloat_TextScale]      = 1.0f;
    g_UI.floats[UIFloat_TextBoxWidth]   = 175.0f;
    g_UI.floats[UIFloat_SliderHeight]   = 18.0f;
    g_UI.floats[UIFloat_Depth]          = 0.9f;
    g_UI.floats[UIFloat_FieldWidth]     = 98.0f;
    g_UI.floats[UIFloat_TextWrapWidth]  = 100.0f;
    g_UI.floats[UIFloat_ScrollWidth]    = 16.0f;
    UILayoutInit();
}

static void UILayoutBeginFrame(void)
{
    if (!g_UILayout.initialized) return;

    u32 drawableW = Maxu32(g_WindowState.prev_width, 1u);
    u32 drawableH = Maxu32(g_WindowState.prev_height, 1u);
    Clay_SetLayoutDimensions((Clay_Dimensions){ (f32)drawableW, (f32)drawableH });
    Clay_SetPointerState((Clay_Vector2){ g_UI.mouse.x, g_UI.mouse.y }, GetMouseDown(MouseButton_Left) != 0u);

    f32 wheel = GetMouseWheelDelta();
    Clay_UpdateScrollContainers(false, (Clay_Vector2){ 0.0f, wheel * 24.0f }, GetDeltaTime());
    PlatformCtx.MouseWheelDelta = 0.0f;
}

Clay_RenderCommandArray UIEndLayout(void)
{
    if (!g_UILayout.initialized) return (Clay_RenderCommandArray){0};
    return Clay_EndLayout(GetDeltaTime());
}

u64 UIAutoID(const void* ptr)
{
    u64 x = (u64)(uintptr_t)ptr ^ ((u64)g_UI.nextAutoID++ * 0x9E3779B97F4A7C15ull);
    return MurmurHash(x);
}

static Clay_Color UIButtonColor(bool hovered, bool selected)
{
    if (hovered) return UIColorToClay(UIGetColor(UIColor_Hovered));
    if (selected) return UIColorToClay(UIGetColor(UIColor_SelectedBorder));
    return UIColorToClay(UIGetColor(UIColor_Quad));
}

static Clay_Color UIPanelColor(void)
{
    return UIColorToClay(UIGetColor(UIColor_TextBoxBG));
}

static void UIRenderLayoutRectangle(const Clay_RenderCommand* command)
{
    const Clay_RectangleRenderData* data = &command->renderData.rectangle;
    f32 radius = Maxf32(Maxf32(data->cornerRadius.topLeft, data->cornerRadius.topRight), Maxf32(data->cornerRadius.bottomLeft, data->cornerRadius.bottomRight));
    UIPushRoundedRect((float2){ command->boundingBox.x, command->boundingBox.y }, (float2){ command->boundingBox.width, command->boundingBox.height }, radius, UIPackClayColor(data->backgroundColor));
}

static void UIRenderLayoutBorder(const Clay_RenderCommand* command)
{
    const Clay_BorderRenderData* data = &command->renderData.border;
    f32 radius = Maxf32(Maxf32(data->cornerRadius.topLeft, data->cornerRadius.topRight), Maxf32(data->cornerRadius.bottomLeft, data->cornerRadius.bottomRight));
    f32 width = Maxf32(Maxf32((f32)data->width.left, (f32)data->width.right), Maxf32((f32)data->width.top, (f32)data->width.bottom));
    if (width <= 0.0f) return;

    bool uniformWidth = data->width.left == data->width.right && data->width.left == data->width.top && data->width.left == data->width.bottom;
    if (!uniformWidth)
    {
        static int warningCount = 0;
        if (warningCount++ < 4) AX_WARN("Clay non-uniform border is not supported yet");
        return;
    }

    UIPushRoundedRect((float2){ command->boundingBox.x, command->boundingBox.y }, (float2){ command->boundingBox.width, command->boundingBox.height }, radius, 0u);
    UIPushBorder(width, UIPackClayColor(data->color));
}

static void UIRenderLayoutText(const Clay_RenderCommand* command)
{
    const Clay_TextRenderData* data = &command->renderData.text;
    Clay_StringSlice text = data->stringContents;
    SlugAppendText2DN(NULL, text.chars, (u32)Maxs32(text.length, 0), (float2){ command->boundingBox.x, command->boundingBox.y }, (f32)Maxu32(data->fontSize, 1u), UIPackClayColor(data->textColor));
}

static void UIRenderLayoutImage(const Clay_RenderCommand* command)
{
    const Clay_ImageRenderData* data = &command->renderData.image;
    const UIImageData* image = (const UIImageData*)data->imageData;
    if (!image || !image->texture)
    {
        if (!g_UILayout.warnedImage)
        {
            AX_WARN("Clay image command skipped, imageData must point to UIImageData with a valid texture");
            g_UILayout.warnedImage = true;
        }
        return;
    }
    if (!g_UIRenderer.images || g_UIRenderer.imageCount >= g_UIRenderer.imageCapacity)
    {
        AX_WARN("UI image batch full: count=%u capacity=%u", g_UIRenderer.imageCount, g_UIRenderer.imageCapacity);
        return;
    }

    UIImageCommand* out = &g_UIRenderer.images[g_UIRenderer.imageCount++];
    out->texture = image->texture;
    out->sampler = image->sampler ? image->sampler : g_RenderState.sampler;
    out->rect[0] = command->boundingBox.x;
    out->rect[1] = command->boundingBox.y;
    out->rect[2] = command->boundingBox.width;
    out->rect[3] = command->boundingBox.height;
    out->uv[0] = image->uv[0];
    out->uv[1] = image->uv[1];
    out->uv[2] = image->uv[2] != 0.0f ? image->uv[2] : 1.0f;
    out->uv[3] = image->uv[3] != 0.0f ? image->uv[3] : 1.0f;
    MemCopy(out->clip, g_UI.clipRect, sizeof(out->clip));
    out->tintColor = UIPackClayColor(data->backgroundColor);
    out->shapeFence = g_UIRenderer.count;
    out->radius = Maxf32(Maxf32(data->cornerRadius.topLeft, data->cornerRadius.topRight), Maxf32(data->cornerRadius.bottomLeft, data->cornerRadius.bottomRight));
}


void UIClear(void)
{
    g_UIRenderer.count = 0u;
    g_UIRenderer.imageCount = 0u;
}

void UIBeginFrame(void)
{
    UIClear();
    g_UISliderValueLabelIndex = 0u;
    g_UI.nextAutoID = 1u;
    g_UI.wasHovered = false;
    g_UI.anyElementClicked = false;
    g_UI.screenSize = (float2){ (f32)Maxu32(g_WindowState.prev_width, 1u), (f32)Maxu32(g_WindowState.prev_height, 1u) };
    g_UI.windowRatio = (float2){ 1.0f, 1.0f };
    g_UI.uiScale = 1.0f;
    g_UI.clipStackCount = 0;
    g_UI.clipRect[0] = 0.0f;
    g_UI.clipRect[1] = 0.0f;
    g_UI.clipRect[2] = g_UI.screenSize.x;
    g_UI.clipRect[3] = g_UI.screenSize.y;

    f32 mx, my;
    wGetMouseWindowPos(&mx, &my);
    if (PlatformCtx.WindowWidth > 0 && PlatformCtx.WindowHeight > 0)
    {
        mx *= g_UI.screenSize.x / (f32)PlatformCtx.WindowWidth;
        my *= g_UI.screenSize.y / (f32)PlatformCtx.WindowHeight;
    }
    g_UI.mouse = (float2){ mx, my };
    if (!GetMouseDown(MouseButton_Left)) g_UI.active = 0u;
    if (!GetMouseDown(MouseButton_Left)) g_UI.textDragFocus = 0u;
    if (!g_UI.keyboardFocus) PlatformConsumeTextKeyEvents(NULL, UINT32_MAX);
    UILayoutBeginFrame();
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
    s->clip[0] = g_UI.clipRect[0];
    s->clip[1] = g_UI.clipRect[1];
    s->clip[2] = g_UI.clipRect[2];
    s->clip[3] = g_UI.clipRect[3];
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

void UIPushBorder(f32 thickness, u32 color)
{
    if (g_UIRenderer.count == 0) return;
    UIShape* s = &g_UIRenderer.shapes[g_UIRenderer.count - 1u];
    s->borderColor = color;
    s->params[1] = thickness;
}

void UIPushClipRect(float2 pos, float2 size)
{
    if (g_UI.clipStackCount >= (s32)UI_STACK_SIZE)
    {
        AX_WARN("UI clip stack full");
        return;
    }

    MemCopy(g_UI.clipStack[g_UI.clipStackCount++], g_UI.clipRect, sizeof(g_UI.clipRect));
    f32 x0 = pos.x;
    f32 y0 = pos.y;
    f32 x1 = pos.x + Maxf32(size.x, 0.0f);
    f32 y1 = pos.y + Maxf32(size.y, 0.0f);
    g_UI.clipRect[0] = Maxf32(g_UI.clipRect[0], x0);
    g_UI.clipRect[1] = Maxf32(g_UI.clipRect[1], y0);
    g_UI.clipRect[2] = Minf32(g_UI.clipRect[2], x1);
    g_UI.clipRect[3] = Minf32(g_UI.clipRect[3], y1);
}

void UIPopClipRect(void)
{
    if (g_UI.clipStackCount <= 0)
    {
        AX_WARN("UI clip stack underflow");
        return;
    }

    MemCopy(g_UI.clipRect, g_UI.clipStack[--g_UI.clipStackCount], sizeof(g_UI.clipRect));
}

void UIGetClipRect(f32 outClip[4])
{
    if (!outClip) return;
    MemCopy(outClip, g_UI.clipRect, sizeof(g_UI.clipRect));
}


bool UIClicked(void)
{
    Clay_PointerData pointer = Clay_GetPointerState();
    return Clay_Hovered() && pointer.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME;
}

bool UIButton(Clay_ElementId id, Clay_String label, Clay_Dimensions size, bool selected)
{
    bool clicked = false;
    CLAY(id, {
        .layout = {
            .sizing = { CLAY_SIZING_FIXED(size.width), CLAY_SIZING_FIXED(size.height) },
            .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER }
        },
        .backgroundColor = UIButtonColor(Clay_Hovered(), selected),
        .cornerRadius = CLAY_CORNER_RADIUS(size.height * 0.5f)
    }) {
        if (UIClicked()) clicked = true;
        CLAY_TEXT(label, CLAY_TEXT_CONFIG({
            .fontSize = (u16)Maxu32((u32)(17.0f * UIGetFloat(UIFloat_TextScale)), 1u),
            .textColor = UIColorToClay(UIGetColor(UIColor_Text))
        }));
    }
    return clicked;
}

bool UICheckbox(Clay_ElementId id, Clay_String label, bool* value)
{
    bool checked = value && *value;
    bool changed = false;

    CLAY(id, {
        .layout = {
            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(28.0f) },
            .childGap = 8,
            .layoutDirection = CLAY_LEFT_TO_RIGHT,
            .childAlignment = { .y = CLAY_ALIGN_Y_CENTER }
        }
    }) {
        bool rowHovered = Clay_Hovered();
        if (value && UIClicked())
        {
            *value = !*value;
            checked = *value;
            changed = true;
        }

        CLAY(CLAY_ID_LOCAL("Box"), {
            .layout = {
                .sizing = { CLAY_SIZING_FIXED(18.0f), CLAY_SIZING_FIXED(18.0f) },
                .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER }
            },
            .backgroundColor = UIColorToClay(rowHovered ? UIGetColor(UIColor_Hovered) : UIGetColor(UIColor_CheckboxBG)),
            .cornerRadius = CLAY_CORNER_RADIUS(4.0f),
            .border = { .color = UIColorToClay(UIGetColor(UIColor_Border)), .width = CLAY_BORDER_ALL((u16)Maxf32(UIGetFloat(UIFloat_LineThickness), 1.0f)) }
        }) {
            if (checked)
            {
                CLAY(CLAY_ID_LOCAL("Mark"), {
                    .layout = { .sizing = { CLAY_SIZING_FIXED(10.0f), CLAY_SIZING_FIXED(10.0f) } },
                    .backgroundColor = UIColorToClay(UIGetColor(UIColor_SliderInside)),
                    .cornerRadius = CLAY_CORNER_RADIUS(2.0f)
                }) {}
            }
        }

        CLAY_TEXT(label, CLAY_TEXT_CONFIG({
            .fontSize = (u16)Maxu32((u32)(15.0f * UIGetFloat(UIFloat_TextScale)), 1u),
            .textColor = UIColorToClay(UIGetColor(UIColor_Text))
        }));
    }

    return changed;
}

void UIProgressBar(Clay_ElementId id, Clay_String label, f32 value01)
{
    value01 = Saturatef32(value01);
    CLAY(id, {
        .layout = {
            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(28.0f) },
            .childGap = 10,
            .layoutDirection = CLAY_LEFT_TO_RIGHT,
            .childAlignment = { .y = CLAY_ALIGN_Y_CENTER }
        }
    }) {
        CLAY_TEXT(label, CLAY_TEXT_CONFIG({
            .fontSize = (u16)Maxu32((u32)(15.0f * UIGetFloat(UIFloat_TextScale)), 1u),
            .textColor = UIColorToClay(UIGetColor(UIColor_Text))
        }));

        CLAY(CLAY_ID_LOCAL("Track"), {
            .layout = {
                .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(12.0f) },
                .childAlignment = { .y = CLAY_ALIGN_Y_CENTER }
            },
            .backgroundColor = UIPanelColor(),
            .cornerRadius = CLAY_CORNER_RADIUS(6.0f)
        }) {
            CLAY(CLAY_ID_LOCAL("Fill"), {
                .layout = { .sizing = { CLAY_SIZING_PERCENT(value01), CLAY_SIZING_FIXED(12.0f) } },
                .backgroundColor = UIColorToClay(UIGetColor(UIColor_SliderInside)),
                .cornerRadius = CLAY_CORNER_RADIUS(6.0f)
            }) {}
        }
    }
}

bool UISliderFloat(Clay_ElementId id, Clay_String label, f32* value, f32 minValue, f32 maxValue)
{
    if (!value || !IsFiniteF32(minValue) || !IsFiniteF32(maxValue) || maxValue <= minValue) return false;

    bool changed = false;
    f32 currentValue = IsFiniteF32(*value) ? *value : minValue;
    f32 clampedValue = Clampf32(currentValue, minValue, maxValue);
    if (clampedValue != *value)
    {
        *value = clampedValue;
        changed = true;
    }

    Clay_ElementId trackId = Clay_GetElementIdWithIndex(CLAY_STRING("UISliderTrack"), id.id);
    Clay_ElementData trackData = Clay_GetElementData(trackId);
    u64 activeId = (u64)id.id;
    if (trackData.found && Clay_PointerOver(trackId) && GetMousePressed(MouseButton_Left))
    {
        g_UI.active = activeId;
    }
    if (trackData.found && g_UI.active == activeId && GetMouseDown(MouseButton_Left))
    {
        f32 t = (g_UI.mouse.x - trackData.boundingBox.x) / Maxf32(trackData.boundingBox.width, 1.0f);
        f32 newValue = Clampf32(minValue + t * (maxValue - minValue), minValue, maxValue);
        if (newValue != *value)
        {
            *value = newValue;
            changed = true;
        }
    }

    f32 value01 = Saturatef32((Clampf32(*value, minValue, maxValue) - minValue) / (maxValue - minValue));
    CLAY(id, {
        .layout = {
            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(34.0f) },
            .padding = { 0, 4, 0, 0 },
            .childGap = 10,
            .layoutDirection = CLAY_LEFT_TO_RIGHT,
            .childAlignment = { .y = CLAY_ALIGN_Y_CENTER }
        }
    }) {
        CLAY_TEXT(label, CLAY_TEXT_CONFIG({
            .fontSize = (u16)Maxu32((u32)(15.0f * UIGetFloat(UIFloat_TextScale)), 1u),
            .textColor = UIColorToClay(UIGetColor(UIColor_Text))
        }));

        CLAY(CLAY_ID_LOCAL("Spacer"), {
            .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1.0f) } }
        }) {}

        f32 knobWidth = 8.0f;
        f32 knobOffset = value01 * Maxf32(trackData.found ? trackData.boundingBox.width - knobWidth : 0.0f, 0.0f);
        CLAY(trackId, {
            .layout = {
                .sizing = { CLAY_SIZING_FIXED(UIGetFloat(UIFloat_TextBoxWidth)), CLAY_SIZING_FIXED(14.0f) },
                .layoutDirection = CLAY_LEFT_TO_RIGHT,
                .childAlignment = { .y = CLAY_ALIGN_Y_CENTER }
            },
            .backgroundColor = UIColorToClay(UIGetColor(UIColor_TextBoxBG)),
            .cornerRadius = CLAY_CORNER_RADIUS(7.0f)
        }) {
            CLAY(CLAY_ID_LOCAL("Fill"), {
                .layout = { .sizing = { CLAY_SIZING_FIXED(knobOffset), CLAY_SIZING_FIXED(14.0f) } },
                .backgroundColor = UIColorToClay(UIGetColor(UIColor_SliderInside)),
                .cornerRadius = CLAY_CORNER_RADIUS(7.0f)
            }) {}

            CLAY(CLAY_ID_LOCAL("Knob"), {
                .layout = { .sizing = { CLAY_SIZING_FIXED(knobWidth), CLAY_SIZING_FIXED(18.0f) } },
                .backgroundColor = UIColorToClay(UIGetColor(UIColor_Text)),
                .cornerRadius = CLAY_CORNER_RADIUS(4.0f)
            }) {}

            CLAY(CLAY_ID_LOCAL("Remainder"), {
                .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1.0f) } }
            }) {}
        }
    }

    return changed;
}

bool UISliderFloatValue(Clay_ElementId id, Clay_String label, f32* value, f32 minValue, f32 maxValue, int decimals)
{
    if (!value) return false;

    u32 index = g_UISliderValueLabelIndex++ % (u32)(sizeof(g_UISliderValueLabels) / sizeof(g_UISliderValueLabels[0]));
    char* text = g_UISliderValueLabels[index];
    u32 capacity = (u32)sizeof(g_UISliderValueLabels[0]);
    u32 length = 0u;

    u32 labelLength = (u32)Maxs32(label.length, 0);
    for (u32 i = 0u; i < labelLength && length + 1u < capacity; i++) text[length++] = label.chars[i];
    if (length + 2u < capacity)
    {
        text[length++] = ':';
        text[length++] = ' ';
    }

    f32 displayValue = IsFiniteF32(*value) ? *value : minValue;
    if (IsFiniteF32(minValue) && IsFiniteF32(maxValue) && maxValue > minValue) displayValue = Clampf32(displayValue, minValue, maxValue);
    if (length < capacity)
    {
        length += (u32)FloatToString(text + length, displayValue, Maxs32(decimals, 0));
        if (length >= capacity) length = capacity - 1u;
    }
    text[length] = '\0';

    Clay_String valueLabel = { .isStaticallyAllocated = false, .length = (s32)length, .chars = text };
    return UISliderFloat(id, valueLabel, value, minValue, maxValue);
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
    if ((u32)count >= UI_STACK_SIZE) { AX_WARN("UI color stack full: %u", (u32)what); return; }
    g_UI.colorStackCount[what] = count;
    g_UI.colorStack[what][count] = color;
}

void UIPopColor(UIColor what)
{
    if ((u32)what >= UIColor_Count) return;
    if (g_UI.colorStackCount[what] < 0) { AX_WARN("UI color stack underflow: %u", (u32)what); return; }
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
    if ((u32)count >= UI_STACK_SIZE) { AX_WARN("UI float stack full: %u", (u32)what); return; }
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
    if (g_UI.floatStackCount[what] < 0) { AX_WARN("UI float stack underflow: %u", (u32)what); return; }
    g_UI.floatStackCount[what]--;
}

UIImageData UIImageFromTexture(Texture* texture)
{
    UIImageData image = {0};
    static int warnCount = 0;
    image.uv[2] = 1.0f;
    image.uv[3] = 1.0f;
    if (!texture || !texture->handle )
    {
        if (warnCount++ < 8) AX_WARN("UIImageFromTexture got null texture");
        image.texture = g_RenderState.textures[0].handle;
    }
    else image.texture = texture->handle;
    return image;
}

void UIRenderCommands(Clay_RenderCommandArray* commands)
{
    if (!g_UILayout.initialized || !commands) return;

    for (s32 i = 0; i < commands->length; i++)
    {
        Clay_RenderCommand* command = Clay_RenderCommandArray_Get(commands, i);
        switch (command->commandType)
        {
            case CLAY_RENDER_COMMAND_TYPE_RECTANGLE: UIRenderLayoutRectangle(command); break;
            case CLAY_RENDER_COMMAND_TYPE_BORDER:    UIRenderLayoutBorder(command); break;
            case CLAY_RENDER_COMMAND_TYPE_TEXT:      UIRenderLayoutText(command); break;
            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START:
                UIPushClipRect((float2){ command->boundingBox.x, command->boundingBox.y }, (float2){ command->boundingBox.width, command->boundingBox.height });
                break;
            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END:
                UIPopClipRect();
                break;
            case CLAY_RENDER_COMMAND_TYPE_IMAGE:     UIRenderLayoutImage(command); break;
            case CLAY_RENDER_COMMAND_TYPE_CUSTOM:
                if (!g_UILayout.warnedCustom)
                {
                    AX_WARN("Clay custom commands are not rendered yet");
                    g_UILayout.warnedCustom = true;
                }
                break;
            case CLAY_RENDER_COMMAND_TYPE_OVERLAY_COLOR_START:
            case CLAY_RENDER_COMMAND_TYPE_OVERLAY_COLOR_END:
                if (!g_UILayout.warnedOverlay)
                {
                    AX_WARN("Clay overlay color commands are not rendered yet");
                    g_UILayout.warnedOverlay = true;
                }
                break;
            case CLAY_RENDER_COMMAND_TYPE_NONE:
            default:
                break;
        }
    }
}


void UIRender(SDL_GPUCommandBuffer* cmd, SDL_GPUColorTargetInfo* colorTarget)
{
    if (g_UIRenderer.count == 0u && g_UIRenderer.imageCount == 0u) return;
    if (g_UIRenderer.count > 0u && (!g_RenderState.uiShapePipeline || !g_RenderState.uiShapeBuffer))
    {
        AX_WARN("UI render skipped, resources not initialized");
        UIClear();
        return;
    }

    UIParams params = {0};
    params.screenScale[0] = (f32)Maxu32(g_WindowState.prev_width, 1u);
    params.screenScale[1] = (f32)Maxu32(g_WindowState.prev_height, 1u);
    params.screenScale[2] = 1.0f;
    params.screenScale[3] = 0.0f;

    if (g_UIRenderer.count > 0u)
    {
        UpdateGPUBuffer(g_RenderState.uiShapeBuffer, g_UIRenderer.shapes, (size_t)g_UIRenderer.count * sizeof(UIShape), 0ull);
    }
    if (g_UIRenderer.imageCount > 0u && (!g_RenderState.uiImagePipeline || !g_RenderState.sampler))
    {
        AX_WARN("UI image render skipped, resources not initialized");
        UIClear();
        return;
    }

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, colorTarget, 1, NULL);
    u32 firstShape = 0u;
    bool shapePipelineBound = false;

    for (u32 i = 0; i < g_UIRenderer.imageCount; i++)
    {
        UIImageCommand* image = &g_UIRenderer.images[i];
        u32 shapeFence = Minu32(image->shapeFence, g_UIRenderer.count);
        if (shapeFence > firstShape)
        {
            if (!shapePipelineBound)
            {
                SDL_BindGPUGraphicsPipeline(pass, g_RenderState.uiShapePipeline);
                SDL_BindGPUVertexStorageBuffers(pass, 0, &g_RenderState.uiShapeBuffer, 1);
                SDL_PushGPUVertexUniformData(cmd, 0, &params, sizeof(params));
                SDL_PushGPUFragmentUniformData(cmd, 0, &params, sizeof(params));
                shapePipelineBound = true;
            }
            SDL_DrawGPUPrimitives(pass, 6, shapeFence - firstShape, 0, firstShape);
            firstShape = shapeFence;
        }

        SDL_BindGPUGraphicsPipeline(pass, g_RenderState.uiImagePipeline);
        shapePipelineBound = false;

        UIImageParams imageParams = {0};
        MemCopy(imageParams.screenScale, params.screenScale, sizeof(imageParams.screenScale));
        MemCopy(imageParams.rect, image->rect, sizeof(imageParams.rect));
        MemCopy(imageParams.uv, image->uv, sizeof(imageParams.uv));
        MemCopy(imageParams.clip, image->clip, sizeof(imageParams.clip));
        imageParams.tintColor = image->tintColor;
        imageParams.radius = image->radius;

        SDL_GPUTextureSamplerBinding binding = {
            .texture = image->texture,
            .sampler = image->sampler ? image->sampler : g_RenderState.sampler
        };
        SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
        SDL_PushGPUVertexUniformData(cmd, 0, &imageParams, sizeof(imageParams));
        SDL_PushGPUFragmentUniformData(cmd, 0, &imageParams, sizeof(imageParams));
        SDL_DrawGPUPrimitives(pass, 6, 1, 0, 0);
    }

    if (g_UIRenderer.count > firstShape)
    {
        if (!shapePipelineBound)
        {
            SDL_BindGPUGraphicsPipeline(pass, g_RenderState.uiShapePipeline);
            SDL_BindGPUVertexStorageBuffers(pass, 0, &g_RenderState.uiShapeBuffer, 1);
            SDL_PushGPUVertexUniformData(cmd, 0, &params, sizeof(params));
            SDL_PushGPUFragmentUniformData(cmd, 0, &params, sizeof(params));
        }
        SDL_DrawGPUPrimitives(pass, 6, g_UIRenderer.count - firstShape, 0, firstShape);
    }

    SDL_EndGPURenderPass(pass);
    UIClear();
}

static void UILayoutDestroy(void)
{
    if (g_UILayout.memory) DeAllocateTLSFGlobal(g_UILayout.memory);
    g_UILayout = (UILayoutContext){0};
}


void UIDestroy(void)
{
    UILayoutDestroy();
    UIDestroyTextShape();
    if (g_UIRenderer.shapes) DeAllocateTLSFGlobal(g_UIRenderer.shapes);
    if (g_UIRenderer.images) DeAllocateTLSFGlobal(g_UIRenderer.images);
    g_UIRenderer = (UIRenderer){0};
}
