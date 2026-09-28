
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_main.h>
#include <box3d/box3d.h>

#include "Include/Platform.h"
#include "Include/Algorithm.h"
#include "Include/Camera.h"
#include "Include/Rendering.h"
#include "Include/Random.h"
#include "Include/UIRenderer.h"
#include "Include/UIWindow.h"
#include "Include/Slug.h"
#include "Include/Editor.h"
#include "Include/Graphics.h"
#include "Include/Memory.h"
#include "Include/Animation.h"
#include "Include/BasisBinding.h"
#include "Include/Scene.h"
#include "Include/DemoScene.h"
#include "Include/Terrain.h"
#include "Include/Editor.h"
#include "Include/JobSystem.h"
#include "Include/AssetManager.h"
#include "Math/Quaternion.h"

static s32 done = 0;
static bool g_MainLoopTicking;

Camera       g_Camera;
SDL_Window*  g_SDLWindow;

extern WindowState g_WindowState;
extern void Gameplay_Update();

static void MainSyncWindowSize(void)
{
    int width, height;
    SDL_GetWindowSize(g_SDLWindow, &width, &height);
    if ((width + height) == 0) return;

    if (PlatformCtx.WindowWidth != width || PlatformCtx.WindowHeight != height)
    {
        Camera_RecalculateProjection(&g_Camera, width, height);
        PlatformCtx.WindowWidth = width;
        PlatformCtx.WindowHeight = height;
    }
}

void DestroyMain()
{
    done = 1;
}

static SDL_AppResult SDLCALL MainAppInit(void** appstate, int argc, char* argv[])
{
    (void)appstate; (void)argc; (void)argv; 
    s32 msaa = 1;
    done = 0;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO))
        return SDL_APP_FAILURE;

    const SDL_WindowFlags windowFlags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_BORDERLESS;
    SDL_Window* window = SDL_CreateWindow("C Engine", 1920, 1080, windowFlags);
    g_SDLWindow = window;
    if (!window) {
        AX_ERROR("creating window failed!");
        return SDL_APP_FAILURE;
    }

    InitGlobalArena();
    PlatformInit();

    EditorConsoleInit();

    BasisuSetup();

    GraphicsInit(msaa);
    TextureSystem_InitDevice();
    RendererInit();
    EditorInit();

    InitBuffers();

    // if (DemoScene_Create()) if (!Scene_MakeActive(DemoScene_Get())) return SDL_APP_FAILURE;
    if (!Scene_NewActive()) return SDL_APP_FAILURE;
    
    // Keep the runnable Transvoxel example in the demo scene instead of reopening the last editor scene.
    CameraInit(&g_Camera, 1920, 1080);
    return SDL_APP_CONTINUE;
}

static void MainLoopTick(void)
{
    if (g_MainLoopTicking) return;

    g_MainLoopTicking = true;
    MainSyncWindowSize();

    SetPressedAndReleasedKeys();
    PlatformUpdate();

    EditorSceneHotkeys();
    
    DemoScene_Update(PlatformCtx.DeltaTime);
    Scene_Update(PlatformCtx.DeltaTime);
    
    Scene_SubmitLights();

    if (!TerrainEditorUpdate(&g_Camera) && !EditorGizmoUpdate(&g_Camera) && !EditorLightGizmoUpdate(&g_Camera))
        EditorPickingUpdate(&g_Camera);

    UIBeginFrame();

    tUpdate();
    Gameplay_Update();
    GraphicsEditorUI();
    
    CameraUpdate(&g_Camera, PlatformCtx.DeltaTime);

    if (!done) Render();
    // else emscripten_cancel_main_loop();

    RecordLastKeys();
    PlatformCtx.FrameCount++;
    PlatformCtx.MouseWheelDelta = 0.0f;
    PlatformCtx.MouseMotionX = 0.0f;
    PlatformCtx.MouseMotionY = 0.0f;

    g_MainLoopTicking = false;
}

static SDL_AppResult SDLCALL MainAppEvent(void* appstate, SDL_Event* event)
{
    (void)appstate;
    if (!event) return SDL_APP_CONTINUE;
    done = done || (event->type == SDL_EVENT_QUIT);
    EventCallback(event);
    return done ? SDL_APP_SUCCESS : SDL_APP_CONTINUE;
}

static SDL_AppResult SDLCALL MainAppIterate(void* appstate)
{
    (void)appstate;
    if (done) return SDL_APP_SUCCESS;
    MainLoopTick();
    return done ? SDL_APP_SUCCESS : SDL_APP_CONTINUE;
}

static void SDLCALL MainAppQuit(void* appstate, SDL_AppResult result)
{
    (void)appstate;
    (void)result;
    Foliage_Destroy();
    tDestroy();
    Physics_Destroy();
}

s32 main(s32 argc, char* argv[])
{
    return SDL_EnterAppMainCallbacks(argc, argv, MainAppInit, MainAppIterate, MainAppEvent, MainAppQuit);
}
