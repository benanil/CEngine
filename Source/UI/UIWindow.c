#include "UI_Internal.h"
#include "Include/UIWindow.h"
#include "Include/Platform.h"
#include "Include/Random.h"

typedef enum UIWindowState_
{
    UIWindowState_None               = 0,
    UIWindowState_Resize_LeftEdge    = 1 << 0,
    UIWindowState_Resize_RightEdge   = 1 << 1,
    UIWindowState_Resize_TopEdge     = 1 << 2,
    UIWindowState_Resize_BottomEdge  = 1 << 3,
    UIWindowState_Resize_EdgeMask    = 0x0F,
    UIWindowState_Move_TabBar        = 1 << 8
} UIWindowState;

static UIWindow g_UIWindows[UI_MAX_WINDOWS];
static u32 g_UIWindowCount;
static s32 g_UIWindowCurrent = -1;
static s32 g_UIWindowActive = -1;
static u32 g_UIWindowState;
static u32 g_UIWindowSnapMask;
static s32 g_UIWindowDockTarget = -1;
static u32 g_UIWindowDockMask;
static bool g_UIWindowContentOpen;
static bool g_UIWindowCursorRequested;
static bool g_UIWindowCursorOwned;

enum
{
    UIWindowSnap_Left   = 1u << 0,
    UIWindowSnap_Right  = 1u << 1,
    UIWindowSnap_Top    = 1u << 2,
    UIWindowSnap_Bottom = 1u << 3
};

static float2 UIWindowClampScale(UIWindow* window, float2 scale)
{
    scale.x = Maxf32(scale.x, window->minScale.x);
    scale.y = Maxf32(scale.y, window->minScale.y);
    return scale;
}

static void UIWindowSetRect(UIWindow* window, float2 position, float2 scale)
{
    window->position = position;
    window->scale = UIWindowClampScale(window, scale);
}

static float2 UIWindowScale(const UIWindow* window)
{
    float2 result = window->scale;
    if (window->isCollapsed) result.y = window->topHeight;
    return result;
}

static bool UIWindowIsOpen(const UIWindow* window)
{
    return !window->isOpenPtr || *window->isOpenPtr;
}

static s32 UIWindowFind(u32 hash)
{
    for (u32 i = 0u; i < g_UIWindowCount; i++)
        if (g_UIWindows[i].hash == hash)
            return (s32)i;
    return (s32)g_UIWindowCount;
}

static bool UIWindowOnTopOf(s32 targetIdx, float2 pos)
{
    UIWindow* target = &g_UIWindows[targetIdx];
    for (u32 i = 0u; i < g_UIWindowCount; i++)
    {
        UIWindow* window = &g_UIWindows[i];
        if ((s32)i == targetIdx || !UIWindowIsOpen(window)) continue;
        if (window->depth > target->depth && RectPointIntersect(window->position, UIWindowScale(window), pos)) return true;
    }
    return false;
}

static void UIWindowBringToFront(s32 windowIndex)
{
    UIWindow* window = &g_UIWindows[windowIndex];
    u8 oldDepth = window->depth;
    for (u32 i = 0u; i < g_UIWindowCount; i++)
    {
        if (g_UIWindows[i].depth > oldDepth) g_UIWindows[i].depth--;
    }
    window->depth = (u8)(g_UIWindowCount - 1u);
    g_UIWindowActive = windowIndex;
}

static bool UIHitRect(float2 pos, float2 size, float2 point)
{
    return RectPointIntersect(pos, size, point) != 0u;
}

static f32 UIWindowOverlap1D(f32 a0, f32 a1, f32 b0, f32 b1)
{
    return Maxf32(0.0f, Minf32(a1, b1) - Maxf32(a0, b0));
}

