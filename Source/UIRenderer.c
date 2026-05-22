#include "RenderingInternal.h"
#include "Include/Platform.h" // PlatformContext.lastTime for milisecond text
#include "Include/String.h"
#include "Extern/kb/kb_text_shape.h"
#include <SDL3/SDL_clipboard.h>

typedef struct UIRenderer_
{
    UIShape* shapes;
    u32 count;
    u32 capacity;
} UIRenderer;

#define UI_STACK_SIZE 6u
#define UI_MAX_SHAPE_FONTS (1u + SLUG_MAX_FALLBACK_FONTS)
#define UI_TEXT_MAX_CODEPOINTS SLUG_MAX_TEXT
#define UI_TEXT_MAX_COMMANDS SLUG_MAX_TEXT
#define UI_TEXT_MAX_LINES 128u

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
    u64 keyboardFocus;
    u64 textDragFocus;
    u32 caret;
    u32 selectionAnchor;
    u32 nextAutoID;
    u32 colors[UIColor_Count];
    f32 floats[UIFloat_Count];
    u32 colorStack[UIColor_Count][UI_STACK_SIZE];
    f32 floatStack[UIFloat_Count][UI_STACK_SIZE];
    s32 colorStackCount[UIColor_Count];
    s32 floatStackCount[UIFloat_Count];
    kbts_shape_context* textShapeContext;
    kbts_font* textShapeFonts[UI_MAX_SHAPE_FONTS];
    u32 textShapeFontCount;
} UIContext;

typedef struct UITextCommand_
{
    u32 faceIndex;
    u32 glyphIndex;
    u32 codepointIndex;
    kbts_direction direction;
    float2 pos;
    f32 advanceX;
    f32 width;
} UITextCommand;

typedef struct UITextLine_
{
    u32 firstCommand;
    u32 onePastLastCommand;
    u32 minCodepoint;
    u32 maxCodepoint;
    kbts_direction direction;
    f32 x;
    f32 y;
    f32 width;
} UITextLine;

typedef struct UITextLayout_
{
    UITextCommand commands[UI_TEXT_MAX_COMMANDS];
    UITextLine lines[UI_TEXT_MAX_LINES];
    kbts_break_flags breakFlags[UI_TEXT_MAX_CODEPOINTS + 1u];
    u32 commandCount;
    u32 lineCount;
    u32 codepointCount;
    f32 lineHeight;
    f32 ascent;
    f32 width;
    f32 height;
    bool valid;
} UITextLayout;

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

static u32 UIStringLength(const char* text, u32 capacity)
{
    if (!text || capacity == 0u) return 0u;
    return (u32)StringLengthSafe(text, capacity);
}

static u32 UICodepointCount(const char* text, u32 bytes)
{
    if (!text) return 0u;
    u32 result = 0u;
    const char* at = text;
    const char* end = text + bytes;
    while (at < end && *at)
    {
        u32 codepoint;
        int step = CodepointFromUtf8(&codepoint, at, end);
        at += step > 0 ? step : 1;
        result++;
    }
    return result;
}

static u32 UIByteFromCodepoint(const char* text, u32 bytes, u32 codepointIndex)
{
    if (!text) return 0u;
    u32 cp = 0u;
    const char* at = text;
    const char* end = text + bytes;
    while (at < end && *at && cp < codepointIndex)
    {
        u32 codepoint;
        int step = CodepointFromUtf8(&codepoint, at, end);
        at += step > 0 ? step : 1;
        cp++;
    }
    return (u32)(at - text);
}

static void UISelectionRange(u32* a, u32* b)
{
    if (*a > *b)
    {
        u32 t = *a;
        *a = *b;
        *b = t;
    }
}

static void UISetCaret(u32 caret, bool selecting)
{
    g_UI.caret = caret;
    if (!selecting) g_UI.selectionAnchor = caret;
}

static bool UIHasSelection(void)
{
    return g_UI.caret != g_UI.selectionAnchor;
}

static void UIDeleteRange(char* buffer, u32* len, u32 start, u32 end)
{
    UISelectionRange(&start, &end);
    if (!buffer || start >= end || end > *len) return;
    SDL_memmove(buffer + start, buffer + end, (size_t)(*len - end + 1u));
    *len -= end - start;
}

static void UIDeleteCodepointRange(char* buffer, u32 capacity, u32 start, u32 end)
{
    u32 len = UIStringLength(buffer, capacity);
    UISelectionRange(&start, &end);
    u32 byteStart = UIByteFromCodepoint(buffer, len, start);
    u32 byteEnd = UIByteFromCodepoint(buffer, len, end);
    UIDeleteRange(buffer, &len, byteStart, byteEnd);
}

