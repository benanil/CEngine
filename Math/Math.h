#ifndef MATH_H
#define MATH_H

// most of the functions are accurate and faster than stl 
// convinient for game programming, be aware of speed and preciseness tradeoffs because cstd has more accurate functions
// https://seblagarde.wordpress.com/2014/12/01/inverse-trigonometric-functions-gpu-optimization-for-amd-gcn-architecture/

// Sections of this file are:
// Essential    : square, sqrt, exp, pow...
// Trigonometry : sin, cos, tan, atan, atan2...
// Half         : IEEE 16bit float, conversion functions
// Color        : packing and unpacking rgba8 color
// Ease         : easeIn, easeOut...

#include "../Include/Common.h"

#define MATH_PI        (3.14159265358f)
#define MATH_HalfPI    (MATH_PI / 2.0f)
#define MATH_QuarterPI (MATH_PI / 4.0f)
#define MATH_RadToDeg  (180.0f / MATH_PI)
#define MATH_DegToRad  (MATH_PI / 180.0f)
#define MATH_OneDivPI  (1.0f / MATH_PI)
#define MATH_TwoPI     (MATH_PI * 2.0f)
#define MATH_Sqrt2     (1.414213562f)
#define MATH_Epsilon   (0.0001f)


purefn v128f VCALL Vec3Cross(v128f vec0, v128f vec1)
{
#if defined(AX_ARM)
    float32x2_t v1xy = vget_low_f32(vec0);
    float32x2_t v2xy = vget_low_f32(vec1);
    float32x2_t v1yx = vrev64_f32(v1xy);
    float32x2_t v2yx = vrev64_f32(v2xy);
    float32x2_t v1zz = vdup_lane_f32(vget_high_f32(vec0), 0);
    float32x2_t v2zz = vdup_lane_f32(vget_high_f32(vec1), 0);
    uint32x4_t FlipY = ARMCreateVecI(0x00000000u, 0x80000000u, 0x00000000u, 0x00000000u);
    v128f vResult = vmulq_f32(vcombine_f32(v1yx, v1xy), vcombine_f32(v2zz, v2yx));
    vResult = vmlsq_f32(vResult, vcombine_f32(v1zz, v1yx), vcombine_f32(v2yx, v2xy));
    vResult = vreinterpretq_f32_u32(veorq_u32(vreinterpretq_u32_f32(vResult), FlipY));
    return vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(vResult), VecMask3));
#else
    v128f tmp0 = VecShuffleR(vec0, vec0, 3,0,2,1);
    v128f tmp1 = VecShuffleR(vec1, vec1, 3,1,0,2);
    v128f tmp2 = VecMul(tmp0, vec1);
    v128f tmp3 = VecMul(tmp0, tmp1);
    v128f tmp4 = VecShuffleR(tmp2, tmp2, 3,0,2,1);
    return VecSub(tmp3, tmp4);
#endif
}

purefn f1 VCALL Min3(v128f ab)
{
    v128f xy = VecMin(VecSplatX(ab), VecSplatY(ab));
    return VecGetX(VecMin(xy, VecSplatZ(ab)));
}

purefn f1 VCALL Max3(v128f ab)
{
    v128f xy = VecMax(VecSplatX(ab), VecSplatY(ab));
    return VecGetX(VecMax(xy, VecSplatZ(ab)));
}

#define VecClamp01(v) VecClamp(v, VecZero(), VecOne())

static forceinline void VCALL Vec3Store(float* f, v128f v)
{
    f[0] = VecGetX(v);
    f[1] = VecGetY(v);
    f[2] = VecGetZ(v);
}

purefn v128f Vec3Proj(v128f v, v128f n)
{
    float num = Vec3DotfV(n, n);
    if (num < MATH_Epsilon) return v;
    float num2 = Vec3DotfV(v, n);
    return VecSub(v, VecMulf(n, num2 / num));
}

purefn v128f Vec3Reflect(v128f in, v128f normal) 
{
    return VecSub(in, VecMul(normal, VecMulf(VecDot(normal, in), 2.0f)));
}

