#ifndef PERLIN_NOISE
#define PERLIN_NOISE
// adopted from shadertoy, s32eger hash for better uniformity and no fp32 problems
// https://www.shadertoy.com/view/slB3z3
// https://www.shadertoy.com/view/NlSGDz
#include "Vec2Wide.h"
#include "Vec3Wide.h"

purefn v128f VCALL Interpolate3W(v128f v000, v128f v100, v128f v010, v128f v110,
                                 v128f v001, v128f v101, v128f v011, v128f v111, Vec3W t) {
    v128f a = VecLerpV(VecLerpV(v000, v100, t.x), VecLerpV(v010, v110, t.x), t.y);
    v128f b = VecLerpV(VecLerpV(v001, v101, t.x), VecLerpV(v011, v111, t.x), t.y);
    return VecLerpV(a, b, t.z);
}

purefn v128f VCALL Interpolate2W(v128f value1, v128f value2, v128f value3, v128f value4, Vec2W t) {
    return VecLerpV(VecLerpV(value1, value2, t.x), VecLerpV(value3, value4, t.x), t.y);
}

purefn Vec3W Fade3W(Vec3W t) {
    return Vec3WMul(t, Vec3WMul(t, Vec3WMul(t,
                Vec3WAddf(Vec3WMul(t, Vec3WSubf(Vec3WMulf(t, 6.0f), 15.0f)), 10.0f))));
}

purefn Vec2W Fade2W(Vec2W t) {
    // 6t^5 - 15t^4 + 10t^3
    return Vec2WMul(t, Vec2WMul(t, Vec2WMul(t,
                        Vec2WAddf(Vec2WMul(t, Vec2WSubf(Vec2WMulf(t, 6.0f), 15.0f)), 10.0f))));
}

purefn v128f VCALL Fade1D(v128f t) {
    v128f a = VecAddf(VecMulf(t, 6.0f), -15.0f);
    v128f b = VecAddf(VecMul(t, a), 10.0f);
    return VecMul(VecMul(t, VecMul(t, t)), b);
}
  
purefn v128u VCALL HashKey(v128u c) {
    v128u k = VeciMuli(c, 0x27d4eb2fu);
    k = VeciXor(k, VeciSrl32(k, 16));
    return VeciMuli(k, 0x85ebca77u);
}

purefn v128u VCALL HashPartial(v128u kx, v128u seed) {
    v128u h = VeciXor(seed, kx);
    h = VeciXor(h, VeciSrl32(h, 16));
    return VeciMuli(h, 0x9e3779b1u);
}

purefn v128u VCALL HashFinish(v128u h, v128u ky) {
    h = VeciXor(h, ky);
    h = VeciXor(h, VeciSrl32(h, 16));
    h = VeciMuli(h, 0x9e3779b1u);
    h = VeciXor(h, VeciSrl32(h, 16));
    h = VeciMuli(h, 0xed5ad4bbu);
    return VeciXor(h, VeciSrl32(h, 16));
}

purefn v128u VCALL Hash1D(v128u key, v128u seed) {
    v128u k = VeciMuli(key, 0x27d4eb2fu);
    k = VeciXor(k, VeciSrl32(k, 16));
    k = VeciMuli(k, 0x85ebca77u);
    v128u h = VeciXor(seed, k);
    h = VeciXor(h, VeciSrl32(h, 16));
    h = VeciMuli(h, 0x9e3779b1u);
    h = VeciXor(h, VeciSrl32(h, 16));
    h = VeciMuli(h, 0xed5ad4bbu);
    return VeciXor(h, VeciSrl32(h, 16));
}

purefn v128f VCALL GradSigned(v128u h, v128f dx, v128f dy) {
    v128u sx = VeciSll32(h, 31);
    v128u sy = VeciSll32(VeciAnd(h, VeciSet1(2)), 30);
    v128f gx = VeciBitcastF32(VeciXor(VecBitcastU32(dx), sx));
    v128f gy = VeciBitcastF32(VeciXor(VecBitcastU32(dy), sy));
    return VecAdd(gx, gy);
}

purefn v128f VCALL GradSigned1D(v128u h, v128f d) {
    return VeciBitcastF32(VeciXor(VecBitcastU32(d), VeciSll32(h, 31)));
}

