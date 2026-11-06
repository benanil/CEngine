#ifndef SIMPLEX_NOISE
#define SIMPLEX_NOISE

#include "NoiseShared.h"

// https://www.shadertoy.com/view/l32fRW
// Generate 2D gradient vector from hash
purefn Vec2W VCALL RGrad2W(Vec2W p, v128f rot) {
    v128f u = VecAdd(VecMulf(NoisePermuteV(VecAdd(NoisePermuteV(p.x), p.y)), 0.0243902439f), rot);
    u = VecMul(VecFract(u), VecSet1(MATH_TwoPI));
    return (Vec2W){ VecCosFast(u), VecSinFast(u) };
}

// https://www.shadertoy.com/view/MfGyzz
purefn v128f VCALL Simplex2(Vec2W pos) {
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

purefn v128f Simplex2FBM(Vec2W position, int octaveCount, f32 persistence, f32 lacunarity) {
    v128f value = VecZero();
    v128f amplitude = VecOne();
    for (int i = 0; i < octaveCount; i++) {
        value     = VecFmadd(Simplex2(position), amplitude, value);
        amplitude = VecMulf(amplitude, persistence);
        position  = Vec2WMulf(position, lacunarity);
    }
    return VecClampf(value, -1.0f, 1.0f);
}
#endif