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


purefn Vector4x32f VECTORCALL Vec3CrossV(Vector4x32f vec0, Vector4x32f vec1)
{
#if defined(AX_ARM)
    float32x2_t v1xy = vget_low_f32(vec0);
    float32x2_t v2xy = vget_low_f32(vec1);
    float32x2_t v1yx = vrev64_f32(v1xy);
    float32x2_t v2yx = vrev64_f32(v2xy);
    float32x2_t v1zz = vdup_lane_f32(vget_high_f32(vec0), 0);
    float32x2_t v2zz = vdup_lane_f32(vget_high_f32(vec1), 0);
    uint32x4_t FlipY = ARMCreateVecI(0x00000000u, 0x80000000u, 0x00000000u, 0x00000000u);
    Vector4x32f vResult = vmulq_f32(vcombine_f32(v1yx, v1xy), vcombine_f32(v2zz, v2yx));
    vResult = vmlsq_f32(vResult, vcombine_f32(v1zz, v1yx), vcombine_f32(v2yx, v2xy));
    vResult = vreinterpretq_f32_u32(veorq_u32(vreinterpretq_u32_f32(vResult), FlipY));
    return vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(vResult), VecMask3));
#else
    Vector4x32f tmp0 = VecShuffleR(vec0, vec0, 3,0,2,1);
    Vector4x32f tmp1 = VecShuffleR(vec1, vec1, 3,1,0,2);
    Vector4x32f tmp2 = VecMul(tmp0, vec1);
    Vector4x32f tmp3 = VecMul(tmp0, tmp1);
    Vector4x32f tmp4 = VecShuffleR(tmp2, tmp2, 3,0,2,1);
    return VecSub(tmp3, tmp4);
#endif
}

purefn float VECTORCALL Min3(Vector4x32f ab)
{
    Vector4x32f xy = VecMin(VecSplatX(ab), VecSplatY(ab));
    return VecGetX(VecMin(xy, VecSplatZ(ab)));
}

purefn float VECTORCALL Max3(Vector4x32f ab)
{
    Vector4x32f xy = VecMax(VecSplatX(ab), VecSplatY(ab));
    return VecGetX(VecMax(xy, VecSplatZ(ab)));
}

#define VecClamp01(v) VecClamp(v, VecZero(), VecOne())

purefn Vector4x32f VECTORCALL VecClamp(Vector4x32f v, Vector4x32f vmin, Vector4x32f vmax)
{
    v = VecSelect(v, vmax, VecCmpGt(v, vmax));
    v = VecSelect(v, vmin, VecCmpLt(v, vmin));
    return v;
}

static forceinline void VECTORCALL Vec3Store(float* f, Vector4x32f v)
{
    f[0] = VecGetX(v);
    f[1] = VecGetY(v);
    f[2] = VecGetZ(v);
}

purefn Vector4x32f VECTORCALL VecHSum(Vector4x32f v) {
    v = VecHadd(v, v); // high half -> low half
    return VecHadd(v, v);
}

#if defined(AX_ARM)
    #define VecFabs(x) vabsq_f32(x)
#elif defined(AX_SUPPORT_SSE)
    #define VecFabs(x) VecAnd(x, VecFromInt1(0x7fffffff))
#else
    #define VecFabs(v) MakeVec4(Abs(v.x), Abs(v.y), Abs(v.z), Abs(v.w))
#endif

purefn Vector4x32f VECTORCALL VecCopySign(Vector4x32f x, Vector4x32f y)
{
    Vector4x32u clearedX = VeciAnd(VeciFromVec(x), VeciSet1(0x7fffffff));
    Vector4x32u signY    = VeciAnd(VeciFromVec(y), VeciSet1(0x80000000));
    Vector4x32u res      = VeciOr(clearedX, signY);
    return VecFromVeci(res);
}

purefn Vector4x32f VECTORCALL VecLerp(Vector4x32f x, Vector4x32f y, float t)
{
    return VecFmadd(VecSub(y, x), VecSet1(t), x);
}

purefn Vector4x32f VECTORCALL VecStep(Vector4x32f edge, Vector4x32f x)
{
    return VecBlend(VecZero(), VecOne(), VecCmpGt(x, edge));
}