// Valid input range -1..1 output is -pi..pi
purefn v128f ACosV(v128f x)   
{
    // Lagarde 2014, "Inverse trigonometric functions GPU optimization for AMD GCN architecture"
    // This is the approximation of degree 1, with a max absolute error of 9.0x10^-3
    v128f y = VecFabs(x);
    v128f p = VecFmadd(VecSet1(-0.1565827f), y, VecSet1(1.570796f));
    p = VecMul(p, VecSqrt(VecSub(VecOne(), y)));
    return VecSelect(p, VecSub(VecSet1(MATH_PI), p),VecCmpGe(x, VecZero()));
}

purefn f1 Vec3Angle(v128f a, v128f b) {
    v128f dot = VecMul(Vec3DotV(a, b), VecRSqrt(VecMul(Vec3DotV(a, a), Vec3DotV(b, b))));
    dot = VecClamp(dot, VecSet1(-1.0f), VecSet1(1.0f));
    return VecGetX(ACosV(dot));
}

purefn v128f VCALL VecHSum(v128f v) {
    v = VecHadd(v, v); // high half -> low half
    return VecHadd(v, v);
}

purefn v128f VCALL VecCopySign(v128f x, v128f y)
{
    v128u clearedX = VeciAnd(VecBitcastU32(x), VeciSet1(0x7fffffff));
    v128u signY    = VeciAnd(VecBitcastU32(y), VeciSet1(0x80000000));
    v128u res      = VeciOr(clearedX, signY);
    return VeciBitcastF32(res);
}

purefn v128f VCALL VecLerp(v128f x, v128f y, f1 t)
{
    return VecFmadd(VecSub(y, x), VecSet1(t), x);
}

purefn v128f VCALL VecStep(v128f edge, v128f x)
{
    return VecBlend(VecZero(), VecOne(), VecCmpGt(x, edge));
}

purefn v128f VCALL VecFract(v128f x)
{
    return VecSub(x, VecFloor(x));
}

//------------------------------------------------------------------------------

purefn v128f VCALL VecModAngles(v128f angles)
{
    v128f twoPi       = VecSet1(2.0f * MATH_PI);
    v128f recipTwoPi  = VecSet1(1.0f / (2.0f * MATH_PI));
    v128f v = VecMul(angles, recipTwoPi); // Multiply by 1/(2*pi)
    v = VecRound(v); 
    return VecSub(angles, VecMul(v, twoPi));
}

purefn v128f VCALL VecSin(const v128f V)
{
    v128f SC0 = VecSetR(-0.16666667f, +0.0083333310f, -0.00019840874f, +2.7525562e-06f);
    v128f SC1 = VecSetR(-2.3889859e-08f, -0.16665852f, +0.0083139502f, -0.00018524670f);
    v128f x       = VecModAngles(V);
    v128f signbit = VecAnd(x, VecNegZero());
    v128f c       = VecOr(VecSet1(MATH_PI), signbit);
    v128f absx    = VecAndNot(signbit, x);
    v128f rflx    = VecSub(c, x);
    v128f comp    = VecCmpLe(absx, VecSet1(MATH_HalfPI));

    x = VecBlend(rflx, x, comp);
    v128f x2 = VecMul(x, x);
    // Horner with explicit splats (same order as DXMath/SSE version)
    v128f Result = VecFmadd(VecSplatX(SC1), x2, VecSplatW(SC0));
    Result = VecFmadd(Result, x2, VecSplatZ(SC0));
    Result = VecFmadd(Result, x2, VecSplatY(SC0));
    Result = VecFmadd(Result, x2, VecSplatX(SC0));
    Result = VecFmadd(Result, x2, VecOne());
    return VecMul(Result, x);
}

