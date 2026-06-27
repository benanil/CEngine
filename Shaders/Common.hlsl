#pragma once
#ifndef HLSL_COMMON_H
#define HLSL_COMMON_H
#pragma dxc enable_16bit_types

#if defined(VULKAN)
    #define PLATFORM_VULKAN 1
#else
    #define PLATFORM_VULKAN 0
#endif

#ifndef FLOAT16_SUPPORTED
    #if PLATFORM_VULKAN
        #define FLOAT16_SUPPORTED 1
    #elif defined(__SHADER_TARGET_MAJOR) && (__SHADER_TARGET_MAJOR >= 6) && (__SHADER_TARGET_MINOR >= 2)
        #define FLOAT16_SUPPORTED 1
    #else
        #define FLOAT16_SUPPORTED 0
    #endif
#endif

#ifndef INT16_SUPPORTED
    #if PLATFORM_VULKAN
        #define INT16_SUPPORTED 1
    #elif defined(__SHADER_TARGET_MAJOR) && (__SHADER_TARGET_MAJOR >= 6) && (__SHADER_TARGET_MINOR >= 2)
        #define INT16_SUPPORTED 1
    #else
        #define INT16_SUPPORTED 0
    #endif
#endif

// these are supported on mobile and 9070xt and above on amd, but not nvidia rtx5000 series
#define FLOAT16_IO_SUPPORTED 0
#define INT16_IO_SUPPORTED   0

#define USE_16BIT_TYPES (INT16_SUPPORTED && FLOAT16_SUPPORTED)

#ifdef __10X__ 
    #define in
    #define out
    #define inout
	#define globallycoherent 
#endif

typedef float f32;
typedef int   s32;
typedef uint  u32;
typedef float4 v128f;
typedef int4   v128i;
typedef uint4  v128u;
typedef float3x3 mat3x3;
typedef float4x4 mat4x4;

#if INT16_SUPPORTED
    typedef int16_t   s16;
    typedef uint16_t  u16;
    typedef int16_t2  s16x2;
    typedef int16_t3  s16x3;
    typedef int16_t4  s16x4;
    typedef uint16_t2 u16x2;
    typedef uint16_t3 u16x3;
    typedef uint16_t4 u16x4;
#else
    typedef int   s16;
    typedef uint  u16;
    typedef int2  s16x2;
    typedef int3  s16x3;
    typedef int4  s16x4;
    typedef uint2 u16x2;
    typedef uint3 u16x3;
    typedef uint4 u16x4;
#endif

#if FLOAT16_SUPPORTED && !PLATFORM_VULKAN
    typedef float16_t    f16;
    typedef float16_t2   f16_2;
    typedef float16_t3   f16_3;
    typedef float16_t4   f16_4;
    typedef float16_t2x4 f16_2x4;
    typedef float16_t3x3 f16_3x3;
    typedef float16_t3x4 f16_3x4;
    typedef float16_t4x4 f16_4x4;
    typedef float16_t4x3 f16_4x3;
#elif FLOAT16_SUPPORTED
    typedef half    f16;
    typedef half2   f16_2;
    typedef half3   f16_3;
    typedef half4   f16_4;
    typedef half2x4 f16_2x4;
    typedef half3x3 f16_3x3;
    typedef half3x4 f16_3x4;
    typedef half4x4 f16_4x4;
    typedef half4x3 f16_4x3;
#elif !PLATFORM_VULKAN && !defined(__DXC_VERSION_MAJOR)
    typedef min16float    f16;
    typedef min16float2   f16_2;
    typedef min16float3   f16_3;
    typedef min16float4   f16_4;
    typedef min16float2x4 f16_2x4;
    typedef min16float3x3 f16_3x3;
    typedef min16float3x4 f16_3x4;
    typedef min16float4x4 f16_4x4;
    typedef min16float4x3 f16_4x3;
#else
    typedef float    f16;
    typedef float2   f16_2;
    typedef float3   f16_3;
    typedef float4   f16_4;
    typedef float2x4 f16_2x4;
    typedef float3x3 f16_3x3;
    typedef float3x4 f16_3x4;
    typedef float4x4 f16_4x4;
    typedef float4x3 f16_4x3;
#endif

#if FLOAT16_IO_SUPPORTED
    typedef f16    f16_io;
    typedef fp16_2 f16_2_io;
    typedef fp16_3 f16_3_io;
    typedef fp16_4 f16_4_io;
#else
    typedef float  f16_io;
    typedef float2 f16_2_io;
    typedef float3 f16_3_io;
    typedef float4 f16_4_io;
