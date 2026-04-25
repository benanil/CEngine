#ifndef SINFL_H_INCLUDED
#define SINFL_H_INCLUDED

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SINFL_LIT_TABLE_BITS    12
#define SINFL_DIST_TABLE_BITS   9
#define SINFL_LIT_TABLE_SIZE    (1 << SINFL_LIT_TABLE_BITS)
#define SINFL_DIST_TABLE_SIZE   (1 << SINFL_DIST_TABLE_BITS)

/* Structure aligned to cache lines for maximum L1 performance */
#if defined(__GNUC__) || defined(__clang__)
  #define SINFL_ALIGN __attribute__((aligned(64)))
#else
  #define SINFL_ALIGN __declspec(align(64))
#endif

struct SINFL_ALIGN sinfl_state {
  const unsigned char *in;
  unsigned long long bit_buf;
  int bits_left;
  unsigned *lits;
  unsigned *dists;

  unsigned lit_mask;
  unsigned dist_mask;
  unsigned fix_lit_mask;
  unsigned fix_dist_mask;

  unsigned dyn_lits[SINFL_LIT_TABLE_SIZE + 2500];
  unsigned dyn_dists[SINFL_DIST_TABLE_SIZE + 2500];
  unsigned fix_lits[SINFL_LIT_TABLE_SIZE + 2500];
  unsigned fix_dists[SINFL_DIST_TABLE_SIZE + 2500];
  int init_fix;
};
size_t sinflate(struct sinfl_state *state, void *out, size_t out_cap, const void *in, size_t in_size);
size_t zsinflate(void *out, size_t cap, const void *mem, size_t size);

#ifdef __cplusplus
}
#endif

#endif

#ifdef SINFL_IMPLEMENTATION

#define ADLER_MOD               65521
#define ADLER_NMAX              5552 
#define SINFL_ADLER_INIT        (1)

#if !defined(SINFL_X64) && !defined(SINFL_ARM64)
  #if defined(__x86_64__) || defined(_M_X64)
    #define SINFL_X64
  #elif defined(__aarch64__) || defined(_M_ARM64)
    #define SINFL_ARM64
  #endif
#endif

#ifdef SINFL_X64
  #include <immintrin.h>
  #if defined(__BMI2__) || (defined(_MSC_VER) && defined(__AVX2__))
    #define SINFL_BMI2
  #endif
  #if defined(__AVX2__)
    #define SINFL_USE_AVX2
  #elif defined(__SSE4_2__) || defined(_M_X64)
    #define SINFL_USE_SSE42
  #endif
#elif defined(SINFL_ARM64)
  #include <arm_neon.h>
  #define SINFL_USE_NEON
#endif

#if defined(__clang__) || defined(__GNUC__)
  #define SINFL_FORCE_INLINE inline __attribute__((always_inline))
  #define SINFL_EXPECT(x, y) __builtin_expect(!!(x), y)
  #define SINFL_ALIGNED_16 __attribute__((aligned(16)))
#elif defined(_MSC_VER)
  #include <intrin.h>
  #define SINFL_FORCE_INLINE __forceinline
  #define SINFL_EXPECT(x, y) (x)
  #define SINFL_ALIGNED_16 __declspec(align(16))
#else
  #define SINFL_FORCE_INLINE inline
  #define SINFL_EXPECT(x, y) (x)
  #define SINFL_ALIGNED_16
#endif

enum sinfl_type {
  SINFL_TYPE_LIT = 0,
  SINFL_TYPE_MATCH = 1,
  SINFL_TYPE_EOB = 2,
  SINFL_TYPE_SUB = 3,
  SINFL_TYPE_DUAL = 4,
  SINFL_TYPE_ERR = 15,
};
static SINFL_FORCE_INLINE unsigned short
SINFL_BITREV16(unsigned short x) {
#if defined(__clang__)
  return __builtin_bitreverse16(x);
#else
  x = ((x & 0xAAAA) >> 1) | ((x & 0x5555) << 1);
  x = ((x & 0xCCCC) >> 2) | ((x & 0x3333) << 2);
  x = ((x & 0xF0F0) >> 4) | ((x & 0x0F0F) << 4);
  return (unsigned short)((x >> 8) | (x << 8));
#endif
}
static SINFL_FORCE_INLINE unsigned long long
sinfl_load64(const void *p) {
  unsigned long long v;
  memcpy(&v, p, 8);
  return v;
}
/* --- Core Primitives --- */
#define SINFL_REFILL() do { \
    bit_buf |= (sinfl_load64(in) << bits_left); \
    in += 7; \
    in -= (bits_left >> 3) & 7; \
    bits_left |= 56; \
  } while(0)

