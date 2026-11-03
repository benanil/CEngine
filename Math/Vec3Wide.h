#ifndef WIDE_VECTOR3
#define WIDE_VECTOR3
#include "Vector.h"

typedef struct iVec3W_ {
    v128u x, y, z;
} iVec3W;

typedef struct Vec3W_ {
    v128f x, y, z;
} Vec3W;

purefn Vec3W Vec3WFromF(f32   x, f32   y, f32   z) { return (Vec3W) { VecSet1(x), VecSet1(y), VecSet1(z) }; }
purefn Vec3W Vec3WFromV(v128f x, v128f y, v128f z) { return (Vec3W) { x, y, z }; }

purefn Vec3W Vec3WFromVTranspose(v128f x, v128f y, v128f z, v128f w)
{
    Vec3W res;
    v128f t0 = VecUnpackLo(x, z);
    v128f t1 = VecUnpackLo(y, w);
    v128f t2 = VecUnpackHi(x, z);
    v128f t3 = VecUnpackHi(y, w);
    res.x = VecUnpackLo(t0, t1); 
    res.y = VecUnpackHi(t0, t1);
    res.z = VecUnpackLo(t2, t3);
    return res; 
}

purefn iVec3W iVec3WZero() { return (iVec3W) { VeciZero(), VeciZero(), VeciZero() }; }
purefn Vec3W  Vec3WZero()  { return  (Vec3W) {  VecZero(),  VecZero(),  VecZero() }; }

purefn iVec3W VCALL iVec3WAdd(iVec3W  a, iVec3W b) { return (iVec3W) { VeciAdd (a.x, b.x), VeciAdd (a.y, b.y), VeciAdd (a.z, b.z) }; }
purefn iVec3W VCALL iVec3WAddv(iVec3W a, v128u b)  { return (iVec3W) { VeciAdd (a.x, b),   VeciAdd (a.y, b),   VeciAdd (a.z, b)   }; }
purefn iVec3W VCALL iVec3WAdd3(iVec3W a, int3 b)   { return (iVec3W) { VeciAddi(a.x, b.x), VeciAddi(a.y, b.y), VeciAddi(a.z, b.z) }; }
purefn iVec3W VCALL iVec3WAddi(iVec3W a, s32 b)    { return (iVec3W) { VeciAddi(a.x, b),   VeciAddi(a.y, b),   VeciAddi(a.z, b)   }; }

purefn iVec3W VCALL iVec3WSub(iVec3W  a, iVec3W b) { return (iVec3W) { VeciSub (a.x, b.x), VeciSub (a.y, b.y), VeciSub (a.z, b.z) }; }
purefn iVec3W VCALL iVec3WSubv(iVec3W a, v128u b)  { return (iVec3W) { VeciSub (a.x, b),   VeciSub (a.y, b),   VeciSub (a.z, b)   }; }
purefn iVec3W VCALL iVec3WSub3(iVec3W a, int3 b)   { return (iVec3W) { VeciSubi(a.x, b.x), VeciSubi(a.y, b.y), VeciSubi(a.z, b.z) }; }
purefn iVec3W VCALL iVec3WSubi(iVec3W a, s32 b)    { return (iVec3W) { VeciSubi(a.x, b),   VeciSubi(a.y, b),   VeciSubi(a.z, b)   }; }

purefn iVec3W VCALL iVec3WMul(iVec3W  a, iVec3W b) { return (iVec3W) { VeciMul (a.x, b.x), VeciMul (a.y, b.y), VeciMul (a.z, b.z) }; }
purefn iVec3W VCALL iVec3WMulv(iVec3W a, v128u b)  { return (iVec3W) { VeciMul (a.x, b),   VeciMul (a.y, b),   VeciMul (a.z, b)   }; }
purefn iVec3W VCALL iVec3WMul3(iVec3W a, int3 b)   { return (iVec3W) { VeciMuli(a.x, b.x), VeciMuli(a.y, b.y), VeciMuli(a.z, b.z) }; }
purefn iVec3W VCALL iVec3WMuli(iVec3W a, s32 b)    { return (iVec3W) { VeciMuli(a.x, b),   VeciMuli(a.y, b),   VeciMuli(a.z, b)   }; }

purefn iVec3W VCALL iNeg3W(iVec3W a) { return (iVec3W) { VeciNeg(a.x), VeciNeg(a.y), VeciNeg(a.z) }; }

purefn iVec3W VCALL iAnd3W(iVec3W a, iVec3W b) { return (iVec3W) { VeciAnd(a.x, b.x), VeciAnd(a.y, b.y), VeciAnd(a.z, b.z) }; }
purefn iVec3W VCALL iOr3W(iVec3W a, iVec3W b)  { return (iVec3W) { VeciOr(a.x, b.x),  VeciOr(a.y, b.y),  VeciOr(a.z, b.z)  }; }
purefn iVec3W VCALL iXor3W(iVec3W a, iVec3W b) { return (iVec3W) { VeciXor(a.x, b.x), VeciXor(a.y, b.y), VeciXor(a.z, b.z) }; }

purefn Vec3W VCALL Vec3WAdd(Vec3W  a, Vec3W b)  { return (Vec3W) { VecAdd (a.x, b.x), VecAdd (a.y, b.y), VecAdd (a.z, b.z) }; }
purefn Vec3W VCALL Vec3WAddv(Vec3W a, v128f b)  { return (Vec3W) { VecAdd (a.x, b),   VecAdd (a.y, b),   VecAdd (a.z, b)   }; }
purefn Vec3W VCALL Vec3WAdd3(Vec3W a, float3 b) { return (Vec3W) { VecAddf(a.x, b.x), VecAddf(a.y, b.y), VecAddf(a.z, b.z) }; }
purefn Vec3W VCALL Vec3WAddf(Vec3W a, f32 b)    { return (Vec3W) { VecAddf(a.x, b),   VecAddf(a.y, b),   VecAddf(a.z, b)   }; }

