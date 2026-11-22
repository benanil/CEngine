#ifndef GAMEPLAY_H
#define GAMEPLAY_H

#include "Include/Scene.h"
#define LOCO_DIRECTIONS 8
#define LOCO_SPEED_ROWS 3
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
enum
{
    ToolNone,
    ToolAxe,
    ToolPickaxe,
    ToolShovel,
    NumTools
};
typedef u32 ModelID;
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

    f32  airTime;
    f32  airWeight;
    f32  fallPhase;
    bool airborne;
    u16  fall, fallToIdle;

    s16  atack, jump;
    u16  idle, lookAround, walk, jogRun;
    u16  strafeRight, jogLeft;
    u16  strafeLeft, jogRight;

    u16  headJoint;
    u16  jointOffset;
    u16  rightHandJoint;
    bool jumpRequested;
    bool wantsMove;
} cAnim;
typedef struct Character_
{
    v128f         size;
    v128f         rotation;
    SceneBundle*  bundle;
    SparseData*   sparse;
    cAnim anim;

    b3Vec3 groundNormal;
    b3Vec3 position;
    b3Vec3 velocity;
             
    f32 verticalSpeed;
    f32 speed;
    f32 jumpSpeed;
    f32 acceleration;
    f32 decceleration;
    f32 currentSpeed; // [0, 1]
    f32 scale; // 1.8

    EntityID entity;
    f32  xRotation;
    f32  yRotation;
    u32  numEntities;
    bool grounded;
    bool active;
    bool jumped;
} Character;

typedef struct Tool_
{
    v128f offset; // VecSetR(-0.05, 0.4f, -0.176f, 0.0f)
    float3 euler; // -142.0f, 91, 140
    EntityID id;
    f32 scale;
    u32 numEntity, pad0, pad1;
} Tool;

typedef struct Gameplay_
{
    union {
        struct {
            Tool axe;
            Tool pickAxe;
            Tool shovel;
        };
        Tool tools[NumTools-1];
    };
    struct SDL_GPUTransferBuffer* boneTransfer;
    Scene* scene;
    mat4x4 headBoneMat;
    mat4x4 handBoneMat;

    float3 cameraOffset;     // { 0.0f, -1.33, 0.3f };
    float  gravity;          // -25.0f;
    float  groundMinNormalY; // 0.7f;
    float  moveTolerance;    // 0.001f;
    float  cameraXRotation;  // MATH_HalfPI;
    
    ModelID  currentTool;
    EntityID sphereID;
    float    padd[3];

    Character character;
} Gameplay;

void Gameplay_Init();

void Gameplay_InitEvents();

void Gameplay_Update(f32 dt);

void Gameplay_Destroy();

void Gameplay_UseBones(SDL_GPUFence* fence);

v128f Gameplay_GetCharacterPivot(Entity* character);

void SpawnCharacter(Character* ch, Scene* scene, Entity* entity, u32 bundleId);

void CharacterMovement(Gameplay* gp, Entity* character, f32 dt);

void cAnimInit(cAnim* an, const Scene* scene, u32 characterId);

void cAnimate(Scene* scene, Character* ch, f32 dt);

void cPlayOnce(cAnim* an, u16 anim, f32 mixInTime, f32 speed, bool upperLower);

#endif