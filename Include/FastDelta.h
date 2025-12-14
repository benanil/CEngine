
#ifndef FAST_DELTA
#define FAST_DELTA

// https://github.com/lemire/FastDifferentialCoding

#if defined(_MSC_VER)

#include <intrin.h>
// write to output the successive differences of input (input[0]-starting_point, input[1]-input[2], ...)
// there are "length" values in input and output
// input and output must be distinct
static inline void DeltaEncodingU32(const uint32_t * __restrict__ input, size_t length, uint32_t * __restrict__ output, uint32_t starting_point) {
    __m128i prev = _mm_set1_epi32(starting_point);
    size_t i = 0;
    for(; i  < length/4; i++) {
        __m128i curr =  _mm_lddqu_si128 (( const __m128i*) input + i );
        __m128i delta = _mm_sub_epi32(curr, _mm_alignr_epi8(curr, prev, 12));
        _mm_storeu_si128((__m128i*)output + i,delta);
        prev = curr;
    }
    uint32_t lastprev = _mm_extract_epi32(prev,3);
    for(i = 4 * i; i < length; ++i) {
        uint32_t curr = input[i];
        output[i] = curr - lastprev;
        lastprev = curr;
    }
}

// write to buffer the successive differences of buffer (buffer[0]-starting_point, buffer[1]-buffer[2], ...)
// there are "length" values in buffer
static inline void DeltaEncodingU32Inplace(uint32_t * buffer, size_t length, uint32_t starting_point) {
    __m128i prev = _mm_set1_epi32(starting_point);
    size_t i = 0;
    for(; i  < length/4; i++) {
        __m128i curr =  _mm_lddqu_si128 (( const __m128i*) buffer + i );
        __m128i delta = _mm_sub_epi32(curr, _mm_alignr_epi8(curr, prev, 12));
        _mm_storeu_si128((__m128i*)buffer + i,delta);
        prev = curr;
    }
    uint32_t lastprev = _mm_extract_epi32(prev,3);
    for(i = 4 * i; i < length; ++i) {
        uint32_t curr = buffer[i];
        buffer[i] = curr - lastprev;
        lastprev = curr;
    }
}

// write to output the successive differences of input (input[0]-starting_point, input[1]-input[2], ...)
// there are "length" values in input and output
// input and output must be distinct
static inline void PrefixSumU32(const uint32_t * __restrict__ input, size_t length, uint32_t * __restrict__ output, uint32_t starting_point) {
    __m128i prev = _mm_set1_epi32(starting_point);
    size_t i = 0;
    for(; i  < length/4; i++) {
        __m128i curr =  _mm_lddqu_si128 (( const __m128i*) input + i );
        const __m128i _tmp1 = _mm_add_epi32(_mm_slli_si128(curr, 8), curr);
        const __m128i _tmp2 = _mm_add_epi32(_mm_slli_si128(_tmp1, 4), _tmp1);
        prev = _mm_add_epi32(_tmp2, _mm_shuffle_epi32(prev, 0xff));
        _mm_storeu_si128((__m128i*)output + i,prev);
    }
    uint32_t lastprev = _mm_extract_epi32(prev,3);
    for(i = 4 * i; i < length; ++i) {
        lastprev = lastprev + input[i];
        output[i] = lastprev;
    }
}

// write to buffer the successive differences of buffer (buffer[0]-starting_point, buffer[1]-buffer[2], ...)
// there are "length" values in buffer
static inline void PrefixSumU32fInplace(uint32_t * buffer, size_t length, uint32_t starting_point) {
    __m128i prev = _mm_set1_epi32(starting_point);
    size_t i = 0;
    for(; i  < length/4; i++) {
        __m128i curr =  _mm_lddqu_si128 (( const __m128i*) buffer + i );
        const __m128i _tmp1 = _mm_add_epi32(_mm_slli_si128(curr, 8), curr);
        const __m128i _tmp2 = _mm_add_epi32(_mm_slli_si128(_tmp1, 4), _tmp1);
        prev = _mm_add_epi32(_tmp2, _mm_shuffle_epi32(prev, 0xff));
        _mm_storeu_si128((__m128i*)buffer + i,prev);
    }
    uint32_t lastprev = _mm_extract_epi32(prev,3);
    for(i = 4 * i ; i < length; ++i) {
        lastprev = lastprev + buffer[i];
        buffer[i] = lastprev;
    }
}

#elif defined(_M_ARM) || defined(_M_ARM64) || defined(_M_HYBRID_X86_ARM64) || defined(_M_ARM64EC) || __arm__ || __aarch64__

#if defined(_MSC_VER) && !defined(__clang__) && (defined(_M_ARM64) || defined(_M_HYBRID_X86_ARM64) || defined(_M_ARM64EC) || defined(__aarch64__))
    #include <arm64_neon.h>
#else
    #include <arm_neon.h>
#endif

