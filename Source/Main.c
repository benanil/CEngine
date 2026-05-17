
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_main.h>

#include "Include/Platform.h"
#include "Include/Camera.h"
#include "Include/Rendering.h"
#include "Include/Graphics.h"
#include "Include/RenderSet.h"
#include "Include/Memory.h"
#include "Include/Random.h"
#include "Include/AssetManager.h"

static Uint32 frames = 0;
static s32 done = 0;

Camera       g_Camera;
SDL_Window*  g_SDLWindow;

extern RenderState  g_RenderState;
extern SceneBundle* gPaladin;
extern SceneBundle* gSponza;

extern RenderSet    skinnedSet;
extern RenderSet    surfaceSet;

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

    if (!done) Render();
    // else emscripten_cancel_main_loop();

    RecordLastKeys();
    PlatformCtx.FrameCount++;
}

// this is going to be in main or something
s32 InitScene()
{
    gPaladin = (SceneBundle*)AllocateTLSFGlobal(sizeof(SceneBundle));
    gSponza  = (SceneBundle*)AllocateTLSFGlobal(sizeof(SceneBundle));
    
    if (!LoadGLTFCached("Assets/Meshes/Paladin/Paladin.gltf", gPaladin, g_RenderState.textures))
    {
        AX_ERROR("gltf scene load failed");
        return 0;
    }
    
    s32 sponzaRes = LoadGLTFCached("Assets/Meshes/Bistro/Bistro.glb", gSponza, g_RenderState.textures + gPaladin->numImages);
    if (!sponzaRes)
    {
        AX_ERROR("gltf sponza load failed: %d", sponzaRes);
        return 0;
    }

    if (!SceneBundleCreateAnimations(gPaladin)) return 0;
    InitAnimationInstances();

    SceneBundle* bundles[2] = { gPaladin, gSponza };
    u32 imageOffsets[2] = { 0, (u32)gPaladin->numImages };
    TextureSystem_BuildPages(bundles, imageOffsets, 2, g_RenderState.textures);

    u32 skinnedBundle = RenderSet_AddSceneBundle(&skinnedSet, gPaladin);
    u32 surfaceBundle = RenderSet_AddSceneBundle(&surfaceSet, gSponza);

    for (s32 i = 0; i < 50; i++)
    {
        u64 hash = MurmurHash(i + 123);
        v128f pos = VecMulf(VecSetR(f32_(i & 7), 0.0f, f32_(i >> 3), 0.0f), 1.5f);
        v128f rot = VecSetR(0.0f, NextDouble01(hash) * 2.0f - 1.0f, 0.0f, NextDouble01(MurmurHash(hash)) * 2.0f - 1.0f);  // x=0, y=random, z=0, w=random
        v128f scale = VecSet1(0.01f);
        if (!RenderSet_AddScene(&skinnedSet, skinnedBundle, pos, rot, scale, true))
            break;
    }
    
    for (s32 i = 0; i < 1; i++)
    {
        u64 hash = MurmurHash(i + 123);
        v128f pos = VecMulf(VecSetR(f32_(i & 7), 0.0f, f32_(i >> 3), 0.0f), 25.5f);
        v128f rot = QFromEuler(0.0f, MATH_PI, 0.0f); // VecSetR(0.0f, 0.0f, 0.0f, 1.0f);  // x=0, y=random, z=0, w=random
        v128f scale = VecSet1(0.1f);
        if (!RenderSet_AddScene(&surfaceSet, surfaceBundle, pos, rot, scale, false))
            break;
    }
    
    InitBuffers();
    return 1;
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
    BasisuSetup();
    RenderSet_Init();

    GraphicsInit(msaa);
    InitTextureSystem();
    RendererInit();
    if (!InitScene()) return 0;
    
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

