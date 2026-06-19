#ifndef BITSET_H
#define BITSET_H

#include <stdint.h>
#include "Common.h"

#if defined(__cplusplus)
extern "C" {
#endif

static inline void BitsetSet(u64* bits, s32 idx) {
    bits[idx >> 6] |= (1ull << (idx & 63));
}

static inline bool BitsetGet(const u64* bits, s32 idx) {
    return (bits[idx >> 6] >> (idx & 63)) & 1ull;
}
   
static inline void BitsetReset(u64* bits, s32 idx) {
    bits[idx / 64] &= ~(1ull << (idx & 63));
}

static inline s32 FindFirstSet(u64 bits) {
    if (bits == 0) return -1;
    return (s32)TrailingZeroCount64(bits);
}

#define BIT_OPERATION(_Name, _Operation, _256Operation)                              \
static inline void _Name##256(u64* res, const u64* a, const u64* b)                  \
{                                                                                    \
    VecStoreU(res,     _Operation(VeciLoad(a)    , VeciLoad(b)));                    \
    VecStoreU(res + 2, _Operation(VeciLoad(a + 2), VeciLoad(b + 2)));                \
}                                                                                    \
static inline void _Name##512(u64* res, const u64* a, const u64* b)                  \
{                                                                                    \
    if (SIMD_NUM_BYTES == 32) {                                                      \
        VecStoreI256(res,     _256Operation(VecLoadI256(a), VecLoadI256(b)));        \
        VecStoreI256(res + 4, _256Operation(VecLoadI256(a + 4), VecLoadI256(b + 4)));\
    } else {                                                                         \
        _Name##256(res,  a,     b);                                                  \
        _Name##256(res + 4, a + 4, b + 4);                                           \
    }                                                                                \
}                                                                                    \
static inline void _Name##1024(u64* res, const u64* a, const u64* b)                 \
{                                                                                    \
    _Name##512(res, a, b);                                                           \
    _Name##512(res + 8, a + 8, b + 8);                                               \
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

static inline void Not256(u64* res, const u64* a)
{
    #if defined(AX_SUPPORT_AVX2)
    VecStoreI256(res, VeciNot256(VecLoadI256(a)));
    #else
    VecStoreU(res,     VeciNot(VeciLoad(a)));
    VecStoreU(res + 2, VeciNot(VeciLoad(a + 2)));
    #endif
}

#if defined(__aarch64__) || defined(__arm__)
    #define HSum32_128(x) vaddvq_u32(x)
#else
purefn u32 VCALL HSum32_128(v128u x)
{
    v128u hi64  = _mm_shuffle_epi32(x,     _MM_SHUFFLE(1, 0, 3, 2));
    v128u sum64 = _mm_add_epi32(x, hi64);
    v128u hi32  = _mm_shuffle_epi32(sum64, _MM_SHUFFLE(2, 3, 0, 1));
    return _mm_cvtsi128_si32(_mm_add_epi32(sum64, hi32));
}
#endif

purefn v128u VCALL PopCount128(v128u x)
{
    v128u y;
    y = VeciAnd(VeciSrl32(x, 1), VeciSet1(0x55555555));
    x = VeciSub(x, y);
    y = VeciAnd(VeciSrl32(x, 2), VeciSet1(0x33333333));
    x = VeciAdd(VeciAnd(x, VeciSet1(0x33333333)), y);
    x = VeciAnd(VeciAdd(x, VeciSrl32(x, 4)), VeciSet1(0x0F0F0F0F));
    return VeciSrl32(VeciMul(x, VeciSet1(0x01010101)), 24);
}

// from Faster Population Counts Using AVX2 Instructions resource paper
purefn u32 VCALL PopCount256(const u64* ptr)
{
    #ifdef AX_SUPPORT_AVX2
    const v256i lookup = _mm256_setr_epi8(0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4, 0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4);
    const v256i low_mask = _mm256_set1_epi8(0x0f);
    v256i v       = _mm256_loadu_si256((__m256i const *)ptr);
    v256i lo      = _mm256_and_si256(v, low_mask);
    v256i hi      = _mm256_and_si256(_mm256_srli_epi32(v, 4), low_mask);
    v256i popcnt1 = _mm256_shuffle_epi8(lookup, lo);
    v256i popcnt2 = _mm256_shuffle_epi8(lookup, hi);
    v256i total   = _mm256_add_epi8(popcnt1, popcnt2);	
    v = _mm256_sad_epu8(total, _mm256_setzero_si256());
    return _mm256_cvtsi256_si32(v) + _mm256_extract_epi64(v, 1) + _mm256_extract_epi64(v, 2) + _mm256_extract_epi64(v, 3);
    #else
    return HSum32_128(PopCount128(VeciLoad(ptr))) + HSum32_128(PopCount128(VeciLoad(ptr + 2)));
    #endif
}

purefn u32 VCALL PopCount512(const u64* ptr) {
    return PopCount256(ptr) + PopCount256(ptr + 4);
}

purefn u32 VCALL PopCount1024(const u64* ptr) {
    return PopCount512(ptr) + PopCount512(ptr + 8);
}

static inline s32 BitsetFindEmptyRange(const u64* bits, u32 bitCount, u32 count)
{
    if (count == 0u) return 0;
    if (count > bitCount) return -1;
    for (u32 start = 0u; start + count <= bitCount; start++)
    {
        u32 i = 0u;
        for (; i < count; i++)
            if (BitsetGet(bits, (s32)(start + i))) break;
        if (i == count) return (s32)start;
        start += i;
    }
    return -1;
}

static inline void BitsetSetRange(u64* bits, u32 offset, u32 count, bool set)
{
    for (u32 i = 0u; i < count; i++)
    {
        if (set) BitsetSet(bits, (s32)(offset + i));
        else     BitsetReset(bits, (s32)(offset + i));
    }
}

static inline s32 BitsetFindFirstEmpty(const u64* bits, s32 bitCount)
{
    if (bitCount <= 0) return -1;

    const s32 wordCount = bitCount >> 6;
    s32 wordIdx = 0;

    while (wordIdx + 4 <= wordCount && PopCount256(bits + wordIdx) == 256)
        wordIdx += 4;

    AX_ASSUME(wordCount - wordIdx >= 0 && wordCount - wordIdx < 8);
    for (; wordIdx < wordCount; ++wordIdx) {
        const s32 bitIdx = FindFirstSet(~bits[wordIdx]);
        if (bitIdx >= 0) return (wordIdx << 6) + bitIdx;
    }

    const s32 remainingBits = bitCount & 63;
    if (remainingBits == 0) return -1;

    const u64 mask = (1ull << remainingBits) - 1ull;
    const s32 bitIdx = FindFirstSet(~bits[wordCount] & mask);
    return bitIdx < 0 ? -1 : (wordCount << 6) + bitIdx;
}

// bitCount = number of valid bits/elements in the bitset
purefn u32 BitsetPopCount(const u64* bits, s32 bitCount)
{
    if (!bits || bitCount <= 0)
        return 0;

    u32 result = 0;
    s32 wordCount = bitCount >> 6;
    s32 tailBits  = bitCount & 63;
    s32 wordIdx   = 0;

    while (wordIdx + 8 <= wordCount) {
        result += PopCount512(bits + wordIdx);
        wordIdx += 8;
    }

    if (wordIdx + 4 <= wordCount) {
        result += PopCount256(bits + wordIdx);
        wordIdx += 4;
    }

    // Remaining full u64 words: max 3.
    switch (wordCount - wordIdx)
    {
        case 3: result += PopCount64(bits[wordIdx++]); // fallthrough
        case 2: result += PopCount64(bits[wordIdx++]); // fallthrough
        case 1: result += PopCount64(bits[wordIdx++]); // fallthrough
    }

    if (tailBits) {
        const u64 mask = (1ull << tailBits) - 1ull;
        result += PopCount64(bits[wordCount] & mask);
    }

    return result;
}

static bool BitsetHasAtLeastEmptyBits(const u64* bits, s32 bitCount, u32 needed)
{
    if (needed == 0)
        return true;

    if (!bits || bitCount <= 0)
        return false;

    u32 found = 0;
    const s32 fullWords = bitCount >> 6;
    const s32 tailBits  = bitCount & 63;

    s32 wordIdx = 0;
    // 512-bit chunks = 8 x u64. 64byte cacheline
    while (wordIdx + 8 <= fullWords)
    {
        found += 512u - PopCount512(bits + wordIdx);
        if (found >= needed)
            return true;
        wordIdx += 8;
    }

    if (wordIdx + 4 <= fullWords)
    {
        found += 256u - PopCount256(bits + wordIdx);
        wordIdx += 4;
    }

    // Remaining full u64 words: max 3.
    switch (fullWords - wordIdx)
    {
        case 3: found += PopCount64(~bits[wordIdx++]); // fallthrough
        case 2: found += PopCount64(~bits[wordIdx++]); // fallthrough
        case 1: found += PopCount64(~bits[wordIdx++]); // fallthrough
    }

    if (tailBits)
    {
        const u64 mask = (1ull << tailBits) - 1ull;
        found += PopCount64((~bits[wordIdx]) & mask);
    }
    return found >= needed;
}

#if defined(__cplusplus)
}
#endif

#endif // BITSET_H