static bool UIDeleteSelection(char* buffer, u32* len)
{
    if (!UIHasSelection()) return false;
    u32 start = g_UI.caret;
    u32 end = g_UI.selectionAnchor;
    UISelectionRange(&start, &end);
    u32 byteStart = UIByteFromCodepoint(buffer, *len, start);
    u32 byteEnd = UIByteFromCodepoint(buffer, *len, end);
    UIDeleteRange(buffer, len, byteStart, byteEnd);
    UISetCaret(start, false);
    return true;
}

static bool UIInsertBytesAtCaret(char* buffer, u32 capacity, const char* text, u32 textLen, bool multiline);

static bool UICopySelectionToClipboard(const char* buffer, u32 capacity)
{
    if (!buffer || !UIHasSelection()) return false;
    u32 len = UIStringLength(buffer, capacity);
    u32 start = g_UI.caret;
    u32 end = g_UI.selectionAnchor;
    UISelectionRange(&start, &end);
    u32 byteStart = UIByteFromCodepoint(buffer, len, start);
    u32 byteEnd = UIByteFromCodepoint(buffer, len, end);
    if (byteStart >= byteEnd) return false;

    u32 size = byteEnd - byteStart;
    char* text = (char*)ArenaPushGlobal((u64)size + 1u);
    MemCopy(text, buffer + byteStart, size);
    text[size] = 0;
    bool result = SDL_SetClipboardText(text);
    ArenaPopGlobal((u64)size + 1u);
    return result;
}

static bool UIPasteClipboardAtCaret(char* buffer, u32 capacity, bool multiline)
{
    if (!buffer || capacity == 0u || !SDL_HasClipboardText()) return false;
    char* clipboard = SDL_GetClipboardText();
    if (!clipboard) return false;
    u32 len = (u32)SDL_strlen(clipboard);
    bool result = UIInsertBytesAtCaret(buffer, capacity, clipboard, len, multiline);
    SDL_free(clipboard);
    return result;
}

static bool UIInsertBytesAtCaret(char* buffer, u32 capacity, const char* text, u32 textLen, bool multiline)
{
    if (!buffer || capacity == 0u || !text || textLen == 0u) return false;
    u32 len = UIStringLength(buffer, capacity);
    UIDeleteSelection(buffer, &len);

    bool edited = false;
    u32 caretByte = UIByteFromCodepoint(buffer, len, g_UI.caret);
    for (u32 i = 0u; i < textLen && len + 1u < capacity;)
    {
        char c = text[i];
        u32 codepoint;
        int step = CodepointFromUtf8(&codepoint, text + i, text + textLen);
        u32 copy = (u32)(step > 0 ? step : 1);
        i += copy;
        if (!multiline && (c == '\r' || c == '\n')) continue;
        if (len + copy >= capacity) break;

        SDL_memmove(buffer + caretByte + copy, buffer + caretByte, (size_t)(len - caretByte + 1u));
        MemCopy(buffer + caretByte, text + i - copy, copy);
        caretByte += copy;
        len += copy;
        g_UI.caret++;
        g_UI.selectionAnchor = g_UI.caret;
        edited = true;
    }
    return edited;
}

static void UIDestroyTextShape(void)
{
    if (g_UI.textShapeContext) kbts_DestroyShapeContext(g_UI.textShapeContext);
    g_UI.textShapeContext = NULL;
    g_UI.textShapeFontCount = 0u;
    SDL_zeroa(g_UI.textShapeFonts);
}

static bool UIEnsureTextShape(SlugFont* font)
{
    u32 faceCount = Minu32(SlugGetFontFaceCount(font), UI_MAX_SHAPE_FONTS);
    if (faceCount == 0u) return false;
    if (g_UI.textShapeContext && g_UI.textShapeFontCount == faceCount) return true;

    UIDestroyTextShape();
    g_UI.textShapeContext = kbts_CreateShapeContext(NULL, NULL);
    if (!g_UI.textShapeContext)
    {
        AX_WARN("UI text shaping context creation failed");
        return false;
    }

    for (u32 i = 0u; i < faceCount; i++)
    {
        u32 dataSize = 0u;
        void* data = SlugGetFontFaceData(font, i, &dataSize);
        if (!data || dataSize == 0u || dataSize > 0x7FFFFFFFu) continue;
        g_UI.textShapeFonts[i] = kbts_ShapePushFontFromMemory(g_UI.textShapeContext, data, (int)dataSize, SlugGetFontFaceCollectionIndex(font, i));
    }
    g_UI.textShapeFontCount = faceCount;
    return true;
}

static u32 UIShapeFontIndex(kbts_font* font)
{
    for (u32 i = 0u; i < g_UI.textShapeFontCount; i++)
    {
        if (g_UI.textShapeFonts[i] == font) return i;
    }
    return UINT32_MAX;
}

static bool UIAppendTextInput(char* buffer, u32 capacity, bool multiline)
{
    char text[256];
    u32 inputBytes = PlatformConsumeTextInput(text, (u32)sizeof(text));
    if (inputBytes == 0u || !buffer || capacity == 0u) return false;
    return UIInsertBytesAtCaret(buffer, capacity, text, inputBytes, multiline);
}

