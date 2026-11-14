
#include <box3d/box3d.h>
#include <SDL3/SDL_gpu.h>

#include "Gameplay.h"
#include "Math/Bitpack.h"
#include "Include/Scene.h"
#include "Include/UIWindow.h"
#include "Include/AssetManager.h"
#include "Include/UIRenderer.h"
#include "Include/Camera.h"
#include "Include/Rendering.h"

typedef struct PlaneCapture
{
    b3CollisionPlane planes[16];
    b3ShapeId shape;
    int count;
} PlaneCapture; 

extern Camera g_Camera;

void SpawnCharacter(Character* ch)
{
    Scene* scene = GetActiveScene();
    u32 characterId = Scene_BundleIdx(scene, ch->bundle);
    u32 numEntity = scene->skinnedSet.numEntities;
    ch->sparse = Scene_Spawn(scene, characterId, VecSetR(0.0f, 0.0f, 0.0f, 0.f), QIdentity(), VecSet1(ch->scale), EntityFlags_None);
    ch->numEntities = scene->skinnedSet.numEntities - numEntity;
    ch->entity = EntityIDFromSparseData(scene, ch->sparse);
    u32 bundleID = Scene_AddBundleCached(scene, GetUnitRock(), "head bone");
    ch->sphereID = EntityIDFromSparseData(scene,
                    Scene_Spawn(scene, bundleID, VecZero(), QIdentity(), VecOne(), EntityFlags_None));

    EntityID entityID = EntityIDFromSparseData(scene, ch->sparse + 1);
    Entity* entity = Scene_GetEntity(scene, entityID);
    RenderSet* rs = Scene_GetEntityRenderSet(scene, entityID);
    ch->center = VecMulf(RenderSet_GroupLocalCenter(rs->primitiveGroups + EntityGetPrimitiveID(entity)), ch->scale);
    VecSetY(ch->center, 0.0f);

    ch->groundNormal    = (b3Vec3){ 0.0f,  1.0f, 0.0f };
    ch->position        = (b3Vec3){ 0.0f, 10.0f, 0.0f }; 
    ch->jumpSpeed       = 15.0f;
    ch->acceleration    = 20.0f;
    ch->decceleration   = 10.0f;
    ch->characterSpeed  = 8.0f;
    ch->verticalSpeed   = 0.0f;
    ch->yRotation       = ch->xRotation = 0.0f;
    ch->grounded = ch->active = ch->jumped = false;
    
    cAnimInit(&ch->anim, scene, characterId);
}

static b3Capsule MakeMover(void)
{
    return (b3Capsule){
        .center1 = (b3Vec3){ 0.0f, -0.85f, 0.0f },
        .center2 = (b3Vec3){ 0.0f,  0.55f, 0.0f },
        .radius = 0.5f
    };
}

static b3QueryFilter MakeFilter(void)
{
    b3QueryFilter filter;
    filter.categoryBits = ~PhysBit_Player;
    filter.maskBits = ~0;
    filter.id = 0;
    filter.name = NULL;
    return filter;
}

static bool CapturePlaneFcn( b3ShapeId shapeId, const b3PlaneResult* planes, int planeCount, void* context )
{
    PlaneCapture* capture = context;
    capture->shape = shapeId;
    for (s32 i = 0; i < planeCount && capture->count < 16; ++i )
    {
        capture->planes[capture->count++] = (b3CollisionPlane){
            .plane        = planes[i].plane,
            .pushLimit    = 0.1f,
            .push         = 0.0f,
            .clipVelocity = true
        };
    }
    return true;
}

static bool FindGround(const PlaneCapture* capture, const Gameplay* gp, b3Vec3* normal)
{
    bool found = false;
    f32 best = gp->groundMinNormalY;
    for (s32 i = 0; i < capture->count; ++i)
    {
        b3Vec3 n = capture->planes[i].plane.normal;
        if (n.y >= best)
        {
            best = n.y;
            *normal = n;
            found = true;
        }
    }
    return found;
}

