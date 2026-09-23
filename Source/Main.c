
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
#include "Include/Slug.h"
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
static EntityID characterEntity = INVALID_ENTITY;
extern WindowState g_WindowState;
SceneBundle* capsuleBundle;

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

    GetUnitCapsule();
    // if (DemoScene_Create()) if (!Scene_MakeActive(DemoScene_Get())) return SDL_APP_FAILURE;
    if (!Scene_NewActive()) return SDL_APP_FAILURE;
    
    // Keep the runnable Transvoxel example in the demo scene instead of reopening the last editor scene.

    CameraInit(&g_Camera, 1920, 1080);
    capsuleBundle  = GenerateCapsule(1.0f, 2.0f, 16u);
    return SDL_APP_CONTINUE;
}

void OpenSceneCallback(const char* path)
{
    Scene* scene = Scene_GetActive();
    u32 capsule  = Scene_AddBundle(scene, capsuleBundle, "Character");
    characterEntity = Scene_Spawn(scene, capsule, VecSetR(0.0f, 0.0f, 0.0f, 0.f), QIdentity(), VecOne());

    Entity* character = RenderSet_GetEntity(&scene->surfaceSet, characterEntity);
    Entity_SetPhysicsShape(scene, character, b3_sphereShape);
    b3BodyId body = Entity_GetPhysicsBody(scene, character);

    if(b3Body_IsValid(body))
        b3Body_SetBullet(body, true);
}

void BeforeDestroySceneCallback(Scene* scene)
{
    characterEntity = INVALID_ENTITY;
}

static void UpdateBall()
{
    Scene* scene = Scene_GetActive();
    static bool ballActive = false;
    static float force = 300.0f;
    Entity* ball = RenderSet_GetEntity(&scene->surfaceSet, characterEntity);
    if (ball == NULL) {
        characterEntity = INVALID_ENTITY; // deleted somewhere we lost its reference
        return;
    }

    force = Maxf32(0.0f, force + GetMouseWheelDelta());
    b3BodyId body = Entity_GetPhysicsBody(scene, ball);

    if (GetKeyPressed(SDLK_J))
    {
        ballActive = !ballActive;
        ball->position = VecZero();
        Entity_SetPhysicsBodyType(scene, ball, ballActive ? b3_dynamicBody : b3_staticBody);
    }
    
    if (GetKeyDown(SDLK_SPACE))
    {
        b3Body_ApplyLinearImpulseToCenter(body, (b3Vec3) { 0.0f, force * 5.0f, 0.0f }, false);
    }

    if (ballActive)
    {
        float fwdButton = GetKeyDown(SDLK_W) ? 1.0f : GetKeyDown(SDLK_S) ? -1.0f : 0.0f;
        float rgtButton = GetKeyDown(SDLK_D) ? 1.0f : GetKeyDown(SDLK_A) ? -1.0f : 0.0f;
        // if no input slowly stop
        if (Absf32(fwdButton) + Absf32(rgtButton) < MATH_Epsilon)
        {
            b3Vec3 vel = b3Body_GetLinearVelocity(body);
            float slowDown = 1.0f - (float)GetDeltaTime();
            b3Body_SetLinearVelocity(body, b3Mul(vel, (b3Vec3){slowDown, slowDown, slowDown}));
        }
        else
        {
            b3Vec3 forward = Float3ToB3Vec3(F3Proj(F3MulF(g_Camera.Front, fwdButton), F3Up()));
            b3Vec3 right   = Float3ToB3Vec3(F3Proj(F3MulF(g_Camera.Right, rgtButton), F3Up()));
            b3Vec3 direction = b3Normalize(b3Add(forward, right));
            b3Body_ApplyLinearImpulseToCenter(body, b3Mul(direction, (b3Vec3){force, force, force}), true);
        }
    }
    // Entity_SyncPhysicsBody(scene, ball);
}

static void MainLoopTick(void)
{
    if (g_MainLoopTicking) return;

    g_MainLoopTicking = true;
    MainSyncWindowSize();

    SetPressedAndReleasedKeys();
    PlatformUpdate();
    CameraUpdate(&g_Camera, PlatformCtx.DeltaTime, EditorSceneInteractAllowed());
    // builtin transvoxel terrain disabled while testing the transvoxel-unity port
    // (tUpdate below); re-enable once the port replaces it for real
    // Terrain_Update(&g_Camera);

    EditorSceneHotkeys();
    
    DemoScene_Update(PlatformCtx.DeltaTime);
    Scene_Update(PlatformCtx.DeltaTime);
    
    Scene_SubmitLights();

    if (!TerrainEditorUpdate(&g_Camera) && !EditorGizmoUpdate(&g_Camera) && !EditorLightGizmoUpdate(&g_Camera))
        EditorPickingUpdate(&g_Camera);

    tUpdate();
    UpdateBall();

    if (!done) Render();
    // else emscripten_cancel_main_loop();

    RecordLastKeys();
    PlatformCtx.FrameCount++;
    PlatformCtx.MouseWheelDelta = 0.0f;
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
