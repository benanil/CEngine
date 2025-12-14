
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
#include "Math/Half.h"


void AnimationController_Create(const SceneBundle* prefab, 
                                AnimationController* result,
                                bool humanoid, 
                                int lowerBodyStart, 
                                Matrix3x4f16* outMatrices)
{
    const ASkin* skin = &prefab->skins[0];
    if (skin == NULL) {
        AX_WARN("skin is null"); return;
    }
    if (skin->numJoints > MaxBonePoses) {
        AX_WARN("number of joints is greater than max capacity"); 
        return; 
    }
    result->mOutMatrices = outMatrices;
    result->mRootNodeIndex = Prefab_FindAnimRootNodeIndex(prefab);
    result->mPrefab = prefab;
    result->mState = AnimState_Update;
    result->mNumNodes = prefab->numNodes;
    result->mTrigerredNorm = 0.0f;
    result->lowerBodyIdxStart = lowerBodyStart;
    result->mRootScale = 0.16f;// prefab->nodes[result->mRootNodeIndex].scale[0];

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

        animNode.numChildren = inputNode.numChildren;
        animNode.childrenStartIndex = childIndex;

        result->mAnimNodes[i] = animNode;
        result->mAnimPoseA[i] = pose;
        
        for (int j = 0; j < animNode.numChildren; j++)
        {
            result->mChildIndices[childIndex++] = (uint8_t)inputNode.children[j];
        }
    }

    if (!humanoid)
        return;
    
    result->mSpineNodeIdx = Prefab_FindNodeFromName(prefab, "mixamorig:Spine");
    result->mNeckNodeIdx  = Prefab_FindNodeFromName(prefab, "mixamorig:Neck");
}

static inline void RotateNode(Pose* node, float xAngle, float yAngle)
{
    node->rotation = QMul(QMul(QFromXAngle(xAngle), QFromYAngle(yAngle)), node->rotation);
}

purefn Matrix4 GetNodeMatrix(const AnimationController* ac, int index)
{
    const Pose pose = ac->mAnimPoseA[index];
    Matrix4 res = Matrix4Identity();
    MatrixFromQuaternion(&res.m[0][0], pose.rotation, 4);
    res.r[3] = pose.translation;
    VecSetW(res.r[3], 1.0f);
    return res;
}

void AnimationController_RecurseBoneMatrices(AnimationController* ac, int nodeIndex, Matrix4 parentMatrix)
{
    const AnimNode* node = &ac->mAnimNodes[nodeIndex];

    for (int c = 0; c < node->numChildren; c++)
    {
        int childIndex = ac->mChildIndices[node->childrenStartIndex + c];

        ac->mBoneMatrices[childIndex] = Matrix4Multiply(GetNodeMatrix(ac, childIndex), parentMatrix);

        AnimationController_RecurseBoneMatrices(ac, childIndex, ac->mBoneMatrices[childIndex]);
    }
}

static void MergeAnims(Pose pose0[MaxBonePoses], const Pose pose1[MaxBonePoses], float animBlend, int numNodes)
{
    for (int i = 0; i < numNodes; i++)
    {
        pose0[i].rotation    = QNLerp(pose0[i].rotation, pose1[i].rotation, animBlend); // slerp+norm?
        pose0[i].translation = VecLerp(pose0[i].translation, pose1[i].translation, animBlend);
    }
}

void AnimationController_SampleAnimationPose(const AnimationController* ac, Pose pose[MaxBonePoses], int animIdx, float normTime)
{
    const AAnimation* animation = &ac->mPrefab->animations[animIdx];
    const bool reverse = normTime < 0.0f;

    normTime = Absf(normTime);
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

        const Vector4x32f begin = ((const Vector4x32f*)sampler->output)[beginIdx];
        const Vector4x32f end   = ((const Vector4x32f*)sampler->output)[endIdx];
    
        float beginTime = Maxf(0.0001f, realTime - sampler->input[beginIdx]);
        float endTime   = Maxf(0.0001f, sampler->input[endIdx] - sampler->input[beginIdx]);
        
        if (reverse) XSWAP(float, beginTime, endTime);
        
        const float t = Clamp01f(beginTime / endTime);
        Quaternion rot;

        switch (channel->targetPath)
        {
            case AAnimTargetPath_Translation:
                pose[targetNode].translation = VecLerp(begin, end, t);
                break;
            case AAnimTargetPath_Rotation:
                rot = QSlerp(begin, end, t);
                pose[targetNode].rotation = QNorm(rot); // QNormEst maybe
                break;
            // no scale for now
        };
    }
}