#endif

#if INT16_IO_SUPPORTED
    typedef s16   s16_io;
    typedef u16   u16_io;
    typedef s16x2 s16x2_io;
    typedef u16x2 u16x2_io;
    typedef s16x4 s16x4_io;
    typedef u16x4 u16x4_io;
#else
    typedef int   s16_io;
    typedef uint  u16_io;
    typedef int2  s16x2_io;
    typedef uint2 u16x2_io;
    typedef int4  s16x4_io;
    typedef uint4 u16x4_io;
#endif


#define VecLerp(a, b, t)      lerp(a, b, t)
                             
#define MMIN(a, b)            min(a, b)
#define MMAX(a, b)            max(a, b)
                             
#define MCLAMP(x, mn, mx)     clamp(x, mn, mx)
#define MCLAMP01(x)           saturate(x)
                             
#define Clamp01f32(x)         saturate(x)

#define Clampf32(x, min, max) clamp(x, min, max)
#define Clamps32(x, min, max) clamp(x, min, max)

#define Minf32(a, b)          min(a, b) 
#define Maxf32(a, b)          max(a, b) 
#define Mins32(a, b)          min(a, b) 
#define Maxs32(a, b)          max(a, b)

#define Absi32(x)             abs(x)
#define Absf32(x)             abs(x)
#define Floorf(x)             floor(x)
#define Ceilf(x)              ceil(x)
#define Fractf(a)             frac(a)
#define Fract(a)              frac(a)
#define Signf(x)              sign(x)

#define M44Transpose(m) transpose(m)
#define VecXY(v) v.xy
#define VecZW(v) v.zw
#define VecCombine(a, b) f16_4(a, b)

#if defined(__HLSL_VERSION)
// -----------------------------------------------------------------------------
// Float vectors
// -----------------------------------------------------------------------------
#define VecZero()               float4(0.0, 0.0, 0.0, 0.0)
#define VecNegZero()            asfloat(uint4(0x80000000u, 0x80000000u, 0x80000000u, 0x80000000u))
#define VecOne()                float4(1.0, 1.0, 1.0, 1.0)
#define VecNegativeOne()        float4(-1.0, -1.0, -1.0, -1.0)
#define VecSet1(x)              float4((x), (x), (x), (x))
#define VecSetBytes(x)          uint4((x), (x), (x), (x))

#define VecSet(x, y, z, w)      float4((x), (y), (z), (w))
#define VecSetR(x, y, z, w)     float4((x), (y), (z), (w))

#define MakeShuffleMask(x,y,z,w)  0

#define VecSplatX(v)            ((v).xxxx)
#define VecSplatY(v)            ((v).yyyy)
#define VecSplatZ(v)            ((v).zzzz)
#define VecSplatW(v)            ((v).wwww)

#define VecGetX(v)              ((v).x)
#define VecGetY(v)              ((v).y)
#define VecGetZ(v)              ((v).z)
#define VecGetW(v)              ((v).w)

#define VecSetX(v, x)           ((v).x = (x))
#define VecSetY(v, y)           ((v).y = (y))
#define VecSetZ(v, z)           ((v).z = (z))
#define VecSetW(v, w)           ((v).w = (w))

// Arithmetic
#define VecAdd(a, b)            ((a) + (b))
#define VecSub(a, b)            ((a) - (b))
#define VecMul(a, b)            ((a) * (b))
#define VecDiv(a, b)            ((a) / (b))

#define VecAddf(a, b)           ((a) + b)
#define VecSubf(a, b)           ((a) - b)
#define VecMulf(a, b)           ((a) * b)
#define VecDivf(a, b)           ((a) / b)

#define VecRound(v)             round(v)
#define VecFmaddLane(a, b, c, l) ((a) * VecSet1(_VEC4_GET(b, l)) + (c))
#define VecFmadd(a, b, c)       ((a) * (b) + (c))
#define VecFmsub(a, b, c)       ((a) * (b) - (c))
#define VecNegMulSub(a, b, c)   ((c) - ((a) * (b)))
#define VecHadd(a, b)           float4((a).x + (a).y, (a).z + (a).w, (b).x + (b).y, (b).z + (b).w)

#define VecNeg(a)               (-(a))
#define VecRcp(a)               rcp(a)
#define VecSqrt(a)              sqrt(a)
#define VecRSqrt(a)             rsqrt(a)