purefn v128f VCALL GradSigned3(v128u h, v128f dx, v128f dy, v128f dz) {
    v128u one  = VeciSet1(1), zero = VeciZero();
    v128u h2 = VeciSrl32(h, 2);
    v128u h3 = VeciSrl32(h, 3);
    v128u sx = VeciSll32(h, 31);
    v128u sy = VeciSll32(VeciAnd(h, VeciSet1(2)), 30);
    v128u sz = VeciSll32(VeciXor(h, VeciSrl32(h, 1)), 31);
    v128u kx = VeciSub(zero, VeciAnd(VeciOr(h2, h3), one));
    v128u ky = VeciSub(zero, VeciAnd(VeciOr(VeciXor(h2, one), h3), one));
    v128u kz = VeciSub(zero, VeciXor(VeciAnd(h3, one), one));
    v128f gx = VeciBitcastF32(VeciAnd(VeciXor(VecBitcastU32(dx), sx), kx));
    v128f gy = VeciBitcastF32(VeciAnd(VeciXor(VecBitcastU32(dy), sy), ky));
    v128f gz = VeciBitcastF32(VeciAnd(VeciXor(VecBitcastU32(dz), sz), kz));
    return VecAdd(VecAdd(gx, gy), gz);
}
 
purefn v128f Perlin3(Vec3W position, v128u seed) {
    Vec3W floorPosition = Floor3W(position);
    Vec3W fractPosition = Vec3WSub(position, floorPosition);
    iVec3W c = ToiVec3W(floorPosition);
 
    v128u hx0 = HashPartial(HashKey(c.x), seed);
    v128u hx1 = HashPartial(HashKey(VeciAddi(c.x, 1)), seed);
    v128u ky0 = HashKey(c.y);
    v128u ky1 = HashKey(VeciAddi(c.y, 1));
    v128u kz0 = HashKey(c.z);
    v128u kz1 = HashKey(VeciAddi(c.z, 1));
 
    v128u h00 = HashPartial(hx0, ky0);
    v128u h10 = HashPartial(hx1, ky0);
    v128u h01 = HashPartial(hx0, ky1);
    v128u h11 = HashPartial(hx1, ky1);
 
    v128f x0 = fractPosition.x;
    v128f y0 = fractPosition.y;
    v128f z0 = fractPosition.z;
    v128f x1 = VecSub(x0, VecOne());
    v128f y1 = VecSub(y0, VecOne());
    v128f z1 = VecSub(z0, VecOne());
 
    v128f v000 = GradSigned3(HashFinish(h00, kz0), x0, y0, z0);
    v128f v100 = GradSigned3(HashFinish(h10, kz0), x1, y0, z0);
    v128f v010 = GradSigned3(HashFinish(h01, kz0), x0, y1, z0);
    v128f v110 = GradSigned3(HashFinish(h11, kz0), x1, y1, z0);
    v128f v001 = GradSigned3(HashFinish(h00, kz1), x0, y0, z1);
    v128f v101 = GradSigned3(HashFinish(h10, kz1), x1, y0, z1);
    v128f v011 = GradSigned3(HashFinish(h01, kz1), x0, y1, z1);
    v128f v111 = GradSigned3(HashFinish(h11, kz1), x1, y1, z1);
 
    return Interpolate3W(v000, v100, v010, v110, v001, v101, v011, v111, Fade3W(fractPosition));
}

purefn v128f Perlin2(Vec2W position, v128u seed) {
    Vec2W floorPosition = Floor2W(position);
    Vec2W fractPosition = Vec2WSub(position, floorPosition);
    iVec2W c = ToiVec2W(floorPosition);

    v128u hx0 = HashPartial(HashKey(c.x), seed);
    v128u hx1 = HashPartial(HashKey(VeciAddi(c.x, 1)), seed);
    v128u ky0 = HashKey(c.y);
    v128u ky1 = HashKey(VeciAddi(c.y, 1));

    v128f x0 = fractPosition.x;
    v128f y0 = fractPosition.y;
    v128f x1 = VecSub(x0, VecOne());
    v128f y1 = VecSub(y0, VecOne());

    v128f value1 = GradSigned(HashFinish(hx0, ky0), x0, y0);
    v128f value2 = GradSigned(HashFinish(hx1, ky0), x1, y0);
    v128f value3 = GradSigned(HashFinish(hx0, ky1), x0, y1);
    v128f value4 = GradSigned(HashFinish(hx1, ky1), x1, y1);

    return Interpolate2W(value1, value2, value3, value4, Fade2W(fractPosition));
}

