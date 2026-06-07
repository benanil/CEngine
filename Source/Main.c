
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
#include "Include/Animation.h"
#include "Include/BasisBinding.h"

static Uint32 frames = 0;
static s32 done = 0;
static LightGPU g_DemoLights[12];

Camera       g_Camera;
SDL_Window*  g_SDLWindow;

extern RenderState  g_RenderState;
SceneBundle* gPaladin;
SceneBundle* gSponza;

extern RenderSet    skinnedSet;
extern RenderSet    surfaceSet;

static void UpdateDemoLights(void)
{
    static const f32 colors[8][3] = {
        { 1.00f, 0.35f, 0.20f }, { 0.25f, 0.55f, 1.00f },
        { 0.25f, 0.80f, 0.35f }, { 1.00f, 0.85f, 0.25f },
        { 0.95f, 0.25f, 1.00f }, { 0.25f, 0.85f, 0.75f },
        { 1.00f, 0.55f, 0.25f }, { 0.55f, 0.35f, 1.00f }
    };

    f32 time = (f32)PlatformCtx.FrameCount * 0.012f;
    int numLights = 0;
    for (u32 i = 0; i < 0u; i++)
    {
        u32 seed = WangHash(0x9e3779b9u + i * 0x85ebca6bu);
        f32 x = f32_(i) * 0.7f + RepeatMinMaxF32(WangHash(seed + 1u), -2.0f, 2.0f);
        f32 y = 2.0f + RepeatMinMaxF32(WangHash(seed + 2u), -0.5f, 0.5f);
        f32 z = RepeatMinMaxF32(WangHash(seed + 3u), -2.0f, 2.0f);
        f32 baseYaw = RepeatMinMaxF32(WangHash(seed + 4u), 0.0f, MATH_TwoPI);
        f32 yaw = baseYaw + time * RepeatMinMaxF32(WangHash(seed + 5u), 0.35f, 0.85f);
        f32 pitch = RepeatMinMaxF32(WangHash(seed + 6u), -0.65f, -0.20f) + Sin(time * 0.7f + baseYaw) * 0.25f;
        f32 dx = Cos(yaw) * Cos(pitch);
        f32 dy = Sin(pitch);
        f32 dz = Sin(yaw) * Cos(pitch);
        f32 radius = RepeatMinMaxF32(WangHash(seed + 7u), 10.0f, 18.0f);

        g_DemoLights[i].positionRadius[0] = x;
        g_DemoLights[i].positionRadius[1] = y;
        g_DemoLights[i].positionRadius[2] = z;
        g_DemoLights[i].positionRadius[3] = radius;
        g_DemoLights[i].directionCone[0] = dx;
        g_DemoLights[i].directionCone[1] = dy;
        g_DemoLights[i].directionCone[2] = dz;
        g_DemoLights[i].directionCone[3] = 0.72f;
        g_DemoLights[i].colorIntensity[0] = colors[i & 7u][0];
        g_DemoLights[i].colorIntensity[1] = colors[i & 7u][1];
        g_DemoLights[i].colorIntensity[2] = colors[i & 7u][2];
        g_DemoLights[i].colorIntensity[3] = RepeatMinMaxF32(WangHash(seed + 8u), 24.0f, 44.0f);
        g_DemoLights[i].type = LightType_Spot;
        g_DemoLights[i].flags = LIGHT_FLAG_SHADOWED;
        g_DemoLights[i].shadowIndex = LIGHT_SHADOW_INDEX_INVALID;
        g_DemoLights[i].padding = 0u;
        numLights++;
    }

    for (u32 i = 0; i < 8u; i++)
    {
        u32 lightIndex = i + numLights;
        f32 fi = (f32)i;
        f32 angle = time * (0.35f + fi * 0.045f) + fi * (MATH_TwoPI / 8.0f);
        f32 orbit = 3.0f + (f32)(i % 4u) * 2.0f;
        f32 x = Cos(angle) * orbit;
        f32 y = 2.5f + Sin(angle * 1.7f + fi) * 1.25f;
        f32 z = Sin(angle) * orbit;
        f32 radius = 7.0f + (f32)(i % 3u) * 2.0f;

        g_DemoLights[lightIndex].positionRadius[0] = x;
        g_DemoLights[lightIndex].positionRadius[1] = y;
        g_DemoLights[lightIndex].positionRadius[2] = z;
        g_DemoLights[lightIndex].positionRadius[3] = radius;
        g_DemoLights[lightIndex].directionCone[0] = 0.0f;
        g_DemoLights[lightIndex].directionCone[1] = -1.0f;
        g_DemoLights[lightIndex].directionCone[2] = 0.0f;
        g_DemoLights[lightIndex].directionCone[3] = 0.0f;
        g_DemoLights[lightIndex].colorIntensity[0] = colors[i & 7u][0];
        g_DemoLights[lightIndex].colorIntensity[1] = colors[i & 7u][1];
        g_DemoLights[lightIndex].colorIntensity[2] = colors[i & 7u][2];
        g_DemoLights[lightIndex].colorIntensity[3] = 16.0f + (f32)(i % 4u) * 5.0f;
        g_DemoLights[lightIndex].type = LightType_Point;
        g_DemoLights[lightIndex].flags = LIGHT_FLAG_SHADOWED;
        g_DemoLights[lightIndex].shadowIndex = LIGHT_SHADOW_INDEX_INVALID;
        g_DemoLights[lightIndex].padding = 0u;
        numLights++;
    }
    RendererSetLights(g_DemoLights, numLights);
}


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
    UpdateDemoLights();

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
        AX_ERROR("gltf scene load failed Paladin");
        return 0;
    }
    
    s32 sponzaRes = LoadGLTFCached("Assets/Meshes/Sponza/scene.gltf", gSponza, g_RenderState.textures + gPaladin->numImages);
    if (!sponzaRes)
    {
        AX_ERROR("gltf Bistro/sponza load failed: %d ", sponzaRes);
        return 0;
    }

    if (!SceneBundleCreateAnimations(gPaladin)) return 0;
    InitAnimationInstances();

    SceneBundle* bundles[2] = { gPaladin, gSponza };
    u32 imageOffsets[2] = { 0, (u32)gPaladin->numImages };
    TextureSystem_BuildPages(bundles, imageOffsets, 2, g_RenderState.textures);
    u32 skinnedBundle = RenderSet_AddSceneBundle(&skinnedSet, gPaladin);
    u32 surfaceBundle = RenderSet_AddSceneBundle(&surfaceSet, gSponza);

    const int numCharacters = 7;
    const int charGridStride = (int)Ceilf(Sqrtf((float)numCharacters));
    for (s32 i = 0; i < numCharacters; i++)
    {
        u64 hash = MurmurHash((u64)i + 123);
        v128f pos = VecMulf(VecSetR(f32_(i % charGridStride), 0.0f, f32_(i / charGridStride), 4.0f), 1.5f);
        v128f rot = QFromAxisAngle(F3Up(), (float)(NextDouble01(hash) * 2.0 * MATH_PI));
        v128f scale = VecSet1(0.01f);
    
        if (!RenderSet_AddScene(&skinnedSet, skinnedBundle, pos, rot, scale, true))
            break;
    }

    const int numSurface = 4;
    const int surfaceGridStride = (int)Ceilf(Sqrtf((float)numSurface));
    for (s32 i = 0; i < numSurface; i++)
    {
        u64 hash = MurmurHash((u64)i + 456);
        v128f pos = VecMulf(VecSetR(0.02f+f32_(i % surfaceGridStride), -0.0f, f32_(i / surfaceGridStride) -0.0f, 0.0f), 150.0f);
        v128f rot = QIdentity(); // QFromEuler(0.0f, (float)(NextDouble01(hash) * MATH_PI), 0.0f);
        v128f scale = VecSet1(0.1f);
        if (!RenderSet_AddScene(&surfaceSet, surfaceBundle, pos, rot, scale, false))
            break;
    }
    
    InitBuffers();
    UpdateDemoLights();
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

