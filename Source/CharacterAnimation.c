#include "Gameplay.h"
#include "Include/Camera.h"
#include "Include/Platform.h"

typedef struct LocomotionPose_
{
    u32 anim0;
    u32 anim1;
    f32 blend;
} LocomotionPose;

extern Camera g_Camera;

purefn f32 MoveTowardsS(f32 current, f32 target, f32 maxDelta)
{
    f32 diff = target - current;
    if (Signf(current) != Signf(target))
        current *= 1.0f - (maxDelta * 2.0f); // if sign is different move faster
    if (Absf32(diff) <= maxDelta) return target;
    return current + Signf(diff) * maxDelta;
}

static void GetUpperJoints(u32 joints[3], const SceneBundle* characterBundle)
{
    s32 numNodes = Mins32(characterBundle->numNodes, 96);
    joints[0] = joints[1] = joints[2] = 0u;

    for (s32 i = 1; i < numNodes; i++) // 0 is root node skip it
    {
        const ANode* node = &characterBundle->nodes[i];
        bool upper = StringContains(node->name, "Head")
            || (StringContains(node->name, "Arm") && !StringContains(node->name, "Armature"))
            || StringContains(node->name, "Shoulder")
            || StringContains(node->name, "Hand")
            || StringContains(node->name, "Neck")
            || StringContains(node->name, "Spine");
        BitsetSet32Val(joints, i, (u32)upper);
    }
}

void cAnimInit(cAnim* an, const Scene* scene, u32 characterId)
{
    const SceneBundle* bundle = scene->bundleRefs[characterId].bundle;
    const u16* nodeToJoint = bundle->skins[0].nodeToJoint;

    an->invBind = (const mat4x4 *)bundle->skins[0].inverseBindMatrices;
    an->animAlloc = &scene->bundleRefs[characterId].animAlloc;
    an->clips = scene->animSystem.animData;
    an->jointOffset = an->animAlloc->skinSlot * MAX_BONES;

    an->headJoint      = an->jointOffset + nodeToJoint[Maxs32(FindANodeFromName(bundle, "Head"), 0)];
    an->rightHandJoint = an->jointOffset + nodeToJoint[Maxs32(FindANodeFromName(bundle, "RightHand"), 0)];
    GetUpperJoints(an->upperJoints, bundle);

    an->jumpRequested = false;
    u16 animOffset = an->animAlloc->animOffset;
    an->idle         = animOffset + FindAnimIndex(bundle, "BreathingIdle0");
    an->walk         = animOffset + FindAnimIndex(bundle, "Walk");
    an->jogRun       = animOffset + FindAnimIndex(bundle, "Run");
    an->atack        = animOffset + FindAnimIndex(bundle, "Shash");
    an->lookAround   = animOffset + FindAnimIndex(bundle, "Idle");
    an->jump         = animOffset + FindAnimIndex(bundle, "Jump");
    an->strafeLeft   = animOffset + FindAnimIndex(bundle, "StrafeLeft");
    an->strafeRight  = animOffset + FindAnimIndex(bundle, "StrafeRight");
    an->jogLeft      = animOffset + FindAnimIndex(bundle, "JogStrafeLeft");
    an->jogRight     = animOffset + FindAnimIndex(bundle, "JogStrafeRight");
    
    an->x = an->y = an->horizontalLook = 0.0f;
    an->trigerAnim = an->lastAnimation = an->idle;
    an->mixInTime  = an->onceTime    = 0.0f;
    an->mixSpeed   = an->onceSpeed   = 1.0f;
    an->animSpeed  = 1.5f;
    an->onceStart  = TimeSinceStartup();
    MemSet(an->anim1Joints, 0xFF, sizeof(u32) * 3);   
}

void cPlayOnce(cAnim* an, u16 anim, f32 mixInTime, f32 speed, bool upperLower)
{
    an->trigerAnim = anim;
    an->mixInTime  = 0.999f;
    an->onceStart  = TimeSinceStartup();
    an->mixSpeed   = 1.0f / mixInTime;
    an->upperLower = upperLower;
    an->onceSpeed  = speed;
    if (an->upperLower) MemCopy(an->anim1Joints, an->upperJoints, sizeof(u32) * 3);
    else MemSet(an->anim1Joints, 0xFF, sizeof(u32) * 3);
}

static LocomotionPose EvaluateLocomotion(cAnim* an, f32 dt)
{
    bool isBack = an->backward;
    f32 lateral = an->x;
    f32 s = Clampf32(an->y, 0.0f, 2.0f);
    bool running = s > 1.0f;
    f32 t = running ? s - 1.0f : s;
    LocomotionPose pose;
    pose.anim0 = running ? an->walk : an->idle;
    pose.anim1 = running ? an->jogRun : an->walk;
    pose.blend = t;

    if (g_Camera.mode != CameraMode_FPS) return pose;

    u32 forward = pose.anim1;
    u16 jog[2]  = { an->jogLeft   , an->jogRight };
    u16 walk[2] = { an->strafeLeft, an->strafeRight };
    
    u32 left  = running ? jog[isBack] : walk[isBack];
    u32 right = running ? jog[1-isBack] : walk[1-isBack];

    if (t > 0.85f)
    {
        pose.anim0 = forward;
        pose.anim1 = lateral < 0.0f ? left : right;
        pose.blend = Minf32(Absf32(lateral * 2.0f), 1.0f);
    }
    else if (lateral < -0.15f) pose.anim1 = left;
    else if (lateral > 0.15f) pose.anim1 = right;
    // play strafe animation when we turning our head and body
    an->horizontalLook = MoveTowardsS(an->horizontalLook, GetLookAxis().x, dt * 32.0f);
    if (s < Absf32(an->horizontalLook * 0.05f))
    {
        pose.anim1 = an->horizontalLook < 0.0f ? left : right;
        pose.blend = Minf32(Absf32(an->horizontalLook * 0.1f), 1.0f);
    }
    return pose;
}

