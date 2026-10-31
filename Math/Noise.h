
#ifndef Noise_H
#define Noise_H

// Procedural noise helpers, moved out of Vector.h. These are the C ports of the
// GLSL "webgl-noise" routines (Stefan Gustavson / Ian McEwan, MIT). The original
// paste targeted HLSL/GLSL (vector swizzles, float4, operator overloads) which do
// not exist in C, so the vectorized bodies are expanded to scalar / engine-vector
// form here. Only the pieces the engine actually needs are kept: the permutation
// polynomial and 2-D cellular (Worley) noise. The 4-D simplex helpers (grad4 /
// rgrad2) were dropped because they require a float4 type the math library has no
// equivalent for and nothing consumes them.

#include "Vec2Wide.h"
#include "SOAMath.h"

// Modulo 289 / 7 without a division, matches the GLSL mod289 / mod7 used to keep
// the permutation polynomial from overflowing single precision.
purefn f32 NoiseMod289(f32 x) { return x - Floorf32(x * (1.0f / 289.0f)) * 289.0f; }
purefn f32 NoiseMod7  (f32 x) { return x - Floorf32(x * (1.0f / 7.0f))   * 7.0f; }

// Permutation polynomial (34x^2 + x) mod 289, the shared hash of the noise family.
purefn f32 NoisePermute(f32 x) { return NoiseMod289((34.0f * x + 1.0f) * x); }

// Vectorized permutation helpers, same math as NoiseMod289/NoiseMod7/NoisePermute
// but operating on 4 lanes at once (one lane per independent sample point).
purefn v128f VCALL NoiseMod289V(v128f x) {
    return VecSub(x, VecMulf(VecFloor(VecMulf(x, 1.0f / 289.0f)), 289.0f));
}

purefn v128f VCALL NoiseMod7V(v128f x) {
    return VecSub(x, VecMulf(VecFloor(VecMulf(x, 1.0f / 7.0f)), 7.0f));
}

purefn v128f VCALL NoisePermuteV(v128f x) {
    return NoiseMod289V(VecMul(VecAddf(VecMulf(x, 34.0f), 1.0f), x));
}

purefn v128f VCALL Interpolate2W(v128f value1, v128f value2, v128f value3, v128f value4, Vec2W t) {
    return VecLerpV(VecLerpV(value1, value2, t.x), VecLerpV(value3, value4, t.x), t.y);
}

