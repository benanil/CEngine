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

#define STB_SPRINTF_IMPLEMENTATION
#include "Extern/stb/stb_sprintf.h"

PlatformContext PlatformCtx = {0};
// extern Camera globalCamera;

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
                // uint64_t current_time = stm_now();
                // double time_diff = stm_sec(stm_diff(current_time, PlatformCtx.LastClickTime));
                // PlatformCtx.DoubleClicked = (time_diff < 0.4);
                // PlatformCtx.SecondsSinceLastClick = 0.0f;
                // PlatformCtx.LastClickTime = current_time;
            }
            PlatformCtx.MouseDown &= ~button_flag;
            
            break;
        }
        case SDL_EVENT_KEY_DOWN: {
            int vk_code = event->key.key;
            Bitset_Set(&PlatformCtx.DownKeys, vk_code);
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
                // Camera_RecalculateProjection(&globalCamera, event->window.data1, event->window.data2);
            break;
        }
        case SDL_EVENT_QUIT:
            
            break;
            
        default:
            break;
    }
}

void FatalError(const char* format, ...)
{
    char buffer[2048];
    va_list args;
    va_start(args, format);
    stbsp_vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
#ifdef PLATFORM_WINDOWS
    HANDLE stdout_handle = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD chars_written;
    WriteFile(stdout_handle, buffer, StringLength(buffer), &chars_written, NULL);
    WriteFile(stdout_handle, "\n", 1, &chars_written, NULL);
    
    OutputDebugString(buffer);
    MessageBoxA(NULL, buffer, "Fatal Error", MB_ICONERROR | MB_OK);
#endif
}

void DebugLog(const char* format, ...)
{
    char buffer[2048];
    va_list args;
    va_start(args, format);
    stbsp_vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
#ifdef PLATFORM_WINDOWS
    HANDLE stdout_handle = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD chars_written;
    WriteFile(stdout_handle, buffer, StringLength(buffer), &chars_written, NULL);
    WriteFile(stdout_handle, "\n", 1, &chars_written, NULL);
    
    OutputDebugString(buffer);
    MessageBoxA(NULL, buffer, "DebugLog", MB_ICONWARNING | MB_OK);
#endif
}

void GetMousePos(float* x, float* y) {
#ifdef PLATFORM_WINDOWS
    //ASSERT((uint64_t)x & (uint64_t)y); // shouldn't be nullptr
    POINT point;
    GetCursorPos(&point);
    *x = (float)point.x;
    *y = (float)point.y;
#endif
}

void SetMousePos(float x, float y)
{
#ifdef PLATFORM_WINDOWS
    SetCursorPos((int)x, (int)y);
#endif
}

void wGetMouseWindowPos(float* x, float* y) {
    *x = PlatformCtx.MousePosX; *y = PlatformCtx.MousePosY;
}

void wGetMonitorSize(int* width, int* height) 
{
#ifdef PLATFORM_WINDOWS
    *width  = GetSystemMetrics(SM_CXSCREEN);
    *height = GetSystemMetrics(SM_CYSCREEN);
#endif
}

void SetMouseWindowPos(float x, float y)
{
    SetMousePos(PlatformCtx.WindowPosX + x, PlatformCtx.WindowPosY + y);
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
#ifdef PLATFORM_WINDOWS
    PlatformCtx.WindowWidth = width; PlatformCtx.WindowHeight = height;
    // SetWindowPos((void*)sapp_win32_get_hwnd(), NULL, PlatformCtx.WindowPosX, PlatformCtx.WindowPosY, width, height, 0);
#endif
}

void wSetWindowPosition(int x, int y)
{
#ifdef PLATFORM_WINDOWS
    PlatformCtx.WindowPosX = x; PlatformCtx.WindowPosY = y;
    // SetWindowPos((void*)sapp_win32_get_hwnd(), NULL, x, y, PlatformCtx.WindowWidth, PlatformCtx.WindowHeight, 0);
#endif
}

static void FixSeperators(char* dst, uint64_t dstSize, const char* src)
{
    int len = StringLengthSafe(src, dstSize);
    SmallMemCpy(dst, src, len);
    
    for (int i = 0; i < len; i++) 
        if (dst[i] == '/') dst[i] = '\\';
}

bool wOpenFolder(const char* folderPath) 
{
#ifdef PLATFORM_WINDOWS
    char copy[1024] = {0};
    FixSeperators(copy, sizeof(copy), folderPath);

    if ((size_t)ShellExecuteA(NULL, "open", copy, NULL, NULL, SW_SHOWNORMAL) <= 32) 
        return false;
    return true;
#else 
    return false;
#endif
}

bool wOpenFile(const char* filePath)
{
#ifdef PLATFORM_WINDOWS
    char copy[1024] = {0};
    FixSeperators(copy, sizeof(copy), filePath);  

    if ((size_t)ShellExecuteA(NULL, NULL, copy, NULL, NULL, SW_SHOW) <= 32)
        return false;
    return true;
#else 
    return false;
#endif
}


double GetDeltaTime() 
{ 
    return PlatformCtx.DeltaTime; 
}

static uint64_t DownKeys[8]; 
static uint64_t LastKeys[8]; 
static uint64_t PressedKeys[8];
static uint64_t ReleasedKeys[8];

void PlatformInit()
{
    PlatformCtx.SecondsSinceLastClick = 0.0f;
    PlatformCtx.DownKeys.bits = DownKeys;
    PlatformCtx.LastKeys.bits = LastKeys;
    PlatformCtx.PressedKeys.bits = PressedKeys;
    PlatformCtx.ReleasedKeys.bits = ReleasedKeys;
    PlatformCtx.StartupTime = 0; // stm_now();
}

double TimeSinceStartup()
{
    return (double)(SDL_GetTicks() - PlatformCtx.StartupTime) / 1000.0;
}

#endif // PLATFORM_C