static s32 UIWindowFindSharedEdgeWindow(s32 windowIndex, u32 edgeMask)
{
    UIWindow* window = &g_UIWindows[windowIndex];
    float2 pos = window->position;
    float2 size = window->scale;
    f32 x0 = pos.x;
    f32 y0 = pos.y;
    f32 x1 = pos.x + size.x;
    f32 y1 = pos.y + size.y;
    const f32 edgeEpsilon = 2.0f;

    s32 best = -1;
    f32 bestOverlap = 0.0f;
    for (u32 i = 0u; i < g_UIWindowCount; i++)
    {
        UIWindow* other = &g_UIWindows[i];
        if ((s32)i == windowIndex || !UIWindowIsOpen(other) || other->isCollapsed) continue;

        float2 otherScale = other->scale;
        f32 ox0 = other->position.x;
        f32 oy0 = other->position.y;
        f32 ox1 = other->position.x + otherScale.x;
        f32 oy1 = other->position.y + otherScale.y;
        f32 overlap = 0.0f;
        bool shared = false;

        if ((edgeMask & UIWindowState_Resize_RightEdge) != 0u)
        {
            shared = Absf32(x1 - ox0) <= edgeEpsilon;
            overlap = UIWindowOverlap1D(y0, y1, oy0, oy1);
        }
        else if ((edgeMask & UIWindowState_Resize_LeftEdge) != 0u)
        {
            shared = Absf32(x0 - ox1) <= edgeEpsilon;
            overlap = UIWindowOverlap1D(y0, y1, oy0, oy1);
        }
        else if ((edgeMask & UIWindowState_Resize_BottomEdge) != 0u)
        {
            shared = Absf32(y1 - oy0) <= edgeEpsilon;
            overlap = UIWindowOverlap1D(x0, x1, ox0, ox1);
        }
        else if ((edgeMask & UIWindowState_Resize_TopEdge) != 0u)
        {
            shared = Absf32(y0 - oy1) <= edgeEpsilon;
            overlap = UIWindowOverlap1D(x0, x1, ox0, ox1);
        }

        if (shared && overlap > bestOverlap)
        {
            best = (s32)i;
            bestOverlap = overlap;
        }
    }
    return best;
}

static u32 UIWindowDockMaskFromTarget(UIWindow* target, float2 mouse)
{
    float2 pos = target->position;
    float2 size = UIWindowScale(target);
    if (!UIHitRect(pos, size, mouse)) return 0u;

    f32 relX = (mouse.x - pos.x) / Maxf32(size.x, 1.0f);
    f32 relY = (mouse.y - pos.y) / Maxf32(size.y, 1.0f);
    f32 leftDist = relX;
    f32 rightDist = 1.0f - relX;
    f32 topDist = relY;
    f32 bottomDist = 1.0f - relY;
    f32 best = Minf32(Minf32(leftDist, rightDist), Minf32(topDist, bottomDist));

    if (best > 0.30f) return 0u;
    if (best == leftDist) return UIWindowSnap_Left;
    if (best == rightDist) return UIWindowSnap_Right;
    if (best == topDist) return UIWindowSnap_Top;
    return UIWindowSnap_Bottom;
}

static s32 UIWindowFindDockTarget(s32 draggedIndex, float2 mouse, u32* outMask)
{
    s32 bestIndex = -1;
    u8 bestDepth = 0u;
    u32 bestMask = 0u;
    for (u32 i = 0u; i < g_UIWindowCount; i++)
    {
        UIWindow* target = &g_UIWindows[i];
        if ((s32)i == draggedIndex || !UIWindowIsOpen(target) || target->isCollapsed) continue;

        u32 mask = UIWindowDockMaskFromTarget(target, mouse);
        if (mask == 0u) continue;
        if (bestIndex < 0 || target->depth >= bestDepth)
        {
            bestIndex = (s32)i;
            bestDepth = target->depth;
            bestMask = mask;
        }
    }

    if (outMask) *outMask = bestMask;
    return bestIndex;
}

static float2 UIWindowSnapPosition(u32 mask)
{
    float2 screen = g_UI.screenSize;
    if ((mask & UIWindowSnap_Left) != 0u) return (float2){ 0.0f, 0.0f };
    if ((mask & UIWindowSnap_Right) != 0u) return (float2){ screen.x * 0.5f, 0.0f };
    if ((mask & UIWindowSnap_Top) != 0u) return (float2){ 0.0f, 0.0f };
    if ((mask & UIWindowSnap_Bottom) != 0u) return (float2){ 0.0f, screen.y * 0.5f };
    return (float2){ 0.0f, 0.0f };
}

static float2 UIWindowSnapScale(u32 mask)
{
    float2 screen = g_UI.screenSize;
    if ((mask & (UIWindowSnap_Left | UIWindowSnap_Right)) != 0u) return (float2){ screen.x * 0.5f, screen.y };
    if ((mask & UIWindowSnap_Top) != 0u) return screen;
    if ((mask & UIWindowSnap_Bottom) != 0u) return (float2){ screen.x, screen.y * 0.5f };
    return (float2){ 0.0f, 0.0f };
}

static float2 UIWindowDockPosition(UIWindow* target, u32 mask)
{
    float2 pos = target->position;
    float2 size = UIWindowScale(target);
    if ((mask & UIWindowSnap_Right) != 0u) pos.x += size.x * 0.5f;
    if ((mask & UIWindowSnap_Bottom) != 0u) pos.y += size.y * 0.5f;
    return pos;
}

