
#ifndef Vector_H
#define Vector_H

#include "Math.h"

typedef struct Vec2_  { f1 x, y;           } f2;
typedef struct Vec3f_ { f1 x, y, z;        } f3;
typedef struct Int2_  { s32 x, y;           } i2;
typedef struct Int3_  { s32 x, y, z;        } i3;
typedef struct Ray_   { f3 origin, dir;    } Ray;
typedef struct RayV_  { v128f origin, dir; } RayV;

typedef const f3* cf3;
purefn f3 F3Add  (f3 a, f3 b)  { return (f3) { a.x + b.x, a.y + b.y, a.z + b.z }; }
purefn i3 I3Add  (i3 a, i3 b)  { return (i3) { a.x + b.x, a.y + b.y, a.z + b.z }; }
purefn f3 F3Sub  (f3 a, f3 b)  { return (f3) { a.x - b.x, a.y - b.y, a.z - b.z }; }
purefn i3 I3Sub  (i3 a, i3 b)  { return (i3) { a.x - b.x, a.y - b.y, a.z - b.z }; }
purefn f3 F3Mul  (f3 a, f3 b)  { return (f3) { a.x * b.x, a.y * b.y, a.z * b.z }; }
purefn i3 I3Mul  (i3 a, i3 b)  { return (i3) { a.x * b.x, a.y * b.y, a.z * b.z }; }
purefn f3 F3Div  (f3 a, f3 b)  { return (f3) { a.x / b.x, a.y / b.y, a.z / b.z }; }
purefn i3 I3Div  (i3 a, i3 b)  { return (i3) { a.x / b.x, a.y / b.y, a.z / b.z }; }
purefn i3 I3AddI (i3 a, s32 b) { return (i3) { a.x + b, a.y + b, a.z + b }; }
purefn f3 F3AddF (f3 a, f1 b)  { return (f3) { a.x + b, a.y + b, a.z + b }; }
purefn f3 F3SubF (f3 a, f1 b)  { return (f3) { a.x - b, a.y - b, a.z - b }; }
purefn i3 I3SubI (i3 a, s32 b) { return (i3) { a.x - b, a.y - b, a.z - b }; }
purefn f3 F3MulF (f3 a, f1 b)  { return (f3) { a.x * b, a.y * b, a.z * b }; }
purefn i3 I3MulI (i3 a, s32 b) { return (i3) { a.x * b, a.y * b, a.z * b }; }
purefn f3 F3DivF (f3 a, f1 b)  { return (f3) { a.x / b, a.y / b, a.z / b }; }
purefn i3 I3DivI (i3 a, s32 b) { return (i3) { a.x / b, a.y / b, a.z / b }; }
purefn f2 F2Add  (f2 a, f2 b)  { return (f2) { a.x + b.x, a.y + b.y }; }
purefn i2 I2Add  (i2 a, i2 b)  { return (i2) { a.x + b.x, a.y + b.y }; }
purefn i2 I2Sub  (i2 a, i2 b)  { return (i2) { a.x - b.x, a.y - b.y }; }
purefn f2 F2Sub  (f2 a, f2 b)  { return (f2) { a.x - b.x, a.y - b.y }; }
purefn i2 I2Mul  (i2 a, i2 b)  { return (i2) { a.x * b.x, a.y * b.y }; }
purefn f2 F2Mul  (f2 a, f2 b)  { return (f2) { a.x * b.x, a.y * b.y }; }
purefn i2 I2Div  (i2 a, i2 b)  { return (i2) { a.x / b.x, a.y / b.y }; }
purefn f2 F2Div  (f2 a, f2 b)  { return (f2) { a.x / b.x, a.y / b.y }; }
purefn f2 F2AddF (f2 a, f1 b)  { return (f2) { a.x + b, a.y + b }; }
purefn i2 I2AddI (i2 a, s32 b) { return (i2) { a.x + b, a.y + b }; }
purefn f2 F2SubF (f2 a, f1 b)  { return (f2) { a.x - b, a.y - b }; }
purefn i2 I2SubI (i2 a, s32 b) { return (i2) { a.x - b, a.y - b }; }
purefn f2 F2MulF (f2 a, f1 b)  { return (f2) { a.x * b, a.y * b }; }
purefn i2 I2MulI (i2 a, s32 b) { return (i2) { a.x * b, a.y * b }; }
purefn f2 F2DivF (f2 a, f1 b)  { return (f2) { a.x / b, a.y / b }; }
purefn i2 I2DivI (i2 a, s32 b) { return (i2) { a.x / b, a.y / b }; }
purefn f3 F3Neg  (f3 a)        { return (f3) { -a.x, -a.y, -a.z }; }
purefn i3 I3Neg  (i3 a)        { return (i3) { -a.x, -a.y, -a.z }; }
purefn f2 F2Neg  (f2 a)        { return (f2) { -a.x, -a.y }; }
purefn i2 I2Neg  (i2 a)        { return (i2) { -a.x, -a.y }; }
purefn f1 F2Len  (f2 a)        { return Sqrtf(a.x * a.x + a.y * a.y); }
purefn f1 F3Len  (f3 a)        { return Vec3LenfV(Vec3Load(&a.x)); }
purefn f1 I2Len  (i2 a)        { return Sqrtf(a.x * a.x + a.y * a.y); }
purefn f1 I3Len  (i3 a)        { return Sqrtf(a.x * a.x + a.y * a.y + a.z * a.z); }
purefn f1 I2LenSq(i2 a)        { return (a.x * a.x + a.y * a.y); }
purefn f1 F2LenSq(f2 a)        { return (a.x * a.x + a.y * a.y); }
purefn f1 F2Dist (f2 a, f2 b)  { return F2Len(F2Sub(a, b)); }
purefn f1 I2Dist (i2 a, i2 b)  { return I2Len(I2Sub(a, b)); }
purefn f1 I3Dist (i3 a, i3 b)  { return I3Len(I3Sub(a, b)); }
purefn f3 F3FromPtr(f1* ptr)   { return (f3){ ptr[0], ptr[1], ptr[2] }; }
purefn f3 F3Zero()             { return (f3){ 0.0f,  0.0f,  0.0f}; }
purefn f3 F3One()              { return (f3){ 1.0f,  1.0f,  1.0f}; }
purefn f3 F3Up()               { return (f3){ 0.0f,  1.0f,  0.0f}; }
purefn f3 F3Left()             { return (f3){-1.0f,  0.0f,  0.0f}; }
purefn f3 F3Down()             { return (f3){ 0.0f, -1.0f,  0.0f}; }
purefn f3 F3Right()            { return (f3){ 1.0f,  0.0f,  0.0f}; }
purefn f3 F3Forward()          { return (f3){ 0.0f,  0.0f,  1.0f}; }
purefn f3 F3Backward()         { return (f3){ 0.0f,  0.0f, -1.0f}; }