purefn Vector4x32f VECTORCALL VecFract(Vector4x32f x)
{
    return VecSub(x, VecFloor(x));
}

static inline Vector4x32f VecModAngles(Vector4x32f angles)
{
    Vector4x32f twoPi       = VecSet1(2.0f * MATH_PI);
    Vector4x32f recipTwoPi  = VecSet1(1.0f / (2.0f * MATH_PI));
    Vector4x32f v = VecMul(angles, recipTwoPi); // Multiply by 1/(2*pi)
    v = VecRound(v); 
    return VecSub(angles, VecMul(v, twoPi));
}

purefn Vector4x32f VECTORCALL VecSin(Vector4x32f V)
{
    Vector4x32f  g_XMSinCoefficients0 = VecSetR( -0.16666667f, +0.0083333310f, -0.00019840874f, +2.7525562e-06f );
    Vector4x32f  g_XMSinCoefficients1 = VecSetR( -2.3889859e-08f, -0.16665852f /*Est1*/, +0.0083139502f /*Est2*/, -0.00018524670f /*Est3*/ );
    // Force the value within the bounds of pi
    Vector4x32f x = VecModAngles(V);
    #if defined(AX_ARM)
    uint32x4_t sign = vandq_u32(vreinterpretq_u32_f32(x), g_XMNegativeZero);
    uint32x4_t c = vorrq_u32(g_XMPi, sign);  // pi when x >= 0, -pi when x < 0
    float32x4_t absx = vabsq_f32(x);
    float32x4_t rflx = vsubq_f32(vreinterpretq_f32_u32(c), x);
    uint32x4_t comp = vcleq_f32(absx, g_XMHalfPi);
    x = vbslq_f32(comp, x, rflx);
    float32x4_t x2 = vmulq_f32(x, x);
    XMVECTOR vConstants = vdupq_lane_f32(vget_high_f32(SC0), 1);
    XMVECTOR Result = vmlaq_lane_f32(vConstants, x2, vget_low_f32(SC1), 0);
    vConstants = vdupq_lane_f32(vget_high_f32(SC0), 0);
    Result = vmlaq_f32(vConstants, Result, x2);
    vConstants = vdupq_lane_f32(vget_low_f32(SC0), 1);
    Result = vmlaq_f32(vConstants, Result, x2);
    vConstants = vdupq_lane_f32(vget_low_f32(SC0), 0);
    Result = vmlaq_f32(vConstants, Result, x2);
    Result = vmlaq_f32(g_XMOne, Result, x2);
    Result = vmulq_f32(Result, x);
    return Result;
    #elif defined(AX_SUPPORT_SSE)

    // Map in [-pi/2,pi/2] with sin(y) = sin(x).
    Vector4x32f sign = _mm_and_ps(x, _mm_set1_ps(-0.0f));
    Vector4x32f c = _mm_or_ps(_mm_set1_ps(3.141592654f), sign);  // pi when x >= 0, -pi when x < 0
    Vector4x32f absx = _mm_andnot_ps(sign, x);  // |x|
    Vector4x32f rflx = _mm_sub_ps(c, x);
    Vector4x32f comp = _mm_cmple_ps(absx, _mm_set1_ps(1.570796327f));
    Vector4x32f select0 = _mm_and_ps(comp, x);
    Vector4x32f select1 = _mm_andnot_ps(comp, rflx);
    x = _mm_or_ps(select0, select1);
    Vector4x32f x2 = _mm_mul_ps(x, x);

    // Compute polynomial approximation
    Vector4x32f vConstantsB = VecSplatX(g_XMSinCoefficients1);
    Vector4x32f Result = VecFmadd(vConstantsB, x2, VecSplatW(g_XMSinCoefficients0));
    Result = VecFmadd(Result, x2, VecSplatZ(g_XMSinCoefficients0));
    Result = VecFmadd(Result, x2, VecSplatY(g_XMSinCoefficients0));
    Result = VecFmadd(Result, x2, VecSplatX(g_XMSinCoefficients0));
    Result = VecFmadd(Result, x2, VecOne());
    Result = VecMul(Result, x);
    return Result;
    #endif
}