#define SINFL_REFILL_SAFE() do { \
    if (SINFL_EXPECT(bits_left < 0, 0)) return -1; \
    unsigned long long _nv = 0; \
    ptrdiff_t _left = (const unsigned char*)in_buf + in_size - in; \
    if (SINFL_EXPECT(_left >= 8, 1)) { \
      _nv = sinfl_load64(in); \
    } else if (_left > 0) { \
      memcpy(&_nv, in, _left); \
    } else { \
      _left = 0; \
    } \
    bit_buf |= (_nv << bits_left); \
    unsigned _ba = (unsigned)((63 - bits_left) >> 3); \
    if ((ptrdiff_t)_ba > _left) _ba = (unsigned)_left; \
    in += _ba;\
    bits_left += (_ba << 3); \
  } while(0)

#ifdef SINFL_BMI2
  #define SINFL_EXTRACT(val, shift, len) ((unsigned)_bzhi_u64((val) >> (shift), (len)))
#else
  #define SINFL_EXTRACT(val, shift, len) ((unsigned)(((val) >> (shift)) & ((1ULL << (len)) - 1)))
#endif

static SINFL_FORCE_INLINE unsigned
sinfl_rev(unsigned code, unsigned L) {
#if defined(SINFL_ARM64) && (defined(__GNUC__) || defined(__clang__))
  return __builtin_arm_rbit(code) >> (32 - L);
#else
  return (unsigned)SINFL_BITREV16((unsigned short)code) >> (16 - L);
#endif
}
static SINFL_FORCE_INLINE void
sinfl_copy_match(unsigned char *out, int dist, int len) {
  const unsigned char *src = out - dist;
  unsigned char *const end = out + len;
  /* 1. MAIN PATH: Distance >= 8 */
  if (SINFL_EXPECT(dist >= 8, 1)) {
    /* Fast path for short lengths (<= 16 bytes). 
     * We unconditionally copy 16 bytes using 64-bit GPRs. 
     * This eliminates the loop branch entirely for the vast majority of matches. */
    if (SINFL_EXPECT(len <= 16, 1)) {
      uint64_t v1, v2;
      memcpy(&v1, src, 8); memcpy(out, &v1, 8);
      memcpy(&v2, src + 8, 8); memcpy(out + 8, &v2, 8);
      return;
    }
    /* Long match, Long distance: Unleash SIMD throughput */
    if (dist >= 16) {
#if defined(SINFL_USE_NEON)
      do {
        vst1q_u8(out, vld1q_u8(src));
        vst1q_u8(out + 16, vld1q_u8(src + 16));
        out += 32; src += 32;
      } while (out < end);
#elif defined(SINFL_USE_AVX2)
      do {
        _mm256_storeu_si256((__m256i*)out, _mm256_loadu_si256((const __m256i*)src));
        out += 32; src += 32;
      } while (out < end);
#elif defined(SINFL_USE_SSE42)
      do {
        _mm_storeu_si128((__m128i*)out, _mm_loadu_si128((const __m128i*)src));
        _mm_storeu_si128((__m128i*)(out + 16), _mm_loadu_si128((const __m128i*)(src + 16)));
        out += 32; src += 32;
      } while (out < end);
#else
      do {
        uint64_t v1, v2, v3, v4;
        memcpy(&v1, src, 8); memcpy(out, &v1, 8);
        memcpy(&v2, src + 8, 8); memcpy(out + 8, &v2, 8);
        memcpy(&v3, src + 16, 8); memcpy(out + 16, &v3, 8);
        memcpy(&v4, src + 24, 8); memcpy(out + 24, &v4, 8);
        out += 32;
        src += 32;
      } while (out < end);
#endif
      return;
    }
    /* Long match, Medium distance (8-15): 8-byte loop to prevent overlap */
    do {
      uint64_t v;
      memcpy(&v, src, 8); memcpy(out, &v, 8);
      out += 8; src += 8;
    } while (out < end);
    return;
  }
  /* 2. RLE PATH: Distance == 1 */
  if (SINFL_EXPECT(dist == 1, 0)) {
    uint64_t v = *src * 0x0101010101010101ULL;
    if (SINFL_EXPECT(len <= 16, 1)) {
      memcpy(out, &v, 8); memcpy(out + 8, &v, 8);
      return;
    }
#if defined(SINFL_USE_NEON)
    uint8x16_t v16 = vdupq_n_u8((uint8_t)v);
    do {
      vst1q_u8(out, v16);
      vst1q_u8(out + 16, v16);
      out += 32;
    } while (out < end);
#elif defined(SINFL_USE_AVX2)
    __m256i v32 = _mm256_set1_epi8((char)v);
    do {
      _mm256_storeu_si256((__m256i*)out, v32);
      out += 32;
    } while (out < end);
#elif defined(SINFL_USE_SSE42)
    __m128i v16 = _mm_set1_epi8((char)v);
    do {
      _mm_storeu_si128((__m128i*)out, v16);
      _mm_storeu_si128((__m128i*)(out + 16), v16);
      out += 32;
    } while (out < end);
#else
    do {
      memcpy(out, &v, 8); memcpy(out + 8, &v, 8);
      memcpy(out + 16, &v, 8); memcpy(out + 24, &v, 8);
      out += 32;
    } while (out < end);
#endif
    return;
  }
  /* 3. SHORT DISTANCE PATH: Distance 2..7 
   * Your original overlapping 64-bit copy is actually the fastest way to handle this 
   * on M4 because it avoids branch mispredictions entirely. */
  do {
    uint64_t v;
    memcpy(&v, src, 8); memcpy(out, &v, 8);
    out += dist; 
    src += dist;
  } while (out < end);
}
/* --- Block Decoding --- */
static unsigned
build_tbl(unsigned *tbl, const unsigned char *lens, int num_syms, unsigned max_tbl_bits, int is_dist) {
  #define LL_LIT(consume, val, bytes)       ((1U << 31) | ((bytes) << 24) | ((val) << 8) | (consume))
  #define LL_MATCH(consume, shift, base)    ((1U << 30) | ((base) << 16) | ((shift) << 8) | (consume))
  #define LL_SUB(consume, sub_bits, base)   ((1U << 29) | ((base) << 16) | ((sub_bits) << 8) | (consume))
  #define LL_EOB(consume)                   ((1U << 28) | (consume))
  #define LL_ERR                            ((1U << 27))

  #define D_MATCH(consume, shift, base)     ((1U << 31) | ((base) << 16) | ((shift) << 8) | (consume))
  #define D_SUB(consume, sub_bits, base)    ((1U << 30) | ((base) << 16) | ((sub_bits) << 8) | (consume))
  #define D_ERR                             ((1U << 29))

  static const unsigned short lb[]={3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
  static const unsigned char le[]={0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
  static const unsigned short db[]={1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
  static const unsigned char de[]={0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

  unsigned cnt[17] = {0};
  unsigned start_code[17];
  for (int i = 0; i < num_syms; i++) {
    cnt[lens[i]]++;
  }
  cnt[0] = 0;

  unsigned max_len = 15;
  while (max_len > 1 && cnt[max_len] == 0) {
    max_len--;
  }
  unsigned tbl_bits = max_tbl_bits;
  if (tbl_bits > max_len) {
    tbl_bits = max_len;
  }
  if (tbl_bits == 0) {
    tbl_bits = 1;
  }
  unsigned code = 0;
  for (int i = 1; i <= 16; i++) {
    start_code[i] = code;
    code = (code + cnt[i]) << 1;
  }
  const unsigned t_size = (1U << tbl_bits);
  unsigned err_ent = is_dist ? D_ERR : LL_ERR;
  for (unsigned i = 0; i < t_size; i++) {
    tbl[i] = err_ent;
  }
  unsigned *sub_heap = tbl + t_size;
  for (int sym = 0; sym < num_syms; sym++) {
    unsigned L = lens[sym];
    if (L == 0) {
      continue;
    }
    unsigned c = start_code[L]++;
    unsigned rev = sinfl_rev(c, L);
    
    unsigned ent;
    if (!is_dist) {
      if (sym < 256) {
        ent = LL_LIT(L, sym, 1);
      } else if (sym == 256) {
        ent = LL_EOB(L);
      } else if (sym <= 285) {
        unsigned msym = sym - 257;
        ent = LL_MATCH(L + le[msym], L, lb[msym]);
      } else {
        ent = LL_ERR;
      }
    } else {
      if (sym <= 29) {
        ent = D_MATCH(L + de[sym], L, db[sym]);
      } else {
        ent = D_ERR;
      }
    }
    if (L <= tbl_bits) {
      for (unsigned j = rev; j < t_size; j += (1U << L)) {
        tbl[j] = ent;
      }
    } else {
      unsigned prefix = rev & (t_size - 1);
      if (tbl[prefix] == err_ent) {
        unsigned sub_bits = 15 - tbl_bits;
        if (!is_dist) {
          tbl[prefix] = LL_SUB(tbl_bits, sub_bits, (unsigned)(sub_heap - tbl));
        } else {
          tbl[prefix] = D_SUB(tbl_bits, sub_bits, (unsigned)(sub_heap - tbl));
        }
        unsigned sub_size = 1U << sub_bits;
        for (unsigned k = 0; k < sub_size; k++) {
          sub_heap[k] = err_ent;
        }
        sub_heap += sub_size;
      }
      unsigned *st = tbl + ((tbl[prefix] >> 16) & 0x1FFF);
      unsigned sub_ent;
      if (!is_dist) {
        if (sym < 256) {
          sub_ent = LL_LIT(L - tbl_bits, sym, 1);
        } else if (sym == 256) {
          sub_ent = LL_EOB(L - tbl_bits);
        } else {
          unsigned msym = sym - 257;
          sub_ent = LL_MATCH(L - tbl_bits + le[msym], L - tbl_bits, lb[msym]);
        }
      } else {
        sub_ent = D_MATCH(L - tbl_bits + de[sym], L - tbl_bits, db[sym]);
      }
      for (unsigned j = rev >> tbl_bits; j < (1U << (15 - tbl_bits)); j += (1U << (L - tbl_bits))) {
        st[j] = sub_ent;
      }
    }
  }
  if (num_syms >= 288) {
    for (int i = 0; i < t_size; i++) {
      unsigned e1 = tbl[i];
      if ((e1 & (1U << 31)) && (((e1 >> 24) & 3) == 1)) {
        unsigned l1 = e1 & 0xFF;
        unsigned e2 = tbl[(i >> l1) & ((t_size >> l1) - 1)];
        if ((e2 & (1U << 31)) && (((e2 >> 24) & 3) == 1) && (l1 + (e2 & 0xFF) <= tbl_bits)) {
          unsigned sym1 = (e1 >> 8) & 0xFF;
          unsigned sym2 = (e2 >> 8) & 0xFF;
          tbl[i] = LL_LIT(l1 + (e2 & 0xFF), sym1 | (sym2 << 8), 2);
        }
      }
    }
  }
  return tbl_bits;
}
extern size_t
sinfl_decompress(struct sinfl_state *s, void *out_buf, size_t out_cap, const void *in_buf, size_t in_size) {
  static const unsigned char ord[]={16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
  unsigned char *out = (unsigned char*)out_buf;
  unsigned char *out_end = out + out_cap;
  unsigned char *out_limit = (out_cap >= 320) ? (out_end - 320) : out;
  const unsigned char *in = (const unsigned char*)in_buf;
  const unsigned char *in_limit = (in_size >= 32) ? ((const unsigned char*)in_buf + in_size - 32) : (const unsigned char*)in_buf;
  s->init_fix = 0;

  int last = 0;
  unsigned long long bit_buf = s->bit_buf;
  int bits_left = s->bits_left;
  unsigned lit_mask = 0;
  unsigned dist_mask = 0;
  
  while (!last) {
    SINFL_REFILL_SAFE();
    last = bit_buf & 0x01;
    bit_buf >>= 1; bits_left -= 1;

    int type = bit_buf & 0x03;
    bit_buf >>= 2; bits_left -= 2;
    if (type == 3) {
      return -1;
    }
    if (type == 0) {
      in -= (bits_left >> 3);
      bit_buf = bits_left = 0;
      if (in + 4 > (const unsigned char*)in_buf + in_size) {
        return -1;
      }
      unsigned short len, nlen;
      memcpy(&len, in, 2);
      memcpy(&nlen, in + 2, 2);
      if (len != (unsigned short)~nlen) {
        return -1;
      }
      in += 4;
      if (out + len > out_end) {
        return -2;
      }
      if (in + len > (const unsigned char*)in_buf + in_size) {
        return -1;
      }
      memcpy(out, in, len);
      out += len;
      in += len;
    } else {
      if (type == 1) {
        if (!s->init_fix) {
          int i = 0;
          unsigned char lens[320];
          for (; i <= 143; i++) lens[i] = 8;
          for (; i <= 255; i++) lens[i] = 9;
          for (; i <= 279; i++) lens[i] = 7;
          for (; i <= 287; i++) lens[i] = 8;
          for (i = 0; i < 32; i++) lens[288+i] = 5;

          unsigned lb = build_tbl(s->fix_lits, lens, 288, SINFL_LIT_TABLE_BITS, 0);
          unsigned db = build_tbl(s->fix_dists, lens + 288, 32, SINFL_DIST_TABLE_BITS, 1);
          
          s->fix_lit_mask = (1U << lb) - 1;
          s->fix_dist_mask = (1U << db) - 1;
          s->init_fix = 1;
        }
        s->lits = s->fix_lits;
        s->dists = s->fix_dists;

        lit_mask = s->fix_lit_mask;
        dist_mask = s->fix_dist_mask;
      } else {
        int nlt = (bit_buf & 31) + 257;
        bit_buf >>= 5; bits_left -= 5;
        int ndt = (bit_buf & 31) + 1;
        bit_buf >>= 5; bits_left -= 5;
        int ncl = (bit_buf & 15) + 4;
        bit_buf >>= 4; bits_left -= 4;
        
        unsigned char ls[320];
        unsigned char cls[19] = {0};
        for (int i = 0; i < ncl; i++) {
          SINFL_REFILL_SAFE();
          cls[ord[i]] = (unsigned char)(bit_buf & 7);
          bit_buf >>= 3; bits_left -= 3;
        }
        unsigned ct[128];
        unsigned clb = build_tbl(ct, cls, 19, 7, 0);
        unsigned cl_mask = (1U << clb) - 1;
        
        for (int i = 0; i < nlt + ndt;) {
          SINFL_REFILL_SAFE();
          unsigned e = ct[bit_buf & cl_mask];
          if (e & LL_ERR) {
            return -1;
          }
          unsigned huff_len = e & 0xFF;
          bit_buf >>= huff_len;
          bits_left -= huff_len;

          unsigned sy = (e >> 8) & 0xFFFF;
          if (sy < 16) {
            ls[i++] = (unsigned char)sy;
          } else {
            if (sy == 16 && i == 0) {
              return -1;
            }
            int r;
            if (sy == 16) {
              r = (bit_buf & 3) + 3;
              bit_buf >>= 2; bits_left -= 2;
            } else if (sy == 17) {
              r = (bit_buf & 7) + 3;
              bit_buf >>= 3; bits_left -= 3;
            } else {
              r = (bit_buf & 127) + 11;
              bit_buf >>= 7; bits_left -= 7;
            }
            unsigned char v = (sy == 16) ? ls[i-1] : 0;
            if (i + r > nlt + ndt) {
              return -1;
            }
            while (r--) {
              ls[i++] = v;
            }
          }
        }
        unsigned lb = build_tbl(s->dyn_lits, ls, nlt, SINFL_LIT_TABLE_BITS, 0);
        unsigned db = build_tbl(s->dyn_dists, ls + nlt, ndt, SINFL_DIST_TABLE_BITS, 1);

        s->lits = s->dyn_lits;
        s->dists = s->dyn_dists;

        lit_mask = (1U << lb) - 1;
        dist_mask = (1U << db) - 1;
      }
      /* ==========================================================
       * FAST PATH LOOP
       * ========================================================== */
      SINFL_REFILL();
      unsigned ent = s->lits[bit_buf & lit_mask];
      while (out < out_limit && in < in_limit) {
        unsigned long long saved_bit_buf = bit_buf;
      decode_litlen:;
        if ((int)ent < 0) { 
          unsigned short val = (unsigned short)(ent >> 8);
          memcpy(out, &val, 2);
          out += (ent >> 24) & 3;

          unsigned consume = ent & 0xFF;
          bit_buf >>= consume;
          bits_left -= consume;

          unsigned ent2 = s->lits[bit_buf & lit_mask];
          if ((int)ent2 < 0) {
            unsigned short val2 = (unsigned short)(ent2 >> 8);
            memcpy(out, &val2, 2);
            out += (ent2 >> 24) & 3;
            unsigned consume2 = ent2 & 0xFF;
            bit_buf >>= consume2;
            bits_left -= consume2;
          } else {
            ent = ent2;
            SINFL_REFILL();
            continue;
          }
          SINFL_REFILL();
          ent = s->lits[bit_buf & lit_mask];
          continue;
        }
        if (ent & (1U << 30)) { 
          unsigned consume = ent & 0xFF;
          unsigned shift = (ent >> 8) & 0xFF;
          unsigned base = (ent >> 16) & 0x3FFF;
          
          int l = base + ((saved_bit_buf & ((1ULL << consume) - 1)) >> shift);
          bit_buf >>= consume;
          bits_left -= consume;

          unsigned long long saved_dist_buf = bit_buf;
          unsigned dent = s->dists[bit_buf & dist_mask];
          if (SINFL_EXPECT((int)dent >= 0, 0)) { 
            if (dent & D_ERR) return -1;
            unsigned dconsume = dent & 0xFF;
            unsigned dsub_bits = (dent >> 8) & 0xFF;
            unsigned dmask = (1U << dsub_bits) - 1;
            unsigned dbase = (dent >> 16) & 0x1FFF;

            bit_buf >>= dconsume;
            bits_left -= dconsume;

            SINFL_REFILL();
            saved_dist_buf = bit_buf;
            dent = s->dists[dbase + (bit_buf & dmask)];
            if ((int)dent >= 0) return -1;
          }
          unsigned dconsume = dent & 0xFF;
          unsigned dshift = (dent >> 8) & 0xFF;
          unsigned dbase = (dent >> 16) & 0x7FFF;
          
          int d = dbase + ((saved_dist_buf & ((1ULL << dconsume) - 1)) >> dshift);
          bit_buf >>= dconsume;
          bits_left -= dconsume;
          if (SINFL_EXPECT(d > (out - (unsigned char*)out_buf), 0)) {
            return -1;
          }
          sinfl_copy_match(out, d, l);
          out += l;

          SINFL_REFILL();
          ent = s->lits[bit_buf & lit_mask];
          continue;
        }
        if (ent & (1U << 29)) { 
          unsigned consume = ent & 0xFF;
          unsigned sub_bits = (ent >> 8) & 0xFF;
          unsigned mask = (1U << sub_bits) - 1;
          unsigned base = (ent >> 16) & 0x1FFF;
          bit_buf >>= consume;
          bits_left -= consume;

          SINFL_REFILL();
          saved_bit_buf = bit_buf;
          ent = s->lits[base + (bit_buf & mask)];
          goto decode_litlen;
        }
        if (ent & (1U << 28)) { 
          unsigned consume = ent & 0xFF;
          bit_buf >>= consume;
          bits_left -= consume;
          goto block_done;
        }
        return -1;
      }

      /* ==========================================================
       * SLOW PATH LOOP
       * ========================================================== */
      while (1) {
        SINFL_REFILL_SAFE();
        unsigned long long saved_bit_buf = bit_buf;
        unsigned ent = s->lits[bit_buf & lit_mask];
      decode_litlen_slow:;
        if ((int)ent < 0) {
          unsigned short val = (unsigned short)(ent >> 8);
          unsigned bytes = (ent >> 24) & 3;
          if (SINFL_EXPECT(out + bytes <= out_end, 1)) {
            if (bytes == 1) {
              *out++ = (unsigned char)val;
            } else {
              memcpy(out, &val, 2);
              out += 2;
            }
          } else {
            return -2;
          }
          unsigned consume = ent & 0xFF;
          bit_buf >>= consume;
          bits_left -= consume;
          if (SINFL_EXPECT(bits_left < 0, 0)) {
            return -1;
          }
          continue;
        }
        if (ent & (1U << 30)) {
          unsigned consume = ent & 0xFF;
          unsigned shift = (ent >> 8) & 0xFF;
          unsigned base = (ent >> 16) & 0x3FFF;

          int l = base + ((saved_bit_buf & ((1ULL << consume) - 1)) >> shift);
          bit_buf >>= consume;
          bits_left -= consume;

          SINFL_REFILL_SAFE();
          unsigned long long saved_dist_buf = bit_buf;
          unsigned dent = s->dists[bit_buf & dist_mask];
          if (SINFL_EXPECT((int)dent >= 0, 0)) {
            if (dent & D_ERR) {
              return -1;
            }
            unsigned dconsume = dent & 0xFF;
            unsigned dsub_bits = (dent >> 8) & 0xFF;
            unsigned dmask = (1U << dsub_bits) - 1;
            unsigned dbase = (dent >> 16) & 0x1FFF;

            bit_buf >>= dconsume;
            bits_left -= dconsume;

            SINFL_REFILL_SAFE();
            saved_dist_buf = bit_buf;
            dent = s->dists[dbase + (bit_buf & dmask)];
            if ((int)dent >= 0) {
              return -1;
            }
          }
          unsigned dconsume = dent & 0xFF;
          unsigned dshift = (dent >> 8) & 0xFF;
          unsigned dbase = (dent >> 16) & 0x7FFF;
          int d = dbase + ((saved_dist_buf & ((1ULL << dconsume) - 1)) >> dshift);

          bit_buf >>= dconsume;
          bits_left -= dconsume;
          if (SINFL_EXPECT(bits_left < 0, 0)) {
            return -1;
          }
          if (SINFL_EXPECT(out < out_limit, 1)) {
            if (SINFL_EXPECT(d > (out - (unsigned char*)out_buf), 0)) {
              return -1;
            }
            sinfl_copy_match(out, d, l);
            out += l;
          } else {
            if (SINFL_EXPECT(d > (out - (unsigned char*)out_buf), 0)) {
              return -1;
            }
            if (out + l > out_end) {
              return -2;
            }
            const unsigned char *src = out - d;
            while (l--) {
              *out++ = *src++;
            }
          }
          continue;
        }
        if (ent & (1U << 29)) {
          unsigned consume = ent & 0xFF;
          unsigned sub_bits = (ent >> 8) & 0xFF;
          unsigned mask = (1U << sub_bits) - 1;
          unsigned base = (ent >> 16) & 0x1FFF;
          bit_buf >>= consume;
          bits_left -= consume;

          SINFL_REFILL_SAFE();
          saved_bit_buf = bit_buf;
          ent = s->lits[base + (bit_buf & mask)];
          goto decode_litlen_slow;
        }
        if (ent & (1U << 28)) {
          unsigned consume = ent & 0xFF;
          bit_buf >>= consume;
          bits_left -= consume;
          goto block_done;
        }
        return -1;
      }
      block_done:;
    }
  }
  s->in = in;
  s->bit_buf = bit_buf;
  s->bits_left = bits_left;
  return (size_t)(out - (unsigned char*)out_buf);
}
extern size_t
sinflate(struct sinfl_state* state, void *out, size_t cap, const void *in, size_t size) {
  state->bit_buf = 0;
  state->bits_left = 0;
  state->in = (const unsigned char*)in;
  return sinfl_decompress(state, (unsigned char*)out, cap, (const unsigned char*)in, size);
}
#if defined(SINFL_USE_AVX2)
static SINFL_FORCE_INLINE int
sinfl_sum_v256(__m256i v) {
  __m128i lo = _mm256_castsi256_si128(v);
  __m128i hi = _mm256_extracti128_si256(v, 1);
  __m128i res = _mm_add_epi32(lo, hi);

  res = _mm_add_epi32(res, _mm_shuffle_epi32(res, _MM_SHUFFLE(1, 0, 3, 2)));
  res = _mm_add_epi32(res, _mm_shuffle_epi32(res, _MM_SHUFFLE(0, 0, 0, 1)));
  return _mm_cvtsi128_si32(res);
}
#elif defined(SINFL_USE_SSE42)
static SINFL_FORCE_INLINE int
sinfl_sum_v128(__m128i v) {
  __m128i res = _mm_add_epi32(v, _mm_shuffle_epi32(v, _MM_SHUFFLE(1, 0, 3, 2)));
  res = _mm_add_epi32(res, _mm_shuffle_epi32(res, _MM_SHUFFLE(0, 0, 0, 1)));
  return _mm_cvtsi128_si32(res);
}
#endif
static unsigned
sinfl_adler32(unsigned adler, const unsigned char *in, size_t len) {
  unsigned s1 = adler & 0xffff;
  unsigned s2 = adler >> 16;
  while (len > 0) {
    size_t tlen = (len > ADLER_NMAX) ? ADLER_NMAX : len;
    size_t remaining = tlen;
    len -= tlen;
#if defined(SINFL_USE_AVX2)
    if (remaining >= 32) {
      __m256i v_s2 = _mm256_setzero_si256();
      __m256i v_s1_vsum = _mm256_setzero_si256();
      const __m256i v_weights = _mm256_setr_epi8(
        32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17,
        16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1);
      while (remaining >= 32) {
        __m256i v_data = _mm256_loadu_si256((const __m256i*)in);
        v_s2 = _mm256_add_epi32(v_s2, _mm256_slli_epi32(v_s1_vsum, 5));
        v_s2 = _mm256_add_epi32(v_s2, _mm256_madd_epi16(_mm256_maddubs_epi16(v_data, v_weights), _mm256_set1_epi16(1)));
        v_s1_vsum = _mm256_add_epi32(v_s1_vsum, _mm256_sad_epu8(v_data, _mm256_setzero_si256()));
        remaining -= 32;
        in += 32;
      }
      s2 += sinfl_sum_v256(v_s2) + s1 * (unsigned)(tlen - remaining);
      s1 += sinfl_sum_v256(v_s1_vsum);
    }
#elif defined(SINFL_USE_SSE42)
    if (remaining >= 16) {
      __m128i v_s2 = _mm_setzero_si128();
      __m128i v_s1_vsum = _mm_setzero_si128();
      const __m128i v_weights = _mm_setr_epi8(16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1);
      while (remaining >= 16) {
        __m128i v_data = _mm_loadu_si128((const __m128i*)in);
        v_s2 = _mm_add_epi32(v_s2, _mm_slli_epi32(v_s1_vsum, 4));
        v_s2 = _mm_add_epi32(v_s2, _mm_madd_epi16(_mm_maddubs_epi16(v_data, v_weights), _mm_set1_epi16(1)));
        v_s1_vsum = _mm_add_epi32(v_s1_vsum, _mm_sad_epu8(v_data, _mm_setzero_si128()));
        remaining -= 16;
        in += 16;
      }
      s2 += sinfl_sum_v128(v_s2) + s1 * (unsigned)(tlen - remaining);
      s1 += sinfl_sum_v128(v_s1_vsum);
    }
#elif defined(SINFL_ARM64)
    if (remaining >= 16) {
      uint32x4_t v_s2 = vdupq_n_u32(0);
      uint32x4_t v_s1_vsum = vdupq_n_u32(0);
      const uint8x16_t v_weights = {16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1};
      while (remaining >= 16) {
        uint8x16_t v_data = vld1q_u8(in);
        v_s2 = vaddq_u32(v_s2, vshlq_n_u32(v_s1_vsum, 4));
        uint16x8_t v_s1_16 = vpaddlq_u8(v_data);
        v_s1_vsum = vaddw_u16(v_s1_vsum, vadd_u16(vget_low_u16(v_s1_16), vget_high_u16(v_s1_16)));
        uint16x8_t low = vmull_u8(vget_low_u8(v_data), vget_low_u8(v_weights));
        uint16x8_t high = vmull_u8(vget_high_u8(v_data), vget_high_u8(v_weights));

        v_s2 = vaddq_u32(v_s2, vpaddlq_u16(low));
        v_s2 = vaddq_u32(v_s2, vpaddlq_u16(high));
        in += 16;
        remaining -= 16;
      }
      s2 += vaddvq_u32(v_s2) + s1 * (unsigned)(tlen - remaining);
      s1 += vaddvq_u32(v_s1_vsum);
    }
#endif
    while (remaining--) {
      s1 += *in++;
      s2 += s1;
    }
    s1 %= ADLER_MOD;
    s2 %= ADLER_MOD;
  }
  return (s2 << 16) | s1;
}
extern size_t
zsinflate(void *out, size_t cap, const void *mem, size_t size) {
  const unsigned char *in = (const unsigned char*)mem;
  if (size >= 6) {
    const unsigned char *eob = in + size - 4;
    struct sinfl_state s;
    s.bit_buf = 0;
    s.bits_left = 0;

    size_t n = sinfl_decompress(&s, out, cap, in + 2, size - 6);
    if (n < 0) {
      return -2;
    }
    unsigned a = sinfl_adler32(1u, (unsigned char*)out, (size_t)n);
    unsigned h = ((unsigned)eob[0] << 24) | ((unsigned)eob[1] << 16) | ((unsigned)eob[2] << 8) | ((unsigned)eob[3] << 0);
    return a == h ? n : -1;
  } else return -1;
}
#endif
