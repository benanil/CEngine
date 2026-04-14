#ifndef A_BITPACK
#define A_BITPACK

#include "../Include/Common.h"

#define cOneOverSqrt2 (0.70710678f)
#define cNumBits      (9)
#define c9Mask        ((1u << cNumBits) - 1)
#define c9MaxValue    (c9Mask - 1)

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

static inline void PackTBNIntoQuaternion64(v128f normal, v128f tangent, u32* out)
{
    v128f binormal = Vec3Cross(tangent, normal);
    v128f quat = QuaternionFromM33Vec(binormal, tangent, normal);
    quat = VecNorm(quat);
    PackQuaternionS16Norm(quat, (u64*)out);
}

static u32 VCALL PackQuat(v128f v)
{
    static const u32 shifts[4][4] = {
        { 32,  0,  9, 18 },
        {  0, 32,  9, 18 },
        {  0,  9, 32, 18 },
        {  0,  9, 18, 32 }
    };

    v128f cScale  = VecSet1((float)c9MaxValue / (2.0f * cOneOverSqrt2));
    v128f absV    = VecFabs(v);
    u32 maxElement = VecMaxElement(absV);

    v128u signBits = VeciAnd(VecBitcastU32(v), VeciSet1(0x80000000u));
    u32 maxSign    = (u32)((u32*)&signBits)[maxElement & 3];
    v128u flipMask = VeciSet1(maxSign);
    v = VecXor(v, VeciBitcastF32(flipMask));

    u32 value = maxSign | (maxElement << 29);

    v = VecFmadd(v, cScale, VecSet1((float)c9MaxValue * 0.5f + 0.5f));
    v = VecClamp(v, VecZero(), VecSet1((float)c9MaxValue));

    v128u i = VecF32ToU32(v);
    i = VeciSll(i, VeciLoad(shifts[maxElement]));
    i = VeciOr(i, VecSwapHalvesU(i));
    i = VeciOr(i, VecSwapPairsU(i));
    return value | VeciGetX(i);
}

static v128f VCALL UnpackQuat(u32 inValue)
{
    v128f cScale  = VecSet1(2.0f * cOneOverSqrt2 / (float)c9MaxValue);
    v128f cOffset = VecSet1(cOneOverSqrt2);

    u32 maxIndex = (inValue >> 29) & 3;
    u32 signBit  = inValue & 0x80000000u;

    v128u raw     = VeciSet1(inValue);
    v128u laneIdx = VeciSetR(0, 1, 2, 3);
    v128u maxLane = VeciSet1(maxIndex);
    v128u isMax   = VeciCmpEq(laneIdx, maxLane);

    v128u gt     = VeciCmpGt(laneIdx, maxLane);
    v128u order  = VeciSub(laneIdx, VeciAnd(gt, VeciSet1(1u)));
    v128u shAmts = VeciMul(order, VeciSet1(9u));
    shAmts       = VeciBlend(shAmts, VeciSet1(0u), isMax);

    v128u extracted = VeciSrl(raw, shAmts);
    extracted       = VeciAnd(extracted, VeciSet1(c9Mask));
    extracted       = VeciAndNot(isMax, extracted);

    v128f v = VecU32ToF32(extracted);
    v       = VecFmsub(v, cScale, cOffset);
    v       = VecAndNot(VeciBitcastF32(isMax), v);

    v128f sq4     = VecDot(v, v);
    v128f clamped = VecMax(VecSub(VecOne(), sq4), VecZero());
    v128f missing = VecSqrt(clamped);
    v128f blended = VecBlend(v, missing, VeciBitcastF32(isMax));
    return VecXor(blended, VecFromInt1((s32)signBit));
}

#endif