static bool UIBackspaceText(char* buffer, u32 capacity)
{
    u32 len = UIStringLength(buffer, capacity);
    if (UIDeleteSelection(buffer, &len)) return true;
    if (g_UI.caret == 0u) return false;
    UIDeleteCodepointRange(buffer, capacity, g_UI.caret - 1u, g_UI.caret);
    UISetCaret(g_UI.caret - 1u, false);
    return true;
}

static bool UIDeleteTextForward(char* buffer, u32 capacity)
{
    u32 len = UIStringLength(buffer, capacity);
    u32 codepoints = UICodepointCount(buffer, len);
    if (UIDeleteSelection(buffer, &len)) return true;
    if (g_UI.caret >= codepoints) return false;
    UIDeleteCodepointRange(buffer, capacity, g_UI.caret, g_UI.caret + 1u);
    UISetCaret(g_UI.caret, false);
    return true;
}

static u32 UIMoveCaretWithBreaks(const UITextLayout* layout, u32 caret, bool forward, kbts_break_flags breakFlags)
{
    if (!layout || !layout->valid) return caret;
    s32 delta = forward ? 1 : -1;
    s32 at = (s32)caret;
    for (;;)
    {
        s32 next = at + delta;
        if (next < 0 || next > (s32)layout->codepointCount) break;
        at = next;
        if (at == (s32)layout->codepointCount || (breakFlags == 0u) || ((layout->breakFlags[at] & breakFlags) == breakFlags)) break;
    }
    return (u32)at;
}

static u32 UITextLayoutHitTest(const UITextLayout* layout, float2 point);
static float2 UITextLayoutCaretPos(const UITextLayout* layout, u32 caret);

static bool UITextEditBehavior(u64 id, char* buffer, u32 capacity, bool multiline, const UITextLayout* layout)
{
    if (g_UI.keyboardFocus != id || !buffer || capacity == 0u) return false;

    bool edited = false;
    u32 len = UIStringLength(buffer, capacity);
    u32 codepoints = UICodepointCount(buffer, len);
    if (g_UI.caret > codepoints) UISetCaret(codepoints, false);
    if (g_UI.selectionAnchor > codepoints) g_UI.selectionAnchor = codepoints;

    PlatformTextKeyEvent keys[64];
    u32 keyCount = PlatformConsumeTextKeyEvents(keys, (u32)ARRAY_SIZE(keys));
    for (u32 i = 0u; i < keyCount; i++)
    {
        bool shift = (keys[i].mod & SDL_KMOD_SHIFT) != 0;
        bool ctrl = (keys[i].mod & SDL_KMOD_CTRL) != 0;
        switch (keys[i].key)
        {
            case SDLK_A:
                if (ctrl)
                {
                    g_UI.selectionAnchor = 0u;
                    g_UI.caret = codepoints;
                }
                break;
            case SDLK_C:
                if (ctrl) UICopySelectionToClipboard(buffer, capacity);
                break;
            case SDLK_X:
                if (ctrl && UICopySelectionToClipboard(buffer, capacity))
                {
                    edited |= UIDeleteSelection(buffer, &len);
                    codepoints = UICodepointCount(buffer, len);
                }
                break;
            case SDLK_V:
                if (ctrl)
                {
                    edited |= UIPasteClipboardAtCaret(buffer, capacity, multiline);
                    len = UIStringLength(buffer, capacity);
                    codepoints = UICodepointCount(buffer, len);
                }
                break;
            case SDLK_LEFT:
                if (!shift && UIHasSelection()) UISetCaret(Minu32(g_UI.caret, g_UI.selectionAnchor), false);
                else UISetCaret(UIMoveCaretWithBreaks(layout, g_UI.caret, false, ctrl ? KBTS_BREAK_FLAG_WORD : KBTS_BREAK_FLAG_GRAPHEME), shift);
                break;
            case SDLK_RIGHT:
                if (!shift && UIHasSelection()) UISetCaret(Maxu32(g_UI.caret, g_UI.selectionAnchor), false);
                else UISetCaret(UIMoveCaretWithBreaks(layout, g_UI.caret, true, ctrl ? KBTS_BREAK_FLAG_WORD : KBTS_BREAK_FLAG_GRAPHEME), shift);
                break;
            case SDLK_UP:
            case SDLK_DOWN:
                if (multiline && layout && layout->valid)
                {
                    float2 caretPos = UITextLayoutCaretPos(layout, g_UI.caret);
                    caretPos.y += keys[i].key == SDLK_UP ? -layout->lineHeight : layout->lineHeight;
                    UISetCaret(UITextLayoutHitTest(layout, caretPos), shift);
                }
                break;
            case SDLK_HOME:
                UISetCaret(0u, shift);
                break;
            case SDLK_END:
                UISetCaret(codepoints, shift);
                break;
            case SDLK_BACKSPACE:
                edited |= UIBackspaceText(buffer, capacity);
                len = UIStringLength(buffer, capacity);
                codepoints = UICodepointCount(buffer, len);
                break;
            case SDLK_DELETE:
                edited |= UIDeleteTextForward(buffer, capacity);
                len = UIStringLength(buffer, capacity);
                codepoints = UICodepointCount(buffer, len);
                break;
            case SDLK_RETURN:
                if (multiline)
                {
                    edited |= UIInsertBytesAtCaret(buffer, capacity, "\n", 1u, true);
                    len = UIStringLength(buffer, capacity);
                    codepoints = UICodepointCount(buffer, len);
                }
                break;
            default:
                break;
        }
    }

    edited |= UIAppendTextInput(buffer, capacity, multiline);
    return edited;
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
    UIDestroyTextShape();
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
    if (!GetMouseDown(MouseButton_Left)) g_UI.textDragFocus = 0u;
    if (!g_UI.keyboardFocus) PlatformConsumeTextKeyEvents(NULL, UINT32_MAX);
}

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

    UITextDirect(fpsText, (float2){32.0f, 600.0f}, 32.0f, WangHash(78091234));
    UITextDirect(msText, (float2){32.0f, 650.0f}, 32.0f, WangHash(67894));
}

