
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
*    Anilcan Gulkaya 2026 anilcangulkaya7@gmail.com github @benanil                       *
*******************************************************************************************/

#include "Include/Animation.h"
#include "Include/Platform.h"
#include "Include/Algorithm.h"
#include "Include/Graphics.h"
#include "Include/GLTFParser.h"
#include "Include/FileSystem.h"
#include "Include/Random.h"
#include "Math/Half.h"

extern RenderState  g_RenderState;

u32 animPoses[MAX_BONES * MAX_GPU_ANIM_FRAMES * ANIM_POSE_NUM_INT32]; // 32mb
u32 animHierarchy[MAX_SKIN_COUNT * ANIM_NODE_COUNT];
u32 animJoints[MAX_BONES * MAX_SKIN_COUNT];
u32 invBindMatrices[MAX_BONES * MAX_SKIN_COUNT * ANIM_MATRIX_NUM_INT32];
GPUAnimationInstance animInstances[MAX_ANIM_INSTANCES];
GPUAnimationData animData[MAX_ANIM_COUNT];
u32 NumGPUAnimations = 0;
s32 AnimTotalFrameOffset = 0;
static u32 AnimTotalJointOffset = 0;
static u32 AnimTotalInvBindOffset = 0;
static u32 AnimTotalHierarchyOffset = 0;

static void StoreHalf4(u32* dst, v128f src)
{
    Float4ToHalf4V((u64*)dst, src);
}

void AnimInitBuffers()
{
    const Uint32 readRasterBit   = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
    const Uint32 writeComputeBit = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
    const Uint32 readCompute     = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;
    const size_t maxBoneMatrices = sizeof(half3x4) * MAX_BONES * MAX_ANIM_INSTANCES;
    g_RenderState.boneBuffer  = CreateBuffer(NULL, maxBoneMatrices , readRasterBit | writeComputeBit, "CPJointMatrices");
    g_RenderState.animPoseBuffer      = CreateBuffer(animPoses      , sizeof(animPoses)      , readCompute, "CPAnimPoses");
    g_RenderState.animHierarchyBuffer = CreateBuffer(animHierarchy  , sizeof(animHierarchy)  , readCompute, "CPAnimHierarchy");
    g_RenderState.animDataBuffer      = CreateBuffer(animData       , sizeof(animData)       , readCompute, "CPAnimationData");
    g_RenderState.jointsBuffer        = CreateBuffer(animJoints     , sizeof(animJoints)     , readCompute, "CPjointsBuffer ");
    g_RenderState.invBindBuffer       = CreateBuffer(invBindMatrices, sizeof(invBindMatrices), readCompute, "CPinvBindBuffer");
    g_RenderState.animInstanceBuffer  = CreateBuffer(animInstances  , sizeof(animInstances)  , readCompute, "CPAnimationInstances");
}

static void UpdateBuffers()
{
    // todo
}

static int AnimationGetGPUData(const SceneBundle* bundle, Pose poses[MAX_BONES], int animIdx, int frameOffset,
                               u32 jointOffset, u32 invBindOffset, u32 hierarchyOffset)
{
    const AAnimation* animation = &bundle->animations[animIdx];
    const ASkin*      skin      = &bundle->skins[0];
    int numFrames = (int)(animation->duration * ANIM_NUM_FRAMES);
    int numPose   = frameOffset * MAX_BONES;

    if (NumGPUAnimations >= MAX_ANIM_COUNT)
    {
        AX_WARN("animation GPU data capacity exceeded anims=%d", NumGPUAnimations);
        return 0;
    }

    for (int i = 0; i < numFrames; i++)
    {
        float norm = (float)i / (float)numFrames;
        SampleSkinnedAnimationPose(bundle, poses, animIdx, norm);
        for (int poseIdx = 0; poseIdx < bundle->numNodes; poseIdx++)
        {
            u32* outPose = animPoses + ((numPose + poseIdx) * ANIM_POSE_NUM_INT32);
            StoreHalf4(outPose, poses[poseIdx].translation);
            StoreHalf4(outPose + 2, poses[poseIdx].rotation);
        }
        numPose += MAX_BONES;
    }

    animData[NumGPUAnimations++] = (GPUAnimationData){
        .frameOffset   = (u32)frameOffset,
        .numFrames     = (u32)numFrames,
        .rootNodeIndex = (u32)bundle->rootNode,
        .numJoints     = (u32)skin->numJoints,
        .numNodes      = (u32)bundle->numNodes,
        .duration      = animation->duration,
        .jointOffset   = jointOffset,
        .invBindOffset = invBindOffset,
        .hierarchyOffset = hierarchyOffset,
        .padding       = 0u,
    };

    return numFrames;
}