// Vector Math
#define VecDot(a, b)            VecSet1(dot((a), (b)))
#define VecDotf(a, b)           dot((a), (b))
#define VecNorm(v)              ((v) * rsqrt(dot((v), (v))))
#define VecNormEst(v)           ((v) * rsqrt(dot((v), (v))))
#define VecLenf(v)              sqrt(dot((v), (v)))
#define VecLen(v)               VecSet1(sqrt(dot((v), (v))))
#define VecLenSq(v)             VecSet1(dot((v), (v)))

#define Vec3DotV(a, b)          VecSet1(dot((a).xyz, (b).xyz))
#define Vec3DotfV(a, b)         dot((a).xyz, (b).xyz)
#define Vec3NormV(v)            normalize((v).xyz)
#define Vec3NormEstV(v)         float4(((v).xyz * rsqrt(dot((v).xyz, (v).xyz))), (v).w)
#define Vec3LenfV(v)            sqrt(dot((v).xyz, (v).xyz))
#define Vec3LenV(v)             VecSet1(sqrt(dot((v).xyz, (v).xyz)))

// Swizzling / shuffling
#define VecRev(v)                       v.wzyy
#define VecSwapPairs(v)                 v.yxww
#define VecSwapHalves(v)                v.zwxx

// Logical / bitwise
#define VecNot(a)                       asfloat(~asuint(a))
#define VecAnd(a, b)                    asfloat(asuint(a) & asuint(b))
#define VecAndNot(a, b)                 asfloat((~asuint(a)) & asuint(b))
#define VecOr(a, b)                     asfloat(asuint(a) | asuint(b))
#define VecXor(a, b)                    asfloat(asuint(a) ^ asuint(b))
#define VecMask(a, msk)                 asfloat(asuint(a) & asuint(msk))

#define VecMax(a, b)                    max((a), (b))
#define VecMin(a, b)                    min((a), (b))
#define VecFloor(a)                     floor(a)

// -----------------------------------------------------------------------------
// Integer vectors
// -----------------------------------------------------------------------------
#define VeciZero()                      int4(0, 0, 0, 0)
#define VeciSet1(x)                     int4((x), (x), (x), (x))
#define VeciSet(x, y, z, w)             int4((x), (y), (z), (w))
#define VeciSetR(x, y, z, w)            int4((x), (y), (z), (w))
#define VeciDup64(x)                    int2((x), (x))   // no real 64-bit SIMD lane model in HLSL

#define VeciSetX(v, x)                  ((v).x = (x))
#define VeciSetY(v, y)                  ((v).y = (y))
#define VeciSetZ(v, z)                  ((v).z = (z))
#define VeciSetW(v, w)                  ((v).w = (w))

#define VeciSelect1111                  int4(-1, -1, -1, -1)

#define VecIdentityR0                   float4(1.0, 0.0, 0.0, 0.0)
#define VecIdentityR1                   float4(0.0, 1.0, 0.0, 0.0)
#define VecIdentityR2                   float4(0.0, 0.0, 1.0, 0.0)
#define VecIdentityR3                   float4(0.0, 0.0, 0.0, 1.0)

#define VeciAdd(a, b)                   ((a) + (b))
#define VeciSub(a, b)                   ((a) - (b))
#define VeciMul(a, b)                   ((a) * (b))

#define VeciNot(a)                      (~(a))
#define VeciAnd(a, b)                   ((a) & (b))
#define VeciOr(a, b)                    ((a) | (b))
#define VeciXor(a, b)                   ((a) ^ (b))

#define VeciAndNot(a, b)                ((~(a)) & (b))
#define VeciSrl(a, b)                   ((uint4(a) >> (b)))
#define VeciSll(a, b)                   ((uint4(a) << (b)))
#define VeciSrl32(a, b)                 ((uint4(a) >> (b)))
#define VeciSll32(a, b)                 ((uint4(a) << (b)))

#define VecFabs(x)                      VecAnd(x, VecFromInt1(0x7fffffff))

#define VecFromInt(x, y, z, w)          asfloat(uint4((x), (y), (z), (w)))
#define VecFromInt1(x)                  asfloat(uint4((x), (x), (x), (x)))
#define VecToInt(x)                     x

#define VecBitcastU32(x)                asuint(x)
#define VeciBitcastF32(x)               asfloat(x)

#define VecF32ToI32(x)                  int4(round(x))
#define VecF32ToU32(x)                  uint4(x)
#define VecI32ToF32(x)                  float4(x)
#define VecU32ToF32(x)                  float4(x)

#define VecZipLo32(a, b)                int4((a).x, (b).x, (a).y, (b).y)
#define VecZipLo16(a, b)                int4((a).x, (b).x, (a).y, (b).y)
#define VecZipHi16(a, b)                int4((a).z, (b).z, (a).w, (b).w)

