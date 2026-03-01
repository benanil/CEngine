#ifndef BITSET_H
#define BITSET_H

#include <stdint.h>
#include "Common.h"

#if defined(__cplusplus)
extern "C" {
#endif

static inline void BitsetSet(uint64_t* bits, int idx) {
    bits[idx >> 6] |= (1ull << (idx & 63));
}

static inline bool BitsetGet(const uint64_t* bits, int idx) {
    return (bits[idx >> 6] >> (idx & 63)) & 1ull;
}
   
static inline void BitsetReset(uint64_t* bits, int idx) {
    bits[idx / 64] &= ~(1ull << (idx & 63));
}

#define BIT_OPERATION(_Name, _Operation, _256Operation)                             \
static inline void _Name##256(uint64_t* res, const uint64_t* a, const uint64_t* b)  \
{                                                                                   \
    Vec4x32u a0 = VeciLoad(a), a1 = VeciLoad(a + 2);                                \
    Vec4x32u b0 = VeciLoad(b), b1 = VeciLoad(b + 2);                                \
    VecStoreU(res,     _Operation(a0, b0));                                         \
    VecStoreU(res + 2, _Operation(a1, b1));                                         \
}                                                                                   \
static inline void _Name##512(uint64_t* res, const uint64_t* a, const uint64_t* b)  \
{                                                                                   \
    if (SIMD_NUM_BYTES == 32) {                                                     \
        Vec8x32i a0 = VecLoadI256(a),     b0 = VecLoadI256(b);                      \
        Vec8x32i a1 = VecLoadI256(a + 4), b1 = VecLoadI256(b + 4);                  \
        VecStoreI256(res,     _256Operation(a0, b0));                               \
        VecStoreI256(res + 4, _256Operation(a1, b1));                               \
    } else {                                                                        \
        _Name##256(res,     a,     b);                                              \
        _Name##256(res + 4, a + 4, b + 4);                                          \
    }                                                                               \
}                                                                                   \
static inline void _Name##1024(uint64_t* res, const uint64_t* a, const uint64_t* b) \
{                                                                                   \
    Name##512(res, a, b);                                                           \
    Name##512(res + 8, a + 8, b + 8);                                               \
}

// Below macro defines the functions:
// AndNot256, AndNot512, AndNot1024
// Or256    , Or512    , Or1024 
// And256   , And512   , And1024 
// Xor256   , Xor512   , Xor1024 
BIT_OPERATION(AndNot, VeciAndNot, VeciAndNot256)
BIT_OPERATION(Or,     VeciOr,     VeciOr256)
BIT_OPERATION(And,    VeciAnd,    VeciAnd256)
BIT_OPERATION(Xor,    VeciXor,    VeciXor256)


#if defined(__aarch64__) || defined(__arm__)
    #define HSum32_128(x) vaddvq_u32(x)
#else
purefn uint32_t VECTORCALL HSum32_128(Vec4x32u x)
{
    Vec4x32u hi64  = _mm_shuffle_epi32(x,     _MM_SHUFFLE(1, 0, 3, 2));
    Vec4x32u sum64 = _mm_add_epi32(x, hi64);
    Vec4x32u hi32  = _mm_shuffle_epi32(sum64, _MM_SHUFFLE(2, 3, 0, 1));
    return _mm_cvtsi128_si32(_mm_add_epi32(sum64, hi32));
}
#endif

purefn Vec4x32u VECTORCALL PopCount128(Vec4x32u x)
{
    Vec4x32u y;
    y = VeciAnd(VeciSrl32(x, 1), VeciSet1(0x55555555));
    x = VeciSub(x, y);
    y = VeciAnd(VeciSrl32(x, 2), VeciSet1(0x33333333));
    x = VeciAdd(VeciAnd(x, VeciSet1(0x33333333)), y);
    x = VeciAnd(VeciAdd(x, VeciSrl32(x, 4)), VeciSet1(0x0F0F0F0F));
    return VeciSrl32(VeciMul(x, VeciSet1(0x01010101)), 24);
}

// from Faster Population Counts Using AVX2 Instructions resource paper
purefn uint32_t VECTORCALL PopCount256(const uint64_t* ptr)
{
    #ifdef AX_SUPPORT_AVX2
    const __m256i lookup = _mm256_setr_epi8(0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4, 0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4);
    const __m256i low_mask = _mm256_set1_epi8(0x0f);
    __m256i v       = _mm256_stream_load_si256((__m256i const *)ptr);
    __m256i lo      = _mm256_and_si256(v, low_mask);
    __m256i hi      = _mm256_and_si256(_mm256_srli_epi32(v, 4), low_mask);
    __m256i popcnt1 = _mm256_shuffle_epi8(lookup, lo);
    __m256i popcnt2 = _mm256_shuffle_epi8(lookup, hi);
    __m256i total   = _mm256_add_epi8(popcnt1, popcnt2);	
    v = _mm256_sad_epu8(total, _mm256_setzero_si256());
    return _mm256_cvtsi256_si32(v) + _mm256_extract_epi64(v, 1) + _mm256_extract_epi64(v, 2) + _mm256_extract_epi64(v, 3);
    #else
    return HSum32_128(PopCount128(VeciLoad(ptr))) + HSum32_128(PopCount128(VeciLoad(ptr + 2)));
    #endif
}

purefn uint32_t VECTORCALL PopCount512(const uint64_t* ptr) {
    return PopCount256(ptr) + PopCount256(ptr + 4);
}

purefn uint32_t VECTORCALL PopCount1024(const uint64_t* ptr) {
    return PopCount512(ptr) + PopCount512(ptr + 8);
}

#if defined(__cplusplus)
}
#endif

#endif // BITSET_H