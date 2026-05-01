#define FLOAT16_SUPPORTED 0
#define INT16_SUPPORTED 0

#include "Bitpack.hlsl"
#include "Math.hlsl"

#define ANIM_NUM_FRAMES   24
#define MAX_ANIM_DURATION 8
#define MAX_ANIM_COUNT    64
#define MaxBoneDepth      32

#define MaxBonePoses    128

struct Pose {
    f16_4 translation;
    f16_4 rotation;
};

struct AnimNode {
    int childrenStartIndex;
    int numChildren;
};

struct PoseInput {
    float4 translation;
    float4 rotation;
};

struct MatrixInput {
    float4 r0;
    float4 r1;
    float4 r2;
    float4 r3;
};

StructuredBuffer<PoseInput> animPoses          : register(t0); // binding 1
StructuredBuffer<uint>     animNodes           : register(t1); // binding 2
StructuredBuffer<uint>     animChildIndices    : register(t2); // binding 3
StructuredBuffer<uint>     joints              : register(t3); // binding 4
StructuredBuffer<MatrixInput> inverseBindMatrices : register(t4); // binding 5

RWStructuredBuffer<uint> outBoneMtx            : register(u0, space1); // binding 6

cbuffer params : register(b0, space2)
{
    float timeSinceStartup;
    float animDuration;
    float numFrames;
    int   rootNodeIndex;
    int   numInstances;
};

void WriteBone(f16_3x4 bone, int idx)
{
    const int MatrixNumInt32 = 6;
    int base = MatrixNumInt32 * idx;
    outBoneMtx[base + 0] = PackHalf2(VecXY(bone[0]));
    outBoneMtx[base + 1] = PackHalf2(VecZW(bone[0]));
    outBoneMtx[base + 2] = PackHalf2(VecXY(bone[1]));
    outBoneMtx[base + 3] = PackHalf2(VecZW(bone[1]));
    outBoneMtx[base + 4] = PackHalf2(VecXY(bone[2]));
    outBoneMtx[base + 5] = PackHalf2(VecZW(bone[2]));
}

f16_2x4 LoadPose(int i, int frame)
{
    PoseInput pose = animPoses[frame * MaxBonePoses + i];
    f16_2x4 result;
    result[0] = f16_4(pose.translation);
    result[1] = f16_4(pose.rotation);
    return result;
}
    // const int AnimPoseSize = 4;
    // int f = frame * MaxBonePoses * AnimPoseSize;
    // f16_4 pos = VecCombine(
    //     UnpackHalf2(animPoses[f + (i * AnimPoseSize) + 0]),
    //     UnpackHalf2(animPoses[f + (i * AnimPoseSize) + 1])
    // );
    // f16_4 rot = VecCombine(
    //     UnpackHalf2(animPoses[f + (i * AnimPoseSize) + 2]),
    //     UnpackHalf2(animPoses[f + (i * AnimPoseSize) + 3])
    // );
    // f16_2x4 mtx = { pos, rot };
    // return mtx;

AnimNode GetAnimNode(int idx)
{
    AnimNode node;
    node.childrenStartIndex = animNodes[idx] & 0xFFFF;
    node.numChildren        = animNodes[idx] >> 16;
    return node;
}

f16_4x4 LoadMatrix(MatrixInput input)
{
    f16_4x4 result;
    result[0] = f16_4(input.r0);
    result[1] = f16_4(input.r1);
    result[2] = f16_4(input.r2);
    result[3] = f16_4(input.r3);
    return result;
}

void RecurseBoneMatrices(int instanceIdx, inout Pose poses[MaxBonePoses])
{
    s32 stack[32];
    s32 stackIndex = 0;
    stack[stackIndex++] = rootNodeIndex;
    f16 parentScale = 0.161000; // VecGetW(poses[rootNodeIndex].translation); // hack for nos
    const s32 skinOffset = 0;

    while (stackIndex)
    {
        s32 idx = stack[--stackIndex];
        Pose parent = poses[idx];
        AnimNode node = GetAnimNode(skinOffset + idx);

        for (s32 c = 0; c < node.numChildren; c++)
        {
            s32 child = (s32)animChildIndices[skinOffset + node.childrenStartIndex + c];
            Pose pose = poses[child];

            f16_4 t = VecMulf(pose.translation, parentScale);
            t = QMulVec3V(parent.rotation, t);
            // pose.translation = t + parent.translation.xyz;
            pose.translation = VecAdd(t, parent.translation); 
            pose.rotation = QMul(pose.rotation, parent.rotation);

            poses[child] = pose;
            stack[stackIndex++] = child;
        }

        parentScale = 1.0f; // to be changed
    }
}

[numthreads(32, 1, 1)]
void main(uint3 GlobalInvocationID : SV_DispatchThreadID)
{
    int instanceIdx = int(GlobalInvocationID.x);
    // if (instanceIdx >= numInstances)
    //     return;

    float animRatio = Fractf((timeSinceStartup + (instanceIdx * 0.1)) / animDuration);
    float animT     = 0.0f; // animRatio * numFrames;
    int frameAIdx   = int(Floorf(animT));
    int frameBIdx   = int(Ceilf(animT)) % int(numFrames);
    
    Pose poses[MaxBonePoses];
    f16 animProgress = f16(Fractf(animT));
    for (int i = 0; i < MaxBonePoses; i++)
    {
        f16_2x4 a = LoadPose(i, frameAIdx);
        f16_2x4 b = LoadPose(i, frameBIdx);
        poses[i].translation = VecLerp(a[0], b[0], animProgress);
        poses[i].rotation    = QNlerp(a[1], b[1], animProgress);
    }

    RecurseBoneMatrices(instanceIdx, poses);

    f16 rootScale = 0.161000; // to be changed
    int numJoints = 69; // to be changed
    for (s32 i = 0; i < numJoints; i++)
    {
        Pose pose = poses[joints[i]];
        f16_4 pos = pose.translation;
        if (i != rootNodeIndex)
             pos = VecMulf(pos, rootScale);
         
        f16_4x4 mat = M44PositionRotationVec(pos, pose.rotation);
        mat = M44Multiply(LoadMatrix(inverseBindMatrices[i]), mat);
        int outIdx = instanceIdx * MaxBonePoses + i;
        WriteBone((f16_3x4)M44Transpose(mat), outIdx);
    }
}
