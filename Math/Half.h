
/******************************************************************************************
    *  Purpose:                                                                               *
    *    Conversion of 16 bit floating point values and 32 bit floating point values          *
    *    SSE and AVX used for performance but not required, scalar versions exists            *
    *  Good To Know:                                                                          *
    *    ...                                                                                  *
    *  Author:                                                                                *
    *    Anilcan Gulkaya 2024 anilcangulkaya7@gmail.com github @benanil                       *
    *******************************************************************************************/

// todo better check for arm neon fp16

#pragma once

#include "../Include/SIMD.h"

/*//////////////////////////////////////////////////////////////////////////*/
/*                             Half                                         */
/*//////////////////////////////////////////////////////////////////////////*/

#define OneFP16       (15360ui16)
#define MinusOneFP16  (48128ui16)
#define ZeroFP16      (0ui16)
#define HalfFP16      (14336ui16) // fp16 0.5
#define Sqrt2FP16     (15784ui16) // fp16 sqrt(2)
#define FP16PI        (0x4248ui16)
#define Half2Up       (OneFP16 << 16u)
#define Half2Down     (MinusOneFP16 << 16u)
#define Half2Left     (MinusOneFP16)
#define Half2Right    (OneFP16)
#define Half2One      (OneFP16 | (OneFP16 << 16))
#define Half4One      (u64_(OneFP16)   * 0x0001000100010001ull)
#define Half4Sqrt2    (u64_(Sqrt2FP16) * 0x0001000100010001ull)
#define Half4PI       (u64_(FP16PI)    * 0x0001000100010001ull)
#define Half2Zero     (0)

#define MakeHalf2(x, y) ((x) | ((y) << 16))
#define Half2SetX(v, x) (v &= 0xFFFF0000u, v |= x;)
#define Half2SetY(v, y) (v &= 0x0000FFFFu, v |= y;)

#ifdef AX_SUPPORT_NEON
typedef float16_t f16;
#else
typedef uint16_t f16;
#endif

// todo better check for half support
purefn float HalfToFloat(f16 x) 
{
    #if defined(AX_SUPPORT_AVX2) 
    return _mm_cvtss_f32(_mm_cvtph_ps(_mm_set1_epi16(x))); 
    #elif defined(AX_SUPPORT_NEON)
    return vgetq_lane_f32(vcvt_f32_f16(vdup_n_f16(x)), 0);
    #else
    u32 h = x;
    u32 h_e = h & 0x00007c00u;
    u32 h_m = h & 0x000003ffu;
    u32 h_s = h & 0x00008000u;
    u32 h_e_f_bias = h_e + 0x0001c000u;
    u32 f_s  = h_s        << 0x00000010u;
    u32 f_e  = h_e_f_bias << 0x0000000du;
    u32 f_m  = h_m        << 0x0000000du;
    u32 f_result = f_s | f_e | f_m;
    return BitCast(float, f_result);
    #endif
}

purefn f16 FloatToHalf(float Value) 
{
    #if defined(AX_SUPPORT_AVX2)
    return _mm_extract_epi16(_mm_cvtps_ph(_mm_set_ss(Value), 0), 0);
    #elif defined(AX_SUPPORT_NEON)
    return vget_lane_f16(vcvt_f16_f32(vdupq_n_f32(Value)), 0);
    #else
    u32 Result; // branch removed version of DirectxMath function
    u32 IValue = BitCast(u32, Value);
    u32 Sign = (IValue & 0x80000000u) >> 16U;
    IValue = IValue & 0x7FFFFFFFu;      // Hack off the sign
    // if (IValue > 0x47FFEFFFu) {
    //     return 0x7FFFU | Sign; // The number is too large to be represented as a half.  Saturate to infinity.
    // }
    u32 mask = 0u - (IValue < 0x38800000u);
    u32 b = IValue + 0xC8000000U;
    u32 a = (0x800000u | (IValue & 0x7FFFFFu)) >> (113u - (IValue >> 23u));
    
    IValue = (mask & a) | (~mask & b);
    Result = ((IValue + 0x0FFFu + ((IValue >> 13u) & 1u)) >> 13u) & 0x7FFFu; 
    return (f16)(Result | Sign);
    #endif
}

