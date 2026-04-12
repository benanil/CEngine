#ifndef A_BITPACK
#define A_BITPACK

#include "../Include/Common.h"

purefn u32 VCALL PackXY11Z10SnormToU32(v128f v)
{
    v = VecClamp(v, VecSet1(-1.0f), VecSet1(1.0f));
    v = VecMul(v, VecSetR(1023.f, 1023.f, 511.f, 0.f));
    v = VecRound(v);
    v128u i = VecF32ToI32(v);
    i = VeciAnd(i, VeciSetR(0x7FF, 0x7FF, 0x3FF, 0));
    i = VeciSll(i, VeciSetR(0, 11, 22, 0));
    i = VeciOr(i, VecSwapHalvesU(i));
    i = VeciOr(i, VecSwapPairsU(i));
    return VeciGetX(i);
}

purefn u32 VCALL PackXY11Z10UnormToU32(v128f v)
{
    v = VecClamp01(v);
    v = VecMul(v, VecSetR(2047.f, 2047.f, 1023.f, 0.f));
    v = VecRound(v);
    v128u i = VecF32ToI32(v);
    i = VeciAnd(i, VeciSetR(0x7FF, 0x7FF, 0x3FF, 0));
    i = VeciSll(i, VeciSetR(0, 11, 22, 0));
    i = VeciOr(i, VecSwapHalvesU(i));
    i = VeciOr(i, VecSwapPairsU(i));
    return VeciGetX(i);
}

static inline void VCALL PackQuaternionS16Norm(v128f quat, u64* result)
{
    quat = VecNorm(quat);
    v128u u32 = VecF32ToI32(VecMulf(quat, INT16_MAX-1));
    VecStoreLo64(result, VecPack16(u32));
}

static inline void VCALL UnpackQuaternionS16Norm2(v128u i16, v128f* q0, v128f* q1)
{
    const v128f inv = VecSet1(1.0f / (INT16_MAX - 1));
    *q0 = VecMul(VecI32ToF32(VecUnpackLo32(i16)), inv);
    *q1 = VecMul(VecI32ToF32(VecUnpackHi32(i16)), inv);
}


#define cOneOverSqrt2 (0.70710678f)
#define cNumBits      (9)
#define c9Mask        ((1u << cNumBits) - 1)
#define c9MaxValue    (c9Mask - 1)

static inline u32 VCALL PackQuat(v128f v)
{
    static const u32 shifts[4][4] = {
        { 32,  0,  9, 18 },
        {  0, 32,  9, 18 },
        {  0,  9, 32, 18 },
        {  0,  9, 18, 32 } 
    };

    v128f cScale = VecSet1((float)(c9MaxValue) / (2.0f * cOneOverSqrt2));

    u32 maxElement = VecMaxElement(VecFabs(v));
    float maxVal   = VecGetN(v, maxElement);

    u32 value = (u32)BitCast(u32, maxVal) & 0x80000000u;
    v = VecXor(v, VeciBitcastF32(VeciSet1(value)));
    value |= maxElement << 29;

    v = VecFmadd(v, cScale, VecSet1((float)c9MaxValue * 0.5f + 0.5f));
    v = VecClamp(v, VecZero(), VecSet1((float)c9MaxValue));

    v128u i = VecF32ToU32(v);
    i = VeciSll(i, VeciLoad(shifts[maxElement]));
    i = VeciOr(i, VecSwapHalvesU(i));
    i = VeciOr(i, VecSwapPairsU(i));
    return value | VeciGetX(i);
}

static inline v128f VCALL UnpackQuat(u32 inValue)
{
    static const u32 shifts[4][4] = {
        { 32,  0,  9, 18 }, 
        {  0, 32,  9, 18 }, 
        {  0,  9, 32, 18 }, 
        {  0,  9, 18, 32 }  
    };

    static const u32 signMasks[4][4] = {
        { 0x80000000u, 0, 0, 0 },
        { 0, 0x80000000u, 0, 0 },
        { 0, 0, 0x80000000u, 0 },
        { 0, 0, 0, 0x80000000u }
    };

    static const u32 laneMasks[4][4] = {
        { 0, ~0u, ~0u, ~0u },
        { ~0u, 0, ~0u, ~0u },
        { ~0u, ~0u, 0, ~0u },
        { ~0u, ~0u, ~0u, 0 }
    };

    static const u32 andMasks[4][4] = {
        { 0,       c9Mask, c9Mask, c9Mask },
        { c9Mask,  0,      c9Mask, c9Mask },
        { c9Mask,  c9Mask, 0,      c9Mask },
        { c9Mask,  c9Mask, c9Mask, 0      }
    };

    v128f cScale = VecSet1(2.0f * cOneOverSqrt2 / (float)(c9MaxValue));
    u32 maxIndex = (inValue >> 29) & 3;
    
    v128u x = VeciSet1(inValue);
    v128u s = VeciAnd(VeciSet1(inValue & 0x80000000u), VeciLoad(signMasks[maxIndex]));

    x = VeciSrl(x, VeciLoad(shifts[maxIndex]));
    x = VeciAnd(x, VeciLoad(andMasks[maxIndex]));

    v128f v = VecU32ToF32(x);
    v = VecFmsub(v, cScale, VecSet1(cOneOverSqrt2));
    v = VecAnd(v, VeciBitcastF32(VeciLoad(laneMasks[maxIndex])));

    float missing = Sqrtf(Maxf32(1.0f - VecGetX(VecDot(v, v)), 0.0f));
    v = VecSetN(v, maxIndex, missing);
    return VeciBitcastF32(VeciXor(VecBitcastU32(v), s));
}

#endif