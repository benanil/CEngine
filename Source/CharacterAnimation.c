// Character animation: locomotion, turning, idle fidgets and one-shot actions.
//
// Locomotion is a grid of clips indexed by [speed row][direction]. Rows are
// idle, walk and run. Columns are an 8-way ring starting at forward and going
// clockwise. Speed picks two rows, the smoothed movement angle picks two
// adjacent columns, and the 2x2 cell gives four clips with bilinear weights.
//
// Every layer adds weighted clips into a BlendSet, which merges duplicate
// clips, so a still character uses one slot and the rest stay free:
//   - turn:   moves part of the idle weight to strafeLeft/strafeRight
//   - fidget: moves part of the idle weight to a random Idle/Idle2 clip
//   - action: owns slot 3 with its own bone mask (full body or upper body)
//
// The action layer runs a small state machine: Squeeze fades out the weakest
// locomotion clip to free slot 3, then MixIn, Play and MixOut run the action,
// and Release gives the slot back. Slot 0 is always active on every bone, so
// its weight is kept non-zero when packed for the shader.
#include "Gameplay.h"
#include "Include/Camera.h"
#include "Include/Platform.h"
#include "Math/Color.h"
#include "Include/Random.h"

#define BLEND_SLOTS          4
#define BLEND_CAPACITY       8
#define DIRECTION_RATE       12.0f
#define MOVE_AXIS_RATE       8.0f
#define SQUEEZE_RATE         12.0f
#define MOVE_INPUT_DEADZONE  0.02f
#define LOOK_TURN_RATE       32.0f
#define LOOK_TURN_GAIN       0.1f
#define LOOK_TURN_STILL      0.5f
#define WEIGHT_EPSILON       0.0001f
#define FIDGET_SPEED_LIMIT   0.02f
#define FIDGET_FADE_TIME     0.35f
#define FIDGET_GATE_RATE     6.0f
#define FIDGET_MIN_DELAY     4.0f
#define FIDGET_MAX_DELAY     9.0f

typedef struct BlendSample_
{
    u32 clip;
    f32 weight;
} BlendSample;

typedef struct BlendSet_
{
    BlendSample samples[BLEND_CAPACITY];
    u32 count;
} BlendSet;

extern Camera g_Camera;

static f32 RandomRange(u32* state, f32 lo, f32 hi)
{
    return RepeatMinMaxF32(PCG2Next(state), lo, hi);
}

static void FilterJoints(u32 joints[3], const SceneBundle* characterBundle, const char* const* nodes, s32 numNodeNames)
{
    s32 numNodes = Mins32(characterBundle->numNodes, 96);
    joints[0] = joints[1] = joints[2] = 0u;

    for (s32 i = 1; i < numNodes; i++)
    {
        const ANode* node = &characterBundle->nodes[i];
        if (StringContains(node->name, "Armature")) continue;

        bool upper = false;
        for (s32 n = 0; n < numNodeNames; n++)
            upper |= StringContains(node->name, nodes[n]);

        BitsetSet32Val(joints, i, (u32)upper);
    }
}

