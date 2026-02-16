#ifndef Quaternion_H
#define Quaternion_H

#include "Vector.h"

typedef struct xyzw_ { 
    float x, y, z, w; 
} xyzw;

typedef Vec4x32f Quaternion;

#define QIdentity()  VecSetR(0.0f, 0.0f, 0.0f, 1.0f)
#define QNorm(q)     VecNorm(q)
#define QNormEst(q)  VecNormEst(q)
#define MakeQuat(_x, _y, _z, _w)  VecSetR(_x, _y, _z, _w)

static inline Vec4x32f VECTORCALL QMul(Vec4x32f Q1, Vec4x32f Q2) 
{
    const Vec4x32f ControlWZYX = { 1.0f,-1.0f, 1.0f,-1.0f };
    const Vec4x32f ControlZWXY = { 1.0f, 1.0f,-1.0f,-1.0f };
    const Vec4x32f ControlYXWZ = { -1.0f, 1.0f, 1.0f,-1.0f };
    
    Vec4x32f vResult = VecSplatW(Q2);
    Vec4x32f Q2X     = VecSplatX(Q2);
    Vec4x32f Q2Y     = VecSplatY(Q2);
    Vec4x32f Q2Z     = VecSplatZ(Q2);
    vResult = VecMul(vResult, Q1);

    Vec4x32f Q1Shuffle = Q1;
    Q1Shuffle = VecRev(Q1Shuffle);
    Q2X       = VecMul(Q2X, Q1Shuffle);
    Q1Shuffle = VecShuffleR(Q1Shuffle, Q1Shuffle, 2, 3, 0, 1);
    Q2X       = VecMul(Q2X, ControlWZYX);
    Q2Y       = VecMul(Q2Y, Q1Shuffle);
    Q1Shuffle = VecRev(Q1Shuffle);
    Q2Y       = VecMul(Q2Y, ControlZWXY);
    Q2Z       = VecMul(Q2Z, Q1Shuffle);
    vResult   = VecAdd(vResult, Q2X);
    Q2Z       = VecMul(Q2Z, ControlYXWZ);
    Q2Y       = VecAdd(Q2Y, Q2Z);
    vResult   = VecAdd(vResult, Q2Y);
    return vResult;
}

// Angle should be between -twopi, twopi
static inline Vec4x32f QFromAxisAngle(float3 axis, float angle)
{
    float SinV = Sin(0.5f * angle);
    float CosV = Cos(0.5f * angle);
    return QNorm(VecSetR(axis.x * SinV, axis.y * SinV, axis.z * SinV, CosV));
}

// below 3 function are same as QFromAxisAngle but with single axis, 
// faster because no normalization and less multipication
static inline Vec4x32f QFromXAngle(float angle) {
    return VecSetR(Sin(0.5f * angle), 0.0f, 0.0f, Cos(0.5f * angle));
}

static inline Vec4x32f QFromYAngle(float angle) {
    return VecSetR(0.0f, Sin(0.5f * angle), 0.0f, Cos(0.5f * angle));
}

static inline Vec4x32f QFromZAngle(float angle) {
    return VecSetR(0.0f, 0.0f, Sin(0.5f * angle), Cos(0.5f * angle));
}

static inline Vec4x32f VECTORCALL QMulVec3V(Vec4x32f vec, Vec4x32f quat)
{
    Vec4x32f temp0 = Vec3Cross(quat, vec);
    Vec4x32f temp1 = VecMul(vec, VecSplatW(quat));
    temp0 = VecAdd(temp0, temp1);
    temp1 = VecMul(Vec3Cross(quat, temp0), VecSet1(2.0f));
    return VecAdd(vec, temp1);
}

static inline float3 VECTORCALL QMulVec3(float3 vec, Quaternion quat)
{
    float3 res;
    Vec3Store(&res.x, QMulVec3V(VecSetR(vec.x, vec.y, vec.z, 1.0f), quat));
    return res;
}

