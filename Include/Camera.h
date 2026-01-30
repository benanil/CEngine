#ifndef CAMERA_DEFINED
#define CAMERA_DEFINED

#include "../Math/Matrix.h"
#include "Platform.h"
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>

//#include "sokol/sokol_app.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct Camera_
{
    Matrix4 projection;
    Matrix4 view;
    
    float verticalFOV;
    float nearClip;
    float farClip;

    int2 viewportSize, monitorSize;
       
    float3 position;
    float2 mouseOld;
    float3 targetPos;
    float3 Front, Right, Up;
 
    float pitch, yaw, senstivity;
    float speed;

    bool wasPressing;
 
    FrustumPlanes frustumPlanes;

    Matrix4 inverseProjection;
    Matrix4 inverseView;
} Camera;


static inline void Camera_RecalculateProjection(Camera* camera, int width, int height)
{
    camera->viewportSize.x = width; camera->viewportSize.y = height;
    camera->projection = PerspectiveFovRH(camera->verticalFOV * MATH_DegToRad, (float)width, (float)height, camera->nearClip, camera->farClip);
    camera->inverseProjection = Matrix4Inverse(camera->projection);
}

static inline void Camera_RecalculateView(Camera* camera)
{
    camera->view = LookAtRH(camera->position, camera->Front, camera->Up);
    camera->inverseView = Matrix4Inverse(camera->view);
}

static inline void Camera_CalculateLook(Camera* camera) // from yaw pitch
{
    camera->Front.x = Cos(camera->yaw * MATH_DegToRad) * Cos(camera->pitch * MATH_DegToRad);
    camera->Front.y = Sin(camera->pitch * MATH_DegToRad);
    camera->Front.z = Sin(camera->yaw * MATH_DegToRad) * Cos(camera->pitch * MATH_DegToRad);
    camera->Front = Float3Norm(camera->Front);
    // also re-calculate the Right and Up vector
    // normalize the vectors, because their length gets closer to 0 the more you look up or down which results in slower movement.
    camera->Right = Float3NormEst(Float3Cross(camera->Front, Float3Up()));
    camera->Up = Float3Cross(camera->Right, camera->Front);
}

static inline RayV ScreenPointToRay(Camera* camera, float2 pos)
{
    float2 coord = (float2){ pos.x / (float)camera->viewportSize.x, pos.y / (float)camera->viewportSize.y };
    coord.y = 1.0f - coord.y;    // Flip Y to match the NDC coordinate system
    coord = Vec2SubF(Vec2MulF(coord, 2.0f), 1.0f); // Map to range [-1, 1]

    Vector4x32f clipSpacePos = VecSetR(coord.x, coord.y, 1.0f, 1.0f);
    Vector4x32f viewSpacePos = Vector4Transform(clipSpacePos, camera->inverseProjection.r);
    viewSpacePos = VecDiv(viewSpacePos, VecSplatW(viewSpacePos));
        
    Vector4x32f worldSpacePos = Vector4Transform(viewSpacePos, camera->inverseView.r);
    Vector4x32f rayDir = Vec3NormV(VecSub(worldSpacePos, VecLoad(&camera->position.x)));
        
    RayV ray;
    ray.origin = VecLoad(&camera->position.x); 
    ray.dir = rayDir;
    return ray;
}

static inline void CameraInit(Camera* camera, int width, int height)
{
    MemsetZero(camera, sizeof(Camera));
    camera->pitch       = 0.0f;
    camera->yaw         = 0.0f;
    camera->senstivity  = 15.0f;
    camera->verticalFOV = 65.0f;
    camera->nearClip    = 0.1f;
    camera->farClip     = 2400.0f;
    camera->speed       = 0.4f;
    camera->position.x -= 6;

    Camera_CalculateLook(camera);
    Camera_RecalculateView(camera);
    Camera_RecalculateProjection(camera, width, height);
}

static inline void InfiniteMouse(float2 point)
{
    int2 monitorSize;
    wGetMonitorSize(&monitorSize.x, &monitorSize.y);
    
    #ifndef __ANDROID__
    if (point.x > monitorSize.x - 2) SetCursorPos(3, (int)point.y);
    if (point.y > monitorSize.y - 2) SetCursorPos((int)point.x, 3);
        
    if (point.x < 2) SetCursorPos(monitorSize.x - 3, (int)point.y);
    if (point.y < 2) SetCursorPos((int)point.x, monitorSize.y - 3);
    #endif
}

static inline void CameraUpdate(Camera* camera, float dt)
{
    bool pressing = GetMouseDown(MouseButton_Right);
    float speed = dt * (1.0f + GetKeyDown(SDLK_LSHIFT) * 2.0f) * 2.0f;
    
    if (!pressing) { camera->wasPressing = false; return; }
        
    float2 mousePos;
    GetMousePos(&mousePos.x, &mousePos.y);
    float2 diff = Vec2Sub(mousePos, camera->mouseOld);
    
    SDL_Cursor* cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_MOVE);
    SDL_SetCursor(cursor);
    SDL_DestroyCursor(cursor);
        
    // if platform is android left side is for movement, right side is for rotating camera
    #ifdef __ANDROID__
    if (mousePos.x > (monitorSize.x / 2.0f))
    #endif
    {
        if (camera->wasPressing && diff.x + diff.y < 130.0f)
        {
            camera->pitch -= diff.y * dt * camera->senstivity;
            camera->yaw   += diff.x * dt * camera->senstivity;
            camera->yaw   = FModf(camera->yaw + 180.0f, 360.0f) - 180.0f;
            camera->pitch = MCLAMP(camera->pitch, -89.0f, 89.0f);
        }
        Camera_CalculateLook(camera);
    }
    #ifdef __ANDROID__
    else if (camera->wasPressing && diff.x + diff.y < 130.0f)
        camera->position += (camera->Right * diff.x * 0.02f) + (camera->Front * -diff.y * 0.02f);
    #endif  

    camera->mouseOld = mousePos;
    camera->wasPressing = true;

    if (GetKeyDown(SDLK_D)) camera->position = Float3Add(camera->position, Float3MulF(camera->Right, camera->speed));
    if (GetKeyDown(SDLK_A)) camera->position = Float3Sub(camera->position, Float3MulF(camera->Right, camera->speed));
    if (GetKeyDown(SDLK_W)) camera->position = Float3Add(camera->position, Float3MulF(camera->Front, camera->speed));
    if (GetKeyDown(SDLK_S)) camera->position = Float3Sub(camera->position, Float3MulF(camera->Front, camera->speed));
    if (GetKeyDown(SDLK_E)) camera->position = Float3Add(camera->position, Float3MulF(camera->Up, camera->speed));
    if (GetKeyDown(SDLK_Q)) camera->position = Float3Sub(camera->position, Float3MulF(camera->Up, camera->speed));

    InfiniteMouse(mousePos);
    Camera_RecalculateView(camera);
    // frustumPlanes = CreateFrustumPlanes(view * projection);
}

#if defined(__cplusplus)
}
#endif

#endif