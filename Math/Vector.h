
#ifndef Vector_H
#define Vector_H

#include "Math.h"

typedef struct Vec3f_ {
    float x, y, z;
} float3;

typedef struct Int2_ {
    int x, y;
} int2;

typedef struct Int3_ {
    int x, y, z;
} int3;

typedef struct Float2f_ {
    float x, y;
} float2;

typedef struct Ray_ {
    float3 origin, dir;
} Ray;

typedef struct RayV_ {
    Vec4x32f origin, dir;
} RayV;

typedef Vec4x32f float4;

// VECTOR32
purefn float3 F3Add(float3 a, float3 b) { return (float3){a.x + b.x, a.y + b.y, a.z + b.z}; }
purefn float3 F3Mul(float3 a, float3 b) { return (float3){a.x * b.x, a.y * b.y, a.z * b.z}; }
purefn float3 F3Div(float3 a, float3 b) { return (float3){a.x / b.x, a.y / b.y, a.z / b.z}; }
purefn float3 F3Sub(float3 a, float3 b) { return (float3){a.x - b.x, a.y - b.y, a.z - b.z}; }
purefn float3 F3AddF(float3 a, float b) { return (float3){a.x + b, a.y + b, a.z + b}; }
purefn float3 F3MulF(float3 a, float b) { return (float3){a.x * b, a.y * b, a.z * b}; }
purefn float3 F3DivF(float3 a, float b) { return (float3){a.x / b, a.y / b, a.z / b}; }
purefn float3 F3SubF(float3 a, float b) { return (float3){a.x - b, a.y - b, a.z - b}; }
purefn float3 F3Neg(float3 a) { return (float3){-a.x, -a.y, -a.z}; }

