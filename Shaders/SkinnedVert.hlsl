// #define half min16float
// #define half2 min16float2
// #define half3 min16float3
// #define half4 min16float4
// #define half3x3 min16float3x3
// #define half3x4 min16float3x4
// #define half4x3 min16float4x3
#pragma dxc enable_16bit_types

cbuffer vs_params : register(b0, space1)
{
    float4x4 uViewProj;
};

StructuredBuffer<uint>   sBoneMtx          : register(t0);
StructuredBuffer<float4> sInstancePosition : register(t1);
StructuredBuffer<uint>   sInstanceRotation : register(t2);

static const uint MaxBonePoses = 128;
static const uint MatrixNumInt32 = 4;

struct VSInput
{
    half4 aPos        : POSITION0;
    uint  aQTangentXY : TANGENT0;
    uint  aQTangentZW : TANGENT1;
    half2 aTexCoords  : TEXCOORD0;
    uint4 aJoints     : BLENDINDICES0;
    uint  aWeights    : BLENDWEIGHT0;
};

struct VSOutput
{
    float4 position  : SV_Position;
    half2  texCoords : TEXCOORD0;
    half3  normal    : NORMAL;
};

half4 QuaternionMul(half4 Q1, half4 Q2) {
    return half4((Q2.w * Q1.x) + (Q2.x * Q1.w) + (Q2.y * Q1.z) - (Q2.z * Q1.y),
                 (Q2.w * Q1.y) - (Q2.x * Q1.z) + (Q2.y * Q1.w) + (Q2.z * Q1.x),
                 (Q2.w * Q1.z) + (Q2.x * Q1.y) - (Q2.y * Q1.x) + (Q2.z * Q1.w),
                 (Q2.w * Q1.w) - (Q2.x * Q1.x) - (Q2.y * Q1.y) - (Q2.z * Q1.z));
}

half3 QuaternionRotateVector(half4 quat, half3 vec) {
    return vec + (cross(quat.xyz, cross(quat.xyz, vec) + (vec * quat.w)) * 2.0);
}

half3x3 Matrix3FromQuaternion(half4 q) 
{
    half3x3 mat;
    const half num9 = q.x * q.x, num8 = q.y * q.y,
    num7 = q.z * q.z, num6 = q.x * q.y,
    num5 = q.z * q.w, num4 = q.z * q.x,
    num3 = q.y * q.w, num2 = q.y * q.z,
    num  = q.x * q.w;

    mat[0][0] = 1.0 - (2.0 * (num8 + num7));
    mat[0][1] = 2.0 * (num6 + num5);
    mat[0][2] = 2.0 * (num4 - num3);
    mat[1][0] = 2.0 * (num6 - num5);
    mat[1][1] = 1.0 - (2.0 * (num7 + num9));
    mat[1][2] = 2.0 * (num2 + num);
    mat[2][0] = 2.0 * (num4 + num3);
    mat[2][1] = 2.0 * (num2 - num);
    mat[2][2] = 1.0 - (2.0 * (num8 + num9));
    return mat;
}

half2 unpackHalf2x16(uint packed) {
    return asfloat16(uint16_t2(uint16_t(packed), uint16_t(packed >> 16)));
}

half4 UnpackRGBA16Snorm(uint xy, uint zw) {
    int16_t4 raw;
    raw.x = int16_t(int(xy << 16u) >> 16);
    raw.y = int16_t(int(xy) >> 16);
    raw.z = int16_t(int(zw << 16u) >> 16);
    raw.w = int16_t(int(zw) >> 16);
    return half4(half4(raw) / half4(32767.0, 32767.0, 32767.0, 32767.0));
}

half3 UnpackVec3XY11Z10Snorm(uint packed) {
    int16_t sx = int16_t((int)( packed << 21) >> 21); // 32 - 11
    int16_t sy = int16_t((int)((packed >> 11) << 21) >> 21);
    int16_t sz = int16_t((int)((packed >> 22) << 22) >> 22); // 32 - 10
    return half3(sx * (1.0 / 1023.0), sy * (1.0 / 1023.0), sz * (1.0 / 511.0));
}