purefn v128f VCALL VecCos(const v128f V)
{
    v128f CC0 = VecSetR(-0.5f, +0.041666638f, -0.0013888378f, +2.4760495e-05f);
    v128f CC1 = VecSetR(-2.6051615e-07f, -0.49992746f, +0.041493919f, -0.0012712436f);
    v128f x       = VecModAngles(V);
    v128f signbit = VecAnd(x, VecNegZero());   // sign bit only
    v128f c       = VecOr(VecSet1(MATH_PI), signbit);
    v128f absx    = VecAndNot(signbit, x);
    v128f rflx    = VecSub(c, x);
    v128f comp    = VecCmpLe(absx, VecSet1(MATH_HalfPI));

    x = VecBlend(rflx, x, comp);
    
    v128f sign = VecBlend(VecNegativeOne(), VecOne(), comp);
    v128f x2   = VecMul(x, x);
    v128f R    = VecFmadd(VecSplatX(CC1), x2, VecSplatW(CC0));
    R = VecFmadd(R, x2, VecSplatZ(CC0));
    R = VecFmadd(R, x2, VecSplatY(CC0));
    R = VecFmadd(R, x2, VecSplatX(CC0));
    R = VecFmadd(R, x2, VecOne());
    return VecMul(R, sign);
}

purefn void VCALL VecSinCos(v128f V, v128f* pSin, v128f* pCos)
{
    v128f SC0 = VecSetR( -0.16666667f   , +0.0083333310f, -0.00019840874f, +2.7525562e-06f );
    v128f SC1 = VecSetR( -2.3889859e-08f, -0.16665852f /*Est1*/, +0.0083139502f /*Est2*/, -0.00018524670f /*Est3*/ );
    v128f CC0 = VecSetR( -0.500000000f  , +0.041666638f, -0.0013888378f, +2.4760495e-05f );
    v128f CC1 = VecSetR( -2.6051615e-07f, -0.49992746f /*Est1*/, +0.041493919f /*Est2*/, -0.0012712436f /*Est3*/ );

    v128f x       = VecModAngles(V);
    v128f signbit = VecAnd(x, VecNegZero());
    v128f c       = VecOr(VecSet1(MATH_PI), signbit);
    v128f absx    = VecAndNot(signbit, x);
    v128f rflx    = VecSub(c, x);
    v128f comp    = VecCmpLe(absx, VecSet1(MATH_HalfPI));

    x = VecBlend(rflx, x, comp); // x = comp ? x : rflx
    v128f s0   = VecAnd(comp, VecOne());            // +1
    v128f s1   = VecAndNot(comp, VecNegativeOne()); // -1
    v128f sign = VecOr(s0, s1);

    v128f x2 = VecMul(x, x);
    v128f R;
    R     = VecFmadd(VecSplatX(SC1), x2, VecSplatW(SC0));
    R     = VecFmadd(R, x2, VecSplatZ(SC0));
    R     = VecFmadd(R, x2, VecSplatY(SC0));
    R     = VecFmadd(R, x2, VecSplatX(SC0));
    R     = VecFmadd(R, x2, VecOne());
    *pSin = VecMul(R, x);

    R     = VecFmadd(VecSplatX(CC1), x2, VecSplatW(CC0));
    R     = VecFmadd(R, x2, VecSplatZ(CC0));
    R     = VecFmadd(R, x2, VecSplatY(CC0));
    R     = VecFmadd(R, x2, VecSplatX(CC0));
    R     = VecFmadd(R, x2, VecOne());
    *pCos = VecMul(R, sign);
}

purefn v128f VCALL VecAtan(v128f x)
{
    const f1 sa1 =  0.99997726f, sa3 = -0.33262347f, sa5  = 0.19354346f,
    sa7 = -0.11643287f, sa9 =  0.05265332f, sa11 = -0.01172120f;
      
    const v128f xx = VecMul(x, x);
    v128f res = VecSet1(sa11); 
    res = VecFmadd(xx, res, VecSet1(sa9));
    res = VecFmadd(xx, res, VecSet1(sa7));
    res = VecFmadd(xx, res, VecSet1(sa5));
    res = VecFmadd(xx, res, VecSet1(sa3));
    res = VecFmadd(xx, res, VecSet1(sa1));
    return VecMul(x, res);
}

purefn v128f VCALL VecAtan2(v128f y, v128f x)
{
    v128f ay = VecFabs(y), ax = VecFabs(x);
    v128i swapMask = VecCmpGt(ay, ax);
    v128f z  = VecDiv(VecBlend(ay, ax, swapMask), VecBlend(ax, ay, swapMask));
    v128f th = VecAtan(z);
    th = VecSelect(th, VecSub(VecSet1(MATH_HalfPI), th), swapMask);
    th = VecSelect(th, VecSub(VecSet1(MATH_PI), th), VecCmpLt(x, VecZero()));
    return VecCopySign(th, y);
}

