
/******************************************************************************************
*  Purpose:                                                                               *
*    Play Blend and Mix Animations Trigger and Transition Between animations              *
*    Can Play Seperate animations on Upper Body and Lower Body: Sword Slash while Walking *
*    Can Rotate The Head and Spine of humanoid character independently,                   *
*    Allows to look around and turn the body                                              *
*  Good To Know:                                                                          *
*    Stores bone matrices into Matrix3x4Half format and sends to GPU within Textures      *
*    Scale interpolation is disabled for now                                              *
*  Author:                                                                                *
*    Anilcan Gulkaya 2024 anilcangulkaya7@gmail.com github @benanil                       *
*******************************************************************************************/

#include "Include/Animation.h"
#include "Include/Platform.h"
#include "Include/Algorithm.h"
#include "Include/Graphics.h"
#include "Include/GLTFParser.h"
#include "Include/FileSystem.h"
#include "Math/Half.h"


void AnimationController_Create(const SceneBundle* prefab, AnimationController* result, Matrix3x4f16* outMatrices)
{
    const ASkin* skin = &prefab->skins[0];
    if (skin == NULL) {
        AX_WARN("skin is null"); return;
    }
    if (skin->numJoints > MaxBonePoses) {
        AX_WARN("number of joints is greater than max capacity"); 
        return; 
    }
    result->mOutMatrices   = outMatrices;
    result->mRootNodeIndex = Prefab_FindAnimRootNodeIndex(prefab);
    result->mPrefab        = prefab;
    result->mNumNodes      = prefab->numNodes;
    result->mRootScale     = prefab->nodes[result->mRootNodeIndex].scale[0]; // 0.1610, rcp: 6.2111
    
    ASSERT(result->mRootNodeIndex < MaxBonePoses);
    ASSERT(prefab->nodes[result->mRootNodeIndex].numChildren > 0); // root node has to have children nodes

    int childIndex = 0;
    for (int i = 0; i < result->mNumNodes; i++)
    {
        Pose pose;
        AnimNode animNode;
        ANode inputNode = prefab->nodes[i];

        pose.translation = VecLoad(inputNode.translation);
        pose.rotation = VecLoad(inputNode.rotation);
        VecSetW(pose.translation, inputNode.scale[1]);

        animNode.numChildren = inputNode.numChildren;
        animNode.childrenStartIndex = childIndex;

        result->mAnimNodes[i] = animNode;
        result->mAnimPoseA[i] = pose;

        for (int j = 0; j < animNode.numChildren; j++)
        {
            result->mChildIndices[childIndex++] = (uint8_t)inputNode.children[j];
        }
    }
}

void AnimationController_RecurseBoneMatrices(AnimationController* ac, int nodeIndex, Vec4x32f parentPos, Vec4x32f parentRot)
{
    const AnimNode* node = &ac->mAnimNodes[nodeIndex];

    for (int c = 0; c < node->numChildren; c++)
    {
        int childIndex = ac->mChildIndices[node->childrenStartIndex + c];

        Pose pose        = ac->mAnimPoseA[childIndex];
        Vec4x32f t       = QMulVec3V(pose.translation, parentRot);
        pose.translation = VecAdd(t, parentPos);
        pose.rotation    = QMul(pose.rotation, parentRot);

        ac->mAnimPoseA[childIndex] = pose; 
        AnimationController_RecurseBoneMatrices(ac, childIndex, pose.translation, pose.rotation);
    }
}

void AnimationController_UploadBoneMatrices(AnimationController* ac)
{
    const ASkin* skin = &ac->mPrefab->skins[0];
    const Matrix4* invMatrices = (const Matrix4*)skin->inverseBindMatrices;

    for (int i = 0; i < skin->numJoints; i++)
    {
        const Pose* pose = ac->mAnimPoseA + skin->joints[i];
        Matrix4 mat = Matrix4Multiply(invMatrices[i], PositionRotationScaleVec(pose->translation, pose->rotation, VecOne()));
        mat = Matrix4Transpose(mat);

        // with AVX F16C this is single insstruction! vcvtps2ph 
        Float8ToHalf8(ac->mOutMatrices[i].x, &mat.m[0][0]);
        Float4ToHalf4(ac->mOutMatrices[i].z, &mat.m[2][0]); // this is single instruction with it as well
    }
}