// Common code for computing the scalar coefficients of SLERP
purefn Vec4x32f QCalculateCoefficient(Vec4x32f vT, Vec4x32f xm1)
{
    const float mu = 1.85298109240830f;
    Vec4x32f one = VecSet1(1.0f);
    // Precomputed constants
    const Vec4x32f u0123 = VecSetR( 1.f / ( 1 * 3 ), 1.f / ( 2 * 5 ), 1.f / ( 3 * 7 ), 1.f / ( 4 * 9 ) );
    const Vec4x32f u4567 = VecSetR( 1.f / ( 5 * 11 ), 1.f / ( 6 * 13 ), 1.f / ( 7 * 15 ), mu / ( 8 * 17 ) );
    const Vec4x32f v0123 = VecSetR( 1.f / 3, 2.f / 5, 3.f / 7, 4.f / 9 );
    const Vec4x32f v4567 = VecSetR( 5.f / 11, 6.f / 13, 7.f / 15, mu * 8 / 17 );

    Vec4x32f vTSquared = VecMul(vT, vT);
    Vec4x32f b4567 = VecFmsub(u4567, vTSquared, v4567);
    b4567 = VecMul(b4567, xm1);

    Vec4x32f c = VecAdd(VecSplatW(b4567), one);
    c = VecFmaddLane(c, b4567, one, 2); // multiply by lane is faster with ARM cpu's
    c = VecFmaddLane(c, b4567, one, 1);
    c = VecFmaddLane(c, b4567, one, 0);

    Vec4x32f b0123 = VecFmsub(u0123, vTSquared, v0123);
    b0123 = VecMul(b0123, xm1);
    c = VecFmaddLane(c, b0123, one, 3);
    c = VecFmaddLane(c, b0123, one, 2);
    c = VecFmaddLane(c, b0123, one, 1);
    c = VecFmaddLane(c, b0123, one, 0);
    c = VecMul(c, vT);
    return c;
}

static inline Quaternion VECTORCALL QSlerp(Quaternion q0, Quaternion q1, float t)
{
    const Vec4x32f one = VecSet1(1.0f);
    // from paper: "A Fast and Accurate Estimate for SLERP" by David Eberly
    // but I have used fused instructions and I've made optimizations on sign part for ARM cpu's
    Vec4x32f x = VecDot(q0, q1); // cos ( theta ) in all components
    Vec4x32f control = VecCmpLt(x, VecZero());
    Vec4x32f sign = VecSelect(VecOne(), VecNegativeOne(), control);
    q1 = VecMul(sign, q1); // do mul instead of xor

    Vec4x32f xm1 = VecFmsub(x, sign, one);
    Vec4x32f cT = QCalculateCoefficient(VecSet1(t), xm1);
    Vec4x32f cD = QCalculateCoefficient(VecSet1(1.0f - t), xm1);
    return VecFmadd(cD, q0, VecMul(cT, q1));
}

// faster but less precise, more error prone version of slerp
purefn Quaternion VECTORCALL QNLerp(Quaternion a, Quaternion b, float t)
{
    Vec4x32i lz = VecCmpLt(VecDot(a, b), VecZero());
    a = VecSelect(a, VecNeg(a), lz);
    a = VecLerp(a, b, t);
    return VecNormEst(a);
}

purefn Quaternion QFromEuler(float x, float y, float z)
{
    x *= 0.5f; y *= 0.5f; z *= 0.5f;
    float c[4], s[4];
    Vec4x32f cv;
    Vec4x32f sv;
    VecSinCos(VecSetR(x, y, z, 1.0f), &sv, &cv);
    VecStore(c, cv);
    VecStore(s, sv);
    
    Quaternion q = VecSetR(
        s[0] * c[1] * c[2] - c[0] * s[1] * s[2],
        c[0] * s[1] * c[2] + s[0] * c[1] * s[2],
        c[0] * c[1] * s[2] - s[0] * s[1] * c[2],
        c[0] * c[1] * c[2] + s[0] * s[1] * s[2]);
    return q;
}

purefn Quaternion VECTORCALL QFromEulerVec3(float3 euler)
{
    return QFromEuler(euler.x, euler.y, euler.z);
}

purefn float3 QToEulerAngles(Quaternion qu)
{
    xyzw q;
    VecStore(&q.x, qu);
    float3 eulerAngles; // using cstd for trigonometric functions recommended
    eulerAngles.x = ATan2(2.0f * (q.y * q.z + q.w * q.x), q.w * q.w - q.x * q.x - q.y * q.y + q.z * q.z);
    eulerAngles.y = ASin(MCLAMP(-2.0f * (q.x * q.z - q.w * q.y), -1.0f, 1.0f));
    eulerAngles.z = ATan2(2.0f * (q.x * q.y + q.w * q.z), q.w * q.w + q.x * q.x - q.y * q.y - q.z * q.z);
    return eulerAngles;
}

