@ctype mat4 Matrix4

@block common

struct BoneWord {
    uint v;
};

// from directxmath
vec4 QuaternionMul(vec4 Q1, vec4 Q2)
{
    return vec4((Q2.w * Q1.x) + (Q2.x * Q1.w) + (Q2.y * Q1.z) - (Q2.z * Q1.y),
                (Q2.w * Q1.y) - (Q2.x * Q1.z) + (Q2.y * Q1.w) + (Q2.z * Q1.x),
                (Q2.w * Q1.z) + (Q2.x * Q1.y) - (Q2.y * Q1.x) + (Q2.z * Q1.w),
                (Q2.w * Q1.w) - (Q2.x * Q1.x) - (Q2.y * Q1.y) - (Q2.z * Q1.z));
}

vec3 QuaternionRotateVector(vec4 quat, vec3 vec)
{
    return vec + (cross(quat.xyz, cross(quat.xyz, vec) + (vec * quat.w)) * 2.0f);
}

mat4 MatrixFromQuaternion(vec4 q)
{
    mat4 mat = mat4(0.0);
    const float num9 = q.x * q.x, num8 = q.y * q.y,
    num7 = q.z * q.z, num6 = q.x * q.y,
    num5 = q.z * q.w, num4 = q.z * q.x,
    num3 = q.y * q.w, num2 = q.y * q.z,
    num  = q.x * q.w;

    mat[0][0] = 1.0f - (2.0f * (num8 + num7));
    mat[0][1] = 2.0f * (num6 + num5);
    mat[0][2] = 2.0f * (num4 - num3);
    mat[1][0] = 2.0f * (num6 - num5);
    mat[1][1] = 1.0f - (2.0f * (num7 + num9));
    mat[1][2] = 2.0f * (num2 + num);
    mat[2][0] = 2.0f * (num4 + num3);
    mat[2][1] = 2.0f * (num2 - num);
    mat[2][2] = 1.0f - (2.0f * (num8 + num9));
    
    mat[3][3] = 1.0f;
    return mat;
}

@end

@cs cs
@include_block common

#define MaxBonePoses   128

struct AnimationData
{
    int poseStart;
    int numFrames;
};

struct InstanceData
{
    float normTime;
    int animIndex;
};

struct MatrixStruct
{
    mat4 v;
}
;
layout(std430, binding = 0) readonly buffer SSBO_AnimPoses
{
    // half4 quatRotion, half4 Positon
    BoneWord sAnimPoses[];
};

layout(std430, binding = 1) readonly buffer SSBO_InverseBindMatrices
{
    MatrixStruct sInverseBindMatrices[];
};

layout(std430, binding = 2) buffer SSBO_BoneMatricesOut
{
    // half3x4 matrix
    BoneWord sBoneMatricesOut[];
};

// per animation
layout(std430, binding = 3) readonly buffer SSBO_AnimData
{
    AnimationData sAnimData[];
};

layout(std430, binding = 4) readonly buffer SSBO_InsttanceData
{
    InstanceData sInstanceData[];
};

layout(binding = 0) uniform cs_params {
    int numBones;
    int numInstances;
};

mat2x4 UnpackPose(int index)
{
    index = index * 4;
    mat2x4 res;

    uvec2 packed1 = uvec2(sAnimPoses[index + 0].v, sAnimPoses[index + 1].v);
    vec2 xy1 = unpackSnorm2x16(packed1.x);
    vec2 zw1 = unpackSnorm2x16(packed1.y);
    res[0] = vec4(xy1, zw1);

    uvec2 packed = uvec2(sAnimPoses[index + 2].v, sAnimPoses[index + 3].v);
    vec2 xy = unpackSnorm2x16(packed.x);
    vec2 zw = unpackSnorm2x16(packed.y);
    res[1] = normalize(vec4(xy.x, xy.y, zw.x, zw.y));
    return res;
}

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;

