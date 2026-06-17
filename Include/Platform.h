#ifndef PLATFORM_H
#define PLATFORM_H

// enables logging no matter what
#define AX_ENABLE_LOGGING

#if defined(AX_ENABLE_LOGGING) || defined(_DEBUG) || defined(DEBUG) || defined(Debug)
    #include <SDL3/SDL_log.h>
    #include <SDL3/SDL_messagebox.h>
    #include <SDL3/SDL_stdinc.h>

    #define AX_ANSI_RESET  "\033[0m"
    #define AX_ANSI_GREEN  "\033[32m"
    #define AX_ANSI_YELLOW "\033[33m"
    #define AX_ANSI_RED    "\033[31m"

    #define AX_LOG(format, ...)  SDL_Log(AX_ANSI_GREEN  "axInfo: %s -line:%i " format AX_ANSI_RESET, GetFileName(__FILE__), __LINE__, ##__VA_ARGS__)
    #define AX_WARN(format, ...) SDL_LogWarn(SDL_LOG_PRIORITY_WARN,  AX_ANSI_YELLOW "%s -line:%i " format AX_ANSI_RESET, GetFileName(__FILE__), __LINE__, ##__VA_ARGS__)
    #define AX_ERROR(format, ...) do { \
        char ax_error_message[2048]; \
        SDL_snprintf(ax_error_message, sizeof(ax_error_message), "%s -line:%i " format, GetFileName(__FILE__), __LINE__, ##__VA_ARGS__); \
        SDL_LogError(SDL_LOG_PRIORITY_ERROR, AX_ANSI_RED "%s" AX_ANSI_RESET, ax_error_message); \
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "AX_ERROR", ax_error_message, NULL); \
    } while (0)
#else
    #define AX_ERROR(format, ...)
    #define AX_LOG(format, ...)
    #define AX_WARN(format, ...)
#endif


#include "Bitset.h"
#include <SDL3/SDL_events.h>
// #include "sokol_app.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct PlatformTextKeyEvent_
{
    s32 key;
    u16 mod;
} PlatformTextKeyEvent;

// Platform context structure
typedef struct PlatformContext_ 
{
    // Mouse state
    f32  MousePosX, MousePosY;
    f32  MouseWheelDelta;
    f32  SecondsSinceLastClick;
    f32  DeltaTime;
    
    s64 LastClickTime;
    s64 CPUFrequency;
    s64 StartupTime;
    s64 LastTime;
    s64 FrameCount;
    
    // Window state
    s32 WindowWidth, WindowHeight;
    s32 WindowPosX, WindowPosY;
    
    s32 MouseDown, MouseLast, MousePressed, MouseReleased;
    char TextInput[256];
    u32  TextInputLength;
    PlatformTextKeyEvent TextKeyEvents[64];
    u32  TextKeyEventCount;
    u8  DoubleClicked;
    
} PlatformContext;


// Mouse button flags
typedef enum MouseButton_ {
    MouseButton_Left     = 1 << 0,
    MouseButton_Middle   = 1 << 1, 
    MouseButton_Right    = 1 << 2,
    MouseButton_Backward = 1 << 3,
    MouseButton_Forward  = 1 << 4
} MouseButton;

typedef enum wCursor_
{
    wCursor_Default = 0,
    wCursor_ResizeEW,
    wCursor_ResizeNS,
    wCursor_ResizeNWSE,
    wCursor_ResizeNESW,
    wCursor_Move,
    wCursor_Count
} wCursor;

// Mouse
void GetMousePos(f32* x, f32* y);
void SetMousePos(f32 x, f32 y);
void SetMouseWindowPos(f32 x, f32 y);
f32  GetMouseWheelDelta();
u8   GetDoubleClicked();
u8   AnyMouseKeyDown();
u8   GetMouseDown(s32 button);
u8   GetMouseReleased(s32 button);
u8   GetMousePressed(s32 button);

void wGetMouseWindowPos(f32* x, f32* y);
void wGetMonitorSize(s32* width, s32* height);
void wSetCursor(wCursor cursor);


// Keyboard
u8   AnyKeyDown();
u8   GetKeyDown(s32 c);
u8   GetKeyReleased(s32 c);
u8   GetKeyPressed(s32 c);
u32  PlatformConsumeTextInput(char* dst, u32 capacity);
u32  PlatformConsumeTextKeyEvents(PlatformTextKeyEvent* dst, u32 capacity);

void SetPressedAndReleasedKeys();
void RecordLastKeys();

// forward declare, for sdl dialog file callback
typedef void (SDLCALL *CP_DialogFileCallback)(void *userdata, const char * const *filelist, s32 filter);

// Window
void wSetWindowSize(s32 width, s32 height);
void wSetWindowPosition(s32 x, s32 y);
void wApplyWindowShape(void);
void wOpenFolder(const char* folderPath, CP_DialogFileCallback callback);
void wOpenFile(const char* filePath, CP_DialogFileCallback callback);

// Time
f32   GetDeltaTime();
f32   TimeSinceStartup();

// time is nanoseconds
f32   TimeToSeconds(s64 time);
s64  TimeToMilliseconds(s64 time);
s64  TimeToMicroseconds(s64 time);

void PlatformInit();
void PlatformUpdate();

extern PlatformContext PlatformCtx;

void EventCallback(const SDL_Event* event);

#if defined(__cplusplus)
}
#endif

#endif // PLATFORM_H
