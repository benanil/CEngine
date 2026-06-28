

#include "Include/Common.h"
#include "Include/Algorithm.h"
#include "Math/Math.h" // Log10_32

#define XSWAP(type, x, y) do { \
    type SWAP_tmp = (x);      \
    (x) = (y);                \
    (y) = SWAP_tmp;           \
} while (0)

void SwapMem(void* a, void* b, size_t elemSize) 
{
    unsigned char tmp[512]; // small buffer
    ASSERT(elemSize < 512);
    SmallMemCpy(tmp, a, elemSize);
    SmallMemCpy(a, b, elemSize);
    SmallMemCpy(b, tmp, elemSize);
}

// quicksort core
void QuickSort(void* arr, int left, int right, size_t elemSize,
               int (*compareFn)(const void*, const void*)) {
    unsigned char* base = (unsigned char*)arr;
    int i, j;

    while (right > left) {
        j = right;
        i = left - 1;
        void* v = base + right * elemSize;

        for (;;) {
            do { i++; } while (compareFn(base + i * elemSize, v) < 0 && i < j);
            do { j--; } while (compareFn(base + j * elemSize, v) >= 0 && i < j);

            if (i >= j) break;
            SwapMem(base + i * elemSize, base + j * elemSize, elemSize);
        }

        SwapMem(base + i * elemSize, base + right * elemSize, elemSize);

        if ((i - 1 - left) <= (right - i - 1)) {
            QuickSort(base, left, i - 1, elemSize, compareFn);
            left = i + 1;
        } else {
            QuickSort(base, i + 1, right, elemSize, compareFn);
            right = i - 1;
        }
    }
}

void Reverse(void** begin, void** end, int elemSize)
{
    while (begin < end)
    {
        SwapMem(*begin, *end, elemSize);
        begin++;
        end--;
    }
}

int* BinarySearch(int* begin, int len, int value)
{
    int low = 0;
    int high = len;
    
    while (low < high)
    {
        int mid = (low + high) >> 1;
        if (begin[mid] < value) low = mid + 1;
        else if (begin[mid] > value) high = mid - 1;
        else return begin + mid; // (begin[mid] == value)
    }
    return NULL;
}

const char* ParseNumberI64(const char* curr, int64_t* result)
{
    while (*curr && (*curr != '-' && !IsNumber(*curr))) 
        curr++; // skip whitespace
    
    int64_t val = 0ll;
    bool negative = false;
    
    if (*curr == '-') 
        curr++, negative = true;
    
    while (*curr > '\n' && IsNumber(*curr))
    {
        if (val >= (INT64_MAX / 10)) goto next; // Check overflow 
        val = val * 10 + (*curr - '0');
        next: { curr++; }
    }
    
    *result = negative ? -val : val;
    return curr;
}

const char* ParsePositiveNumber(const char* str, int* outValue)
{
    int64_t number;
    str = ParseNumberI64(str, &number);
    *outValue = (int)number;
    return str;
}

const char* ParsePositiveNumberU16(const char* str, uint16_t* outValue)
{
    int64_t number;
    str = ParseNumberI64(str, &number);
    *outValue = (uint16_t)number;
    return str;
}

bool IsParsable(const char* curr)
{   // additional checks
    if (*curr == 0 || *curr == '\n') return false;
    if (!IsNumber(*curr) || *curr != '-') return false;
    return true;
}