purefn Vec3W VCALL Vec3WSub(Vec3W  a, Vec3W b)  { return (Vec3W) { VecSub (a.x, b.x), VecSub (a.y, b.y), VecSub (a.z, b.z) }; }
purefn Vec3W VCALL Vec3WSubv(Vec3W a, v128f b)  { return (Vec3W) { VecSub (a.x, b),   VecSub (a.y, b),   VecSub (a.z, b)   }; }
purefn Vec3W VCALL Vec3WSub3(Vec3W a, float3 b) { return (Vec3W) { VecSubf(a.x, b.x), VecSubf(a.y, b.y), VecSubf(a.z, b.z) }; }
purefn Vec3W VCALL Vec3WSubf(Vec3W a, f32 b)    { return (Vec3W) { VecSubf(a.x, b),   VecSubf(a.y, b),   VecSubf(a.z, b)   }; }

purefn Vec3W VCALL Vec3WMul(Vec3W  a, Vec3W b)  { return (Vec3W) { VecMul (a.x, b.x), VecMul (a.y, b.y), VecMul (a.z, b.z) }; }
purefn Vec3W VCALL Vec3WMulv(Vec3W a, v128f b)  { return (Vec3W) { VecMul (a.x, b),   VecMul (a.y, b),   VecMul (a.z, b)   }; }
purefn Vec3W VCALL Vec3WMul3(Vec3W a, float3 b) { return (Vec3W) { VecMulf(a.x, b.x), VecMulf(a.y, b.y), VecMulf(a.z, b.z) }; }
purefn Vec3W VCALL Vec3WMulf(Vec3W a, f32 b)    { return (Vec3W) { VecMulf(a.x, b),   VecMulf(a.y, b),   VecMulf(a.z, b)   }; }

purefn Vec3W VCALL Vec3WDiv(Vec3W  a, Vec3W b)  { return (Vec3W) { VecDiv (a.x, b.x), VecDiv (a.y, b.y), VecDiv (a.z, b.z) }; }
purefn Vec3W VCALL Vec3WDivv(Vec3W a, v128f b)  { return (Vec3W) { VecDiv (a.x, b),   VecDiv (a.y, b),   VecDiv (a.z, b)   }; }
purefn Vec3W VCALL Vec3WDiv3(Vec3W a, float3 b) { return (Vec3W) { VecDivf(a.x, b.x), VecDivf(a.y, b.y), VecDivf(a.z, b.z) }; }
purefn Vec3W VCALL Vec3WDivf(Vec3W a, f32 b)    { return (Vec3W) { VecDivf(a.x, b),   VecDivf(a.y, b),   VecDivf(a.z, b)   }; }

purefn Vec3W VCALL Min3W(Vec3W a, Vec3W b) { return (Vec3W) { VecMin(a.x, b.x), VecMin(a.y, b.y), VecMin(a.z, b.z) }; }
purefn Vec3W VCALL Max3W(Vec3W a, Vec3W b) { return (Vec3W) { VecMax(a.x, b.x), VecMax(a.y, b.y), VecMax(a.z, b.z) }; }

purefn Vec3W VCALL Round3W(Vec3W a) { return (Vec3W) { VecRound(a.x), VecRound(a.y), VecRound(a.z) }; }
purefn Vec3W VCALL Ceil3W(Vec3W a)  { return (Vec3W) { VecCeil(a.x),  VecCeil(a.y),  VecCeil(a.z)  }; }
purefn Vec3W VCALL Floor3W(Vec3W a) { return (Vec3W) { VecFloor(a.x), VecFloor(a.y), VecFloor(a.z) }; }
purefn Vec3W VCALL Fract3W(Vec3W a) { return (Vec3W) { VecFract(a.x), VecFract(a.y), VecFract(a.z) }; }
purefn Vec3W VCALL Neg3W(Vec3W a)   { return (Vec3W) { VecNeg(a.x),   VecNeg(a.y),   VecNeg(a.z)   }; }
purefn Vec3W VCALL Rcp3W(Vec3W a)   { return (Vec3W) { VecRcp(a.x),   VecRcp(a.y),   VecRcp(a.z)   }; }
purefn Vec3W VCALL Sqrt3W(Vec3W a)  { return (Vec3W) { VecSqrt(a.x),  VecSqrt(a.y),  VecSqrt(a.z)  }; }
purefn Vec3W VCALL RSqrt3W(Vec3W a) { return (Vec3W) { VecRSqrt(a.x), VecRSqrt(a.y), VecRSqrt(a.z) }; }

purefn v128f VCALL Vec3WDot(Vec3W a, Vec3W b) { return VecFmadd(a.x, b.x, VecFmadd(a.y, b.y, VecMul(a.z, b.z))); }
purefn v128f VCALL Vec3WSqrLen(Vec3W a)       { return VecFmadd(a.x, a.x, VecFmadd(a.y, a.y, VecMul(a.z, a.z))); }

purefn v128f VCALL Vec3WLen(Vec3W a) { return VecSqrt(Vec3WDot(a, a)); }

purefn Vec3W VCALL Vec3WNorm(Vec3W a)    { return Vec3WDivv(a, VecSqrt(Vec3WDot(a, a))); }
purefn Vec3W VCALL Vec3WNormEst(Vec3W a) { return Vec3WMulv(a, VecRSqrt(Vec3WDot(a, a))); }

purefn iVec3W VCALL ToiVec3W(Vec3W a) { return (iVec3W) { VecF32ToI32(a.x), VecF32ToI32(a.y), VecF32ToI32(a.z) }; }

#endif