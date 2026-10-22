#define INT16_SUPPORTED 0
#define FLOAT16_SUPPORTED 1

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
    uint  animIdx2;   // u16x2
    uint  animRatio2; // unorm16x2
    uint  blend; // f16, u8 anim1BoneBegin, u8 anim1BoneEnd
    uint  neckSpineAngle;
};

struct AnimationData {
    uint frameOffset;
    uint numFrames;
    uint rootNodeIndex; // u16
    uint numJoints;     // u16
    uint numNodes;      // u16
    float duration;     
    uint jointOffset;  
    uint invBindOffset;
    uint hierarchyOffset;
    uint neckSpineNodes;
};

StructuredBuffer<uint>               animPoses             : register(t0); // per bone * frame
StructuredBuffer<uint>               animHierarchy         : register(t1); // per skin * bone
StructuredBuffer<AnimationData>      animData              : register(t2); // per animation
StructuredBuffer<uint>               joints                : register(t3); // per instance * bone
StructuredBuffer<uint>               inverseBindMatrices   : register(t4); // per skin * bone
StructuredBuffer<AnimationInstance>  animInstances         : register(t5); // per instance 
StructuredBuffer<uint>               visibleSparseIndices  : register(t6); // invisible animations are not calculated nice optimization

RWStructuredBuffer<uint> outBoneMtx : register(u0, space1); // per instance * bone

cbuffer params : register(b0, space2)
{
    int numInstances;
};

// Bone matrices are stored fp16 (12 halves / 6 u32) to keep the skinning gather at half bandwidth
// and fp16-friendly registers. The metre-scale translation column quantizes to ~1 mm; to keep that
// from showing as swim, bake the bounds center into the translation before packing (needs the
// per-instance center plumbed in) - it is currently subtracted later in AnimateVertices.
void WriteBone(f16_3x4 bone, uint idx)
{
    uint base = MatrixNumInt32 * idx;
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

f16_2x4 LoadPose(uint i, uint frameOffset, uint frame)
{
    const uint AnimPoseSize = 4u;
    uint pose = ((frameOffset + frame) * MAX_BONES + i) * AnimPoseSize;
    f16_2x4 result;
    result[0] = GetHalf4(animPoses, pose + 0);
    result[1] = GetHalf4(animPoses, pose + 2);
    return result;
}

u16 GetParentIndex(u16 idx, uint hierarchyOffset) { 
    return (u16)(animHierarchy[hierarchyOffset + idx] & 0xFFFF); 
}

f16_4x4 LoadMatrix(uint idx, uint invBindOffset)
{
    uint mtx = uint(invBindOffset + idx) * ANIM_MATRIX_NUM_INT32;
    f16_4x4 result;
    result[0] = GetHalf4(inverseBindMatrices, mtx + 0);
    result[1] = GetHalf4(inverseBindMatrices, mtx + 2);
    result[2] = GetHalf4(inverseBindMatrices, mtx + 4);
    result[3] = GetHalf4(inverseBindMatrices, mtx + 6);
    return result;
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
    uint visibleIdx = GlobalInvocationID.x;
    if (visibleIdx >= uint(numInstances)) return;
    
    uint instanceIdx = visibleSparseIndices[visibleIdx];
    if (instanceIdx >= uint(MAX_ANIM_INSTANCES)) return;
    AnimationInstance anim = animInstances[instanceIdx];
    AnimationData     data = animData[anim.animIdx2 & 0xFFFFu];

    f32 animT = UnpackUnorm16(anim.animRatio2) * float(data.numFrames);
    uint2 frameABIdx = uint2(uint(Floorf(animT)), uint(Ceilf(animT)) % uint(data.numFrames));
    
    Pose poses[MAX_BONES];
    f16 animProgress = f16(Fractf(animT));
    for (int i = 0; i < data.numNodes; i++)
    {
        f16_2x4 a = LoadPose(i, int(data.frameOffset), frameABIdx.x);
        f16_2x4 b = LoadPose(i, int(data.frameOffset), frameABIdx.y);
        poses[i].translation = VecLerp(a[0], b[0], animProgress);
        poses[i].rotation    = QNlerp(a[1], b[1], animProgress);
    }

    f16 blend = UnpackHalf(anim.blend);
    if (blend > 0.005f)
    {
        data  = animData[anim.animIdx2 >> 16];
        animT = UnpackUnorm16(anim.animRatio2 >> 16) * float(data.numFrames);
        frameABIdx = uint2(uint(Floorf(animT)), uint(Ceilf(animT)) % uint(data.numFrames));
        
        animProgress = f16(Fractf(animT));
        uint anim1BoneStart = (anim.blend >> 16) & 0xff;
        uint anim1BoneEnd   = (anim.blend >> 24) & 0xff;
        // merge animations
        for (uint i = anim1BoneStart; i < anim1BoneEnd; i++)
        {
            f16_2x4 a = LoadPose(i, data.frameOffset, frameABIdx.x);
            f16_2x4 b = LoadPose(i, data.frameOffset, frameABIdx.y);
            poses[i].translation = VecLerp(poses[i].translation,  VecLerp(a[0], b[0], animProgress), blend);
            poses[i].rotation    = QNlerp(poses[i].rotation    ,  QNlerp(a[1], b[1], animProgress), blend);
        }
    }
    
    u32 neckNode  = data.neckSpineNodes & 0xFFFFu;
    u32 spineNode = (data.neckSpineNodes >> 16u) & 0xFFFFu;
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
        f16_4x4 mat = M44PositionRotationVec(pose.translation, pose.rotation);
        mat = M44Multiply(LoadMatrix(i, data.invBindOffset), mat);
        uint outIdx = instanceIdx * MAX_BONES + i;
        WriteBone((f16_3x4)M44Transpose(mat), outIdx);
    }
}
