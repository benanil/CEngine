#ifndef SDEFL_H_INCLUDED
#define SDEFL_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

#define SDEFL_MAX_OFF   (1 << 15)
#define SDEFL_WIN_SIZ   SDEFL_MAX_OFF
#define SDEFL_WIN_MSK   (SDEFL_WIN_SIZ-1)

#define SDEFL_HASH_BITS 15
#define SDEFL_HASH_SIZ  (1 << SDEFL_HASH_BITS)
#define SDEFL_HASH_MSK  (SDEFL_HASH_SIZ-1)

#define SDEFL_MIN_MATCH 4
#define SDEFL_MAX_MATCH 258
#define SDEFL_BLK_MAX   (256*1024)
#define SDEFL_SEQ_SIZ   (16*1024)

#define SDEFL_SYM_MAX   (288)
#define SDEFL_OFF_MAX   (32)
#define SDEFL_PRE_MAX   (19)

#define SDEFL_LVL_MIN   0
#define SDEFL_LVL_DEF   5
#define SDEFL_LVL_MAX   8

#define SDEFL_MAX_OFF_DIST      (32768)
#define SDEFL_LIT_LEN_MAX       (255)
#define SDEFL_MATCH_LEN_MIN     (257)
#define SDEFL_MATCH_LEN_MAX     (285)

struct sdefl_freq {
  unsigned lit[SDEFL_SYM_MAX];
  unsigned off[SDEFL_OFF_MAX];
};
struct sdefl_codes {
  struct {
    unsigned lit[SDEFL_SYM_MAX];
    unsigned off[SDEFL_OFF_MAX];
  } word;
  struct {
    unsigned char lit[SDEFL_SYM_MAX];
    unsigned char off[SDEFL_OFF_MAX];
  } len;
};
struct sdefl_seqt {
  int off, len;
};
struct sdefl {
  unsigned long long bits;
  int bitcnt;
  unsigned long long tbl[SDEFL_HASH_SIZ]; // Store (tag32 << 32 | absolute_index32)
  unsigned long long prv[SDEFL_WIN_SIZ];  // Store (tag32 << 32 | absolute_index32)
  int seq_cnt;
  struct sdefl_seqt seq[SDEFL_SEQ_SIZ];
  struct sdefl_freq freq;
  struct sdefl_codes cod;
};
extern int sdefl_bound(int in_len);
extern int sdeflate(struct sdefl *s, void *o, const void *i, int n, int lvl);
extern int zsdeflate(struct sdefl *s, void *o, const void *i, int n, int lvl);

#ifdef __cplusplus
}
#endif
#endif /* SDEFL_H_INCLUDED */

#ifdef SDEFL_IMPLEMENTATION

#include <string.h>
#include <assert.h>

/* --- 1. Hardware Intrinsics & Compiler Hints --- */
#if defined(__GNUC__) || defined(__clang__)
  #define SDEFL_FORCE_INLINE inline __attribute__((always_inline))
  #define SDEFL_EXPECT(x, y) __builtin_expect(!!(x), y)
  #define SDEFL_CTZLL(x)     __builtin_ctzll(x)
  #define SDEFL_CTZ(x)       __builtin_ctz(x)
  #define SDEFL_CLZ(x)       __builtin_clz(x)
#elif defined(_MSC_VER)
  #include <intrin.h>
  #define SDEFL_FORCE_INLINE __forceinline
  #define SDEFL_EXPECT(x, y) (x)
  #if defined(_M_X64) || defined(_M_ARM64)
    static inline int SDEFL_CTZLL(unsigned long long x) { unsigned long r; _BitScanForward64(&r, x); return (int)r; }
    static inline int SDEFL_CTZ(unsigned long long x) { unsigned long r; _BitScanForward(&r, x); return (int)r; }
    static inline int SDEFL_CLZLL(uint64_t x) { unsigned long r; _BitScanReverse64(&r, x); return 63 - (int)r; }
  #else
    #define SDEFL_CTZ(x)   _tzcnt_u32(x)
    static inline int
    SDEFL_CTZLL(unsigned long long x) {
      unsigned long r;
      if (_BitScanForward(&r, (unsigned)(x & 0xFFFFFFFF))) {
        return (int)r;
      }
      _BitScanForward(&r, (unsigned)(x >> 32)); return (int)(r + 32);
    }
  #endif
  static inline int SDEFL_CLZ(unsigned x) { unsigned long r; _BitScanReverse(&r, x); return 31 - (int)r; }
#endif
#define SDEFL_BITREV16(x) sdefl_bitrev16(x)

/* --- Architecture & SIMD Detection --- */
#if !defined(SDEFL_X64) && !defined(SDEFL_ARM64)
  #if defined(__x86_64__) || defined(_M_X64)
    #define SDEFL_X64
  #elif defined(__aarch64__) || defined(_M_ARM64)
    #define SDEFL_ARM64
  #endif
#endif
#ifdef SDEFL_X64
  /* SSE4.2 */
  #include <immintrin.h>
  #ifndef SDEFL_SSE42
    #if defined(__SSE4_2__) || (defined(_MSC_VER) && defined(__AVX__))
      /* Note: MSVC doesn't have a specific SSE4.2 macro, but AVX implies SSE4.2 */
      #define SDEFL_SSE42
    #endif
  #endif
  /* AVX2 */
  #ifndef SDEFL_AVX2
    #if defined(__AVX2__)
      #define SDEFL_AVX2
    #endif
  #endif
#endif
/* 3. Detect ARM Features (NEON) */
#ifdef SDEFL_ARM64
  #ifndef SDEFL_NEON
    #if defined(__ARM_NEON) || defined(__ARM_NEON__)
      #define SDEFL_NEON
      #include <arm_neon.h>
    #endif
  #endif
#endif

/* --- Endian Detection --- */
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
  #define SDEFL_BIG_ENDIAN
#elif defined(__BIG_ENDIAN__) || defined(__ARMEB__) || defined(__THUMBEB__) || \
      defined(__AARCH64EB__) || defined(_MIPSEB) || defined(__MIPSEB) || \
      defined(__MIPSEB__) || defined(__s390__) || defined(__sparc__)
  #define SDEFL_BIG_ENDIAN
#endif