purefn f1 VCALL Min3v(v128f ab)
{
    v128f xy = VecMin(VecSplatX(ab), VecSplatY(ab));
    return VecGetX(VecMin(xy, VecSplatZ(ab)));
}

purefn f1 VCALL Max3v(v128f ab)
{
    v128f xy = VecMax(VecSplatX(ab), VecSplatY(ab));
    return VecGetX(VecMax(xy, VecSplatZ(ab)));
}

purefn u8 IsPointInsideAABB(v128f point, v128f aabbMin, v128f aabbMax)
{
    v128i cmpMin = VecCmpGe(point, aabbMin);
    v128i cmpMax = VecCmpLe(point, aabbMax);
    #if defined(AX_ARM)
    u32 movemask = VecMovemask(VeciAnd(cmpMin, cmpMax));
    #else
    u32 movemask = VecMovemask(VecAnd(cmpMin, cmpMax));
    #endif
    return (movemask & 0b111) == 0b111;
}

purefn f1 VCALL IntersectAABB(v128f origin, v128f invDir, v128f aabbMin, v128f aabbMax, f1 minSoFar)
{
    if (IsPointInsideAABB(origin, aabbMin, aabbMax)) return 0.1f;
    v128f tmin = VecMul(VecSub(aabbMin, origin), invDir);
    v128f tmax = VecMul(VecSub(aabbMax, origin), invDir);
    f1 tnear = Max3v(VecMin(tmin, tmax));
    f1 tfar  = Min3v(VecMax(tmin, tmax));
    // return tnear < tfar && tnear > 0.0f && tnear < minSoFar;
    if (tnear < tfar && tnear > 0.0f && tnear < minSoFar)
        return tnear; else return 1e30f;
}

purefn f1 Sqrf(f1 x) {
    return x * x;
}

purefn f1 Sqrtf(f1 a) {
    #if defined(AX_SUPPORT_SSE) || defined(AX_SUPPORT_NEON)
    return VecGetX(VecSqrt(VecSet1(a)));
    #else
    return __builtin_sqrt(a);
    #endif
}

purefn f1 Lerpf(f1 x, f1 y, f1 t) {
    return x + (y - x) * t;
}

purefn u8 IsZerof(f1 x) {
    return Absf32(x) <= 0.0001f; 
}

purefn u8 AlmostEqualf(f1 x, f1 y) {
    return Absf32(x-y) <= 0.001f;
}

purefn f1 Signf(f1 x) {
    f1 one = 1.0f;
    s32 res = BitCast(s32, one);
    s32 r1 = BitCast(s32, x);
    res |= r1 & 0x80000000;
    return BitCast(f1, res);
} 

purefn s32 Sign32(s32 x) {
    return x < 0 ? -1 : 1; // equal to above f1 version
} 

purefn f1 CopySignf(f1 x, f1 y) {
    s32 ix = BitCast(s32, x) & 0x7fffffff;
    s32 iy = BitCast(s32, y) & 0x80000000;
    s32 ir = ix | iy;
    return BitCast(f1, ir);
}

purefn u8 IsNanf(f1 f) {
    u32 intValue = BitCast(u32, f);
    u32 exponent = (intValue >> 23) & 0xFF;
    u32 fraction = intValue & 0x7FFFFF;
    return (exponent == 0xFF) && (fraction != 0);
}

purefn s64 Int64MulDiv(s64 value, s64 numer, s64 denom) {
    s64 q = value / denom;
    s64 r = value % denom;
    return q * numer + r * numer / denom;
}

purefn f1 FModf(f1 x, f1 y) 
{
    f1 quotient = x / y;
    s32 whole = (s32)quotient;  // truncate toward zero
    f1 remainder = x - (f1)whole * y;
    if (remainder < 0.0f) remainder += y;
    return remainder;
}