//------------------------------------------------------------------------------

purefn Vector4x32f VECTORCALL VecCos(Vector4x32f V)
{
    Vector4x32f  g_XMCosCoefficients0 = VecSetR( -0.5f, +0.041666638f, -0.0013888378f, +2.4760495e-05f  );
    Vector4x32f  g_XMCosCoefficients1 = VecSetR( -2.6051615e-07f, -0.49992746f /*Est1*/, +0.041493919f /*Est2*/, -0.0012712436f /*Est3*/  );
    // Map V to x in [-pi,pi].
    Vector4x32f  x = VecModAngles(V);
    #if defined(AX_ARM)
    // Map in [-pi/2,pi/2] with cos(y) = sign*cos(x).
    uint32x4_t sign = vandq_u32(vreinterpretq_u32_f32(x), g_XMNegativeZero);
    uint32x4_t c = vorrq_u32(g_XMPi, sign);  // pi when x >= 0, -pi when x < 0
    float32x4_t absx = vabsq_f32(x);
    float32x4_t rflx = vsubq_f32(vreinterpretq_f32_u32(c), x);
    uint32x4_t comp = vcleq_f32(absx, g_XMHalfPi);
    x = vbslq_f32(comp, x, rflx);
    float32x4_t fsign = vbslq_f32(comp, g_XMOne, g_XMNegativeOne);
    float32x4_t x2 = vmulq_f32(x, x);
    XMVECTOR vConstants = vdupq_lane_f32(vget_high_f32(g_XMCosCoefficients0), 1);
    XMVECTOR Result = vmlaq_lane_f32(vConstants, x2, vget_low_f32(g_XMCosCoefficients1), 0);
    vConstants = vdupq_lane_f32(vget_high_f32(g_XMCosCoefficients0), 0);
    Result = vmlaq_f32(vConstants, Result, x2);
    vConstants = vdupq_lane_f32(vget_low_f32(g_XMCosCoefficients0), 1);
    Result = vmlaq_f32(vConstants, Result, x2);
    vConstants = vdupq_lane_f32(vget_low_f32(g_XMCosCoefficients0), 0);
    Result = vmlaq_f32(vConstants, Result, x2);
    Result = vmlaq_f32(g_XMOne, Result, x2);
    Result = vmulq_f32(Result, fsign);
    return Result;
    #elif defined(AX_SUPPORT_SSE)
    // Map in [-pi/2,pi/2] with cos(y) = sign*cos(x).
    Vector4x32f sign = _mm_and_ps(x, _mm_set1_ps(-0.0f));
    Vector4x32f c = _mm_or_ps(_mm_set1_ps(3.141592654f), sign);  // pi when x >= 0, -pi when x < 0
    Vector4x32f absx = _mm_andnot_ps(sign, x);  // |x|
    Vector4x32f comp = _mm_cmple_ps(absx, _mm_set1_ps(3.141592654f * 0.5f));
    Vector4x32f select0 = _mm_and_ps(comp, x);
    Vector4x32f select1 = _mm_andnot_ps(comp, _mm_sub_ps(c, x));
    x = _mm_or_ps(select0, select1);
    select0 = _mm_and_ps(comp, VecOne());
    select1 = _mm_andnot_ps(comp, VecNegativeOne());
    sign = _mm_or_ps(select0, select1);
    Vector4x32f x2 = _mm_mul_ps(x, x);

    // Compute polynomial approximation
    Vector4x32f Result = VecFmadd(VecSplatX(g_XMCosCoefficients1), x2, VecSplatW(g_XMCosCoefficients0));
    Result = VecFmadd(Result, x2, VecSplatZ(g_XMCosCoefficients0));
    Result = VecFmadd(Result, x2, VecSplatY(g_XMCosCoefficients0));
    Result = VecFmadd(Result, x2, VecSplatX(g_XMCosCoefficients0));
    Result = VecFmadd(Result, x2, VecOne());
    Result = VecMul(Result, sign);
    return Result;
    #endif
}


