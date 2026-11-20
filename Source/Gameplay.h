#ifndef GAMEPLAY_H
#define GAMEPLAY_H

#include "Include/Scene.h"

enum
{
    ToolNone,
    ToolPickaxe,
    ToolAxe,
    ToolShovel,
    NumTools
};
typedef u32 ModelID;

typedef struct ChrAnim_
{
    AnimationBundleAlloc* animAlloc;
    const GPUAnimClip* clips;
    const mat4x4* invBind;
    f32  onceTime;
    f32  onceStart;
    f32  onceSpeed;
    f32  mixInTime;
    f32  mixSpeed;
    f32  mixOutTime;
    f32  speed;
    u32  upperJoints[3];
    u32  anim1Joints[3];
    f32  x, y, horizontalLook;
    f32  locoPhase, idlePhase;
    f32  flyWeight;
    s16  atack, jump, fallingIdle;
    u16  idle, lookAround, walk, jogRun;
    u16  strafeRight, jogLeft;
    u16  strafeLeft, jogRight;
         
    u16  lastAnimation;
    u16  trigerAnim;
    u16  headJoint;
    u16  jointOffset;
    u16  rightHandJoint;
    bool jumpRequested;    
    bool upperLower;
    bool backward;
} cAnim;

typedef struct Character_
{
    v128f         size;
    v128f         center;
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
    u32 numEntity;
} Tool;

typedef struct Gameplay_
{
    struct SDL_GPUTransferBuffer* boneTransfer;
    Scene* scene;
    mat4x4 headBoneMat;
    mat4x4 handBoneMat;
    Character character;

    union {
        struct {
            Tool axe;
            Tool pickAxe;
            Tool shovel;
        };
        Tool tools[NumTools];
    };

    float3 cameraOffset;     // { 0.0f, -1.33, 0.3f };
    float  gravity;          // -25.0f;
    float  groundMinNormalY; // 0.7f;
    float  snapDistance;     // 0.3f;
    float  groundTolerance;  // 0.1f;
    float  moveTolerance;    // 0.001f;
    float  cameraXRotation;  // MATH_HalfPI;
    
    ModelID currentTool;
    EntityID sphereID;
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