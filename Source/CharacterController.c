#include <box3d/box3d.h>
#include <SDL3/SDL_gpu.h>

#include "Gameplay.h"
#include "Include/Scene.h"
#include "Include/Camera.h"

extern Camera g_Camera;

static void GetCharacterSizeInfo(Character* ch, Scene* scene, Entity* entity)
{
    RenderSet* rs = &scene->skinnedSet;
    u32 bundleId = EntityGetBundleId(rs, entity);
    Range range = rs->bundlePrimRange[bundleId];
    const SceneBundle* bundle = rs->bundles[bundleId];
    const ANode* rootNode = &bundle->nodes[1];
    const AMesh* mesh = &bundle->meshes[rootNode->index];
    u32 localId = rootNode->index + range.start;
    const PrimitiveGroup* pg = &rs->primitiveGroups[localId];
    ch->size = VecMulf(RenderSet_GroupLocalSize(pg), ch->scale);
}

static b3Capsule MakeMover(v128f size, f32 radius)
{
    v128f up = VecSetR(0.0f, 1.0f, 0.0f, 0.0f);
    radius = Minf32(radius, VecGetY(size) * 0.5f);
    return (b3Capsule){
        .center1 = v128fToB3Vec3(VecMulf(up, radius)),
        .center2 = v128fToB3Vec3(VecMulf(up, VecGetY(size) - radius)),
        .radius  = radius
    };
}

void SpawnCharacter(Character* ch, Scene* scene, Entity* entity, u32 bundleId)
{
    GetCharacterSizeInfo(ch, scene, entity);
    ch->groundNormal  = (b3Vec3){ 0.0f,  1.0f, 0.0f };
    ch->position      = (b3Vec3){ 0.0f, 10.0f, 0.0f };
    ch->velocity      = (b3Vec3){ 0.0f,  0.0f, 0.0f };
    ch->jumpSpeed     = 15.0f;
    ch->acceleration  = 20.0f;
    ch->decceleration = 10.0f;
    ch->speed         = 8.0f;
    ch->verticalSpeed = 0.0f;
    ch->currentSpeed  = 0.0f;
    ch->yRotation     = ch->xRotation = 0.0f;
    ch->grounded = ch->active = ch->jumped = false;
    cAnimInit(&ch->anim, scene, bundleId);

    DynamicMoverDef def = DefaultDynamicMoverDef();
    def.position = ch->position;
    def.capsule  = MakeMover(ch->size, VecGetZ(ch->size));
    def.filter.categoryBits = PhysBit_Player;
    def.filter.maskBits     = ~0ull;
    def.jumpSpeed  = ch->jumpSpeed;
    def.maxSpeed   = ch->speed;
    def.accelerate = ch->acceleration;
    def.friction   = ch->decceleration;

    DynamicMover_Init(&ch->mover);
    DynamicMover_Create(&ch->mover, Physics_GetWorld(), &def);
}

void Character_Destroy(Character* ch)
{
    DynamicMover_Destroy(&ch->mover);
}

void CharacterMovement(Gameplay* gp, Entity* character, f32 dt)
{
    Character* ch = &gp->character;
    if (!ch->active) return;

    float2 axis = GetMovementAxis();
    f32 axisLen = F2Len(axis);
    if (axisLen > 1.0f) axis = F2MulF(axis, 1.0f / axisLen);

    b3Vec3 forward = Float3ToB3Vec3(g_Camera.front);
    forward.y = 0.0f;
    forward = b3Normalize(forward);
    b3Vec3 right = Float3ToB3Vec3(g_Camera.right);
    right.y = 0.0f;
    right = b3Normalize(right);

    f32 speedTarget = Absf32(axisLen - MATH_Epsilon);
    f32 runSpeed = Maxf32((f32)GetKeyDown(SDLK_LSHIFT), GetGamepadAxis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER));
    f32 speedScale = 1.0f;
    if (runSpeed > 0.1f)
    {
        speedScale = 1.0f + runSpeed;
        speedTarget *= speedScale;
    }
    ch->currentSpeed = MoveTowards(ch->currentSpeed, speedTarget, dt * 5.0f);

    bool jumpNow = false;
    if (ch->grounded && ch->anim.jumpRequested)
    {
        jumpNow = DynamicMover_Jump(&ch->mover);
        if (jumpNow) cPlayOnce(&ch->anim, ch->anim.jump, 0.1f, 1.0f, false);
    }
    ch->anim.jumpRequested = false;
    ch->jumped = jumpNow;

    DynamicMover_Update(&ch->mover, dt, forward, right, (b3Vec2){ axis.y, axis.x }, speedScale);

    Quaternion mRotation;
    if (g_Camera.mode == CameraMode_TPS)
    {
        f32 x = g_Camera.yaw * -MATH_DegToRad;
        f32 inputAngle = ATan2(axis.y, -axis.x);
        if (IsNanF32(inputAngle)) inputAngle = 0.0f;
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

    ch->rotation = mRotation;
}

void CharacterApplyTransform(Gameplay* gp)
{
    Scene* scene = gp->scene;
    Character* ch = &gp->character;
    if (!ch->active) return;

    b3Vec3 previous = ch->position;
    ch->position = b3Body_GetPosition(DynamicMover_GetBodyId(&ch->mover));
    ch->velocity = b3Body_GetLinearVelocity(ch->mover.moverId); //b3Sub(ch->position, previous);
    ch->verticalSpeed = ch->mover.velocity.y;
    ch->grounded = ch->mover.onGround;
    ch->groundNormal = (ch->grounded && ch->mover.castResult.hit) ? ch->mover.castResult.normal : (b3Vec3){ 0.0f, 1.0f, 0.0f };
    cAnimate(gp->scene, ch, GetDeltaTime());

    v128f position = Vec3Load(&ch->position.x);
    for (s32 i = 0; i < ch->numEntities; i++)
    {
        Entity* mesh = &scene->skinnedSet.entities[ch->sparse[i].id];
        EntitySetPositionV(mesh, position);
        EntitySetRotation(mesh, ch->rotation);
        EntitySetScaleV(mesh, VecSet1(ch->scale));
    }
}