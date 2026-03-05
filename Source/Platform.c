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
extern Camera g_Camera;
extern SDL_Window* g_SDLWindow;

static ALIGNAS(SIMD_NUM_BYTES) uint64_t DownKeys[8];
static ALIGNAS(SIMD_NUM_BYTES) uint64_t LastKeys[8]; 
static ALIGNAS(SIMD_NUM_BYTES) uint64_t PressedKeys[8];
static ALIGNAS(SIMD_NUM_BYTES) uint64_t ReleasedKeys[8];

inline static int GetRealKey(int x)
{
    if (x & 0x40000000u) return SDLK_PLUSMINUS + x - 0x40000039u;
    if (x & 0x20000000u) return SDLK_PLUSMINUS + 0x122u + x - 0x20000001u;
    return x;
}

// Sokol event callback
void EventCallback(const SDL_Event* event) 
{
    switch (event->type) {
        case SDL_EVENT_KEY_DOWN: {
            int vk_code = GetRealKey(event->key.key);
            
            if (vk_code > 0 && vk_code < 512) BitsetSet(DownKeys, vk_code);
            else SDL_Log("unhandelled key code down: %x", vk_code);

            break;
        }
        case SDL_EVENT_KEY_UP: {
            int vk_code = GetRealKey(event->key.key);
            
            if (vk_code > 0 && vk_code < 512) BitsetReset(DownKeys, vk_code);
            else SDL_Log("unhandelled key code up: %x", vk_code);
            
            break;
        }
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
        case  SDL_EVENT_WINDOW_RESIZED: {
            if ((event->window.data1 + event->window.data2) != 0)
                Camera_RecalculateProjection(&g_Camera, event->window.data1, event->window.data2);
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
    SDL_WarpMouseInWindow(g_SDLWindow, x, y);
}

bool AnyKeyDown()          { return PopCount512(DownKeys) > 0; }
bool GetKeyDown(int c)     { return BitsetGet(DownKeys    , GetRealKey(c) & 511); }
bool GetKeyReleased(int c) { return BitsetGet(ReleasedKeys, GetRealKey(c) & 511); }
bool GetKeyPressed(int c)  { return BitsetGet(PressedKeys , GetRealKey(c) & 511); }

// Mouse
float GetMouseWheelDelta() { return PlatformCtx.MouseWheelDelta; }
bool GetDoubleClicked()    { return PlatformCtx.DoubleClicked; }
bool AnyMouseKeyDown()            { return PlatformCtx.MouseDown > 0; }
bool GetMouseDown(int button)     { return !!(PlatformCtx.MouseDown     & button); }
bool GetMouseReleased(int button) { return !!(PlatformCtx.MouseReleased & button); }
bool GetMousePressed(int button)  { return !!(PlatformCtx.MousePressed  & button); }


void SetPressedAndReleasedKeys()
{
    AndNot512(ReleasedKeys, LastKeys, DownKeys);
    AndNot512(PressedKeys , DownKeys, LastKeys);
    
    // Mouse
    PlatformCtx.MouseReleased = PlatformCtx.MouseLast & ~PlatformCtx.MouseDown;
    PlatformCtx.MousePressed  = ~PlatformCtx.MouseLast & PlatformCtx.MouseDown;
}

void RecordLastKeys()
{
    MemCpy(LastKeys, DownKeys, sizeof(uint64_t) * 8);
    // PlatformCtx.LastKeys  = PlatformCtx.DownKeys;
    PlatformCtx.MouseLast = PlatformCtx.MouseDown;
}

void wSetWindowSize(int width, int height)
{
    SDL_SetWindowSize(g_SDLWindow, width, height);
    PlatformCtx.WindowWidth = width;
    PlatformCtx.WindowHeight = height;
}

void wSetWindowPosition(int x, int y)
{
    SDL_SetWindowPosition(g_SDLWindow, x, y);
    PlatformCtx.WindowPosX = x;
    PlatformCtx.WindowPosY = y;
}

//  void FolderCallback(void *userdata, const char * const *filelist, int filter)
void wOpenFolder(const char* folderPath, SDL_DialogFileCallback callback)
{
    SDL_ShowOpenFolderDialog(callback, NULL, NULL, folderPath, false);
}

void wOpenFile(const char* filePath, SDL_DialogFileCallback callback)
{
    SDL_ShowOpenFileDialog(callback, NULL, NULL, NULL, 0, filePath, false);
}

double GetDeltaTime() 
{ 
    return PlatformCtx.DeltaTime; 
}

int64_t TimeNow()
{
    return SDL_GetPerformanceCounter();
}

// time is nanoseconds
double  TimeToSeconds(int64_t t)       { return (double)t / (double)PlatformCtx.CPUFrequency; }
int64_t TimeToMilliseconds(int64_t t)  { return Int64MulDiv(t, 1000, PlatformCtx.CPUFrequency); }
int64_t TimeToMicroseconds(int64_t t)  { return Int64MulDiv(t, 1000000, PlatformCtx.CPUFrequency); }

void PlatformInit()
{
    PlatformCtx.SecondsSinceLastClick = 0.0f;
    PlatformCtx.CPUFrequency          = SDL_GetPerformanceFrequency();
    PlatformCtx.StartupTime           = SDL_GetPerformanceCounter();
    PlatformCtx.LastTime              = PlatformCtx.StartupTime;
    PlatformCtx.FrameCount            = 0;
}

void PlatformUpdate()
{
    int64_t now = TimeNow();
    PlatformCtx.DeltaTime = Clampf64((double)(now - PlatformCtx.LastTime) / (double)PlatformCtx.CPUFrequency, 0.0, 1.0);
    PlatformCtx.LastTime  = now;
    SetPressedAndReleasedKeys();
    RecordLastKeys();
}

double TimeSinceStartup()
{
    return (double)(TimeNow() - PlatformCtx.StartupTime) / (double)PlatformCtx.CPUFrequency;
}

#endif // PLATFORM_C
