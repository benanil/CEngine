#ifndef PLATFORM_H
#define PLATFORM_H

// enables logging no matter what
#define AX_ENABLE_LOGGING

#if defined(AX_ENABLE_LOGGING) || defined(_DEBUG) || defined(DEBUG) || defined(Debug)
    #include <SDL3/SDL_log.h>
    
    #define AX_LOG(format, ...)  SDL_Log("axInfo: %s -line:%i " format, GetFileName(__FILE__), __LINE__, ##__VA_ARGS__)
    #define AX_WARN(format, ...) SDL_LogWarn(SDL_LOG_PRIORITY_WARN, "axWarn: %s -line:%i " format, GetFileName(__FILE__), __LINE__, ##__VA_ARGS__)

    #if !(defined(__GNUC__) || defined(__GNUG__))
    #   define AX_ERROR(format, ...) SDL_LogError(SDL_LOG_PRIORITY_ERROR, "%s -line:%i " format, GetFileName(__FILE__), __LINE__, ##__VA_ARGS__)
    #else                                                             
    #   define AX_ERROR(format, ...) SDL_LogError(SDL_LOG_PRIORITY_ERROR, "%s -line:%i " format, GetFileName(__FILE__), __LINE__,##__VA_ARGS__)
    #endif
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


// Platform context structure
typedef struct PlatformContext_ 
{
    // Mouse state
    f1  MousePosX, MousePosY;
    f1  MouseWheelDelta;
    f1  SecondsSinceLastClick;
    d1  DeltaTime;
    
    i64 LastClickTime;
    i64 CPUFrequency;
    i64 StartupTime;
    i64 LastTime;
    i64 FrameCount;
    
    // Window state
    i32 WindowWidth, WindowHeight;
    i32 WindowPosX, WindowPosY;
    
    i32 MouseDown, MouseLast, MousePressed, MouseReleased;
    u8  DoubleClicked;
    
} PlatformContext;


// Mouse button flags
typedef enum MouseButton_ {
    MouseButton_Left     = 1 << 0,
    MouseButton_Right    = 1 << 1, 
    MouseButton_Middle   = 1 << 2,
    MouseButton_Forward  = 1 << 3,
    MouseButton_Backward = 1 << 4
} MouseButton;

// Mouse
void GetMousePos(f1* x, f1* y);
void SetMousePos(f1 x, f1 y);
void wGetMouseWindowPos(f1* x, f1* y);
void wGetMonitorSize(i32* width, i32* height);
void SetMouseWindowPos(f1 x, f1 y);
f1   GetMouseWheelDelta();
u8   GetDoubleClicked();
u8   AnyMouseKeyDown();
u8   GetMouseDown(i32 button);
u8   GetMouseReleased(i32 button);
u8   GetMousePressed(i32 button);


// Keyboard
u8   AnyKeyDown();
u8   GetKeyDown(i32 c);
u8   GetKeyReleased(i32 c);
u8   GetKeyPressed(i32 c);

void SetPressedAndReleasedKeys();
void RecordLastKeys();

// forward declare, for sdl dialog file callback
typedef void (SDLCALL *CP_DialogFileCallback)(void *userdata, const char * const *filelist, i32 filter);

// Window
void wSetWindowSize(i32 width, i32 height);
void wSetWindowPosition(i32 x, i32 y);
void wOpenFolder(const char* folderPath, CP_DialogFileCallback callback);
void wOpenFile(const char* filePath, CP_DialogFileCallback callback);

// Time
d1   GetDeltaTime();
d1   TimeSinceStartup();

// time is nanoseconds
d1   TimeToSeconds(i64 time);
i64  TimeToMilliseconds(i64 time);
i64  TimeToMicroseconds(i64 time);

void PlatformInit();
void PlatformUpdate();

extern PlatformContext PlatformCtx;

void EventCallback(const SDL_Event* event);

#if defined(__cplusplus)
}
#endif

#endif // PLATFORM_H