static float2 UIWindowDockScale(UIWindow* target, u32 mask)
{
    float2 size = UIWindowScale(target);
    if ((mask & (UIWindowSnap_Left | UIWindowSnap_Right)) != 0u) size.x *= 0.5f;
    if ((mask & (UIWindowSnap_Top | UIWindowSnap_Bottom)) != 0u) size.y *= 0.5f;
    return size;
}

static void UIWindowSplitTargetRect(UIWindow* target, u32 draggedMask, float2* targetPos, float2* targetScale)
{
    *targetPos = target->position;
    *targetScale = UIWindowScale(target);
    if ((draggedMask & UIWindowSnap_Left) != 0u)
    {
        targetPos->x += targetScale->x * 0.5f;
        targetScale->x *= 0.5f;
    }
    else if ((draggedMask & UIWindowSnap_Right) != 0u)
    {
        targetScale->x *= 0.5f;
    }
    else if ((draggedMask & UIWindowSnap_Top) != 0u)
    {
        targetPos->y += targetScale->y * 0.5f;
        targetScale->y *= 0.5f;
    }
    else if ((draggedMask & UIWindowSnap_Bottom) != 0u)
    {
        targetScale->y *= 0.5f;
    }
}

static bool UIWindowDockCanSplit(UIWindow* dragged, UIWindow* target, u32 mask)
{
    float2 draggedScale = UIWindowDockScale(target, mask);
    float2 targetPos, targetScale;
    UIWindowSplitTargetRect(target, mask, &targetPos, &targetScale);
    (void)targetPos;
    return draggedScale.x >= dragged->minScale.x && draggedScale.y >= dragged->minScale.y && targetScale.x >= target->minScale.x && targetScale.y >= target->minScale.y;
}

static u32 UIWindowSnapMaskFromMouse(float2 mouse)
{
    const f32 zone = 56.0f;
    float2 screen = g_UI.screenSize;
    if (mouse.x <= zone) return UIWindowSnap_Left;
    if (mouse.x >= screen.x - zone) return UIWindowSnap_Right;
    if (mouse.y <= zone) return UIWindowSnap_Top;
    if (mouse.y >= screen.y - zone) return UIWindowSnap_Bottom;
    return 0u;
}

static void UIWindowDrawSnapRect(Clay_ElementId id, float2 pos, float2 size, u32 color, s16 zIndex)
{
    CLAY(id, {
        .layout = { .sizing = { CLAY_SIZING_FIXED(size.x), CLAY_SIZING_FIXED(size.y) } },
        .backgroundColor = UIColorToClay(color),
        .cornerRadius = CLAY_CORNER_RADIUS(6.0f),
        .border = { .color = UIGetClayColor(UIColor_SelectedBorder), .width = CLAY_BORDER_ALL(1) },
        .floating = { .offset = { pos.x, pos.y }, .zIndex = zIndex, .attachTo = CLAY_ATTACH_TO_ROOT }
    }) {}
}

static void UIWindowDrawSnapPreview(UIWindow* window)
{
    if (g_UIWindowState != UIWindowState_Move_TabBar || g_UIWindowActive != g_UIWindowCurrent) return;

    float2 screen = g_UI.screenSize;
    const f32 zone = 56.0f;
    const u32 zoneColor = 0x22E8A400u;
    s16 zIndex = (s16)(window->depth + 96u);

    UIWindowDrawSnapRect(CLAY_ID("UIWindowSnapLeft"), (float2){ 0.0f, 0.0f }, (float2){ zone, screen.y }, zoneColor, zIndex);
    UIWindowDrawSnapRect(CLAY_ID("UIWindowSnapRight"), (float2){ screen.x - zone, 0.0f }, (float2){ zone, screen.y }, zoneColor, zIndex);
    UIWindowDrawSnapRect(CLAY_ID("UIWindowSnapTop"), (float2){ 0.0f, 0.0f }, (float2){ screen.x, zone }, zoneColor, zIndex);
    UIWindowDrawSnapRect(CLAY_ID("UIWindowSnapBottom"), (float2){ 0.0f, screen.y - zone }, (float2){ screen.x, zone }, zoneColor, zIndex);

    g_UIWindowDockTarget = UIWindowFindDockTarget(g_UIWindowCurrent, g_UI.mouse, &g_UIWindowDockMask);
    if (g_UIWindowDockTarget >= 0)
    {
        UIWindow* target = &g_UIWindows[g_UIWindowDockTarget];
        if (UIWindowDockCanSplit(window, target, g_UIWindowDockMask))
        {
            UIWindowDrawSnapRect(CLAY_ID("UIWindowDockTarget"), UIWindowDockPosition(target, g_UIWindowDockMask), UIWindowDockScale(target, g_UIWindowDockMask), 0x55E8A400u, (s16)(zIndex + 2));
            return;
        }
        g_UIWindowDockTarget = -1;
        g_UIWindowDockMask = 0u;
    }

    g_UIWindowSnapMask = UIWindowSnapMaskFromMouse(g_UI.mouse);
    if (g_UIWindowSnapMask != 0u)
    {
        UIWindowDrawSnapRect(CLAY_ID("UIWindowSnapTarget"), UIWindowSnapPosition(g_UIWindowSnapMask), UIWindowSnapScale(g_UIWindowSnapMask), 0x44E8A400u, (s16)(zIndex + 1));
    }
}