// send matrices to GPU
void AnimationController_UploadBoneMatrices(AnimationController* ac)
{
    const ASkin* skin = &ac->mPrefab->skins[0];
    const Matrix4* invMatrices = (const Matrix4*)skin->inverseBindMatrices;

    // give this, thousands of joints it will process it rapidly!
    for (int i = 0; i < skin->numJoints; i++)
    {
        Matrix4 mat = Matrix4Multiply(invMatrices[i], ac->mBoneMatrices[skin->joints[i]]);
        mat = Matrix4Transpose(mat);
        // with AVX F16C this is single instruction! vcvtps2ph 
        ConvertFloat8ToHalf8(ac->mOutMatrices[i].x, &mat.m[0][0]);
        ConvertFloat4ToHalf4(ac->mOutMatrices[i].z, &mat.m[2][0]); // this is single instruction with it as well
    }
}

void AnimationController_UploadPose(AnimationController* ac, const Pose pose[MaxBonePoses])
{
    ac->mBoneMatrices[ac->mRootNodeIndex] = MatrixFromScalef(ac->mRootScale);
    
    AnimationController_RecurseBoneMatrices(ac, ac->mRootNodeIndex, ac->mBoneMatrices[ac->mRootNodeIndex]);
    // // handle neck, spine rotations
    // if (ac->mSpineNodeIdx != -1 && Absf(ac->mSpineYAngle) + Absf(ac->mSpineXAngle) > MATH_Epsilon) {
    //     RotateNode(ac->mAnimPoseA + ac->mSpineNodeIdx, ac->mSpineXAngle, ac->mSpineYAngle); 
    // }
    //     
    // if (ac->mNeckNodeIdx != -1 && (Absf(ac->mNeckYAngle) + Absf(ac->mSpineXAngle) > MATH_Epsilon)) {
    //     RotateNode(ac->mAnimPoseA + ac->mNeckNodeIdx, ac->mNeckXAngle, ac->mNeckYAngle); 
    // }
    AnimationController_UploadBoneMatrices(ac);
}

static void InitNodes(Pose* nodes, const Pose* pose, int begin, int numNodes)
{
    numNodes += begin;
    for (int i = begin; i < numNodes; i++)
    {
        nodes[i].translation = pose[i].translation;
        nodes[i].rotation    = pose[i].rotation;
    }
}

// when we want to play different animations with lower body and upper body
void AnimationController_UploadPoseUpperLower(AnimationController* ac, const Pose lowerPose[MaxBonePoses], const Pose uperPose[MaxBonePoses])
{
    // apply posess to lower body and upper body seperately, so both of it has diferrent animations
    InitNodes(ac->mAnimPoseA, lowerPose, ac->lowerBodyIdxStart, ac->mNumNodes - ac->lowerBodyIdxStart);
    InitNodes(ac->mAnimPoseA, uperPose, 0, ac->lowerBodyIdxStart);

    const AnimNode rootNode = ac->mAnimNodes[ac->mRootNodeIndex];
    const Matrix4 rootMatrix = GetNodeMatrix(ac, ac->mRootNodeIndex);
    ac->mBoneMatrices[ac->mRootNodeIndex] = rootMatrix;

    AnimationController_RecurseBoneMatrices(ac, ac->mRootNodeIndex, rootMatrix);
    AnimationController_UploadBoneMatrices(ac);
}

void AnimationController_PlayAnim(AnimationController* ac, int index, float norm)
{
    AnimationController_SampleAnimationPose(ac, ac->mAnimPoseA, index, norm);
    AnimationController_UploadPose(ac, ac->mAnimPoseA);
}

bool AnimationController_TriggerAnim(AnimationController* ac, int index, float transitionInTime, float transitionOutTime, eAnimTriggerOpt triggerOpt)
{
    if (AnimationController_IsTrigerred(ac)) return false; // already trigerred

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
    SmallMemCpy(ac->mAnimPoseC, ac->mAnimPoseA, sizeof(ac->mAnimPoseA));
    if ((triggerOpt & eAnimTriggerOpt_ReverseOut))
        ac->mAnimTime.y = 0.0f;
    return true;
}

bool AnimationController_TriggerTransition(AnimationController* ac, float deltaTime, int targetAnim)
{
    float newNorm   = Clamp01f((ac->mTransitionTime - ac->mCurTransitionTime) / ac->mTransitionTime);
    float animDelta = Clamp01f(deltaTime * (1.0f / MMAX(1.0f - newNorm, MATH_Epsilon)));
    AnimationController_SampleAnimationPose(ac, ac->mAnimPoseD, targetAnim, ac->mAnimTime.y);
    MergeAnims(ac->mAnimPoseC, ac->mAnimPoseD, animDelta, ac->mNumNodes);
    ac->mCurTransitionTime -= deltaTime;
    return ac->mCurTransitionTime <= 0.0f;
}

