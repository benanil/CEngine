
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
#include "Include/Memory.h"
#include "Include/Random.h"
#include "Include/Bitset.h"
#include "Math/Half.h"

extern SDL_GPUDevice* g_GPUDevice;

static void StoreHalf4(u32* dst, v128f src)
{
    Float4ToHalf4V((u64*)dst, src);
}

static u32 AnimationBundleFrameCount(const SceneBundle* bundle)
{
    u32 frameCount = 0u;
    for (u32 animIdx = 0u; animIdx < (u32)bundle->numAnimations; animIdx++)
        frameCount += (u32)(bundle->animations[animIdx].duration * ANIM_NUM_FRAMES);
    return frameCount;
}

void AnimationSystem_Init(AnimationSystem* anims)
{
    MemsetZero(anims, sizeof(*anims));
    RangeAllocator_Init(&anims->frameAllocator, anims->freeFrameRanges, MAX_ANIM_COUNT, MAX_GPU_ANIM_FRAMES);
}

void AnimationSystem_Destroy(AnimationSystem* anims)
{
    if (anims->boneBuffer)      SDL_ReleaseGPUBuffer(g_GPUDevice, anims->boneBuffer);
    if (anims->poseBuffer)      SDL_ReleaseGPUBuffer(g_GPUDevice, anims->poseBuffer);
    if (anims->hierarchyBuffer) SDL_ReleaseGPUBuffer(g_GPUDevice, anims->hierarchyBuffer);
    if (anims->dataBuffer)      SDL_ReleaseGPUBuffer(g_GPUDevice, anims->dataBuffer);
    if (anims->jointsBuffer)    SDL_ReleaseGPUBuffer(g_GPUDevice, anims->jointsBuffer);
    if (anims->invBindBuffer)   SDL_ReleaseGPUBuffer(g_GPUDevice, anims->invBindBuffer);
    if (anims->instanceBuffer)  SDL_ReleaseGPUBuffer(g_GPUDevice, anims->instanceBuffer);
    MemsetZero(anims, sizeof(*anims));
}

static void AnimationSystem_EnsureBuffers(AnimationSystem* anims)
{
    if (anims->poseBuffer) return;
    const size_t maxBoneMatrices = sizeof(half3x4) * MAX_BONES * MAX_ANIM_INSTANCES;
    const size_t poseBytes       = (size_t)MAX_BONES * MAX_GPU_ANIM_FRAMES * ANIM_POSE_NUM_INT32 * sizeof(u32);
    const size_t hierarchyBytes  = (size_t)MAX_SKIN_COUNT * ANIM_NODE_COUNT * sizeof(u32);
    const size_t jointBytes      = (size_t)MAX_BONES * MAX_SKIN_COUNT * sizeof(u32);
    const size_t invBindBytes    = (size_t)MAX_BONES * MAX_SKIN_COUNT * ANIM_MATRIX_NUM_INT32 * sizeof(u32);
    anims->boneBuffer      = CreateBuffer(NULL, maxBoneMatrices, BReadRasterBit | BWriteComputeBit, "CPJointMatrices");
    anims->poseBuffer      = CreateBuffer(NULL, poseBytes      , BReadCompute, "CPAnimPoses");
    anims->hierarchyBuffer = CreateBuffer(NULL, hierarchyBytes , BReadCompute, "CPAnimHierarchy");
    anims->dataBuffer      = CreateBuffer(NULL, sizeof(anims->animData), BReadCompute, "CPAnimationData");
    anims->jointsBuffer    = CreateBuffer(NULL, jointBytes     , BReadCompute, "CPjointsBuffer ");
    anims->invBindBuffer   = CreateBuffer(NULL, invBindBytes   , BReadCompute, "CPinvBindBuffer");
    anims->instanceBuffer  = CreateBuffer(NULL, MAX_ANIM_INSTANCES * sizeof(GPUAnimationInstance), BReadCompute, "CPAnimationInstances");
}