static void UIWindowApplyPendingSnap(void)
{
    if (g_UIWindowActive < 0 || (u32)g_UIWindowActive >= g_UIWindowCount || g_UIWindowSnapMask == 0u) return;

    UIWindow* window = &g_UIWindows[g_UIWindowActive];
    if (!UIWindowIsOpen(window)) return;
    UIWindowSetRect(window, UIWindowSnapPosition(g_UIWindowSnapMask), UIWindowSnapScale(g_UIWindowSnapMask));
    g_UIWindowSnapMask = 0u;
}

static void UIWindowApplyPendingDock(void)
{
    if (g_UIWindowActive < 0 || (u32)g_UIWindowActive >= g_UIWindowCount || g_UIWindowDockTarget < 0 || (u32)g_UIWindowDockTarget >= g_UIWindowCount || g_UIWindowDockMask == 0u) return;

    UIWindow* dragged = &g_UIWindows[g_UIWindowActive];
    UIWindow* target = &g_UIWindows[g_UIWindowDockTarget];
    if (!UIWindowIsOpen(dragged) || !UIWindowIsOpen(target)) return;

    float2 draggedPos = UIWindowDockPosition(target, g_UIWindowDockMask);
    float2 draggedScale = UIWindowDockScale(target, g_UIWindowDockMask);
    float2 targetPos, targetScale;
    UIWindowSplitTargetRect(target, g_UIWindowDockMask, &targetPos, &targetScale);

    if (draggedScale.x < dragged->minScale.x || draggedScale.y < dragged->minScale.y || targetScale.x < target->minScale.x || targetScale.y < target->minScale.y) return;

    UIWindowSetRect(dragged, draggedPos, draggedScale);
    UIWindowSetRect(target, targetPos, targetScale);
    g_UIWindowDockTarget = -1;
    g_UIWindowDockMask = 0u;
}

static u32 UIWindowResizeHitMask(UIWindow* window, float2 mouse)
{
    if ((window->flags & UIWindowFlags_NoResize) != 0u || window->isCollapsed) return 0u;

    const f32 edgeDist = 6.0f;
    const f32 cornerDist = 18.0f;
    float2 pos = window->position;
    float2 size = window->scale;

    bool left   = Absf32(mouse.x - pos.x) < edgeDist && mouse.y >= pos.y && mouse.y <= pos.y + size.y;
    bool right  = Absf32(mouse.x - (pos.x + size.x)) < edgeDist && mouse.y >= pos.y && mouse.y <= pos.y + size.y;
    bool top    = Absf32(mouse.y - pos.y) < edgeDist && mouse.x >= pos.x && mouse.x <= pos.x + size.x;
    bool bottom = Absf32(mouse.y - (pos.y + size.y)) < edgeDist && mouse.x >= pos.x && mouse.x <= pos.x + size.x;

    bool nearLeft   = mouse.x >= pos.x && mouse.x <= pos.x + cornerDist;
    bool nearRight  = mouse.x <= pos.x + size.x && mouse.x >= pos.x + size.x - cornerDist;
    bool nearTop    = mouse.y >= pos.y && mouse.y <= pos.y + cornerDist;
    bool nearBottom = mouse.y <= pos.y + size.y && mouse.y >= pos.y + size.y - cornerDist;

    u32 mask = 0u;
    if ((left && nearTop) || (top && nearLeft)) mask = UIWindowState_Resize_LeftEdge | UIWindowState_Resize_TopEdge;
    else if ((right && nearTop) || (top && nearRight)) mask = UIWindowState_Resize_RightEdge | UIWindowState_Resize_TopEdge;
    else if ((left && nearBottom) || (bottom && nearLeft)) mask = UIWindowState_Resize_LeftEdge | UIWindowState_Resize_BottomEdge;
    else if ((right && nearBottom) || (bottom && nearRight)) mask = UIWindowState_Resize_RightEdge | UIWindowState_Resize_BottomEdge;
    else
    {
        if (left)   mask |= UIWindowState_Resize_LeftEdge;
        if (right)  mask |= UIWindowState_Resize_RightEdge;
        if (top)    mask |= UIWindowState_Resize_TopEdge;
        if (bottom) mask |= UIWindowState_Resize_BottomEdge;
    }
    return mask;
}