static inline void Half2ToFloat2(f32* result, u32 h) 
{
    #if defined(AX_SUPPORT_AVX2)
    _mm_storel_pi((__m64 *)result, _mm_cvtph_ps(_mm_set1_epi16(h)));
    #elif defined(AX_SUPPORT_NEON)
    float16x4_t halfVec = vreinterpret_f16_u32(vdup_n_u32(h));
    vst1_f32(result, vget_low_f32(vcvt_f32_f16(halfVec)));
    #else
    u64 x2 = (u64)(h & 0x0000FFFFull) | (u64(h & 0xFFFF0000ull) << 16ull);
    u64 h_e = x2 & 0x00007c0000007c00ull;
    u64 h_m = x2 & 0x000003ff000003ffull;
    u64 h_s = x2 & 0x0000800000008000ull;
    u64 h_e_f_bias = h_e + 0x0001c0000001c000ull;
    u64 f_s  = h_s        << 0x00000010ull;
    u64 f_e  = h_e_f_bias << 0x0000000dull;
    u64 f_m  = h_m        << 0x0000000dull;
    u64 f_result = f_s | f_e | f_m;
        
    result[0] = BitCast(f32, (u32)(f_result & 0xFFFFFFFFu));
    result[1] = BitCast(f32, (u32)(f_result >> 32ull));
    #endif
}

static inline u32 Float2ToHalf2(const f32* float2)
{
    #if defined(AX_SUPPORT_NEON)
    float32x2_t x = vld1_dup_f32(float2);
    float32x4_t x4 = vcombine_f32(x, x);
    return vget_lane_u32(vreinterpret_u32_f16(vcvt_f16_f32(x4)), 0);
    #elif defined(AX_SUPPORT_AVX)
    return _mm_extract_epi32(_mm_cvtps_ph(_mm_setr_ps(float2[0], float2[1], 0.0f, 0.0f), 0), 0);
    #endif
    u32 result = 0;
    result  = FloatToHalf(float2[0]);
    result |= (u32)FloatToHalf(float2[1]) << 16;
    return result;
}

// input half4 is 4x 16 bit integer for example it can be uint64_t
static inline void Half4ToFloat4(f32* result, const f16 half4[4])
{
    #ifdef AX_SUPPORT_AVX2
    _mm_storeu_ps(result, _mm_cvtph_ps(_mm_loadu_si64(half4)));

    #elif defined(AX_SUPPORT_NEON)
    vst1q_f32(result, vcvt_f32_f16(vld1_dup_f16(half4)));

    #elif defined(AX_SUPPORT_SSE)
    v128u x4 = VeciLoad64((const u64*)half4);
    x4 = VeciUnpackLo16(x4, VeciZero());   // [half4.xy, half4.xy, half4.zw, half4.zw] 
    
    v128u h_e = VeciAnd(x4, VeciSet1(0x00007c00));
    v128u h_m = VeciAnd(x4, VeciSet1(0x000003ff));
    v128u h_s = VeciAnd(x4, VeciSet1(0x00008000));
    v128u h_e_f_bias = VeciAdd(h_e, VeciSet1(0x0001c000));
    
    v128u f_s  = VeciSll32(h_s, 0x00000010);
    v128u f_e  = VeciSll32(h_e_f_bias, 0x0000000d);
    v128u f_m  = VeciSll32(h_m, 0x0000000d);
    v128u f_em = VeciOr(f_e, f_m);

    v128u i_result = VeciOr(f_s, f_em);
    VecStore(result, VeciToVecf(i_result));
    
    #else // no intrinsics
    Half2ToFloat2(result, *(u32*)half4);
    Half2ToFloat2(result + 2, *((u32*)(half4) + 1));
    #endif
}

static inline void Float4ToHalf4V(u64* result, v128f f4)
{
    #ifdef AX_SUPPORT_AVX2
    _mm_storel_pi((__m64*)result, _mm_castsi128_ps(_mm_cvtps_ph(f4, _MM_FROUND_TO_NEAREST_INT)));
    #elif defined(AX_SUPPORT_NEON)
    vst1_f16(result, vcvt_f16_f32(f4));
    #else
    STATIC_ASSERT(0, "undefined Float4ToHalf4V");
    #endif
}

