@ctype mat4 Matrix4

@block common

struct BoneWord {
    uint v;
};

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

@end

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
out vec2 vTexCoords;
out mat3 vTBN;
out vec4 vColor;


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

layout(binding = 0) uniform texture2D tex;
layout(binding = 0) uniform sampler texSampler;

in vec2 vTexCoords;
in mat3 vTBN;
in vec4 vColor;

out vec4 frag_color;

void main() {

    frag_color = texture(sampler2D(tex, texSampler), vTexCoords); //  * color;
    frag_color.y += length(vColor.yzw);
}

@end
@program cube vs fs