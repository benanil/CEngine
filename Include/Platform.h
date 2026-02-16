#ifndef PLATFORM_H
#define PLATFORM_H

// enables logging no matter what
// #define AX_ENABLE_LOGGING

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
    float MousePosX, MousePosY;
    float MouseWheelDelta;
    float SecondsSinceLastClick;
    double DeltaTime;
    
    uint64_t LastClickTime;
    uint64_t CPUFrequency;
    uint64_t StartupTime;
    uint64_t LastTime;
    
    // Window state
    int WindowWidth, WindowHeight;
    int WindowPosX, WindowPosY;
    
    // Keyboard state
    Bitset DownKeys, LastKeys, PressedKeys, ReleasedKeys;
    int MouseDown, MouseLast, MousePressed, MouseReleased;
    bool DoubleClicked;
    
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
void   GetMousePos(float* x, float* y);
void   SetMousePos(float x, float y);
void   wGetMouseWindowPos(float* x, float* y);
void   wGetMonitorSize(int* width, int* height);
void   SetMouseWindowPos(float x, float y);
float  GetMouseWheelDelta();
bool   GetDoubleClicked();
bool   AnyMouseKeyDown();
bool   GetMouseDown(uint32_t button);
bool   GetMouseReleased(uint32_t button);
bool   GetMousePressed(uint32_t button);

// Keyboard
bool   AnyKeyDown();
bool   GetKeyDown(char c);
bool   GetKeyReleased(char c);
bool   GetKeyPressed(char c);

// forward declare, for sdl dialog file callback
typedef void (SDLCALL *CP_DialogFileCallback)(void *userdata, const char * const *filelist, int filter);

// window
void   wSetWindowSize(int width, int height);
void   wSetWindowPosition(int x, int y);
bool   wOpenFolder(const char* folderPath, CP_DialogFileCallback callback);
bool   wOpenFile(const char* filePath, CP_DialogFileCallback callback);

double GetDeltaTime();
double TimeSinceStartup();

void   PlatformInit();
void   PlatformUpdate();

extern PlatformContext PlatformCtx;

void EventCallback(const SDL_Event* event);

#if defined(__cplusplus)
}
#endif

#endif // PLATFORM_H