const char* ParseFloat(const char* ptr, float* res)
{
    const double POWER_10_POS[20] =
    {
        1.0e0,  1.0e1,  1.0e2,  1.0e3,  1.0e4,  1.0e5,  1.0e6,  1.0e7,  1.0e8,  1.0e9,
        1.0e10, 1.0e11, 1.0e12, 1.0e13, 1.0e14, 1.0e15, 1.0e16, 1.0e17, 1.0e18, 1.0e19,
    };

    const double POWER_10_NEG[20] =
    {
        1.0e0,   1.0e-1,  1.0e-2,  1.0e-3,  1.0e-4,  1.0e-5,  1.0e-6,  1.0e-7,  1.0e-8,  1.0e-9,
        1.0e-10, 1.0e-11, 1.0e-12, 1.0e-13, 1.0e-14, 1.0e-15, 1.0e-16, 1.0e-17, 1.0e-18, 1.0e-19,
    };

    while (!IsNumber(*ptr) && *ptr != '-') ptr++;
	
    double sign = 1.0;
    if(*ptr == '-')
        sign = -1.0, ptr++; 

    double num = 0.0;

    while (IsNumber(*ptr)) 
        num = 10.0 * num + (double)(*ptr++ - '0');

    if (*ptr == '.') ptr++;

    double fra = 0.0, div = 1.0;

    while (IsNumber(*ptr) && div < 1.0e9) // 1e8 is 1 and 8 zero 1000000000
        fra = 10.0f * fra + (double)(*ptr++ - '0'), div *= 10.0f;

    num += fra / div;

    while (IsNumber(*ptr)) ptr++;

    if (*ptr == 'e' || *ptr == 'E') // exponent
    {
        ptr++;
        const double* powers = POWER_10_POS;

        switch (*ptr)
        {
            case '+':
                ptr++;
                break;
            case '-':
                powers = POWER_10_NEG;
                ptr++;
                break;
        }

        int eval = 0;
        while (IsNumber(*ptr))
            eval = 10 * eval + (*ptr++ - '0');

        num *= (eval >= 20) ? 0.0 : powers[eval];
    }

    *res = (float)(sign * num);
    return ptr;
}

// time complexity O(numDigits(x)), space complexity O(1), afterpoint is 0 
// @returns number of characters added
int IntToString(char* ptr, int64_t x, int afterPoint)
{
    int size = 0;
    if (afterPoint < 0) goto end; // possible error
    if (x < 0) ptr[size++] = '-', x = 0-x;
    
    int numDigits = Log10_64(x);
    int blen = numDigits;
    
    AX_NO_UNROLL while (++blen <= afterPoint)
    {
        ptr[size++] = '0';
    }

    if (x == 0)
    {
        ptr[size++] = '0';
        goto end;
    }

    int64_t digitCursor = Pow10Table64(numDigits-1);

    while (digitCursor)
    {
        int64_t digit = x / digitCursor;
        ptr[size++] = (char)(digit + '0');
        x -= digitCursor * digit;
        digitCursor /= 10;
    }
end:
    ptr[size] = '\0';
    return size;
}

// converts floating point to string
// @param afterpoint num digits after friction: 4
// @returns number of characters added
int FloatToString(char* ptr, float f, int afterpoint)
{
    int lessThanZero = f < 0.0f;
    int numChars = 0;
    
    if (lessThanZero) 
        ptr[numChars++] = '-';

    f = f >= 0.0f ? f : -f; // fabs(f)
    int iPart = (int)f;
    numChars += IntToString(ptr + numChars, iPart, 0);
    float fPart = f - (float)iPart;
    ptr[numChars++] = '.';
    int power = afterpoint == 0 ? 0 : Pow10Table64(afterpoint);
    int frac = (int)(fPart * power);
    // emit exactly afterpoint zero-padded fractional digits. IntToString pads a non-zero
    // value to the requested width, but writes an extra digit for zero, so a leading-zero
    // fraction like 0.067 would otherwise drop its zero (".67") 
    if (frac == 0)
    {
        for (int i = 0; i < afterpoint; i++) ptr[numChars++] = '0';
        ptr[numChars] = '\0';
        return numChars;
    }
    return numChars + IntToString(ptr + numChars, frac, afterpoint);
}

// return index if found, -1 otherwise
int aIndexOf(const void* arr, const void* val, int n, size_t elemSize, int (*cmp)(const void*, const void*))
{
    const unsigned char* p = (const unsigned char*)arr;
    for (int i = 0; i < n; i++, p += elemSize)
        if (cmp(p, val) == 0) return i;
    return -1;
}

// count how many times val appears
int aCountIf(const void* arr, const void* val, int n, size_t elemSize, int (*cmp)(const void*, const void*))
{
    const unsigned char* p = (const unsigned char*)arr;
    int count = 0;
    for (int i = 0; i < n; i++, p += elemSize)
        if (cmp(p, val) == 0) count++;
    return count;
}

