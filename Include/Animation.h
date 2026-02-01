
#ifndef _ANIMATION_H
#define _ANIMATION_H

#include "Graphics.h"
#include "../Math/Matrix.h"

// make 192 or 256 if we use more joints
#define MaxBonePoses  128


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

typedef int eAnimTriggerOpt;
typedef int eAnimLocation;
typedef int eAnimState;
typedef int eAnimControllerState;

typedef struct Matrix3x4f16_ {
    half x[4];
    half y[4];
    half z[4];
} Matrix3x4f16;

typedef struct DualQuaternionHalf_ {
    half real[4];
    half dual[4];
} DualQuaternionHalf;

typedef struct Pose_ {
    Vec4x32f translation;
    Vec4x32f rotation;
    // Vector4x32f scale;
} Pose;

typedef struct HalfPose_ {
    half position[4];
    half rotation[4];
} HalfPose;

typedef struct AnimNode_
{
    uint8_t numChildren;
    uint8_t childrenStartIndex;
} AnimNode;

typedef struct AnimationController_
{
    const SceneBundle* mPrefab;

    int   mRootNodeIndex;
    int   mNumNodes;
    float mRootScale;
   
    Matrix3x4f16* mOutMatrices;

    AnimNode mAnimNodes[MaxBonePoses];
    Pose     mAnimPoseA[MaxBonePoses]; // < the result bone array that we send to GPU
    uint8_t  mChildIndices[MaxBonePoses * 2];

} AnimationController;

typedef struct AnimatedCharacter_
{
    // lower body bones are starting from 60th with Brute character and 58 with mixamo Paladin Character
    // used for animating diferrent animations for legs and uper body
    // this value can change from character to character. 
    // you can detect using 3DVert and GBuffer shader just uncomment lines with vBoneIdx, and it will visualize lower body as white
    // Maybe Add: automatic detect.
    int lowerBodyIdxStart; 

    int mSpineNodeIdx; // < upper body root bone 
    int mNeckNodeIdx;
 
    int mTriggerredAnim;
    int mLastAnim;
   
    float mTrigerredNorm;
    float mTransitionTime; // trigger transition time
    float mTransitionOutTime;
    float mCurTransitionTime;

    eAnimTriggerOpt mTriggerOpt;
    eAnimState mState;

    // angle's recomended values are between (-PI/3, PI/3)
    // calculate the angle between target and player, then clamp the value between the limits
    // to enable spine or neck additive rotation you just have to set angle's any value which is not zero
    float mSpineYAngle;
    float mNeckYAngle;
    float mSpineXAngle; // < will rotate around this axis (normalized) default vec3::up
    float mNeckXAngle;  // < will rotate around this axis (normalized) default vec3::up
    
    float2 mAnimTime;

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
    int mLocomotionIndices   [4][3];
    int mLocomotionIndicesInv[3][3]; 
    
    AnimationController controller;
} AnimatedCharacter;


////////           ANIMATION CONTROLLER           ////////


// bool humanoid = true, int lowerBodyStart = 58, animId = global animation controllerIndex 
void AnimationController_Create(const SceneBundle* prefab, 
                                AnimationController* animController, 
                                Matrix3x4f16* outMatrices);


// play the given animation, norm is the animation progress between 0.0 and 1.0
void AnimationController_PlayAnim(AnimationController* ac, int index, float norm);

void AnimationController_UploadBoneMatrices(AnimationController* ac);

void AnimationController_Clear(AnimationController* ac);


////////            ANIMATED CHARACTER            ////////


static inline void AnimatedCharacter_SetAnim(AnimatedCharacter* ac, int x, int y, int index)
{
    if (y >= 0) ac->mLocomotionIndices[y][x] = index;
    else        ac->mLocomotionIndicesInv[Absi32(y-1.0f)][x] = index;
}

static inline int AnimatedCharacter_GetAnim(const AnimatedCharacter* ac, int x, int y)
{
    if (y >= 0) return ac->mLocomotionIndices[y][x];
    else        return ac->mLocomotionIndicesInv[Absi32(y)-1][x];
}
    
static inline bool AnimatedCharacter_IsTrigerred(const AnimatedCharacter* ac)
{
    return (ac->mState & AnimState_TriggerMask) != 0;
}

void AnimatedCharacter_Create(const SceneBundle* prefab, AnimatedCharacter* result, int lowerBodyStart, Matrix3x4f16* outMatrices);

// x, y has to be between -1.0 and 1.0 (normalized)
// xspeed and yspeed is between 0 and infinity speed of animation
// normTime should be between 0 and 1
// runs the walking running etc animations from given inputs
void AnimatedCharacter_EvaluateLocomotion(AnimatedCharacter* ac, float x, float y, float animSpeed);

bool AnimatedCharacter_TriggerTransition(AnimatedCharacter* ac, float dt, int targetAnim);

// trigger time is the animation transition time
// standing anims are animations that we can play when walking or running
// returns true if triggered successfully (wasn't trigerred already)
bool AnimatedCharacter_Trigger(AnimatedCharacter* ac, int animIndex, float triggerInTime, float triggerOutTime, eAnimTriggerOpt triggerOpt);


////////              PRIVATE                 ////////  feel free to use


// upload to gpu. internal usage only for now
void AnimationController_UploadPose(AnimationController* ac, const Pose nodeMatrices[MaxBonePoses]);
    
void AnimationController_RecurseBoneMatrices(AnimationController* ac, int nodeIndex, Vec4x32f position, Vec4x32f rotation); // Matrix4 parentMatrix);

// use negative normTime to sample animation reversely
void AnimationController_SampleAnimationPose(const AnimationController* ac, Pose pose[MaxBonePoses], int animIdx, float normTime);

// when we want to play different animations with lower body and upper body
void AnimatedCharacter_UploadPoseUpperLower(AnimatedCharacter* ac, const Pose lowerPose[MaxBonePoses], const Pose uperPose[MaxBonePoses]);


#if defined(__cplusplus)
}
#endif

#endif // _ANIMATION_H