#ifdef SDEFL_BIG_ENDIAN
  /* Disable SIMD on Big Endian to prevent lane-ordering bugs. 
     The scalar fallbacks are heavily optimized and Endian-safe. */
  #undef SDEFL_AVX2
  #undef SDEFL_SSE42
  #undef SDEFL_NEON

  #if defined(__GNUC__) || defined(__clang__)
    #define SDEFL_BSWAP64(x) __builtin_bswap64(x)
    #define SDEFL_CLZLL(x)   __builtin_clzll(x)
  #elif defined(_MSC_VER)
    #define SDEFL_BSWAP64(x) _byteswap_uint64(x)
    static inline int SDEFL_CLZLL(uint64_t x) { unsigned long r; _BitScanReverse64(&r, x); return 63 - (int)r; }
  #endif
#endif

#define SDEFL_NIL               0x7FFF
#define SDEFL_MAX_MATCH         258
#define SDEFL_MAX_CODE_LEN      (15)
#define SDEFL_SYM_BITS          (10u)
#define SDEFL_SYM_MSK           ((1u << SDEFL_SYM_BITS)-1u)
#define SDEFL_RAW_BLK_SIZE      (65535)
#define SDEFL_LIT_LEN_CODES     (14)
#define SDEFL_OFF_CODES         (15)
#define SDEFL_PRE_CODES         (7)
#define SDEFL_CNT_NUM(n)        ((((n)+3u/4u)+3u)&~3u)
#define SDEFL_EOB               (256)
#define ADLER_MOD               65521
#define ADLER_NMAX              5552 /* Largest n such that s2 doesn't overflow uint32 */
#define SDEFL_ADLER_INIT        (1)

