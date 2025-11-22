

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

// String to number functions
const char* ParseNumber(const char* curr, int* result)
{
    while (*curr && (*curr != '-' && !IsNumber(*curr))) 
        curr++; // skip whitespace
    
    int val = 0l;
    bool negative = false;
    
    if (*curr == '-') 
        curr++, negative = true;
    
    while (*curr > '\n' && IsNumber(*curr))
    {
        if (val >= (INT_MAX / 10)) goto next; // Check overflow 
        val = val * 10 + (*curr - '0');
        next: { curr++; }
    }
    *result = negative ? -val : val;
    return curr;
}

const char* ParseNumberLong(const char* curr, int64_t* result)
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

// if you really want to squeeze performance no negative and such checks
const char* ParsePositiveNumber(const char* str, int* outValue)
{
    while (*str && !IsNumber(*str))
        str++;

    int v = 0;
    while (*str && IsNumber(*str)) 
    {
        if (v > (INT_MAX / 10)) goto next; // Check overflow 
        v = v * 10 + (*str - '0');
        next: { str++; }
    }

    *outValue = v;
    return str;
}

const char* ParsePositiveNumberU16(const char* str, uint16_t* outValue)
{
    int number = 0;
    str = ParsePositiveNumber(str, &number);
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
int IntToString(char* ptr, int x, int afterPoint)
{
    if (x == 0 && afterPoint == 0)
    {
        ptr[0] = '0';
        return 1;
    }
    if (afterPoint < 0) return 0;
    int size = 0;
    if (x < 0) ptr[size++] = '-', x = 0-x;
    
    int numDigits = Log10_32(x);
    int blen = numDigits;
    
    AX_NO_UNROLL while (++blen <= afterPoint)
    {
        ptr[size++] = '0';
    }

    unsigned int const PowersOf10[10] = { 1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000 };
    numDigits = PowersOf10[numDigits];

    while (numDigits)
    {
        int digit = x / numDigits;
        ptr[size++] = (char)(digit + '0');
        x -= numDigits * digit;
        numDigits /= 10;
    }

    ptr[size] = '\0';
    return size;
}

int Pow10(int x) 
{
    int res = x != 0;
    while (x-- > 0) res *= 10;
    return res;
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
    int power = Pow10(afterpoint); 
    return numChars + IntToString(ptr + numChars, (int)(fPart * power), afterpoint-1);
}

bool aStartsWith(const char** curr, const char* str)
{
    const char* currStart = *curr;
    while (IsWhitespace(**curr))
        (*curr)++;

    if (**curr != *str)
        return false;

    while (*str && **curr == *str)
        (*curr)++, str++;

    bool isEqual = *str == 0;
    if (!isEqual)
        *curr = currStart;

    return isEqual;
}

// fill [begin, end) with val
void aFill(void* begin, void* end, const void* val, size_t elemSize) {
    unsigned char* p = (unsigned char*)begin;
    unsigned char* e = (unsigned char*)end;
    while (p < e) {
        SmallMemCpy(p, val, elemSize);
        p += elemSize;
    }
}

// fill n elements with val
void aFillN(void* arr, const void* val, int n, size_t elemSize) {
    unsigned char* p = (unsigned char*)arr;
    for (int i = 0; i < n; i++, p += elemSize)
        SmallMemCpy(p, val, elemSize);
}


// check if arr contains val
bool aContains(const void* arr, const void* val, int n, size_t elemSize, int (*cmp)(const void*, const void*)) 
{
    const unsigned char* p = (const unsigned char*)arr;
    for (int i = 0; i < n; i++, p += elemSize)
        if (cmp(p, val) == 0) return true;
    return false;
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

bool StringEqual(const char* RESTRICT a, const char* RESTRICT b, int n) 
{
    int i = 0;
    for (; i < n-16; i += 16)
        if (StrCmp16(a, b, 16) == false)
            return false;
   
    for (; i < n; i++)
        if (a[i] != b[i])
            return false;
    return true;
}