purefn Vector4x32f VECTORCALL VecAtan(Vector4x32f x)
{
    const float sa1 =  0.99997726f, sa3 = -0.33262347f, sa5  = 0.19354346f,
    sa7 = -0.11643287f, sa9 =  0.05265332f, sa11 = -0.01172120f;
      
    const Vector4x32f xx = VecMul(x, x);
    // (a9 + x_sq * a11
    Vector4x32f res = VecSet1(sa11); 
    res = VecFmadd(xx, res, VecSet1(sa9));
    res = VecFmadd(xx, res, VecSet1(sa7));
    res = VecFmadd(xx, res, VecSet1(sa5));
    res = VecFmadd(xx, res, VecSet1(sa3));
    res = VecFmadd(xx, res, VecSet1(sa1));
    return VecMul(x, res);
}

purefn Vector4x32f VECTORCALL VecAtan2(Vector4x32f y, Vector4x32f x)
{
    Vector4x32f ay = VecFabs(y), ax = VecFabs(x);
    Vector4x32i swapMask = VecCmpGt(ay, ax);
    Vector4x32f z  = VecDiv(VecBlend(ay, ax, swapMask), VecBlend(ax, ay, swapMask));
    Vector4x32f th = VecAtan(z);
    th = VecSelect(th, VecSub(VecSet1(MATH_HalfPI), th), swapMask);
    th = VecSelect(th, VecSub(VecSet1(MATH_PI), th), VecCmpLt(x, VecZero()));
    return VecCopySign(th, y);
}

purefn Vector4x32f VECTORCALL VecSinCos(Vector4x32f* cv, Vector4x32f x)
{
    Vector4x32f s = VecSin(x);
    *cv = VecCos(x);
    return s;
}

purefn float VECTORCALL Min3v(Vector4x32f ab)
{
    Vector4x32f xy = VecMin(VecSplatX(ab), VecSplatY(ab));
    return VecGetX(VecMin(xy, VecSplatZ(ab)));
}

purefn float VECTORCALL Max3v(Vector4x32f ab)
{
    Vector4x32f xy = VecMax(VecSplatX(ab), VecSplatY(ab));
    return VecGetX(VecMax(xy, VecSplatZ(ab)));
}

purefn bool IsPointInsideAABB(Vector4x32f point, Vector4x32f aabbMin, Vector4x32f aabbMax)
{
    Vector4x32i cmpMin = VecCmpGe(point, aabbMin);
    Vector4x32i cmpMax = VecCmpLe(point, aabbMax);
    #if defined(AX_ARM)
    uint32_t movemask = VecMovemask(VeciAnd(cmpMin, cmpMax));
    #else
    uint32_t movemask = VecMovemask(VecAnd(cmpMin, cmpMax));
    #endif
    return (movemask & 0b111) == 0b111;
}

purefn float VECTORCALL IntersectAABB(Vector4x32f origin, Vector4x32f invDir, Vector4x32f aabbMin, Vector4x32f aabbMax, float minSoFar)
{
    if (IsPointInsideAABB(origin, aabbMin, aabbMax)) return 0.1f;
    Vector4x32f tmin = VecMul(VecSub(aabbMin, origin), invDir);
    Vector4x32f tmax = VecMul(VecSub(aabbMax, origin), invDir);
    float tnear = Max3v(VecMin(tmin, tmax));
    float tfar  = Min3v(VecMax(tmin, tmax));
    // return tnear < tfar && tnear > 0.0f && tnear < minSoFar;
    if (tnear < tfar && tnear > 0.0f && tnear < minSoFar)
        return tnear; else return 1e30f;
}

// calculate popcount of 4 32 bit integer concurrently
purefn Vector4x32u VECTORCALL PopCount32_128(Vector4x32u x)
{
    Vector4x32u y;
    y = VeciAnd(VeciSrl32(x, 1), VeciSet1(0x55555555));
    x = VeciSub(x, y);
    y = VeciAnd(VeciSrl32(x, 2), VeciSet1(0x33333333));
    x = VeciAdd(VeciAnd(x, VeciSet1(0x33333333)), y);  
    x = VeciAnd(VeciAdd(x, VeciSrl32(x, 4)), VeciSet1(0x0F0F0F0F));
    return VeciSrl32(VeciMul(x, VeciSet1(0x01010101)),  24);
}