half3 UnpackVec3XY11Z10Unorm(uint packed) {
    uint16_t ux = uint16_t( packed        & 0x7FFu);
    uint16_t uy = uint16_t((packed >> 11) & 0x7FFu);
    uint16_t uz = uint16_t((packed >> 22)         );
    return half3(ux / 2047.0, uy / 2047.0, uz / 1023.0);
}

half4 GetInstanceRotation(uint instanceID) {
    return normalize(UnpackRGBA16Snorm(sInstanceRotation[instanceID], sInstanceRotation[instanceID+1]));
}

half2x4 GetBoneDualQuat(uint matIdx)
{
    return half2x4(
        half4(unpackHalf2x16(sBoneMtx[matIdx + 0]), unpackHalf2x16(sBoneMtx[matIdx + 1])),
        half4(unpackHalf2x16(sBoneMtx[matIdx + 2]), unpackHalf2x16(sBoneMtx[matIdx + 3]))
    );
}

half3 DQGetPos(half2x4 dq)
{
    return 2.0 * (dq[0].w * dq[1].xyz - dq[1].w * dq[0].xyz + cross(dq[1].xyz, dq[0].xyz));
}

half2x4 DQFromRotationTranslation(half4 rotation, half3 pos)
{
    return half2x4(rotation, QuaternionMul(rotation, half4(pos, 0.0)) * 0.5);
}

half3 DQTransformPos(half2x4 dq, half3 pos)
{
    half3 position = QuaternionRotateVector(dq[0], pos); 
    return DQGetPos(dq) + position;
}

// https://users.cs.utah.edu/~ladislav/dq/dqs.cg
half3 DQBlend4(uint4 bone_indices, half4 bone_weights, half3 pos, uint boneStart)
{
    // Fetch bones
    half2x4 dq0 = GetBoneDualQuat(boneStart + (bone_indices.x << 2));
    half2x4 dq1 = GetBoneDualQuat(boneStart + (bone_indices.y << 2));
    half2x4 dq2 = GetBoneDualQuat(boneStart + (bone_indices.z << 2));
    half2x4 dq3 = GetBoneDualQuat(boneStart + (bone_indices.w << 2));
    
    // Ensure all bone transforms are in the same neighbourhood
    bone_weights.y *= dot(dq0[0], dq1[0]) < 0 ? (half)-1 : (half)1;
    bone_weights.z *= dot(dq0[0], dq2[0]) < 0 ? (half)-1 : (half)1;
    bone_weights.w *= dot(dq0[0], dq3[0]) < 0 ? (half)-1 : (half)1;

    half2x4 blendDQ  = bone_weights.x * dq0;
    blendDQ         += bone_weights.y * dq1;
    blendDQ         += bone_weights.z * dq2;
    blendDQ         += bone_weights.w * dq3;
		
    blendDQ /= length(blendDQ[0]);
    blendDQ[1] -= blendDQ[0] * dot(blendDQ[0], blendDQ[1]);
    return DQTransformPos(blendDQ, pos);
}

VSOutput main(VSInput input, uint instanceID : SV_InstanceID)
{
    VSOutput o;
    half3x4 animMat = half3x4(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
    uint boneStart  = instanceID * MaxBonePoses * MatrixNumInt32;
    
    half4 weights;
    weights.xyz = UnpackVec3XY11Z10Unorm(input.aWeights);
    weights.w = max(1.0 - (weights.x + weights.y + weights.z), 0.0);
    
    float3 instancePos = sInstancePosition[instanceID].xyz;
    half3  worldPos = DQBlend4(input.aJoints, weights, input.aPos.xyz, boneStart);
    half4  instanceRotation = GetInstanceRotation(instanceID << 1);
    worldPos = QuaternionRotateVector(instanceRotation, worldPos);
    
    half3x3 tbn; // = Matrix3FromQuaternion(qtangent);
    tbn[2] = UnpackVec3XY11Z10Snorm(input.aQTangentXY);
    // tbn[1] = UnpackVec3XY11Z10Snorm(input.aQTangentZW);
    // tbn[0] = cross(tbn[2], tbn[1]);

    half3x3 instanceRotMat = Matrix3FromQuaternion(instanceRotation);
    tbn = mul(tbn, instanceRotMat);
    o.texCoords = input.aTexCoords;
    o.position  = mul(uViewProj, float4(float3(worldPos) + instancePos, 1.0));
    o.normal    = normalize(tbn[2]); // Normalize after transformations
    return o;
}
