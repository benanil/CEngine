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
#define HALF_EPS       (fp16)0.00048828

fp16 ACos(fp16 x) {
    // Lagarde 2014, "Inverse trigonometric functions GPU optimization for AMD GCN architecture"
    // This is the approximation of degree 1, with a max absolute error of 9.0x10^-3
    fp16 y = abs(x);
    fp16 p = -0.1565827 * y + 1.570796;
    p *= sqrt(1.0 - y);
    return x >= 0.0f ? p : MATH_PI - p;
}

fp16 ACosPositive(fp16 x) {
    fp16 p = -0.1565827 * x + 1.570796;
    return p * sqrt(1.0 - x);
}

// input [-1, 1] and output [-PI/2, PI/2]
fp16 ASin(fp16 x) {
    return MATH_HalfPI - ACos(x);
}

fp16_3 SafeNormalize(fp16_3 inVec) {
    half dp3 = max(HALF_EPS, dot(inVec, inVec));
    return inVec * rsqrt(dp3);
}

fp16_3 Orthonormalize(fp16_3 tangent, fp16_3 normal) {
    return (tangent - dot(tangent, normal) * normal);
}

fp16_4 QuaternionMul(fp16_4 Q1, fp16_4 Q2) {
    return fp16_4((Q2.w * Q1.x) + (Q2.x * Q1.w) + (Q2.y * Q1.z) - (Q2.z * Q1.y),
                  (Q2.w * Q1.y) - (Q2.x * Q1.z) + (Q2.y * Q1.w) + (Q2.z * Q1.x),
                  (Q2.w * Q1.z) + (Q2.x * Q1.y) - (Q2.y * Q1.x) + (Q2.z * Q1.w),
                  (Q2.w * Q1.w) - (Q2.x * Q1.x) - (Q2.y * Q1.y) - (Q2.z * Q1.z));
}

fp16_3 QuaternionRotateVector(fp16_4 quat, fp16_3 vec) {
    return vec + (cross(quat.xyz, cross(quat.xyz, vec) + (vec * quat.w)) * 2.0);
}

fp16_3x3 Matrix3FromQuaternion(fp16_4 q) 
{
    fp16_3x3 mat;
    const fp16 num9 = q.x * q.x, num8 = q.y * q.y,
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
#endif // MATH_HLSL