void UIEndFrame(SDL_GPUCommandBuffer* cmd, SDL_GPUColorTargetInfo* colorTarget)
{
    UIRender(cmd, colorTarget);
    ShowFps();
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
    // pos = F2SubF(pos, thickness);
    // size = F2AddF(size, thickness * 2.0f);
    if (!UIPushShape(pos, size, 0.0f, color, UIShapeType_RoundedRect)) return false;
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
    g_UI.anyElementClicked |= g_UI.wasHovered && released;
    if ((flags & UIClickOpt_WhileMouseDown) && GetMouseDown(MouseButton_Left)) return g_UI.wasHovered;
    return g_UI.wasHovered && released;
}

bool UIIsHovered(void)
{
    return g_UI.wasHovered;
}

static bool UITextShapeInternal(const char* text, float2 resolvedPos, f32 size, u32 color, bool append, float2* outSize)
{
    if (outSize) *outSize = (float2){0.0f, 0.0f};
    if (!text) return false;

    SlugFont* font = SlugGetDemoFont();
    if (!UIEnsureTextShape(font)) return false;

    u32 textBytes = (u32)StringLengthSafe(text, SLUG_MAX_TEXT + 1u);
    if (textBytes == 0u)
    {
        if (outSize) outSize->y = Maxf32((SlugGetFontAscent(font) - SlugGetFontDescent(font)) * size, size);
        return true;
    }
    if (textBytes > SLUG_MAX_TEXT) return false;

    kbts_ShapeBegin(g_UI.textShapeContext, KBTS_DIRECTION_DONT_KNOW, KBTS_LANGUAGE_DONT_KNOW);
    kbts_ShapeUtf8(g_UI.textShapeContext, text, (int)textBytes, KBTS_USER_ID_GENERATION_MODE_CODEPOINT_INDEX);
    kbts_ShapeEnd(g_UI.textShapeContext);
    if (kbts_ShapeError(g_UI.textShapeContext) != KBTS_SHAPE_ERROR_NONE) return false;

    f32 width = 0.0f;
    f32 penX = 0.0f;
    f32 baselineY = resolvedPos.y + SlugGetFontAscent(font) * size;
    kbts_run run;
    while (kbts_ShapeRun(g_UI.textShapeContext, &run))
    {
        u32 faceIndex = UIShapeFontIndex(run.Font);
        if (faceIndex == UINT32_MAX) return false;
        f32 emScale = SlugGetFontFaceEmScale(font, faceIndex);
        if (emScale <= 0.0f) return false;

        s32 cursorX = 0;
        s32 cursorY = 0;
        kbts_glyph* glyph;
        while (kbts_GlyphIteratorNext(&run.Glyphs, &glyph))
        {
            f32 glyphX = (f32)(cursorX + glyph->OffsetX) * emScale;
            f32 glyphY = (f32)(cursorY + glyph->OffsetY) * emScale;
            if (append)
            {
                SlugAppendGlyph2D(font, faceIndex, glyph->Id, (float2){ resolvedPos.x + penX + glyphX * size, baselineY - glyphY * size }, size, color);
            }
            cursorX += glyph->AdvanceX;
            cursorY += glyph->AdvanceY;
        }
        penX += (f32)cursorX * emScale * size;
        width = Maxf32(width, penX);
    }

    if (outSize)
    {
        outSize->x = width;
        outSize->y = Maxf32((SlugGetFontAscent(font) - SlugGetFontDescent(font)) * size, size);
    }
    return true;
}

