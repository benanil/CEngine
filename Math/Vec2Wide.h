#ifndef WIDE_VECTOR
#define WIDE_VECTOR
#include "Vector.h"
// wide vectors
typedef struct iVec2W_ {
    v128u x, y;
} iVec2W;

typedef struct Vec2W_ {
    v128f x, y;
} Vec2W;

purefn iVec2W VCALL iVec2WAdd(iVec2W  a, iVec2W b) { return (iVec2W) { VeciAdd (a.x, b.x), VeciAdd (a.y, b.y) }; }
purefn iVec2W VCALL iVec2WAddv(iVec2W a, v128u b)  { return (iVec2W) { VeciAdd (a.x, b),   VeciAdd (a.y, b)   }; }
purefn iVec2W VCALL iVec2WAdd2(iVec2W a, int2 b)   { return (iVec2W) { VeciAddi(a.x, b.x), VeciAddi(a.y, b.y) }; }
purefn iVec2W VCALL iVec2WAddi(iVec2W a, s32 b)    { return (iVec2W) { VeciAddi(a.x, b),   VeciAddi(a.y, b)   }; }

purefn iVec2W VCALL iVec2WSub(iVec2W  a, iVec2W b) { return (iVec2W) { VeciSub (a.x, b.x), VeciSub (a.y, b.y) }; }
purefn iVec2W VCALL iVec2WSubv(iVec2W a, v128u b)  { return (iVec2W) { VeciSub (a.x, b),   VeciSub (a.y, b)   }; }
purefn iVec2W VCALL iVec2WSub2(iVec2W a, int2 b)   { return (iVec2W) { VeciSubi(a.x, b.x), VeciSubi(a.y, b.y) }; }
purefn iVec2W VCALL iVec2WSubi(iVec2W a, f32 b)    { return (iVec2W) { VeciSubi(a.x, b),   VeciSubi(a.y, b)   }; }

purefn iVec2W VCALL iVec2WMul(iVec2W  a, iVec2W b) { return (iVec2W) { VeciMul (a.x, b.x), VeciMul (a.y, b.y) }; }
purefn iVec2W VCALL iVec2WMulv(iVec2W a, v128u b)  { return (iVec2W) { VeciMul (a.x, b),   VeciMul (a.y, b)   }; }
purefn iVec2W VCALL iVec2WMul2(iVec2W a, int2 b)   { return (iVec2W) { VeciMuli(a.x, b.x), VeciMuli(a.y, b.y) }; }
purefn iVec2W VCALL iVec2WMuli(iVec2W a, s32 b)    { return (iVec2W) { VeciMuli(a.x, b),   VeciMuli(a.y, b)   }; }

purefn iVec2W VCALL iNeg2W(iVec2W a) { return (iVec2W) { VeciNeg(a.x),  VeciNeg(a.y)  }; }

purefn iVec2W VCALL iAnd2W(iVec2W a, iVec2W b) { return (iVec2W) { VeciAnd(a.x, b.x), VeciAnd(a.y, b.y) }; }
purefn iVec2W VCALL iOr2W(iVec2W a, iVec2W b)  { return (iVec2W) { VeciOr(a.x, b.x),  VeciOr(a.y, b.y)  }; }
purefn iVec2W VCALL iXor2W(iVec2W a, iVec2W b) { return (iVec2W) { VeciXor(a.x, b.x), VeciXor(a.y, b.y) }; }

// f32
purefn Vec2W VCALL Vec2WAdd(Vec2W  a, Vec2W b)  { return (Vec2W) { VecAdd (a.x, b.x), VecAdd (a.y, b.y) }; }
purefn Vec2W VCALL Vec2WAddv(Vec2W a, v128f b)  { return (Vec2W) { VecAdd (a.x, b),   VecAdd (a.y, b)   }; }
purefn Vec2W VCALL Vec2WAdd2(Vec2W a, float2 b) { return (Vec2W) { VecAddf(a.x, b.x), VecAddf(a.y, b.y) }; }
purefn Vec2W VCALL Vec2WAddf(Vec2W a, f32 b)    { return (Vec2W) { VecAddf(a.x, b),   VecAddf(a.y, b)   }; }