// number of columns of matrix, 3 or 4
static inline void QuaternionFromMatrix(float* Orientation, const float* m, int numCol) {
    int i, j, k = 0;
    float root, trace = m[0*numCol+0] + m[1 * numCol + 1] + m[2 * numCol + 2];
    
    if (trace > 0.0f)
    {
        root = Sqrtf(trace + 1.0f);
        Orientation[3] = 0.5f * root;
        root = 0.5f / root;
        Orientation[0] = root * (m[1 * numCol + 2] - m[2 * numCol + 1]);
        Orientation[1] = root * (m[2 * numCol + 0] - m[0 * numCol + 2]);
        Orientation[2] = root * (m[0 * numCol + 1] - m[1 * numCol + 0]);
    }
    else
    {
        static const int Next[3] = { 1, 2, 0 };
        i = 0;
        i += m[1 * numCol + 1] > m[0 * numCol + 0]; // if (M.m[1][1] > M.m[0][0]) i = 1
        if (m[2 * numCol + 2] > m[i * numCol + i]) i = 2;
        j = Next[i];
        k = Next[j];
        
        root = Sqrtf(m[i * numCol + i] - m[j * numCol + j] - m[k * numCol + k] + 1.0f);
        
        Orientation[i] = 0.5f * root;
        root = 0.5f / root;
        Orientation[j] = root * (m[i * numCol + j] + m[j * numCol + i]);
        Orientation[k] = root * (m[i * numCol + k] + m[k * numCol + i]);
        Orientation[3] = root * (m[j * numCol + k] - m[k*numCol+j]);
    } 
}