static int AnimationGetGPUData(AnimationSystem* anims, const SceneBundle* bundle, Pose poses[MAX_BONES], int animIdx,
                               u32 animSlot, u32 frameOffset, u32 jointOffset, u32 invBindOffset, u32 hierarchyOffset)
{
    const AAnimation* animation = &bundle->animations[animIdx];
    const ASkin*      skin      = &bundle->skins[0];
    int numFrames = (int)(animation->duration * ANIM_NUM_FRAMES);

    // bake into transient scratch and upload only this animation's frame range
    size_t bakeBytes = (size_t)numFrames * MAX_BONES * ANIM_POSE_NUM_INT32 * sizeof(u32);
    ArenaMark mark = ArenaSave(&GlobalArena);
    u32* bakedPoses = (u32*)ArenaAllocZero(&GlobalArena, bakeBytes);

    int numPose = 0;
    for (int i = 0; i < numFrames; i++)
    {
        float norm = (float)i / (float)numFrames;
        SampleSkinnedAnimationPose(bundle, poses, animIdx, norm);
        for (int poseIdx = 0; poseIdx < bundle->numNodes; poseIdx++)
        {
            u32* outPose = bakedPoses + ((numPose + poseIdx) * ANIM_POSE_NUM_INT32);
            StoreHalf4(outPose, poses[poseIdx].translation);
            StoreHalf4(outPose + 2, poses[poseIdx].rotation);
        }
        numPose += MAX_BONES;
    }

    if (numFrames > 0)
        UpdateGPUBuffer(anims->poseBuffer, bakedPoses, bakeBytes,
                        (size_t)frameOffset * MAX_BONES * ANIM_POSE_NUM_INT32 * sizeof(u32));
    ArenaRestore(&GlobalArena, mark);
    anims->animData[animSlot] = (GPUAnimationData){
        .frameOffset   = frameOffset,
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

static bool StoreBundleSkinGPUData(AnimationSystem* anims, const SceneBundle* bundle, u32* jointOffset, u32* invBindOffset, u32* hierarchyOffset)
{
    const ASkin* skin = &bundle->skins[0];
    if (*jointOffset + (u32)skin->numJoints > MAX_BONES * MAX_SKIN_COUNT ||
        *invBindOffset + (u32)skin->numJoints > MAX_BONES * MAX_SKIN_COUNT ||
        *hierarchyOffset + (u32)bundle->numNodes > MAX_SKIN_COUNT * ANIM_NODE_COUNT)
    {
        AX_WARN("animation skin GPU data capacity exceeded joints=%d hierarchy=%d", *jointOffset, *hierarchyOffset);
        return false;
    }

    ArenaMark mark = ArenaSave(&GlobalArena);
    u32* hierarchy = (u32*)ArenaAllocZero(&GlobalArena, (size_t)bundle->numNodes * sizeof(u32));
    u32* joints    = (u32*)ArenaAllocZero(&GlobalArena, (size_t)skin->numJoints * sizeof(u32));
    u32* invBind   = (u32*)ArenaAllocZero(&GlobalArena, (size_t)skin->numJoints * ANIM_MATRIX_NUM_INT32 * sizeof(u32));

    for (int i = 0; i < bundle->numNodes; i++)
    {
        const ANode* node = &bundle->nodes[i];
        hierarchy[i] = node->parent >= 0 ? (u32)node->parent : 0xFFFFu;
    }

    for (int i = 0; i < skin->numJoints; i++)
        joints[i] = (u32)skin->joints[i];

    const mat4x4* inv = (const mat4x4*)skin->inverseBindMatrices;
    for (int i = 0; i < skin->numJoints; i++)
    {
        u32* outMtx = invBind + (i * ANIM_MATRIX_NUM_INT32);
        StoreHalf4(outMtx + 0, inv[i].r[0]);
        StoreHalf4(outMtx + 2, inv[i].r[1]);
        StoreHalf4(outMtx + 4, inv[i].r[2]);
        StoreHalf4(outMtx + 6, inv[i].r[3]);
    }

    if (bundle->numNodes > 0)
        UpdateGPUBuffer(anims->hierarchyBuffer, hierarchy, (size_t)bundle->numNodes * sizeof(u32),
                        (size_t)*hierarchyOffset * sizeof(u32));
    if (skin->numJoints > 0)
    {
        UpdateGPUBuffer(anims->jointsBuffer, joints, (size_t)skin->numJoints * sizeof(u32),
                        (size_t)*jointOffset * sizeof(u32));
        UpdateGPUBuffer(anims->invBindBuffer, invBind, (size_t)skin->numJoints * ANIM_MATRIX_NUM_INT32 * sizeof(u32),
                        (size_t)*invBindOffset * ANIM_MATRIX_NUM_INT32 * sizeof(u32));
    }
    ArenaRestore(&GlobalArena, mark);
    return true;
}

void AnimationSystem_UpdateInstances(AnimationSystem* anims, const GPUAnimationInstance* instances, u32 count)
{
    if (!anims->instanceBuffer || !instances || count == 0) return;
    count = Minu32(count, MAX_ANIM_INSTANCES);
    UpdateGPUBuffer(anims->instanceBuffer, instances, count * sizeof(GPUAnimationInstance), 0);
}

void AnimationSystem_SetInstance(AnimationSystem* anims, u32 sparseIdx, GPUAnimationInstance instance)
{
    if (!anims->instanceBuffer || sparseIdx >= MAX_ANIM_INSTANCES) return;
    UpdateGPUBuffer(anims->instanceBuffer, &instance, sizeof(instance), sparseIdx * sizeof(GPUAnimationInstance));
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

s32 AnimationSystem_AppendBundle(AnimationSystem* anims, const SceneBundle* bundle, AnimationBundleAlloc* outAlloc)
{
    if (outAlloc) MemsetZero(outAlloc, sizeof(*outAlloc));
    Pose poses[MAX_BONES];
    if (!SceneBundleInitAnimations(bundle, poses))
        return 0;

    AnimationSystem_EnsureBuffers(anims);

    u32 animCount  = (u32)bundle->numAnimations;
    u32 frameCount = AnimationBundleFrameCount(bundle);
    s32 animOffset = BitsetFindEmptyRange(anims->usedAnimSlots, MAX_ANIM_COUNT, animCount);
    s32 skinSlot   = BitsetFindFirstEmpty(anims->usedSkinSlots, MAX_SKIN_COUNT);
    u32 frameOffset = 0u;

    if (animOffset < 0)
    {
        AX_WARN("animation GPU data capacity exceeded anims=%d count=%d", anims->numAnimations, animCount);
        return 0;
    }
    if (skinSlot < 0)
    {
        AX_WARN("animation skin slot capacity exceeded");
        return 0;
    }
    if (!RangeAllocator_Alloc(&anims->frameAllocator, frameCount, &frameOffset))
    {
        AX_WARN("animation frame capacity exceeded count=%d max=%d", frameCount, MAX_GPU_ANIM_FRAMES);
        return 0;
    }

    BitsetSetRange(anims->usedAnimSlots, (u32)animOffset, animCount, true);
    BitsetSet(anims->usedSkinSlots, skinSlot);

    u32 jointOffset = (u32)skinSlot * MAX_BONES;
    u32 invBindOffset = (u32)skinSlot * MAX_BONES;
    u32 hierarchyOffset = (u32)skinSlot * ANIM_NODE_COUNT;
    if (!StoreBundleSkinGPUData(anims, bundle, &jointOffset, &invBindOffset, &hierarchyOffset))
    {
        BitsetSetRange(anims->usedAnimSlots, (u32)animOffset, animCount, false);
        BitsetReset(anims->usedSkinSlots, skinSlot);
        RangeAllocator_Free(&anims->frameAllocator, frameOffset, frameCount);
        return 0;
    }

    u32 frameCursor = frameOffset;
    for (u32 animIdx = 0; animIdx < bundle->numAnimations; animIdx++)
    {
        s32 numFrames = (s32)(bundle->animations[animIdx].duration * ANIM_NUM_FRAMES);
        numFrames = AnimationGetGPUData(anims, bundle, poses, (int)animIdx,
                                        (u32)animOffset + animIdx, frameCursor,
                                        jointOffset, invBindOffset, hierarchyOffset);
        if (numFrames == 0)
        {
            AnimationSystem_RemoveBundle(anims, (AnimationBundleAlloc){ (u32)animOffset, animCount, frameOffset, frameCount, (u32)skinSlot });
            return 0;
        }
        frameCursor += (u32)numFrames;
    }

    if (animCount > 0u)
        UpdateGPUBuffer(anims->dataBuffer, anims->animData + animOffset,
                        animCount * sizeof(GPUAnimationData),
                        (u32)animOffset * sizeof(GPUAnimationData));

    anims->numAnimations += animCount;
    if (outAlloc)
    {
        *outAlloc = (AnimationBundleAlloc){
            .animOffset = (u32)animOffset,
            .animCount = animCount,
            .frameOffset = frameOffset,
            .frameCount = frameCount,
            .skinSlot = (u32)skinSlot,
        };
    }
    AX_LOG("num animation: %d", anims->numAnimations);
    return 1;
}

void AnimationSystem_RemoveBundle(AnimationSystem* anims, AnimationBundleAlloc alloc)
{
    if (!anims || alloc.skinSlot >= MAX_SKIN_COUNT) return;
    BitsetSetRange(anims->usedAnimSlots, alloc.animOffset, alloc.animCount, false);
    BitsetReset(anims->usedSkinSlots, (s32)alloc.skinSlot);
    RangeAllocator_Free(&anims->frameAllocator, alloc.frameOffset, alloc.frameCount);
    if (alloc.animCount > 0u)
    {
        MemsetZero(anims->animData + alloc.animOffset, alloc.animCount * sizeof(GPUAnimationData));
        if (anims->dataBuffer)
            UpdateGPUBuffer(anims->dataBuffer, anims->animData + alloc.animOffset,
                            alloc.animCount * sizeof(GPUAnimationData),
                            alloc.animOffset * sizeof(GPUAnimationData));
        anims->numAnimations = anims->numAnimations >= alloc.animCount ? anims->numAnimations - alloc.animCount : 0u;
    }
}

u32 AnimationSystem_GetNthUsedAnim(const AnimationSystem* anims, u32 ordinal)
{
    if (!anims || anims->numAnimations == 0u) return 0u;
    for (u32 i = 0u; i < MAX_ANIM_COUNT; i++)
    {
        if (!BitsetGet(anims->usedAnimSlots, (s32)i)) continue;
        if (ordinal == 0u) return i;
        ordinal--;
    }
    return 0u;
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

