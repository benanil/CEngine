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
    u32 tabGroup; // windows sharing a group id occupy the same rect as switchable tabs, 0 = ungrouped
    s32 currElement;
    s32 selectedElement;
    s32 numElements;

    u8 depth;
    bool started;
    bool isCollapsed;
    bool isFocused;
    bool tabActive; // the group member currently shown
    char tabTitle[24];
} UIWindow;

void UIWindowBeginFrame(void);
void UIWindowEndFrame(void);

// reserves screen space at the top (e.g. the editor tab bar). snapping, docking,
// dragging and the resize remap all keep windows inside the remaining work area.
// persists across frames, call once or whenever the reserved height changes
void UIWindowSetTopInset(f32 inset);

// window layout persistence. the file is line separated text, one "window <hash>" line
// per window followed by tab indented properties (rect, depth, collapsed, open, tabgroup,
// tabactive). loaded placements are applied when their window is next built, including its open state
bool UIWindowSaveLayout(const char* path);
bool UIWindowLoadLayout(const char* path);

// the changed flag is set when a window is moved, resized, docked, snapped, collapsed,
// closed or remapped after an application window resize. consume it to know when to save
void UIWindowMarkLayoutChanged(void);
bool UIWindowConsumeLayoutChanged(void);
bool UIBeginWindowId(Clay_ElementId id, const char* title, float2 position, float2 scale, bool* open, u32 flags);
bool UIBeginWindow(const char* title, float2 position, float2 scale, bool* open, u32 flags);
void UIEndWindow(void);
bool UIAnyWindowHovered(void);
UIWindow* UIGetWindow(Clay_ElementId id);

// true when point hits this window and no other window covers it there
bool UIWindowPointVisible(Clay_ElementId id, float2 point);

// pixel height left for elementId inside the window content, measured from last
// frame's layout. reserveBelow keeps space for elements drawn after it.
// out: small fallback before the element's first layout
f32 UIWindowRemainingHeight(Clay_ElementId windowId, Clay_ElementId elementId, f32 reserveBelow);

typedef void (*UIRightClickEventFn)(void* data);

// registers a context menu entry for the window currently being built. call every
// frame between UIBeginWindow and UIEndWindow, the menu opens on right click when
// the window was created with UIWindowFlags_RightClickable. label must outlive the frame
void UIRightClickAddEvent(const char* label, UIRightClickEventFn fn, void* data);

#if defined(__cplusplus)
}
#endif

#endif