bool UITextDirect(const char* text, float2 resolvedPos, f32 size, u32 color)
{
    return UITextShapeInternal(text, resolvedPos, size, color, true, NULL);
}

static UITextLine* UILayoutBeginLine(UITextLayout* layout, f32 x, f32 y, u32 codepointIndex)
{
    if (layout->lineCount >= UI_TEXT_MAX_LINES) return NULL;
    UITextLine* line = &layout->lines[layout->lineCount++];
    *line = (UITextLine){0};
    line->firstCommand = layout->commandCount;
    line->onePastLastCommand = layout->commandCount;
    line->minCodepoint = codepointIndex;
    line->maxCodepoint = codepointIndex;
    line->direction = KBTS_DIRECTION_DONT_KNOW;
    line->x = x;
    line->y = y;
    return line;
}

static bool UITextBuildLayout(const char* text, u32 bytes, float2 pos, f32 textScale, bool multiline, UITextLayout* layout)
{
    SDL_zero(*layout);
    SlugFont* font = SlugGetDemoFont();
    if (!text || !UIEnsureTextShape(font)) return false;

    f32 sizePx = 32.0f * textScale * g_UI.uiScale;
    layout->ascent = SlugGetFontAscent(font) * sizePx / Maxf32(g_UI.windowRatio.y, 0.01f);
    layout->lineHeight = Maxf32((SlugGetFontAscent(font) - SlugGetFontDescent(font)) * sizePx, sizePx) / Maxf32(g_UI.windowRatio.y, 0.01f);
    layout->codepointCount = UICodepointCount(text, bytes);
    if (layout->codepointCount > UI_TEXT_MAX_CODEPOINTS) return false;

    UITextLine* line = UILayoutBeginLine(layout, pos.x, pos.y, 0u);
    if (!line) return false;
    f32 penX = 0.0f;
    u32 cpBase = 0u;
    u32 lineStartByte = 0u;

    while (lineStartByte <= bytes)
    {
        u32 lineBytes = 0u;
        while (lineStartByte + lineBytes < bytes && text[lineStartByte + lineBytes] != '\n') lineBytes++;

        if (lineBytes == 0u)
        {
            line->onePastLastCommand = layout->commandCount;
            line->width = 0.0f;
            line->maxCodepoint = cpBase;
            if (!multiline || lineStartByte >= bytes) break;

            lineStartByte += 1u;
            cpBase += 1u;
            penX = 0.0f;
            line = UILayoutBeginLine(layout, pos.x, pos.y + layout->lineHeight * (f32)layout->lineCount, cpBase);
            if (!line) break;
            continue;
        }

        kbts_ShapeBegin(g_UI.textShapeContext, KBTS_DIRECTION_DONT_KNOW, KBTS_LANGUAGE_DONT_KNOW);
        kbts_ShapeUtf8(g_UI.textShapeContext, text + lineStartByte, (int)lineBytes, KBTS_USER_ID_GENERATION_MODE_CODEPOINT_INDEX);
        kbts_ShapeEnd(g_UI.textShapeContext);
        if (kbts_ShapeError(g_UI.textShapeContext) != KBTS_SHAPE_ERROR_NONE) return false;

        kbts_run run;
        while (kbts_ShapeRun(g_UI.textShapeContext, &run))
        {
            u32 faceIndex = UIShapeFontIndex(run.Font);
            if (faceIndex == UINT32_MAX) return false;
            f32 emScale = SlugGetFontFaceEmScale(font, faceIndex);
            if (emScale <= 0.0f) return false;
            f32 scaleX = emScale * sizePx / Maxf32(g_UI.windowRatio.x, 0.01f);
            f32 scaleY = emScale * sizePx / Maxf32(g_UI.windowRatio.y, 0.01f);

            if (line->direction == KBTS_DIRECTION_DONT_KNOW) line->direction = run.ParagraphDirection;
            s32 cursorX = 0;
            s32 cursorY = 0;
            kbts_glyph* glyph;
            while (kbts_GlyphIteratorNext(&run.Glyphs, &glyph))
            {
                u32 cp = cpBase + (u32)glyph->UserIdOrCodepointIndex;
                if (cp < UI_TEXT_MAX_CODEPOINTS)
                {
                    kbts_shape_codepoint shapeCodepoint = {0};
                    if (kbts_ShapeGetShapeCodepoint(g_UI.textShapeContext, glyph->UserIdOrCodepointIndex, &shapeCodepoint))
                    {
                        layout->breakFlags[cp] = shapeCodepoint.BreakFlags;
                    }
                }
                if (layout->commandCount < UI_TEXT_MAX_COMMANDS)
                {
                    UITextCommand* cmd = &layout->commands[layout->commandCount++];
                    cmd->faceIndex = faceIndex;
                    cmd->glyphIndex = glyph->Id;
                    cmd->codepointIndex = cp;
                    cmd->direction = run.Direction;
                    cmd->pos.x = pos.x + penX + (f32)(cursorX + glyph->OffsetX) * scaleX;
                    cmd->pos.y = line->y + layout->ascent - (f32)(cursorY + glyph->OffsetY) * scaleY;
                    cmd->advanceX = (f32)glyph->AdvanceX * scaleX;
                    cmd->width = Absf32(cmd->advanceX);
                    line->maxCodepoint = Maxu32(line->maxCodepoint, cp + 1u);
                }
                cursorX += glyph->AdvanceX;
                cursorY += glyph->AdvanceY;
            }
            penX += (f32)cursorX * emScale * sizePx / Maxf32(g_UI.windowRatio.x, 0.01f);
        }

        line->onePastLastCommand = layout->commandCount;
        line->width = penX;
        layout->width = Maxf32(layout->width, penX);
        if (!multiline || lineStartByte + lineBytes >= bytes) break;

        lineStartByte += lineBytes + 1u;
        cpBase += UICodepointCount(text + lineStartByte - lineBytes - 1u, lineBytes + 1u);
        penX = 0.0f;
        line = UILayoutBeginLine(layout, pos.x, pos.y + layout->lineHeight * (f32)layout->lineCount, cpBase);
        if (!line) break;
    }

    layout->height = layout->lineHeight * Maxf32((f32)layout->lineCount, 1.0f);
    layout->valid = true;
    return true;
}