purefn Vec2W VCALL Vec2WSub(Vec2W  a, Vec2W b)  { return (Vec2W) { VecSub (a.x, b.x), VecSub (a.y, b.y) }; }
purefn Vec2W VCALL Vec2WSubv(Vec2W a, v128f b)  { return (Vec2W) { VecSub (a.x, b),   VecSub (a.y, b)   }; }
purefn Vec2W VCALL Vec2WSub2(Vec2W a, float2 b) { return (Vec2W) { VecSubf(a.x, b.x), VecSubf(a.y, b.y) }; }
purefn Vec2W VCALL Vec2WSubf(Vec2W a, f32 b)    { return (Vec2W) { VecSubf(a.x, b),   VecSubf(a.y, b)   }; }

purefn Vec2W VCALL Vec2WMul(Vec2W  a, Vec2W b)  { return (Vec2W) { VecMul (a.x, b.x), VecMul (a.y, b.y) }; }
purefn Vec2W VCALL Vec2WMulv(Vec2W a, v128f b)  { return (Vec2W) { VecMul (a.x, b),   VecMul (a.y, b)   }; }
purefn Vec2W VCALL Vec2WMul2(Vec2W a, float2 b) { return (Vec2W) { VecMulf(a.x, b.x), VecMulf(a.y, b.y) }; }
purefn Vec2W VCALL Vec2WMulf(Vec2W a, f32 b)    { return (Vec2W) { VecMulf(a.x, b),   VecMulf(a.y, b)   }; }

purefn Vec2W VCALL Vec2WDiv(Vec2W  a, Vec2W b)  { return (Vec2W) { VecDiv (a.x, b.x), VecDiv (a.y, b.y) }; }
purefn Vec2W VCALL Vec2WDivv(Vec2W a, v128f b)  { return (Vec2W) { VecDiv (a.x, b),   VecDiv (a.y, b)   }; }
purefn Vec2W VCALL Vec2WDiv2(Vec2W a, float2 b) { return (Vec2W) { VecDivf(a.x, b.x), VecDivf(a.y, b.y) }; }
purefn Vec2W VCALL Vec2WDivf(Vec2W a, f32 b)    { return (Vec2W) { VecDivf(a.x, b),   VecDivf(a.y, b)   }; }

purefn Vec2W VCALL Min2W(Vec2W a, Vec2W b) { return (Vec2W) { VecMin(a.x, b.x), VecMin(a.y, b.y) }; }
purefn Vec2W VCALL Max2W(Vec2W a, Vec2W b) { return (Vec2W) { VecMax(a.x, b.x), VecMax(a.y, b.y) }; }

purefn Vec2W VCALL Round2W(Vec2W a) { return (Vec2W) { VecRound(a.x), VecRound(a.y) }; }
purefn Vec2W VCALL Ceil2W(Vec2W a)  { return (Vec2W) { VecCeil(a.x),  VecCeil(a.y)  }; }
purefn Vec2W VCALL Floor2W(Vec2W a) { return (Vec2W) { VecFloor(a.x), VecFloor(a.y) }; }
purefn Vec2W VCALL Fract2W(Vec2W a) { return (Vec2W) { VecFract(a.x), VecFract(a.y) }; }
purefn Vec2W VCALL Neg2W(Vec2W a)   { return (Vec2W) { VecNeg(a.x),   VecNeg(a.y)   }; }
purefn Vec2W VCALL Rcp2W(Vec2W a)   { return (Vec2W) { VecRcp(a.x),   VecRcp(a.y)   }; }
purefn Vec2W VCALL Sqrt2W(Vec2W a)  { return (Vec2W) { VecSqrt(a.x),  VecSqrt(a.y)  }; }
purefn Vec2W VCALL RSqrt2W(Vec2W a) { return (Vec2W) { VecRSqrt(a.x), VecRSqrt(a.y) }; }

purefn v128f Vec2WDot(Vec2W a, Vec2W b) { return VecFmadd(a.x, b.x, VecMul(a.y, b.y)); }

purefn v128f Vec2WLen(Vec2W a) { return VecSqrt(Vec2WDot(a, a)); }

purefn Vec2W Vec2WNorm(Vec2W a)    { return Vec2WDivv(a, VecSqrt(Vec2WDot(a, a))); }
purefn Vec2W Vec2WNormEst(Vec2W a) { return Vec2WMulv(a, VecRSqrt(Vec2WDot(a, a))); }

purefn iVec2W VCALL ToiVec2W(Vec2W a) { return (iVec2W) { VecF32ToI32(a.x), VecF32ToI32(a.y) }; }

#endif