static wCursor UIWindowCursorFromResizeMask(u32 mask)
{
    bool left = (mask & UIWindowState_Resize_LeftEdge) != 0u;
    bool right = (mask & UIWindowState_Resize_RightEdge) != 0u;
    bool top = (mask & UIWindowState_Resize_TopEdge) != 0u;
    bool bottom = (mask & UIWindowState_Resize_BottomEdge) != 0u;
    if ((left && top) || (right && bottom)) return wCursor_ResizeNWSE;
    if ((right && top) || (left && bottom)) return wCursor_ResizeNESW;
    if (left || right) return wCursor_ResizeEW;
    if (top || bottom) return wCursor_ResizeNS;
    return wCursor_Default;
}

static void UIWindowDrawResizeHighlight(UIWindow* window, u32 mask)
{
    if (mask == 0u) return;

    const f32 thickness = 4.0f;
    const f32 cornerSize = 18.0f;
    const u32 color = UIGetColor(UIColor_SelectedBorder);
    float2 pos = window->position;
    float2 size = window->scale;
    float2 highlightPos = pos;
    float2 highlightSize = { thickness, thickness };
    bool left = (mask & UIWindowState_Resize_LeftEdge) != 0u;
    bool right = (mask & UIWindowState_Resize_RightEdge) != 0u;
    bool top = (mask & UIWindowState_Resize_TopEdge) != 0u;
    bool bottom = (mask & UIWindowState_Resize_BottomEdge) != 0u;

    if ((left || right) && (top || bottom))
    {
        highlightPos.x = right ? pos.x + size.x - cornerSize : pos.x;
        highlightPos.y = bottom ? pos.y + size.y - cornerSize : pos.y;
        highlightSize = F2Set1(cornerSize);
    }
    else if (left || right)
    {
        highlightPos.x = right ? pos.x + size.x - thickness : pos.x;
        highlightSize = (float2){ thickness, size.y };
    }
    else
    {
        highlightPos.y = bottom ? pos.y + size.y - thickness : pos.y;
        highlightSize = (float2){ size.x, thickness };
    }

    Clay_ElementId id = Clay_GetElementIdWithIndex(CLAY_STRING("UIWindowResizeHighlight"), window->hash ^ mask);
    CLAY(id, {
        .layout = { .sizing = { CLAY_SIZING_FIXED(highlightSize.x), CLAY_SIZING_FIXED(highlightSize.y) } },
        .backgroundColor = UIColorToClay(color),
        .cornerRadius = CLAY_CORNER_RADIUS(2.0f),
        .floating = { .offset = { highlightPos.x, highlightPos.y }, .zIndex = (int16_t)(window->depth + 64u), .attachTo = CLAY_ATTACH_TO_ROOT }
    }) {}
}

static void UIWindowHandleMove(UIWindow* window, s32 windowIndex, float2 titlePos, float2 titleSize, float2 mouse, bool mouseDown)
{
    if ((window->flags & UIWindowFlags_NoMove) != 0u) return;
    if (g_UIWindowState != UIWindowState_None && g_UIWindowState != UIWindowState_Move_TabBar) return;
    if (g_UIWindowActive != windowIndex) return;

    bool titleHovered = UIHitRect(titlePos, titleSize, mouse);
    if ((titleHovered || g_UIWindowState == UIWindowState_Move_TabBar) && mouseDown)
    {
        window->position = F2Add(window->position, F2Sub(g_UI.mouse, g_UI.mouseOld));
        window->position.x = Maxf32(window->position.x, 0.0f);
        window->position.y = Maxf32(window->position.y, 0.0f);
        g_UIWindowState = UIWindowState_Move_TabBar;
    }
}

