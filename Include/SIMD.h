

#ifndef SIMD_H
#define SIMD_H

// this is a helper file for working with ARM neon and SSE-SSE4.2 intrinsics, 
// But everytime writing both versions can be pain, so I’ve used macros for this purpose,.
// another reason why I’ve used macros is if I use operator overriding or functions for abstracting intrinsics,
// in debug mode, code compiles down to call instruction which is slower than instruction itself
// and macro's allows many more optimizations that compiler can do.
// a bit more explanation here: https://medium.com/@anilcangulkaya7/what-is-simd-and-how-to-use-it-3d1125faac89

#include <stdint.h>

#if defined(_MSC_VER)       /* MSVC */
#  define AX_ALIGN(N) __declspec(align(N))
#elif defined(__GNUC__)     /* GCC, Clang */
#  define AX_ALIGN(N) __attribute__((aligned(N)))
#elif defined(__INTEL_COMPILER) /* Intel C Compiler */
#  define AX_ALIGN(N) __attribute__((aligned(N)))
#else                       /* Unknown compiler, no alignment */
#  define ALIGN(N)
#endif

#if defined(__cplusplus)
extern "C" {
#endif

#if defined(__x86_64__) || defined(_M_X64)
    #define AX_X64
#elif defined(__i386) || defined(_M_IX86)
    #define AX_X86
#elif defined(_M_ARM) || defined(_M_ARM64) || defined(_M_HYBRID_X86_ARM64) || defined(_M_ARM64EC) || __arm__ || __aarch64__
    #define AX_ARM
#endif

#if defined( _M_ARM64 ) || defined( __aarch64__ ) || defined( __arm64__ ) || defined(__ARM_NEON__)
    #define AX_SUPPORT_NEON
    #include <arm_fp16.h>
#endif

#if defined(AX_ARM)
    #if defined(_MSC_VER) && !defined(__clang__) && (defined(_M_ARM64) || defined(_M_HYBRID_X86_ARM64) || defined(_M_ARM64EC) || defined(__aarch64__))
        #include <arm64_neon.h>
    #else
        #include <arm_neon.h>
    #endif
#endif

/* Intrinsics Support */
#if (defined(AX_X64) || defined(AX_X86)) && !defined(AX_ARM)
    #if defined(_MSC_VER) && !defined(__clang__)
        #if _MSC_VER >= 1400   /* 2005 */
            #define AX_SUPPORT_SSE
        #endif
        #if _MSC_VER >= 1700 && !defined(AX_NO_AVX2)   /* 2012 */
            #define AX_SUPPORT_AVX2
        #endif
    #else
        #if defined(__SSE2__) 
            #define AX_SUPPORT_SSE
        #endif
        #if defined(__AVX2__) && !defined(AX_NO_AVX2)
            #define AX_SUPPORT_AVX2
        #endif
    #endif
    
    #include <intrin.h>

    /* If at this point we still haven't determined compiler support for the intrinsics just fall back to __has_include. */
    #if !defined(__GNUC__) && !defined(__clang__) && defined(__has_include)
        #if !defined(AX_SUPPORT_SSE) && __has_include(<emmintrin.h>)
            #define AX_SUPPORT_SSE
        #endif
        #if !defined(AX_SUPPORT_AVX2) && __has_include(<immintrin.h>)
            #define AX_SUPPORT_AVX2
        #endif
    #endif

    #if defined(AX_SUPPORT_AVX2) || defined(AX_SUPPORT_AVX)
        #include <immintrin.h>
    #elif defined(AX_SUPPORT_SSE)
        #include <emmintrin.h>
    #endif
#endif


#ifdef AX_SUPPORT_AVX2
    #define SIMD_NUM_BYTES 32
#elif defined(AX_SUPPORT_SSE) || defined(AX_SUPPORT_NEON)
    #define SIMD_NUM_BYTES 16
#else
    #define SIMD_NUM_BYTES sizeof(long)
#endif


#ifdef _MSC_VER
    #define VCALL __vectorcall
#elif __CLANG__
    #define VCALL [[clang::vectorcall]] 
#elif __GNUC__
    #define VCALL  
#endif

#if defined(_MSC_VER)
    #define forceinline __forceinline
#elif defined(__clang__) || defined(__GNUC__)
    #define forceinline inline __attribute__((always_inline))
#else
    #define forceinline inline
#endif

#if defined(__clang__) || defined(__GNUC__)
    #define purefn static inline __attribute__((pure))
#elif defined(_MSC_VER)
    #define purefn static __forceinline __declspec(noalias)
#else
    #define purefn static inline __attribute__((always_inline))
#endif

typedef float    f1;
typedef double   d1;
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;

// less typing for casting, Timothy Lottes way
#define  f1_(x)  ((f1)(x))
#define  d1_(x)  ((d1)(x))
#define  u8_(x)  ((u8)(x))
#define u16_(x) ((u16)(x))
#define u32_(x) ((u32)(x))
#define u64_(x) ((u64)(x))
#define  s8_(x)  ((s8)(x))
#define s16_(x) ((s16)(x))
#define s32_(x) ((s32)(x))
#define s64_(x) ((s64)(x))


#if defined(AX_SUPPORT_SSE) && !defined(AX_ARM)
/*//////////////////////////////////////////////////////////////////////////*/
/*                                 SSE                                      */
/*//////////////////////////////////////////////////////////////////////////*/

typedef __m128  v128f;
typedef __m128  v128i;
typedef __m128i v128u;

#define VecZero()                _mm_setzero_ps()
#define VecNegZero()             _mm_set1_ps(-0.0f)
#define VecOne()                 _mm_set1_ps(1.0f)
#define VecNegativeOne()         _mm_setr_ps( -1.0f, -1.0f, -1.0f, -1.0f)
#define VecSet1(x)               _mm_set1_ps(x)
#define VecSetBytes(x)           _mm_set1_epi8(x)
                                 
#define VecSet(x, y, z, w)       _mm_set_ps(x, y, z, w)  /* -> {w, z, y, x} */
#define VecSetR(x, y, z, w)      _mm_setr_ps(x, y, z, w) /* -> {x, y, z, w} */
#define VecLoad(x)               _mm_loadu_ps(x)
#define VecLoadA(x)              _mm_load_ps(x)
#define VecLoadI(x)              _mm_load_si128(x)
#define VecLoadIU(x)             _mm_loadu_si128(x)
#define VecStoreU(ptr, x)        _mm_storeu_si128((v128u*)ptr, x)
                                
#define VecStore(ptr, x)         _mm_storeu_ps(ptr, x)
#define VecStoreA(ptr, x)        _mm_store_ps(ptr, x)


#define MakeShuffleMask(x,y,z,w)     (x | (y<<2) | (z<<4) | (w<<6)) /* internal use only */
// Get Set
// _mm_permute_ps is avx only
#define VecSplatX(v)             _mm_permute_ps(v, MakeShuffleMask(0, 0, 0, 0)) /* { v.x, v.x, v.x, v.x} */
#define VecSplatY(v)             _mm_permute_ps(v, MakeShuffleMask(1, 1, 1, 1)) /* { v.y, v.y, v.y, v.y} */
#define VecSplatZ(v)             _mm_permute_ps(v, MakeShuffleMask(2, 2, 2, 2)) /* { v.z, v.z, v.z, v.z} */
#define VecSplatW(v)             _mm_permute_ps(v, MakeShuffleMask(3, 3, 3, 3)) /* { v.w, v.w, v.w, v.w} */
                          
#define VecGetX(v)               _mm_cvtss_f32(v)             /* return v.x */
#define VecGetY(v)               _mm_cvtss_f32(VecSplatY(v))  /* return v.y */
#define VecGetZ(v)               _mm_cvtss_f32(VecSplatZ(v))  /* return v.z */
#define VecGetW(v)               _mm_cvtss_f32(VecSplatW(v))  /* return v.w */

#define VeciGetX(v)    ((u32)_mm_cvtsi128_si32(v))
#define VeciGetY(v)    ((u32)_mm_cvtsi128_si32(_mm_shuffle_epi32((v), _MM_SHUFFLE(1,1,1,1))))
#define VeciGetZ(v)    ((u32)_mm_cvtsi128_si32(_mm_shuffle_epi32((v), _MM_SHUFFLE(2,2,2,2))))
#define VeciGetW(v)    ((u32)_mm_cvtsi128_si32(_mm_shuffle_epi32((v), _MM_SHUFFLE(3,3,3,3))))

// SSE4.1                 
#define VecSetX(v, x)     ((v) = _mm_move_ss  ((v), _mm_set_ss(x)))
#define VecSetY(v, y)     ((v) = _mm_insert_ps((v), _mm_set_ss(y), 0x10))
#define VecSetZ(v, z)     ((v) = _mm_insert_ps((v), _mm_set_ss(z), 0x20))
#define VecSetW(v, w)     ((v) = _mm_insert_ps((v), _mm_set_ss(w), 0x30))

// Arithmetic
#define VecAdd(a, b)             _mm_add_ps(a, b)
#define VecSub(a, b)             _mm_sub_ps(a, b)
#define VecMul(a, b)             _mm_mul_ps(a, b)
#define VecDiv(a, b)             _mm_div_ps(a, b)
                                 
#define VecAddf(a, b)            _mm_add_ps(a, VecSet1(b))
#define VecSubf(a, b)            _mm_sub_ps(a, VecSet1(b))
#define VecMulf(a, b)            _mm_mul_ps(a, VecSet1(b))
#define VecDivf(a, b)            _mm_div_ps(a, VecSet1(b))
#define VecRound(v)              _mm_round_ps((v), _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC)

// a * b[l] + c
#define VecFmaddLane(a, b, c, l) _mm_fmadd_ps(a, _mm_permute_ps(b, MakeShuffleMask(l, l, l, l)), c)
#define VecFmadd(a, b, c)        _mm_fmadd_ps(a, b, c) /* a * b + c */
#define VecFmsub(a, b, c)        _mm_fmsub_ps(a, b, c)
#define VecNegMulSub(a, b, c)    VecFmsub(c, a, b) 
#define VecHadd(a, b)            _mm_hadd_ps(a, b) /* pairwise add (aw+bz, ay+bx, aw+bz, ay+bx) */

#define VecNeg(a)                _mm_sub_ps(_mm_setzero_ps(), a) /* -a */
#define VecRcp(a)                _mm_rcp_ps(a) /* 1.0f / a */
#define VecSqrt(a)               _mm_sqrt_ps(a)
#define VecRSqrt(a)              _mm_rsqrt_ps(a)
                                 
#define VeciNeg(a)               _mm_sub_epi32(_mm_set1_epi32(0), a) /* -a */

// Vector Math
#define VecDot(a, b)             _mm_dp_ps(a, b, 0xff)
#define VecDotf(a, b)            _mm_cvtss_f32(_mm_dp_ps(a, b, 0xff))
#define VecNorm(v)               _mm_div_ps(v, _mm_sqrt_ps(_mm_dp_ps(v, v, 0xff)))
#define VecNormEst(v)            _mm_mul_ps(_mm_rsqrt_ps(_mm_dp_ps(v, v, 0xff)), v)
#define VecLenf(v)               _mm_cvtss_f32(_mm_sqrt_ss(_mm_dp_ps(v, v, 0xff)))
#define VecLen(v)                _mm_sqrt_ps(_mm_dp_ps(v, v, 0xff))
#define VecLenSq(v)              _mm_dp_ps(v, v, 0xff)

#define Vec3DotV(a, b)           _mm_dp_ps(a, b, 0x7f)
#define Vec3DotfV(a, b)          _mm_cvtss_f32(_mm_dp_ps(a, b, 0x7f))
#define Vec3NormV(v)             _mm_div_ps(v, _mm_sqrt_ps(_mm_dp_ps(v, v, 0x7f)))
#define Vec3NormEstV(v)          _mm_mul_ps(_mm_rsqrt_ps(_mm_dp_ps(v, v, 0x7f)), v)
#define Vec3LenfV(v)             _mm_cvtss_f32(_mm_sqrt_ss(_mm_dp_ps(v, v, 0x7f)))
#define Vec3LenV(v)              _mm_sqrt_ps(_mm_dp_ps(v, v, 0x7f))

// Swizzling Masking
#define VecSelect1000  _mm_castsi128_ps(_mm_setr_epi32(0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000))
#define VecSelect1100  _mm_castsi128_ps(_mm_setr_epi32(0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000))
#define VecSelect1110  _mm_castsi128_ps(_mm_setr_epi32(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000))
#define VecSelect1011  _mm_castsi128_ps(_mm_setr_epi32(0xFFFFFFFF, 0x00000000, 0xFFFFFFFF, 0xFFFFFFFF))
#define VecSelect1111  _mm_castsi128_ps(_mm_setr_epi32(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF))

#define VecMaskXY      _mm_castsi128_ps(_mm_setr_epi32(0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000))
#define VecMask3       _mm_castsi128_ps(_mm_setr_epi32(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000))
#define VecMaskX       _mm_castsi128_ps(_mm_setr_epi32(0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000))
#define VecMaskY       _mm_castsi128_ps(_mm_setr_epi32(0x00000000, 0xFFFFFFFF, 0x00000000, 0x00000000))
#define VecMaskZ       _mm_castsi128_ps(_mm_setr_epi32(0x00000000, 0x00000000, 0xFFFFFFFF, 0x00000000))
#define VecMaskW       _mm_castsi128_ps(_mm_setr_epi32(0x00000000, 0x00000000, 0x00000000, 0xFFFFFFFF))

// vec(0, 1, 2, 3) -> (vec[x], vec[y], vec[z], vec[w])
#define VecSwizzleMask(vec, msk)    _mm_shuffle_ps(vec, vec, msk)
#define VecSwizzle(vec, x, y, z, w) VecSwizzleMask(vec, MakeShuffleMask(x,y,z,w))

// return (vec1[x], vec1[y], vec2[z], vec2[w])
#define VecShuffle(vec1, vec2, x, y, z, w)  _mm_shuffle_ps(vec1, vec2, MakeShuffleMask(x,y,z,w))
#define VecShuffleR(vec1, vec2, x, y, z, w) _mm_shuffle_ps(vec1, vec2, MakeShuffleMask(w,z,y,x))

// Special shuffle
#define VecShuffle_0101(vec1, vec2) _mm_movelh_ps(vec1, vec2)
#define VecShuffle_2323(vec1, vec2) _mm_movehl_ps(vec2, vec1)
#define VecRev(v) VecShuffle((v), (v), 3, 2, 1, 0)

// Pairwise swap (0<->1, 2<->3)
#define VecSwapPairs(v)             _mm_shuffle_ps(v,v,_MM_SHUFFLE(2,3,0,1))
#define VecSwapPairsU(a)            _mm_shuffle_epi32((a), _MM_SHUFFLE(2,3,0,1))
// Half swap (0123 -> 2301)
#define VecSwapHalves(v)            _mm_shuffle_ps(v,v,_MM_SHUFFLE(1,0,3,2))
#define VecSwapHalvesU(a)           _mm_shuffle_epi32((a), _MM_SHUFFLE(1,0,3,2))

// Logical
#define VecNot(a)                   _mm_andnot_ps(a, VecSelect1111)
#define VecAnd(a, b)                _mm_and_ps(a, b)
#define VecAndNot(a, b)             _mm_andnot_ps(a, b)
#define VecOr(a, b)                 _mm_or_ps(a, b)
#define VecXor(a, b)                _mm_xor_ps(a, b)
#define VecMask(a, msk)             _mm_and_ps(a, msk)

#define VecMax(a, b)                _mm_max_ps(a, b)
#define VecMin(a, b)                _mm_min_ps(a, b)
#define VecFloor(a)                 _mm_floor_ps(a)

#define VecCmpGt(a, b)              _mm_cmpgt_ps(a, b) /* greater than */
#define VecCmpGe(a, b)              _mm_cmpge_ps(a, b) /* greater or equal */
#define VecCmpLt(a, b)              _mm_cmplt_ps(a, b) /* less than */
#define VecCmpLe(a, b)              _mm_cmple_ps(a, b) /* less or equal */
#define VecCmpEq(a, b)              _mm_cmpeq_ps(a, b)
#define VecMovemask(a)              _mm_movemask_ps(a) /* */

#define VecSelect(V1, V2, Control)  _mm_blendv_ps(V1, V2, Control)
#define VecBlend(a, b, c)           _mm_blendv_ps(a, b, c)

//------------------------------------------------------------------------
// Veci
#define VeciZero()                  _mm_set1_epi32(0)
#define VeciSet1(x)                 _mm_set1_epi32(x)
#define VeciSet(x, y, z, w)         _mm_set_epi32(x, y, z, w)
#define VeciSetR(x, y, z, w)        _mm_setr_epi32(x, y, z, w)
#define VeciLoadA(x)                _mm_load_epi32(x)
#define VeciLoad(x)                 _mm_loadu_epi32(x)
#define VeciLoad64(qword)           _mm_loadu_si64(qword)     /* loads 64bit integer to first 8 bytes of register */
                                    
// SSE4.1                           
#define VeciSetX(v, x)              ((v) = _mm_insert_epi32((v), 0, x))
#define VeciSetY(v, y)              ((v) = _mm_insert_epi32((v), 1, y))
#define VeciSetZ(v, z)              ((v) = _mm_insert_epi32((v), 2, z))
#define VeciSetW(v, w)              ((v) = _mm_insert_epi32((v), 3, w))
                                    
#define VeciSelect1111              _mm_set1_epi32(0xFFFFFFFF)
                                    
#define VecIdentityR0               _mm_setr_ps(1.0f, 0.0f, 0.0f, 0.0f)
#define VecIdentityR1               _mm_setr_ps(0.0f, 1.0f, 0.0f, 0.0f)
#define VecIdentityR2               _mm_setr_ps(0.0f, 0.0f, 1.0f, 0.0f)
#define VecIdentityR3               _mm_setr_ps(0.0f, 0.0f, 0.0f, 1.0f)
                                    
#define VeciAdd(a, b)               _mm_add_epi32(a, b)
#define VeciSub(a, b)               _mm_sub_epi32(a, b)
#define VeciMul(a, b)               _mm_mullo_epi32(a, b)
                                    
#define VeciNot(a)                  _mm_andnot_si128(a, _mm_set1_epi32(0xFFFFFFFF))
#define VeciAnd(a, b)               _mm_and_si128(a, b)
#define VeciOr(a, b)                _mm_or_si128(a, b)
#define VeciXor(a, b)               _mm_xor_si128(a, b)
                                    
#define VeciAndNot(a, b)            _mm_andnot_si128(a, b)  /* ~a  & b */
#define VeciSrl(a, b)               _mm_srlv_epi32(a, b)    /*  a >> b */
#define VeciSll(a, b)               _mm_sllv_epi32(a, b)    /*  a << b */
#define VeciSrl32(a, b)             _mm_srli_epi32(a, b)    /*  a >> b */
#define VeciSll32(a, b)             _mm_slli_epi32(a, b)    /*  a << b */
#define VeciToVecf(a)               _mm_castsi128_ps(a)     /*  a << b */
                                    
#define VeciCmpLt(a, b)             _mm_cmplt_epi32(a, b)
#define VeciCmpLe(a, b)             _mm_cmple_epi32(a, b)
#define VeciCmpGt(a, b)             _mm_cmpgt_epi32(a, b)
#define VeciCmpGe(a, b)             _mm_cmpge_epi32(a, b)
                                    
#define VeciBlend(a, b, c)          _mm_blendv_epi8(a, b, c)
#define VecFabs(x)                  VecAnd(x, VecFromInt1(0x7fffffff))

#define VecFromInt(x, y, z, w)      _mm_castsi128_ps(_mm_setr_epi32(x, y, z, w))
#define VecFromInt1(x)              _mm_castsi128_ps(_mm_set1_epi32(x))
#define VecToInt(x) x               
                                    
#define VecBitcastU32(x)            _mm_castps_si128(x)
#define VeciBitcastF32(x)           _mm_castsi128_ps(x)
                                    
#define VecF32ToI32(x)              _mm_cvtps_epi32(x)                         /* f32[4] -> i32[4] (round) | NEON: vcvtq_s32_f32 | scalar: (int)roundf */
#define VecF32ToU32(x)              _mm_cvtps_epu32(x)                         /* f32[4] -> u32[4] (round) | NEON: vcvtq_u32_f32 */
#define VecI32ToF32(x)              _mm_cvtepi32_ps(x)                         /* i32[4] -> f32[4]         | NEON: vcvtq_f32_s32 */
#define VecU32ToF32(x)              _mm_cvtepu32_ps(x)                         /* i32[4] -> f32[4]         | NEON: vcvtq_f32_s32 */
                                                                               
#define VecZipLo32(a, b)            _mm_unpacklo_epi32(a, b)                   /* interleave low i32: [a0 b0 a1 b1] | NEON: vzip1q_s32 */
#define VecZipLo16(a, b)            _mm_unpacklo_epi16(a, b)                   /* interleave low i16  | NEON: vzip1q_s16 */
#define VecZipHi16(a, b)            _mm_unpackhi_epi16(a, b)                   /* interleave high i16 | NEON: vzip2q_s16 */
                                    
#define VecUnpackLo32(x)            _mm_unpacklo_epi16(x, _mm_setzero_si128()) /* zero-extend low 4 i16 -> i32 | NEON: vmovl_s16(vget_low_s16) */
#define VecUnpackHi32(x)            _mm_unpackhi_epi16(x, _mm_setzero_si128()) /* zero-extend high 4 i16 -> i32 | NEON: vmovl_s16(vget_high_s16) */
                                    
#define VecPack16(x)                _mm_packus_epi32((x), _mm_setzero_si128()) /* narrow u32 -> u16 (sat) | NEON: vqmovn_u32 */
                                    
#define VecStoreLo64(p, v)          _mm_storel_pi((__m64*)(p), _mm_castsi128_ps(v)) /* store lower 64 bits | NEON: vst1 */
#define VecStoreHi64(p, v)          _mm_storeh_pi((__m64*)(p), _mm_castsi128_ps(v)) /* store lower 64 bits | NEON: vst1 */
#define VecLoadLo64(p, v)           _mm_loadl_pi(v, (__m64*)(p))  /* store lower 64 bits | NEON: vst1 */
#define VecLoadHi64(p, v)           _mm_loadh_pi(v, (__m64*)(p))  /* store lower 64 bits | NEON: vst1 */

static inline v128f VCALL Vec3Load(void const* x) {
    v128f v = _mm_loadu_ps((float const*)x); 
    VecSetW(v, 0.0); return v;
}

#elif defined(AX_ARM)
/*//////////////////////////////////////////////////////////////////////////*/
/*                                 NEON                                     */
/*//////////////////////////////////////////////////////////////////////////*/

typedef float32x4_t v128f;
typedef uint32x4_t v128i;
typedef uint32x4_t v128u;

#define VecZero()                   vdupq_n_f32( 0.0f)
#define VecNegZero()                vdupq_n_f32(-0.0f)
#define VecOne()                    vdupq_n_f32( 1.0f)
#define VecNegativeOne()            vdupq_n_f32(-1.0f)
#define VecSet1(x)                  vdupq_n_f32(x)
#define VecSet (x, y, z, w)         ARMCreateVec(w, z, y, x) /* -> {w, z, y, x} */
#define VecSetR(x, y, z, w)         ARMCreateVec(x, y, z, w) /* -> {x, y, z, w} */
#define VecLoad(x)                  vld1q_f32(x)
#define VecLoadA(x)                 vld1q_f32(x)
#define VecLoadI(x)                 vld1q_s32((const s32*)x)
#define VecLoadIU(x)                vld1q_u32((const u32*)x)
#define VecStoreU(ptr, x)           vst1q_u32((u32*)ptr, x)
#define VecSetBytes(x)              vdupq_n_u8(x)
                                    
#define Vec3Load(x)                 ARMVector3Load(x)
                                    
#define VecF32ToI32(x)              vcvtq_s32_f32(x)             /* f32[4] -> i32[4] round */
#define VecF32ToU32(x)              vcvtq_u32_f32(x)             /* f32[4] -> u32[4] round */
#define VecI32ToF32(x)              vcvtq_f32_s32(x)             /* i32[4] -> f32[4] */
#define VecU32ToF32(x)              vcvtq_f32_u32(x)             /* i32[4] -> f32[4] */
                                    
#define VecZipLo32(a, b)            vzip1q_s32(a, b)             /* interleave low i32: [a0 b0 a1 b1] */
#define VecZipLo16(a, b)            vzip1q_s16(a, b)             /* interleave low i16 lanes */
#define VecZipHi16(a, b)            vzip2q_s16(a, b)             /* interleave high i16 lanes */
                                    
#define VecUnpackLo32(x)            vmovl_s16(vget_low_s16(x))   /* zero-extend low 4 i16 -> i32 */
#define VecUnpackHi32(x)            vmovl_s16(vget_high_s16(x))  /* zero-extend high 4 i16 -> i32 */

#define VecPack16(x)                vqmovn_u32(x)                /* narrow u32 -> u16 with saturation */
#define VecStoreLo64(p, v)          vst1_s16((int16_t*)(p), vget_low_s16(v)) /* store lower 64 bits */
#define VecStoreHi64(p, v)          vst2_s16((int16_t*)(p), vget_high_s16(v)) /* store higher 64 bits */
#define VecLoadLo64(p, v)           vld1_s16((int16_t*)(p)) /* store lower 64 bits */
#define VecLoadHi64(p, v)           vld2_s16((int16_t*)(p)) /* store higher 64 bits */

// Get Set                          
#define VecSplatX(v)                vdupq_lane_f32(vget_low_f32(v), 0)
#define VecSplatY(v)                vdupq_lane_f32(vget_low_f32(v), 1)
#define VecSplatZ(v)                vdupq_lane_f32(vget_high_f32(v), 0)
#define VecSplatW(v)                vdupq_lane_f32(vget_high_f32(v), 1)
                                    
#define VecGetX(v)                  vgetq_lane_f32(v, 0)
#define VecGetY(v)                  vgetq_lane_f32(v, 1)
#define VecGetZ(v)                  vgetq_lane_f32(v, 2)
#define VecGetW(v)                  vgetq_lane_f32(v, 3)
                                    
#define VeciGetX(v)                 vgetq_lane_u32((v), 0)
#define VeciGetY(v)                 vgetq_lane_u32((v), 1)
#define VeciGetZ(v)                 vgetq_lane_u32((v), 2)
#define VeciGetW(v)                 vgetq_lane_u32((v), 3)

#define VecSetX(v, x)               ((v) = vsetq_lane_f32(x, v, 0))
#define VecSetY(v, y)               ((v) = vsetq_lane_f32(y, v, 1))
#define VecSetZ(v, z)               ((v) = vsetq_lane_f32(z, v, 2))
#define VecSetW(v, w)               ((v) = vsetq_lane_f32(w, v, 3))
                                    
// Arithmetic                       
#define VecAdd(a, b)                vaddq_f32(a, b)
#define VecSub(a, b)                vsubq_f32(a, b)
#define VecMul(a, b)                vmulq_f32(a, b)
#define VecDiv(a, b)                ARMVectorDevide(a, b)
                                              
#define VecAddf(a, b)               vaddq_f32(a, vdupq_n_f32(b))
#define VecSubf(a, b)               vsubq_f32(a, vdupq_n_f32(b))
#define VecMulf(a, b)               vmulq_n_f32(a, b)
#define VecDivf(a, b)               ARMVectorDevide(a, VecSet1(b))
#define VecRound(v)                 vrndnq_f32(v)   // round to nearest int (float output)
                                    
// a * b[l] + c                     
#define VecFmaddLane(a, b, c, l)    vfmaq_laneq_f32(c, a, b, l)
#define VecFmadd(a, b, c)           vfmaq_f32(c, a, b)
#define VecFmsub(a, b, c)           vnegq_f32(vfmsq_f32(c, a, b))
#define VecNegMulSub(a, b, c)       VecFmsub(c, a, b) 
#define VecHadd(a, b)               vpaddq_f32(a, b)
#define VecSqrt(a)                  vsqrtq_f32(a)
#define VecRcp(a)                   vrecpeq_f32(a)
#define VecNeg(a)                   vnegq_f32(a)
                                    
// Vector Math                      
#define VecDot(a, b)                ARMVectorDot(a, b)
#define VecDotf(a, b)               VecGetX(ARMVectorDot(a, b))
#define VecNorm(v)                  ARMVectorNorm(v)
#define VecNormEst(v)               ARMVectorNormEst(v)
#define VecLenf(v)                  VecGetX(ARMVectorLength(v))
#define VecLen(v)                   ARMVectorLength(v)
#define VecLenSq(v)                 ARMVectorSqrLength(v)
                                    
#define Vec3DotV(a, b)              ARMVector3Dot(a, b)
#define Vec3DotfV(a, b)             VecGetX(ARMVector3Dot(a, b))
#define Vec3NormV(v)                ARMVector3Norm(v)
#define Vec3NormEstV(v)             ARMVector3NormEst(v)
#define Vec3LenfV(v)                VecGetX(ARMVector3Length(v))
#define Vec3LenV(v)                 ARMVector3Length(v)

#define ARMVectorSwizzle(E0, E1, E2, E3, v) \
    VecSetR( \
        vgetq_lane_f32((v), (E0)), \
        vgetq_lane_f32((v), (E1)), \
        vgetq_lane_f32((v), (E2)), \
        vgetq_lane_f32((v), (E3))  \
    )

#define ARMVectorShuffle(E0, E1, E2, E3, v0, v1) \
    VecSetR( \
        vgetq_lane_f32((v0), (E0)), \
        vgetq_lane_f32((v0), (E1)), \
        vgetq_lane_f32((v1), (E2)), \
        vgetq_lane_f32((v1), (E3))  \
    )

#define ARMVectorU32Swizzle(E0, E1, E2, E3, v) \
VecSetR( \
    vgetq_lane_u32((v), (E0)), \
    vgetq_lane_u32((v), (E1)), \
    vgetq_lane_u32((v), (E2)), \
    vgetq_lane_u32((v), (E3))  \
)

#define ARMVectorU32Shuffle(E0, E1, E2, E3, v0, v1) \
    VecSetR( \
        vgetq_lane_f32((v0), (E0)), \
        vgetq_lane_f32((v0), (E1)), \
        vgetq_lane_f32((v1), (E2)), \
        vgetq_lane_f32((v1), (E3))  \
    )


// vec(0, 1, 2, 3) -> (vec[x], vec[y], vec[z], vec[w])
#define VecSwizzle(vec, x, y, z, w)         ARMVectorSwizzle(x, y, z, w, vec)

#define VecShuffle(vec1, vec2, x, y, z, w)  ARMVectorShuffle(x, y, z, w, vec1, vec2)
#define VecShuffleR(vec1, vec2, x, y, z, w) ARMVectorShuffle(w, z, y, x, vec1, vec2)

// special shuffle
#define VecShuffle_0101(vec1, vec2) vcombine_f32(vget_low_f32(vec1), vget_low_f32(vec2))
#define VecShuffle_2323(vec1, vec2) vcombine_f32(vget_high_f32(vec1), vget_high_f32(vec2))
#define VecRev(v) ARMVectorRev(v)
// Pairwise swap (0<->1, 2<->3)
#define VecSwapPairs(v)             vrev64q_f32(v)
#define VecSwapPairsU(a)            vrev64q_u32(a)
// Half swap (0123 -> 2301)         
#define VecSwapHalves(v)            vextq_f32(v,v,2)
#define VecSwapHalvesU(a)           vcombine_u32(vget_high_u32(a), vget_low_u32(a))

#define VecNot(a)                   vreinterpretq_f32_u32(vmvnq_u32(vreinterpretq_u32_f32(a)))
#define VecAnd(a, b)                vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(a), b))
#define VecAndNot(a, b)             vreinterpretq_f32_u32(vandnotq_u32(vreinterpretq_u32_f32(a), b))
#define VecOr(a, b)                 vreinterpretq_f32_u32(vorrq_u32(vreinterpretq_u32_f32(a), b))
#define VecXor(a, b)                vreinterpretq_f32_u32(veorq_u32(vreinterpretq_u32_f32(a), b))
                                    
#define VecMask(a, msk)             VecSelect(vdupq_n_f32(0.0f), a, msk)
                                    
#define VecMax(a, b)                vmaxq_f32(a, b)
#define VecMin(a, b)                vminq_f32(a, b)
#define VecFloor(a)                 vrndmq_f32(a)
                                    
#define VecCmpGt(a, b)              vcgtq_f32(a, b) // greater or equal
#define VecCmpGe(a, b)              vcgeq_f32(a, b) // greater or equal
#define VecCmpLt(a, b)              vcltq_f32(a, b) // less than
#define VecCmpLe(a, b)              vcleq_f32(a, b) // less or equal
#define VecCmpEq(a, b)              vceqq_f32(a, b) // less or equal
#define VecMovemask(a)              ARMVecMovemask(a) /* not done */

#define VecSelect(V1, V2, Control)  vbslq_f32(Control, V2, V1)
#define VecBlend(a, b, Control)     vbslq_f32(Control, b, a)

//------------------------------------------------------------------------
// Veci
#define VeciZero()                  vdupq_n_u32(0)
#define VeciSet1(x)                 vdupq_n_u32(x)
#define VeciSetR(x, y, z, w)        ARMCreateVecI(x, y, z, w)
#define VeciSet(x, y, z, w)         ARMCreateVecI(w, z, y, x)
#define VeciLoadA(x)                vld1q_u32(x)
#define VeciLoad(x)                 vld1q_u32(x)
#define VeciLoad64(qword)           vcombine_u32(vcreate_u32(qword), vcreate_u32(0ull)) /* loads 64bit integer to first 8 bytes of register */

#define VeciSetX(v, x)       ((v) = vsetq_lane_u32(x, v, 0))
#define VeciSetY(v, y)       ((v) = vsetq_lane_u32(y, v, 1))
#define VeciSetZ(v, z)       ((v) = vsetq_lane_u32(z, v, 2))
#define VeciSetW(v, w)       ((v) = vsetq_lane_u32(w, v, 3))

#define VeciAdd(a, b)               vaddq_u32(a, b)
#define VeciSub(a, b)               vsubq_u32(a, b)
#define VeciMul(a, b)               vmulq_u32(a, b)

#define VecBitcastU32(x)            vreinterpretq_f32_u32(x)
#define VeciBitcastF32(x)           vreinterpretq_u32_f32(x)
// Swizzling Masking
#define VecSelect1000  ARMCreateVecI(0xFFFFFFFFu, 0x00000000u, 0x00000000u, 0x00000000u)
#define VecSelect1100  ARMCreateVecI(0xFFFFFFFFu, 0xFFFFFFFFu, 0x00000000u, 0x00000000u)
#define VecSelect1110  ARMCreateVecI(0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0x00000000u)
#define VecSelect1011  ARMCreateVecI(0xFFFFFFFFu, 0x00000000u, 0xFFFFFFFFu, 0xFFFFFFFFu)
#define VecSelect1111  ARMCreateVecI(0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu)

#define VeciSelect1111 ARMCreateVecI(0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu)

#define VecIdentityR0  ARMCreateVec(1.0f, 0.0f, 0.0f, 0.0f)
#define VecIdentityR1  ARMCreateVec(0.0f, 1.0f, 0.0f, 0.0f)
#define VecIdentityR2  ARMCreateVec(0.0f, 0.0f, 1.0f, 0.0f)
#define VecIdentityR3  ARMCreateVec(0.0f, 0.0f, 0.0f, 1.0f)

#define VecMaskXY      ARMCreateVecI(0xFFFFFFFFu, 0xFFFFFFFFu, 0x00000000u, 0x00000000u)
#define VecMask3       ARMCreateVecI(0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0x00000000u)
#define VecMaskX       ARMCreateVecI(0xFFFFFFFFu, 0x00000000u, 0x00000000u, 0x00000000u)
#define VecMaskY       ARMCreateVecI(0x00000000u, 0xFFFFFFFFu, 0x00000000u, 0x00000000u)
#define VecMaskZ       ARMCreateVecI(0x00000000u, 0x00000000u, 0xFFFFFFFFu, 0x00000000u)
#define VecMaskW       ARMCreateVecI(0x00000000u, 0x00000000u, 0x00000000u, 0xFFFFFFFFu)

// Logical
#define VeciNot(a)                  vmvnq_u32(a)
#define VeciAnd(a, b)               vandq_u32(a, b)
#define VeciOr(a, b)                vorrq_u32(a, b)
#define VeciXor(a, b)               veorq_u32(a, b)
                                    
#define VeciAndNot(a, b)            vandq_u32(vmvnq_u32(a), b)  /* ~a & b */
#define VeciSrl(a, b)               vshlq_u32(a, vnegq_s32(b))  /* a >> b */
#define VeciSll(a, b)               vshlq_u32(a, b)             /* a << b */
#define VeciSrl32(a, b)             vshrq_n_u32(a, b)           /* a >> b */
#define VeciSll32(a, b)             vshlq_n_u32(a, b)           /* a << b */
#define VeciToVecf(a)               vreinterpretq_f32_s32(a)    /* Reinterpret int as float */
                                    
#define VeciCmpLt(a, b)             vcltq_u32(a, b)
#define VeciCmpLe(a, b)             vclte_u32(a, b)
#define VeciCmpGt(a, b)             vcgtq_u32(a, b)
#define VeciCmpGe(a, b)             vcgeq_u32(a, b)

#define VeciBlend(a, b, c)          vbslq_u8(c, b, a)  /* Blend a and b based on mask c */
#define VecFabs(x)                  vabsq_f32(x)

purefn v128f ARMVectorRev(v128f v)
{
    float32x4_t rev64 = vrev64q_f32(v);
    return vextq_f32(rev64, rev64, 2);
}

purefn v128f ARMVector3Load(float* src) {
    return vcombine_f32(vld1_f32(src), vld1_lane_f32(src + 2, vdup_n_f32(0), 0));
}

purefn v128f ARMCreateVec(float x, float y, float z, float w) {
    AX_ALIGN(16) float v[4] = {x, y, z, w};
    return vld1q_f32(v);
}

purefn v128i ARMCreateVecI(u32 x, u32 y, u32 z, u32 w) {
    return vcombine_u32(vcreate_u32(((uint64_t)x) | (((uint64_t)y) << 32)),
                        vcreate_u32(((uint64_t)z) | (((uint64_t)w) << 32)));
}

purefn v128f ARMVector3NormEst(v128f v) {
    float32x4_t vTemp = vmulq_f32(v, v);
    float32x2_t v1 = vget_low_f32(vTemp);
    float32x2_t v2 = vget_high_f32(vTemp);
    v1 = vpadd_f32(v1, v1);
    v2 = vdup_lane_f32(v2, 0);
    v1 = vadd_f32(v1, v2); // Dot3
    v2 = vrsqrte_f32(v1); // Reciprocal sqrt (estimate)
    return vmulq_f32(v, vcombine_f32(v2, v2)); // Normalize
}

static inline v128f ARMVector3Norm(v128f v) {
    // Dot3
    float32x4_t vTemp = vmulq_f32(v, v);
    float32x2_t v1 = vget_low_f32(vTemp);
    float32x2_t v2 = vget_high_f32(vTemp);
    v1 = vpadd_f32(v1, v1);
    v2 = vdup_lane_f32(v2, 0);
    v1 = vadd_f32(v1, v2);
    uint32x2_t VEqualsZero = vceq_f32(v1, vdup_n_f32(0));
    // Reciprocal sqrt (2 iterations of Newton-Raphson)
    float32x2_t S0 = vrsqrte_f32(v1);
    float32x2_t P0 = vmul_f32(v1, S0);
    float32x2_t R0 = vrsqrts_f32(P0, S0);
    float32x2_t S1 = vmul_f32(S0, R0);
    float32x2_t R1 = vrsqrts_f32(vmul_f32(v1, S1), S1);
    v2 = vmul_f32(S1, R1);
    // Normalize
    v128f vResult = vmulq_f32(v, vcombine_f32(v2, v2));
    vResult = vbslq_f32(vcombine_u32(VEqualsZero, VEqualsZero), vdupq_n_f32(0), vResult);
    return vResult;
}

purefn v128f ARMVector3Dot(v128f a, v128f b) {
    float32x4_t vTemp = vmulq_f32(a, b);
    float32x2_t v1 = vget_low_f32(vTemp);
    float32x2_t v2 = vget_high_f32(vTemp);
    v1 = vpadd_f32(v1, v1);
    v2 = vdup_lane_f32(v2, 0);
    v1 = vadd_f32(v1, v2);
    return vcombine_f32(v1, v1);
}

static inline v128f ARMVector3Length(v128f v) {
    float32x4_t vTemp = vmulq_f32(v, v);
    float32x2_t v1 = vget_low_f32(vTemp);
    float32x2_t v2 = vget_high_f32(vTemp);
    v1 = vpadd_f32(v1, v1);
    v2 = vdup_lane_f32(v2, 0);
    v1 = vadd_f32(v1, v2);
    const float32x2_t zero = vdup_n_f32(0);
    uint32x2_t VEqualsZero = vceq_f32(v1, zero);
    float32x2_t Result = vrsqrte_f32(v1);
    Result = vmul_f32(v1, Result);
    Result = vbsl_f32(VEqualsZero, zero, Result);
    return vcombine_f32(Result, Result);
}

static inline v128f ARMVectorLengthEst(v128f v) {
    float32x4_t vTemp = vmulq_f32(v, v);
    float32x2_t v1 = vget_low_f32(vTemp);
    float32x2_t v2 = vget_high_f32(vTemp);
    v1 = vadd_f32(v1, v2);
    v1 = vpadd_f32(v1, v1);
    const float32x2_t zero = vdup_n_f32(0);
    uint32x2_t VEqualsZero = vceq_f32(v1, zero);
    float32x2_t Result = vrsqrte_f32(v1);
    Result = vmul_f32(v1, Result);
    Result = vbsl_f32(VEqualsZero, zero, Result);
    return vcombine_f32(Result, Result);
}

static inline v128f ARMVectorSqrLength(v128f v) {
	// Dot4
    float32x4_t vTemp = vmulq_f32(v, v);
    float32x2_t v1 = vget_low_f32(vTemp);
    float32x2_t v2 = vget_high_f32(vTemp);
    v1 = vadd_f32(v1, v2);
    v1 = vadd_f32(v1, vrev64_f32(v1));
    return vcombine_f32(v1, v1);
}

static inline v128f ARMVectorLength(v128f v) {
	// Dot4
    float32x4_t vTemp = vmulq_f32(v, v);
    float32x2_t v1 = vget_low_f32(vTemp);
    float32x2_t v2 = vget_high_f32(vTemp);
    v1 = vadd_f32(v1, v2);
    v1 = vpadd_f32(v1, v1);
    const float32x2_t zero = vdup_n_f32(0);
    uint32x2_t VEqualsZero = vceq_f32(v1, zero);
    // Sqrt
    float32x2_t S0 = vrsqrte_f32(v1);
    float32x2_t P0 = vmul_f32(v1, S0);
    float32x2_t R0 = vrsqrts_f32(P0, S0);
    float32x2_t S1 = vmul_f32(S0, R0);
    float32x2_t P1 = vmul_f32(v1, S1);
    float32x2_t R1 = vrsqrts_f32(P1, S1);
    float32x2_t Result = vmul_f32(S1, R1);
    Result = vmul_f32(v1, Result);
    Result = vbsl_f32(VEqualsZero, zero, Result);
    return vcombine_f32(Result, Result);
}

purefn v128f ARMVectorDevide(v128f V1, v128f V2) {
    // 2 iterations of Newton-Raphson refinement of reciprocal
    float32x4_t Reciprocal = vrecpeq_f32(V2);
    float32x4_t S = vrecpsq_f32(Reciprocal, V2);
    Reciprocal = vmulq_f32(S, Reciprocal);
    S = vrecpsq_f32(Reciprocal, V2);
    Reciprocal = vmulq_f32(S, Reciprocal);
    return vmulq_f32(V1, Reciprocal);
}

purefn v128f ARMVectorDot(v128f a, v128f b) {
    float32x4_t vTemp = vmulq_f32(a, b);
    float32x2_t v1 = vget_low_f32(vTemp);
    float32x2_t v2 = vget_high_f32(vTemp);
    v1 = vadd_f32(v1, v2);
    v1 = vpadd_f32(v1, v1);
    return vcombine_f32(v1, v1);
}

purefn v128f ARMVectorNormEst(v128f v) {
    float32x4_t vTemp = vmulq_f32(v, v);
    float32x2_t v1 = vget_low_f32(vTemp);
    float32x2_t v2 = vget_high_f32(vTemp);
    v1 = vadd_f32(v1, v2);
    v1 = vpadd_f32(v1, v1);
    v2 = vrsqrte_f32(v1);
    return vmulq_f32(v, vcombine_f32(v2, v2));
}

purefn v128f ARMVectorNorm(v128f v) 
{
    return ARMVectorDevide(v, ARMVectorLength(v));
}

purefn int ARMVecMovemask(v128i v) {
    const int shiftArr[4] = { 0, 1, 2, 3 };
    int32x4_t shift = vld1q_s32(shiftArr);
    return vaddvq_u32(vshlq_u32(vshrq_n_u32(v, 31), shift));
}

#endif

#if defined(AX_SUPPORT_AVX2)
typedef __m256  v256f;
typedef __m256i v256i;
typedef __m256i v256u;

#define VecLoadI256(ptr)     _mm256_stream_load_si256((__m256i const *)ptr)
#define VecStoreI256(ptr, x) _mm256_stream_si256((__m256i *)ptr, x)

#define VeciAndNot256(x, y)  _mm256_andnot_si256(x, y)
#define VeciAnd256(x, y)     _mm256_and_si256(x, y)
#define VeciOr256(x, y)      _mm256_or_si256(x, y)
#define VeciXor256(x, y)     _mm256_xor_si256(x, y)

#define VeciSrl256(a, b)     _mm256_srlv_epi32(a, b)    /*  a >> b */
#define VeciSll256(a, b)     _mm256_sllv_epi32(a, b)    /*  a << b */
#define VeciSrl32_256(a, b)  _mm256_srli_epi32(a, b)    /*  a >> b */
#define VeciSll32_256(a, b)  _mm256_slli_epi32(a, b)
#define VeciSet1_256(x)      _mm256_set1_epi32(x)

#define VeciAdd256(a, b)     _mm256_add_epi32(a, b)
#define VeciSub256(a, b)     _mm256_sub_epi32(a, b)
#define VeciMul256(a, b)     _mm256_mullo_epi32(a, b)
#define VeciDiv256(a, b)     _mm256_div_epi32(a, b)

#define VecSetBytes256(x)    _mm256_set1_epi8(x)

#else
typedef struct Vec8x32f_ { v128f lo, hi; } v256f;
typedef struct Vec8x32i_ { v128i lo, hi; } v256i;
typedef struct Vec8x32u_ { v128u lo, hi; } v256u;

#define VeciAndNot256(x, y)   (v256u){ VeciAndNot((x).lo, (y).lo), VeciAndNot((x).hi, (y).hi) }
#define VeciAnd256(x, y)      (v256u){ VeciAnd   ((x).lo, (y).lo), VeciAnd   ((x).hi, (y).hi) }
#define VeciOr256(x, y)       (v256u){ VeciOr    ((x).lo, (y).lo), VeciOr    ((x).hi, (y).hi) }
#define VeciXor256(x, y)      (v256u){ VeciXor   ((x).lo, (y).lo), VeciXor   ((x).hi, (y).hi) }
#define VeciSrl256(a, b)      (v256u){ VeciSrl   ((a).lo, (b).lo), VeciSrl   ((a).hi, (b).hi) }
#define VeciSll256(a, b)      (v256u){ VeciSll   ((a).lo, (b).lo), VeciSll   ((a).hi, (b).hi) }
#define VeciSrl32_256(a, b)   (v256u){ VeciSrl32 ((a).lo, (b).lo), VeciSrl32 ((a).hi, (b).hi) }
#define VeciSll32_256(a, b)   (v256u){ VeciSll32 ((a).lo, (b).lo), VeciSll32 ((a).hi, (b).hi) }
#define VeciAdd256(a, b)      (v256u){ VeciAdd   ((a).lo, (b).lo), VeciAdd   ((a).hi, (b).hi) }
#define VeciSub256(a, b)      (v256u){ VeciSub   ((a).lo, (b).lo), VeciSub   ((a).hi, (b).hi) }
#define VeciMul256(a, b)      (v256u){ VeciMul   ((a).lo, (b).lo), VeciMul   ((a).hi, (b).hi) }
#define VeciDiv256(a, b)      (v256u){ VeciDiv   ((a).lo, (b).lo), VeciDiv   ((a).hi, (b).hi) }

#define VecLoadI256(ptr)      (v256u){ VecLoadI(ptr)    , VecLoadI((char*)(ptr) + 16)     }
#define VecStoreI256(ptr, x)  (v256u){ VecStoreI(ptr, x), VecStoreI((char*)(ptr) + 16, x) }
#define VeciSet1_256(x)       (v256u){ VeciSet1(x)      , VeciSet1(x)                     }
#define VecSetBytes256(x)     (v256u){ VecSetBytes(x)   , VecSetBytes(x)                  }

#endif
typedef v128f f4;
typedef v128i i4;

// shared 
purefn f4 VCALL Vec3DistV    (f4 a, f4 b) { f4 x = VecSub(a, b); return Vec3LenV(x);  } 
purefn f1 VCALL Vec3DistfV   (f4 a, f4 b) { f4 x = VecSub(a, b); return Vec3LenfV(x); } 
purefn f1 VCALL Vec3DistSqrfV(f4 a, f4 b) { f4 x = VecSub(a, b); return Vec3DotfV(x, x); } 

purefn v128f VCALL VecClamp(v128f v, v128f vmin, v128f vmax)
{
    v = VecSelect(v, vmax, VecCmpGt(v, vmax));
    return VecSelect(v, vmin, VecCmpLt(v, vmin));
}

purefn u32 VCALL VecMaxElement(v128f a)
{
    v128f t = VecSwapPairs(a);
    v128f m = VecMax(a, t);
    t = VecSwapHalves(m);
    v128f max_val = VecMax(m, t);
    u32 mask = (u32)VecMovemask(VecCmpGe(a, max_val));
    return TrailingZeroCount32(mask);
}

#if defined(AX_SUPPORT_SSE) || defined(AX_ARM)
static inline float VCALL VecGetN(v128f v, int n)
{
    return ((float*)&v)[n & 3];
}

static inline v128f VCALL VecSetN(v128f v, int n, float f)
{
    ((float*)&v)[n & 3] = f;
    return v;
}

static inline int VCALL VeciGetN(v128f v, int n)
{
    return ((int*)&v)[n & 3];
}

static inline v128i VCALL VeciSetN(v128i v, int n, int i)
{
    ((int*)&v)[n & 3] = i;
    return v;
}
#endif 

#if defined(__cplusplus)
}
#endif

#endif // simd.h