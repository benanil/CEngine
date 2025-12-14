@ctype mat4 Matrix4

@vs vs
layout(binding = 0) uniform vs_params {
    highp mat4 uLightMatrix;
    highp mat4 uViewProj;
};

#define MaxBonePoses   128
#define NUM_ANIMS      1024
#define MatrixNumInt32 6

struct BoneWord {
    uint v;
};

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

// https://www.shadertoy.com/view/3s33zj
highp mat3 adjoint(in highp mat4 m)
{
    return mat3(cross(m[1].xyz, m[2].xyz),
                cross(m[2].xyz, m[0].xyz),
                cross(m[0].xyz, m[1].xyz));
}

mat4 Matrix4FromQuaternion(vec4 q)
{
    mat4 mat = mat4(0.0);
    float num9 = q.x * q.x, num8 = q.y * q.y,
    num7 = q.z * q.z, num6 = q.x * q.y,
    num5 = q.z * q.w, num4 = q.z * q.x,
    num3 = q.y * q.w, num2 = q.y * q.z,
    num  = q.x * q.w;

    mat[0][0] = 1.0 - (2.0f * (num8 + num7));
    mat[0][1] = 2.0 * (num6 + num5);
    mat[0][2] = 2.0 * (num4 - num3);
                   
    mat[1][0] = 2.0 * (num6 - num5);
    mat[1][1] = 1.0 - (2.0f * (num7 + num9));
    mat[1][2] = 2.0 * (num2 + num);
                   
    mat[2][0] = 2.0 * (num4 + num3);
    mat[2][1] = 2.0 * (num2 - num);
    mat[2][2] = 1.0 - (2.0f * (num8 + num9));
    mat[3][3] = 1.0;
    return mat;
}

vec4 QuaternionMul(vec4 Q1, vec4 Q2)
{
    return vec4((Q2.w * Q1.x) + (Q2.x * Q1.w) + (Q2.y * Q1.z) - (Q2.z * Q1.y),
                (Q2.w * Q1.y) - (Q2.x * Q1.z) + (Q2.y * Q1.w) + (Q2.z * Q1.x),
                (Q2.w * Q1.z) + (Q2.x * Q1.y) - (Q2.y * Q1.x) + (Q2.z * Q1.w),
                (Q2.w * Q1.w) - (Q2.x * Q1.x) - (Q2.y * Q1.y) - (Q2.z * Q1.z));
}

mat4 UnpackRotation()
{
    int index = gl_InstanceIndex * 2;
    uvec2 packed = uvec2(sRotationPacked[index].v, sRotationPacked[index + 1].v);
    
    vec2 xy = unpackSnorm2x16(packed.x);
    vec2 zw = unpackSnorm2x16(packed.y);
    
    vec4 q = normalize(vec4(xy.x, xy.y, zw.x, zw.y));
    return Matrix4FromQuaternion(q);
}

void main() 
{
    highp mat4 model = mat4(0.0);
    model[0][0] = 1.0;
    model[1][1] = 1.0;
    model[2][2] = 1.0;
    model[3][3] = 1.0;

    mat4 rotation = UnpackRotation();
    vec3 position = sPositions[gl_InstanceIndex].v.xyz;

    mediump mat4 animMat = mat4(0.0);
    animMat[3].w = 1.0; // last row is [0.0, 0.0, 0.0, 1.0]
    
    uint boneStart = uint(gl_InstanceIndex * MaxBonePoses * MatrixNumInt32);
    
    for (int i = 0; i < 4; i++)
    {
        uint matIdx = uint(aJoints[i]) * 6u + boneStart;
        animMat[0] += vec4(unpackHalf2x16(sBoneMtx[matIdx + 0].v), unpackHalf2x16(sBoneMtx[matIdx + 1].v)) * aWeights[i];
        animMat[1] += vec4(unpackHalf2x16(sBoneMtx[matIdx + 2].v), unpackHalf2x16(sBoneMtx[matIdx + 3].v)) * aWeights[i];
        animMat[2] += vec4(unpackHalf2x16(sBoneMtx[matIdx + 4].v), unpackHalf2x16(sBoneMtx[matIdx + 5].v)) * aWeights[i];
    }
    model = model * transpose(animMat);

    highp vec4 outPos = model * vec4(aPos, 1.0);
    outPos *= rotation;
    outPos.xyz += position;

    vTexCoords  = aTexCoords; 
    gl_Position = uViewProj * outPos;
}

// mediump mat3 normalMatrix = adjoint(model);
// vTBN[0] = normalize(normalMatrix * aTangent.xyz); 
// vTBN[2] = normalize(normalMatrix * aNormal);
// vTBN[1] = cross(vTBN[0], vTBN[2]) * aTangent.w;
    

@end

@fs fs
layout(binding = 0) uniform lowp texture2D tex;
layout(binding = 0) uniform sampler texSampler;

in mediump vec2 vTexCoords;
in lowp mat3 vTBN;

out lowp vec4 frag_color;

void main() {
    frag_color = texture(sampler2D(tex, texSampler), vTexCoords); //  * color;
}
@end
@program cube vs fs