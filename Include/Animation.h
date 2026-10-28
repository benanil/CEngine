
#ifndef _ANIMATION_H
#define _ANIMATION_H

#include "Graphics.h"
#include "Memory.h"
#include "../Math/Matrix.h"
#include "RenderLimits.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct DualQuaternionHalf_ {
    f16 real[4];
    f16 dual[4];
} DualQuaternionHalf;

typedef struct Pose_ {
    v128f translation;
    v128f rotation;
} Pose;

typedef struct AnimNode_
{
    uint8_t numChildren;
    uint8_t childrenStartIndex;
} AnimNode;

typedef struct GPUAnimationInstance_
{
    u32 anim1Joints[3]; // bitset 96 joint
    u32 animRatio2;
    u16 animIdx2;
    u16 blend;
    s16 neckAngle;
    s16 spineAngle;
} GPUAnimationInstance;

STATIC_ASSERT(sizeof(GPUAnimationInstance) == 24, "gpu animation instance should be 16 byte");

typedef struct GPUAnimationData_
{
    u32 frameOffset;
    u32 numFrames; 
    u32 rootNode;  // useless, 0 is root node
    u32 numJoints; //u8
    u32 numNodes;
    f32 duration;
    u32 jointOffset;
    u32 invBindOffset;
    u32 hierarchyOffset;
    u16 neckNode;
    u16 spineNode;
} GPUAnimationData;

typedef struct AnimationBundleAlloc_
{
    u32 frameOffset;
    u32 frameCount;
    u16 skinSlot;
    u16 animOffset;
    u16 animCount;
    u16 neckJoint;
    u16 spineJoint;
    u16 numNodes;
} AnimationBundleAlloc;

// per scene animation state, owned by Scene. gpu buffers are created lazily on the
// first skinned bundle append so surface only scenes don't pay for them
typedef struct AnimationSystem_
{
    GPUAnimationData animData[MAX_ANIM_COUNT];
    u64 usedAnimSlots[(MAX_ANIM_COUNT + 63u) >> 6];
    u64 usedSkinSlots[(MAX_SKIN_COUNT + 63u) >> 6];
    RangeU32 freeFrameRanges[MAX_ANIM_COUNT];
    RangeAllocator frameAllocator; // animation bone frames (not per frame)
    u32 numAnimations;   // active animation slots
    u32 jointOffset;
    u32 invBindOffset;
    u32 hierarchyOffset;

    SDL_GPUBuffer* boneBuffer;      // per instance bone matrices, written by compute
    SDL_GPUBuffer* poseBuffer;      // baked animation poses, per bone * frame
    SDL_GPUBuffer* hierarchyBuffer;
    SDL_GPUBuffer* dataBuffer;
    SDL_GPUBuffer* jointsBuffer;
    SDL_GPUBuffer* invBindBuffer;
    SDL_GPUBuffer* instanceBuffer;
    GPUAnimationInstance instances[MAX_ANIM_INSTANCES];
    u32 instanceDirtyStart;
    u32 instanceDirtyEnd;
} AnimationSystem;

void AnimationSystem_Init(AnimationSystem* anims);
void AnimationSystem_Destroy(AnimationSystem* anims);

// bakes the bundle's animations and skin data and uploads only the appended ranges.
// creates the gpu buffers on first use. fail return 0
s32 AnimationSystem_AppendBundle(AnimationSystem* anims, const SceneBundle* bundle, AnimationBundleAlloc* outAlloc);
void AnimationSystem_RemoveBundle(AnimationSystem* anims, AnimationBundleAlloc alloc);

// out: GPU animation slot at ordinal among active animations, 0 when no active animation exists
u32 AnimationSystem_GetNthUsedAnim(const AnimationSystem* anims, u32 ordinal);

// uploads instance assignments to the gpu, instances are indexed by the skinned
// render set's sparse entity id. count is clamped to MAX_ANIM_INSTANCES
void AnimationSystem_UpdateInstances(AnimationSystem* anims, const GPUAnimationInstance* instances, u32 count);

// assigns one animation instance slot, indexed by the skinned render set's sparse id
void AnimationSystem_SetInstance(AnimationSystem* anims, u32 sparseIdx, GPUAnimationInstance instance);

// uploads dirty CPU-owned animation instance assignments once before animation compute
void AnimationSystem_FlushInstances(AnimationSystem* anims);

// fail return 0
s32 SceneBundleInitAnimations(const SceneBundle* prefab, Pose pose[MAX_BONES]);

// use negative normTime to sample animation reversely
void SampleSkinnedAnimationPose(const SceneBundle* bundle, Pose pose[MAX_BONES], s32 animIdx, f32 normTime);

u16 FindAnimIndex(const SceneBundle* prefab, const char* name);

mat4x4 UnpackGPUBone(u32 buffer[8]);

#if defined(__cplusplus)
}
#endif

#endif // _ANIMATION_H


