#ifndef MATH_HLSL
#define MATH_HLSL

#include "Common.hlsl"

#define MATH_PI        (3.14159265358)
#define MATH_HalfPI    (MATH_PI / 2.0)
#define MATH_QuarterPI (MATH_PI / 4.0)
#define MATH_RadToDeg  (180.0 / MATH_PI)
#define MATH_DegToRad  (MATH_PI / 180.0)
#define MATH_OneDivPI  (1.0 / MATH_PI)
#define MATH_TwoPI     (MATH_PI * 2.0)
#define MATH_Sqrt2     (1.414213562)
#define MATH_Epsilon   (0.0001)
#define HALF_EPS       (f16)0.00048828

f16 ACos(f16 x) {
    // Lagarde 2014, "Inverse trigonometric functions GPU optimization for AMD GCN architecture"
    // This is the approximation of degree 1, with a max absolute error of 9.0x10^-3
    f16 y = abs(x);
    f16 p = -0.1565827 * y + 1.570796;
    p *= sqrt(1.0 - y);
    return x >= 0.0f ? p : MATH_PI - p;
}

f16 ACosPositive(f16 x) {
    f16 p = -0.1565827 * x + 1.570796;
    return p * sqrt(1.0 - x);
}

// input [-1, 1] and output [-PI/2, PI/2]
f16 ASin(f16 x) {
    return MATH_HalfPI - ACos(x);
}

f16_3 SafeNormalize(f16_3 inVec) {
    half dp3 = max(HALF_EPS, dot(inVec, inVec));
    return inVec * rsqrt(dp3);
}

f16_3 Orthonormalize(f16_3 tangent, f16_3 normal) {
    return (tangent - dot(tangent, normal) * normal);
}

f16_4 QNlerp(f16_4 q, f16_4 r, f16 t)
{
    return normalize(lerp(q, ((dot(q, r) < 0.) ? -r : r), t));
}

f16_4 QMul(f16_4 Q1, f16_4 Q2) {
    return f16_4((Q2.w * Q1.x) + (Q2.x * Q1.w) + (Q2.y * Q1.z) - (Q2.z * Q1.y),
                 (Q2.w * Q1.y) - (Q2.x * Q1.z) + (Q2.y * Q1.w) + (Q2.z * Q1.x),
                 (Q2.w * Q1.z) + (Q2.x * Q1.y) - (Q2.y * Q1.x) + (Q2.z * Q1.w),
                 (Q2.w * Q1.w) - (Q2.x * Q1.x) - (Q2.y * Q1.y) - (Q2.z * Q1.z));
}

f16_3 QMulVec3(f16_4 quat, f16_3 vec) {
    return vec + (cross(quat.xyz, cross(quat.xyz, vec) + (vec * quat.w)) * 2.0);
}

float3 QMulVec3F32(float4 quat, float3 vec)
{
    return vec + cross(quat.xyz, cross(quat.xyz, vec) + vec * quat.w) * 2.0f;
}

float3x3 M33FromQuaternionF32(float4 q)
{
    float x2 = q.x + q.x;
    float y2 = q.y + q.y;
    float z2 = q.z + q.z;

    float xx = q.x * x2;
    float yy = q.y * y2;
    float zz = q.z * z2;

    float xy = q.x * y2;
    float xz = q.x * z2;
    float yz = q.y * z2;

    float wx = q.w * x2;
    float wy = q.w * y2;
    float wz = q.w * z2;

    float3x3 m;
    m[0] = float3(1.0f - yy - zz, xy + wz,        xz - wy);
    m[1] = float3(xy - wz,        1.0f - xx - zz, yz + wx);
    m[2] = float3(xz + wy,        yz - wx,        1.0f - xx - yy);
    return m;
}

f16_4 QMulVec3V(f16_4 quat, f16_4 v)
{
    f16_3 vec = v.xyz;
    return f16_4(vec + (cross(quat.xyz, cross(quat.xyz, vec) + (vec * quat.w)) * 2.0), 0.0f);
}

f16_4x4 M44FromQuaternion(f16_4 q)
{
    f16_4x4 mat = (f16_4x4)0;
    const f16 num9 = q.x * q.x, num8 = q.y * q.y,
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
    mat[3][3] = 1.0;
    return mat;
}

f16_4x4 M44PositionRotationVec(f16_4 position, f16_4 rotation)
{
    f16_4x4 res = M44FromQuaternion(rotation);
    res[3] = position;
    res[3].w = 1.0f;
    return res; 
}


#define M44Multiply(a, b) mul(a, b)

#endif // MATH_HLSL
