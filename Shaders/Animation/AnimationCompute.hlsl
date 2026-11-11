#define INT16_SUPPORTED   0
#define FLOAT16_SUPPORTED 0
#pragma pack_matrix(row_major)

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
    uint animJoints[9];
    uint animRatio[2];  // unorm16x4
    uint animWeights;   // unorm8x4
    uint animIdx4;      // u8x4
    uint neckSpineAngle;
};

struct AnimationData {
    uint  frameOffset;
    uint  numFrames;
    float invRotScale; // u16
    uint  numJoints; // u16
    uint  numNodes;  // u16
    float duration;     
    uint  jointOffset;  
    uint  invBindOffset;
    uint  hierarchyOffset;
    uint  neckSpineNodes;
};

StructuredBuffer<uint>               animPoses             : register(t0); // per bone * frame
StructuredBuffer<uint>               animHierarchy         : register(t1); // per skin * bone
StructuredBuffer<AnimationData>      animData              : register(t2); // per animation
StructuredBuffer<uint>               joints                : register(t3); // per instance * bone
StructuredBuffer<mat4x4>             inverseBindMatrices   : register(t4); // per skin * bone
StructuredBuffer<AnimationInstance>  animInstances         : register(t5); // per instance 

RWStructuredBuffer<uint> outBoneMtx : register(u0, space1); // per instance * bone

cbuffer params : register(b0, space2)
{
    int numInstances;
};

void WriteBone(mat4x4 bone, uint idx)
{
    uint base = AnimMtxNumF32 * idx;
    outBoneMtx[base + 0] = PackHalf2(f16_2(bone[0][0], bone[0][1]));
    outBoneMtx[base + 1] = PackHalf2(f16_2(bone[0][2], bone[1][0]));
    outBoneMtx[base + 2] = PackHalf2(f16_2(bone[1][1], bone[1][2]));
    outBoneMtx[base + 3] = PackHalf2(f16_2(bone[2][0], bone[2][1]));
    outBoneMtx[base + 4] = PackHalf2(f16_2(bone[2][2], 0.0));
    outBoneMtx[base + 5] = asuint(bone[3].x);
    outBoneMtx[base + 6] = asuint(bone[3].y);
    outBoneMtx[base + 7] = asuint(bone[3].z);
}

f16_4 GetHalf4(StructuredBuffer<uint> buffer, uint idx)
{
    return VecCombine(UnpackHalf2(buffer[idx]), UnpackHalf2(buffer[idx + 1]));
}

bool IsJointActive(in uint animJoints[9], uint animSlot, uint joint)
{
    if (animSlot == 0) return true;
    if (joint >= 96u) return false;
    uint idx = (animSlot - 1u) * 3u + (joint >> 5u);
    return (animJoints[idx] >> (joint & 31u)) & 1u;
}

mat4x4 LoadInvBind(uint idx, uint invBindOffset)
{
    return (inverseBindMatrices[invBindOffset + idx]);
}

f16_2x4 LoadPose(uint i, uint frameOffset, uint frame)
{
    const uint AnimPoseSize = 4u;
    uint pose = ((frameOffset + frame) * MAX_BONES + i) * AnimPoseSize;
    f16_2x4 result;
    result[0] = GetHalf4(animPoses, pose + 0);
    result[1] = GetHalf4(animPoses, pose + 2);
    return result;
}

Pose SampleLocal(uint i, AnimationData d, uint2 ab, f16 t)
{
    f16_2x4 a = LoadPose(i, d.frameOffset, ab.x);
    f16_2x4 b = LoadPose(i, d.frameOffset, ab.y);
    Pose res;
    res.translation = VecLerp(a[0], b[0], t);
    res.rotation    = QNlerp(a[1], b[1], t);
    return res;
}

u16 GetParentIndex(u16 idx, uint hierarchyOffset) { 
    return (u16)(animHierarchy[hierarchyOffset + idx] & 0xFFFF); 
}

void FlattenBoneMatrices(uint numNodes, uint hierarchyOffset, inout Pose poses[MAX_BONES])
{
    // 0 is root node
    for (uint idx = 1; idx < numNodes; idx++)
    {
        // if (parentIdx == 0xFFFFu) continue;
        Pose parent = poses[GetParentIndex(idx, hierarchyOffset)];
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
    uint instanceIdx = GlobalInvocationID.x;
    if (instanceIdx >= numInstances) return;
    
    AnimationInstance anim = animInstances[instanceIdx];
    
    Pose poses[MAX_BONES];
    AnimationData data = animData[anim.animIdx4 & 0xFFu];

    uint ratio = anim.animRatio[0] & 0xFFFFu;
    f32 animT = UnpackUnorm16(ratio) * float(data.numFrames);
    uint2 frameABIdx = uint2(uint(Floorf(animT)), uint(Ceilf(animT)) % uint(data.numFrames));
    f16 progress = f16(Fractf(animT));

    // Base pose
    for (uint i = 0; i < data.numNodes; i++)
        poses[i] = SampleLocal(i, data, frameABIdx, progress);

    // Overlay animations
    for (uint a = 1; a < 4; a++)
    {
        f32 weight = f32((anim.animWeights >> (a * 8u)) & 0xFFu) / 255.0f;
        if (weight < 0.005f) continue;

        AnimationData layer = animData[(anim.animIdx4 >> (a * 8u)) & 0xFFu];
        uint packed = (anim.animRatio[a >> 1u] >> ((a & 1u) * 16u)) & 0xFFFFu;
        animT = UnpackUnorm16(packed) * float(layer.numFrames);

        frameABIdx = uint2(uint(Floorf(animT)), uint(Ceilf(animT)) % uint(layer.numFrames));
        progress = f16(Fractf(animT));

        for (uint i = 0; i < min(data.numNodes, layer.numNodes); i++)
        {
            if (!IsJointActive(anim.animJoints, a, i)) continue;
            Pose pose = SampleLocal(i, layer, frameABIdx, progress);
            poses[i].translation = VecLerp(poses[i].translation, pose.translation, weight);
            poses[i].rotation = QNlerp(poses[i].rotation, pose.rotation, weight);
        }
    }
    
    for (int i = 0; i < data.numNodes; i++) // normalize only once
        poses[i].rotation = normalize(poses[i].rotation);
    
    u32 neckNode   =  data.neckSpineNodes & 0xFFFFu;
    u32 spineNode  = (data.neckSpineNodes >> 16u) & 0xFFFFu;
    f16 neckAngle  = Unpack16Snorm(anim.neckSpineAngle & 0xFFFFu);
    f16 spineAngle = Unpack16Snorm(anim.neckSpineAngle >> 16u);
    
    if (abs(neckAngle) > 0.05f)
        poses[neckNode].rotation = QMul(QFromYAngle(neckAngle), poses[neckNode].rotation);
    
    if (abs(spineAngle) > 0.05f)
        poses[spineNode].rotation = QMul(QFromXAngle(spineAngle), poses[spineNode].rotation);

    FlattenBoneMatrices(data.numNodes, data.hierarchyOffset, poses);

    int numJoints = int(data.numJoints);
    for (u32 i = 0; i < numJoints; i++)
    {
        Pose pose = poses[joints[data.jointOffset + i]];
        mat4x4 mat = (mat4x4)M44PositionRotationVec(pose.translation, pose.rotation);
        mat = M44Multiply(LoadInvBind(i, data.invBindOffset), mat);
        uint outIdx = instanceIdx * MAX_BONES + i;
        WriteBone(mat, outIdx);
    }
}
