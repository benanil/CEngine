
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_main.h>

#include "Include/Platform.h"
#include "Include/Camera.h"
#include "Include/Rendering.h"
#include "Include/Graphics.h"
#include "Include/Memory.h"
#include "Include/Animation.h"
#include "Include/BasisBinding.h"
#include "Include/Scene.h"
#include "Include/DemoScene.h"
#include "Include/Terrain.h"
#include "Math/Quaternion.h"

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
    Terrain_Update(&g_Camera);
    DemoScene_Update(PlatformCtx.DeltaTime);
    Scene_SubmitLights();

    extern void EditorSceneHotkeys(void);
    EditorSceneHotkeys();

    // the gizmo owns the mouse while hovered or dragging, picking only runs otherwise
    extern bool EditorGizmoUpdate(Camera* camera);
    extern bool EditorLightGizmoUpdate(Camera* camera);
    extern void EditorPickingUpdate(Camera* camera);
    if (!EditorGizmoUpdate(&g_Camera) && !EditorLightGizmoUpdate(&g_Camera))
        EditorPickingUpdate(&g_Camera);

    if (!done) Render();
    // else emscripten_cancel_main_loop();

    RecordLastKeys();
    PlatformCtx.FrameCount++;
}

s32 main(s32 argc, char* argv[])
{
    s32 msaa = 1;
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
    PlatformInit();

    // hook the sdl log output before the systems init so the console records everything
    extern void EditorConsoleInit(void);
    EditorConsoleInit();

    BasisuSetup();

    GraphicsInit(msaa);
    TextureSystem_InitDevice();
    RendererInit();

    // boot into an empty editor scene, the demo scene stays available from code
    extern Scene* EditorNewScene(void);
    if (!EditorNewScene()) return 0;
    extern void EditorSceneStartup(void);
    EditorSceneStartup();
    InitBuffers();
    Terrain_Init();

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