// LeadingZeroCount of 4 32 bit integer concurrently
purefn Vector4x32u VECTORCALL LeadingZeroCount32_128(Vector4x32u x)
{
    x = VeciOr(x, VeciSrl32(x, 1));
    x = VeciOr(x, VeciSrl32(x, 2));
    x = VeciOr(x, VeciSrl32(x, 4));
    x = VeciOr(x, VeciSrl32(x, 8));
    x = VeciOr(x, VeciSrl32(x, 16)); 
    return VeciSub(VeciSet1(sizeof(uint32_t) * 8), PopCount32_128(x));
}   

purefn float Sqrf(float x) {
    return x * x;
}

purefn float Sqrtf(float a) {
    #if defined(AX_SUPPORT_SSE) || defined(AX_SUPPORT_NEON)
    return VecGetX(VecSqrt(VecSet1(a)));
    #else
    return __builtin_sqrt(a);
    #endif
}

purefn float Lerpf(float x, float y, float t) {
    return x + (y - x) * t;
}

purefn bool IsZerof(float x) {
    return Absf(x) <= 0.0001f; 
}

purefn bool AlmostEqualf(float x, float y) {
    return Absf(x-y) <= 0.001f;
}

purefn float Signf(float x) {
    float one = 1.0f;
    int res = BitCast(int, one);
    int r1 = BitCast(int, x);
    res |= r1 & 0x80000000;
    return BitCast(float, res);
} 

purefn int Sign32(int x) {
    return x < 0 ? -1 : 1; // equal to above float version
} 

purefn float CopySignf(float x, float y) {
    int ix = BitCast(int, x) & 0x7fffffff;
    int iy = BitCast(int, y) & 0x80000000;
    int ir = ix | iy;
    return BitCast(float, ir);
}

purefn bool IsNanf(float f) {
    uint32_t intValue = BitCast(uint32_t, f);
    uint32_t exponent = (intValue >> 23) & 0xFF;
    uint32_t fraction = intValue & 0x7FFFFF;
    return (exponent == 0xFF) && (fraction != 0);
}

purefn float FModf(float x, float y) 
{
    float quotient = x / y;
    int whole = (int)quotient;  // truncate toward zero
    float remainder = x - (float)whole * y;
    if (remainder < 0.0f) remainder += y;
    return remainder;
}

purefn float Floorf(float x) {
    float whole = (float)(int)x;  // truncate quotient to integer
    return x - (x-whole);
}

purefn float Ceilf(float x) {
    float whole = (float)(int)x;  // truncate quotient to integer
    return whole + (float)(x > whole);
}

purefn float Fractf(float a) {
    return a - (int)(a); 
}

purefn double Fract(double a) {
    return a - (uint64_t)(a); 
}

// https://github.com/id-Software/DOOM-3/blob/master/neo/idlib/math/Math.h
purefn float Expf(float f) {
    const int IEEE_FLT_MANTISSA_BITS  =	23;
    const int IEEE_FLT_EXPONENT_BITS  =	8;
    const int IEEE_FLT_EXPONENT_BIAS  =	127;
    const int IEEE_FLT_SIGN_BIT       =	31;
    
    float x = f * 1.44269504088896340f; // multiply with ( 1 / log( 2 ) )
    
    int i = BitCast(int, x);
    int s = ( i >> IEEE_FLT_SIGN_BIT );
    int e = ( ( i >> IEEE_FLT_MANTISSA_BITS ) & ( ( 1 << IEEE_FLT_EXPONENT_BITS ) - 1 ) ) - IEEE_FLT_EXPONENT_BIAS;
    int m = ( i & ( ( 1 << IEEE_FLT_MANTISSA_BITS ) - 1 ) ) | ( 1 << IEEE_FLT_MANTISSA_BITS );
    i = ( ( m >> ( IEEE_FLT_MANTISSA_BITS - e ) ) & ~( e >> 31 ) ) ^ s;
    
    int exponent = ( i + IEEE_FLT_EXPONENT_BIAS ) << IEEE_FLT_MANTISSA_BITS;
    float y = BitCast(float, exponent);
    x -= (float) i;
    if ( x >= 0.5f ) {
        x -= 0.5f;
        y *= 1.4142135623730950488f;	// multiply with sqrt( 2 )
    }
    float x2 = x * x;
    float p = x * ( 7.2152891511493f + x2 * 0.0576900723731f );
    float q = 20.8189237930062f + x2;
    x = y * ( q + p ) / ( q - p );
    return x;
}

