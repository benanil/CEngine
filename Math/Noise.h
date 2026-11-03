
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
#include "Perlin.h"

// Permutation polynomial (34x^2 + x) mod 289, the shared hash of the noise family.
purefn f32 NoisePermute(f32 x) { return NoiseMod289((34.0f * x + 1.0f) * x); }

purefn v128f VCALL NoiseMod289V(v128f x) {
    return VecSub(x, VecMulf(VecFloor(VecMulf(x, 1.0f / 289.0f)), 289.0f));
}

purefn v128f VCALL NoiseMod7V(v128f x) {
    return VecSub(x, VecMulf(VecFloor(VecMulf(x, 1.0f / 7.0f)), 7.0f));
}

purefn v128f VCALL NoisePermuteV(v128f x) {
    return NoiseMod289V(VecMul(VecAddf(VecMulf(x, 34.0f), 1.0f), x));
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

static inline v128f SimplexNoiseFBM(Vec2W position, int octaveCount, f32 persistence, f32 lacunarity) {
    v128f value = VecZero();
    v128f amplitude = VecOne();
    for (int i = 0; i < octaveCount; i++) {
        value     = VecFmadd(PSRNoise(position), amplitude, value);
        amplitude = VecMulf(amplitude, persistence);
        position  = Vec2WMulf(position, lacunarity);
    }
    return value;
}

static inline Vec2W CeullularNoiseFBM(Vec2W position, int octaveCount, f32 persistence, f32 lacunarity) {
    Vec2W value = Vec2WZero();
    v128f amplitude = VecOne();
    for (int i = 0; i < octaveCount; i++) {
        Vec2W c;
        NoiseCellular2Dx4(position.x, position.y, &c.x, &c.y);
        value.x   = VecFmadd(c.x, amplitude, value.x);
        value.y   = VecFmadd(c.y, amplitude, value.y);
        amplitude = VecMulf(amplitude, persistence);
        position  = Vec2WMulf(position, lacunarity);
    }
    return value;
}

#endif // Noise_H