struct sdefl_match_codest {
  int ls, lc, dc, dx;
};
struct sdefl_match {
  int off, len;
};
struct sdefl_symcnt {
  int items, lit, off;
};
enum sdefl_blk_type {
  SDEFL_BLK_UCOMPR,
  SDEFL_BLK_DYN
};
/* --- 2. Helpers --- */
#define sdefl_div_round_up(n,d) (int)(((unsigned)(n)+((d)-1))/(d))
static SDEFL_FORCE_INLINE unsigned sdefl_uload32(const void *p) { unsigned v; memcpy(&v, p, 4); return v; }
static SDEFL_FORCE_INLINE unsigned long long sdefl_uload64(const void *p) { unsigned long long v; memcpy(&v, p, 8); return v; }
static int sdefl_ilog2(unsigned n) { return SDEFL_EXPECT(n > 0, 1) ? (31 - SDEFL_CLZ(n)) : 0; }
static SDEFL_FORCE_INLINE unsigned short
sdefl_bitrev16(unsigned short x) {
  x = ((x & 0xAAAA) >> 1) | ((x & 0x5555) << 1);
  x = ((x & 0xCCCC) >> 2) | ((x & 0x3333) << 2);
  x = ((x & 0xF0F0) >> 4) | ((x & 0x0F0F) << 4);
  return (unsigned short)((x >> 8) | (x << 8));
}
/* --- 3. Optimized 64-bit Bit Buffer --- */
static SDEFL_FORCE_INLINE void
sdefl_put(unsigned char **dst, struct sdefl *s, unsigned long long code, int bitcnt) {
  /* 1. Add bits to the accumulator. On ARM64, variable shifts are native and fast. */
  s->bits |= (code << s->bitcnt);
  s->bitcnt += bitcnt;
  /* 2. Always write the full 64-bit accumulator.
   * We use memcpy because it is the standard C idiom for an unaligned store.
   * Compilers (GCC/Clang) on ARM64 and x64 will optimize this into 
   * a single instruction: 'str' (ARM64) or 'mov' (x64). */
#ifdef SDEFL_BIG_ENDIAN
  /* BSWAP effectively converts the uint64_t to a Little-Endian memory layout */
  uint64_t out_bits = SDEFL_BSWAP64(s->bits);
  memcpy(*dst, &out_bits, 8);
#else
  memcpy(*dst, &s->bits, 8);
#endif
  /* 3. Advance the pointer by the number of WHOLE bytes consumed.
   * This eliminates the 'if (bitcnt >= 32)' branch. */
  int bytes_consumed = s->bitcnt >> 3;
  *dst += bytes_consumed;
  /* 4. Clear the consumed bytes from the accumulator.
   * Because we flush every time, s->bitcnt stays low (usually 0-7 bits),
   * meaning (bytes_consumed << 3) will never be >= 64, avoiding UB. */
  s->bits >>= (bytes_consumed << 3);
  s->bitcnt &= 7;
}
static void
sdefl_put16(unsigned char **dst, unsigned short x) {
  /* Guaranteed Little-Endian byte order */
  (*dst)[0] = (unsigned char)(x & 0xFF);
  (*dst)[1] = (unsigned char)(x >> 8);
  *dst += 2;
}
static SDEFL_FORCE_INLINE void
sdefl_bit_flush(unsigned char **dst, struct sdefl *s) {
  if (s->bitcnt > 0) {
    **dst = (unsigned char)(s->bits & 0xFF);
    (*dst)++;
  }
  s->bits = 0;
  s->bitcnt = 0;
}
/* --- 4. Binning Identities --- */
static SDEFL_FORCE_INLINE void
sdefl_match_codes(struct sdefl_match_codest *cod, int dist, int len) {
  int lx_r = sdefl_ilog2((unsigned)(len - 3)) - 2;
  int lx = (lx_r < 0) ? 0 : lx_r;
  int lc = (lx == 0) ? (len + 254) : (((lx - 1) << 2) + 265 + ((len - ((1 << (lx + 2)) + 3)) >> lx));
  int is_258 = (len >= 258);
  cod->lc = is_258 ? 285 : lc;
  cod->ls = is_258 ? 0 : lx;

  int dx_r = sdefl_ilog2((unsigned)(dist - 1)) - 1;
  int dx = (dx_r < 0) ? 0 : dx_r;
  cod->dc = (dx > 0) ? (((dx + 1) << 1) + (dist > (3 << dx))) : (dist - 1);
  cod->dx = dx;
}
/* --- 5. Support Components --- */
static void
sdefl_seq(struct sdefl *s, int off, int len) {
  assert(s->seq_cnt < SDEFL_SEQ_SIZ);
  s->seq[s->seq_cnt].off = off;
  s->seq[s->seq_cnt].len = len;
  s->seq_cnt++;
}
static void
sdefl_reg_match(struct sdefl *s, int dist, int len) {
  struct sdefl_match_codest cod;
  sdefl_match_codes(&cod, dist, len);
  s->freq.lit[cod.lc]++;
  s->freq.off[cod.dc]++;
}
static void
sdefl_match_write(unsigned char **dst, struct sdefl *s, int dist, int len) {
  struct sdefl_match_codest cod;
  sdefl_match_codes(&cod, dist, len);
  sdefl_put(dst, s, s->cod.word.lit[cod.lc], s->cod.len.lit[cod.lc]);
  sdefl_put(dst, s, (unsigned long long)(len - 3) & ((1ULL << cod.ls) - 1), cod.ls);
  sdefl_put(dst, s, s->cod.word.off[cod.dc], s->cod.len.off[cod.dc]);
  sdefl_put(dst, s, (unsigned long long)(dist - 1) & ((1ULL << cod.dx) - 1), cod.dx);
}
/* --- 6. Huffman Construction & Sorting --- */
static void
sdefl_heap_sub(unsigned A[], unsigned len, unsigned sub) {
  unsigned c, p = sub;
  unsigned v = A[sub];
  while ((c = p << 1) <= len) {
    if (c < len && A[c + 1] > A[c]) {
      c++;
    }
    if (v >= A[c]) {
      break;
    }
    A[p] = A[c];
    p = c;
  }
  A[p] = v;
}
static void
sdefl_heap_array(unsigned *A, unsigned len) {
  unsigned sub;
  for (sub = len >> 1; sub >= 1; sub--) {
    sdefl_heap_sub(A, len, sub);
  }
}
static void
sdefl_heap_sort(unsigned *A, unsigned n) {
  A--;
  sdefl_heap_array(A, n);
  while (n >= 2) {
    unsigned tmp = A[n];
    A[n--] = A[1];
    A[1] = tmp;
    sdefl_heap_sub(A, n, 1);
  }
}
static unsigned
sdefl_sort_sym(unsigned sym_cnt, unsigned *freqs,
               unsigned char *lens, unsigned *sym_out) {

  unsigned cnts[SDEFL_CNT_NUM(SDEFL_SYM_MAX)] = {0};
  unsigned cnt_num = SDEFL_CNT_NUM(sym_cnt);
  unsigned used_sym = 0;
  unsigned sym, i;
  for (sym = 0; sym < sym_cnt; sym++) {
    cnts[freqs[sym] < cnt_num-1 ? freqs[sym]: cnt_num-1]++;
  }
  for (i = 1; i < cnt_num; i++) {
    unsigned cnt = cnts[i];
    cnts[i] = used_sym;
    used_sym += cnt;
  }
  for (sym = 0; sym < sym_cnt; sym++) {
    unsigned freq = freqs[sym];
    if (freq) {
      unsigned idx = freq < cnt_num-1 ? freq : cnt_num-1;
      sym_out[cnts[idx]++] = sym | (freq << SDEFL_SYM_BITS);
    } else {
      lens[sym] = 0;
    }
  }
  sdefl_heap_sort(sym_out + cnts[cnt_num-2], cnts[cnt_num-1] - cnts[cnt_num-2]);
  return used_sym;
}
static void
sdefl_build_tree(unsigned *A, unsigned sym_cnt) {
  unsigned i = 0;
  unsigned b = 0;
  unsigned e = 0;
  do {
    unsigned m;
    unsigned n;
    unsigned freq_shift;
    if (i != sym_cnt && (b == e || (A[i] >> SDEFL_SYM_BITS) <= (A[b] >> SDEFL_SYM_BITS))) {
      m = i++;
    } else {
      m = b++;
    }
    if (i != sym_cnt && (b == e || (A[i] >> SDEFL_SYM_BITS) <= (A[b] >> SDEFL_SYM_BITS))) {
      n = i++;
    } else {
      n = b++;
    }
    freq_shift = (A[m] & ~SDEFL_SYM_MSK) + (A[n] & ~SDEFL_SYM_MSK);
    A[m] = (A[m] & SDEFL_SYM_MSK) | (e << SDEFL_SYM_BITS);
    A[n] = (A[n] & SDEFL_SYM_MSK) | (e << SDEFL_SYM_BITS);
    A[e] = (A[e] & SDEFL_SYM_MSK) | freq_shift;
  } while (sym_cnt - ++e > 1);
}
static void
sdefl_gen_len_cnt(unsigned *A, unsigned root, unsigned *len_cnt,
                  unsigned max_code_len) {
  int n;
  unsigned i;
  for (i = 0; i <= max_code_len; i++) {
    len_cnt[i] = 0;
  }
  len_cnt[1] = 2;

  A[root] &= SDEFL_SYM_MSK;
  for (n = (int)root - 1; n >= 0; n--) {
    unsigned p = A[n] >> SDEFL_SYM_BITS;
    unsigned pdepth = A[p] >> SDEFL_SYM_BITS;
    unsigned depth = pdepth + 1;
    unsigned len = depth;

    A[n] = (A[n] & SDEFL_SYM_MSK) | (depth << SDEFL_SYM_BITS);
    if (len >= max_code_len) {
      len = max_code_len;
      do {
        len--;
      } while (!len_cnt[len]);
    }
    len_cnt[len]--;
    len_cnt[len+1] += 2;
  }
}
static void
sdefl_gen_codes(unsigned *A, unsigned char *lens, const unsigned *len_cnt,
                unsigned max_code_word_len, unsigned sym_cnt) {

  unsigned i;
  unsigned sym;
  unsigned len;
  unsigned nxt[SDEFL_MAX_CODE_LEN + 1];
  for (i = 0, len = max_code_word_len; len >= 1; len--) {
    unsigned cnt = len_cnt[len];
    while (cnt--) {
      lens[A[i++] & SDEFL_SYM_MSK] = (unsigned char)len;
    }
  }
  nxt[0] = nxt[1] = 0;
  for (len = 2; len <= max_code_word_len; len++) {
    nxt[len] = (nxt[len-1] + len_cnt[len-1]) << 1;
  }
  for (sym = 0; sym < sym_cnt; sym++) {
    A[sym] = nxt[lens[sym]]++;
  }
}
static void
sdefl_huff(unsigned char *lens, unsigned *codes, unsigned *freqs,
           unsigned num_syms, unsigned max_code_len) {

  unsigned len_cnt[16];
  unsigned used_syms = sdefl_sort_sym(num_syms, freqs, lens, codes);
  if (!used_syms) {
    return;
  }
  if (used_syms == 1) {
    unsigned s = codes[0] & 0x3FF; unsigned i = s ? 0 : 1;
    codes[s] = 0; lens[s] = 1; codes[i] = 1; lens[i] = 1;
    return;
  }
  sdefl_build_tree(codes, used_syms);
  sdefl_gen_len_cnt(codes, used_syms - 2, len_cnt, max_code_len);
  sdefl_gen_codes(codes, lens, len_cnt, max_code_len, num_syms);
  for (unsigned c = 0; c < num_syms; c++) {
    if (lens[c]) {
      codes[c] = SDEFL_BITREV16((unsigned short)codes[c]) >> (16 - lens[c]);
    }
  }
}
/* --- 7. Cost Calculation and Precode Logic --- */
#if defined(SDEFL_AVX2)
static SDEFL_FORCE_INLINE int
sdefl_sum_v256(__m256i v) {
  __m128i lo = _mm256_castsi256_si128(v);
  __m128i hi = _mm256_extracti128_si256(v, 1);
  __m128i res = _mm_add_epi32(lo, hi);
  /* Fold 128-bit to 64-bit */
  res = _mm_add_epi32(res, _mm_shuffle_epi32(res, _MM_SHUFFLE(1, 0, 3, 2)));
  /* Fold 64-bit to 32-bit */
  res = _mm_add_epi32(res, _mm_shuffle_epi32(res, _MM_SHUFFLE(0, 0, 0, 1)));
  return _mm_cvtsi128_si32(res);
}
#elif defined(SDEFL_SSE42)
static SDEFL_FORCE_INLINE int
sdefl_sum_v128(__m128i v) {
  __m128i res = _mm_add_epi32(v, _mm_shuffle_epi32(v, _MM_SHUFFLE(1, 0, 3, 2)));
  res = _mm_add_epi32(res, _mm_shuffle_epi32(res, _MM_SHUFFLE(0, 0, 0, 1)));
  return _mm_cvtsi128_si32(res);
}
#endif
static enum sdefl_blk_type
sdefl_blk_type(const struct sdefl *s, int blk_len, int pre_item_len,
                    const unsigned *pre_freq, const unsigned char *pre_len) {

  static const unsigned char x_pre_bits[32] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,3,7,0,0,0,0,0,0,0,0,0,0,0,0,0};
  static const unsigned char x_len_bits[32] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0,0,0,0};
  static const unsigned char x_off_bits[32] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13,0,0};

  /* Base cost: headers */
  int acc = 0;
  int dyn_cost = 5 + 5 + 4 + (3 * pre_item_len);
  for (int i = 0; i < SDEFL_PRE_MAX; i++) {
    /* 1. Pre-code loop (19 symbols) */
    dyn_cost += pre_freq[i] * (x_pre_bits[i] + pre_len[i]);
  }
  /* 2. Main Literal/Length loop (0-255) */