static inline void Float4ToHalf4(void* result, const f32* f4)
{
    #ifdef AX_SUPPORT_AVX2
    _mm_storel_pi((__m64*)result, _mm_castsi128_ps(_mm_cvtps_ph(_mm_loadu_ps(f4), _MM_FROUND_TO_NEAREST_INT)));
    #elif defined(AX_SUPPORT_NEON)
    vst1_f16((f16*)result, vcvt_f16_f32(vld1q_f32(f4)));
    #elif defined(AX_SUPPORT_SSE) || defined(AX_SUPPORT_NEON)

    v128u IValue = VeciLoad((const unsigned int*)f4);
    v128u Sign = VeciSrl32(VeciAnd(IValue, VeciSet1(0x80000000u)), 16);
    IValue = VeciAnd(IValue, VeciSet1(0x7FFFFFFFu));      // Hack off the sign
    
    v128u mask = VeciCmpLt(IValue, VeciSet1(0x38800000u));
    v128u b = VeciAdd(IValue, VeciSet1(0xC8000000u));
    v128u a = VeciOr(VeciSet1(0x800000u), VeciAnd(IValue, VeciSet1(0x7FFFFFu)));
    a = VeciSrl(a, VeciSub(VeciSet1(113u), VeciSrl32(IValue, 23u)));
    
    IValue = VeciBlend(b, a, mask);

    v128u Result = VeciAdd(IValue, VeciSet1(0x0FFFu));
    Result = VeciAdd(Result, VeciAnd(VeciSrl32(IValue, 13u), VeciSet1(1u)));
    Result = VeciSrl32(Result, 13u);
    Result = VeciAnd(Result, VeciSet1(0x7FFFu));
    Result = VeciOr(Result, Sign);

    #ifdef AX_SUPPORT_SSE
    const int shufleMask = MakeShuffleMask(0, 2, 1, 3);
    __m128i lo = _mm_shufflelo_epi16(Result, shufleMask);
    __m128i hi = _mm_shufflehi_epi16(lo, shufleMask);
    Result = _mm_shuffle_epi32(hi, shufleMask);
    *((long long*)result) = _mm_extract_epi64(Result, 0);
    #else
    // todo test
    // Narrow the 32-bit to 16-bit, effectively extracting the lower 16 bits of each element
    uint16x4_t low16_bits = vmovn_u32(Result);  // Narrow to 16 bits per element
    // Directly cast the `uint16x4_t` to `uint64_t`
    vst1_u64((uint64_t*)result, vreinterpret_u64_u16(low16_bits));
    #endif

    #else // no intrinsics
    *(u32*)result = Float2ToHalf2(f4);
    *((u32*)result + 1) = Float2ToHalf2(f4 + 2);
    #endif
}


#ifdef AX_SUPPORT_AVX2

// convert 8 float and half with one instruction
#define Float8ToHalf8(result, float8) _mm_storeu_si128((__m128i*)result, _mm256_cvtps_ph(_mm256_loadu_ps(float8), _MM_FROUND_TO_NEAREST_INT))

#define Half8ToFloat8(result, half8)  _mm256_storeu_ps(result,  _mm256_cvtph_ps(_mm_loadu_si128((const __m128i*)half8)))

#else

purefn void Half8ToFloat8(f32* float8, const f16* half8)
{
    Half4ToFloat4(float8    , half8);
    Half4ToFloat4(float8 + 4, half8 + 4);
}

purefn void Float8ToHalf8(f16* result, const f32* float8)
{
    Float4ToHalf4(result    , float8);
    Float4ToHalf4(result + 4, float8 + 4);
}

#endif // AX_SUPPORT_AVX2

static forceinline void RandomHalf4(u64* result, u64 hash, float mn, float mx, float scale)
{
    v128f u16MaxHalf = VecSet1((1.0f / f32_(UINT16_MAX)) * scale);
    v128u vhash    = VecBitcastU32(VecLoadLo64(&hash, VecZero()));
    v128f unpacked = VecI32ToF32(VecUnpackLo32(vhash));
    unpacked = VecFmsub(unpacked, u16MaxHalf , VecOne());
    unpacked = VecClamp(unpacked, VecSet1(mn), VecSet1(mx));
    Float4ToHalf4V(result, unpacked);
}

static forceinline void RandomHalf4Positive(u64* result, u64 hash, float mn, float mx, float scale)
{
    v128f u16MaxHalf = VecSet1((1.0f / f32_(UINT16_MAX)) * scale);
    v128u vhash    = VecBitcastU32(VecLoadLo64(&hash, VecZero()));
    v128f unpacked = VecI32ToF32(VecUnpackLo32(vhash));
    unpacked = VecMul(unpacked, u16MaxHalf);
    unpacked = VecClamp(unpacked, VecSet1(mn), VecSet1(mx));
    Float4ToHalf4V(result, unpacked);
}

static forceinline void HalfToFloatN(f32* res, const f16* x, const s32 n) 
{   
    for (s32 i = 0; i < n; i += 8, x += 8, res += 8)
        Half8ToFloat8(res, x);
 
    for (s32 i = 0; i < (n & 7); i++, res++, x++)
        *res = HalfToFloat(*x);
}

static forceinline void FloatToHalfN(f16* res, const f32* x, const s32 n) 
{   
    for (s32 i = 0; i < n; i += 8, x += 8, res += 8)
        Float8ToHalf8(res, x);
 
    for (s32 i = 0; i < (n & 7); i++, res++, x++)
        *res = FloatToHalf(*x);
}

static forceinline void F3ToHalf3(f32* f, f16* res) {
    *(u32*)res = Float2ToHalf2(f); 
    res[2] = FloatToHalf(f[2]); 
}

// purefn Vec3f ConvertHalf3ToF3(half* h) {
// 	Vec3f res; 
//     ConvertHalf2ToFloat2(&res.x, *(u32*)h); 
//     res.z = ConvertHalfToFloat(h[2]); 
//     return res;
// }