purefn v128f Perlin1(v128f position, v128u seed) {
    v128f floorPosition = VecFloor(position);
    v128f t = VecSub(position, floorPosition);
    v128u c = VecF32ToU32(floorPosition);
    v128u h0 = Hash1D(c, seed);
    v128u h1 = Hash1D(VeciAddi(c, 1), seed);
    v128f value0 = GradSigned1D(h0, t);
    v128f value1 = GradSigned1D(h1, VecSub(t, VecOne()));
    return VecLerpV(value0, value1, Fade1D(t));
}

purefn v128f Perlin3FBM(Vec3W position, s32 octaveCount, f32 persistence, f32 lacunarity, u32 seed) {
    v128f value = VecZero();
    v128f amplitude = VecOne();
    for (s32 i = 0; i < octaveCount; i++) {
        v128u  s = Hash1D(VeciSet1(i), VeciSet1(seed));
        value     = VecFmadd(Perlin3(position, s), amplitude, value);
        amplitude = VecMulf(amplitude, persistence);
        position  = Vec3WMulf(position, lacunarity);
    }
    return value;
}

purefn v128f Perlin2FBM(Vec2W position, s32 octaveCount,
                        f32 persistence, f32 lacunarity, u32 seed) {
    v128f value = VecZero();
    v128f amplitude = VecOne();
    for (s32 i = 0; i < octaveCount; i++) {
        v128u  s = Hash1D(VeciSet1(i), VeciSet1(seed)); 
        value     = VecFmadd(Perlin2(position, s), amplitude, value);
        amplitude = VecMulf(amplitude, persistence);
        position  = Vec2WMulf(position, lacunarity);
    }
    return VecClampf(value, -1.0f, 1.0f);
}

purefn v128f Perlin1FBM(v128f position, s32 octaveCount, f32 persistence, f32 lacunarity, u32 seed) {
    v128f value = VecZero();
    v128f amplitude = VecOne();
    for (s32 i = 0; i < octaveCount; i++) {
        v128u s = Hash1D(VeciSet1((u32)i), VeciSet1(seed));
        value     = VecFmadd(Perlin1(position, s), amplitude, value);
        amplitude = VecMulf(amplitude, persistence);
        position  = VecMulf(position, lacunarity);
    }
    return value;
}

purefn v128f PerlinDistinct2FBM(Vec2W pos, f32 scale, u32 seed)
{
    v128f fine, coarse, absence, blend;
    v128u s4 = HashKey(VeciAdd(VeciSet1(seed), VeciSetR(87, 65, 43, 21)));

    fine    = Perlin2FBM(Vec2WMulf(pos, scale * 1.2f), 2, 0.5f, 2.0f, VeciGetX(s4));
    coarse  = Perlin2FBM(Vec2WMulf(pos, scale * 0.8f), 2, 0.5f, 2.0f, VeciGetY(s4));
    absence = Perlin2(Vec2WMulf(pos, scale * 0.15f), VeciSplatZ(s4));
    blend   = Perlin2(Vec2WMulf(pos, scale * 0.07f), VeciSplatW(s4));
    
    blend   = SmoothstepV(blend, -0.25f, 0.0f); // %60 coarse
    coarse  = Remap01V(VecLerpV(fine, coarse, blend));
    return VecMul(coarse, SmoothstepV(absence, -0.65f, -0.25f));// %20 absence
}

purefn v128f PerlinDistinct2(Vec2W pos, f32 scale, u32 seed)
{
    v128f fine, coarse, absence, blend;
    v128u s4 = HashKey(VeciAdd(VeciSet1(seed), VeciSetR(12, 34, 56, 78)));
    
    fine    = Perlin2(Vec2WMulf(pos, scale * 1.20f), VeciSplatX(s4));
    coarse  = Perlin2(Vec2WMulf(pos, scale * 0.80f), VeciSplatY(s4));
    absence = Perlin2(Vec2WMulf(pos, scale * 0.15f), VeciSplatZ(s4));
    blend   = Perlin2(Vec2WMulf(pos, scale * 0.07f), VeciSplatW(s4));

    blend   = SmoothstepV(blend, -0.25f, 0.0f); // %65 coarse
    coarse  = Remap01V(VecLerpV(fine, coarse, blend));
    return VecMul(coarse, SmoothstepV(absence, -0.65f, -0.25f)); // %20 absence
}

#endif