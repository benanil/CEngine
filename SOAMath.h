
#include "Math/Quaternion.h"

#define Do3(X) { int i = 0; X; i = 1; X; i = 2; X;}
#define Do4(X) { int i = 0; X; i = 1; X; i = 2; X; i = 3; X;}

#define Vec3MulSoa(V1, V2, res)  Do3(res[i] = VecMul(V1[i], V2[i]))
#define Vec3AddSoa(V1, V2, res)  Do3(res[i] = VecAdd(V1[i], V2[i])) 
#define Vec3SubSoa(V1, V2, res)  Do3(res[i] = VecSub(V1[i], V2[i]))
#define Vec3DivSoa(V1, V2, res)  Do3(res[i] = VecDiv(V1[i], V2[i]))

#define Vec3MulSoaf(V1, V2, res) Do3(res[i] = VecMul(V1[i], V2))
#define Vec3AddSoaf(V1, V2, res) Do3(res[i] = VecAdd(V1[i], V2))
#define Vec3SubSoaf(V1, V2, res) Do3(res[i] = VecSub(V1[i], V2))
#define Vec3DivSoaf(V1, V2, res) Do3(res[i] = VecDiv(V1[i], V2))

#define Vec4MulSoa(V1, V2, res)  Do4(res[i] = VecMul(V1[i], V2[i]))
#define Vec4AddSoa(V1, V2, res)  Do4(res[i] = VecAdd(V1[i], V2[i])) 
#define Vec4SubSoa(V1, V2, res)  Do4(res[i] = VecSub(V1[i], V2[i]))
#define Vec4DivSoa(V1, V2, res)  Do4(res[i] = VecDiv(V1[i], V2[i]))

#define Vec4MulSoaf(V1, V2, res) Do4(res[i] = VecMul(V1[i], V2))
#define Vec4AddSoaf(V1, V2, res) Do4(res[i] = VecAdd(V1[i], V2))
#define Vec4SubSoaf(V1, V2, res) Do4(res[i] = VecSub(V1[i], V2))
#define Vec4DivSoaf(V1, V2, res) Do4(res[i] = VecDiv(V1[i], V2))

#define Vec3DotSoa(V1, V2) VecFmadd(q0[0], q1[0], VecFmadd(q0[1], q1[1], VecAdd(q0[2], q1[2]))) 
#define Vec4DotSoa(V1, V2) VecFmadd(q0[0], q1[0], VecFmadd(q0[1], q1[1], VecFmadd(q0[2], q1[2], VecAdd(q0[3], q1[3])))) 

#define Vec3LenSoa(xyz) VecSqrt(Vec3DotSoa(xyz, xyz))
#define Vec4LenSoa(vec) VecSqrt(Vec4DotSoa(vec, vec))

#define Vec3LerpSoa(a, b, t, res) Do3(res[i] = VecFmadd(VecSub(b[i], a[i]), t, a[i]))
#define Vec4LerpSoa(a, b, t, res) Do4(res[i] = VecFmadd(VecSub(b[i], a[i]), t, a[i]))

static inline void VECTORCALL Vec3NormSoa(const Vector4x32f V1[3], Vector4x32f res[3])
{
    Vector4x32f len = VecSqrt(Vec3DotSoa(V1, V1));
    Vec3DivSoaf(V1, len, res);
}

static inline void VECTORCALL Vec3NormEstSoa(const Vector4x32f V1[3], Vector4x32f res[3])
{
    Vector4x32f lenRcp = VecRSqrt(Vec3DotSoa(V1, V1));
    Vec3MulSoaf(V1, lenRcp, res);
}

static inline void VECTORCALL Vec4NormEstSoa(const Vector4x32f V1[4], Vector4x32f res[4])
{
    Vector4x32f lenRcp = VecRSqrt(Vec4DotSoa(V1, V1));
    Vec4MulSoaf(V1, lenRcp, res);
}

static inline void VECTORCALL Vec3CrossSoa(const Vector4x32f a[3], const Vector4x32f b[3], Vector4x32f res[3])
{
    res[0] = VecFmsub(a[1], b[2], VecMul(b[1], a[2]));
    res[1] = VecFmsub(a[2], b[0], VecMul(b[2], a[0]));
    res[2] = VecFmsub(a[0], b[1], VecMul(b[0], a[1]));
}

static inline Vector4x32f VECTORCALL Vec3DistSoa(const Vector4x32f V1[3], const Vector4x32f V2[3], Vector4x32f res)
{
    Vector4x32f subResult[3];
    Vec3SubSoa(V1, V2, subResult);
    return Vec3LenSoa(subResult);
}