#if defined(SDEFL_AVX2)
  __m256i v_acc = _mm256_setzero_si256();
  for (int i = 0; i < 256; i += 8) {
    __m256i freq = _mm256_loadu_si256((const __m256i*)&s->freq.lit[i]);
    /* Convert 8 bytes of lengths to 8 dwords */
    __m256i lens = _mm256_cvtepu8_epi32(_mm_loadu_si128((const __m128i*)&s->cod.len.lit[i]));
    v_acc = _mm256_add_epi32(v_acc, _mm256_mullo_epi32(freq, lens));
  }
  acc += sdefl_sum_v256(v_acc);
#elif defined(SDEFL_SSE42)
  __m128i v_acc_128 = _mm_setzero_si128();
  for (int i = 0; i < 256; i += 4) {
    __m128i freq = _mm_loadu_si128((const __m128i*)&s->freq.lit[i]);
    unsigned b4 = sdefl_uload32(&s->cod.len.lit[i]);
    __m128i lens = _mm_cvtepu8_epi32(_mm_cvtsi32_si128(b4));
    v_acc_128 = _mm_add_epi32(v_acc_128, _mm_mullo_epi32(freq, lens));
  }
  acc += sdefl_sum_v128(v_acc_128);
#elif defined(SDEFL_NEON)
  uint32x4_t v_acc = vdupq_n_u32(0);
  for (int i = 0; i < 256; i += 4) {
    uint32x4_t freq = vld1q_u32(&s->freq.lit[i]);
    /* Convert 4 bytes to 4 dwords */
    uint8x8_t l8 = vld1_u8(&s->cod.len.lit[i]);
    uint32x4_t lens = vmovl_u16(vget_low_u16(vmovl_u8(l8)));
    v_acc = vmlaq_u32(v_acc, freq, lens);
  }
  acc += vaddvq_u32(v_acc);
#else
  for (int i = 0; i < 256; i++) {
    acc += s->freq.lit[i] * s->cod.len.lit[i];
  }