purefn f1 FMod(f1 x, f1 y) 
{
    f1 quotient = x / y;
    s64 whole = (s64)quotient;  // truncate toward zero
    f1 remainder = x - (f1)whole * y;
    if (remainder < 0.0) remainder += y;
    return remainder;
}

purefn f1 Floorf(f1 x) {
    f1 whole = (f1)(s32)x;  // truncate quotient to integer
    return x - (x-whole);
}

purefn f1 Ceilf(f1 x) {
    f1 whole = (f1)(s32)x;  // truncate quotient to integer
    return whole + (f1)(x > whole);
}

purefn f1 Fractf(f1 a) {
    return a - (s32)(a); 
}

purefn f1 Fract(f1 a) {
    return a - (s64)(a); 
}

// https://github.com/id-Software/DOOM-3/blob/master/neo/idlib/math/Math.h
purefn f1 Expf(f1 f) {
    const s32 IEEE_FLT_MANTISSA_BITS  =	23;
    const s32 IEEE_FLT_EXPONENT_BITS  =	8;
    const s32 IEEE_FLT_EXPONENT_BIAS  =	127;
    const s32 IEEE_FLT_SIGN_BIT       =	31;
    
    f1 x = f * 1.44269504088896340f; // multiply with ( 1 / log( 2 ) )
    
    s32 i = BitCast(s32, x);
    s32 s = ( i >> IEEE_FLT_SIGN_BIT );
    s32 e = ( ( i >> IEEE_FLT_MANTISSA_BITS ) & ( ( 1 << IEEE_FLT_EXPONENT_BITS ) - 1 ) ) - IEEE_FLT_EXPONENT_BIAS;
    s32 m = ( i & ( ( 1 << IEEE_FLT_MANTISSA_BITS ) - 1 ) ) | ( 1 << IEEE_FLT_MANTISSA_BITS );
    i = ( ( m >> ( IEEE_FLT_MANTISSA_BITS - e ) ) & ~( e >> 31 ) ) ^ s;
    
    s32 exponent = ( i + IEEE_FLT_EXPONENT_BIAS ) << IEEE_FLT_MANTISSA_BITS;
    f1 y = BitCast(f1, exponent);
    x -= (f1) i;
    if ( x >= 0.5f ) {
        x -= 0.5f;
        y *= 1.4142135623730950488f;	// multiply with sqrt( 2 )
    }
    f1 x2 = x * x;
    f1 p = x * ( 7.2152891511493f + x2 * 0.0576900723731f );
    f1 q = 20.8189237930062f + x2;
    x = y * ( q + p ) / ( q - p );
    return x;
}

// https://martin.ankerl.com/2012/01/25/optimized-approximative-pow-in-c-and-cpp/
// https://github.com/ekmett/approximate/blob/master/cbits/fast.c#L81
// should be much more precise with large b
purefn f1 Powf(f1 a, f1 b) {
    // calculate approximation with fraction of the exponent
    s32 e = (s32) b;
    union {
        f1 d;
        s32 x[2];
    } u = { (f1)a };
    u.x[1] = (s32)((b - e) * (u.x[1] - 1072632447) + 1072632447.0f);
    u.x[0] = 0;
    
    // exponentiation by squaring with the exponent's integer part
    // d1 r = u.d makes everything much slower, not sure why
    f1 r = 1.0;
    while (e) {
        if (e & 1) {
            r *= a;
        }
        a *= a;
        e >>= 1;
    }
    
    return (f1)(u.d * r);
}

// https://github.com/ekmett/approximate/blob/master/cbits/fast.c#L81 <--you can find d1 versions
purefn f1 Logf(f1 x) {
    return (BitCast(s32, x) - 1064866805) * 8.262958405176314e-8f;
}

// if you want log10 for integer you can look at Algorithms.hpp
purefn f1 Log10f(f1 x) { 
    return Logf(x) / 2.30258509299f; // ln(x) / ln(10)
} 

