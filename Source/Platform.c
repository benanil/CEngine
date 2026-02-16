#ifndef PLATFORM_C
#define PLATFORM_C


#if defined(_WIN32) || defined(_WIN64)
    #define WIN32_LEAN_AND_MEAN
    #include <Windows.h>
#endif

#include "Include/Platform.h"
#include "Include/Bitset.h"
#include "Include/Camera.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_dialog.h>

#define STB_SPRINTF_IMPLEMENTATION
#include "Extern/stb/stb_sprintf.h"

PlatformContext PlatformCtx = {0};
extern Camera globalCamera;
extern SDL_Window* sdlWindow;

static uint64_t DownKeys[8]; 
static uint64_t LastKeys[8]; 
static uint64_t PressedKeys[8];
static uint64_t ReleasedKeys[8];

// Sokol event callback
void EventCallback(const SDL_Event* event) 
{
    switch (event->type) {
        case SDL_EVENT_MOUSE_MOTION:
            PlatformCtx.MousePosX = event->motion.x;
            PlatformCtx.MousePosY = event->motion.y;
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            PlatformCtx.MouseWheelDelta = event->wheel.y;
            break;
            
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            uint32_t button_flag = 1 << (event->button.button);
            PlatformCtx.MouseDown |= button_flag;
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_UP: 
        {
            uint32_t button_flag = 1 << (event->button.button);
            
            // Handle double click detection for left button
            if (button_flag == MouseButton_Left) {
                uint64_t current_time = SDL_GetPerformanceCounter();
                double time_diff = (current_time - PlatformCtx.LastClickTime) / (double)PlatformCtx.CPUFrequency;
                PlatformCtx.DoubleClicked = (time_diff < 0.4);
                PlatformCtx.SecondsSinceLastClick = 0.0f;
                PlatformCtx.LastClickTime = current_time;
            }
            PlatformCtx.MouseDown &= ~button_flag;
            
            break;
        }
        case SDL_EVENT_KEY_DOWN: {
            int vk_code = event->key.key;
            if (vk_code > 0 && vk_code < 512) {
                Bitset_Set(&PlatformCtx.DownKeys, vk_code);
            }
            break;
        }
        case SDL_EVENT_KEY_UP: {
            int vk_code = event->key.key;
            if (vk_code > 0 && vk_code < 512) {
                Bitset_Reset(&PlatformCtx.DownKeys, vk_code);
            }
            break;
        }
        case  SDL_EVENT_WINDOW_RESIZED: {
            if ((event->window.data1 + event->window.data2) != 0)
                Camera_RecalculateProjection(&globalCamera, event->window.data1, event->window.data2);
            PlatformCtx.WindowWidth = event->window.data1;
            PlatformCtx.WindowHeight = event->window.data2;
            break;
        }
        case SDL_EVENT_WINDOW_MOVED:
            PlatformCtx.WindowPosX = event->window.data1;
            PlatformCtx.WindowPosY = event->window.data2;
            break;
        case SDL_EVENT_QUIT:
            
            break;
            
        default:
            break;
    }
}

void GetMousePos(float* x, float* y) { SDL_GetMouseState(x, y); }

void SetMousePos(float x, float y) {  SDL_WarpMouseGlobal(x, y); }

void wGetMouseWindowPos(float* x, float* y) {
    float globalX, globalY;
    GetMousePos(&globalX, &globalY);
    *x = PlatformCtx.MousePosX - globalX; 
    *y = PlatformCtx.MousePosY - globalY;
}

void wGetMonitorSize(int* width, int* height) 
{
    const SDL_DisplayMode* DM = SDL_GetCurrentDisplayMode(0);
    if (DM)
    {
        *width  = DM->w;
        *height = DM->h;
    }
    else
    {
        *width = 1920;
        *height = 1090;
    }
}

void SetMouseWindowPos(float x, float y)
{
    SDL_WarpMouseInWindow(sdlWindow, x, y);
}

