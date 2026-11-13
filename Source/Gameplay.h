#ifndef GAMEPLAY_H
#define GAMEPLAY_H

#include "Include/Scene.h"

#define LOCO_SPEED_ROWS 3
#define LOCO_DIRECTIONS 8
#define MAX_FIDGETS     4

typedef enum ActionState_
{
    ActionState_None,
    ActionState_Squeeze,
    ActionState_MixIn,
    ActionState_Play,
    ActionState_MixOut,
    ActionState_Release
} ActionState;

typedef struct ChrAnim_
{
    AnimationBundleAlloc* animAlloc;
    const GPUAnimClip* clips;
    const mat4x4* invBind;

    u16  locomotion[LOCO_SPEED_ROWS][LOCO_DIRECTIONS];
    u16  fidgets[MAX_FIDGETS];
    u32  upperJoints[3];
    u32  actionJoints[3];
    u32  rng;
    u32  numFidgets;

    f32  animSpeed;
    f32  speed;
    f32  dirAngle;
    f32  moveX;
    f32  moveY;
    f32  lookTurn;
    f32  locoPhase;
    f32  idlePhase;

    f32  fidgetTimer;
    f32  fidgetPhase;
    f32  fidgetGate;
    f32  fidgetWeight;
    u16  fidgetClip;
    bool fidgetPlaying;

    ActionState actionState;
    f32  actionWeight;
    f32  actionPhase;
    f32  actionElapsed;
    f32  actionSpeed;
    f32  actionFade;
    f32  squeeze;
    u16  actionClip;

    s16  atack, jump;
    u16  idle, lookAround, walk, jogRun;
    u16  strafeRight, jogLeft;
    u16  strafeLeft, jogRight;

    u16  headJoint;
    u16  jointOffset;
    u16  rightHandJoint;
    bool jumpRequested;
} cAnim;

typedef struct Character_
{
    v128f         center;
    SceneBundle*  bundle;
    SparseData*   sparse;
    cAnim anim;

    b3Vec3 groundNormal;
    b3Vec3 position;
    b3Vec3 velocity;

    f32 verticalSpeed;
    f32 characterSpeed;
    f32 jumpSpeed;
    f32 acceleration;
    f32 decceleration;
    f32 currentSpeed;

    EntityID characterEntity;
    f32  xRotation;
    f32  yRotation;
    u32  numCharacterEntities;
    bool grounded;
    bool characterActive;
    bool jumped;
} Character;

void Gameplay_Update();

void UpdateCharacter();

void cAnimInit(cAnim* an, const Scene* scene, u32 characterId);

void cAnimate(Scene* scene, Character* ch, f32 dt);

void cPlayOnce(cAnim* an, u16 anim, f32 mixInTime, f32 speed, bool upperLower);

bool cAnimActionActive(const cAnim* an);

#endif