#define INT16_SUPPORTED 0

#include "../../Include/RenderLimits.h"
#include "../Bitpack.hlsl"
#include "../Math.hlsl"
#include "../CommonStructs.hlsl"

struct Pose {
    f16_4 translation;
    f16_4 rotation;
};

struct AnimNode {
    u16 childrenStartIndex;
    u16 numChildren;
};

struct AnimationInstance {
    uint animIdx;
    float timeOffset;
};

struct AnimationData {
    uint frameOffset;
    uint numFrames;
    uint rootNodeIndex;
    uint numJoints;
    uint numNodes;
    float duration;
};

StructuredBuffer<uint>               animPoses             : register(t0); // per bone * frame
StructuredBuffer<uint>               animHierarchy         : register(t1); // per skin * bone
StructuredBuffer<AnimationData>      animData              : register(t2); // per animation
StructuredBuffer<uint>               joints                : register(t3); // per instance * bone
StructuredBuffer<uint>               inverseBindMatrices   : register(t4); // per skin * bone
StructuredBuffer<AnimationInstance>  animInstances         : register(t5); // per instance 
StructuredBuffer<uint>               visibleSparseIndices  : register(t6); // unique visible instances

RWStructuredBuffer<uint> outBoneMtx          : register(u0, space1); // per instance * bone

cbuffer params : register(b0, space2)
{
    float timeSinceStartup;
    int   numInstances;
};

void WriteBone(f16_3x4 bone, int idx)
{
    int base = MatrixNumInt32 * idx;
    outBoneMtx[base + 0] = PackHalf2(VecXY(bone[0]));
    outBoneMtx[base + 1] = PackHalf2(VecZW(bone[0]));
    outBoneMtx[base + 2] = PackHalf2(VecXY(bone[1]));
    outBoneMtx[base + 3] = PackHalf2(VecZW(bone[1]));
    outBoneMtx[base + 4] = PackHalf2(VecXY(bone[2]));
    outBoneMtx[base + 5] = PackHalf2(VecZW(bone[2]));
}

f16_4 GetHalf4(StructuredBuffer<uint> buffer, uint idx)
{
    return VecCombine(UnpackHalf2(buffer[idx]), UnpackHalf2(buffer[idx + 1]));
}

f16_2x4 LoadPose(int i, int frameOffset, int frame)
{
    const int AnimPoseSize = 4;
    int pose = ((frameOffset + frame) * MAX_BONES + i) * AnimPoseSize;
    f16_2x4 result;
    result[0] = GetHalf4(animPoses, pose + 0);
    result[1] = GetHalf4(animPoses, pose + 2);
    return result;
}

AnimNode GetAnimNode(int idx)
{
    u16 packed = animHierarchy[idx];
    AnimNode node;
    node.childrenStartIndex = packed & 0xFF;
    node.numChildren        = (packed >> 8) & 0xFF;
    return node;
}

u16 GetParentIndex(u16 idx) { return (u16)(animHierarchy[idx] & 0xFFFF); }

f16_4x4 LoadMatrix(int idx)
{
    const int MatrixSize = 8;
    int mtx = idx * MatrixSize;
    f16_4x4 result;
    result[0] = GetHalf4(inverseBindMatrices, mtx + 0);
    result[1] = GetHalf4(inverseBindMatrices, mtx + 2);
    result[2] = GetHalf4(inverseBindMatrices, mtx + 4);
    result[3] = GetHalf4(inverseBindMatrices, mtx + 6);
    return result;
}

void FlattenBoneMatrices(uint numNodes, inout Pose poses[MAX_BONES])
{
    // 0 is root node
    for (u16 idx = 1; idx < numNodes; idx++)
    {
        u16 parentIdx = GetParentIndex(idx);
        // if (parentIdx == 0xFFFFu) continue;
        Pose parent = poses[parentIdx];
        Pose pose = poses[idx];
        f16_4 t = QMulVec3V(parent.rotation, pose.translation);
        pose.translation = VecAdd(t, parent.translation);
        pose.rotation = QMul(pose.rotation, parent.rotation);
        poses[idx] = pose;
    }
}

[numthreads(32, 1, 1)]
void main(uint3 GlobalInvocationID : SV_DispatchThreadID)
{
    uint visibleSlot = GlobalInvocationID.x;
    if (visibleSlot >= uint(numInstances))
        return;

    uint instanceIdx = visibleSparseIndices[visibleSlot];
    AnimationInstance anim = animInstances[instanceIdx];
    AnimationData     data = animData[anim.animIdx];
    
    float animRatio = Fractf((timeSinceStartup + anim.timeOffset) / data.duration);
    float animT     = animRatio * float(data.numFrames);
    int frameAIdx   = int(Floorf(animT));
    int frameBIdx   = int(Ceilf(animT)) % int(data.numFrames);
    
    Pose poses[MAX_BONES];
    f16 animProgress = f16(Fractf(animT));
    for (int i = 0; i < data.numNodes; i++)
    {
        f16_2x4 a = LoadPose(i, int(data.frameOffset), frameAIdx);
        f16_2x4 b = LoadPose(i, int(data.frameOffset), frameBIdx);
        poses[i].translation = VecLerp(a[0], b[0], animProgress);
        poses[i].rotation    = QNlerp(a[1], b[1], animProgress);
    }

    FlattenBoneMatrices(data.numNodes, poses);

    int numJoints = int(data.numJoints);
    for (s16 i = 0; i < numJoints; i++)
    {
        Pose pose = poses[joints[i]];
        f16_4x4 mat = M44PositionRotationVec(pose.translation, pose.rotation);
        mat = M44Multiply(LoadMatrix(i), mat);
        int outIdx = instanceIdx * MAX_BONES + i;
        WriteBone((f16_3x4)M44Transpose(mat), outIdx);
    }
}
