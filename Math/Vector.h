
#ifndef Vector_H
#define Vector_H

#include "Math.h"
#include "Half.h"

typedef struct Vec2_  { f32 x, y;           } fv2;
typedef struct Vec3f_ { f32 x, y, z;        } fv3;
typedef struct Int2_  { s32 x, y;           } iv2;
typedef struct Int3_  { s32 x, y, z;        } iv3;
typedef struct Ray_   { fv3 origin, dir;    } Ray;
typedef struct RayV_  { v128f origin, dir; } RayV;

typedef const fv3* cf3;
purefn fv3 F3Add  (fv3 a, fv3 b)  { return (fv3) { a.x + b.x, a.y + b.y, a.z + b.z }; }
purefn iv3 I3Add  (iv3 a, iv3 b)  { return (iv3) { a.x + b.x, a.y + b.y, a.z + b.z }; }
purefn fv3 F3Sub  (fv3 a, fv3 b)  { return (fv3) { a.x - b.x, a.y - b.y, a.z - b.z }; }
purefn iv3 I3Sub  (iv3 a, iv3 b)  { return (iv3) { a.x - b.x, a.y - b.y, a.z - b.z }; }
purefn fv3 F3Mul  (fv3 a, fv3 b)  { return (fv3) { a.x * b.x, a.y * b.y, a.z * b.z }; }
purefn iv3 I3Mul  (iv3 a, iv3 b)  { return (iv3) { a.x * b.x, a.y * b.y, a.z * b.z }; }
purefn fv3 F3Div  (fv3 a, fv3 b)  { return (fv3) { a.x / b.x, a.y / b.y, a.z / b.z }; }
purefn iv3 I3Div  (iv3 a, iv3 b)  { return (iv3) { a.x / b.x, a.y / b.y, a.z / b.z }; }
purefn iv3 I3AddI (iv3 a, s32 b)  { return (iv3) { a.x + b, a.y + b, a.z + b }; }
purefn fv3 F3AddF (fv3 a, f32 b)  { return (fv3) { a.x + b, a.y + b, a.z + b }; }
purefn fv3 F3SubF (fv3 a, f32 b)  { return (fv3) { a.x - b, a.y - b, a.z - b }; }
purefn iv3 I3SubI (iv3 a, s32 b)  { return (iv3) { a.x - b, a.y - b, a.z - b }; }
purefn fv3 F3MulF (fv3 a, f32 b)  { return (fv3) { a.x * b, a.y * b, a.z * b }; }
purefn iv3 I3MulI (iv3 a, s32 b)  { return (iv3) { a.x * b, a.y * b, a.z * b }; }
purefn fv3 F3DivF (fv3 a, f32 b)  { return (fv3) { a.x / b, a.y / b, a.z / b }; }
purefn iv3 I3DivI (iv3 a, s32 b)  { return (iv3) { a.x / b, a.y / b, a.z / b }; }
purefn fv2 F2Add  (fv2 a, fv2 b)  { return (fv2) { a.x + b.x, a.y + b.y }; }
purefn iv2 I2Add  (iv2 a, iv2 b)  { return (iv2) { a.x + b.x, a.y + b.y }; }
purefn iv2 I2Sub  (iv2 a, iv2 b)  { return (iv2) { a.x - b.x, a.y - b.y }; }
purefn fv2 F2Sub  (fv2 a, fv2 b)  { return (fv2) { a.x - b.x, a.y - b.y }; }
purefn iv2 I2Mul  (iv2 a, iv2 b)  { return (iv2) { a.x * b.x, a.y * b.y }; }
purefn fv2 F2Mul  (fv2 a, fv2 b)  { return (fv2) { a.x * b.x, a.y * b.y }; }
purefn iv2 I2Div  (iv2 a, iv2 b)  { return (iv2) { a.x / b.x, a.y / b.y }; }
purefn fv2 F2Div  (fv2 a, fv2 b)  { return (fv2) { a.x / b.x, a.y / b.y }; }
purefn fv2 F2AddF (fv2 a, f32 b)  { return (fv2) { a.x + b, a.y + b }; }
purefn iv2 I2AddI (iv2 a, s32 b)  { return (iv2) { a.x + b, a.y + b }; }
purefn fv2 F2SubF (fv2 a, f32 b)  { return (fv2) { a.x - b, a.y - b }; }
purefn iv2 I2SubI (iv2 a, s32 b)  { return (iv2) { a.x - b, a.y - b }; }
purefn fv2 F2MulF (fv2 a, f32 b)  { return (fv2) { a.x * b, a.y * b }; }
purefn iv2 I2MulI (iv2 a, s32 b)  { return (iv2) { a.x * b, a.y * b }; }
purefn fv2 F2DivF (fv2 a, f32 b)  { return (fv2) { a.x / b, a.y / b }; }
purefn iv2 I2DivI (iv2 a, s32 b)  { return (iv2) { a.x / b, a.y / b }; }
purefn fv3 F3Neg  (fv3 a)         { return (fv3) { -a.x, -a.y, -a.z }; }
purefn iv3 I3Neg  (iv3 a)         { return (iv3) { -a.x, -a.y, -a.z }; }
purefn fv2 F2Neg  (fv2 a)         { return (fv2) { -a.x, -a.y }; }
purefn iv2 I2Neg  (iv2 a)         { return (iv2) { -a.x, -a.y }; }
purefn f32 F2Len  (fv2 a)         { return Sqrtf(a.x * a.x + a.y * a.y); }
purefn f32 F3Len  (fv3 a)         { return Vec3LenfV(Vec3Load(&a.x)); }
purefn f32 I2Len  (iv2 a)         { return Sqrtf(a.x * a.x + a.y * a.y); }
purefn f32 I3Len  (iv3 a)         { return Sqrtf(a.x * a.x + a.y * a.y + a.z * a.z); }
purefn f32 I2LenSq(iv2 a)         { return (a.x * a.x + a.y * a.y); }
purefn f32 F2LenSq(fv2 a)         { return (a.x * a.x + a.y * a.y); }
purefn f32 F2Dist (fv2 a, fv2 b)  { return F2Len(F2Sub(a, b)); }
purefn f32 I2Dist (iv2 a, iv2 b)  { return I2Len(I2Sub(a, b)); }
purefn f32 I3Dist (iv3 a, iv3 b)  { return I3Len(I3Sub(a, b)); }
purefn fv3 F3FromPtr(f32* ptr)    { return (fv3){ ptr[0], ptr[1], ptr[2] }; }
purefn fv3 F3Zero()               { return (fv3){ 0.0f,  0.0f,  0.0f}; }
purefn fv3 F3One()                { return (fv3){ 1.0f,  1.0f,  1.0f}; }
purefn fv3 F3Up()                 { return (fv3){ 0.0f,  1.0f,  0.0f}; }
purefn fv3 F3Left()               { return (fv3){-1.0f,  0.0f,  0.0f}; }
purefn fv3 F3Down()               { return (fv3){ 0.0f, -1.0f,  0.0f}; }
purefn fv3 F3Right()              { return (fv3){ 1.0f,  0.0f,  0.0f}; }
purefn fv3 F3Forward()            { return (fv3){ 0.0f,  0.0f,  1.0f}; }
purefn fv3 F3Backward()           { return (fv3){ 0.0f,  0.0f, -1.0f}; }

