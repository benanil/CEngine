#ifndef CP_UI_RENDERER_H
#define CP_UI_RENDERER_H

#include "Graphics.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define UI_MAX_SHAPES 8192u

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
    UIColor_Count
} UIColor;

typedef enum UIFloat_
{
    UIFloat_LineThickness = 0,
    UIFloat_ContentStart,
    UIFloat_ButtonSpace,
    UIFloat_TextScale,
    UIFloat_TextBoxWidth,
    UIFloat_SliderHeight,
    UIFloat_Depth,
    UIFloat_FieldWidth,
    UIFloat_TextWrapWidth,
    UIFloat_ScrollWidth,
    UIFloat_Count
} UIFloat;

typedef enum UIClickOpt_
{
    UIClickOpt_None = 0,
    UIClickOpt_BigCollision = 1 << 0,
    UIClickOpt_WhileMouseDown = 1 << 1
} UIClickOpt;

typedef enum UIShapeType_
{
    UIShapeType_Rect = 0,
    UIShapeType_RoundedRect,
    UIShapeType_Circle,
    UIShapeType_Capsule
} UIShapeType;

typedef struct UIShape_
{
    f32 rect[4];   // x, y, width, height in logical pixels.
    f32 params[4]; // radius, border width, softness, unused.
    u32 color;
    u32 borderColor;
    u32 shape;
    u32 flags;
} UIShape;

void UIInit(void);
void UIDestroy(void);
void UIBeginFrame(void);
void UIEndFrame(SDL_GPUCommandBuffer* cmd, SDL_GPUColorTargetInfo* colorTarget);
void UIClear(void);
bool UIPushRect(float2 pos, float2 size, u32 color);
bool UIPushRoundedRect(float2 pos, float2 size, f32 radius, u32 color);
bool UIPushCircle(float2 center, f32 radius, u32 color);
bool UIPushCapsule(float2 pos, float2 size, u32 color);
bool UIPushBorder(float2 pos, float2 size, f32 thickness, u32 color);
void UISetColor(UIColor what, u32 color);
u32 UIGetColor(UIColor what);
void UIPushColor(UIColor what, u32 color);
void UIPopColor(UIColor what);
void UISetFloat(UIFloat what, f32 value);
f32 UIGetFloat(UIFloat what);
void UIPushFloat(UIFloat what, f32 value);
void UIPushFloatAdd(UIFloat what, f32 value);
void UIPopFloat(UIFloat what);
bool UIClickCheck(float2 pos, float2 size, UIClickOpt flags);
bool UIIsHovered(void);
float2 UITextSize(const char* text);
void UIText(const char* text, float2 pos);
bool UIButton(const char* text, float2 pos, float2 size);
bool UICheckbox(const char* text, float2 pos, bool* enabled);
bool UISliderFloat(const char* label, float2 pos, f32* value, f32 width);
void UIRender(SDL_GPUCommandBuffer* cmd, SDL_GPUColorTargetInfo* colorTarget);
void UIRenderDemo(void);

#if defined(__cplusplus)
}
#endif

#endif