#endif
  /* 3. Match Length loop (257-285) */
  dyn_cost += acc + s->cod.len.lit[SDEFL_EOB];
  for (int i = 257; i < 286; i++) {
    dyn_cost += s->freq.lit[i] * (x_len_bits[i - 257] + s->cod.len.lit[i]);
  }
  /* 4. Distance loop (0-29) */
  for (int i = 0; i < 30; i++) {
    dyn_cost += s->freq.off[i] * (x_off_bits[i] + s->cod.len.off[i]);
  }
  /* Stored block cost calculation (Scalar is O(1)) */
  int fix_cost = 8 * (5 * ((blk_len + SDEFL_RAW_BLK_SIZE - 1) / SDEFL_RAW_BLK_SIZE) + blk_len + 1 + 2);
  return (dyn_cost < fix_cost) ? SDEFL_BLK_DYN : SDEFL_BLK_UCOMPR;
}
static void
sdefl_precode(struct sdefl_symcnt *cnt, unsigned *freqs, unsigned *items,
              const unsigned char *litlen, const unsigned char *offlen) {
  
  unsigned *at = items;
  unsigned run_start = 0;
  unsigned char lens[SDEFL_SYM_MAX + SDEFL_OFF_MAX];
  for (cnt->lit = SDEFL_SYM_MAX; cnt->lit > 257; cnt->lit--) {
    if (litlen[cnt->lit - 1]) {
      break;
    }
  }
  for (cnt->off = SDEFL_OFF_MAX; cnt->off > 1; cnt->off--) {
    if (offlen[cnt->off - 1]) {
      break;
    }
  }
  unsigned total = (unsigned)(cnt->lit + cnt->off);
  memcpy(lens, litlen, (size_t)cnt->lit); memcpy(lens + cnt->lit, offlen, (size_t)cnt->off);
  do {
    unsigned len = lens[run_start], run_end = run_start;
    do {
      run_end++;
    } while (run_end != total && len == lens[run_end]);
    if (!len) {
      while ((run_end - run_start) >= 11) {
        unsigned x = (run_end - run_start - 11);
        x = x < 0x7f ? x : 0x7f;
        freqs[18]++;
        *at++ = 18u | (x << 5u);
        run_start += 11 + x;
      }
      if ((run_end - run_start) >= 3) {
        unsigned x = (run_end - run_start - 3);
        x = x < 0x7 ? x : 0x7;
        freqs[17]++;
        *at++ = 17u | (x << 5u);
        run_start += 3 + x;
      }
    } else if ((run_end - run_start) >= 4) {
      freqs[len]++; *at++ = len; run_start++;
      do {
        unsigned x = (run_end - run_start - 3);
        x = x < 0x03 ? x : 0x03;
        freqs[16]++;
        *at++ = 16u | (x << 5u);
        run_start += 3 + x;
      } while ((run_end - run_start) >= 3);
    }
    while (run_start != run_end) {
      freqs[len]++;
      *at++ = len;
      run_start++;
    }
  } while (run_start != total);
  cnt->items = (int)(at - items);
}
/* --- 8. Full Block Flushing Logic --- */
static void
sdefl_flush(unsigned char **dst, struct sdefl *s, int is_last,
            const unsigned char *in, int blk_begin, int blk_end) {

  static const unsigned char perm[SDEFL_PRE_MAX] = {
    16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
  int blk_len = blk_end - blk_begin;
  struct sdefl_symcnt symcnt = {0};
  unsigned p_codes[SDEFL_PRE_MAX];
  unsigned p_freqs[SDEFL_PRE_MAX] = {0};
  unsigned items[SDEFL_SYM_MAX + SDEFL_OFF_MAX];
  unsigned char p_lens[SDEFL_PRE_MAX];

  s->freq.lit[SDEFL_EOB]++;
  sdefl_huff(s->cod.len.lit, s->cod.word.lit, s->freq.lit, SDEFL_SYM_MAX, 15);
  sdefl_huff(s->cod.len.off, s->cod.word.off, s->freq.off, SDEFL_OFF_MAX, 15);
  sdefl_precode(&symcnt, p_freqs, items, s->cod.len.lit, s->cod.len.off);
  sdefl_huff(p_lens, p_codes, p_freqs, SDEFL_PRE_MAX, 7);

  int item_cnt;
  for (item_cnt = SDEFL_PRE_MAX; item_cnt > 4; item_cnt--) {
    if (p_lens[perm[item_cnt - 1]]) {
      break;
    }
  }
  enum sdefl_blk_type blk_type = sdefl_blk_type(s, blk_len, item_cnt, p_freqs, p_lens);
  switch (blk_type) {
  case SDEFL_BLK_UCOMPR: {
    int n = (blk_len == 0) ? 1 : (blk_len + SDEFL_RAW_BLK_SIZE - 1) / SDEFL_RAW_BLK_SIZE;
    for (int i = 0; i < n; ++i) {
      int fin = is_last && (i + 1 == n);
      int amt = blk_len < SDEFL_RAW_BLK_SIZE ? blk_len : SDEFL_RAW_BLK_SIZE;

      sdefl_put(dst, s, !!fin, 1);
      sdefl_put(dst, s, 0x00, 2);
      sdefl_bit_flush(dst, s);

      sdefl_put16(dst, (unsigned short)amt);
      sdefl_put16(dst, (unsigned short)~amt);
      memcpy(*dst, in + blk_begin + i * SDEFL_RAW_BLK_SIZE, amt);
      *dst += amt;
      blk_len -= amt;
    }
  } break;

  case SDEFL_BLK_DYN: {
    sdefl_put(dst, s, !!is_last, 1);
    sdefl_put(dst, s, 0x02, 2);
    sdefl_put(dst, s, symcnt.lit - 257, 5);
    sdefl_put(dst, s, symcnt.off - 1, 5);
    sdefl_put(dst, s, item_cnt - 4, 4);
    for (int i = 0; i < item_cnt; ++i) {
      sdefl_put(dst, s, p_lens[perm[i]], 3);
    }
    for (int i = 0; i < symcnt.items; ++i) {
      unsigned sym = items[i] & 0x1F;
      sdefl_put(dst, s, p_codes[sym], p_lens[sym]);
      if (sym >= 16) {
        sdefl_put(dst, s, items[i] >> 5, (sym == 16) ? 2 : (sym == 17) ? 3 : 7);
      }
    }
    for (int i = 0; i < s->seq_cnt; ++i) {
      if (s->seq[i].off >= 0) {
        const unsigned char *p = in + s->seq[i].off;
        for (int j = 0; j < s->seq[i].len; ++j) {
          sdefl_put(dst, s, s->cod.word.lit[p[j]], s->cod.len.lit[p[j]]);
        }
      } else {
        sdefl_match_write(dst, s, -s->seq[i].off, s->seq[i].len);
      }
    }
    sdefl_put(dst, s, s->cod.word.lit[SDEFL_EOB], s->cod.len.lit[SDEFL_EOB]);
  } break;
  }
  memset(&s->freq, 0, sizeof(s->freq)); s->seq_cnt = 0;
}
/* --- 9. Match Finder --- */
static SDEFL_FORCE_INLINE int
sdefl_compare_simd(const unsigned char *p1, const unsigned char *p2, int max) {
  int n = 0;
#if defined(SDEFL_AVX2)
  while (n <= max - 32) {
    __m256i v1 = _mm256_loadu_si256((const __m256i*)(p1 + n));
    __m256i v2 = _mm256_loadu_si256((const __m256i*)(p2 + n));
    unsigned mask = (unsigned)_mm256_movemask_epi8(_mm256_cmpeq_epi8(v1, v2));
    if (mask != 0xFFFFFFFF) {
      return n + SDEFL_CTZ(~mask);
    }
    n += 32;
  }
  if (max >= 32 && n < max) { /* Overlap tail */
    unsigned mask = (unsigned)_mm256_movemask_epi8(_mm256_cmpeq_epi8(
      _mm256_loadu_si256((const __m256i*)(p1 + max - 32)),
      _mm256_loadu_si256((const __m256i*)(p2 + max - 32))));
    if (mask != 0xFFFFFFFF) {
      return max - 32 + SDEFL_CTZ(~mask);
    }
    return max;
  }
#elif defined(SDEFL_SSE42)
  while (n <= max - 16) {
    __m128i v1 = _mm_loadu_si128((const __m128i*)(p1 + n));
    __m128i v2 = _mm_loadu_si128((const __m128i*)(p2 + n));
    unsigned mask = (unsigned)_mm_movemask_epi8(_mm_cmpeq_epi8(v1, v2));
    if (mask != 0xFFFF) {
      return n + __builtin_ctz(~mask);
    }
    n += 16;
  }
  if (max >= 16 && n < max) {
    unsigned mask = (unsigned)_mm_movemask_epi8(_mm_cmpeq_epi8(
      _mm_loadu_si128((const __m128i*)(p1 + max - 16)),
      _mm_loadu_si128((const __m128i*)(p2 + max - 16))));
    if (mask != 0xFFFF) {
        return max - 16 + SDEFL_CTZ(~mask);
    }
    return max;
  }
#elif defined(SDEFL_NEON)
  while (n <= max - 16) {
    uint8x16_t cmp = vceqq_u8(vld1q_u8(p1 + n), vld1q_u8(p2 + n));
    unsigned long long mask = vgetq_lane_u64(vreinterpretq_u64_u8(cmp), 0) & vgetq_lane_u64(vreinterpretq_u64_u8(cmp), 1);
    if (mask != 0xFFFFFFFFFFFFFFFFULL) {
      break; /* Fallback to scalar waterfall for exact index */
    }
    n += 16;
  }
#endif
  /* Scalar Waterfall (Branchless XOR) */
  while (n <= max - 8) {
    unsigned long long diff = sdefl_uload64(p1 + n) ^ sdefl_uload64(p2 + n);
    if (diff) {
#ifdef SDEFL_BIG_ENDIAN
      return n + (SDEFL_CLZLL(diff) >> 3);
#else
      return n + (SDEFL_CTZLL(diff) >> 3);
#endif
    }
    n += 8;
  }
  if (n <= max - 4) {
    unsigned diff = sdefl_uload32(p1 + n) ^ sdefl_uload32(p2 + n);
    if (diff) {
#ifdef SDEFL_BIG_ENDIAN
      return n + (SDEFL_CLZ(diff) >> 3);
#else
      return n + (SDEFL_CTZ(diff) >> 3);
#endif
    }
    n += 4;
  }
  while (n < max && p1[n] == p2[n]) {
    n++;
  }
  return n;
}
static void
sdefl_fnd(struct sdefl_match *m, const struct sdefl *s, int chain_len, 
          int max_match, int nice_match, const unsigned char *in, int p, int lvl) {
    
  unsigned u4 = sdefl_uload32(in + p);
  unsigned h = (u4 * 0x9E377989) >> (32 - SDEFL_HASH_BITS);
  unsigned long long entry = s->tbl[h];
  int limit = (p > SDEFL_MAX_OFF_DIST) ? p - SDEFL_MAX_OFF_DIST - 1 : -1;
  if (lvl <= 1) {
    /* Special path with single lookup for fast levels */
    unsigned i = (unsigned)(entry & 0xFFFFFFFF);
    if (i != 0xFFFFFFFF && (int)i > limit && (unsigned)(entry >> 32) == u4) {
      m->len = sdefl_compare_simd(in + i, in + p, max_match);
      m->off = p - i;
    }
    return;
  }
  int best_len = m->len;
  /* Calculate the initial score to beat. (1 length byte ~ 8 bits).
     If no valid match exists yet, the score to beat is 0. */
  int best_score = (best_len >= SDEFL_MIN_MATCH) ? ((best_len << 3) - sdefl_ilog2(m->off)) : 0;
  if (best_len >= 32) {
    chain_len >>= 2;
  }
  while (chain_len--) {
    unsigned tag = (unsigned)(entry >> 32);
    unsigned i   = (unsigned)(entry & 0xFFFFFFFF);
    if (i == 0xFFFFFFFF || (int)i <= limit) {
      break;
    }
    /* 1. Perfect Tag Check (0% collision probability for 4-byte match) */
    if (tag == u4) {
      /* End-Byte Guard (Filters matches that aren't longer than current) */
      /* Used to avoid the uload32 cost */
      if (in[i + best_len] == in[p + best_len]) {
        int n = sdefl_compare_simd(in + i, in + p, max_match);
        if (n > best_len) {
          int dist = p - i;
          int score = (n << 3) - sdefl_ilog2(dist);
          if (score > best_score) {
            best_len = n;
            best_score = score;
            m->off = dist;
            if (n >= nice_match) {
              break;
            }
            /* Scale down remaining budget as match gets longer */
            chain_len >>= 1; 
          }
        }
      }
    }
    entry = s->prv[i & SDEFL_WIN_MSK];
    /* prefetch: Hiding memory latency for the next candidate */
#if defined(SDEFL_X64)
    _mm_prefetch((const char*)&s->prv[(entry & 0xFFFFFFFF) & SDEFL_WIN_MSK], _MM_HINT_T0);
#elif defined(SDEFL_ARM64)
    __builtin_prefetch((const char*)&s->prv[(entry & 0xFFFFFFFF) & SDEFL_WIN_MSK]);
#endif
  }
  m->len = best_len;
}
/* --- 10. Main Loop --- */
static SDEFL_FORCE_INLINE void
sdefl_hash_update(struct sdefl *s, const unsigned char *in, int p, int len) {
  int i = 0;
  static const unsigned SDEFL_HASH_MAGIC = 0x9E377989;
#if defined(SDEFL_AVX2)
  /* Shuffles 16 bytes into 8 overlapping 4-byte lanes: [0123][1234][2345]...[789A] */
  static const unsigned char s_hash_shuf_avx2[32] = {
    0,1,2,3, 1,2,3,4, 2,3,4,5, 3,4,5,6, 4,5,6,7, 5,6,7,8, 6,7,8,9, 7,8,9,10,
  };
#elif defined(SDEFL_SSE42) || defined(SDEFL_NEON)
  /* Shuffles 16 bytes into 4 overlapping 4-byte lanes: [0123][1234][2345][3456] */
  static const unsigned char s_hash_shuf_128[16] = {
    0,1,2,3, 1,2,3,4, 2,3,4,5, 3,4,5,6
  };
#endif
#if defined(SDEFL_AVX2)
  if (len >= 16) {
    const __m256i m_magic = _mm256_set1_epi32(SDEFL_HASH_MAGIC);
    const __m256i m_shuf  = _mm256_loadu_si256((const __m256i*)s_hash_shuf_avx2);
    for (; i <= len - 16; i += 8) {
      /* 1. Load 16 bytes and broadcast across the 256-bit register */
      __m128i data128 = _mm_loadu_si128((const __m128i*)(in + p + i));
      __m256i v = _mm256_shuffle_epi8(_mm256_broadcastsi128_si256(data128), m_shuf);
      /* 2. Parallel Hash: (u32 * magic) >> (32 - bits) */
      __m256i hash = _mm256_srli_epi32(_mm256_mullo_epi32(v, m_magic), 32 - SDEFL_HASH_BITS);
      /* 3. Extract results to stack (Mixing SIMD and Scalar for table updates) */
      unsigned h[8], tags[8];
      _mm256_storeu_si256((__m256i*)h, hash);
      _mm256_storeu_si256((__m256i*)tags, v);
      /* 4. Serial update: Must be serial to handle hash collisions within the batch */
      for (int j = 0; j < 8; ++j) {
        unsigned abs_idx = (unsigned)(p + i + j);
        s->prv[abs_idx & SDEFL_WIN_MSK] = s->tbl[h[j]];
        s->tbl[h[j]] = ((unsigned long long)tags[j] << 32) | abs_idx;
      }
    }
  }
#elif defined(SDEFL_SSE42)
  if (len >= 16) {
    const __m128i m_magic = _mm_set1_epi32(SDEFL_HASH_MAGIC);
    const __m128i m_shuf  = _mm_loadu_si128((const __m128i*)s_hash_shuf_128);
    for (; i <= len - 12; i += 4) {
      __m128i v = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(in + p + i)), m_shuf);
      __m128i hash = _mm_srli_epi32(_mm_mullo_epi32(v, m_magic), 32 - SDEFL_HASH_BITS);
      unsigned h[4], tags[4];
      _mm_storeu_si128((__m128i*)h, hash);
      _mm_storeu_si128((__m128i*)tags, v);
      for (int j = 0; j < 4; ++j) {
        unsigned abs_idx = (unsigned)(p + i + j);
        s->prv[abs_idx & SDEFL_WIN_MSK] = s->tbl[h[j]];
        s->tbl[h[j]] = ((unsigned long long)tags[j] << 32) | abs_idx;
      }
    }
  }
