#ifndef CP_UI_WINDOW_H
#define CP_UI_WINDOW_H

#include "UIRenderer.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define UI_MAX_WINDOWS 32u

typedef enum UIWindowFlags_
{
    UIWindowFlags_NoMove            = 1u << 0,
    UIWindowFlags_NoResize          = 1u << 1,
    UIWindowFlags_FixedElementStart = 1u << 2,
    UIWindowFlags_NoTabBar          = 1u << 3,
    UIWindowFlags_RightClickable    = 1u << 4
} UIWindowFlags;

typedef struct UIWindow_
{
    float2 position;
    float2 scale;
    float2 minScale;
    float2 elementPos;
    bool* isOpenPtr;

    f32 elementOffsetY;
    f32 lastElementsTotalHeight;
    f32 elementsTotalHeight;
    f32 scrollPercent;
    f32 topHeight;

    u32 flags;
    u32 hash;
    s32 currElement;
    s32 selectedElement;
    s32 numElements;

    u8 depth;
    bool started;
    bool isCollapsed;
    bool isFocused;
} UIWindow;

void UIWindowBeginFrame(void);
void UIWindowEndFrame(void);
bool UIBeginWindowId(Clay_ElementId id, const char* title, float2 position, float2 scale, bool* open, u32 flags);
bool UIBeginWindow(const char* title, float2 position, float2 scale, bool* open, u32 flags);
void UIEndWindow(void);
bool UIAnyWindowHovered(void);
UIWindow* UIGetWindow(Clay_ElementId id);

// pixel height left for elementId inside the window content, measured from last
// frame's layout. reserveBelow keeps space for elements drawn after it.
// out: small fallback before the element's first layout
f32 UIWindowRemainingHeight(Clay_ElementId windowId, Clay_ElementId elementId, f32 reserveBelow);

#if defined(__cplusplus)
}
#endif

#endif