// https://martin.ankerl.com/2012/01/25/optimized-approximative-pow-in-c-and-cpp/
// https://github.com/ekmett/approximate/blob/master/cbits/fast.c#L81
// should be much more precise with large b
purefn float Powf(float a, float b) {
    // calculate approximation with fraction of the exponent
    int e = (int) b;
    union {
        double d;
        int x[2];
    } u = { (double)a };
    u.x[1] = (int)((b - e) * (u.x[1] - 1072632447) + 1072632447.0f);
    u.x[0] = 0;
    
    // exponentiation by squaring with the exponent's integer part
    // double r = u.d makes everything much slower, not sure why
    float r = 1.0;
    while (e) {
        if (e & 1) {
            r *= a;
        }
        a *= a;
        e >>= 1;
    }
    
    return (float)(u.d * r);
}

// https://github.com/ekmett/approximate/blob/master/cbits/fast.c#L81 <--you can find double versions
purefn float Logf(float x) {
    return (BitCast(int, x) - 1064866805) * 8.262958405176314e-8f;
}

// if you want log10 for integer you can look at Algorithms.hpp
purefn float Log10f(float x) { 
    return Logf(x) / 2.30258509299f; // ln(x) / ln(10)
} 

// you might look at this link as well: 
// https://tech.ebayinc.com/engineering/fast-approximate-logarithms-part-i-the-basics/
// A Collection of Float Tricks pdf
purefn float Log2f(float val) {
    float result = (float)*((int*)&val);
    result *= 1.0f / (1 << 23);
    result = result - 127.0f;
    float tmp = result - Floorf(result);
    tmp = (tmp - tmp*tmp) * 0.346607f;
    return tmp + result; // ln(x) / ln(2) 
}

// https://graphics.stanford.edu/~seander/bithacks.html#IntegerLog
purefn unsigned int Log2_32(unsigned int v) {
    static const uint8_t MultiplyDeBruijnBitPosition[32] = 
    {
        0, 9, 1, 10, 13, 21, 2, 29, 11, 14, 16, 18, 22, 25, 3, 30,
        8, 12, 20, 28, 15, 17, 24, 7, 19, 27, 23, 6, 26, 5, 4, 31
    };
    
    // first round down to one less than a power of 2
    v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16;
    return MultiplyDeBruijnBitPosition[(uint32_t)(v * 0x07C4ACDDU) >> 27];
}

purefn unsigned int Log10_32(unsigned int v) {
    // this would work too
    unsigned int const PowersOf10[] = {             // if (n <= 9) return 1;
        1, 10, 100, 1000, 10000, 100000,            // if (n <= 99) return 2;
        1000000, 10000000, 100000000, 1000000000    // if (n <= 999) return 3;
    };                                              // ...
    unsigned int t = (Log2_32(v) + 1) * 1233 >> 12;    // if (n <= 2147483647) return 10; // 2147483647 = int max
    return t - (v < PowersOf10[t]);                                                      
}                                                                                        

/*//////////////////////////////////////////////////////////////////////////*/
/*                      Trigonometric Functions                             */
/*//////////////////////////////////////////////////////////////////////////*/

// https://mazzo.li/posts/vectorized-atan2.html
purefn float ATan(float x) 
{
    return VecGetX(VecAtan(VecSet1(x)));
}

// Warning! if y and x is zero this will return HalfPI instead of 0.0f unlike cstdlib
purefn float ATan2(float y, float x) {
    
    return VecGetX(VecAtan2(VecSet1(x), VecSet1(y)));
}