static bool StoreBundleSkinGPUData(const SceneBundle* bundle, u32* jointOffset, u32* invBindOffset, u32* hierarchyOffset)
{
    const ASkin* skin = &bundle->skins[0];
    *jointOffset = AnimTotalJointOffset;
    *invBindOffset = AnimTotalInvBindOffset;
    *hierarchyOffset = AnimTotalHierarchyOffset;

    if (*jointOffset + (u32)skin->numJoints > MAX_BONES * MAX_SKIN_COUNT ||
        *invBindOffset + (u32)skin->numJoints > MAX_BONES * MAX_SKIN_COUNT ||
        *hierarchyOffset + (u32)bundle->numNodes > MAX_SKIN_COUNT * ANIM_NODE_COUNT)
    {
        AX_WARN("animation skin GPU data capacity exceeded joints=%d hierarchy=%d", *jointOffset, *hierarchyOffset);
        return false;
    }

    for (int i = 0; i < bundle->numNodes; i++)
    {
        const ANode* node = i < bundle->numNodes ? &bundle->nodes[i] : NULL;
        u32 parent = node && node->parent >= 0 ? (u32)node->parent : 0xFFFFu;
        animHierarchy[*hierarchyOffset + (u32)i] = parent;
    }

    for (int i = 0; i < skin->numJoints; i++)
        animJoints[*jointOffset + (u32)i] = (u32)skin->joints[i];

    const mat4x4* inv = (const mat4x4*)skin->inverseBindMatrices;
    for (int i = 0; i < skin->numJoints; i++)
    {
        u32* outMtx = invBindMatrices + ((*invBindOffset + (u32)i) * ANIM_MATRIX_NUM_INT32);
        StoreHalf4(outMtx + 0, inv[i].r[0]);
        StoreHalf4(outMtx + 2, inv[i].r[1]);
        StoreHalf4(outMtx + 4, inv[i].r[2]);
        StoreHalf4(outMtx + 6, inv[i].r[3]);
    }

    AnimTotalJointOffset += (u32)skin->numJoints;
    AnimTotalInvBindOffset += (u32)skin->numJoints;
    AnimTotalHierarchyOffset += (u32)bundle->numNodes;
    return true;
}

void InitAnimationInstances(void)
{
    if (NumGPUAnimations == 0) return;

    for (u32 i = 0; i < MAX_ANIM_INSTANCES; i++)
    {
        u32 hash = WangHash(i + 645u);
        u32 animIdx = Maxu32(hash % NumGPUAnimations, 1u);
        f32 duration = animData[animIdx].duration;

        animInstances[i] = (GPUAnimationInstance){
            .animIdx = animIdx,
            .timeOffset = NextFloat01(hash) * duration,
        };
    }
}

s32 SceneBundleInitAnimations(const SceneBundle* gltfScene, Pose result[MAX_BONES])
{
    const ASkin* skin = &gltfScene->skins[0];
    if (skin == NULL) {
        AX_WARN("skin is null"); 
        return 0;
    }
    if (gltfScene->numNodes >= MAX_BONES) {
        AX_WARN("number of joints is greater than max capacity"); 
        return 0; 
    }
    
    ASSERT(gltfScene->rootNode < MAX_BONES);
    ASSERT(gltfScene->nodes[gltfScene->rootNode].numChildren > 0); // root node has to have children nodes

    for (s32 i = 0; i < gltfScene->numNodes; i++)
    {
        ANode inputNode = gltfScene->nodes[i];
        result[i] = (Pose){
            .translation = VecLoad(inputNode.translation),
            .rotation    = QNorm(VecLoad(inputNode.rotation))
        };
    }
    return 1;
}

// maybe return animation handle we might want to delete
s32 SceneBundleCreateAnimations(const SceneBundle* bundle)
{
    Pose poses[MAX_BONES];
    if (!SceneBundleInitAnimations(bundle, poses))
        return 0;

    u32 jointOffset = 0;
    u32 invBindOffset = 0;
    u32 hierarchyOffset = 0;
    if (!StoreBundleSkinGPUData(bundle, &jointOffset, &invBindOffset, &hierarchyOffset))
        return 0;

    for (u32 animIdx = 0; animIdx < bundle->numAnimations; animIdx++)
    {
        s32 numFrames = (s32)(bundle->animations[animIdx].duration * ANIM_NUM_FRAMES);
        if (AnimTotalFrameOffset + numFrames > MAX_GPU_ANIM_FRAMES)
        {
            AX_WARN("animation couldn't added frame capacity is not enough");
            break;
        }
        numFrames = AnimationGetGPUData(bundle, poses, (int)animIdx, AnimTotalFrameOffset,
                                        jointOffset, invBindOffset, hierarchyOffset);
        if (numFrames == 0) break;
        AnimTotalFrameOffset += numFrames;
    }
    AX_LOG("num animation: %d", NumGPUAnimations);
    return 1;
}

void SampleSkinnedAnimationPose(const SceneBundle* bundle, Pose pose[MAX_BONES], s32 animIdx, f32 normTime)
{
    const AAnimation* animation = &bundle->animations[animIdx];
    const bool reverse = normTime < 0.0f;

    normTime = Minf32(Absf32(normTime), 1.0f);
    if (reverse) normTime = MMAX(1.0f - normTime, 0.0f);

    f32 realTime = normTime * animation->duration;
    
    for (s32 c = 0; c < animation->numChannels; c++)
    {
        const AAnimChannel* channel = &animation->channels[c];
        const AAnimSampler* sampler = &animation->samplers[channel->sampler];
        // morph targets are not supported
        // if (channel->targetPath == AAnimTargetPath_Weight || sampler->interpolation == ASamplerInterpolation_CubicSpline || sampler->inputType  != AComponentType_FLOAT || sampler->outputType != AComponentType_FLOAT)
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
        
        const float t = Saturatef32(beginTime / endTime);

        switch (channel->targetPath)
        {
            case AAnimTargetPath_Translation:
                pose[channel->targetNode].translation = VecLerp(begin, end, t);
                break;
            case AAnimTargetPath_Rotation:
                pose[channel->targetNode].rotation = QNorm(QSlerp(begin, end, t));
                break;
        }
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