void AnimationController_UploadPose(AnimationController* ac, const Pose pose[MaxBonePoses])
{
    int rootIdx = ac->mRootNodeIndex;
    const Pose* root = pose + rootIdx;
    AnimationController_RecurseBoneMatrices(ac, rootIdx, root->translation, root->rotation);
    AnimationController_UploadBoneMatrices(ac);
}

void AnimationController_PlayAnim(AnimationController* ac, int index, float norm)
{   
    AnimationController_SampleAnimationPose(ac, ac->mAnimPoseA, index, norm);
    AnimationController_UploadPose(ac, ac->mAnimPoseA);
}

void AnimationController_SampleAnimationPose(const AnimationController* ac, Pose pose[MaxBonePoses], int animIdx, float normTime)
{
    const AAnimation* animation = &ac->mPrefab->animations[animIdx];
    const bool reverse = normTime < 0.0f;

    normTime = Absf32(normTime);
    if (reverse) normTime = MMAX(1.0f - normTime, 0.0f);

    float realTime = normTime * animation->duration;
    
    for (int c = 0; c < animation->numChannels; c++)
    {
        const AAnimChannel* channel = &animation->channels[c];
        const int targetNode = channel->targetNode;
        const AAnimSampler* sampler = &animation->samplers[channel->sampler];
    
        // morph targets are not supported
        if (channel->targetPath == AAnimTargetPath_Weight)
            continue;
    
        // binary search
        int beginIdx = 0;
        int endIdx   = sampler->count - 1;

        while (beginIdx + 1 < endIdx)
        {
            int mid = (beginIdx + endIdx) >> 1;

            if (realTime < sampler->input[mid])
                endIdx = mid;
            else
                beginIdx = mid;
        }

        if (reverse) XSWAP(int, beginIdx, endIdx);

        const Vec4x32f begin = ((const Vec4x32f*)sampler->output)[beginIdx];
        const Vec4x32f end   = ((const Vec4x32f*)sampler->output)[endIdx];
    
        float beginTime = Maxf32(0.0001f, realTime - sampler->input[beginIdx]);
        float endTime   = Maxf32(0.0001f, sampler->input[endIdx] - sampler->input[beginIdx]);
        
        if (reverse) XSWAP(float, beginTime, endTime);
        
        const float t = Clamp01f32(beginTime / endTime);
        Quaternion rot;

        switch (channel->targetPath)
        {
            case AAnimTargetPath_Translation:
                pose[targetNode].translation = VecLerp(begin, end, t);
                break;
            case AAnimTargetPath_Rotation:
                rot = QNLerp(begin, end, t);
                pose[targetNode].rotation = QNorm(rot); // QNormEst maybe
                break;
        };
    }
}


////////            ANIMATED CHARACTER            ////////


void AnimatedCharacter_Create(const SceneBundle* prefab, AnimatedCharacter* result, int lowerBodyStart, Matrix3x4f16* outMatrices)
{
    result->lowerBodyIdxStart = lowerBodyStart;
    result->mState = AnimState_Update;
    result->mTrigerredNorm = 0.0f;

    result->mSpineNodeIdx = Prefab_FindNodeFromName(prefab, "mixamorig:Spine");
    result->mNeckNodeIdx  = Prefab_FindNodeFromName(prefab, "mixamorig:Neck");    
    
    AnimationController_Create(prefab, &result->controller, outMatrices);
}


static inline void RotateNode(Pose* node, float xAngle, float yAngle)
{
    node->rotation = QMul(QMul(QFromXAngle(xAngle), QFromYAngle(yAngle)), node->rotation);
}

static void MergeAnims(Pose pose0[MaxBonePoses], const Pose pose1[MaxBonePoses], float animBlend, int numNodes)
{
    for (int i = 0; i < numNodes; i++)
    {
        pose0[i].rotation    = QNLerp(pose0[i].rotation, pose1[i].rotation, animBlend); // slerp+norm?
        pose0[i].translation = VecLerp(pose0[i].translation, pose1[i].translation, animBlend);
    }
}

