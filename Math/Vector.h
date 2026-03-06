
#ifndef Vector_H
#define Vector_H

#include "Math.h"

typedef struct Vec3f_ {
    f1 x, y, z;
} f3;

typedef struct Int2_ {
    i32 x, y;
} i2;

typedef struct Int3_ {
    i32 x, y, z;
} i3;

typedef struct f22f_ {
    f1 x, y;
} f2;

typedef struct Ray_ {
    f3 origin, dir;
} Ray;

typedef struct RayV_ {
    v128f origin, dir;
} RayV;

typedef v128f f4;

// VECTOR32
purefn f3 F3Add(f3 a, f3 b)  { return (f3){a.x + b.x, a.y + b.y, a.z + b.z}; }
purefn f3 F3Mul(f3 a, f3 b)  { return (f3){a.x * b.x, a.y * b.y, a.z * b.z}; }
purefn f3 F3Div(f3 a, f3 b)  { return (f3){a.x / b.x, a.y / b.y, a.z / b.z}; }
purefn f3 F3Sub(f3 a, f3 b)  { return (f3){a.x - b.x, a.y - b.y, a.z - b.z}; }
purefn f3 F3AddF(f3 a, f1 b) { return (f3){a.x + b, a.y + b, a.z + b}; }
purefn f3 F3MulF(f3 a, f1 b) { return (f3){a.x * b, a.y * b, a.z * b}; }
purefn f3 F3DivF(f3 a, f1 b) { return (f3){a.x / b, a.y / b, a.z / b}; }
purefn f3 F3SubF(f3 a, f1 b) { return (f3){a.x - b, a.y - b, a.z - b}; }
purefn f3 F3Neg(f3 a)        { return (f3){-a.x, -a.y, -a.z}; }

purefn f1 F3Len(f3 a)        { return Sqrtf(a.x * a.x + a.y * a.y + a.z * a.z); }
purefn f1 F3Dot(f3 a, f3 b)  { return a.x * b.x + a.y * b.y + a.z * b.z; }

purefn f3 F3Norm(f3 a)       { return F3DivF(a, Sqrtf(a.x * a.x + a.y * a.y + a.z * a.z)); }
purefn f3 F3NormSafe(f3 a)   { return F3DivF(a, MATH_Epsilon + Sqrtf(a.x * a.x + a.y * a.y + a.z * a.z)); }
purefn f3 F3NormEst(f3 a)    { return F3MulF(a, RSqrtf(a.x * a.x + a.y * a.y + a.z * a.z)); }

purefn f1 F3Dist(f3 a, f3 b) {
    f3 diff = F3Sub(a, b);
    return Sqrtf(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z); 
}

purefn f1 F3DistSqr(f3 a, f3 b) {
    f3 diff = F3Sub(a, b);
    return (diff.x * diff.x + diff.y * diff.y + diff.z * diff.z); 
}

purefn f1 F3AngleBetween(f3 a, f3 b) {
    f1 dot = F3Dot(a, b) * RSqrtf(F3Dot(a, a) * F3Dot(b, b));
    dot = MCLAMP(dot, -1.0f, 1.0f);
    return ACos(dot);
}

purefn f3 F3Reflect(f3 in, f3 normal) {
    return F3Sub(in, F3MulF(normal, F3Dot(normal, in) * 2.0f));
}

purefn f3 F3Lerp(f3 a, f3 b, f1 t) {
    return (f3) {
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t
    };
}

purefn f3 F3Cross(f3 a, f3 b) {
    return (f3) {
        a.y * b.z - b.y * a.z,
        a.z * b.x - b.z * a.x,
        a.x * b.y - b.x * a.y
    };
}

purefn f3 F3FromPtr(f1* ptr)    { return (f3){ ptr[0], ptr[1], ptr[2] }; }

purefn f3 F3Zero()              { return (f3){ 0.0f,  0.0f,  0.0f}; }
purefn f3 F3One()               { return (f3){ 1.0f,  1.0f,  1.0f}; }
purefn f3 F3Up()                { return (f3){ 0.0f,  1.0f,  0.0f}; }
purefn f3 F3Left()              { return (f3){-1.0f,  0.0f,  0.0f}; }
purefn f3 F3Down()              { return (f3){ 0.0f, -1.0f,  0.0f}; }
purefn f3 F3Right()             { return (f3){ 1.0f,  0.0f,  0.0f}; }
purefn f3 F3Forward()           { return (f3){ 0.0f,  0.0f,  1.0f}; }
purefn f3 F3Backward()          { return (f3){ 0.0f,  0.0f, -1.0f}; }

// VECTOR3I
purefn i3 I3Add(i3 a, i3 b)     { return (i3){a.x + b.x, a.y + b.y, a.z + b.z}; }
purefn i3 I3Mul(i3 a, i3 b)     { return (i3){a.x * b.x, a.y * b.y, a.z * b.z}; }
purefn i3 I3Div(i3 a, i3 b)     { return (i3){a.x / b.x, a.y / b.y, a.z / b.z}; }
purefn i3 I3Sub(i3 a, i3 b)     { return (i3){a.x - b.x, a.y - b.y, a.z - b.z}; }
purefn i3 I3Neg(i3 a)           { return (i3){-a.x, -a.y, -a.z}; }
                                