purefn f3 VCALL Vec3Get(f4 v)            { return (f3) { VecGetX(v), VecGetY(v), VecGetZ(v) }; }
purefn f1 F3Dot     (f3 a, f3 b)         { return VecDotf(Vec3Load(&a.x), Vec3Load(&b.x));  }
purefn f3 F3NormSafe(f3 a)               { return F3DivF(a, MATH_Epsilon + Sqrtf(a.x * a.x + a.y * a.y + a.z * a.z)); }
purefn f1 F3DistSqr (cf3 a, cf3 b)       { return Vec3DistSqrfV(Vec3Load(&a->x), Vec3Load(&b->x));           }
purefn f1 F3Angle   (cf3 a, cf3 b)       { return Vec3Angle(Vec3Load(&a->x), Vec3Load(&b->x));              }
purefn f1 F3Dist    (cf3 a, cf3 b)       { return Vec3DistfV(Vec3Load(&a->x), Vec3Load(&b->x));              }
purefn f3 F3Norm    (f3 a)               { return Vec3Get(Vec3NormV(Vec3Load(&a.x)));                        }
purefn f3 F3NormEst (f3 a)               { return Vec3Get(Vec3NormEstV(Vec3Load(&a.x)));                     }
purefn f3 F3Proj    (cf3 v, cf3 n)       { return Vec3Get(Vec3Proj   (Vec3Load(&v->x), Vec3Load(&n->x)));    }
purefn f3 F3Reflect (cf3 i, cf3 n)       { return Vec3Get(Vec3Reflect(Vec3Load(&i->x), Vec3Load(&n->x)));    }
purefn f3 F3Cross   (cf3 a, cf3 b)       { return Vec3Get(Vec3Cross  (Vec3Load(&a->x), Vec3Load(&b->x)));    }
purefn f3 F3Lerp    (cf3 a, cf3 b, f1 t) { return Vec3Get(VecLerp    (Vec3Load(&a->x), Vec3Load(&b->x), t)); }
purefn f2 F2Lerp    (f2 a, f2 b, f1 t)   { return (f2) { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t }; }

purefn f2 F2Rotate(f2 vec, f1 angle) {
    f1 s = Sin(angle), c = Cos(angle);
    return (f2){vec.x * c - vec.y * s, vec.x * s + vec.y * c};
}

purefn f2 Tof22(i2 vec)  { return (f2){(f1)vec.x,(f1)vec.y }; }
purefn i2 ToInt2(f2 vec) { return (i2){(s32)vec.x,(s32)vec.y }; }

purefn u64 PointBoxIntersection(f2 min, f2 max, f2 point) {
    return point.x <= max.x && point.y <= max.y && point.x >= min.x && point.y >= min.y;
}

purefn u64 RectPointIntersect(f2 min, f2 scale, f2 point) {
    f2 max = { min.x + scale.x, min.y + scale.y };
    return point.x <= max.x && point.y <= max.y && point.x >= min.x && point.y >= min.y;
}

purefn s32 GreaterThan2(f2 a, f2 b) { return (s32)(a.x > b.x) | ((s32)(a.y > b.y) << 1); }
purefn s32 LessThan2(f2 a, f2 b)    { return (s32)(a.x < b.x) | ((s32)(a.y < b.y) << 1); }
purefn s32 GreaterThan3(f3 a, f3 b) { return (s32)(a.x > b.x) | ((s32)(a.y > b.y) << 1) | ((s32)(a.y > b.y) << 2); }
purefn s32 LessThan3(f3 a, f3 b)    { return (s32)(a.x < b.x) | ((s32)(a.y < b.y) << 1) | ((s32)(a.y < b.y) << 2); }

purefn s32 All2(s32 msk) { return msk == 0b11u; }
purefn s32 All3(s32 msk) { return msk == 0b111u; }

purefn s32 Any2(s32 msk) { return msk > 0; }
purefn s32 Any3(s32 msk) { return msk > 0; }

#endif //Vector.h