static void CopyPoses(Pose* destination, const Pose* pose, int begin, int numPoses)
{
    numPoses += begin;
    for (int i = begin; i < numPoses; i++)
    {
        destination[i].translation = pose[i].translation;
        destination[i].rotation    = pose[i].rotation;
    }
}

// when we want to play different animations with lower body and upper body
void AnimatedCharacter_UploadPoseUpperLower(AnimatedCharacter* ac, const Pose lowerPose[MaxBonePoses], const Pose uperPose[MaxBonePoses])
{
    // apply posess to lower body and upper body seperately, so both of it has diferrent animations
    CopyPoses(ac->controller.mAnimPoseA, lowerPose, ac->lowerBodyIdxStart, ac->controller.mNumNodes - ac->lowerBodyIdxStart);
    CopyPoses(ac->controller.mAnimPoseA, uperPose, 0, ac->lowerBodyIdxStart);

    const Pose* root = ac->controller.mAnimPoseA + ac->controller.mRootNodeIndex;
    AnimationController_RecurseBoneMatrices(&ac->controller, ac->controller.mRootNodeIndex, root->translation, root->rotation);
    AnimationController_UploadBoneMatrices(&ac->controller);
}

bool AnimatedCharacter_Trigger(AnimatedCharacter* ac, int index, float transitionInTime, float transitionOutTime, eAnimTriggerOpt triggerOpt)
{
    if (AnimatedCharacter_IsTrigerred(ac)) return false; // already trigerred

    ac->mTriggerredAnim = index;
    ac->mTriggerOpt = triggerOpt;
    ac->mTransitionTime = transitionInTime;
    ac->mCurTransitionTime = transitionInTime;
    ac->mTransitionOutTime = transitionOutTime;
    if (transitionInTime < 0.02f) { // no transition requested
        ac->mState = AnimState_TriggerPlaying;
        return true;
    }

    ac->mState = AnimState_TriggerIn;
    SmallMemCpy(ac->mAnimPoseC, ac->controller.mAnimPoseA, sizeof(ac->controller.mAnimPoseA));
    if ((triggerOpt & eAnimTriggerOpt_ReverseOut))
        ac->mAnimTime.y = 0.0f;
    return true;
}

bool AnimatedCharacter_TriggerTransition(AnimatedCharacter* ac, float deltaTime, int targetAnim)
{
    float newNorm   = Clamp01f32((ac->mTransitionTime - ac->mCurTransitionTime) / ac->mTransitionTime);
    float animDelta = Clamp01f32(deltaTime * (1.0f / MMAX(1.0f - newNorm, MATH_Epsilon)));
    AnimationController_SampleAnimationPose(&ac->controller, ac->mAnimPoseD, targetAnim, ac->mAnimTime.y);
    MergeAnims(ac->mAnimPoseC, ac->mAnimPoseD, animDelta, ac->controller.mNumNodes);
    ac->mCurTransitionTime -= deltaTime;
    return ac->mCurTransitionTime <= 0.0f;
}