void main()
{
    uint instanceIndex = gl_GlobalInvocationID.x / (numInstances * numBones);
    uint boneIndex = gl_GlobalInvocationID.x % numBones;

    InstanceData  instanceData  = sInstanceData[instanceIndex];
    AnimationData animationData = sAnimData[instanceData.animIndex];
    
    int begin = int(floor(instanceData.normTime * animationData.numFrames));
    int end = (begin + 1) % animationData.numFrames;

    mat2x4 a = UnpackPose(animationData.poseStart + begin);
    mat2x4 b = UnpackPose(animationData.poseStart + end);

    float t = fract(instanceData.normTime * animationData.numFrames);
    a[0] = mix(a[0], b[0], t);
    a[1] = mix(a[1], b[1], t);

    mat4 localMat = MatrixFromQuaternion(a[1]);
    localMat[3] = vec4(a[0].xyz, 1.0);
    
    mat4 invBind = sInverseBindMatrices[boneIndex].v;
    mat4 skinMat = transpose(invBind * localMat);

    const uint matrixSize = 6;
    uint base = instanceIndex * 128 * matrixSize + (boneIndex * matrixSize);
    
    // row 0 (xy, zw)
    sBoneMatricesOut[base + 0u].v = packHalf2x16(skinMat[0].xy);
    sBoneMatricesOut[base + 1u].v = packHalf2x16(skinMat[0].zw);

    // row 1
    sBoneMatricesOut[base + 2u].v = packHalf2x16(skinMat[1].xy);
    sBoneMatricesOut[base + 3u].v = packHalf2x16(skinMat[1].zw);

    // row 2
    sBoneMatricesOut[base + 4u].v = packHalf2x16(skinMat[2].xy);
    sBoneMatricesOut[base + 5u].v = packHalf2x16(skinMat[2].zw);
}

@end
@program compute cs

@vs vs
@include_block common

layout(binding = 0) uniform vs_params {
    highp mat4 uLightMatrix;
    highp mat4 uViewProj;
};

#define MaxBonePoses   128

struct PosWord {
    vec4 v;
};

layout(std430, binding = 0) readonly buffer SSBO_Bones {
    BoneWord sBoneMtx[];
};

layout(std430, binding = 1) readonly buffer SSBO_Positions {
    PosWord sPositions[];
};

layout(std430, binding = 2) readonly buffer SSBO_Rotations {
    BoneWord sRotationPacked[];
};

layout(location = 0) in highp   vec3  aPos;
layout(location = 1) in lowp    vec3  aNormal;
layout(location = 2) in lowp    vec4  aTangent;
layout(location = 3) in mediump vec2  aTexCoords;
layout(location = 4) in lowp    uvec4 aJoints; // lowp int ranges between 0-255 
layout(location = 5) in lowp    vec4  aWeights;

@sampler_type smp nonfiltering
layout(binding = 1) uniform sampler smp;

// out highp   vec4 vLightSpaceFrag;
out mediump vec2 vTexCoords;
out lowp    mat3 vTBN;
out         vec4 vColor;


vec4 UnpackRotation()
{
    int index = gl_InstanceIndex * 2;
    uvec2 packed = uvec2(sRotationPacked[index].v, sRotationPacked[index + 1].v);
    vec2 xy = unpackSnorm2x16(packed.x);
    vec2 zw = unpackSnorm2x16(packed.y);
    return normalize(vec4(xy.x, xy.y, zw.x, zw.y));
}

void main()
{
    mediump mat3x4 animMat = mat3x4(0.0);
                                                                     
    const uint MatrixNumInt32 = 6;
    uint boneStart = uint(gl_InstanceIndex * MaxBonePoses * MatrixNumInt32);
    
    for (int i = 0; i < 4; i++)
    {
        uint matIdx = uint(aJoints[i]) * MatrixNumInt32 + boneStart;
        animMat[0] += vec4(unpackHalf2x16(sBoneMtx[matIdx + 0].v), unpackHalf2x16(sBoneMtx[matIdx + 1].v)) * aWeights[i];
        animMat[1] += vec4(unpackHalf2x16(sBoneMtx[matIdx + 2].v), unpackHalf2x16(sBoneMtx[matIdx + 3].v)) * aWeights[i];
        animMat[2] += vec4(unpackHalf2x16(sBoneMtx[matIdx + 4].v), unpackHalf2x16(sBoneMtx[matIdx + 5].v)) * aWeights[i];
    }

    vec3 instancePos = sPositions[gl_InstanceIndex].v.xyz;
    vec3 worldPos = transpose(animMat) * vec4(aPos, 1.0);
    worldPos  = QuaternionRotateVector(UnpackRotation(), worldPos);
    worldPos *= 0.1610; // todo per instance scale
    worldPos += instancePos;
    vTexCoords  = aTexCoords; 
    gl_Position = uViewProj * vec4(worldPos, 1.0);
}

    
@end


@fs fs

layout(binding = 0) uniform lowp texture2D tex;
layout(binding = 0) uniform sampler texSampler;

in mediump vec2 vTexCoords;
in lowp mat3 vTBN;
in      vec4 vColor;

out lowp vec4 frag_color;

void main() {

    frag_color = texture(sampler2D(tex, texSampler), vTexCoords); //  * color;
    frag_color.y += length(vColor.yzw);
}

@end
@program cube vs fs