static u32 DominantClip(const LocomotionPose* pose)
{
    return pose->blend < 0.5f ? pose->anim0 : pose->anim1;
}

static void AdvanceLocomotionPhases(cAnim* an, const LocomotionPose* pose, f32 dt)
{
    const GPUAnimationData* clips = an->clips;
    f32 d0 = clips[pose->anim0].duration;
    f32 d1 = clips[pose->anim1].duration;
    f32 duration = pose->anim0 == an->idle ? d1 : d0 + (d1 - d0) * pose->blend;
    an->locoPhase = Fractf32(an->locoPhase + dt * an->animSpeed / duration);
    an->idlePhase = Fractf32(an->idlePhase + dt * an->animSpeed / clips[an->idle].duration);
}

static f32 LocomotionClipPhase(const cAnim* an, u32 clip)
{
    return clip == an->idle ? an->idlePhase : an->locoPhase;
}

static void AnimDirections(Character* ch, f32 dt)
{
    float2 moveDir = GetMovementAxis();
    b3Vec3 velocity = ch->velocity;
    f32 lenSq = (velocity.x * velocity.x) + (velocity.z * velocity.z);
    f32 targetSpeed = (lenSq > MATH_Epsilon ? Sqrtf(lenSq) : 0.0f) / (ch->characterSpeed * dt);
    f32 acceleration = targetSpeed < ch->anim.y ? 8.0f : 32.0f;
    if (!ch->grounded) acceleration *= 0.4f;
    ch->anim.y = MoveTowardsS(ch->anim.y, targetSpeed, acceleration * dt);
    ch->anim.x = MoveTowardsS(ch->anim.x, moveDir.x, dt * 8.0f);
    ch->anim.backward = moveDir.y < -0.02f;
}

void cAnimate(Scene* scene, Character* ch, f32 dt)
{
    f32 now = TimeSinceStartup();
    cAnim* an = &ch->anim;
    const GPUAnimationData* clips = an->clips;
    GPUAnimationInstance instance = { 0 };
    MemCopy(instance.activeBoneBitset, an->anim1Joints, sizeof(u32) * 3);
    
    AnimDirections(ch, dt);
    LocomotionPose loco = EvaluateLocomotion(an, dt);
    AdvanceLocomotionPhases(an, &loco, dt);

    u32 anim0 = loco.anim0;
    u32 anim1 = loco.anim1;
    f32 blend = loco.blend;
    f32 anim0t = LocomotionClipPhase(an, anim0);
    f32 anim1t = an->locoPhase;

    u32 baseAnim = DominantClip(&loco);
    f32 baseTime = LocomotionClipPhase(an, baseAnim);

    if (an->mixInTime > 0.0f)
    {
        anim0  = baseAnim;
        anim0t = baseTime;
        anim1  = an->trigerAnim;
        anim1t = 0.0f;

        an->mixInTime = Maxf32(0.0f, an->mixInTime - dt * an->mixSpeed);
        blend = 0.999f - an->mixInTime;

        if (an->mixInTime <= 0.0f)
        {
            an->onceStart = now;
            an->onceTime = clips[an->trigerAnim].duration;
        }
    }
    else if (an->onceTime > 0.0f)
    {
        f32 duration = clips[an->trigerAnim].duration;
        f32 t = Clampf32(((now - an->onceStart) * an->onceSpeed) / duration, 0.0f, 0.999f);

        anim0  = baseAnim;
        anim0t = baseTime;
        anim1  = an->trigerAnim;
        anim1t = t;
        blend  = 0.999f;

        an->onceTime -= dt * an->onceSpeed;
        if (an->onceTime <= 0.0f)
            an->mixOutTime = 1.0f;
    }
    else if (an->mixOutTime > 0.0f)
    {
        anim0  = baseAnim;
        anim0t = baseTime;
        anim1  = an->trigerAnim;
        anim1t = 0.999f;

        an->mixOutTime = Maxf32(0.0f, an->mixOutTime - (dt * an->mixSpeed));
        blend = an->mixOutTime;

        if (an->mixOutTime <= 0.0f)
            MemSet(an->anim1Joints, 0xFF, sizeof(u32) * 3);
    }

    u32 w = (u32)(Clampf32(blend, 0.0f, 1.0f) * 255.0f);
    instance.weightsUnorm8x4  = 255 - w; 
    instance.weightsUnorm8x4 |= w << 8; // anim1 weight
    
    instance.ratioUnorm16x4[0] = (u32)PackUnorm16(anim0t) | ((u32)PackUnorm16(anim1t) << 16);
    instance.Idx8x4            = anim0 | (anim1 << 8);

    u32 charSparse = SparseIDFromSparseData(&scene->skinnedSet, ch->sparse);
    for (s32 i = 0; i < ch->numCharacterEntities; i++)
    {
        AnimationSystem_SetInstance(&scene->animSystem, charSparse + i, instance);
    }
}