static inline void DeltaEncodingU32(const uint32_t * __restrict__ input, size_t length, uint32_t * __restrict__ output, uint32_t starting_point) {
    uint32x4_t prev = vdupq_n_u32(starting_point);
    size_t i = 0;
    for(; i < length/4; i++) {
        uint32x4_t curr = vld1q_u32(input + i * 4);
        // Shift curr right by one element and insert last element of prev
        uint32x4_t shifted = vextq_u32(prev, curr, 3);
        uint32x4_t delta = vsubq_u32(curr, shifted);
        vst1q_u32(output + i * 4, delta);
        prev = curr;
    }
    uint32_t lastprev = vgetq_lane_u32(prev, 3);
    for(i = 4 * i; i < length; ++i) {
        uint32_t curr = input[i];
        output[i] = curr - lastprev;
        lastprev = curr;
    }
}

static inline void DeltaEncodingU32Inplace(uint32_t * buffer, size_t length, uint32_t starting_point) {
    uint32x4_t prev = vdupq_n_u32(starting_point);
    size_t i = 0;
    for(; i < length/4; i++) {
        uint32x4_t curr = vld1q_u32(buffer + i * 4);
        uint32x4_t shifted = vextq_u32(prev, curr, 3);
        uint32x4_t delta = vsubq_u32(curr, shifted);
        vst1q_u32(buffer + i * 4, delta);
        prev = curr;
    }
    uint32_t lastprev = vgetq_lane_u32(prev, 3);
    for(i = 4 * i; i < length; ++i) {
        uint32_t curr = buffer[i];
        buffer[i] = curr - lastprev;
        lastprev = curr;
    }
}

static inline void PrefixSumU32(const uint32_t * __restrict__ input, size_t length, uint32_t * __restrict__ output, uint32_t starting_point) {
    uint32x4_t prev = vdupq_n_u32(starting_point);
    size_t i = 0;
    for(; i < length/4; i++) {
        uint32x4_t curr = vld1q_u32(input + i * 4);
        // Parallel prefix sum within the vector
        // Step 1: [a, b, c, d] -> [a, a+b, c, c+d]
        uint32x4_t tmp1 = vaddq_u32(curr, vextq_u32(vdupq_n_u32(0), curr, 2));
        // Step 2: [a, a+b, c, c+d] -> [a, a+b, a+b+c, a+b+c+d]
        uint32x4_t tmp2 = vaddq_u32(tmp1, vextq_u32(vdupq_n_u32(0), tmp1, 3));
        // Add the last value from previous iteration to all elements
        uint32x4_t broadcast = vdupq_n_u32(vgetq_lane_u32(prev, 3));
        prev = vaddq_u32(tmp2, broadcast);
        vst1q_u32(output + i * 4, prev);
    }
    uint32_t lastprev = vgetq_lane_u32(prev, 3);
    for(i = 4 * i; i < length; ++i) {
        lastprev = lastprev + input[i];
        output[i] = lastprev;
    }
}

static inline void PrefixSumU32Inplace(uint32_t * buffer, size_t length, uint32_t starting_point) {
    uint32x4_t prev = vdupq_n_u32(starting_point);
    size_t i = 0;
    for(; i < length/4; i++) {
        uint32x4_t curr = vld1q_u32(buffer + i * 4);
        uint32x4_t tmp1 = vaddq_u32(curr, vextq_u32(vdupq_n_u32(0), curr, 2));
        uint32x4_t tmp2 = vaddq_u32(tmp1, vextq_u32(vdupq_n_u32(0), tmp1, 3));
        uint32x4_t broadcast = vdupq_n_u32(vgetq_lane_u32(prev, 3));
        prev = vaddq_u32(tmp2, broadcast);
        vst1q_u32(buffer + i * 4, prev);
    }
    uint32_t lastprev = vgetq_lane_u32(prev, 3);
    for(i = 4 * i; i < length; ++i) {
        lastprev = lastprev + buffer[i];
        buffer[i] = lastprev;
    }
}

#else

static inline void DeltaEncodingU32(const uint32_t* in, size_t n, uint32_t* out, uint32_t starting_point)
{
    if (n == 0) return;
    out[0] = in[0] - starting_point;
    for (size_t i = 1; i < n; ++i)
        out[i] = in[i] - in[i - 1];
}

static inline void DeltaEncodingU32Inplace(uint32_t* inout, size_t n, uint32_t starting_point)
{
    if (n == 0) return;
    uint32_t prev = starting_point;
    for (size_t i = 0; i < n; ++i) {
        uint32_t curr = inout[i];
        inout[i] = curr - prev;
        prev = curr;
    }
}

static inline void PrefixSumU32(const uint32_t* in, size_t n, uint32_t* out, uint32_t starting_point)
{
    if (n == 0) return;
    uint32_t acc = starting_point;
    for (size_t i = 0; i < n; ++i) {
        acc += in[i];
        out[i] = acc;
    }
}

static inline void PrefixSumU32Inplace(uint32_t* inout, size_t n, uint32_t starting_point)
{
    if (n == 0) return;
    uint32_t acc = starting_point;
    for (size_t i = 0; i < n; ++i) {
        acc += inout[i];
        inout[i] = acc;
    }
}

#endif


#endif // delta defined