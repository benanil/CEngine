#ifndef GAMEPLAY_H
#define GAMEPLAY_H

#include "CharacterAnim.h"
#include "DynamicMover.h"

enum {
    ToolNone,
    ToolAxe,
    ToolPickaxe,
    ToolShovel,
    NumTools
};
typedef u32 ModelID;

typedef struct Character_
{
    v128f         size;
    v128f         rotation;
    SceneBundle*  bundle;
    SparseData*   sparse;
    cAnim anim;
    DynamicMover mover;

    b3Vec3 groundNormal;
    b3Vec3 position;
    b3Vec3 velocity;

    f32 verticalSpeed;
    f32 speed;
    f32 jumpSpeed;
    f32 acceleration;
    f32 decceleration;
    f32 currentSpeed;
    f32 scale;

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

void Character_Destroy(Character* ch);

#endif