bool AnyKeyDown()           { return Bitset_Count(&PlatformCtx.DownKeys) > 0; }
bool GetKeyDown(char c)     { return Bitset_Get(&PlatformCtx.DownKeys, c); }
bool GetKeyReleased(char c) { return Bitset_Get(&PlatformCtx.ReleasedKeys, c); }
bool GetKeyPressed(char c)  { return Bitset_Get(&PlatformCtx.PressedKeys, c); }

float GetMouseWheelDelta() { return PlatformCtx.MouseWheelDelta; }
bool GetDoubleClicked() { return PlatformCtx.DoubleClicked; }
bool AnyMouseKeyDown()                 { return PlatformCtx.MouseDown > 0; }
bool GetMouseDown(uint32_t button)     { return !!(PlatformCtx.MouseDown     & button); }
bool GetMouseReleased(uint32_t button) { return !!(PlatformCtx.MouseReleased & button); }
bool GetMousePressed(uint32_t button)  { return !!(PlatformCtx.MousePressed  & button); }

static void SetPressedAndReleasedKeys()
{
    Bitset_AndNot(&PlatformCtx.ReleasedKeys, &PlatformCtx.LastKeys, &PlatformCtx.DownKeys);
    Bitset_AndNot(&PlatformCtx.PressedKeys , &PlatformCtx.DownKeys, &PlatformCtx.LastKeys);
    // Mouse
    PlatformCtx.MouseReleased = PlatformCtx.MouseLast & ~PlatformCtx.MouseDown;
    PlatformCtx.MousePressed  = ~PlatformCtx.MouseLast & PlatformCtx.MouseDown;
}

void wSetWindowSize(int width, int height)
{
    SDL_SetWindowSize(sdlWindow, width, height);
    PlatformCtx.WindowWidth = width;
    PlatformCtx.WindowHeight = height;
}

void wSetWindowPosition(int x, int y)
{
    SDL_SetWindowPosition(sdlWindow, x, y);
    PlatformCtx.WindowPosX = x;
    PlatformCtx.WindowPosY = y;
}

//  void FolderCallback(void *userdata, const char * const *filelist, int filter)
bool wOpenFolder(const char* folderPath, SDL_DialogFileCallback callback)
{
    SDL_ShowOpenFolderDialog(
        callback,
        NULL,               // supply state if you want the result
        NULL,               // parent window if you have one
        folderPath,
        false
    );

    return true;
}

// void FileCallback(void *userdata, const char * const *filelist, int filter)
bool wOpenFile(const char* filePath, SDL_DialogFileCallback callback)
{
    SDL_ShowOpenFileDialog(
        callback,
        NULL,
        NULL,
        NULL,   // filters
        0,
        filePath,
        false
    );

    return true;
}

double GetDeltaTime() 
{ 
    return PlatformCtx.DeltaTime; 
}

void PlatformInit()
{
    PlatformCtx.SecondsSinceLastClick = 0.0f;
    PlatformCtx.DownKeys.bits = DownKeys;
    PlatformCtx.LastKeys.bits = LastKeys;
    PlatformCtx.PressedKeys.bits = PressedKeys;
    PlatformCtx.ReleasedKeys.bits = ReleasedKeys;
    PlatformCtx.CPUFrequency = SDL_GetPerformanceFrequency();
    PlatformCtx.StartupTime = SDL_GetPerformanceCounter();
    PlatformCtx.LastTime = PlatformCtx.StartupTime;
}

void PlatformUpdate()
{
    Uint64 now = SDL_GetPerformanceCounter();
    Uint64 delta = now - PlatformCtx.LastTime;
    PlatformCtx.DeltaTime = (double)delta / (double)PlatformCtx.CPUFrequency;
    PlatformCtx.LastTime = now;
}

double TimeSinceStartup()
{
    return (double)(SDL_GetPerformanceCounter() - PlatformCtx.StartupTime) / (double)PlatformCtx.CPUFrequency;
}

#endif // PLATFORM_C