// you might look at this link as well: 
// https://tech.ebayinc.com/engineering/fast-approximate-logarithms-part-i-the-basics/
// A Collection of f1 Tricks pdf
purefn f1 Log2f(f1 val) {
    f1 result = (f1)*((s32*)&val);
    result *= 1.0f / (1 << 23);
    result = result - 127.0f;
    f1 tmp = result - Floorf(result);
    tmp = (tmp - tmp*tmp) * 0.346607f;
    return tmp + result; // ln(x) / ln(2) 
}

// https://graphics.stanford.edu/~seander/bithacks.html#IntegerLog
purefn u32 Log2u32(u32 v) {
    u8 const MultiplyDeBruijnBitPosition[32] = 
    {
        0, 9, 1, 10, 13, 21, 2, 29, 11, 14, 16, 18, 22, 25, 3, 30,
        8, 12, 20, 28, 15, 17, 24, 7, 19, 27, 23, 6, 26, 5, 4, 31
    };
    
    // first round down to one less than a power of 2
    v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16;
    return MultiplyDeBruijnBitPosition[(u32)(v * 0x07C4ACDDU) >> 27];
}

purefn u32 Log10_32(u32 v) {
    // this would work too
    const u32 PowersOf10[] = {             // if (n <= 9) return 1;
        1, 10, 100, 1000, 10000, 100000,            // if (n <= 99) return 2;
        1000000, 10000000, 100000000, 1000000000    // if (n <= 999) return 3;
    };                                              // ...
    u32 t = (Log2u32(v) + 1) * 1233 >> 12;    // if (n <= 2147483647) return 10; // 2147483647 = i32 max
    return t - (v < PowersOf10[t]);                                                      
}                            

static inline u64 Pow10Table64(s32 numDigits)
{
    const u64 Pow10Table[20] = {
        1ULL,
        10ULL,
        100ULL,
        1000ULL,
        10000ULL,
        100000ULL,
        1000000ULL,
        10000000ULL,
        100000000ULL,
        1000000000ULL,
        10000000000ULL,
        100000000000ULL,
        1000000000000ULL,
        10000000000000ULL,
        100000000000000ULL,
        1000000000000000ULL,
        10000000000000000ULL,
        100000000000000000ULL,
        1000000000000000000ULL,
        10000000000000000000ULL
    };
    return Pow10Table[numDigits];
}

static inline s32 Log10_64(u64 v)
{
    s32 l2 = 63 - LeadingZeroCount64(v);
    s32 d = (s32)((l2 + 1) * 0.30103);
    return d + (v >= Pow10Table64(d));
}


/*//////////////////////////////////////////////////////////////////////////*/
/*                      Trigonometric Functions                             */
/*//////////////////////////////////////////////////////////////////////////*/

// https://mazzo.li/posts/vectorized-atan2.html
purefn f1 ATan(f1 x) 
{
    return VecGetX(VecAtan(VecSet1(x)));
}

// Warning! if y and x is zero this will return HalfPI instead of 0.0f unlike cstdlib
purefn f1 ATan2(f1 y, f1 x) {
    
    return VecGetX(VecAtan2(VecSet1(x), VecSet1(y)));
}

// Valid input range -1..1 output is -pi..pi
purefn f1 ACos(f1 x)   
{
    // Lagarde 2014, "Inverse trigonometric functions GPU optimization for AMD GCN architecture"
    // This is the approximation of degree 1, with a max absolute error of 9.0x10^-3
    f1 y = Absf32(x);
    f1 p = -0.1565827f * y + 1.570796f;
    p *= Sqrtf(1.0f - y);
    return x >= 0.0f ? p : MATH_PI - p;
}

purefn f1 ACosPositive(f1 x)
{
    f1 p = -0.1565827f * x + 1.570796f;
    return p * Sqrtf(1.0f - x);
}

// Same cost as Acos + 1 FR Same error
// input [-1, 1] and output [-PI/2, PI/2]
purefn f1 ASin(f1 x) {
    return MATH_HalfPI - ACos(x);
}

// https://en.wikipedia.org/wiki/Sine_and_cosine
// warning: accepts input between -TwoPi and TwoPi  if (Abs(x) > TwoPi) use x = FMod(x + PI, TwoPI) - PI;

purefn f1 RepeatPI(f1 x) {
    return FModf(x + MATH_PI, MATH_TwoPI) - MATH_PI;
}