// Valid input range -1..1 output is -pi..pi
purefn float ACos(float x)   
{
    // Lagarde 2014, "Inverse trigonometric functions GPU optimization for AMD GCN architecture"
    // This is the approximation of degree 1, with a max absolute error of 9.0x10^-3
    float y = Absf(x);
    float p = -0.1565827f * y + 1.570796f;
    p *= Sqrtf(1.0f - y);
    return x >= 0.0f ? p : MATH_PI - p;
}

purefn float ACosPositive(float x)
{
    float p = -0.1565827f * x + 1.570796f;
    return p * Sqrtf(1.0f - x);
}

// Same cost as Acos + 1 FR Same error
// input [-1, 1] and output [-PI/2, PI/2]
purefn float ASin(float x) {
    return MATH_HalfPI - ACos(x);
}

// https://en.wikipedia.org/wiki/Sine_and_cosine
// warning: accepts input between -TwoPi and TwoPi  if (Abs(x) > TwoPi) use x = FMod(x + PI, TwoPI) - PI;

purefn float RepeatPI(float x) {
    return FModf(x + MATH_PI, MATH_TwoPI) - MATH_PI;
}

// https://en.wikipedia.org/wiki/Fast_inverse_square_root
// https://rrrola.wz.cz/inv_sqrt.html   <- fast and 2.5x accurate
purefn float RSqrtf(float x) {
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
    float f = x;
    uint32_t i = BitCast(uint32_t, f);
    i = (uint32_t)(0x5F1FFFF9ul - (i >> 1));
    f = BitCast(float, i);
    return 0.703952253f * f * (2.38924456f - x * f * f);
    #endif
}

// cbrt
purefn float CubeRootf(float val)
{
    const float fPower = 0.25f;
    const float fScale = 1.0f;
    const float oneRepresentationAsFloat = (float)(0x3f800000);
    float magicValue = (float)((*((const unsigned int*)&fScale))) - (oneRepresentationAsFloat * fPower);
    float tmp = (float)*((unsigned int*)&val);
    tmp = (tmp * fPower) + magicValue;
    unsigned int tmp2 = (unsigned int)tmp;
    return *(float*)&tmp2;
}

purefn float Sin0pi(float x) {
    x *= 0.63655f; // constant founded using desmos
    x *= 2.0f - x;
    return x * (0.225f * x + 0.775f); 
}

purefn float Sin(float x) 
{
    return VecGetX(VecSin(VecSet1(x)));
}

// Accepts input between -TwoPi and TwoPi, use CosR if value is bigger than this range  
purefn float Cos(float x)
{
    return VecGetX(VecCos(VecSet1(x)));
}

// R suffix allows us to use with greater range than -TwoPI, TwoPI
purefn float SinR(float x) {
    return Sin(FModf(x + MATH_PI, MATH_TwoPI) - MATH_PI);
}

// R suffix allows us to use with greater range than -TwoPI, TwoPI
purefn float CosR(float x) {
    return Cos(FModf(x + MATH_PI, MATH_TwoPI) - MATH_PI);
}

static forceinline void SinCos(float x, float* sp, float* cp) 
{
    *sp = Sin(x);
    *cp = Cos(x);
}

// https://github.com/id-Software/DOOM-3/blob/master/neo/idlib/math/Math.h
// tanf equivalent
purefn float Tan(float a) {
    float s = 0.0f;
    bool reciprocal = false;

    if (( a < 0.0f ) || (a >= MATH_PI)) {
        a -= Floorf(a / MATH_PI) * MATH_PI;
    }
    
    if ( a < MATH_HalfPI ) {
        bool greater = a > MATH_QuarterPI;
        reciprocal = greater;
        if (greater) a = MATH_HalfPI - a;
    } else {
        bool greater = a > MATH_HalfPI + MATH_QuarterPI;
        reciprocal = !greater;
        a = greater ? a - MATH_PI : MATH_HalfPI - a;
    }

    s = a * a;
    s = a * ((((((9.5168091e-03f * s + 2.900525e-03f ) * s + 2.45650893e-02f) * s + 5.33740603e-02f) * s + 1.333923995e-01f) * s + 3.333314036e-01f) * s + 1.0f);
    return reciprocal ? 1.0f / s : s;
}

