
/******************************************************************************************
*  Purpose:                                                                               *
*    Play Blend and Mix Animations Trigger and Transition Between animations              *
*    Can Play Seperate animations on Upper Body and Lower Body: Sword Slash while Walking *
*    Can Rotate The Head and Spine of humanoid character independently,                   *
*    Allows to look around and turn the body                                              *
*  Good To Know:                                                                          *
*    Stores bone matrices into M33x4Half format and sends to GPU within Textures      *
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

void AnimationController_Create(const SceneBundle* gltfScene, AnimationController* result)
{
    Pose pose;
    AnimNode animNode;
    ANode inputNode;
    s32 childIndex;
    const ASkin* skin = &gltfScene->skins[0];

    if (skin == NULL) {
        AX_WARN("skin is null"); return;
    }
    if (skin->numJoints > MAX_BONES) {
        AX_WARN("number of joints is greater than max capacity"); 
        return; 
    }
    result->mRootNodeIndex = gltfScene->rootNode; // Prefab_FindAnimRootNodeIndex(prefab);
    result->mPrefab        = gltfScene;
    result->mNumJoints     = skin->numJoints;
    result->mRootScale     = gltfScene->nodes[result->mRootNodeIndex].scale[0]; // 0.1610, rcp: 6.2111
    
    ASSERT(result->mRootNodeIndex < MAX_BONES);
    ASSERT(gltfScene->nodes[result->mRootNodeIndex].numChildren > 0); // root node has to have children nodes
    MemSet(result->mChildIndices, 255, sizeof(result->mChildIndices));

    childIndex = 0;
    for (s32 i = 0; i < gltfScene->numNodes; i++)
    {
        inputNode  = gltfScene->nodes[i];
        pose.translation = VecLoad(inputNode.translation);
        pose.rotation    = VecLoad(inputNode.rotation);
        pose.rotation    = QNorm(pose.rotation);

        animNode.numChildren = inputNode.numChildren;
        animNode.childrenStartIndex = childIndex;

        result->mAnimNodes[i] = animNode;
        result->mAnimPoseA[i] = pose;

        for (s32 j = 0; j < animNode.numChildren; j++)
        {
            result->mChildIndices[childIndex++] = (u8)inputNode.children[j];
        }
    }
}

void AnimationController_Clear(AnimationController* ac) { }

void AnimationController_SampleAnimationPose(const AnimationController* ac, Pose pose[MAX_BONES], s32 animIdx, f32 normTime)
{
    const AAnimation* animation = &ac->mPrefab->animations[animIdx];
    const bool reverse = normTime < 0.0f;

    normTime = Minf32(Absf32(normTime), 1.0f);
    if (reverse) normTime = MMAX(1.0f - normTime, 0.0f);

    f32 realTime = normTime * animation->duration;
    
    for (s32 c = 0; c < animation->numChannels; c++)
    {
        const AAnimChannel* channel = &animation->channels[c];
        const AAnimSampler* sampler = &animation->samplers[channel->sampler];
        const s32 targetNode = channel->targetNode;
    
        // // morph targets are not supported
        // if (channel->targetPath == AAnimTargetPath_Weight || 
        //     sampler->interpolation == ASamplerInterpolation_CubicSpline || 
        //     sampler->inputType  != AComponentType_FLOAT || 
        //     sampler->outputType != AComponentType_FLOAT)
        //     continue;
    
        // binary search
        s32 beginIdx = 0;
        s32 endIdx   = sampler->count - 1;

        while (beginIdx + 1 < endIdx)
        {
            s32 mid = (beginIdx + endIdx) >> 1;

            if (realTime < sampler->input[mid])
                endIdx = mid;
            else
                beginIdx = mid;
        }

        if (reverse) XSWAP(int, beginIdx, endIdx);

        const v128f begin = ((v128f*)sampler->output)[beginIdx];
        const v128f end   = ((v128f*)sampler->output)[endIdx];

        float beginTime = Maxf32(0.0001f, realTime - sampler->input[beginIdx]);
        float endTime   = Maxf32(0.0001f, sampler->input[endIdx] - sampler->input[beginIdx]);
        
        if (reverse) XSWAP(float, beginTime, endTime);
        
        const float t = Clamp01f32(beginTime / endTime);

        switch (channel->targetPath)
        {
            case AAnimTargetPath_Translation:
                pose[targetNode].translation = VecLerp(begin, end, t);
                break;
            case AAnimTargetPath_Rotation:
                pose[targetNode].rotation = QNLerp(begin, end, t); // QNormEst maybe
                break;
        };
    }
}


////////            ANIMATED CHARACTER            ////////
// I can use this for more advenced animation
// "mixamorig:Spine"
// "mixamorig:Neck"    
// 
// static inline void RotateNode(Pose* node, float xAngle, float yAngle)
// {
//     node->rotation = QMul(QMul(QFromXAngle(xAngle), QFromYAngle(yAngle)), node->rotation);
// }
// 
// static void MergeAnims(Pose pose0[MAX_BONES], const Pose pose1[MAX_BONES], float animBlend, s32 numNodes)
// {
//     for (s32 i = 0; i < numNodes; i++)
//     {
//         pose0[i].rotation    = QNLerp(pose0[i].rotation, pose1[i].rotation, animBlend); // slerp+norm?
//         pose0[i].translation = VecLerp(pose0[i].translation, pose1[i].translation, animBlend);
//     }
// }
// 
// static void CopyPoses(Pose* destination, const Pose* pose, s32 begin, s32 numPoses)
// {
//     numPoses += begin;
//     for (s32 i = begin; i < numPoses; i++)
//     {
//         destination[i].translation = pose[i].translation;
//         destination[i].rotation    = pose[i].rotation;
//     }
// }
// 
// // when we want to play different animations with lower body and upper body
// void AnimatedCharacter_UploadPoseUpperLower(AnimatedCharacter* ac, const Pose lowerPose[MAX_BONES], const Pose uperPose[MAX_BONES])
// {
//     // apply posess to lower body and upper body seperately, so both of it has diferrent animations
//     CopyPoses(ac->controller.mAnimPoseA, lowerPose, ac->lowerBodyIdxStart, ac->controller.mNumJoints - ac->lowerBodyIdxStart);
//     CopyPoses(ac->controller.mAnimPoseA, uperPose, 0, ac->lowerBodyIdxStart);
// }
//
// handle neck, spine rotations
// if (ac->mSpineNodeIdx != -1 && Absf(ac->mSpineYAngle) + Absf(ac->mSpineXAngle) > MATH_Epsilon) {
//     RotateNode(ac->mAnimPoseA + ac->mSpineNodeIdx, ac->mSpineXAngle, ac->mSpineYAngle); 
// }
//     
// if (ac->mNeckNodeIdx != -1 && (Absf(ac->mNeckYAngle) + Absf(ac->mSpineXAngle) > MATH_Epsilon)) {
//     RotateNode(ac->mAnimPoseA + ac->mNeckNodeIdx, ac->mNeckXAngle, ac->mNeckYAngle); 
// }