inline Vec4x32f VECTORCALL QuaternionFromMatrix3Vec(Vec4x32f r0, Vec4x32f r1, Vec4x32f r2)
{
    #if defined(AX_ARM)
    static const Vec4x32f XMPMMP = { +1.0f, -1.0f, -1.0f, +1.0f };
    static const Vec4x32f XMMPMP = { -1.0f, +1.0f, -1.0f, +1.0f };
    static const Vec4x32f XMMMPP = { -1.0f, -1.0f, +1.0f, +1.0f };
    static const Vec4x32u Select0110 = { 0u, ~0u, ~0u, 0u } ;
    static const Vec4x32u Select0010 = { 0u,  0u, ~0u, 0u } ;

    float32x4_t r00 = vdupq_lane_f32(vget_low_f32(r0), 0);
    float32x4_t r11 = vdupq_lane_f32(vget_low_f32(r1), 1);
    float32x4_t r22 = vdupq_lane_f32(vget_high_f32(r2), 0);

    // x^2 >= y^2 equivalent to r11 - r00 <= 0
    float32x4_t r11mr00 = vsubq_f32(r11, r00);
    uint32x4_t x2gey2 = vcleq_f32(r11mr00, VecZero());

    // z^2 >= w^2 equivalent to r11 + r00 <= 0
    float32x4_t r11pr00 = vaddq_f32(r11, r00);
    uint32x4_t z2gew2 = vcleq_f32(r11pr00, VecZero());

    // x^2 + y^2 >= z^2 + w^2 equivalent to r22 <= 0
    uint32x4_t x2py2gez2pw2 = vcleq_f32(r22, VecZero());

    // (4*x^2, 4*y^2, 4*z^2, 4*w^2)
    float32x4_t t0 = vmulq_f32(XMPMMP, r00);
    float32x4_t x2y2z2w2 = vmlaq_f32(t0, XMMPMP, r11);
    x2y2z2w2 = vmlaq_f32(x2y2z2w2, XMMMPP, r22);
    x2y2z2w2 = vaddq_f32(x2y2z2w2, VecOne());

    // (r01, r02, r12, r11)
    t0 = vextq_f32(r0, r0, 1);
    float32x4_t t1 = vextq_f32(r1, r1, 1);
    t0 = vcombine_f32(vget_low_f32(t0), vrev64_f32(vget_low_f32(t1)));

    // (r10, r20, r21, r10)
    t1 = vextq_f32(r2, r2, 3);
    float32x4_t r10 = vdupq_lane_f32(vget_low_f32(r1), 0);
    t1 = vbslq_f32(Select0110, t1, r10);

    // (4*x*y, 4*x*z, 4*y*z, unused)
    float32x4_t xyxzyz = vaddq_f32(t0, t1);

    // (r21, r20, r10, r10)
    t0 = vcombine_f32(vrev64_f32(vget_low_f32(r2)), vget_low_f32(r10));

    // (r12, r02, r01, r12)
    float32x4_t t2 = vcombine_f32(vrev64_f32(vget_high_f32(r0)), vrev64_f32(vget_low_f32(r0)));
    float32x4_t t3 = vdupq_lane_f32(vget_high_f32(r1), 0);
    t1 = vbslq_f32(Select0110, t2, t3);

    // (4*x*w, 4*y*w, 4*z*w, unused)
    float32x4_t xwywzw = vsubq_f32(t0, t1);
    xwywzw = vmulq_f32(XMMPMP, xwywzw);

    // (4*x*x, 4*x*y, 4*x*z, 4*x*w)
    t0 = vextq_f32(xyxzyz, xyxzyz, 3);
    t1 = vbslq_f32(Select0110, t0, x2y2z2w2);
    t2 = vdupq_lane_f32(vget_low_f32(xwywzw), 0);
    float32x4_t tensor0 = vbslq_f32(g_XMSelect1110, t1, t2);

    // (4*y*x, 4*y*y, 4*y*z, 4*y*w)
    t0 = vbslq_f32(g_XMSelect1011, xyxzyz, x2y2z2w2);
    t1 = vdupq_lane_f32(vget_low_f32(xwywzw), 1);
    float32x4_t tensor1 = vbslq_f32(g_XMSelect1110, t0, t1);

    // (4*z*x, 4*z*y, 4*z*z, 4*z*w)
    t0 = vextq_f32(xyxzyz, xyxzyz, 1);
    t1 = vcombine_f32(vget_low_f32(t0), vrev64_f32(vget_high_f32(xwywzw)));
    float32x4_t tensor2 = vbslq_f32(Select0010, x2y2z2w2, t1);

    // (4*w*x, 4*w*y, 4*w*z, 4*w*w)
    float32x4_t tensor3 = vbslq_f32(g_XMSelect1110, xwywzw, x2y2z2w2);

    // Select the row of the tensor-product matrix that has the largest
    // magnitude.
    t0 = vbslq_f32(x2gey2, tensor0, tensor1);
    t1 = vbslq_f32(z2gew2, tensor2, tensor3);
    t2 = vbslq_f32(x2py2gez2pw2, t0, t1);

    // Normalize the row.  No division by zero is possible because the
    // quaternion is unit-length (and the row is a nonzero multiple of
    // the quaternion).
    t0 = VecLen(t2);
    return VecDiv(t2, t0);
    #elif defined(AX_SUPPORT_SSE)
    static const Vec4x32f XMPMMP = { +1.0f, -1.0f, -1.0f, +1.0f };
    static const Vec4x32f XMMPMP = { -1.0f, +1.0f, -1.0f, +1.0f };
    static const Vec4x32f XMMMPP = { -1.0f, -1.0f, +1.0f, +1.0f };

    Vec4x32f r00 = VecSplatX(r0);
    Vec4x32f r11 = VecSplatY(r1);
    Vec4x32f r22 = VecSplatZ(r2);
    Vec4x32f r11mr00 = _mm_sub_ps(r11, r00);
    Vec4x32f x2gey2  = _mm_cmple_ps(r11mr00, VecZero());
    Vec4x32f r11pr00 = _mm_add_ps(r11, r00);
    Vec4x32f z2gew2  = _mm_cmple_ps(r11pr00, VecZero());
    Vec4x32f x2py2gez2pw2 = _mm_cmple_ps(r22, VecZero());

    Vec4x32f t0 = VecFmadd(XMPMMP, r00, VecOne());
    Vec4x32f t1 = _mm_mul_ps(XMMPMP, r11);
    Vec4x32f t2 = VecFmadd(XMMMPP, r22, t0);
    Vec4x32f x2y2z2w2 = _mm_add_ps(t1, t2);
    
    t0 = _mm_shuffle_ps(r0, r1, _MM_SHUFFLE(1, 2, 2, 1));
    t1 = _mm_shuffle_ps(r1, r2, _MM_SHUFFLE(1, 0, 0, 0));
    t1 = _mm_permute_ps(t1, _MM_SHUFFLE(1, 3, 2, 0));
    Vec4x32f xyxzyz = _mm_add_ps(t0, t1);
    
    t0 = _mm_shuffle_ps(r2, r1, _MM_SHUFFLE(0, 0, 0, 1));
    t1 = _mm_shuffle_ps(r1, r0, _MM_SHUFFLE(1, 2, 2, 2));
    t1 = _mm_permute_ps(t1, _MM_SHUFFLE(1, 3, 2, 0));
    Vec4x32f xwywzw = _mm_sub_ps(t0, t1);
    xwywzw = _mm_mul_ps(XMMPMP, xwywzw);

    t0 = _mm_shuffle_ps(x2y2z2w2, xyxzyz, _MM_SHUFFLE(0, 0, 1, 0));
    t1 = _mm_shuffle_ps(x2y2z2w2, xwywzw, _MM_SHUFFLE(0, 2, 3, 2));
    t2 = _mm_shuffle_ps(xyxzyz, xwywzw, _MM_SHUFFLE(1, 0, 2, 1));
    Vec4x32f tensor0 = _mm_shuffle_ps(t0, t2, _MM_SHUFFLE(2, 0, 2, 0));
    Vec4x32f tensor1 = _mm_shuffle_ps(t0, t2, _MM_SHUFFLE(3, 1, 1, 2));
    Vec4x32f tensor2 = _mm_shuffle_ps(t2, t1, _MM_SHUFFLE(2, 0, 1, 0));
    Vec4x32f tensor3 = _mm_shuffle_ps(t2, t1, _MM_SHUFFLE(1, 2, 3, 2));
    
    t0 = _mm_and_ps(x2gey2, tensor0);
    t1 = _mm_andnot_ps(x2gey2, tensor1);
    t0 = _mm_or_ps(t0, t1);
    t1 = _mm_and_ps(z2gew2, tensor2);
    t2 = _mm_andnot_ps(z2gew2, tensor3);
    t1 = _mm_or_ps(t1, t2);
    t0 = _mm_and_ps(x2py2gez2pw2, t0);
    t1 = _mm_andnot_ps(x2py2gez2pw2, t1);
    t2 = _mm_or_ps(t0, t1);
    t0 = VecLen(t2);
    return _mm_div_ps(t2, t0);
    #else
    STATIC_ASSERT(0, "function is not defined");
    #endif
}