// inspired from Casey Muratori's performance aware programming
// this functions makes the code more readable. OpenCL and Cuda has the same functions as well
purefn float ATan2PI(float y, float x) { return ATan2(y, x) / MATH_PI; }
purefn float ASinPI(float z) { return ASin(z) / MATH_PI; }
purefn float ACosPI(float x) { return ACos(x) / MATH_PI; }
purefn float CosPI(float x)  { return Cos(x) / MATH_PI; }
purefn float SinPI(float x)  { return Sin(x) / MATH_PI; }

///////////////////////////////////////////////////////////////////////////
// Packing

// packs -1,1 range float to short
purefn short PackSnorm16(float x) {
    return (short)Clampf(x * (float)INT16_MAX, (float)INT16_MIN, (float)INT16_MAX);
}

purefn float UnpackSnorm16(short x) {
    return (float)x / (float)INT16_MAX;
}

// packs 0,1 range float to short
purefn short PackUnorm16(float x) {
    return (short)Clampf(x * (float)INT16_MAX, (float)INT16_MIN, (float)INT16_MAX);
}

purefn float UnpackUnorm16(short x) {
    return (float)x / (float)UINT16_MAX;
}

///////////////////////////////////////////////////////////////////////////
// Easing

// to see visually: https://easings.net/ 
purefn float EaseIn(float x) {
    return x * x;
}

purefn float EaseOut(float x) { 
    float r = 1.0f - x;
    return 1.0f - (r * r); 
}

purefn float EaseInOut(float x) {
    return x < 0.5f ? 2.0f * x * x : 1.0f - Sqrf(-2.0f * x + 2.0f) / 2.0f;
}

// integral symbol shaped interpolation, similar to EaseInOut
purefn float SmoothStep(float edge0, float edge1, float x) {
    float t = Clamp01f((x - edge0) / (edge1 - edge0));
    return t * t * (3.0f - t * 2.0f);
}

purefn float EaseInSine(float x) {
    return 1.0f - Cos((x * MATH_PI) * 0.5f);
}

purefn float EaseOutSine(float x) {
    return Sin((x * MATH_PI) * 0.5f);
}


///////////////////////////////////////////////////////////////////////////
// Other

// Gradually changes a value towards a desired goal over time.
purefn float SmoothDamp(float current, float target, float* currentVelocity, float smoothTime, float maxSpeed, float deltaTime)
{
    // Based on Game Programming Gems 4 Chapter 1.10
    smoothTime = MMAX(0.0001f, smoothTime);
    float omega = 2.0f / smoothTime;

    float x = omega * deltaTime;
    float exp = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
    float change = current - target;
    float originalTo = target;

    // Clamp maximum speed
    float maxChange = maxSpeed * smoothTime;
    change = Clampf(change, -maxChange, maxChange);
    target = current - change;

    float temp = (*currentVelocity + omega * change) * deltaTime;
    *currentVelocity = (*currentVelocity - omega * temp) * exp;
    float output = target + (change + temp) * exp;

    // Prevent overshooting
    if (originalTo - current > 0.0f == output > originalTo)
    {
        output = originalTo;
        *currentVelocity = (output - originalTo) / deltaTime;
    }

    return output;
}

purefn float Remap(float in, float inMin, float inMax, float outMin, float outMax)
{
    return outMin + (in - inMin) * (outMax - outMin) / (inMax - inMin);
}

purefn float Repeat(float t, float length)
{
    return Clampf(t - Floorf(t / length) * length, 0.0f, length);
}

purefn float Step(float edge, float x)
{
    return (float)(x > edge);
}

// https://en.wikipedia.org/wiki/Distance_from_a_point_to_a_line
// position(x0, y0), linePos1(x1, y1), linePos2(x2, y2) 
purefn float LineDistance(float x0, float y0, float x1, float y1, float x2, float y2)
{
    float a = ((x2 - x1) * (y0 - y1)) - ((x0 - x1) * (y2 - y1));
    return Absf(a) * RSqrtf(Sqrf(x2 - x1) + Sqrf(y2 - y1));
}

#endif // MATH_H