#elif defined(SDEFL_NEON)
  if (len >= 16) {
    const uint32x4_t m_magic = vdupq_n_u32(SDEFL_HASH_MAGIC);
    const uint8x16_t m_shuf  = vld1q_u8(s_hash_shuf_128);
    for (; i <= len - 12; i += 4) {
      /* NEON TBL for 4-lane overlapping extract */
      uint8x16_t data = vld1q_u8(in + p + i);
      uint32x4_t v = vreinterpretq_u32_u8(vqtbl1q_u8(data, m_shuf));
      /* Hash calculation: h = (v * magic) >> (32 - bits) */
      uint32x4_t hash = vshrq_n_u32(vmulq_u32(v, m_magic), 32 - SDEFL_HASH_BITS);
      unsigned h[4], tags[4];
      vst1q_u32(h, hash);
      vst1q_u32(tags, v);
      for (int j = 0; j < 4; ++j) {
        unsigned abs_idx = (unsigned)(p + i + j);
        s->prv[abs_idx & SDEFL_WIN_MSK] = s->tbl[h[j]];
        s->tbl[h[j]] = ((unsigned long long)tags[j] << 32) | abs_idx;
      }
    }
  }
#endif
  /* Scalar Tail: Process remaining positions or small inputs */
  for (; i < len; ++i) {
    /* Use uload32 for the tag - ensures we get the same 4 bytes as SIMD */
    unsigned u32 = sdefl_uload32(in + p + i);
    unsigned h = (u32 * SDEFL_HASH_MAGIC) >> (32 - SDEFL_HASH_BITS);
    unsigned abs_idx = (unsigned)(p + i);
    s->prv[abs_idx & SDEFL_WIN_MSK] = s->tbl[h];
    s->tbl[h] = ((unsigned long long)u32 << 32) | abs_idx;
  }
}
static int
sdefl_compr(struct sdefl *s, unsigned char *out, const unsigned char *in, int in_len, int in_lvl) {
  unsigned char *q = out;
  static const unsigned char nice_m[] = {8, 10, 14, 24, 30, 48, 65, 96, 130};
  int lvl = (in_lvl < SDEFL_LVL_MIN) ? SDEFL_LVL_MIN : (in_lvl > SDEFL_LVL_MAX) ? SDEFL_LVL_MAX : in_lvl;
  int max_chain = (lvl < 8) ? (1 << (lvl + 1)) : (1 << 13);
  int i = 0, litlen = 0;
  for (int n = 0; n < SDEFL_HASH_SIZ; ++n) {
    s->tbl[n] = 0xFFFFFFFF; // Initialize with invalid index
  }
  do {
    int b_beg = i;
    int b_end = ((i + SDEFL_BLK_MAX) < in_len) ? (i + SDEFL_BLK_MAX) : in_len;
    while (i < b_end && s->seq_cnt + 2 < SDEFL_SEQ_SIZ) {
      struct sdefl_match m = { .off = 0, .len = SDEFL_MIN_MATCH - 1 };
      int left = in_len - i;
      int max_m = (left > SDEFL_MAX_MATCH) ? SDEFL_MAX_MATCH : left;
      int nice_match = nice_m[lvl] < max_m ? nice_m[lvl] : max_m;
      if (SDEFL_EXPECT(max_m >= SDEFL_MIN_MATCH, 1)) {
        /* Optimization: Pass nice_match for early exit */
        sdefl_fnd(&m, s, max_chain, max_m, nice_match, in, i, lvl);
      }
      /* Lazy matching + "Very Good Match" Skip
       * If we found a match > 128, don't even check for a lazy match at i+1.
       * The gain is < 0.01% but the speed increase is massive. */
      if (lvl >= 5 && m.len >= SDEFL_MIN_MATCH && m.len < 65 && m.len < nice_match && in_len - (i + 1) >= SDEFL_MIN_MATCH) {
        struct sdefl_match m2 = { .off = 0, .len = SDEFL_MIN_MATCH - 1 };
        int max_m2 = (in_len - (i + 1) > SDEFL_MAX_MATCH) ? SDEFL_MAX_MATCH : (in_len - (i + 1));
        sdefl_fnd(&m2, s, max_chain, max_m2, nice_match, in, i + 1, lvl);
        if (m2.len >= SDEFL_MIN_MATCH) {
          int score1 = (m.len << 3) - sdefl_ilog2(m.off);
          int score2 = (m2.len << 3) - sdefl_ilog2(m2.off);
          if (score2 > score1) {
            m.len = 0;
          }
        }
      }
      if (m.len >= SDEFL_MIN_MATCH) {
        if (litlen) {
          sdefl_seq(s, i - litlen, litlen);
          litlen = 0;
        }
        sdefl_seq(s, -m.off, m.len);
        sdefl_reg_match(s, m.off, m.len);
        /* Optimization: Hash Tunneling (The Skip Heuristic)
           If level is low and match is long, only hash the start. */
        int hashable_run = (lvl < 7 && m.len > 32) ? 3 : m.len;
        int safe_hash = (i + hashable_run > in_len - 4) ? (in_len - 4 - i) : hashable_run;
        if (safe_hash > 0) {
          sdefl_hash_update(s, in, i, safe_hash);
          // Prime the tail of long matches to bridge the dictionary gap
          if (hashable_run < m.len && m.len >= SDEFL_MIN_MATCH) {
            int tail = m.len - 3;
            if (tail > safe_hash && i + tail <= in_len - 4) {
              sdefl_hash_update(s, in, i + tail, 3);
            }
          }
        }
        i += m.len;
      } else {
        s->freq.lit[in[i]]++;
        if (SDEFL_EXPECT(i < in_len - SDEFL_MIN_MATCH, 1)) {
          unsigned lu4 = sdefl_uload32(in + i);
          unsigned h = (lu4 * 0x9E377989) >> (32 - SDEFL_HASH_BITS);
          unsigned tag = lu4 >> 16;
          unsigned abs_idx = (unsigned)i;
          s->prv[abs_idx & SDEFL_WIN_MSK] = s->tbl[h];
          s->tbl[h] = ((unsigned long long)lu4 << 32) | abs_idx;
        }
        i++;
        litlen++;
      }
    }
    if (litlen) {
      sdefl_seq(s, i - litlen, litlen);
      litlen = 0;
    }
    int is_final = (i >= in_len);
    sdefl_flush(&q, s, is_final, in, b_beg, i);
  } while (i < in_len);

  sdefl_bit_flush(&q, s);
  s->bits = 0;
  s->bitcnt = 0;
  return (int)(q - out);
}
extern int
sdeflate(struct sdefl *s, void *out, const void *in, int n, int lvl) {
  s->bits = s->bitcnt = 0;
  return sdefl_compr(s, (unsigned char*)out, (const unsigned char*)in, n, lvl);
}
static unsigned 
sdefl_adler32(unsigned adler, const unsigned char *in, int len) {
  unsigned s1 = adler & 0xffff;
  unsigned s2 = adler >> 16;
  while (len > 0) {
    int tlen = (len > ADLER_NMAX) ? ADLER_NMAX : len;
    int remaining = tlen;
    len -= tlen;
#if defined(SDEFL_AVX2)
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
        in += 32;
        remaining -= 32;
      }
      s2 += sdefl_sum_v256(v_s2) + s1 * (unsigned)(tlen - remaining);
      s1 += sdefl_sum_v256(v_s1_vsum);
    }
