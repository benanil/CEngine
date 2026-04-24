
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_main.h>

#include "Include/Platform.h"
#include "Include/Camera.h"
#include "Include/Rendering.h"
#include "Include/Graphics.h"

static Uint32 frames = 0;
static s32 done = 0;

Camera       g_Camera;
SDL_Window*  g_SDLWindow;

static void MainLoop(void)
{
    SDL_Event event;
    while (SDL_PollEvent(&event) && !done)
    {
        done = (event.type == SDL_EVENT_QUIT);
        EventCallback(&event);
    }
    
    SetPressedAndReleasedKeys();
    PlatformUpdate();
    CameraUpdate(&g_Camera, PlatformCtx.DeltaTime);
    UpdateAnimations();

    if (!done) Render();
    // else emscripten_cancel_main_loop();

    RecordLastKeys();
    PlatformCtx.FrameCount++;
}

s32 main(s32 argc, char* argv[])
{
    s32 msaa = 0;
    done = 0;
    
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO))
        return 0;
    
    const SDL_WindowFlags windowFlags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    SDL_Window* window = SDL_CreateWindow("CPlayground", 1920, 1080, windowFlags);
    g_SDLWindow = window;
    if (!window) 
    {
        AX_ERROR("creating window failed!");
        return 0; 
    }
    
    InitGlobalArena();
    BasisuSetup();
    ECS_Init();

    GraphicsInit(msaa);
    RendererInit(msaa);
    if (!InitScene()) return 0;
    InitBuffers();

    PlatformInit();
    CameraInit(&g_Camera, 1920, 1080);
    
    // emscripten_set_main_loop(MainLoop, 0, 1);
    while (!done) 
    {
        MainLoop();
    }

    #if !defined(__ANDROID__)
    Quit(0);
    #endif
    return 0;
}

