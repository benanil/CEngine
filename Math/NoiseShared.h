#ifndef NOISE_SHARED
#define NOISE_SHARED
#include "Math.h"

// Permutation polynomial (34x^2 + x) mod 289, the shared hash of the noise family.
purefn v128f VCALL NoiseMod289V(v128f x) {
    return VecSub(x, VecMulf(VecFloor(VecMulf(x, 1.0f / 289.0f)), 289.0f));
}

purefn v128f VCALL NoiseMod7V(v128f x) {
    return VecSub(x, VecMulf(VecFloor(VecMulf(x, 1.0f / 7.0f)), 7.0f));
}

purefn v128f VCALL NoisePermuteV(v128f x) {
    return NoiseMod289V(VecMul(VecAddf(VecMulf(x, 34.0f), 1.0f), x));
}

#endif