static void BuildLocomotionGrid(cAnim* an)
{
    const u16 walk[LOCO_DIRECTIONS] =
    {
        an->walk, an->jogRight, an->strafeRight, an->jogLeft,
        an->walk, an->jogRight, an->strafeLeft,  an->jogLeft
    };
    const u16 run[LOCO_DIRECTIONS] =
    {
        an->jogRun, an->jogRight, an->jogRight, an->jogLeft,
        an->jogRun, an->jogRight, an->jogLeft,  an->jogLeft
    };

    for (s32 d = 0; d < LOCO_DIRECTIONS; d++)
    {
        an->locomotion[0][d] = an->idle;
        an->locomotion[1][d] = walk[d];
        an->locomotion[2][d] = run[d];
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
    const char* upperBones[] = { "Head", "Arm", "Shoulder", "Hand", "Neck", "Spine" };
    FilterJoints(an->upperJoints, bundle, upperBones, ARRAY_SIZE(upperBones));

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

    BuildLocomotionGrid(an);

    const char* fidgetNames[] = { "Idle", "Idle1" };
    an->numFidgets = 0;
    for (s32 i = 0; i < (s32)ARRAY_SIZE(fidgetNames); i++)
    {
        s32 idx = (s32)FindAnimIndex(bundle, fidgetNames[i]);
        if (idx >= 0 && an->numFidgets < MAX_FIDGETS)
            an->fidgets[an->numFidgets++] = animOffset + idx;
    }

    an->rng = (u32)(TimeSinceStartup() * 1000.0f) | 1u;
    an->animSpeed = 1.5f;
    an->speed = an->dirAngle = an->lookTurn = an->locoPhase = an->idlePhase = 0.0f;
    an->moveX = an->moveY = 0.0f;

    an->fidgetClip    = an->idle;
    an->fidgetPhase   = 0.0f;
    an->fidgetGate    = 0.0f;
    an->fidgetWeight  = 0.0f;
    an->fidgetPlaying = false;
    an->fidgetTimer   = RandomRange(&an->rng, FIDGET_MIN_DELAY, FIDGET_MAX_DELAY);

    an->actionState   = ActionState_None;
    an->actionClip    = an->idle;
    an->actionWeight  = 0.0f;
    an->actionPhase   = 0.0f;
    an->actionElapsed = 0.0f;
    an->actionSpeed   = 1.0f;
    an->actionFade    = 1.0f;
    an->squeeze       = 0.0f;
    MemSet(an->actionJoints, 0xFF, sizeof(u32) * 3);
}

void cPlayOnce(cAnim* an, u16 anim, f32 mixInTime, f32 speed, bool upperLower)
{
    an->actionClip    = anim;
    an->actionSpeed   = speed;
    an->actionFade    = 1.0f / Maxf32(mixInTime, 0.001f);
    an->actionElapsed = 0.0f;
    an->actionPhase   = 0.0f;

    if (upperLower) MemCopy(an->actionJoints, an->upperJoints, sizeof(u32) * 3);
    else MemSet(an->actionJoints, 0xFF, sizeof(u32) * 3);

    an->actionState = an->actionWeight > 0.0f ? ActionState_MixIn : ActionState_Squeeze;
}

bool cAnimActionActive(const cAnim* an)
{
    return an->actionState != ActionState_None;
}

static void BlendAdd(BlendSet* set, u32 clip, f32 weight)
{
    if (weight <= 0.0f) return;
    for (u32 i = 0; i < set->count; i++)
    {
        if (set->samples[i].clip == clip)
        {
            set->samples[i].weight += weight;
            return;
        }
    }
    if (set->count < BLEND_CAPACITY)
    {
        set->samples[set->count].clip = clip;
        set->samples[set->count++].weight = weight;
    }
}

static void SortByWeight(BlendSet* set)
{
    for (u32 i = 1; i < set->count; i++)
    {
        BlendSample key = set->samples[i];
        u32 j = i;
        while (j > 0 && set->samples[j - 1].weight < key.weight)
        {
            set->samples[j] = set->samples[j - 1];
            j--;
        }
        set->samples[j] = key;
    }
}

static u32 CountSignificant(const BlendSet* set)
{
    u32 n = 0;
    for (u32 i = 0; i < set->count; i++)
        n += set->samples[i].weight > WEIGHT_EPSILON;
    return n;
}

static void UpdateMotion(Character* ch, f32 dt)
{
    cAnim* an = &ch->anim;
    b3Vec3 velocity = ch->velocity;
    f32 lenSq = (velocity.x * velocity.x) + (velocity.z * velocity.z);
    f32 targetSpeed = (lenSq > MATH_Epsilon ? Sqrtf(lenSq) : 0.0f) / (ch->characterSpeed * dt);
    f32 acceleration = targetSpeed < an->speed ? 8.0f : 32.0f;
    if (!ch->grounded) acceleration *= 0.4f;
    an->speed = MoveTowardsS(an->speed, targetSpeed, acceleration * dt);

    bool fps = g_Camera.mode == CameraMode_FPS;
    f32 inputX = 0.0f;
    f32 inputY = 0.0f;
    f32 targetLook = 0.0f;
    if (fps)
    {
        float2 move = GetMovementAxis();
        inputX = move.x;
        inputY = move.y;
        targetLook = GetLookAxis().x;
    }

    f32 axisStep = dt * MOVE_AXIS_RATE;
    an->moveX = MoveTowardsS(an->moveX, inputX, axisStep);
    an->moveY = MoveTowardsS(an->moveY, inputY, axisStep);

    f32 axisLenSq = (an->moveX * an->moveX) + (an->moveY * an->moveY);
    if (fps && axisLenSq > MOVE_INPUT_DEADZONE * MOVE_INPUT_DEADZONE)
        an->dirAngle = ATan2(an->moveX, an->moveY);
    else
        an->dirAngle = MoveTowardsAngle(an->dirAngle, 0.0f, dt * DIRECTION_RATE);

    an->lookTurn = MoveTowardsS(an->lookTurn, targetLook, dt * LOOK_TURN_RATE);
}

static void StartFidget(cAnim* an)
{
    u32 pick = PCG2Next(&an->rng) % an->numFidgets;
    if (an->numFidgets > 1 && an->fidgets[pick] == an->fidgetClip)
        pick = (pick + 1) % an->numFidgets;
    an->fidgetClip = an->fidgets[pick];
    an->fidgetPhase = 0.0f;
    an->fidgetPlaying = true;
}

static void UpdateFidget(cAnim* an, f32 dt)
{
    bool still = an->speed < FIDGET_SPEED_LIMIT
        && an->actionState == ActionState_None
        && Absf32(an->lookTurn) < LOOK_TURN_STILL;
    an->fidgetGate = MoveTowards(an->fidgetGate, still ? 1.0f : 0.0f, dt * FIDGET_GATE_RATE);

    if (!an->fidgetPlaying)
    {
        an->fidgetWeight = 0.0f;
        if (!still)
        {
            an->fidgetTimer = RandomRange(&an->rng, FIDGET_MIN_DELAY, FIDGET_MAX_DELAY);
            return;
        }
        an->fidgetTimer -= dt;
        if (an->fidgetTimer <= 0.0f && an->numFidgets > 0)
            StartFidget(an);
        return;
    }

    f32 duration = an->clips[an->fidgetClip].duration;
    an->fidgetPhase += dt * an->animSpeed / duration;

    f32 elapsed = an->fidgetPhase * duration / an->animSpeed;
    f32 remaining = (1.0f - an->fidgetPhase) * duration / an->animSpeed;
    f32 envelope = Clampf32(Minf32(elapsed, remaining) / FIDGET_FADE_TIME, 0.0f, 1.0f);
    an->fidgetWeight = envelope * an->fidgetGate;

    if (an->fidgetPhase >= 1.0f || (!still && an->fidgetGate <= 0.0f))
    {
        an->fidgetPlaying = false;
        an->fidgetWeight = 0.0f;
        an->fidgetTimer = RandomRange(&an->rng, FIDGET_MIN_DELAY, FIDGET_MAX_DELAY);
    }
}

static void SampleLocomotion(const cAnim* an, BlendSet* set)
{
    f32 s = Clampf32(an->speed, 0.0f, (f32)(LOCO_SPEED_ROWS - 1));
    u32 row = (u32)s;
    if (row > LOCO_SPEED_ROWS - 2) row = LOCO_SPEED_ROWS - 2;
    f32 rowFrac = EaseOut(s - (f32)row);

    f32 angle = an->dirAngle < 0.0f ? an->dirAngle + MATH_TwoPI: an->dirAngle;
    f32 pos = angle * ((f32)LOCO_DIRECTIONS / MATH_TwoPI);
    u32 col0 = (u32)pos;
    f32 colFrac = pos - (f32)col0;
    col0 %= LOCO_DIRECTIONS;
    u32 col1 = (col0 + 1) % LOCO_DIRECTIONS;

    const f32 rowWeights[2] = { 1.0f - rowFrac, rowFrac };
    const f32 colWeights[2] = { 1.0f - colFrac, colFrac };
    const u32 cols[2] = { col0, col1 };

    for (u32 r = 0; r < 2; r++)
        for (u32 c = 0; c < 2; c++)
            BlendAdd(set, an->locomotion[row + r][cols[c]], rowWeights[r] * colWeights[c]);
}

static void AdvanceLocomotionPhases(cAnim* an, const BlendSet* set, f32 dt)
{
    const GPUAnimClip* clips = an->clips;
    f32 weightSum = 0.0f;
    f32 durationSum = 0.0f;

    for (u32 i = 0; i < set->count; i++)
    {
        const BlendSample* sample = &set->samples[i];
        if (sample->clip == an->idle) continue;
        weightSum += sample->weight;
        durationSum += sample->weight * clips[sample->clip].duration;
    }

    if (weightSum > WEIGHT_EPSILON)
        an->locoPhase = Fractf32(an->locoPhase + dt * an->animSpeed * weightSum / durationSum);
    an->idlePhase = Fractf32(an->idlePhase + dt * an->animSpeed / clips[an->idle].duration);
}

static void ApplyTurn(const cAnim* an, BlendSet* set)
{
    if (g_Camera.mode != CameraMode_FPS) return;

    f32 turnWeight = Minf32(Absf32(an->lookTurn * LOOK_TURN_GAIN), 1.0f);
    if (turnWeight <= 0.0f) return;

    u32 clip = an->lookTurn < 0.0f ? an->strafeLeft : an->strafeRight;
    for (u32 i = 0; i < set->count; i++)
    {
        if (set->samples[i].clip != an->idle) continue;
        f32 moved = set->samples[i].weight * turnWeight;
        set->samples[i].weight -= moved;
        BlendAdd(set, clip, moved);
        return;
    }
}

static void ApplyFidget(const cAnim* an, BlendSet* set)
{
    if (!an->fidgetPlaying || an->fidgetWeight <= 0.0f) return;

    for (u32 i = 0; i < set->count; i++)
    {
        if (set->samples[i].clip != an->idle) continue;
        f32 moved = set->samples[i].weight * an->fidgetWeight;
        set->samples[i].weight -= moved;
        BlendAdd(set, an->fidgetClip, moved);
        return;
    }
}

static void UpdateAction(cAnim* an, f32 dt, u32 locoCount)
{
    f32 duration = an->clips[an->actionClip].duration;

    switch (an->actionState)
    {
        case ActionState_None:
            break;

        case ActionState_Squeeze:
            an->squeeze = locoCount < BLEND_SLOTS ? 1.0f : Minf32(an->squeeze + dt * SQUEEZE_RATE, 1.0f);
            if (an->squeeze >= 1.0f) an->actionState = ActionState_MixIn;
            break;

        case ActionState_MixIn:
            an->actionPhase = 0.0f;
            an->actionWeight = Minf32(an->actionWeight + dt * an->actionFade, 1.0f);
            if (an->actionWeight >= 1.0f)
            {
                an->actionElapsed = 0.0f;
                an->actionState = ActionState_Play;
            }
            break;

        case ActionState_Play:
            an->actionElapsed += dt * an->actionSpeed;
            an->actionPhase = Clampf32(an->actionElapsed / duration, 0.0f, 0.999f);
            if (an->actionElapsed >= duration) an->actionState = ActionState_MixOut;
            break;

        case ActionState_MixOut:
            an->actionPhase = 0.999f;
            an->actionWeight = Maxf32(an->actionWeight - dt * an->actionFade, 0.0f);
            if (an->actionWeight <= 0.0f) an->actionState = ActionState_Release;
            break;

        case ActionState_Release:
            an->squeeze = Maxf32(an->squeeze - dt * SQUEEZE_RATE, 0.0f);
            if (an->squeeze <= 0.0f) an->actionState = ActionState_None;
            break;
    }
}

static f32 SlotPhase(const cAnim* an, u32 clip)
{
    if (clip == an->idle) return an->idlePhase;
    if (an->fidgetPlaying && clip == an->fidgetClip) return Minf32(an->fidgetPhase, 0.999f);
    return an->locoPhase;
}

static GPUAnimationInstance ComposeInstance(const cAnim* an, const BlendSet* set)
{
    GPUAnimationInstance instance = { 0 };
    u32 clip[BLEND_SLOTS] = { 0 };
    f32 phase[BLEND_SLOTS] = { 0 };
    f32 weight[BLEND_SLOTS] = { 0 };

    u32 n = set->count < BLEND_SLOTS ? set->count : BLEND_SLOTS;
    for (u32 i = 0; i < n; i++)
    {
        clip[i] = set->samples[i].clip;
        weight[i] = set->samples[i].weight;
    }
    if (n == BLEND_SLOTS) weight[BLEND_SLOTS - 1] *= 1.0f - an->squeeze;

    f32 total = 0.0f;
    for (u32 i = 0; i < n; i++) total += weight[i];
    f32 inv = total > MATH_Epsilon ? 1.0f / total : 0.0f;
    for (u32 i = 0; i < n; i++)
    {
        weight[i] *= inv;
        phase[i] = SlotPhase(an, clip[i]);
    }
    for (u32 i = n; i < BLEND_SLOTS; i++) clip[i] = clip[0];

    MemSet(instance.activeBoneBitset, 0xFF, sizeof(instance.activeBoneBitset));
    bool actionOccupiesSlot = an->actionState == ActionState_MixIn
        || an->actionState == ActionState_Play
        || an->actionState == ActionState_MixOut;
    if (actionOccupiesSlot)
    {
        f32 a = an->actionWeight;
        for (u32 i = 0; i < BLEND_SLOTS - 1; i++) weight[i] *= 1.0f - a;
        clip[3] = an->actionClip;
        phase[3] = an->actionPhase;
        weight[3] = a;
        MemCopy(&instance.activeBoneBitset[6], an->actionJoints, sizeof(u32) * 3);
    }

    weight[0] = Maxf32(weight[0], 1.5f / 255.0f);

    instance.ratioUnorm16x4[0] = (u32)PackUnorm16(phase[0]) | ((u32)PackUnorm16(phase[1]) << 16);
    instance.ratioUnorm16x4[1] = (u32)PackUnorm16(phase[2]) | ((u32)PackUnorm16(phase[3]) << 16);
    instance.Idx8x4 = clip[0] | (clip[1] << 8) | (clip[2] << 16) | (clip[3] << 24);
    instance.weightsUnorm8x4 = PackColor4PtrToUint(weight); //  PackWeights(weight);
    return instance;
}

void cAnimate(Scene* scene, Character* ch, f32 dt)
{
    cAnim* an = &ch->anim;

    UpdateMotion(ch, dt);
    UpdateFidget(an, dt);

    BlendSet set = { 0 };
    SampleLocomotion(an, &set);
    ApplyTurn(an, &set);
    AdvanceLocomotionPhases(an, &set, dt);
    ApplyFidget(an, &set);
    SortByWeight(&set);
    UpdateAction(an, dt, CountSignificant(&set));

    GPUAnimationInstance instance = ComposeInstance(an, &set);
    u32 charSparse = SparseIDFromSparseData(&scene->skinnedSet, ch->sparse);
    for (u32 i = 0; i < ch->numCharacterEntities; i++)
    {
        AnimationSystem_SetInstance(&scene->animSystem, charSparse + i, instance);
    }
}