// https://en.wikipedia.org/wiki/Fast_inverse_square_root
// https://rrrola.wz.cz/inv_sqrt.html   <- fast and 2.5x accurate
purefn f1 RSqrtf(f1 x) {
    #ifdef AX_SUPPORT_SSE
    // when I compile with godbolt -O1 expands to one instruction vrsqrtss
    // which is approximately equal latency as mulps.
    // RSqrt(float):                             # @RSqrt3(float)
    //       vrsqrtss        xmm0, xmm0, xmm0
    //       ret
    return _mm_cvtss_f32(_mm_rsqrt_ps(_mm_set_ps1(x)));
    #elif defined(AX_SUPPORT_NEON)
    return vget_lane_f32(vrsqrte_f32(vdup_n_f32(x)), 0);
    #else
    f1 f = x;
    u32 i = BitCast(u32, f);
    i = (u32)(0x5F1FFFF9ul - (i >> 1));
    f = BitCast(f1, i);
    return 0.703952253f * f * (2.38924456f - x * f * f);
    #endif
}

// cbrt
purefn f1 CubeRootf(f1 val)
{
    const f1 fPower = 0.25f;
    const f1 fScale = 1.0f;
    const f1 oneRepresentationAsf1 = (f1)(0x3f800000);
    f1 magicValue = (f1)((*((const u32*)&fScale))) - (oneRepresentationAsf1 * fPower);
    f1 tmp = (f1)*((u32*)&val);
    tmp = (tmp * fPower) + magicValue;
    u32 tmp2 = (u32)tmp;
    return *(f1*)&tmp2;
}

purefn f1 Sin0pi(f1 x) {
    x *= 0.63655f; // constant founded using desmos
    x *= 2.0f - x;
    return x * (0.225f * x + 0.775f); 
}

purefn f1 Sin(f1 x) 
{
    return VecGetX(VecSin(VecSet1(x)));
}

// Accepts input between -TwoPi and TwoPi, use CosR if value is bigger than this range  
purefn f1 Cos(f1 x)
{
    return VecGetX(VecCos(VecSet1(x)));
}

// R suffix allows us to use with greater range than -TwoPI, TwoPI
purefn f1 SinR(f1 x) {
    return Sin(FModf(x + MATH_PI, MATH_TwoPI) - MATH_PI);
}

// R suffix allows us to use with greater range than -TwoPI, TwoPI
purefn f1 CosR(f1 x) {
    return Cos(FModf(x + MATH_PI, MATH_TwoPI) - MATH_PI);
}

static forceinline void SinCos(f1 x, f1* sp, f1* cp) 
{
    *sp = Sin(x);
    *cp = Cos(x);
}

// https://github.com/id-Software/DOOM-3/blob/master/neo/idlib/math/Math.h
// tanf equivalent
purefn f1 Tan(f1 a) {
    f1 s = 0.0f;
    u8 reciprocal = false;

    if (( a < 0.0f ) || (a >= MATH_PI)) {
        a -= Floorf(a / MATH_PI) * MATH_PI;
    }
    
    if ( a < MATH_HalfPI ) {
        u8 greater = a > MATH_QuarterPI;
        reciprocal = greater;
        if (greater) a = MATH_HalfPI - a;
    } else {
        u8 greater = a > MATH_HalfPI + MATH_QuarterPI;
        reciprocal = !greater;
        a = greater ? a - MATH_PI : MATH_HalfPI - a;
    }

    s = a * a;
    s = a * ((((((9.5168091e-03f * s + 2.900525e-03f ) * s + 2.45650893e-02f) * s + 5.33740603e-02f) * s + 1.333923995e-01f) * s + 3.333314036e-01f) * s + 1.0f);
    return reciprocal ? 1.0f / s : s;
}

// inspired from Casey Muratori's performance aware programming
// this functions makes the code more readable. OpenCL and Cuda has the same functions as well
purefn f1 ATan2PI(f1 y, f1 x) { return ATan2(y, x) / MATH_PI; }
purefn f1 ASinPI(f1 z) { return ASin(z) / MATH_PI; }
purefn f1 ACosPI(f1 x) { return ACos(x) / MATH_PI; }
purefn f1 CosPI(f1 x)  { return Cos(x) / MATH_PI; }
purefn f1 SinPI(f1 x)  { return Sin(x) / MATH_PI; }

