
#ifndef _ANIMATION_H
#define _ANIMATION_H

#include "Graphics.h"
#include "../Math/Matrix.h"

// make 192 or 256 if we use more joints
#define MAX_BONES          128
#define MaxBoneDepth       32
#define MAX_ANIM_INSTANCES 2048
#define ANIM_NUM_FRAMES    24
#define MAX_ANIM_DURATION  8
#define MAX_ANIM_COUNT     128
#define MAX_SKIN_COUNT     128

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
    
    AnimNode mAnimNodes[MAX_BONES * 2];
    Pose     mAnimPoseA[MAX_BONES * 2]; // < the result bone array that we send to GPU
    u8       mChildIndices[MAX_BONES * 2];

} AnimationController;

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
    
    fv2 mAnimTime;

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
    
    AnimationController controller;
} AnimatedCharacter;


// bool humanoid = true, i32 lowerBodyStart = 58, animId = global animation controllerIndex 
void AnimationController_Create(const SceneBundle* prefab, AnimationController* animController);


void AnimationController_Clear(AnimationController* ac);

// use negative normTime to sample animation reversely
void AnimationController_SampleAnimationPose(const AnimationController* ac, Pose pose[MAX_BONES], s32 animIdx, f32 normTime);


#if defined(__cplusplus)
}
#endif

#endif // _ANIMATION_H


