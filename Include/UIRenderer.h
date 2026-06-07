#ifndef CP_UI_RENDERER_H
#define CP_UI_RENDERER_H

#include "Graphics.h"
#include "Extern/clay/clay.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define UI_MAX_SHAPES 8192u
#define UI_MAX_IMAGES 1024u
#define UI_MAX_TEXTS  4096u
#define UI_MAX_BATCHES (UI_MAX_IMAGES + UI_MAX_TEXTS)


typedef enum UICustomType_
{
    UICustomType_TextArea = 1
} UICustomType;

typedef enum UITextAreaFlagBits_
{
    UITextAreaFlags_CenterX = 1u << 0,
    UITextAreaFlags_CenterY = 1u << 1
} UITextAreaFlagBits;

typedef enum UIColor_
{
    UIColor_Text = 0,
    UIColor_Quad,
    UIColor_Hovered,
    UIColor_Line,
    UIColor_Border,
    UIColor_CheckboxBG,
    UIColor_TextBoxBG,
    UIColor_SliderInside,
    UIColor_TextBoxCursor,
    UIColor_SelectedBorder,
    UIColor_SubText ,
    UIColor_Count
} UIColor;

typedef enum UIFloat_
{
    UIFloat_BorderWidth = 0,
    UIFloat_ContentStart,
    UIFloat_ButtonSize,
    UIFloat_TextScale,
    UIFloat_TextBoxWidth,
    UIFloat_SliderHeight,
    UIFloat_Depth,
    UIFloat_FieldWidth,
    UIFloat_TextWrapWidth,
    UIFloat_ScrollWidth,
    UIFloat_CornerRadius,
    UIFloat_Count
} UIFloat;

typedef enum UIShapeType_
{
    UIShapeType_Rect = 0,
    UIShapeType_RoundedRect,
    UIShapeType_Circle,
    UIShapeType_Capsule
} UIShapeType;

typedef struct UIShape_
{
    f32 rect[4];   // x, y, width, height in framebuffer pixels.
    f32 params[4]; // radius, border width, softness, unused.
    u32 color;
    u32 borderColor;
    u32 shape;
    u32 flags;
    f32 clip[4];   // x0, y0, x1, y1 in framebuffer pixels.
} UIShape;

typedef struct UIImageData_
{
    SDL_GPUTexture* texture;
    SDL_GPUSampler* sampler;
    f32 uv[4]; // x, y, width, height. Zero width/height means full image.
} UIImageData;

typedef struct UITextAreaCustomData_
{
    u32 type;
    char* buffer;
    u32 capacity;
    u32 flags;
} UITextAreaCustomData;

void UIInit(void);
void UIDestroy(void);
void UIBeginFrame(void);
void UIEndFrame(SDL_GPUCommandBuffer* cmd, SDL_GPUColorTargetInfo* colorTarget);
void UIClear(void);
char* UIFrameStringAlloc(u32 size);
void UIBeginBatch(void);
void UIEndBatch(void);

bool UIPushRect(float2 pos, float2 size, u32 color);
bool UIPushRoundedRect(float2 pos, float2 size, f32 radius, u32 color);
bool UIPushCircle(float2 center, f32 radius, u32 color);
bool UIPushCapsule(float2 pos, float2 size, u32 color);
void UIPushBorder(f32 thickness, u32 color);

void UIPopClipRect(void);
void UIGetClipRect(f32 outClip[4]);

void UISetColor(UIColor what, u32 color);
u32  UIGetColor(UIColor what);
Clay_Color UIGetClayColor(UIColor what);
void UIPushColor(UIColor what, u32 color);
void UIPopColor(UIColor what);
void UISetFloat(UIFloat what, f32 value);
f32  UIGetFloat(UIFloat what);
void UIPushFloat(UIFloat what, f32 value);
void UIPushFloatAdd(UIFloat what, f32 value);
void UIPopFloat(UIFloat what);
bool UIFloatStackZero(UIFloat what);

Clay_Color UIPanelColor(void);

bool   UITextArea(const char* label, float2 pos, char* buffer, u32 capacity, float2 size);
bool   UITextAreaFlags(const char* label, float2 pos, char* buffer, u32 capacity, float2 size, u32 flags);
void   UIRender(SDL_GPUCommandBuffer* cmd, SDL_GPUColorTargetInfo* colorTarget);
UIImageData UIImageFromTexture(Texture* texture);

Clay_RenderCommandArray UIEndLayout(void);
void UIRenderCommands(Clay_RenderCommandArray* commands);
bool UIClicked(void);
bool UIButton(Clay_ElementId id, Clay_String label, Clay_Dimensions size, bool selected);
bool UICheckbox(Clay_ElementId id, Clay_String label, bool* value);
void UIProgressBar(Clay_ElementId id, Clay_String label, f32 value01);
bool UISliderFloat(Clay_ElementId id, Clay_String label, f32* value, f32 minValue, f32 maxValue);
bool UISliderFloatValue(Clay_ElementId id, Clay_String label, f32* value, f32 minValue, f32 maxValue, int decimals);
bool UIEditInt(Clay_ElementId id, Clay_String label, f32* value, s32 minValue, s32 maxValue);
bool UIEditFloat(Clay_ElementId id, Clay_String label, f32* value, f32 minValue, f32 maxValue, f32 step, int decimals);

u64 UIAutoID(const void* ptr);

static inline Clay_Color UIColorToClay(u32 color)
{
    v128u lanes = VeciSrl(VeciSet1(color), VeciSetR(0, 8, 16, 24));
    lanes = VeciAnd(lanes, VeciSet1(0xFFu));
    Clay_Color result;
    VecStore(&result.r, VecI32ToF32(lanes));
    return result;
}

static inline u32 UIPackClayColor(Clay_Color color)
{
    v128f v = VecLoad(&color.r);
    v = VecClamp(v, VecZero(), VecSet1(255.0f));
    v128u i = VecF32ToU32(v);
    i = VeciSll(i, VeciSetR(0, 8, 16, 24));
    i = VeciOr(i, VecSwapHalvesU(i));
    i = VeciOr(i, VecSwapPairsU(i));
    return VeciGetX(i);
}

#if defined(__cplusplus)
}
#endif

#endif