///////////////////////////////////////////////////////////////////////////
// Packing

// packs -1,1 range f1 to short
purefn u16 PackSnorm16(f1 x) {
    return (u16)Clampf32(x * (f1)INT16_MAX, (f1)INT16_MIN, (f1)INT16_MAX);
}

purefn f1 UnpackSnorm16(u16 x) {
    return (f1)x / (f1)INT16_MAX;
}

// packs 0,1 range f1 to short
purefn u16 PackUnorm16(f1 x) {
    return (u16)Clampf32(x * (f1)INT16_MAX, (f1)INT16_MIN, (f1)INT16_MAX);
}

purefn f1 UnpackUnorm16(u16 x) {
    return (f1)x / (f1)UINT16_MAX;
}

///////////////////////////////////////////////////////////////////////////
// Easing

// to see visually: https://easings.net/ 
purefn f1 EaseIn(f1 x) {
    return x * x;
}

purefn f1 EaseOut(f1 x) { 
    f1 r = 1.0f - x;
    return 1.0f - (r * r); 
}

purefn f1 EaseInOut(f1 x) {
    return x < 0.5f ? 2.0f * x * x : 1.0f - Sqrf(-2.0f * x + 2.0f) / 2.0f;
}

// integral symbol shaped interpolation, similar to EaseInOut
purefn f1 SmoothStep(f1 edge0, f1 edge1, f1 x) {
    f1 t = Clamp01f32((x - edge0) / (edge1 - edge0));
    return t * t * (3.0f - t * 2.0f);
}

purefn f1 EaseInSine(f1 x) {
    return 1.0f - Cos((x * MATH_PI) * 0.5f);
}

purefn f1 EaseOutSine(f1 x) {
    return Sin((x * MATH_PI) * 0.5f);
}


///////////////////////////////////////////////////////////////////////////
// Other

// Gradually changes a value towards a desired goal over time.
purefn f1 SmoothDamp(f1 current, f1 target, f1* currentVelocity, f1 smoothTime, f1 maxSpeed, f1 deltaTime)
{
    // Based on Game Programming Gems 4 Chapter 1.10
    smoothTime = MMAX(0.0001f, smoothTime);
    f1 omega = 2.0f / smoothTime;

    f1 x = omega * deltaTime;
    f1 exp = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
    f1 change = current - target;
    f1 originalTo = target;

    // Clamp maximum speed
    f1 maxChange = maxSpeed * smoothTime;
    change = Clampf32(change, -maxChange, maxChange);
    target = current - change;

    f1 temp = (*currentVelocity + omega * change) * deltaTime;
    *currentVelocity = (*currentVelocity - omega * temp) * exp;
    f1 output = target + (change + temp) * exp;

    // Prevent overshooting
    if (originalTo - current > 0.0f == output > originalTo)
    {
        output = originalTo;
        *currentVelocity = (output - originalTo) / deltaTime;
    }

    return output;
}

purefn f1 Remap(f1 in, f1 inMin, f1 inMax, f1 outMin, f1 outMax)
{
    return outMin + (in - inMin) * (outMax - outMin) / (inMax - inMin);
}

purefn f1 Repeat(f1 t, f1 length)
{
    return Clampf32(t - Floorf(t / length) * length, 0.0f, length);
}

purefn f1 Step(f1 edge, f1 x)
{
    return (f1)(x > edge);
}

// https://en.wikipedia.org/wiki/Distance_from_a_point_to_a_line
// position(x0, y0), linePos1(x1, y1), linePos2(x2, y2) 
purefn f1 LineDistance(f1 x0, f1 y0, f1 x1, f1 y1, f1 x2, f1 y2)
{
    f1 a = ((x2 - x1) * (y0 - y1)) - ((x0 - x1) * (y2 - y1));
    return Absf32(a) * RSqrtf(Sqrf(x2 - x1) + Sqrf(y2 - y1));
}

#endif // MATH_H