static u32 UIWindowHandleResize(UIWindow* window, s32 windowIndex, float2 mouse, bool mouseDown)
{
    if ((window->flags & UIWindowFlags_NoResize) != 0u || window->isCollapsed) return 0u;
    if (g_UIWindowState == UIWindowState_Move_TabBar) return 0u;
    if (g_UIWindowActive != windowIndex) return 0u;

    float2 pos = window->position;
    float2 size = window->scale;
    u32 hoverMask = UIWindowResizeHitMask(window, mouse);

    if (hoverMask != 0u || (g_UIWindowState & UIWindowState_Resize_EdgeMask) != 0u)
    {
        wSetCursor(UIWindowCursorFromResizeMask((g_UIWindowState & UIWindowState_Resize_EdgeMask) ? g_UIWindowState : hoverMask));
        g_UIWindowCursorRequested = true;
        g_UIWindowCursorOwned = true;
    }

    if (mouseDown)
    {
        if (g_UIWindowState == UIWindowState_None)
        {
            g_UIWindowState = hoverMask;
        }

        u32 resizeMask = g_UIWindowState & UIWindowState_Resize_EdgeMask;
        s32 horizontalNeighbor = -1;
        s32 verticalNeighbor = -1;
        if ((resizeMask & UIWindowState_Resize_RightEdge) != 0u) horizontalNeighbor = UIWindowFindSharedEdgeWindow(windowIndex, UIWindowState_Resize_RightEdge);
        else if ((resizeMask & UIWindowState_Resize_LeftEdge) != 0u) horizontalNeighbor = UIWindowFindSharedEdgeWindow(windowIndex, UIWindowState_Resize_LeftEdge);
        if ((resizeMask & UIWindowState_Resize_BottomEdge) != 0u) verticalNeighbor = UIWindowFindSharedEdgeWindow(windowIndex, UIWindowState_Resize_BottomEdge);
        else if ((resizeMask & UIWindowState_Resize_TopEdge) != 0u) verticalNeighbor = UIWindowFindSharedEdgeWindow(windowIndex, UIWindowState_Resize_TopEdge);

        if ((g_UIWindowState & UIWindowState_Resize_LeftEdge) != 0u)
        {
            f32 right = pos.x + size.x;
            if (horizontalNeighbor >= 0)
            {
                UIWindow* other = &g_UIWindows[horizontalNeighbor];
                f32 otherLeft = other->position.x;
                f32 boundary = Clampf32(mouse.x, otherLeft + other->minScale.x, right - window->minScale.x);
                other->scale.x = boundary - otherLeft;
                pos.x = boundary;
                size.x = right - boundary;
            }
            else
            {
                f32 newX = Minf32(mouse.x, right - window->minScale.x);
                size.x += pos.x - newX;
                pos.x = newX;
            }
        }
        if ((g_UIWindowState & UIWindowState_Resize_RightEdge) != 0u)
        {
            if (horizontalNeighbor >= 0)
            {
                UIWindow* other = &g_UIWindows[horizontalNeighbor];
                f32 otherRight = other->position.x + other->scale.x;
                f32 boundary = Clampf32(mouse.x, pos.x + window->minScale.x, otherRight - other->minScale.x);
                size.x = boundary - pos.x;
                other->position.x = boundary;
                other->scale.x = otherRight - boundary;
            }
            else size.x = Maxf32(mouse.x - pos.x, window->minScale.x);
        }
        if ((g_UIWindowState & UIWindowState_Resize_TopEdge) != 0u)
        {
            f32 bottom = pos.y + size.y;
            if (verticalNeighbor >= 0)
            {
                UIWindow* other = &g_UIWindows[verticalNeighbor];
                f32 otherTop = other->position.y;
                f32 boundary = Clampf32(mouse.y, otherTop + other->minScale.y, bottom - window->minScale.y);
                other->scale.y = boundary - otherTop;
                pos.y = boundary;
                size.y = bottom - boundary;
            }
            else
            {
                f32 newY = Minf32(mouse.y, bottom - window->minScale.y);
                size.y += pos.y - newY;
                pos.y = newY;
            }
        }
        if ((g_UIWindowState & UIWindowState_Resize_BottomEdge) != 0u)
        {
            if (verticalNeighbor >= 0)
            {
                UIWindow* other = &g_UIWindows[verticalNeighbor];
                f32 otherBottom = other->position.y + other->scale.y;
                f32 boundary = Clampf32(mouse.y, pos.y + window->minScale.y, otherBottom - other->minScale.y);
                size.y = boundary - pos.y;
                other->position.y = boundary;
                other->scale.y = otherBottom - boundary;
            }
            else size.y = Maxf32(mouse.y - pos.y, window->minScale.y);
        }
    }

    UIWindowSetRect(window, pos, size);
    return (g_UIWindowState & UIWindowState_Resize_EdgeMask) ? g_UIWindowState : hoverMask;
}

void UIWindowBeginFrame(void)
{
    g_UIWindowCurrent = -1;
    g_UIWindowContentOpen = false;
    g_UIWindowCursorRequested = false;
    if (!GetMouseDown(MouseButton_Left))
    {
        if (g_UIWindowState == UIWindowState_Move_TabBar)
        {
            if (g_UIWindowDockTarget >= 0) UIWindowApplyPendingDock();
            else UIWindowApplyPendingSnap();
        }
        g_UIWindowState = UIWindowState_None;
        g_UIWindowSnapMask = 0u;
        g_UIWindowDockTarget = -1;
        g_UIWindowDockMask = 0u;
    }
}

