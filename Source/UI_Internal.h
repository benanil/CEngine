#ifndef CP_UI_INTERNAL
#define CP_UI_INTERNAL

#include "Include/Graphics.h"
#include "Include/String.h"
#include "Extern/kb/kb_text_shape.h"

#define UI_STACK_SIZE           6u
#define UI_MAX_SHAPE_FONTS      (1u + SLUG_MAX_FALLBACK_FONTS)
#define UI_TEXT_MAX_CODEPOINTS  SLUG_MAX_TEXT
#define UI_TEXT_MAX_COMMANDS    SLUG_MAX_TEXT
#define UI_TEXT_MAX_LINES       128u

typedef struct UIRenderer_
{
    UIShape* shapes;
    struct UIImageCommand_* images;
    u32 count;
    u32 imageCount;
    u32 capacity;
    u32 imageCapacity;
} UIRenderer;

typedef struct UIImageCommand_
{
    SDL_GPUTexture* texture;
    SDL_GPUSampler* sampler;
    f32 rect[4];
    f32 uv[4];
    f32 clip[4];
    u32 tintColor;
    u32 shapeFence;
    f32 radius;
} UIImageCommand;

typedef struct UIImageParams_
{
    f32 screenScale[4];
    f32 rect[4];
    f32 uv[4];
    f32 clip[4];
    u32 tintColor;
    f32 radius;
    u32 padding[2];
} UIImageParams;

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
    f32 clipRect[4];
    f32 clipStack[UI_STACK_SIZE][4];
    s32 colorStackCount[UIColor_Count];
    s32 floatStackCount[UIFloat_Count];
    s32 clipStackCount;
    kbts_shape_context* textShapeContext;
    kbts_font* textShapeFonts[UI_MAX_SHAPE_FONTS];
    u32 textShapeFontCount;
} UIContext;

typedef struct UIParams_
{
    f32 screenScale[4];
} UIParams;

typedef struct UILayoutContext_
{
    void* memory;
    u64 memorySize;
    bool initialized;
    bool warnedImage;
    bool warnedCustom;
    bool warnedOverlay;
} UILayoutContext;


extern UIRenderer g_UIRenderer;
extern UIContext g_UI;
extern UILayoutContext g_UILayout;

void UIDestroyTextShape(void);

#endif // ui internal