static inline void VECTORCALL QMulSoa(const Vector4x32f Q1[4], const Vector4x32f Q2[4], Vector4x32f res[4]) 
{
    // preload once
    const Vector4x32f q10 = Q1[0], q11 = Q1[1], q12 = Q1[2], q13 = Q1[3];
    const Vector4x32f q20 = Q2[0], q21 = Q2[1], q22 = Q2[2], q23 = Q2[3];
    res[0] = VecFmsub(q22, q11, VecFmadd(q21, q12, VecFmadd(q20, q13, VecMul(q23, q10))));
    res[1] = VecFmadd(q22, q10, VecFmadd(q21, q13, VecFmsub(q20, q12, VecMul(q23, q11))));
    res[2] = VecFmadd(q22, q13, VecFmsub(q21, q10, VecFmadd(q20, q11, VecMul(q23, q12))));
    res[3] = VecFmsub(q22, q12, VecFmsub(q21, q11, VecFmsub(q20, q10, VecMul(q23, q13))));
}

static inline void VECTORCALL QMulVec3Soa(const Vector4x32f vec[3], const Vector4x32f quat[4], Vector4x32f res[3])
{
    Vector4x32f temp0[3];
    Vec3CrossSoa(quat, vec, temp0);
    Vec3MulSoaf(vec, quat[2], res);
    Vec3AddSoa(temp0, res, res);
    Vec3CrossSoa(quat, temp0, res);
    Vec3MulSoaf(res, VecSet1(2.0f), temp0);
    Vec3AddSoa(vec, temp0, res);
}

static inline void VECTORCALL QSlerpSoa(const Vector4x32f q0[4], const Vector4x32f q1[4], Vector4x32f t, Vector4x32f res[4])
{
    const Vector4x32f one = VecSet1(1.0f);

    Vector4x32f x = Vec4DotSoa(q0, q1); 
    Vector4x32f control = VecCmpLt(x, VecZero());
    Vector4x32f sign = VecSelect(VecOne(), VecNegativeOne(), control);
    
    Vector4x32f q1t[4];
    Vec4MulSoaf(q1, sign, q1t);

    Vector4x32f xm1 = VecFmsub(x, sign, one);

    Vector4x32f cT = QCalculateCoefficient(t, xm1);
    Vector4x32f cD = QCalculateCoefficient(VecSub(one, t), xm1);

    Do4(res[i] = VecFmadd(cD, q0[i], VecMul(cT, q1t[i])));
}


static inline void VECTORCALL QNlerpSoa(const Vector4x32f a[4], const Vector4x32f b[4], Vector4x32f t, Vector4x32f res[4])
{
    Vector4x32f dot = Vec4DotSoa(a, b);
    Vector4x32i lz = VecCmpLt(dot, VecZero());
    VecSelect(a[0], VecNeg(a[0]), VecSplatX(lz));
    VecSelect(a[1], VecNeg(a[1]), VecSplatY(lz));
    VecSelect(a[2], VecNeg(a[2]), VecSplatZ(lz));
    VecSelect(a[3], VecNeg(a[4]), VecSplatW(lz));
    
    Vec4LerpSoa(a, b, t, res);
    Vec4NormEstSoa(res, res);
}

static inline void VECTORCALL QConjugateSoa(const Vector4x32f q[4], Vector4x32f res[4])
{
    res[0] = VecNeg(q[0]);
    res[1] = VecNeg(q[1]);
    res[2] = VecNeg(q[2]);
    res[3] = q[3];
}

static inline void VECTORCALL QInverseSoa(const Vector4x32f q[4], Vector4x32f res[4])
{
    Vector4x32f dot = Vec4DotSoa(q, q);
    Vector4x32f invDot = VecRcp(dot);
    QConjugateSoa(q, res);
    Vec4MulSoaf(res, invDot, res);
}

static inline void QFromXAngleSoa(const Vector4x32f angle, Vector4x32f resultXYZW[4]) 
{
    const Vector4x32f halfAngle = VecMulf(angle, 0.5f);
    const Vector4x32f x = VecSin(halfAngle);
    const Vector4x32f w = VecCos(halfAngle);
    resultXYZW[0] = x;
    resultXYZW[1] = VecZero();
    resultXYZW[2] = VecZero();
    resultXYZW[3] = w;
}

static inline void QFromYAngleSoa(const Vector4x32f angle, Vector4x32f resultXYZW[4]) 
{
    const Vector4x32f halfAngle = VecMulf(angle, 0.5f);
    const Vector4x32f y = VecSin(halfAngle);
    const Vector4x32f w = VecCos(halfAngle);
    resultXYZW[0] = VecZero();
    resultXYZW[1] = y;
    resultXYZW[2] = VecZero();
    resultXYZW[3] = w;
}

static inline void QFromZAngleSoa(Vector4x32f angle, Vector4x32f resultXYZW[4]) 
{
    const Vector4x32f halfAngle = VecMulf(angle, 0.5f);
    const Vector4x32f z = VecSin(halfAngle);
    const Vector4x32f w = VecCos(halfAngle);
    resultXYZW[0] = VecZero();
    resultXYZW[1] = VecZero();
    resultXYZW[2] = z;
    resultXYZW[3] = w;
}
