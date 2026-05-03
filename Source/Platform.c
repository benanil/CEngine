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

static ALIGNAS(SIMD_NUM_BYTES) u64 DownKeys[8];
static ALIGNAS(SIMD_NUM_BYTES) u64 LastKeys[8]; 
static ALIGNAS(SIMD_NUM_BYTES) u64 PressedKeys[8];
static ALIGNAS(SIMD_NUM_BYTES) u64 ReleasedKeys[8];

inline static s32 GetRealKey(s32 x)
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
            s32 vk_code = GetRealKey(event->key.key);
            
            if (vk_code > 0 && vk_code < 512) BitsetSet(DownKeys, vk_code);
            else SDL_Log("unhandelled key code down: %x", vk_code);

            break;
        }
        case SDL_EVENT_KEY_UP: {
            s32 vk_code = GetRealKey(event->key.key);
            
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
            u32 button_flag = 1 << (event->button.button);
            PlatformCtx.MouseDown |= button_flag;
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_UP: 
        {
            u32 button_flag = 1 << (event->button.button);
            // Handle f64 click detection for left button
            if (button_flag == MouseButton_Left) {
                u64 current_time = SDL_GetPerformanceCounter();
                f32 time_diff = (current_time - PlatformCtx.LastClickTime) / (double)PlatformCtx.CPUFrequency;
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

u8 AnyKeyDown()          { return PopCount512(DownKeys) > 0; }
u8 GetKeyDown(s32 c)     { return BitsetGet(DownKeys    , GetRealKey(c) & 511); }
u8 GetKeyReleased(s32 c) { return BitsetGet(ReleasedKeys, GetRealKey(c) & 511); }
u8 GetKeyPressed(s32 c)  { return BitsetGet(PressedKeys , GetRealKey(c) & 511); }

// Mouse
f32 GetMouseWheelDelta()  { return PlatformCtx.MouseWheelDelta; }
u8 GetDoubleClicked()    { return PlatformCtx.DoubleClicked; }
u8 AnyMouseKeyDown()            { return PlatformCtx.MouseDown > 0; }
u8 GetMouseDown(s32 button)     { return !!(PlatformCtx.MouseDown     & button); }
u8 GetMouseReleased(s32 button) { return !!(PlatformCtx.MouseReleased & button); }
u8 GetMousePressed(s32 button)  { return !!(PlatformCtx.MousePressed  & button); }


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
    MemCopy(LastKeys, DownKeys, sizeof(u64) * 8);
    // PlatformCtx.LastKeys  = PlatformCtx.DownKeys;
    PlatformCtx.MouseLast = PlatformCtx.MouseDown;
}

void wSetWindowSize(s32 width, s32 height)
{
    SDL_SetWindowSize(g_SDLWindow, width, height);
    PlatformCtx.WindowWidth = width;
    PlatformCtx.WindowHeight = height;
}

void wSetWindowPosition(s32 x, s32 y)
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

static void EnableConsoleColors(void)
{
    #if defined(_WIN32)
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    #endif
}

f32 GetDeltaTime() 
{ 
    return PlatformCtx.DeltaTime; 
}

s64 TimeNow()
{
    return SDL_GetPerformanceCounter();
}

// time is nanoseconds
f32  TimeToSeconds(s64 t)     { return (double)t / (double)PlatformCtx.CPUFrequency; }
s64 TimeToMilliseconds(s64 t) { return Int64MulDiv(t, 1000, PlatformCtx.CPUFrequency); }
s64 TimeToMicroseconds(s64 t) { return Int64MulDiv(t, 1000000, PlatformCtx.CPUFrequency); }

void PlatformInit()
{
    EnableConsoleColors();
    SDL_SetLogPriorities(SDL_LOG_PRIORITY_VERBOSE);
    PlatformCtx.SecondsSinceLastClick = 0.0f;
    PlatformCtx.CPUFrequency          = SDL_GetPerformanceFrequency();
    PlatformCtx.StartupTime           = SDL_GetPerformanceCounter();
    PlatformCtx.LastTime              = PlatformCtx.StartupTime;
    PlatformCtx.FrameCount            = 0;
}

void PlatformUpdate()
{
    s64 now = TimeNow();
    s64 elapsed = now - PlatformCtx.LastTime;
    PlatformCtx.DeltaTime = Clampf64((double)(elapsed) / (double)PlatformCtx.CPUFrequency, 0.0, 1.0);
    PlatformCtx.LastTime  = now;
    SetPressedAndReleasedKeys();
    RecordLastKeys();
    char buff[128] = {0};
    IntToString(buff, (int64_t)TimeToMilliseconds(elapsed), 0);
    SDL_SetWindowTitle(g_SDLWindow, buff);
}

f32 TimeSinceStartup()
{
    return (double)(TimeNow() - PlatformCtx.StartupTime) / (double)PlatformCtx.CPUFrequency;
}

#endif // PLATFORM_C