static inline void Matrix3FromQuaternion(float* mat, Quaternion quat)
{
    xyzw q;
    VecStore(&q.x, quat);
    const float num9 = q.x * q.x, num8 = q.y * q.y,
    num7 = q.z * q.z, num6 = q.x * q.y,
    num5 = q.z * q.w, num4 = q.z * q.x,
    num3 = q.y * q.w, num2 = q.y * q.z,
    num  = q.x * q.w;

    mat[3 * 0 + 0] = 1.0f - (2.0f * (num8 + num7));
    mat[3 * 0 + 1] = 2.0f * (num6 + num5);
    mat[3 * 0 + 2] = 2.0f * (num4 - num3);

    mat[3 * 1 + 0] = 2.0f * (num6 - num5);
    mat[3 * 1 + 1] = 1.0f - (2.0f * (num7 + num9));
    mat[3 * 1 + 2] = 2.0f * (num2 + num);

    mat[3 * 2 + 0] = 2.0f * (num4 + num3);
    mat[3 * 2 + 1] = 2.0f * (num2 - num);
    mat[3 * 2 + 2] = 1.0f - (2.0f * (num8 + num9));
}

static inline void Matrix4FromQuaternion(float* mat, Quaternion quat)
{
    xyzw q;
    VecStore(&q.x, quat);
    const float num9 = q.x * q.x, num8 = q.y * q.y,
    num7 = q.z * q.z, num6 = q.x * q.y,
    num5 = q.z * q.w, num4 = q.z * q.x,
    num3 = q.y * q.w, num2 = q.y * q.z,
    num  = q.x * q.w;

    mat[4 * 0 + 0] = 1.0f - (2.0f * (num8 + num7));
    mat[4 * 0 + 1] = 2.0f * (num6 + num5);
    mat[4 * 0 + 2] = 2.0f * (num4 - num3);
    mat[4 * 1 + 0] = 2.0f * (num6 - num5);
    mat[4 * 1 + 1] = 1.0f - (2.0f * (num7 + num9));
    mat[4 * 1 + 2] = 2.0f * (num2 + num);
    mat[4 * 2 + 0] = 2.0f * (num4 + num3);
    mat[4 * 2 + 1] = 2.0f * (num2 - num);
    mat[4 * 2 + 2] = 1.0f - (2.0f * (num8 + num9));
    mat[4 * 3 + 3] = 1.0f;
}

purefn void VECTORCALL PackQuaternionS16Norm(Vec4x32f quat, uint32_t* result)
{
    quat = VecNorm(quat);
    Vec4x32u u32 = VecCvtF32I32(VecMul(quat, VecSet1(INT16_MAX-1)));
    Vec4x32u u16 = VecNarrowU32U16(u32);
    VecStoreI16(result, u16);
}

