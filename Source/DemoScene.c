
#include "Include/DemoScene.h"
#include "Include/Platform.h"
#include "Include/Graphics.h"
#include "Include/Rendering.h"
#include "Include/Random.h"
#include "Include/IntFloat.h"
#include "Math/Quaternion.h"

static Scene g_DemoScene;
static u32 g_PaladinBundle = INVALID_BUNDLE;
static u32 g_BistroBundle  = INVALID_BUNDLE;
static LightGPU g_DemoLights[12];

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

    for (u32 i = 0; i < 8u && numLights < (int)ARRAY_SIZE(g_DemoLights); i++)
    {
        u32 lightIndex = (u32)numLights;
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

s32 DemoScene_Create(void)
{
    Scene_Init(&g_DemoScene);

    g_PaladinBundle = Scene_AddBundle(&g_DemoScene, "Assets/Meshes/Paladin/Paladin.gltf", true);
    g_BistroBundle  = Scene_AddBundle(&g_DemoScene, "Assets/Meshes/Bistro/Bistro.glb", false);
    if (g_PaladinBundle == INVALID_BUNDLE || g_BistroBundle == INVALID_BUNDLE)
        return 0;

    const int numCharacters = 7;
    const int charGridStride = (int)Ceilf(Sqrtf((float)numCharacters));
    for (s32 i = 0; i < numCharacters; i++)
    {
        u64 hash = MurmurHash((u64)i + 123);
        v128f pos = VecMulf(VecSetR(f32_(i % charGridStride), 0.0f, f32_(i / charGridStride), 4.0f), 1.5f);
        v128f rot = QFromAxisAngle(F3Up(), (float)(NextDouble01(hash) * 2.0 * MATH_PI));
        v128f scale = VecSet1(0.01f);

        if (!Scene_Spawn(&g_DemoScene, g_PaladinBundle, pos, rot, scale))
            break;
    }

    const int numSurface = 4;
    const int surfaceGridStride = (int)Ceilf(Sqrtf((float)numSurface));
    for (s32 i = 0; i < numSurface; i++)
    {
        v128f pos = VecMulf(VecSetR(0.02f+f32_(i % surfaceGridStride), -0.0f, f32_(i / surfaceGridStride) -0.0f, 0.0f), 150.0f);
        v128f rot = QIdentity();
        v128f scale = VecSet1(0.1f);
        if (!Scene_Spawn(&g_DemoScene, g_BistroBundle, pos, rot, scale))
            break;
    }
    return 1;
}

void DemoScene_Update(f32 deltaTime)
{
    UpdateDemoLights();
}

Scene* DemoScene_Get(void)
{
    return &g_DemoScene;
}