void UIWindowEndFrame(void)
{
    if (g_UIWindowCursorOwned && !g_UIWindowCursorRequested && !GetMouseDown(MouseButton_Right))
    {
        wSetCursor(wCursor_Default);
        g_UIWindowCursorOwned = false;
    }
}

UIWindow* UIGetWindow(Clay_ElementId id)
{
    s32 index = UIWindowFind(id.id);
    return ((u32)index < g_UIWindowCount) ? &g_UIWindows[index] : NULL;
}

bool UIAnyWindowHovered(void)
{
    for (u32 i = 0u; i < g_UIWindowCount; i++)
    {
        UIWindow* window = &g_UIWindows[i];
        if (UIWindowIsOpen(window) && UIHitRect(window->position, UIWindowScale(window), g_UI.mouse)) return true;
    }
    return false;
}

bool UIBeginWindow(const char* title, float2 position, float2 scale, bool* open, u32 flags)
{
    u32 hash = StringToHash((const u8*)title, 5381u);
    return UIBeginWindowId((Clay_ElementId){ .id = hash }, title, position, scale, open, flags);
}

bool UIBeginWindowId(Clay_ElementId id, const char* title, float2 position, float2 scale, bool* open, u32 flags)
{
    if (open && !*open) return false;
    if (!title) title = "Window";

    s32 windowIndex = UIWindowFind(id.id);
    if ((u32)windowIndex >= UI_MAX_WINDOWS)
    {
        AX_WARN("UI window limit reached: %u", UI_MAX_WINDOWS);
        return false;
    }

    if ((u32)windowIndex == g_UIWindowCount)
    {
        UIWindow* newWindow = &g_UIWindows[g_UIWindowCount++];
        MemsetZero(newWindow, sizeof(*newWindow));
        newWindow->position = position;
        newWindow->scale = scale;
        newWindow->hash = id.id;
        newWindow->depth = (u8)(g_UIWindowCount - 1u);
        newWindow->selectedElement = -1;
        newWindow->started = true;
        if (g_UIWindowActive < 0) g_UIWindowActive = windowIndex;
    }

    g_UIWindowCurrent = windowIndex;
    UIWindow* window = &g_UIWindows[windowIndex];
    window->isOpenPtr = open;
    window->flags = flags;
    window->currElement = 0;
    window->numElements = 0;
    window->elementPos = window->position;
    window->topHeight = ((flags & UIWindowFlags_NoTabBar) != 0u) ? 0.0f : 34.0f;
    float2 titleSize = SlugCalcTextSizeN(NULL, title, (u32)StringLength(title), 16.0f);
    window->minScale.x = Maxf32(360.0f, titleSize.x + 96.0f);
    window->minScale.y = Maxf32(140.0f, window->topHeight + 72.0f);
    window->scale = UIWindowClampScale(window, window->scale);

    float2 visibleScale = UIWindowScale(window);
    bool mousePressed = GetMousePressed(MouseButton_Left) != 0u;
    bool mouseDown = GetMouseDown(MouseButton_Left) != 0u;
    bool hovered = UIHitRect(window->position, visibleScale, g_UI.mouse);
    bool anyOnTop = hovered && UIWindowOnTopOf(windowIndex, g_UI.mouse);
    window->isFocused = !anyOnTop && g_UIWindowActive == windowIndex;

    if (hovered && !anyOnTop && mousePressed) UIWindowBringToFront(windowIndex);

    f32 titlePad = 10.0f;
    f32 buttonSize = 18.0f;
    float2 closePos = { window->position.x + window->scale.x - titlePad - buttonSize, window->position.y + (window->topHeight - buttonSize) * 0.5f };
    float2 collapsePos = { closePos.x - buttonSize - 8.0f, closePos.y };
    bool closeHovered = window->topHeight > 0.0f && UIHitRect(closePos, F2Set1(buttonSize), g_UI.mouse);
    bool collapseHovered = window->topHeight > 0.0f && UIHitRect(collapsePos, F2Set1(buttonSize), g_UI.mouse);

    if (!anyOnTop && mousePressed && closeHovered && open) *open = false;
    if (!anyOnTop && mousePressed && collapseHovered) window->isCollapsed = !window->isCollapsed;

    u32 resizeHighlightMask = 0u;
    if (!anyOnTop)
    {
        float2 titlePos = window->position;
        float2 titleSize = { Maxf32(window->scale.x - buttonSize * 2.0f - titlePad * 3.0f, 1.0f), window->topHeight };
        resizeHighlightMask = UIWindowHandleResize(window, windowIndex, g_UI.mouse, mouseDown);
        if (!closeHovered && !collapseHovered) UIWindowHandleMove(window, windowIndex, titlePos, titleSize, g_UI.mouse, mouseDown);
    }

    if (open && !*open) return false;

    visibleScale = UIWindowScale(window);

    u16 borderWidth = (u16)Maxf32(UIGetFloat(UIFloat_BorderWidth), 1.0f);
    Clay_ElementDeclaration outer = {
        .layout = {
            .sizing = { CLAY_SIZING_FIXED(window->scale.x), CLAY_SIZING_FIXED(visibleScale.y) },
            .layoutDirection = CLAY_TOP_TO_BOTTOM
        },
        .backgroundColor = UIColorToClay(UIGetColor(UIColor_Quad) & 0xF8FFFFFFu),
        .cornerRadius = CLAY_CORNER_RADIUS(UIGetFloat(UIFloat_CornerRadius)),
        .border = { .color = UIGetClayColor(UIColor_Border), .width = CLAY_BORDER_ALL(borderWidth) },
        .floating = { .offset = { window->position.x, window->position.y }, .zIndex = (s16)window->depth, .attachTo = CLAY_ATTACH_TO_ROOT }
    };
    Clay__OpenElementWithId(id);
    Clay__ConfigureOpenElement(outer);
    UIWindowDrawResizeHighlight(window, resizeHighlightMask);
    UIWindowDrawSnapPreview(window);

    if (window->topHeight > 0.0f)
    {
        CLAY(CLAY_ID_LOCAL("TitleBar"), {
            .layout = {
                .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(window->topHeight) },
                .padding = { 12, 8, 0, 0 },
                .childGap = 8,
                .layoutDirection = CLAY_LEFT_TO_RIGHT,
                .childAlignment = { .y = CLAY_ALIGN_Y_CENTER }
            },
            .backgroundColor = UIColorToClay(UIGetColor(UIColor_TextBoxBG) & 0xF0FFFFFFu)
        }) {
            Clay_String titleString = { .isStaticallyAllocated = false, .length = (s32)StringLength(title), .chars = title };
            CLAY_TEXT(titleString, CLAY_TEXT_CONFIG({ .fontSize = 16, .textColor = UIGetClayColor(UIColor_Text) }));
            CLAY(CLAY_ID_LOCAL("TitleSpacer"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1.0f) } } }) {}
            CLAY(CLAY_ID_LOCAL("Collapse"), {
                .layout = { .sizing = { CLAY_SIZING_FIXED(buttonSize), CLAY_SIZING_FIXED(buttonSize) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
                .backgroundColor = UIColorToClay(collapseHovered ? UIGetColor(UIColor_Hovered) : UIGetColor(UIColor_CheckboxBG)),
                .cornerRadius = CLAY_CORNER_RADIUS(3.0f)
            }) {
                CLAY_TEXT(window->isCollapsed ? CLAY_STRING("+") : CLAY_STRING("-"), CLAY_TEXT_CONFIG({ .fontSize = 16, .textColor = UIGetClayColor(UIColor_Text) }));
            }
            if (open)
            {
                CLAY(CLAY_ID_LOCAL("Close"), {
                    .layout = { .sizing = { CLAY_SIZING_FIXED(buttonSize), CLAY_SIZING_FIXED(buttonSize) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
                    .backgroundColor = UIColorToClay(closeHovered ? 0xFF3030FFu : UIGetColor(UIColor_CheckboxBG)),
                    .cornerRadius = CLAY_CORNER_RADIUS(3.0f)
                }) {
                    CLAY_TEXT(CLAY_STRING("x"), CLAY_TEXT_CONFIG({ .fontSize = 14, .textColor = UIGetClayColor(UIColor_Text) }));
                }
            }
        }
    }

    if (window->isCollapsed)
    {
        Clay__CloseElement();
        return false;
    }

    Clay_ElementId contentId = Clay_GetElementIdWithIndex(CLAY_STRING("UIWindowContent"), id.id);
    Clay__OpenElementWithId(contentId);
    Clay__ConfigureOpenElement((Clay_ElementDeclaration){
        .layout = {
            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
            .padding = { 14, 14, 12, 12 },
            .childGap = 12,
            .layoutDirection = CLAY_TOP_TO_BOTTOM
        },
        .clip = { .vertical = true, .childOffset = Clay_GetScrollOffset() }
    });
    g_UIWindowContentOpen = true;
    return true;
}

void UIEndWindow(void)
{
    if (!g_UIWindowContentOpen)
    {
        AX_WARN("UIEndWindow called without an open window content");
        return;
    }
    Clay__CloseElement();
    Clay__CloseElement();
    g_UIWindowContentOpen = false;
    g_UIWindowCurrent = -1;
}