bool StringContains(const char* name, const char* search)
{
    if (!*search) return true;

    const char* nameStart = name;
    u64 nameLen = StringLength(name);
    u64 searchLen = StringLength(search);
    if (searchLen > nameLen) return false;

    char s0Lower = ToLower(*search);
    v128u firstChar = VeciSet1_8((u8)s0Lower);
    v128u zero = VeciZero();

    u64 safeVectorLen = (nameLen >= 16) ? (nameLen - 16) : 0;
    u64 offset = 0;

    for (; offset <= safeVectorLen; offset += 16)
    {
        v128u chunk = VeciLoad(name + offset);
        v128u lower = VeciToLowerASCII(chunk);
        
        u32 matchMask = (u32)VeciMovemask8(VeciCmpEq8(lower, firstChar));
        u32 zeroMask  = (u32)VeciMovemask8(VeciCmpEq8(chunk, zero));

        // If a null terminator is present in this chunk, ignore any matches that appear AFTER the null terminator.
        if (zeroMask)
        {
            u32 zeroIdx = TrailingZeroCount32(zeroMask);
            matchMask &= (1u << zeroIdx) - 1u; 
        }

        while (matchMask)
        {
            u32 idx = TrailingZeroCount32(matchMask);
            const char* a = name + offset + idx;
            const char* b = search;
            u64 remaining = searchLen;
            u64 nameOffset = (u64)(a - nameStart);

            if (nameLen - nameOffset >= searchLen)
            {
                while (remaining >= 16 && StrCmp16Lower(a, b, 16))
                {
                    a += 16; b += 16; remaining -= 16;
                }

                while (remaining && ToLower(*a) == ToLower(*b))
                {
                    a++; b++; remaining--;
                }
                if (remaining == 0) return true;
            }

            matchMask &= matchMask - 1u;
        }

        // If we hit a null-terminator in this chunk, we are done looking.
        if (zeroMask) return false;
    }

    // Handles the remaining tail of the string (< 16 bytes) safely without page-faulting
    for (; offset < nameLen; offset++)
    {
        if (ToLower(name[offset]) == s0Lower)
        {
            const char* a = name + offset;
            const char* b = search;
            u64 remaining = searchLen;

            if (nameLen - offset >= searchLen)
            {
                while (remaining && ToLower(*a) == ToLower(*b))
                {
                    a++; b++; remaining--;
                }
                if (remaining == 0) return true;
            }
        };
    }

    return false;
}

bool StringEqual(const char* RESTRICT a, const char* RESTRICT b, int n) 
{
    int i = 0;
    for (; i < n-16; i += 16)
        if (StrCmp16(a + i, b + i, 16) == false)
            return false;
   
    AX_ASSUME(n - i < 16);
    AX_NO_UNROLL
    for (; i < n; i++)
        if (a[i] != b[i]) return false;
    return true;
}


#ifdef TEST_ALGO
#define CHECK(x) printf("%s ", buf); if(!(x)) { printf("fail line %d\n", __LINE__); printf("failed\n"); } else printf("passed\n")

static void Test_IntToString()
{
    char buf[64];
    IntToString(buf, 0, 0);         CHECK(strcmp(buf,"0")==0);
    IntToString(buf, 8, 0);         CHECK(strcmp(buf,"8")==0);
    IntToString(buf, -44, 0);       CHECK(strcmp(buf,"-44")==0);
    IntToString(buf, 42, 5);        CHECK(strcmp(buf,"00042")==0);
    IntToString(buf, 123456789, 0); CHECK(strcmp(buf,"123456789")==0);
}

static void Test_FloatToString()
{
    char buf[64];
    FloatToString(buf, 0.0f, 2);        CHECK(strcmp(buf, "0.00") == 0);
    FloatToString(buf, 5.0f, 3);        CHECK(strcmp(buf, "5.000") == 0);
    FloatToString(buf, 3.14159f, 3);    CHECK(strcmp(buf, "3.141") == 0);
    FloatToString(buf, -7.25f, 2);      CHECK(strcmp(buf, "-7.25") == 0);
    FloatToString(buf, 123456.789f, 1); CHECK(strcmp(buf, "123456.7") == 0);
    FloatToString(buf, 9.99f, 1);       CHECK(strcmp(buf, "9.9") == 0);      /* matches previous expectation */
    FloatToString(buf, 0.00051f, 5);    CHECK(strcmp(buf, "0.00051") == 0);   /* leading zeros in fraction */
    FloatToString(buf, -0.1234f, 2);    CHECK(strcmp(buf, "-0.12") == 0);
    FloatToString(buf, 0.9999f, 3);     CHECK(strcmp(buf, "0.999") == 0);   /* truncation, not rounding */
}
#endif