static void UITextDrawLayout(const UITextLayout* layout)
{
    if (!layout || !layout->valid) return;
    SlugFont* font = SlugGetDemoFont();
    f32 sizePx = 32.0f * UIGetFloat(UIFloat_TextScale) * g_UI.uiScale;
    for (u32 i = 0u; i < layout->commandCount; i++)
    {
        const UITextCommand* cmd = &layout->commands[i];
        SlugAppendGlyph2D(font, cmd->faceIndex, cmd->glyphIndex, UIResolvePos(cmd->pos), sizePx, UIGetColor(UIColor_Text));
    }
}

static u32 UITextLineCodepointAtX(const UITextLayout* layout, const UITextLine* line, f32 x)
{
    if (!layout || !line || line->firstCommand == line->onePastLastCommand) return line ? line->minCodepoint : 0u;
    u32 result = line->maxCodepoint;
    u32 prev = UINT32_MAX;
    bool found = false;
    for (u32 i = line->firstCommand; i < line->onePastLastCommand; i++)
    {
        const UITextCommand* cmd = &layout->commands[i];
        result = cmd->codepointIndex;
        if (cmd->codepointIndex != prev && x < cmd->pos.x + cmd->width * 0.5f)
        {
            found = true;
            break;
        }
        prev = cmd->codepointIndex;
    }
    if (!found) return Minu32(line->maxCodepoint, layout->codepointCount);
    if (line->direction == KBTS_DIRECTION_RTL && prev != UINT32_MAX) result = prev;
    return Minu32(result, layout->codepointCount);
}

static u32 UITextLayoutHitTest(const UITextLayout* layout, float2 point)
{
    if (!layout || !layout->valid || layout->lineCount == 0u) return 0u;
    u32 lineIndex = (u32)(Maxf32(point.y - layout->lines[0].y, 0.0f) / Maxf32(layout->lineHeight, 0.001f));
    lineIndex = Minu32(lineIndex, layout->lineCount - 1u);
    return UITextLineCodepointAtX(layout, &layout->lines[lineIndex], point.x);
}

static float2 UITextLayoutCaretPos(const UITextLayout* layout, u32 caret)
{
    if (!layout || !layout->valid || layout->lineCount == 0u) return (float2){0.0f, 0.0f};
    caret = Minu32(caret, layout->codepointCount);
    const UITextLine* line = &layout->lines[layout->lineCount - 1u];
    for (u32 li = 0u; li < layout->lineCount; li++)
    {
        if (caret <= layout->lines[li].maxCodepoint)
        {
            line = &layout->lines[li];
            break;
        }
    }

    f32 x = line->x + line->width;
    for (u32 i = line->firstCommand; i < line->onePastLastCommand; i++)
    {
        const UITextCommand* cmd = &layout->commands[i];
        if (cmd->codepointIndex >= caret)
        {
            x = cmd->pos.x;
            break;
        }
        x = cmd->pos.x + cmd->advanceX;
    }
    return (float2){ x, line->y };
}