purefn fv3 VCALL Vec3Get(f4 v)             { return (fv3) { VecGetX(v), VecGetY(v), VecGetZ(v) }; }
purefn f32 F3Dot     (fv3 a, fv3 b)        { return VecDotf(Vec3Load(&a.x), Vec3Load(&b.x));  }
purefn fv3 F3NormSafe(fv3 a)               { return F3DivF(a, MATH_Epsilon + Sqrtf(a.x * a.x + a.y * a.y + a.z * a.z)); }
purefn f32 F3DistSqr (cf3 a, cf3 b)        { return Vec3DistSqrfV(Vec3Load(&a->x), Vec3Load(&b->x));           }
purefn f32 F3Angle   (cf3 a, cf3 b)        { return Vec3Angle(Vec3Load(&a->x), Vec3Load(&b->x));              }
purefn f32 F3Dist    (cf3 a, cf3 b)        { return Vec3DistfV(Vec3Load(&a->x), Vec3Load(&b->x));              }
purefn fv3 F3Norm    (fv3 a)               { return Vec3Get(Vec3NormV(Vec3Load(&a.x)));                        }
purefn fv3 F3NormEst (fv3 a)               { return Vec3Get(Vec3NormEstV(Vec3Load(&a.x)));                     }
purefn fv3 F3Proj    (cf3 v, cf3 n)        { return Vec3Get(Vec3Proj   (Vec3Load(&v->x), Vec3Load(&n->x)));    }
purefn fv3 F3Reflect (cf3 i, cf3 n)        { return Vec3Get(Vec3Reflect(Vec3Load(&i->x), Vec3Load(&n->x)));    }
purefn fv3 F3Cross   (cf3 a, cf3 b)        { return Vec3Get(Vec3Cross  (Vec3Load(&a->x), Vec3Load(&b->x)));    }
purefn fv3 F3Lerp    (cf3 a, cf3 b, f32 t) { return Vec3Get(VecLerp    (Vec3Load(&a->x), Vec3Load(&b->x), t)); }
purefn fv2 F2Lerp    (fv2 a, fv2 b, f32 t) { return (fv2) { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t }; }