purefn i3 I3AddI(i3 a, int b)   { return (i3){a.x + b, a.y + b, a.z + b}; }
purefn i3 I3MulI(i3 a, int b)   { return (i3){a.x * b, a.y * b, a.z * b}; }
purefn i3 I3DivI(i3 a, int b)   { return (i3){a.x / b, a.y / b, a.z / b}; }
purefn i3 I3SubI(i3 a, int b)   { return (i3){a.x - b, a.y - b, a.z - b}; }
                                
// VECTOR2I                     
purefn i2 I2Add(i2 a, i2 b)     { return (i2){a.x + b.x, a.y + b.y}; }
purefn i2 I2Mul(i2 a, i2 b)     { return (i2){a.x * b.x, a.y * b.y}; }
purefn i2 I2Div(i2 a, i2 b)     { return (i2){a.x / b.x, a.y / b.y}; }
purefn i2 I2Sub(i2 a, i2 b)     { return (i2){a.x - b.x, a.y - b.y}; }
purefn i2 I2Neg(i2 a)           { return (i2){-a.x, -a.y}; }
                                
purefn i2 I2AddI(i2 a, int b)   { return (i2){a.x + b, a.y + b}; }
purefn i2 I2MulI(i2 a, int b)   { return (i2){a.x * b, a.y * b}; }
purefn i2 I2DivI(i2 a, int b)   { return (i2){a.x / b, a.y / b}; }
purefn i2 I2SubI(i2 a, int b)   { return (i2){a.x - b, a.y - b}; }
                                
purefn f1 I2Len(i2 a)           { return Sqrtf(a.x * a.x + a.y * a.y); }
purefn f1 I2LenSqr(i2 a)        { return (a.x * a.x + a.y * a.y); }
purefn f1 I2Dist(i2 a, i2 b)    { return I2Len(I2Sub(a, b)); }

// Float2f
purefn f2 F2Add(f2 a, f2 b)     { return (f2){a.x + b.x, a.y + b.y}; }
purefn f2 F2Mul(f2 a, f2 b)     { return (f2){a.x * b.x, a.y * b.y}; }
purefn f2 F2Div(f2 a, f2 b)     { return (f2){a.x / b.x, a.y / b.y}; }
purefn f2 F2Sub(f2 a, f2 b)     { return (f2){a.x - b.x, a.y - b.y}; }
purefn f2 F2Neg(f2 a)           { return (f2){-a.x, -a.y}; }

purefn f2 F2AddF(f2 a, f1 b)    { return (f2){a.x + b, a.y + b}; }
purefn f2 F2MulF(f2 a, f1 b)    { return (f2){a.x * b, a.y * b}; }
purefn f2 F2DivF(f2 a, f1 b)    { return (f2){a.x / b, a.y / b}; }
purefn f2 F2SubF(f2 a, f1 b)    { return (f2){a.x - b, a.y - b}; }
                                
purefn f1 F2Len(f2 a)           { return Sqrtf(a.x * a.x + a.y * a.y); }
purefn f1 F2LenSqr(f2 a)        { return (a.x * a.x + a.y * a.y); }
purefn f1 F2Dist(f2 a, f2 b)    { return F2Len(F2Sub(a, b)); }

purefn f2 Lerp(f2 a, f2 b, f1 t) {
    return (f2) { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t };
}

purefn f2 F2Rotate(f2 vec, f1 angle) {
    f1 s = Sin(angle), c = Cos(angle);
    return (f2){vec.x * c - vec.y * s, vec.x * s + vec.y * c};
}

purefn f2 Tof22(i2 vec)  { return (f2){(f1)vec.x, (f1)vec.y }; }
purefn i2 ToInt2(f2 vec) { return (i2){(i32)vec.x,(i32)vec.y }; }

purefn bool PointBoxIntersection(f2 min, f2 max, f2 point)
{
    return point.x <= max.x && point.y <= max.y &&
           point.x >= min.x && point.y >= min.y;
}

purefn bool RectPointIntersect(f2 min, f2 scale, f2 point)
{
    f2 max = { min.x + scale.x, min.y + scale.y };
    return point.x <= max.x && point.y <= max.y &&
           point.x >= min.x && point.y >= min.y;
}

purefn i32 GreaterThan2(f2 a, f2 b)
{
    return (i32)(a.x > b.x) | ((i32)(a.y > b.y) << 1);
}

purefn i32 LessThan2(f2 a, f2 b)
{
    return (i32)(a.x < b.x) | ((i32)(a.y < b.y) << 1);
}

purefn i32 GreaterThan3(f3 a, f3 b)
{
    return (i32)(a.x > b.x) | ((i32)(a.y > b.y) << 1) | ((i32)(a.y > b.y) << 2);
}

purefn i32 LessThan3(f3 a, f3 b)
{
    return (i32)(a.x < b.x) | ((i32)(a.y < b.y) << 1) | ((i32)(a.y < b.y) << 2);
}

purefn i32 All2(i32 msk) { return msk == 0b11u; }
purefn i32 All3(i32 msk) { return msk == 0b111u; }

purefn i32 Any2(i32 msk) { return msk > 0; }
purefn i32 Any3(i32 msk) { return msk > 0; }

#endif //Vector.h