#elif defined(SDEFL_SSE42)
    if (remaining >= 16) {
      __m128i v_s2 = _mm_setzero_si128();
      __m128i v_s1_vsum = _mm_setzero_si128();
      const __m128i v_weights = _mm_setr_epi8(16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1);
      while (remaining >= 16) {
        __m128i v_data = _mm_loadu_si128((const __m128i*)in);
        v_s2 = _mm_add_epi32(v_s2, _mm_slli_epi32(v_s1_vsum, 4));
        v_s2 = _mm_add_epi32(v_s2, _mm_madd_epi16(_mm_maddubs_epi16(v_data, v_weights), _mm_set1_epi16(1)));
        v_s1_vsum = _mm_add_epi32(v_s1_vsum, _mm_sad_epu8(v_data, _mm_setzero_si128()));
        in += 16;
        remaining -= 16;
      }
      s2 += sdefl_sum_v128(v_s2) + s1 * (unsigned)(tlen - remaining);
      s1 += sdefl_sum_v128(v_s1_vsum);
    }
#elif defined(SDEFL_NEON)
    if (remaining >= 16) {
      uint32x4_t v_s2 = vdupq_n_u32(0);
      uint32x4_t v_s1_vsum = vdupq_n_u32(0);
      const uint8x16_t v_weights = {16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1};
      while (remaining >= 16) {
        uint8x16_t v_data = vld1q_u8(in);
        /* s2 += s1 * 16 (vertical accumulation) */
        v_s2 = vaddq_u32(v_s2, vshlq_n_u32(v_s1_vsum, 4));
        
        /* Calculate s1 sum within block */
        uint16x8_t v_s1_16 = vpaddlq_u8(v_data);
        v_s1_vsum = vaddw_u16(v_s1_vsum, vadd_u16(vget_low_u16(v_s1_16), vget_high_u16(v_s1_16)));

        /* Weighted sum for s2: Multiply bytes by weights */
        uint16x8_t low = vmull_u8(vget_low_u8(v_data), vget_low_u8(v_weights));
        uint16x8_t high = vmull_u8(vget_high_u8(v_data), vget_high_u8(v_weights));
        
        /* Pairwise add 16-bit products into 32-bit s2 lanes */
        v_s2 = vaddq_u32(v_s2, vpaddlq_u16(low));
        v_s2 = vaddq_u32(v_s2, vpaddlq_u16(high));

        in += 16;
        remaining -= 16;
      }
      s2 += vaddvq_u32(v_s2) + s1 * (unsigned)(tlen - remaining);
      s1 += vaddvq_u32(v_s1_vsum);
    }
