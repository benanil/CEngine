
#ifndef ALGORITHM_INCLUDED
#define ALGORITHM_INCLUDED

#include "SIMD.h"

#define XSWAP(type, x, y) do { \
    type SWAP_tmp = (x);      \
    (x) = (y);                \
    (y) = SWAP_tmp;           \
} while (0)

#if defined(__cplusplus)
extern "C" {
#endif

purefn bool IsNumber(char a) { return a <= '9' && a >= '0'; };
purefn bool IsLower(char a)  { return a >= 'a' && a <= 'z'; };
purefn bool IsUpper(char a)  { return a >= 'A' && a <= 'Z'; };
purefn bool ToLower(char a)  { return a < 'a' ? a + ('A' - 'a') : a; }
purefn bool ToUpper(char a)  { return a > 'Z' ? a - 'a' + 'A' : a; }
// is alphabetical character?
purefn bool IsChar(char a) { return IsUpper(a) || IsLower(a); };
purefn bool IsWhitespace(char c) { return c <= ' '; }

void SwapMem(void* a, void* b, size_t elemSize);

// quicksort core
void QuickSort(void* arr, int left, int right, size_t elemSize,
                  int (*compareFn)(const void*, const void*));

void Reverse(void** begin, void** end, int elemSize);

int* BinarySearch(int* begin, int len, int value);

// String to number functions
const char* ParseNumber(const char* curr, int* result);

const char* ParseNumberLong(const char* ptr, int64_t* result);

// if you really want to squeeze performance no negative and such checks
const char* ParsePositiveNumber(const char* str, int* outValue);

const char* ParsePositiveNumberU16(const char* str, uint16_t* outValue);

bool IsParsable(const char* curr);

const char* ParseFloat(const char* text, float* out);

int IntToString(char* ptr, int64_t x, int afterPoint);

int Pow10(int x);

// converts floating point to string
// @param afterpoint num digits after friction: 4
// @returns number of characters added
int FloatToString(char* ptr, float f, int afterpoint);

// return index if found, -1 otherwise
int aIndexOf(const void* arr, const void* val, int n, size_t elemSize, int (*cmp)(const void*, const void*));

// count how many times val appears
int aCountIf(const void* arr, const void* val, int n, size_t elemSize, int (*cmp)(const void*, const void*));

#define StrCMP16(_str, _otr) StrCmp16(_str, _otr, sizeof(_otr) - 1)

#if defined(AX_SUPPORT_SSE)
static inline bool StrCmp16(const char* a, const char* b, uint64_t n)
{
    n = n > 16 ? 16 : n;
    __m128i va = _mm_loadu_si128((const __m128i*)a);
    __m128i vb = _mm_loadu_si128((const __m128i*)b);
    __m128i cmp = _mm_cmpeq_epi8(va, vb);
    int mask = _mm_movemask_epi8(cmp);
    int expect = (1 << n) - 1;
    return (mask & expect) == expect;
}
#elif defined(__ARM_NEON) || defined(__ARM_NEON__)

static inline bool StrCmp16(const char* a, const char* b, uint64_t n)
{
    n = n > 16 ? 16 : n;
    uint8x16_t va  = vld1q_u8((const uint8_t*)a);
    uint8x16_t vb  = vld1q_u8((const uint8_t*)b);
    uint8x16_t cmp = vceqq_u8(va, vb);
    uint32_t mask = ARMVecMovemask(cmp);
    uint32_t expect = n >= 32 ? 0xFFFFFFFFu : ((1u << n) - 1u);
    return (mask & expect) == expect;
}
#endif

bool StringEqual(const char* a, const char* b, int n);

#if defined(__cplusplus)
}
#endif

#endif