purefn Quaternion QFromLookRotation(float3 direction, float3 up)
{
    const float3 matrix[3] = {
        F3Cross(up, direction), up, direction 
    };
    xyzw result;
    QuaternionFromMatrix(&result.x, &matrix[0].x, 3);
    return VecLoad(&result.x);
}

purefn Quaternion VECTORCALL QConjugate(Quaternion vec)
{
    return VecMul(vec, VecSetR(-1.0f, -1.0f, -1.0f, 1.0f));
}

purefn Quaternion VECTORCALL QInverse(Quaternion q)
{
    const float lengthSq = VecDotf(q, q);
    if (AlmostEqualf(lengthSq, 1.0f))
    {
        q = QConjugate(q);
        return q;
    }
    else if (lengthSq >= 0.001f)
    {
        q = VecMul(QConjugate(q), VecSet1(1.0f / lengthSq));
        return q;
    }
    else
    {
    	return QIdentity();
    }
}

static inline float3 VECTORCALL QGetForward(Quaternion vec) {
    float3 res;
    Vec3Store(&res.x, QMulVec3V(VecSetR( 0.0f, 0.0f, 1.0f, 0.0f), QConjugate(vec)));
    return res; 
}

static inline float3 VECTORCALL QGetRight(Quaternion vec) {
    float3 res;
    Vec3Store(&res.x, QMulVec3V(VecSetR( 1.0f, 0.0f, 0.0f, 0.0f), QConjugate(vec)));
    return res; 
}

static inline float3 VECTORCALL QGetLeft(Quaternion vec) {
    float3 res;
    Vec3Store(&res.x, QMulVec3V(VecSetR(-1.0f, 0.0f, 0.0f, 0.0f), QConjugate(vec)));
    return res; 
}

static inline float3 VECTORCALL QGetUp(Quaternion vec) {
    float3 res;
    Vec3Store(&res.x, QMulVec3V(VecSetR( 0.0f, 1.0f, 0.0f, 0.0f), QConjugate(vec)));
    return res; 
}


// Dual Quaternion structure
typedef struct DualQuaternion_ {
    Vec4x32f real;  // rotation quaternion
    Vec4x32f dual;  // encodes translation
} DualQuaternion;

static inline DualQuaternion DQMultiply(DualQuaternion a, DualQuaternion b)
{
    DualQuaternion result;
    result.real = QMul(a.real, b.real);
    // dual = real_a * dual_b + dual_a * real_b
    result.dual = VecAdd(QMul(a.real, b.dual), QMul(a.dual, b.real));
    return result;
}

static inline Vec4x32f DQGetTranslation(DualQuaternion dq)
{
    // t = 2 * dual * conjugate(real)
    Vec4x32f real_conj = QConjugate(dq.real);
    Vec4x32f t = QMul(VecMul(dq.dual, VecSet1(2.0f)), real_conj);
    VecSetW(t, 0.0f);
    return t;
}

static inline DualQuaternion DQFromRotationTranslation(Vec4x32f rotation, Vec4x32f translation)
{
    VecSetW(translation, 0.0f);
    DualQuaternion dq;
    dq.real = rotation;
    dq.dual = VecMulf(QMul(translation, rotation), 0.5f);
    // // enforce q·d = 0
    // float dotRD = VecDotf(dq.real, dq.dual);
    // dq.dual = VecSub(dq.dual, VecMulf(dq.real, dotRD));
    return dq;
}

static inline DualQuaternion DQBlend(DualQuaternion x, DualQuaternion y, float a)
{
    // Check dot product to handle antipodality
    Vec4x32f k = VecDot(x.real, y.real);
    Vec4x32i le = VecCmpLe(k, VecZero());
    
    // If dot < 0, negate dq1 to take shorter path
    Vec4x32f neg_one = VecSet1(-1.0f);
    y.real = VecBlend(y.real, VecMul(y.real, neg_one), le);
    y.dual = VecBlend(y.dual, VecMul(y.dual, neg_one), le);
    
    // Linear blend
    Vec4x32f a_vec = VecSet1(a);
    Vec4x32f one_minus_a = VecSub(VecOne(), a_vec);
    
    DualQuaternion result;
    result.real = VecFmadd(x.real, one_minus_a, VecMul(y.real, k));
    result.dual = VecFmadd(x.dual, one_minus_a, VecMul(y.dual, k));
    return result;
}

#endif // Quaternion_H