#define VecUnpackLo32(x)                int4((x).x, (x).y, 0, 0)
#define VecUnpackHi32(x)                int4((x).z, (x).w, 0, 0)

#define VecPack16(x)                    uint4(saturate(float4(x)))   // approximate
#endif

#define F3Add(a, b)     ((a) + (b))
#define F3Sub(a, b)     ((a) - (b))
#define F3Mul(a, b)     ((a) * (b))
#define F3Div(a, b)     ((a) / (b))
#define F2Add(a, b)     ((a) + (b))
#define F2Sub(a, b)     ((a) - (b))
#define F2Mul(a, b)     ((a) * (b))
#define F2Div(a, b)     ((a) / (b))
#define F3AddF(a, b)    ((a) + (b))
#define F3SubF(a, b)    ((a) - (b))
#define F3MulF(a, b)    ((a) * (b))
#define F3DivF(a, b)    ((a) / (b))
#define F2AddF(a, b)    ((a) + (b))
#define F2SubF(a, b)    ((a) - (b))
#define F2MulF(a, b)    ((a) * (b))
#define F2DivF(a, b)    ((a) / (b))
#define F2Neg(a)        -(a)
#define F3Neg(a)        -(a)
#define I3Add(a, b)     ((a) + (b))
#define I3Sub(a, b)     ((a) - (b))
#define I3Mul(a, b)     ((a) * (b))
#define I3Div(a, b)     ((a) / (b))
#define I2Add(a, b)     ((a) + (b))
#define I2Sub(a, b)     ((a) - (b))
#define I2Mul(a, b)     ((a) * (b))
#define I2Div(a, b)     ((a) / (b))
#define I3AddI(a, b)    ((a) + (b))
#define I3SubI(a, b)    ((a) - (b))
#define I3MulI(a, b)    ((a) * (b))
#define I3DivI(a, b)    ((a) / (b))
#define I2AddI(a, b)    ((a) + (b))
#define I2SubI(a, b)    ((a) - (b))
#define I2MulI(a, b)    ((a) * (b))
#define I2DivI(a, b)    ((a) / (b))
#define I3Neg(a)        -(a)
#define I2Neg(a)        -(a)
#define F2Len(a)        length(a)
#define F3Len(a)        length(a)
#define I2Len(a)        length(a)
#define I3Len(a)        length(a)
#define I2LenSq(a)      length(a)
#define F2LenSq(a)      length(a)
#define F2Dist(a, b)    distance(a, b)
#define I2Dist(a, b)    distance(a, b)
#define I3Dist(a, b)    distance(a, b)
#define F3Zero()        float3( 0.0f,  0.0f,  0.0f)
#define F3One()         float3( 1.0f,  1.0f,  1.0f)
#define F3Up()          float3( 0.0f,  1.0f,  0.0f)
#define F3Left()        float3(-1.0f,  0.0f,  0.0f)
#define F3Down()        float3( 0.0f, -1.0f,  0.0f)
#define F3Right()       float3( 1.0f,  0.0f,  0.0f)
#define F3Forward()     float3( 0.0f,  0.0f,  1.0f)
#define F3Backward()    float3( 0.0f,  0.0f, -1.0f)
#define Vec3Get(v)      v.xyz
#define F3Dot(a, b)     dot(a, b)
#define F3Cross(a, b)   cross(a, b)
#define F3Lerp(a, b, t) lerp(a, b, t)
#define F2Lerp(a, b, t) lerp(a, b, t)
#define F3Norm(a)       normalize(a)
#define F3Abs(a)        abs(a)

float3 F3NormSafe(float3 a) {
    float lenSqr = dot(a, a);
    return a * rsqrt(max(lenSqr, 1e-8f));
}

float F3DistSqr(float3 a, float3 b) {
    float3 d = a - b;
    return dot(d, d);
}

float F3Angle(float3 a, float3 b) {
    float d = dot(F3NormSafe(a), F3NormSafe(b));
    return acos(clamp(d, -1.0f, 1.0f));
}

float F3Dist(float3 a, float3 b) {
    return sqrt(F3DistSqr(a, b));
}

float3 F3NormEst(float3 a) {
    return a * rsqrt(dot(a, a));
}

float3 F3Proj(float3 v, float3 n) {
    return n * (dot(v, n) / max(dot(n, n), 1e-8f));
}

float3 F3Reflect(float3 i, float3 n) {
    return i - 2.0f * F3Proj(i, n);
}

#endif // HLSL_COMMON_HN_HON_H
