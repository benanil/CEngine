
#ifndef _ANIMATION_H
#define _ANIMATION_H

#include "Graphics.h"
#include "../Math/Matrix.h"

// make 192 or 256 if we use more joints
#define MaxBonePoses      128
#define MaxBoneDepth      32
#define NUM_ANIMS         16
#define ANIM_NUM_FRAMES   24
#define MAX_ANIM_DURATION 8
#define MAX_ANIM_COUNT    128
#define MAX_SKIN_COUNT    128

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

typedef struct AnimationController_
{
    const SceneBundle* mPrefab;

    s32      mRootNodeIndex;
    s32      mNumJoints;
    f32       mRootScale;
   
    half3x4* mOutMatrices;

    AnimNode mAnimNodes[MaxBonePoses * 2];
    Pose     mAnimPoseA[MaxBonePoses * 2]; // < the result bone array that we send to GPU
    u8       mChildIndices[MaxBonePoses * 2];

} AnimationController;

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
    
    fv2 mAnimTime;

    Pose mAnimPoseB[MaxBonePoses]; // < blend target
    // two posses for blending
    Pose mAnimPoseC[MaxBonePoses]; // < Trigerred animations result
    Pose mAnimPoseD[MaxBonePoses]; // < Trigerred Animations blend target

    // animation indexes to blend coordinates
    // Given xy blend coordinates, we will blend animations.
    // in typical animation system, the diagram should be like the diagran below.
    //  #  #  #  <- DiagonalRun , ForwardRun , DiagonalRun
    //  #  #  #  <- DiagonalJog , ForwardJog , DiagonalJog
    //  #  #  #  <- DiagonalWalk, ForwardWalk, DiagonalWalk
    //  #  #  #  <- StrafeLeft  , Idle       , StrafeRight 
    s32 mLocomotionIndices   [4][3];
    s32 mLocomotionIndicesInv[3][3]; 
    
    AnimationController controller;
} AnimatedCharacter;


////////           ANIMATION CONTROLLER           ////////


// bool humanoid = true, i32 lowerBodyStart = 58, animId = global animation controllerIndex 
void AnimationController_Create(const SceneBundle* prefab, 
                                AnimationController* animController, 
                                half3x4* outMatrices);


// play the given animation, norm is the animation progress between 0.0 and 1.0
void AnimationController_PlayAnim(AnimationController* ac, s32 index, f32 norm);

void AnimationController_UploadBoneMatrices(AnimationController* ac);

void AnimationController_Clear(AnimationController* ac);


////////            ANIMATED CHARACTER            ////////


static inline void AnimatedCharacter_SetAnim(AnimatedCharacter* ac, s32 x, s32 y, s32 index)
{
    if (y >= 0) ac->mLocomotionIndices[y][x] = index;
    else        ac->mLocomotionIndicesInv[Absi32(y-1.0f)][x] = index;
}

static inline s32 AnimatedCharacter_GetAnim(const AnimatedCharacter* ac, s32 x, s32 y)
{
    if (y >= 0) return ac->mLocomotionIndices[y][x];
    else        return ac->mLocomotionIndicesInv[Absi32(y)-1][x];
}
    
static inline bool AnimatedCharacter_IsTrigerred(const AnimatedCharacter* ac)
{
    return (ac->mState & AnimState_TriggerMask) != 0;
}

void AnimatedCharacter_Create(const SceneBundle* prefab, AnimatedCharacter* result, s32 lowerBodyStart, half3x4* outMatrices);

// x, y has to be between -1.0 and 1.0 (normalized)
// xspeed and yspeed is between 0 and infinity speed of animation
// normTime should be between 0 and 1
// runs the walking running etc animations from given inputs
void AnimatedCharacter_EvaluateLocomotion(AnimatedCharacter* ac, f32 x, f32 y, f32 animSpeed);

bool AnimatedCharacter_TriggerTransition(AnimatedCharacter* ac, f32 dt, s32 targetAnim);

// trigger time is the animation transition time
// standing anims are animations that we can play when walking or running
// returns true if triggered successfully (wasn't trigerred already)
bool AnimatedCharacter_Trigger(AnimatedCharacter* ac, s32 animIndex, f32 triggerInTime, f32 triggerOutTime, eAnimTriggerOpt triggerOpt);


////////              PRIVATE                 ////////  feel free to use


// upload to gpu. internal usage only for now
void AnimationController_UploadPose(AnimationController* ac, const Pose nodeMatrices[MaxBonePoses]);
    
// void AnimationController_RecurseBoneMatrices(AnimationController* ac); 

void AnimationController_RecurseBoneMatrices(AnimationController* ac);

// use negative normTime to sample animation reversely
void AnimationController_SampleAnimationPose(const AnimationController* ac, Pose pose[MaxBonePoses], s32 animIdx, f32 normTime);

// when we want to play different animations with lower body and upper body
void AnimatedCharacter_UploadPoseUpperLower(AnimatedCharacter* ac, const Pose lowerPose[MaxBonePoses], const Pose uperPose[MaxBonePoses]);


#if defined(__cplusplus)
}
#endif

#endif // _ANIMATION_H