static void CharacterMovement(Scene* scene, const Gameplay* gp, Character* ch, Entity* character, f32 dt)
{
    if (!ch->active) return;
    b3WorldId worldId = Physics_GetWorld();
    b3Capsule mover = MakeMover();
    b3QueryFilter filter = MakeFilter();

    float2 axis = GetMovementAxis();
    f32 axisLen = F2Len(axis);
    if (axisLen > 1.0f) axis = F2MulF(axis, 1.0f / axisLen);
    b3Vec3 forward = Float3ToB3Vec3(g_Camera.front);
    forward.y = 0.0f;
    forward = b3Normalize(forward);
    b3Vec3 right = Float3ToB3Vec3(g_Camera.right);
    right.y = 0.0f;
    right = b3Normalize(right);

    b3Vec3 wish = b3Add(b3MulSV(axis.y, forward), b3MulSV(axis.x, right));
    f32 wishLength = b3Length(wish);
    if (wishLength > 1.0f)
    {
        wish = b3MulSV(1.0f / wishLength, wish);
        wishLength = 1.0f;
    }
    if (ch->grounded && wishLength > 0.0f)
    {
        wish = b3MulSV(wishLength, b3Normalize(b3ProjectOnPlane(wish, ch->groundNormal)));
    }

    if (ch->grounded) ch->verticalSpeed = 0.0f;
    else ch->verticalSpeed += gp->gravity * dt;

    bool jumpNow = false;
    if (ch->grounded && ch->anim.jumpRequested)
    {
        ch->verticalSpeed = ch->jumpSpeed;
        ch->grounded = false;
        jumpNow = true;
    }
    ch->anim.jumpRequested = false;
    f32 speedTarget = Absf32(F2Len(axis) - MATH_Epsilon);
    f32 runSpeed = Maxf32((f32)GetKeyDown(SDLK_LSHIFT), GetGamepadAxis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER));
    if (runSpeed > 0.1f) speedTarget *= 1.0f + runSpeed;
    ch->currentSpeed = MoveTowards(ch->currentSpeed, speedTarget, dt * 5.0f); // SmoothDamp(ch->currentSpeed, axis.y, &ch->smoothVel, smoothTime, 2.0f, dt);
    
    b3Vec3 velocity = b3MulSV(ch->currentSpeed * ch->characterSpeed, wish);
    velocity.y += ch->verticalSpeed;

    b3Vec3 target = b3Add(ch->position, b3MulSV(dt, velocity));
    b3Vec3 start = ch->position;

    for (s32 iteration = 0; iteration < 5; ++iteration)
    {
        b3Pos origin = ch->position;

        PlaneCapture capture = { 0 };
        b3World_CollideMover(worldId, origin, &mover, filter, CapturePlaneFcn, &capture);

        b3Vec3 remaining = b3Sub(target, ch->position);
        b3PlaneSolverResult result = b3SolvePlanes(remaining, capture.planes, capture.count);

        f32 fraction = b3World_CastMover(worldId, origin, &mover, result.delta, filter, NULL, NULL);
        b3Vec3 delta = b3MulSV(fraction, result.delta);
        ch->position = b3Add(ch->position, delta);
        if (b3LengthSquared(delta) < Sqrf(gp->moveTolerance)) break;
    }

    ch->velocity = b3Sub(ch->position, start);
    if (!jumpNow && ch->verticalSpeed > 0.0f)
    {
        f32 intended = ch->verticalSpeed * dt;
        f32 actual = ch->position.y - start.y;
        if (actual < intended * 0.5f) ch->verticalSpeed = 0.0f;
    }

    bool wasGrounded = ch->grounded;
    b3Vec3 normal = { 0.0f, 1.0f, 0.0f };
    ch->grounded = ch->verticalSpeed <= 0.5f;
    PlaneCapture capture = { 0 };
    if (ch->grounded)
    {
        b3World_CollideMover(worldId, ch->position, &mover, filter, CapturePlaneFcn, &capture);
        ch->grounded = FindGround(&capture, gp, &normal);
    }

    if (!ch->grounded && wasGrounded && ch->verticalSpeed <= 0.0f)
    {
        b3Pos origin = ch->position;
        b3Vec3 down = { 0.0f, -gp->snapDistance, 0.0f };
        f32 fraction = b3World_CastMover(worldId, origin, &mover, down, filter, NULL, NULL);
        if (fraction < 1.0f)
        {
            b3Vec3 newPos = b3Add(ch->position, b3MulSV(fraction, down));
            ch->grounded = FindGround(&capture, gp, &normal);
        }
    }
    
    ch->groundNormal = ch->grounded ? normal : (b3Vec3){ 0.0f, 1.0f, 0.0f };

    Quaternion mRotation;
    if (g_Camera.mode == CameraMode_TPS)
    {
        f32 x = g_Camera.yaw * -MATH_DegToRad;
        f32 inputAngle = ATan2(axis.y, -axis.x);
        f32 movementValue = F2LenSq(axis);
        if (movementValue < 0.001f) inputAngle = 0.0f;
        x += -inputAngle;
    
        if (movementValue > 0.1f)
        {
            const f32 smoothingRate = 8.0f;
            f32 interp = 1.0f - Expf(-smoothingRate * dt);
            ch->yRotation = LerpAngle(ch->yRotation, x + MATH_PI, interp);
        }
        ch->xRotation = MoveTowards(ch->xRotation, axis.x * 0.1f, dt);
        mRotation = QFromYAngle(ch->yRotation);
        mRotation = QMul(mRotation, QFromAxisAngleV(B3VecTov128f(forward), ch->xRotation));
    }
    else
    {
        mRotation = QFromYAngle(g_Camera.yaw * -MATH_DegToRad + gp->cameraXRotation);
    }

    v128f position = VecAdd(Vec3Load(&ch->position.x), VecSetR(0.0f, -1.3f, 0.0f, 0.0f));
    position = VecSub(position, QMulVec3V(ch->center, mRotation));

    for (s32 i = 0; i < ch->numEntities; i++)
    {
        Entity* mesh = &scene->skinnedSet.entities[ch->sparse[i].id];
        EntitySetPositionV(mesh, position);
        EntitySetRotation(mesh, mRotation);
    }
    if (jumpNow) cPlayOnce(&ch->anim, ch->anim.jump, 0.05f, 1.0f, false);
    g_Camera.position = F3Add(B3VecToFloat3(ch->position), QMulVec3(gp->cameraOffset, mRotation));
    g_Camera.position = F3Add(M44ExtractPosition(gp->boneMat), g_Camera.position);
    g_Camera.target = g_Camera.position;
}

