
#ifndef _ANIMATION_H
#define _ANIMATION_H

#include "Graphics.h"
#include "../Math/Matrix.h"
#include "RenderLimits.h"

#if defined(__cplusplus)
extern "C" {
#endif

enum eAnimLocation_
{
    aLeft, aMiddle, aRight
};

enum eAnimTriggerOpt_
{
    eAnimTriggerOpt_None = 0,
    // allows sword slash while walking,
    // Enable if you want to play different animations on lower and upper body
    eAnimTriggerOpt_Standing   = 1,
    // reverse the animation when transitionning out instead of lerping to previous animation
    eAnimTriggerOpt_ReverseOut = 2
};

enum eAnimControllerState_
{
    AnimState_None           = 0,
    AnimState_Update         = 1,
    AnimState_TriggerIn      = 2,
    AnimState_TriggerOut     = 4,
    AnimState_TriggerPlaying = 8,
    AnimState_TriggerMask = AnimState_TriggerIn | AnimState_TriggerOut | AnimState_TriggerPlaying
};

typedef s32 eAnimTriggerOpt;
typedef s32 eAnimLocation;
typedef s32 eAnimState;
typedef s32 eAnimControllerState;

typedef struct half3x4_ {
    f16 x[4];
    f16 y[4];
    f16 z[4];
} half3x4;

typedef struct DualQuaternionHalf_ {
    f16 real[4];
    f16 dual[4];
} DualQuaternionHalf;

typedef struct Pose_ {
    v128f translation;
    v128f rotation;
} Pose;

typedef struct HalfPose_ {
    u64 position;
    u64 rotation;
} HalfPose;

typedef struct AnimNode_
{
    uint8_t numChildren;
    uint8_t childrenStartIndex;
} AnimNode;

typedef struct GPUAnimationInstance_
{
    u32 animIdx;
    f32 timeOffset;
} GPUAnimationInstance;

typedef struct GPUAnimationData_
{
    u32 frameOffset;
    u32 numFrames;
    u32 rootNodeIndex;
    u32 numJoints;
    u32 numNodes;
    f32 duration;
    u32 jointOffset;
    u32 invBindOffset;
    u32 hierarchyOffset;
    u32 padding;
} GPUAnimationData;

// per scene animation state, owned by Scene. gpu buffers are created lazily on the
// first skinned bundle append so surface only scenes don't pay for them
typedef struct AnimationSystem_
{
    GPUAnimationData animData[MAX_ANIM_COUNT];
    u32 numAnimations;
    u32 frameOffset;     // baked pose frame cursor
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
} AnimationSystem;

void AnimationSystem_Init(AnimationSystem* anims);
void AnimationSystem_Destroy(AnimationSystem* anims);

// bakes the bundle's animations and skin data and uploads only the appended ranges.
// creates the gpu buffers on first use. fail return 0
s32 AnimationSystem_AppendBundle(AnimationSystem* anims, const SceneBundle* bundle);

// assigns a random animation and time offset to every instance slot and uploads them
void AnimationSystem_RandomizeInstances(AnimationSystem* anims);

// fail return 0
s32 SceneBundleInitAnimations(const SceneBundle* prefab, Pose pose[MAX_BONES]);

// use negative normTime to sample animation reversely
void SampleSkinnedAnimationPose(const SceneBundle* bundle, Pose pose[MAX_BONES], s32 animIdx, f32 normTime);

// this is here for reference not used now
typedef struct AnimatedCharacter_
{
    // lower body bones are starting from 60th with Brute character and 58 with mixamo Paladin Character
    // used for animating diferrent animations for legs and uper body
    // this value can change from character to character. 
    // you can detect using 3DVert and GBuffer shader just uncomment lines with vBoneIdx, and it will visualize lower body as white
    // Maybe Add: automatic detect.
    s32 lowerBodyIdxStart; 

    s32 mSpineNodeIdx; // < upper body root bone 
    s32 mNeckNodeIdx;
 
    s32 mTriggerredAnim;
    s32 mLastAnim;
   
    f32 mTrigerredNorm;
    f32 mTransitionTime; // trigger transition time
    f32 mTransitionOutTime;
    f32 mCurTransitionTime;

    eAnimTriggerOpt mTriggerOpt;
    eAnimState mState;

    // angle's recomended values are between (-PI/3, PI/3)
    // calculate the angle between target and player, then clamp the value between the limits
    // to enable spine or neck additive rotation you just have to set angle's any value which is not zero
    f32 mSpineYAngle;
    f32 mNeckYAngle;
    f32 mSpineXAngle; // < will rotate around this axis (normalized) default vec3::up
    f32 mNeckXAngle;  // < will rotate around this axis (normalized) default vec3::up
    
    float2 mAnimTime;

    Pose mAnimPoseB[MAX_BONES]; // < blend target
    // two posses for blending
    Pose mAnimPoseC[MAX_BONES]; // < Trigerred animations result
    Pose mAnimPoseD[MAX_BONES]; // < Trigerred Animations blend target

    // animation indexes to blend coordinates
    // Given xy blend coordinates, we will blend animations.
    // in typical animation system, the diagram should be like the diagran below.
    //  #  #  #  <- DiagonalRun , ForwardRun , DiagonalRun
    //  #  #  #  <- DiagonalJog , ForwardJog , DiagonalJog
    //  #  #  #  <- DiagonalWalk, ForwardWalk, DiagonalWalk
    //  #  #  #  <- StrafeLeft  , Idle       , StrafeRight 
    s32 mLocomotionIndices   [4][3];
    s32 mLocomotionIndicesInv[3][3]; 
    
} AnimatedCharacter;


#if defined(__cplusplus)
}
#endif

#endif // _ANIMATION_H


