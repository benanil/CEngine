#include "Include/Scene.h"
#include "Include/Graphics.h"
#include "Include/Rendering.h"
#include "Include/Camera.h"
#include "Include/UIWindow.h"
#include "Include/Platform.h"
#include "Math/Color.h"
#include "EditorInternal.h"

#define SCENE_LIGHT_GIZMO_RADIUS 10.0f

s32  sceneSelectedLight  = -1;
static bool sceneLightsOpen     = true;
static bool sceneLightGizmoDragging;
static f32  sceneLightGizmoDepth;

static void SceneAddLight(Scene* scene, u32 type)
{
    if (scene->numLights >= MAX_SCENE_LIGHTS)
    {
        AX_WARN("maximum scene lights reached: %d", MAX_SCENE_LIGHTS);
        return;
    }
    LightGPU* light = &scene->lights[scene->numLights];
    MemsetZero(light, sizeof(*light));
    light->positionRadius[1] = 4.0f;
    light->positionRadius[3] = 12.0f;
    f32 directionCone[4] = { 0.0f, -1.0f, 0.0f, type == LightType_Spot ? 0.72f : 0.0f };
    f32 color[3] = { 1.0f, 1.0f, 1.0f };
    LightGPU_SetDirectionCone(light, directionCone);
    LightGPU_SetColor3(light, color);
    LightGPU_SetIntensity(light, 25.0f);
    light->type = type;
    light->flags = LIGHT_FLAG_SHADOWED;
    light->shadowIndex = LIGHT_SHADOW_INDEX_INVALID;
    sceneSelectedLight = (s32)scene->numLights++;
}

static bool SceneLightWorldToScreen(Camera* camera, const LightGPU* light, float2* outScreen, f32* outDepth)
{
    if (!camera || !light || camera->viewportSize.x <= 0 || camera->viewportSize.y <= 0) return false;

    mat4x4 viewProj = M44Multiply(camera->view, camera->projection);
    v128f world = VecSetR(light->positionRadius[0], light->positionRadius[1], light->positionRadius[2], 1.0f);
    v128f clip = Vec4Transform(world, viewProj.r);
    f32 w = VecGetW(clip);
    if (w <= 0.001f) return false;

    v128f ndc = VecDivf(clip, w);
    f32 x = (VecGetX(ndc) * 0.5f + 0.5f) * (f32)camera->viewportSize.x;
    f32 y = (0.5f - VecGetY(ndc) * 0.5f) * (f32)camera->viewportSize.y;
    if (x < -SCENE_LIGHT_GIZMO_RADIUS || y < -SCENE_LIGHT_GIZMO_RADIUS ||
        x > (f32)camera->viewportSize.x + SCENE_LIGHT_GIZMO_RADIUS ||
        y > (f32)camera->viewportSize.y + SCENE_LIGHT_GIZMO_RADIUS)
        return false;

    *outScreen = (float2){ x, y };
    if (outDepth)
    {
        v128f camPos = VecLoad(&camera->position.x);
        v128f normal = Vec3NormV(VecLoad(&camera->front.x));
        *outDepth = Maxf32(Vec3DotfV(VecSub(world, camPos), normal), 0.001f);
    }
    return true;
}

static u32 SceneLightGizmoColor(const LightGPU* light)
{
    f32 color[3];
    LightGPU_GetColor3(light, color);
    return 0xCC000000u | PackColor3PtrToUint(color);
}

static bool SceneLightPlaneHit(Camera* camera, float2 mouse, f32 depth, v128f* outHit)
{
    RayV ray = ScreenPointToRay(camera, mouse);
    v128f camPos = VecLoad(&camera->position.x);
    v128f normal = Vec3NormV(VecLoad(&camera->front.x));
    v128f planePoint = VecAdd(camPos, VecMulf(normal, depth));
    return RayPlaneHit(ray.origin, ray.dir, planePoint, normal, outHit);
}

bool EditorLightGizmoUpdate(Camera* camera)
{
    Scene* scene = GetActiveScene();
    if (!scene || scene->numLights == 0u) return false;

    float2 mouse = EditorSceneMouse();
    f32 mx = mouse.x, my = mouse.y;

    if (sceneLightGizmoDragging)
    {
        if (!GetMouseDown(MouseButton_Left))
        {
            sceneLightGizmoDragging = false;
            return true;
        }
        if (sceneSelectedLight >= 0 && sceneSelectedLight < (s32)scene->numLights)
        {
            v128f hit;
            if (SceneLightPlaneHit(camera, mouse, sceneLightGizmoDepth, &hit))
            {
                LightGPU* light = &scene->lights[sceneSelectedLight];
                light->positionRadius[0] = VecGetX(hit);
                light->positionRadius[1] = VecGetY(hit);
                light->positionRadius[2] = VecGetZ(hit);
            }
        }
        return true;
    }

    if (!GetMousePressed(MouseButton_Left) || !EditorSceneInteractAllowed()) return false;

    for (s32 i = (s32)scene->numLights - 1; i >= 0; i--)
    {
        float2 screen;
        f32 depth;
        if (!SceneLightWorldToScreen(camera, &scene->lights[i], &screen, &depth)) continue;
        f32 dx = mx - screen.x;
        f32 dy = my - screen.y;
        if (dx * dx + dy * dy <= SCENE_LIGHT_GIZMO_RADIUS * SCENE_LIGHT_GIZMO_RADIUS)
        {
            sceneSelectedLight = i;
            sceneLightGizmoDragging = true;
            sceneLightGizmoDepth = depth;
            EditorGizmoClear();
            return true;
        }
    }
    return false;
}