static void CharacterUI(Character* ch, Gameplay* gp)
{
    static bool open = true;
    if (UIBeginWindowId(CLAY_ID("Character"), "Character",
                        (float2) { 1400.0f, 40.0f }, (float2) { 500.0f, 260.0f }, 
                        &open, UIWindowFlags_NoClose))
    {
        UIText("Character Settings");
        UIEditFloat("speed", &ch->characterSpeed, 0.0f, 100.0f, 0.2f, 2);
        UIEditFloat("jump force", &ch->jumpSpeed, 0.0f, 100.0f, 0.2f, 2);
        UIEditFloatN("cameraOffset", &gp->cameraOffset.x, 3, -4.0f, 4.0f, 3);
        UIEditFloat("cameraXRot", &gp->cameraXRotation, 0.0f, 100.0f, 0.1f, 3);
        UIReadonlyRow(CLAY_ID("CharVelocityR"), "velocity", UIFmtV3(&ch->velocity.x, 3));
        f32 sqr = b3LengthSquared(ch->velocity);
        UIReadonlyRow(CLAY_ID("CharSpeedR"), "speed", UIFmtF(Sqrtf(sqr + 0.001f), 3));

        UIEndWindow();
    }
}

void UpdateCharacter(Gameplay* gp, Entity* character)
{
    Character* ch = &gp->character;
    f32 dt = GetDeltaTime();
    CharacterMovement(gp->scene, gp, ch, character, dt);
    cAnimate(gp->scene, ch, dt);
    CharacterUI(ch, gp);

    Entity* sphereEntity = Scene_GetEntity(gp->scene, ch->sphereID);
    if (sphereEntity)
    {
        v128f neckPos = VecAdd(EntityGetPosV(character), M44ExtractPositionV(gp->boneMat));
        // EntitySetPositionV(sphereEntity, neckPos);
        EntitySetScaleV(sphereEntity, VecSet1(0.07f));
        LightGPU light;
        MemsetZero(&light, sizeof(LightGPU));
        neckPos = VecAddf(neckPos, 3.4f); 
        Vec3Store(light.positionRadius, neckPos);
        light.positionRadius[3] = 7.0f;
        f32 directionCone[4] = { 0.0f, -1.0f, 0.0f, 0.0f };
        f32 color[3] = { 1.0f, 0.9f, 0.75f };
        LightGPU_SetDirectionCone(&light, directionCone);
        LightGPU_SetColor3(&light, color);
        LightGPU_SetIntensity(&light, 16.5f);
        light.type = LightType_Point;
        light.flags = LIGHT_FLAG_SHADOWED;
        light.shadowIndex = 0;
        RendererSetLights(&light, 1);
    }
}