static void UITextDrawSelection(const UITextLayout* layout)
{
    if (!layout || !layout->valid || !UIHasSelection()) return;
    u32 selStart = g_UI.caret;
    u32 selEnd = g_UI.selectionAnchor;
    UISelectionRange(&selStart, &selEnd);
    for (u32 li = 0u; li < layout->lineCount; li++)
    {
        const UITextLine* line = &layout->lines[li];
        f32 minX = FLT_MAX;
        f32 maxX = -FLT_MAX;
        for (u32 i = line->firstCommand; i < line->onePastLastCommand; i++)
        {
            const UITextCommand* cmd = &layout->commands[i];
            if (cmd->codepointIndex >= selStart && cmd->codepointIndex < selEnd)
            {
                minX = Minf32(minX, Minf32(cmd->pos.x, cmd->pos.x + cmd->advanceX));
                maxX = Maxf32(maxX, Maxf32(cmd->pos.x, cmd->pos.x + cmd->advanceX));
            }
        }
        if (minX != FLT_MAX && maxX > minX)
        {
            UIPushRoundedRect((float2){ minX, line->y + 1.0f }, (float2){ maxX - minX, layout->lineHeight - 2.0f }, 2.0f, 0x884A90E2u);
        }
    }
}

float2 UITextSize(const char* text)
{
    f32 size = 32.0f * UIGetFloat(UIFloat_TextScale) * g_UI.uiScale;
    float2 px;
    if (!UITextShapeInternal(text, (float2){0.0f, 0.0f}, size, 0u, false, &px)) px = SlugCalcTextSize(SlugGetDemoFont(), text, size);
    return (float2){ px.x / Maxf32(g_UI.windowRatio.x, 0.01f), px.y / Maxf32(g_UI.windowRatio.y, 0.01f) };
}

void UIText(const char* text, float2 pos)
{
    if (!text) return;
    f32 size = 32.0f * UIGetFloat(UIFloat_TextScale) * g_UI.uiScale;
    float2 resolved = UIResolvePos(pos);
    UITextShapeInternal(text, resolved, size, UIGetColor(UIColor_Text), true, NULL);
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
    // UIPushBorder(barPos, barSize, UIGetFloat(UIFloat_LineThickness), UIGetColor(UIColor_Border));
    if (*value > 0.0f)
    {
        float2 fillSize = { barSize.x * UIClamp01(*value), barSize.y };
        UIPushCapsule(barPos, fillSize, UIGetColor(UIColor_SliderInside));
    }
    return edited;
}

static void UISetKeyboardFocus(u64 id)
{
    if (g_UI.keyboardFocus == id) return;
    g_UI.keyboardFocus = id;
    if (id) SDL_StartTextInput(g_SDLWindow);
    else SDL_StopTextInput(g_SDLWindow);
}

bool UITextBox(const char* label, float2 pos, char* buffer, u32 capacity, f32 width)
{
    float2 labelSize = UITextSize(label);
    if (label) UIText(label, pos);

    f32 height = Maxf32(labelSize.y, 34.0f);
    f32 controlX = Maxf32(pos.x + UIGetFloat(UIFloat_ContentStart) - width, pos.x + labelSize.x + UIGetFloat(UIFloat_ButtonSpace));
    float2 boxPos = { controlX, pos.y + (labelSize.y - height) * 0.5f };
    float2 boxSize = { width, height };
    u64 id = UIAutoID(buffer);
    bool hovered = RectPointIntersect(boxPos, boxSize, g_UI.mouse) != 0u;
    bool focused = g_UI.keyboardFocus == id;

    UIPushRoundedRect(boxPos, boxSize, 4.0f, UIGetColor(UIColor_TextBoxBG));
    UIPushBorder(boxPos, boxSize, UIGetFloat(UIFloat_LineThickness), focused ? UIGetColor(UIColor_SelectedBorder) : UIGetColor(UIColor_Border));
    UIPushFloat(UIFloat_TextScale, 0.82f);

    float2 textPos = { boxPos.x + 8.0f, boxPos.y + 2.0f };
    u32 len = UIStringLength(buffer, capacity);
    UITextLayout layout;
    UITextBuildLayout(buffer ? buffer : "", len, textPos, UIGetFloat(UIFloat_TextScale), false, &layout);
    if (GetMousePressed(MouseButton_Left))
    {
        if (hovered)
        {
            UISetKeyboardFocus(id);
            bool shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
            UISetCaret(UITextLayoutHitTest(&layout, g_UI.mouse), shift);
            g_UI.textDragFocus = id;
        }
        else if (g_UI.keyboardFocus == id) UISetKeyboardFocus(0u);
    }
    if (g_UI.textDragFocus == id && GetMouseDown(MouseButton_Left))
    {
        UISetCaret(UITextLayoutHitTest(&layout, g_UI.mouse), true);
    }
    focused = g_UI.keyboardFocus == id;

    bool edited = UITextEditBehavior(id, buffer, capacity, false, &layout);
    len = UIStringLength(buffer, capacity);
    UITextBuildLayout(buffer ? buffer : "", len, textPos, UIGetFloat(UIFloat_TextScale), false, &layout);
    g_UI.wasHovered = hovered;

    if (focused) UITextDrawSelection(&layout);
    UITextDrawLayout(&layout);
    if (focused)
    {
        float2 caretPos = UITextLayoutCaretPos(&layout, g_UI.caret);
        f32 cursorX = Minf32(caretPos.x + 2.0f, boxPos.x + boxSize.x - 4.0f);
        UIPushRect((float2){ cursorX, boxPos.y + 6.0f }, (float2){ 1.5f, boxSize.y - 12.0f }, UIGetColor(UIColor_TextBoxCursor));
    }
    UIPopFloat(UIFloat_TextScale);
    return edited;
}