purefn fv2 F2Rotate(fv2 vec, f32 angle) {
    f32 s = Sin(angle), c = Cos(angle);
    return (fv2){vec.x * c - vec.y * s, vec.x * s + vec.y * c};
}

purefn fv2 Tof22(iv2 vec)  { return (fv2){(f32)vec.x,(f32)vec.y }; }
purefn iv2 ToInt2(fv2 vec) { return (iv2){(s32)vec.x,(s32)vec.y }; }

purefn u64 PointBoxIntersection(fv2 min, fv2 max, fv2 point) {
    return point.x <= max.x && point.y <= max.y && point.x >= min.x && point.y >= min.y;
}

purefn u64 RectPointIntersect(fv2 min, fv2 scale, fv2 point) {
    fv2 max = { min.x + scale.x, min.y + scale.y };
    return point.x <= max.x && point.y <= max.y && point.x >= min.x && point.y >= min.y;
}

purefn s32 GreaterThan2(fv2 a, fv2 b) { return (s32)(a.x > b.x) | ((s32)(a.y > b.y) << 1); }
purefn s32 LessThan2(fv2 a, fv2 b)    { return (s32)(a.x < b.x) | ((s32)(a.y < b.y) << 1); }
purefn s32 GreaterThan3(fv3 a, fv3 b) { return (s32)(a.x > b.x) | ((s32)(a.y > b.y) << 1) | ((s32)(a.y > b.y) << 2); }
purefn s32 LessThan3(fv3 a, fv3 b)    { return (s32)(a.x < b.x) | ((s32)(a.y < b.y) << 1) | ((s32)(a.y < b.y) << 2); }

purefn s32 All2(s32 msk) { return msk == 0b11u; }
purefn s32 All3(s32 msk) { return msk == 0b111u; }

purefn s32 Any2(s32 msk) { return msk > 0; }
purefn s32 Any3(s32 msk) { return msk > 0; }

typedef fv2   f16_2;
typedef v128f f16_4;

static inline u32 PackHalf2(fv2 v)
{
    return Float2ToHalf2(&v.x);
}

static inline f16_2 UnpackHalf2(u32 h)
{
    f16_2 res;
    Half2ToFloat2(&res.x, h);
    return res;
}

static inline f16_2 VecXY(v128f v)
{
    f16_2 res={};
    VecLoadLo64(&res.x, v);
    return res;
}

static inline f16_2 VecZW(v128f v)
{
    f16_2 res={};
    VecLoadHi64(&res.x, v);
    return res;
}

static inline f16_4 VecCombine(fv2 a, fv2 b)
{
    return VecSetR(a.x, a.y, b.x, b.y);
}

#endif //Vector.h