void DrawSceneLightGizmos(Camera* camera)
{
    Scene* scene = GetActiveScene();
    if (!scene) return;

    float2 origin = EditorSceneViewOrigin();
    bool sceneView = EditorSceneViewActive();
    for (u32 i = 0u; i < scene->numLights; i++)
    {
        float2 screen;
        if (!SceneLightWorldToScreen(camera, &scene->lights[i], &screen, NULL)) continue;
        screen = F2Add(screen, origin);
        // in scene view mode the icons draw after the window quads, skip the spots
        // where another window covers the view so they don't bleed over it
        if (sceneView && !EditorSceneViewPointVisible(screen)) continue;
        f32 radius = (s32)i == sceneSelectedLight ? SCENE_LIGHT_GIZMO_RADIUS + 3.0f : SCENE_LIGHT_GIZMO_RADIUS;
        UIPushCircle(screen, radius, (s32)i == sceneSelectedLight ? 0xFFFFFFFFu : 0xAA000000u);
        UIPushCircle(screen, SCENE_LIGHT_GIZMO_RADIUS - 2.0f, SceneLightGizmoColor(&scene->lights[i]));
    }
}

static void SceneRemoveLight(Scene* scene, u32 lightIdx)
{
    if (lightIdx >= scene->numLights) return;
    for (u32 i = lightIdx + 1; i < scene->numLights; i++)
        scene->lights[i - 1] = scene->lights[i];
    scene->numLights--;
    if (sceneSelectedLight >= (s32)scene->numLights) sceneSelectedLight = (s32)scene->numLights - 1;
    if (scene->numLights == 0u)
        RendererSetLights(NULL, 0u); // clear the renderer's copy, submit skips empty scenes
}

static Clay_String SceneLightLabel(const LightGPU* light, u32 lightIdx)
{
    const char* kind = light->type == LightType_Spot ? "Spot " : "Point ";
    u32 len = (u32)StringLength(kind);
    return (Clay_String) { .isStaticallyAllocated = true, .length = (s32)len, .chars = kind };
}

void SceneLightsUI(Scene* scene)
{
    sceneLightsOpen ^= UICollapsingHeader(CLAY_ID("SceneLightsHeader"), CLAY_STRING("Lights"), sceneLightsOpen);
    if (!sceneLightsOpen) return;

    CLAY(CLAY_ID("SceneLightButtons"), {
        .layout = {
            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(30.0f) },
            .childGap = 8,
            .layoutDirection = CLAY_LEFT_TO_RIGHT
        }
    }) {
        UIPushFloatAdd(UIFloat_TextScale, -0.15f);
        if (UIButton("Add Point", (Clay_Dimensions){ 100.0f, 26.0f }, false))
            SceneAddLight(scene, LightType_Point);
        if (UIButton("Add Spot", (Clay_Dimensions){ 100.0f, 26.0f }, false))
            SceneAddLight(scene, LightType_Spot);
        if (sceneSelectedLight >= 0 && sceneSelectedLight < (s32)scene->numLights &&
            UIButton("Delete", (Clay_Dimensions){ 90.0f, 26.0f }, false))
            SceneRemoveLight(scene, (u32)sceneSelectedLight);
        UIPopFloat(UIFloat_TextScale);
    }

    for (u32 i = 0u; i < scene->numLights; i++)
    {
        Clay_ElementId id = Clay_GetElementIdWithIndex(CLAY_STRING("SceneLightRow"), i);
        u32 flags = UITreeNodeFlags_Leaf;
        if ((s32)i == sceneSelectedLight) flags |= UITreeNodeFlags_Selected;
        bool selected = false;
        UITreeNode(id, SceneLightLabel(&scene->lights[i], i), 1u, flags, false, &selected);
        if (selected) sceneSelectedLight = (s32)i;
    }

    if (sceneSelectedLight < 0 || sceneSelectedLight >= (s32)scene->numLights) return;
    LightGPU* light = &scene->lights[sceneSelectedLight];

    UIEditFloatN("Position", light->positionRadius, 3u, -10000.0f, 10000.0f, 2);
    UISliderFloatValue("Radius", &light->positionRadius[3], 0.1f, 200.0f, 1);
    if (light->type == LightType_Spot)
    {
        f32 directionCone[4];
        LightGPU_GetDirectionCone(light, directionCone);
        bool directionChanged = UIEditFloatN("Direction", directionCone, 3u, -1.0f, 1.0f, 2);
        directionChanged |= UISliderFloatValue("Cone", &directionCone[3], 0.0f, 0.99f, 2);
        if (directionChanged) LightGPU_SetDirectionCone(light, directionCone);
    }

    f32 color[3];
    LightGPU_GetColor3(light, color);
    if (UIColorEdit3("Color", color))
        LightGPU_SetColor3(light, color);

    f32 intensity = LightGPU_GetIntensity(light);
    if (UISliderFloatValue("Intensity", &intensity, 0.0f, 200.0f, 1))
        LightGPU_SetIntensity(light, intensity);

    bool shadowed = (light->flags & LIGHT_FLAG_SHADOWED) != 0u;
    UICheckbox("Shadowed", &shadowed);
    light->flags = shadowed ? LIGHT_FLAG_SHADOWED : 0u;
}