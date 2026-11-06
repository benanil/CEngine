#ifndef CELLULAR_H
#define CELLULAR_H

#include "NoiseShared.h"
#include "Vec2Wide.h"

// Procedural noise helpers ported from webgl-noise (Gustavson / McEwan, MIT).
// Scalar/engine-vector form; keeps only the permutation polynomial and 2-D Worley noise.
// 4-wide 2-D cellular noise: four (Px, Py) points in parallel, one per lane.
// *outF1 / *outF2 receive nearest and second-nearest feature distances.
purefn Vec2W VCALL Cellular2(Vec2W P)
{
    const f32 K      = 1.0f / 7.0f;   // 0.142857
    const f32 Ko     = 3.0f / 7.0f;   // 0.428571 (grid mean offset)
    const f32 jitter = 1.0f;         // lower gives a more regular pattern
    
    const f32 oi[3]    = { -1.0f,  0.0f,  1.0f };  // integer cell neighbour offsets
    const f32 of[3]    = { -0.5f,  0.5f,  1.5f };  // row distance offsets
    const f32 colDx[3] = {  0.5f, -0.5f, -1.5f }; // column distance offsets

    v128f PiX = NoiseMod289V(VecFloor(P.x));
    v128f PiY = NoiseMod289V(VecFloor(P.y));
    v128f PfX = VecFract(P.x)   , PfY = VecFract(P.y);
    v128f f1  = VecSet1(1.0e30f), f2  = VecSet1(1.0e30f);

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
    return (Vec2W) { VecSqrt(f1), VecSqrt(f2) };
}

purefn Vec2W Ceullular2FBM(Vec2W position, s32 octaveCount, f32 persistence, f32 lacunarity) {
    Vec2W value = Vec2WZero();
    v128f amplitude = VecOne();
    for (s32 i = 0; i < octaveCount; i++) {
        Vec2W c = Cellular2(position);
        value.x   = VecFmadd(c.x, amplitude, value.x);
        value.y   = VecFmadd(c.y, amplitude, value.y);
        amplitude = VecMulf(amplitude, persistence);
        position  = Vec2WMulf(position, lacunarity);
    }
    return value;
}

#endif