#endif
    /* Scalar tail for the chunk */
    while (remaining--) {
      s1 += *in++;
      s2 += s1;
    }
    s1 %= ADLER_MOD;
    s2 %= ADLER_MOD;
  }
  return (s2 << 16) | s1;
}
extern int
zsdeflate(struct sdefl *s, void *out, const void *in, int n, int lvl) {
  int p = 0;
  unsigned a = 0;
  unsigned char *q = (unsigned char*)out;

  s->bits = s->bitcnt = 0;
  sdefl_put(&q, s, 0x78, 8); /* deflate, 32k window */
  sdefl_put(&q, s, 0x01, 8); /* fast compression */
  q += sdefl_compr(s, q, (const unsigned char*)in, n, lvl);

  /* append adler checksum */
  a = sdefl_adler32(SDEFL_ADLER_INIT, (const unsigned char*)in, n);
  for (p = 0; p < 4; ++p) {
    sdefl_put(&q, s, (a >> 24) & 0xFF, 8);
    a <<= 8;
  }
  return (int)(q - (unsigned char*)out);
}
extern int
sdefl_bound(int len) {
  int max_blocks = 1 + sdefl_div_round_up(len, SDEFL_RAW_BLK_SIZE);
  int bound = 5 * max_blocks + len + 1 + 4 + 8 + 3;
  return bound;
}
#endif /* SDEFL_IMPLEMENTATION */