// x, y has to be between -1.0 and 1.0
void AnimatedCharacter_EvaluateLocomotion(AnimatedCharacter* ac, float x, float y, float animSpeed)
{
    const float deltaTime = (float)GetDeltaTime();
    const bool wasTriggerState = AnimatedCharacter_IsTrigerred(ac);

    if (ac->mState == AnimState_TriggerIn)
    {
        if (AnimatedCharacter_TriggerTransition(ac, deltaTime, ac->mTriggerredAnim)) 
            ac->mState = AnimState_TriggerPlaying;
    }
    else if (ac->mState == AnimState_TriggerOut)
    {
        if (!!!(ac->mTriggerOpt & eAnimTriggerOpt_ReverseOut)) 
        {
            if (AnimatedCharacter_TriggerTransition(ac, deltaTime, ac->mLastAnim))
                ac->mState = AnimState_Update;
        }
        else 
        {
            AnimationController_SampleAnimationPose(&ac->controller, ac->mAnimPoseC, ac->mTriggerredAnim, -ac->mTrigerredNorm);
            float animStep = 1.0f / ac->controller.mPrefab->animations[ac->mTriggerredAnim].duration;
            ac->mTrigerredNorm = Clamp01f32(ac->mTrigerredNorm + (animSpeed * animStep * deltaTime));
            if (ac->mTrigerredNorm >= 1.0f)
                ac->mState = AnimState_Update;
        }
    }
    else if (ac->mState == AnimState_TriggerPlaying)
    {
        AnimationController_SampleAnimationPose(&ac->controller, ac->mAnimPoseC, ac->mTriggerredAnim, ac->mTrigerredNorm);

        float animStep = 1.0f / ac->controller.mPrefab->animations[ac->mTriggerredAnim].duration;
        ac->mTrigerredNorm = Clamp01f32(ac->mTrigerredNorm + (animSpeed * animStep * deltaTime));
          
        if (ac->mTrigerredNorm >= 1.0f)
        {
            ac->mTrigerredNorm = 0.0f; // trigger stage complated
            ac->mTransitionTime = ac->mTransitionOutTime;
            ac->mCurTransitionTime = ac->mTransitionOutTime;
            ac->mState = ac->mTransitionTime < 0.02f ? AnimState_Update : AnimState_TriggerOut;
        }
    }

    int yIndex = AnimatedCharacter_GetAnim(ac, aMiddle, 0);
    // if trigerred animation is not standing, we don't have to sample walking or running animations
    if (!wasTriggerState || (wasTriggerState && !!(ac->mTriggerOpt & eAnimTriggerOpt_Standing) && Absf32(y) > 0.001f))
    {
        // play and blend walking and running anims
        y = Absf32(y); 
        int yi = (int)(y);
        // sample y anim
        ASSERTR(yi <= 3, return); // must be between 1 and 4
        yIndex = AnimatedCharacter_GetAnim(ac, aMiddle, yi);

        AnimationController_SampleAnimationPose(&ac->controller, ac->controller.mAnimPoseA, yIndex, ac->mAnimTime.y);
        float yBlend = Fractf(y);

        bool shouldAnimBlendY = yi != 3 && yBlend > 0.00002f;
        if (shouldAnimBlendY)
        {
            yIndex = AnimatedCharacter_GetAnim(ac, aMiddle, yi + 1);
            AnimationController_SampleAnimationPose(&ac->controller, ac->mAnimPoseB, yIndex, ac->mAnimTime.y);
            MergeAnims(ac->controller.mAnimPoseA, ac->mAnimPoseB, EaseOut(yBlend), ac->controller.mNumNodes);
        }

        // if anim is two seconds animStep is 0.5 because we are using normalized value
        float yAnimStep = 1.0f / ac->controller.mPrefab->animations[yIndex].duration;
        ac->mAnimTime.y += animSpeed * yAnimStep * deltaTime;
        ac->mAnimTime.y  = Fractf(ac->mAnimTime.y);
    }
    ac->mLastAnim = yIndex;

    if (!wasTriggerState) {
        AnimationController_UploadPose(&ac->controller, ac->controller.mAnimPoseA);
    }
    else {
        if (!!(ac->mTriggerOpt & eAnimTriggerOpt_Standing) && y > 0.001f)
            AnimatedCharacter_UploadPoseUpperLower(ac, ac->controller.mAnimPoseA, ac->mAnimPoseC);
        else
            AnimationController_UploadPose(&ac->controller, ac->mAnimPoseC);
    }
}

void AnimationController_Clear(AnimationController* animSystem)
{
}

void DestroyAnimationSystem()
{ }
    

// // handle neck, spine rotations
// if (ac->mSpineNodeIdx != -1 && Absf(ac->mSpineYAngle) + Absf(ac->mSpineXAngle) > MATH_Epsilon) {
//     RotateNode(ac->mAnimPoseA + ac->mSpineNodeIdx, ac->mSpineXAngle, ac->mSpineYAngle); 
// }
//     
// if (ac->mNeckNodeIdx != -1 && (Absf(ac->mNeckYAngle) + Absf(ac->mSpineXAngle) > MATH_Epsilon)) {
//     RotateNode(ac->mAnimPoseA + ac->mNeckNodeIdx, ac->mNeckXAngle, ac->mNeckYAngle); 
// }