purefn Vec2W Fade(Vec2W t) {
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

static inline v128f PerlinNoiseOctav(Vec2W position, v128u seed) {
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

    return Interpolate2W(value1, value2, value3, value4, Fade(fractPosition));
}

static inline v128f PerlinNoise1DOctav(v128f position, v128u seed) {
    v128f floorPosition = VecFloor(position);
    v128f t = VecSub(position, floorPosition);
    v128u c = VecF32ToU32(floorPosition);
    v128u h0 = Hash1D(c, seed);
    v128u h1 = Hash1D(VeciAddi(c, 1), seed);
    v128f value0 = GradSigned1D(h0, t);
    v128f value1 = GradSigned1D(h1, VecSub(t, VecOne()));
    return VecLerpV(value0, value1, Fade1D(t));
}

// 2-D cellular (Worley) noise over the 3x3 neighbourhood of feature points. Returns
// the distance to the nearest (x = F1) and second nearest (y = F2) feature point,
// both in [0, ~1.5]. Direct port of Gustavson's cellular(): the three columns and
// rows are unrolled and the nine candidate distances reduced to the two smallest.
purefn float2 NoiseCellular2D(float2 P)
{
    const f32 K  = 1.0f / 7.0f; // 0.142857
    const f32 Ko = 3.0f / 7.0f; // 0.428571 (grid mean offset)
    const f32 jitter = 1.0f;    // lower gives a more regular pattern

    float2 Pi = (float2){ NoiseMod289(Floorf32(P.x)), NoiseMod289(Floorf32(P.y)) };
    float2 Pf = (float2){ Fractf32(P.x), Fractf32(P.y) };

    const f32 oi[3] = { -1.0f, 0.0f, 1.0f }; // integer cell neighbour offsets
    const f32 of[3] = { -0.5f, 0.5f, 1.5f }; // row distance offsets
    const f32 colDx[3] = { 0.5f, -0.5f, -1.5f }; // column distance offsets

    f32 f1 = 1.0e30f, f2 = 1.0e30f;
    for (s32 i = 0; i < 3; i++)
    {
        f32 px = NoisePermute(Pi.x + oi[i]);
        for (s32 r = 0; r < 3; r++)
        {
            f32 p  = NoisePermute(px + Pi.y + oi[r]);
            f32 ox = Fractf32(p * K) - Ko;
            f32 oy = NoiseMod7(Floorf32(p * K)) * K - Ko;
            f32 dx = Pf.x + colDx[i] + jitter * ox;
            f32 dy = Pf.y - of[r]    + jitter * oy;
            f32 d  = dx * dx + dy * dy;
            if (d < f1)      { f2 = f1; f1 = d; }
            else if (d < f2) { f2 = d; }
        }
    }
    return (float2){ Sqrtf(f1), Sqrtf(f2) };
}

// 4-wide 2-D cellular (Worley) noise: computes NoiseCellular2D for 4 independent
// (Px, Py) points in parallel, one per SIMD lane. *outF1 / *outF2 receive the
// nearest / second-nearest feature distances (one per lane), mirroring the
// scalar version's (x, y) return.
static inline void VCALL NoiseCellular2Dx4(v128f Px, v128f Py, v128f* outF1, v128f* outF2)
{
    const f32 K      = 1.0f / 7.0f;   // 0.142857
    const f32 Ko     = 3.0f / 7.0f;   // 0.428571 (grid mean offset)
    const f32 jitter = 1.0f;         // lower gives a more regular pattern

    v128f PiX = NoiseMod289V(VecFloor(Px));
    v128f PiY = NoiseMod289V(VecFloor(Py));
    v128f PfX = VecFract(Px);
    v128f PfY = VecFract(Py);

    const f32 oi[3]    = { -1.0f,  0.0f,  1.0f };  // integer cell neighbour offsets
    const f32 of[3]    = { -0.5f,  0.5f,  1.5f };  // row distance offsets
    const f32 colDx[3] = {  0.5f, -0.5f, -1.5f }; // column distance offsets

    v128f f1 = VecSet1(1.0e30f);
    v128f f2 = VecSet1(1.0e30f);

    for (s32 i = 0; i < 3; i++)
    {
        v128f px     = NoisePermuteV(VecAddf(PiX, oi[i]));
        v128f pxPiY  = VecAdd(px, PiY);
        for (s32 r = 0; r < 3; r++)
        {
            v128f p  = NoisePermuteV(VecAddf(pxPiY, oi[r]));
            v128f ox = VecSubf(VecFract(VecMulf(p, K)), Ko);
            v128f oy = VecSubf(VecMulf(NoiseMod7V(VecFloor(VecMulf(p, K))), K), Ko);
            v128f dx = VecAdd(VecAddf(PfX, colDx[i]), VecMulf(ox, jitter));
            v128f dy = VecAdd(VecSubf(PfY, of[r]),    VecMulf(oy, jitter));
            v128f d  = VecAdd(VecMul(dx, dx), VecMul(dy, dy));

            v128f closer  = VecCmpLt(d, f1);      // d < f1 (per lane)
            v128f second  = VecCmpLt(d, f2);      // d < f2 (per lane)
            v128f f2FromD = VecSelect(f2, d, second);  // second ? d : f2
            f2 = VecSelect(f2FromD, f1, closer);       // closer ? old-f1 : f2FromD
            f1 = VecSelect(f1, d, closer);             // closer ? d : f1
        }
    }

    *outF1 = VecSqrt(f1);
    *outF2 = VecSqrt(f2);
}

// https://www.shadertoy.com/view/l32fRW
// Generate 2D gradient vector from hash
purefn Vec2W VCALL RGrad2W(Vec2W p, v128f rot) {
    v128f u = VecAdd(VecMulf(NoisePermuteV(VecAdd(NoisePermuteV(p.x), p.y)), 0.0243902439f), rot);
    u = VecMul(VecFract(u), VecSet1(MATH_TwoPI));
    return (Vec2W){ VecCosFast(u), VecSinFast(u) };
}

// SIMPLEX NOISE CORE FUNCTION
purefn v128f VCALL PSRNoise(Vec2W pos) {
    const v128f F2 = VecSet1(0.366025403f);
    const v128f G2 = VecSet1(0.211324865f);
    const v128f half = VecSet1(0.5f);

    v128f s = VecMul(VecAdd(pos.x, pos.y), F2);
    Vec2W skewed = { VecAdd(pos.x, s), VecAdd(pos.y, s) };
    Vec2W i = Floor2W(skewed);
    Vec2W f = Vec2WSub(skewed, i);

    v128f t = VecMul(VecAdd(i.x, i.y), G2);
    Vec2W X0 = { VecSub(i.x, t), VecSub(i.y, t) };

    v128f xFirst = VecAnd(VecCmpGt(f.x, f.y), VecOne());
    Vec2W i1 = { xFirst, VecSub(VecOne(), xFirst) };

    v128f c2 = VecSub(VecOne(), VecAdd(G2, G2));
    Vec2W X1 = { VecAdd(X0.x, VecSub(i1.x, G2)), VecAdd(X0.y, VecSub(i1.y, G2)) };
    Vec2W X2 = { VecAdd(X0.x, c2), VecAdd(X0.y, c2) };

    Vec2W im = { NoiseMod289V(i.x), NoiseMod289V(i.y) };
    Vec2W g0 = RGrad2W(im, VecZero());
    Vec2W g1 = RGrad2W(Vec2WAdd(im, i1), VecZero());
    Vec2W g2 = RGrad2W(Vec2WAddf(im, 1.0f), VecZero());

    Vec2W d0 = Vec2WSub(pos, X0);
    Vec2W d1 = Vec2WSub(pos, X1);
    Vec2W d2 = Vec2WSub(pos, X2);

    v128f n0 = Vec2WDot(g0, d0);
    v128f n1 = Vec2WDot(g1, d1);
    v128f n2 = Vec2WDot(g2, d2);

    v128f w0 = VecMax(VecSub(half, Vec2WDot(d0, d0)), VecZero());
    v128f w1 = VecMax(VecSub(half, Vec2WDot(d1, d1)), VecZero());
    v128f w2 = VecMax(VecSub(half, Vec2WDot(d2, d2)), VecZero());
    w0 = VecMul(w0, w0); w0 = VecMul(w0, w0);
    w1 = VecMul(w1, w1); w1 = VecMul(w1, w1);
    w2 = VecMul(w2, w2); w2 = VecMul(w2, w2);

    v128f result = VecFmadd(w0, n0, VecFmadd(w1, n1, VecMul(w2, n2)));
    return VecMulf(result, 99.0f);
}

// https://www.shadertoy.com/view/NlSGDz
static inline v128f PerlinNoiseW(Vec2W position, int octaveCount, float persistence, float lacunarity, u32 seed) {
    v128f value = VecZero();
    v128f amplitude = VecOne();
    for (int i = 0; i < octaveCount; i++) {
        v128u  s = Hash1D(VeciSet1(i), VeciSet1(seed)); 
        value     = VecFmadd(PerlinNoiseOctav(position, s), amplitude, value);
        amplitude = VecMulf(amplitude, persistence);
        position  = Vec2WMulf(position, lacunarity);
    }
    return value;
}

static inline v128f PerlinNoise1DW(v128f position, int octaveCount, float persistence, float lacunarity, u32 seed) {
    v128f value = VecZero();
    v128f amplitude = VecOne();
    for (int i = 0; i < octaveCount; i++) {
        v128u s = Hash1D(VeciSet1((u32)i), VeciSet1(seed));
        value     = VecFmadd(PerlinNoise1DOctav(position, s), amplitude, value);
        amplitude = VecMulf(amplitude, persistence);
        position  = VecMulf(position, lacunarity);
    }
    return value;
}

static inline v128f SimplexNoiseW(Vec2W position, int octaveCount, f32 persistence, f32 lacunarity, u32 seed) {
    v128f value = VecZero();
    v128f amplitude = VecOne();
    for (int i = 0; i < octaveCount; i++) {
        value     = VecFmadd(PSRNoise(position), amplitude, value);
        amplitude = VecMulf(amplitude, persistence);
        position  = Vec2WMulf(position, lacunarity);
    }
    return value;
}

#endif // Noise_H