purefn float F3Len(float3 a)           { return Sqrtf(a.x * a.x + a.y * a.y + a.z * a.z); }
purefn float F3Dot(float3 a, float3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

purefn float3 F3Norm(float3 a)    { return F3DivF(a, Sqrtf(a.x * a.x + a.y * a.y + a.z * a.z)); }
purefn float3 F3NormSafe(float3 a){ return F3DivF(a, MATH_Epsilon + Sqrtf(a.x * a.x + a.y * a.y + a.z * a.z)); }
purefn float3 F3NormEst(float3 a) { return F3MulF(a, RSqrtf(a.x * a.x + a.y * a.y + a.z * a.z)); }

purefn float F3Dist(float3 a, float3 b) {
    float3 diff = F3Sub(a, b);
    return Sqrtf(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z); 
}

purefn float F3DistSqr(float3 a, float3 b) {
    float3 diff = F3Sub(a, b);
    return (diff.x * diff.x + diff.y * diff.y + diff.z * diff.z); 
}

purefn float F3AngleBetween(float3 a, float3 b) {
    float dot = F3Dot(a, b) * RSqrtf(F3Dot(a, a) * F3Dot(b, b));
    dot = MCLAMP(dot, -1.0f, 1.0f);
    return ACos(dot);
}

purefn float3 F3Reflect(float3 in, float3 normal) {
    return F3Sub(in, F3MulF(normal, F3Dot(normal, in) * 2.0f));
}

purefn float3 F3Lerp(float3 a, float3 b, float t) {
    return (float3) {
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t
    };
}

purefn float3 F3Cross(float3 a, float3 b) {
    return (float3) {
        a.y * b.z - b.y * a.z,
        a.z * b.x - b.z * a.x,
        a.x * b.y - b.x * a.y
    };
}

purefn float3 F3FromPtr(float* ptr) { return (float3){ ptr[0], ptr[1], ptr[2] }; }

purefn float3 F3Zero()     { return (float3){ 0.0f,  0.0f,  0.0f}; }
purefn float3 F3One()      { return (float3){ 1.0f,  1.0f,  1.0f}; }
purefn float3 F3Up()       { return (float3){ 0.0f,  1.0f,  0.0f}; }
purefn float3 F3Left()     { return (float3){-1.0f,  0.0f,  0.0f}; }
purefn float3 F3Down()     { return (float3){ 0.0f, -1.0f,  0.0f}; }
purefn float3 F3Right()    { return (float3){ 1.0f,  0.0f,  0.0f}; }
purefn float3 F3Forward()  { return (float3){ 0.0f,  0.0f,  1.0f}; }
purefn float3 F3Backward() { return (float3){ 0.0f,  0.0f, -1.0f}; }

// VECTOR3I
purefn int3 Int3Add(int3 a, int3 b) { return (int3){a.x + b.x, a.y + b.y, a.z + b.z}; }
purefn int3 Int3Mul(int3 a, int3 b) { return (int3){a.x * b.x, a.y * b.y, a.z * b.z}; }
purefn int3 Int3Div(int3 a, int3 b) { return (int3){a.x / b.x, a.y / b.y, a.z / b.z}; }
purefn int3 Int3Sub(int3 a, int3 b) { return (int3){a.x - b.x, a.y - b.y, a.z - b.z}; }
purefn int3 Int3Neg(int3 a)         { return (int3){-a.x, -a.y, -a.z}; }

purefn int3 Int3AddI(int3 a, int b) { return (int3){a.x + b, a.y + b, a.z + b}; }
purefn int3 Int3MulI(int3 a, int b) { return (int3){a.x * b, a.y * b, a.z * b}; }
purefn int3 Int3DivI(int3 a, int b) { return (int3){a.x / b, a.y / b, a.z / b}; }
purefn int3 Int3SubI(int3 a, int b) { return (int3){a.x - b, a.y - b, a.z - b}; }

// VECTOR2I
purefn int2 Int2Add(int2 a, int2 b) { return (int2){a.x + b.x, a.y + b.y}; }
purefn int2 Int2Mul(int2 a, int2 b) { return (int2){a.x * b.x, a.y * b.y}; }
purefn int2 Int2Div(int2 a, int2 b) { return (int2){a.x / b.x, a.y / b.y}; }
purefn int2 Int2Sub(int2 a, int2 b) { return (int2){a.x - b.x, a.y - b.y}; }
purefn int2 Int2Neg(int2 a)         { return (int2){-a.x, -a.y}; }

purefn int2 Int2AddI(int2 a, int b) { return (int2){a.x + b, a.y + b}; }
purefn int2 Int2MulI(int2 a, int b) { return (int2){a.x * b, a.y * b}; }
purefn int2 Int2DivI(int2 a, int b) { return (int2){a.x / b, a.y / b}; }
purefn int2 Int2SubI(int2 a, int b) { return (int2){a.x - b, a.y - b}; }

purefn float Int2Len(int2 a)    { return Sqrtf(a.x * a.x + a.y * a.y); }
purefn float Int2LenSqr(int2 a) { return (a.x * a.x + a.y * a.y); }
purefn float Int2Dist(int2 a, int2 b) { return Int2Len(Int2Sub(a, b)); }

// Float2f
purefn float2 F2Add(float2 a, float2 b) { return (float2){a.x + b.x, a.y + b.y}; }
purefn float2 F2Mul(float2 a, float2 b) { return (float2){a.x * b.x, a.y * b.y}; }
purefn float2 F2Div(float2 a, float2 b) { return (float2){a.x / b.x, a.y / b.y}; }
purefn float2 F2Sub(float2 a, float2 b) { return (float2){a.x - b.x, a.y - b.y}; }
purefn float2 F2Neg(float2 a)           { return (float2){-a.x, -a.y}; }

purefn float2 F2AddF(float2 a, float b) { return (float2){a.x + b, a.y + b}; }
purefn float2 F2MulF(float2 a, float b) { return (float2){a.x * b, a.y * b}; }
purefn float2 F2DivF(float2 a, float b) { return (float2){a.x / b, a.y / b}; }
purefn float2 F2SubF(float2 a, float b) { return (float2){a.x - b, a.y - b}; }

purefn float F2Len(float2 a)            { return Sqrtf(a.x * a.x + a.y * a.y); }
purefn float F2LenSqr(float2 a)         { return (a.x * a.x + a.y * a.y); }
purefn float F2Dist(float2 a, float2 b) { return F2Len(F2Sub(a, b)); }

purefn float2 Lerp(float2 a, float2 b, float t) {
    return (float2) { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t };
}

purefn float2 F2Rotate(float2 vec, float angle) {
    float s = Sin(angle), c = Cos(angle);
    return (float2){vec.x * c - vec.y * s, vec.x * s + vec.y * c};
}

purefn float2 ToFloat2(int2 vec) { return (float2){(float)vec.x, (float)vec.y }; }
purefn int2 ToInt2(float2 vec)   { return   (int2){  (int)vec.x,   (int)vec.y }; }

purefn bool PointBoxIntersection(float2 min, float2 max, float2 point)
{
    return point.x <= max.x && point.y <= max.y &&
           point.x >= min.x && point.y >= min.y;
}

purefn bool RectPointIntersect(float2 min, float2 scale, float2 point)
{
    float2 max = { min.x + scale.x, min.y + scale.y };
    return point.x <= max.x && point.y <= max.y &&
           point.x >= min.x && point.y >= min.y;
}

purefn uint32_t GreaterThan2(float2 a, float2 b)
{
    return (uint32_t)(a.x > b.x) | ((uint32_t)(a.y > b.y) << 1);
}

purefn uint32_t LessThan2(float2 a, float2 b)
{
    return (uint32_t)(a.x < b.x) | ((uint32_t)(a.y < b.y) << 1);
}

purefn uint32_t GreaterThan3(float3 a, float3 b)
{
    return (uint32_t)(a.x > b.x) | ((uint32_t)(a.y > b.y) << 1) | ((uint32_t)(a.y > b.y) << 2);
}

purefn uint32_t LessThan3(float3 a, float3 b)
{
    return (uint32_t)(a.x < b.x) | ((uint32_t)(a.y < b.y) << 1) | ((uint32_t)(a.y < b.y) << 2);
}

purefn uint32_t All2(uint32_t msk) { return msk == 0b11u; }
purefn uint32_t All3(uint32_t msk) { return msk == 0b111u; }

purefn uint32_t Any2(uint32_t msk) { return msk > 0; }
purefn uint32_t Any3(uint32_t msk) { return msk > 0; }

#endif //Vector.h