// x, y has to be between -1.0 and 1.0
void AnimationController_EvaluateLocomotion(AnimationController* ac, float x, float y, float animSpeed)
{
    const float deltaTime = (float)GetDeltaTime();
    const bool wasTriggerState = AnimationController_IsTrigerred(ac);

    if (ac->mState == AnimState_TriggerIn)
    {
        if (AnimationController_TriggerTransition(ac, deltaTime, ac->mTriggerredAnim)) 
            ac->mState = AnimState_TriggerPlaying;
    }
    else if (ac->mState == AnimState_TriggerOut)
    {
        if (!!!(ac->mTriggerOpt & eAnimTriggerOpt_ReverseOut)) 
        {
            if (AnimationController_TriggerTransition(ac, deltaTime, ac->mLastAnim))
                ac->mState = AnimState_Update;
        }
        else 
        {
            AnimationController_SampleAnimationPose(ac, ac->mAnimPoseC, ac->mTriggerredAnim, -ac->mTrigerredNorm);
            float animStep = 1.0f / ac->mPrefab->animations[ac->mTriggerredAnim].duration;
            ac->mTrigerredNorm = Clamp01f(ac->mTrigerredNorm + (animSpeed * animStep * deltaTime));
            if (ac->mTrigerredNorm >= 1.0f)
                ac->mState = AnimState_Update;
        }
    }
    else if (ac->mState == AnimState_TriggerPlaying)
    {
        AnimationController_SampleAnimationPose(ac, ac->mAnimPoseC, ac->mTriggerredAnim, ac->mTrigerredNorm);

        float animStep = 1.0f / ac->mPrefab->animations[ac->mTriggerredAnim].duration;
        ac->mTrigerredNorm = Clamp01f(ac->mTrigerredNorm + (animSpeed * animStep * deltaTime));
          
        if (ac->mTrigerredNorm >= 1.0f)
        {
            ac->mTrigerredNorm = 0.0f; // trigger stage complated
            ac->mTransitionTime = ac->mTransitionOutTime;
            ac->mCurTransitionTime = ac->mTransitionOutTime;
            ac->mState = ac->mTransitionTime < 0.02f ? AnimState_Update : AnimState_TriggerOut;
        }
    }

    int yIndex = AnimationController_GetAnim(ac, aMiddle, 0);
    // if trigerred animation is not standing, we don't have to sample walking or running animations
    if (!wasTriggerState || (wasTriggerState && !!(ac->mTriggerOpt & eAnimTriggerOpt_Standing) && Absf(y) > 0.001f))
    {
        // play and blend walking and running anims
        y = Absf(y); 
        int yi = (int)(y);
        // sample y anim
        ASSERTR(yi <= 3, return); // must be between 1 and 4
        yIndex = AnimationController_GetAnim(ac, aMiddle, yi);

        AnimationController_SampleAnimationPose(ac, ac->mAnimPoseA, yIndex, ac->mAnimTime.y);
        float yBlend = Fractf(y);

        bool shouldAnimBlendY = yi != 3 && yBlend > 0.00002f;
        if (shouldAnimBlendY)
        {
            yIndex = AnimationController_GetAnim(ac, aMiddle, yi + 1);
            AnimationController_SampleAnimationPose(ac, ac->mAnimPoseB, yIndex, ac->mAnimTime.y);
            MergeAnims(ac->mAnimPoseA, ac->mAnimPoseB, EaseOut(yBlend), ac->mNumNodes);
        }

        // if anim is two seconds animStep is 0.5 because we are using normalized value
        float yAnimStep = 1.0f / ac->mPrefab->animations[yIndex].duration;
        ac->mAnimTime.y += animSpeed * yAnimStep * deltaTime;
        ac->mAnimTime.y  = Fractf(ac->mAnimTime.y);
    }
    ac->mLastAnim = yIndex;

    if (!wasTriggerState) {
        AnimationController_UploadPose(ac, ac->mAnimPoseA);
    }
    else {
        if (!!(ac->mTriggerOpt & eAnimTriggerOpt_Standing) && y > 0.001f)
            AnimationController_UploadPoseUpperLower(ac, ac->mAnimPoseA, ac->mAnimPoseC);
        else
            AnimationController_UploadPose(ac, ac->mAnimPoseC);
    }
}

void AnimationController_Clear(AnimationController* animSystem)
{
}

void DestroyAnimationSystem()
{ }
    