bool UITextArea(const char* label, float2 pos, char* buffer, u32 capacity, float2 size)
{
    if (label) UIText(label, pos);
    float2 labelSize = UITextSize(label);
    float2 boxPos = { pos.x, pos.y + labelSize.y + 8.0f };
    u64 id = UIAutoID(buffer);
    bool hovered = RectPointIntersect(boxPos, size, g_UI.mouse) != 0u;
    bool focused = g_UI.keyboardFocus == id;

    UIPushRoundedRect(boxPos, size, 6.0f, UIGetColor(UIColor_TextBoxBG));
    UIPushBorder(boxPos, size, UIGetFloat(UIFloat_LineThickness), focused ? UIGetColor(UIColor_SelectedBorder) : UIGetColor(UIColor_Border));
    UIPushFloat(UIFloat_TextScale, 0.78f);

    float2 textPos = { boxPos.x + 10.0f, boxPos.y + 8.0f };
    u32 len = UIStringLength(buffer, capacity);
    UITextLayout layout;
    UITextBuildLayout(buffer ? buffer : "", len, textPos, UIGetFloat(UIFloat_TextScale), true, &layout);
    if (GetMousePressed(MouseButton_Left))
    {
        if (hovered)
        {
            UISetKeyboardFocus(id);
            bool shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
            UISetCaret(UITextLayoutHitTest(&layout, g_UI.mouse), shift);
            g_UI.textDragFocus = id;
        }
        else if (g_UI.keyboardFocus == id) UISetKeyboardFocus(0u);
    }
    if (g_UI.textDragFocus == id && GetMouseDown(MouseButton_Left))
    {
        UISetCaret(UITextLayoutHitTest(&layout, g_UI.mouse), true);
    }
    focused = g_UI.keyboardFocus == id;

    bool edited = UITextEditBehavior(id, buffer, capacity, true, &layout);
    len = UIStringLength(buffer, capacity);
    UITextBuildLayout(buffer ? buffer : "", len, textPos, UIGetFloat(UIFloat_TextScale), true, &layout);
    g_UI.wasHovered = hovered;

    if (focused) UITextDrawSelection(&layout);
    UITextDrawLayout(&layout);
    if (focused)
    {
        float2 caretPos = UITextLayoutCaretPos(&layout, g_UI.caret);
        f32 cursorX = Minf32(caretPos.x + 2.0f, boxPos.x + size.x - 4.0f);
        UIPushRect((float2){ cursorX, caretPos.y }, (float2){ 1.5f, layout.lineHeight }, UIGetColor(UIColor_TextBoxCursor));
    }
    UIPopFloat(UIFloat_TextScale);
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
    static char textBox[128] = "edit me";
    static char textArea[512] = "Text area 中文测试 日本語テスト\nArabic: العربية\nGreek: Ελληνικά";
    UIPushRoundedRect((float2){ 32.0f-4, 32.0f-4 }, (float2){ 768.0f, 508.0f }, 8.0f, UIGetColor(UIColor_Border) & 0xccFFFFFF);
    UIPushRoundedRect((float2){ 32.0f, 32.0f }, (float2){ 760.0f, 500.0f }, 8.0f, 0xCC202028u);
    // UIPushBorder((float2){ 32.0f, 32.0f }, (float2){ 760.0f, 500.0f }, 1.5f, UIGetColor(UIColor_Border));
    UIPushFloat(UIFloat_TextScale, 0.86f);
    UIText("SDF + Slug Immediate UI", (float2){ 56.0f, 56.0f });
    UIPopFloat(UIFloat_TextScale);
    if (UIButton("Button", (float2) { 56.0f, 94.0f }, (float2) { 160.0f, 44.0f }))
        AX_LOG("button clicked");
    UICheckbox("Checkbox", (float2){ 56.0f, 150.0f }, &enabled);
    UISliderFloat("Slider", (float2){ 56.0f, 196.0f }, &slider, 180.0f);
    UITextBox("Text Box", (float2){ 56.0f, 242.0f }, textBox, (u32)sizeof(textBox), 260.0f);
    UITextArea("Text Area", (float2){ 56.0f, 292.0f }, textArea, (u32)sizeof(textArea), (float2){ 520.0f, 160.0f });
}
