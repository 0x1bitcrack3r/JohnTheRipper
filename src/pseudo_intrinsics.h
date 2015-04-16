/*
 * Minimalistic pseudo-instrinsics for width-agnostic x86 SIMD code.
 *
 * This software is Copyright (c) 2015 magnum,
 * Copyright (c) 2015 JimF,
 * Copyright (c) 2015 Lei Zhang,
 * and it is hereby released to the general public under the following terms:
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted.
 *
 * Synopsis:
 *
 * SSE2:     __m128i a = _mm_add_epi32(b, c);
 * AVX2:     __m256i a = _mm256_add_epi32(b, c);
 * AVX512:   __m512i a = _mm512_add_epi32(b, c);
 * -> Pseudo:  vtype a = vadd_epi32(b, c);
 *
 * SSE2:     __m128i a = _mm_load_si128(p);
 * AVX2:     __m256i a = _mm256_load_si256(p);
 * AVX512:   __m512i a = _mm512_load_si512(p);
 * -> Pseudo:  vtype a = vload(p);
 *
 * intrinsics are emulated where the target does not support them.
 */

#ifndef _SSE_PSEUDO_H
#define _SSE_PSEUDO_H

#include "arch.h"

#if SIMD_COEF_32

#include "aligned.h"
#include "stdint.h"
#include "common.h" /* for is_aligned() */

/*************************** AVX512 and MIC ***************************/
#if __MIC__ || __AVX512__
#include <immintrin.h>

typedef __m512i vtype;

#define vadd_epi32              _mm512_add_epi32
#define vadd_epi64              _mm512_add_epi64
#define vand                    _mm512_and_si512
#define vandnot                 _mm512_andnot_si512
#define vcmov(y, z, x)          vxor(z, vand(x, vxor(y, z)))
#define vcmpeq_epi8             _mm512_cmpeq_epi8
#define vcmpeq_epi32            _mm512_cmpeq_epi32
#define vcvtsi32                _mm512_cvtsi32_si512
#if __MIC__
/*
 * NOTE: AVX2 has it as (base, index, scale) while MIC and AVX512 are
 * different (even from each other).
 */
#define vgather_epi32(b, i, s)  _mm512_i32gather_epi32(i, (void*)(b), s)
#else
/*
 * AVX512, untested, see https://software.intel.com/en-us/node/523826
 */
#define vgather_epi32(b, i, s)  _mm512_i32gather_epi32(i, (void*)(b),         \
                                                       _MM_UPCONV_EPI32_NONE, \
                                                       s, _MM_HINT_NONE)
#endif
#define vinsert_epi32           _mm512_insert_epi32
#define vload(x)                _mm512_load_si512((void*)(x))
#define vloadu(x)               _mm512_loadu_si512((void*)(x))
#define vmovemask_epi8          _mm512_movemask_epi8
#define vor                     _mm512_or_si512
#define vpermute2x128           _mm512_permute2x128_si512
#define vpermute4x64_epi64      _mm512_permute4x64_epi64
#define vset1_epi8              _mm512_set1_epi8
#define vset1_epi32             _mm512_set1_epi32
#define vset1_epi64x            _mm512_set1_epi64
#define vset_epi32              _mm512_set_epi32
#define vset_epi64x             _mm512_set_epi64
#define vsetzero                _mm512_setzero_si512
#define vshuffle_epi8           _mm512_shuffle_epi8
#define vshuffle_epi32          _mm512_shuffle_epi32
#define vshufflehi_epi16        _mm512_shufflehi_epi16
#define vshufflelo_epi16        _mm512_shufflelo_epi16
#define vslli_epi16             _mm512_slli_epi16
#define vslli_epi32             _mm512_slli_epi32
#define vslli_epi64             _mm512_slli_epi64
#define vsrli_epi16             _mm512_srli_epi16
#define vsrli_epi32             _mm512_srli_epi32
#define vsrli_epi64             _mm512_srli_epi64
#define vstore(x, y)            _mm512_store_si512((void*)(x), y)
#define vstoreu(x, y)           _mm512_storeu_si512((void*)(x), y)
#define vunpackhi_epi32         _mm512_unpackhi_epi32
#define vunpackhi_epi64         _mm512_unpackhi_epi64
#define vunpacklo_epi32         _mm512_unpacklo_epi32
#define vunpacklo_epi64         _mm512_unpacklo_epi64
#define vxor                    _mm512_xor_si512

// MIC doesn't support some SIMD instructions in AVX-512
// TODO: those workarounds are inefficient and need optimizing
#if __MIC__
static inline __m512i _mm512_loadu_si512(void const *addr)
{
	char JTR_ALIGN(64) buf[64];
	return _mm512_load_si512(is_aligned(addr, 64) ?
	                         addr : memcpy(buf, addr, 64));
}

static inline void _mm512_storeu_si512(void *addr, vtype d)
{
	char JTR_ALIGN(64) buf[64];

	if (is_aligned(addr, 64))
		_mm512_store_si512(addr, d);
	else {
		_mm512_store_si512(buf, d);
		memcpy(addr, buf, 64);
	}
}

#define vswap32(n)                                                          \
    n = vxor(vsrli_epi32(n, 24),                                            \
             vxor(vslli_epi32(vsrli_epi32(vslli_epi32(n, 8), 24), 8),       \
                  vxor(vsrli_epi32(vslli_epi32(vsrli_epi32(n, 8), 24), 8),  \
                       vslli_epi32(n, 24))))
#define vswap64(n) {                                \
    n = vshuffle_epi32(n, _MM_SHUFFLE(2, 3, 0, 1)); \
    vswap32(n);                                     \
}
#else /* non-MIC AVX512 (untested) */
#define vswap32(n)                                              \
    n = vshuffle_epi8(n, vset_epi32(0x3c3d3e3f, 0x38393a3b,     \
                                    0x34353637, 0x30313233,     \
                                    0x2c2d2e2f, 0x28292a2b,     \
                                    0x24252627, 0x20212223,     \
                                    0x1c1d1e1f, 0x18191a1b,     \
                                    0x14151617, 0x10111213,     \
                                    0x0c0d0e0f, 0x08090a0b,     \
                                    0x04050607, 0x00010203))
#define vswap64(n) \
    n = vshuffle_epi8(n, vset_epi64(0x38393a3b3c3d3e3f, 0x3031323334353637, \
                                    0x28292a2b2c2d2e2f, 0x2021222324252627, \
                                    0x18191a1b1c1d1e1f, 0x1011121314151617, \
                                    0x08090a0b0c0d0e0f, 0x0001020304050607))
#endif /* MIC */

#define vtestz_epi32(n)         !_mm512_min_epu32(n)
#define vtesteq_epi32(x, y)                                     \
    _mm512_mask2int(_mm512_cmp_epi32_mask(x, y, _MM_CMPINT_EQ))

#define GATHER_4x(x, y, z)                               \
{                                                        \
    vtype indices = vset_epi32(15<<6,14<<6,13<<6,12<<6,  \
                               11<<6,10<<6, 9<<6, 8<<6,  \
                                7<<6, 6<<6, 5<<6, 4<<6,  \
                                3<<6, 2<<6, 1<<6, 0<<6); \
    x = vgather_epi32(&y[z], indices, sizeof(y[z]));     \
}
#define GATHER_2x(x, y, z)                               \
{                                                        \
    vtype indices = vset_epi32(15<<5,14<<5,13<<5,12<<5,  \
                               11<<5,10<<5, 9<<5, 8<<5,  \
                                7<<5, 6<<5, 5<<5, 4<<5,  \
                                3<<5, 2<<5, 1<<5, 0<<5); \
    x = vgather_epi32(&y[z], indices, sizeof(y[z]));     \
}
#define GATHER(x, y, z)                                  \
{                                                        \
    vtype indices = vset_epi32(15<<4,14<<4,13<<4,12<<4,  \
                               11<<4,10<<4, 9<<4, 8<<4,  \
                                7<<4, 6<<4, 5<<4, 4<<4,  \
                                3<<4, 2<<4, 1<<4, 0<<4); \
    x = vgather_epi32(&y[z], indices, sizeof(y[z]));     \
}

#define GATHER64(x, y, z)                                \
{                                                        \
    x = vset_epi64x(y[7][z], y[6][z], y[5][z], y[4][z],  \
                    y[3][z], y[2][z], y[1][z], y[0][z]); \
}

/******************************** AVX2 ********************************/
#elif __AVX2__
#include <immintrin.h>

typedef __m256i vtype;

#define vadd_epi32              _mm256_add_epi32
#define vadd_epi64              _mm256_add_epi64
#define vand                    _mm256_and_si256
#define vandnot                 _mm256_andnot_si256
#define vcmov(y, z, x)          vxor(z, vand(x, vxor(y, z)))
#define vcmpeq_epi8             _mm256_cmpeq_epi8
#define vcmpeq_epi32            _mm256_cmpeq_epi32
#define vcvtsi32                _mm256_cvtsi32_si256
#define vgather_epi32(b, i, s)  _mm256_i32gather_epi32((void*)(b), i, s)
#define vinsert_epi32           _mm256_insert_epi32
#define vload(x)                _mm256_load_si256((void*)(x))
#define vloadu(x)               _mm256_loadu_si256((void*)(x))
#define vmovemask_epi8          _mm256_movemask_epi8
#define vor                     _mm256_or_si256
#define vpermute2x128           _mm256_permute2x128_si256
#define vpermute4x64_epi64      _mm256_permute4x64_epi64
#define vset1_epi8              _mm256_set1_epi8
#define vset1_epi32             _mm256_set1_epi32
#define vset1_epi64x            _mm256_set1_epi64x
#define vset_epi32              _mm256_set_epi32
#define vset_epi64x             _mm256_set_epi64x
#define vsetzero                _mm256_setzero_si256
#define vshuffle_epi8           _mm256_shuffle_epi8
#define vshuffle_epi32          _mm256_shuffle_epi32
#define vshufflehi_epi16        _mm256_shufflehi_epi16
#define vshufflelo_epi16        _mm256_shufflelo_epi16
#define vslli_epi16             _mm256_slli_epi16
#define vslli_epi32             _mm256_slli_epi32
#define vslli_epi64             _mm256_slli_epi64
#define vsrli_epi16             _mm256_srli_epi16
#define vsrli_epi32             _mm256_srli_epi32
#define vsrli_epi64             _mm256_srli_epi64
#define vstore(x, y)            _mm256_store_si256((void*)(x), y)
#define vstoreu(x, y)           _mm256_storeu_si256((void*)(x), y)
#define vunpackhi_epi32         _mm256_unpackhi_epi32
#define vunpackhi_epi64         _mm256_unpackhi_epi64
#define vunpacklo_epi32         _mm256_unpacklo_epi32
#define vunpacklo_epi64         _mm256_unpacklo_epi64
#define vxor                    _mm256_xor_si256

#define swap_endian_mask                                                \
    _mm256_set_epi32(0x1c1d1e1f, 0x18191a1b, 0x14151617, 0x10111213,    \
                     0x0c0d0e0f, 0x08090a0b, 0x04050607, 0x00010203)

#define swap_endian64_mask                              \
    vset_epi64x(0x18191a1b1c1d1e1f, 0x1011121314151617, \
                0x08090a0b0c0d0e0f, 0x0001020304050607)

#define vswap32(n)                              \
    (n = vshuffle_epi8(n, swap_endian_mask))

#define vswap64(n)                              \
    (n = vshuffle_epi8(n, swap_endian64_mask))

#if __clang__
static inline int vtestz_epi32(vtype __X)
{
	uint32_t JTR_ALIGN(SIMD_COEF_32 * 4) words[4];
	vstore(words, __X);
	return !words[0] || !words[1] || !words[2] || !words[3];
}
#else
// This is a modified SSE2 port of Algorithm 6-2 from "Hackers Delight" by
// Henry Warren, ISBN 0-201-91465-4. Returns non-zero if any double word in X
// is zero using a branchless algorithm. -- taviso.
#if !__INTEL_COMPILER && !__llvm__
// This intrinsic is not always available in GCC, so define it here.
static inline int vtestz(vtype __M, vtype __V)
{
	return __builtin_ia32_ptestz256((__v4di)__M, (__v4di)__V);
}
#endif
static inline int vtestz_epi32(vtype __X)
{
	vtype M = vcmpeq_epi32(__X, __X);
	vtype Z = vsrli_epi32(M, 1);
	vtype Y = vandnot(vor(vor(vadd_epi32(vand(__X, Z), Z), __X), Z), M);
	return ! vtestz(Y, M);
}
#endif /* __clang__ */

#define vtesteq_epi32(x, y)                                         \
(                                                                   \
    0xffffffff != vmovemask_epi8(vcmpeq_epi32(vcmpeq_epi32(x, y),   \
                                              vsetzero()))          \
)

#define GATHER_4x(x, y, z)                           \
{                                                    \
    vtype indices = vset_epi32(7<<6,6<<6,5<<6,4<<6,  \
                               3<<6,2<<6,1<<6,0<<6); \
    x = vgather_epi32(&y[z], indices, sizeof(y[z])); \
}
#define GATHER_2x(x, y, z)                           \
{                                                    \
    vtype indices = vset_epi32(7<<5,6<<5,5<<5,4<<5,  \
                               3<<5,2<<5,1<<5,0<<5); \
    x = vgather_epi32(&y[z], indices, sizeof(y[z])); \
}
#define GATHER(x, y, z)                              \
{                                                    \
    vtype indices = vset_epi32(7<<4,6<<4,5<<4,4<<4,  \
                               3<<4,2<<4,1<<4,0<<4); \
    x = vgather_epi32(&y[z], indices, sizeof(y[z])); \
}

#define GATHER64(x,y,z)                                         \
    { x = vset_epi64x (y[3][z], y[2][z], y[1][z], y[0][z]); }

/************************* SSE2/3/4/AVX/XOP ***************************/
#elif __SSE2__

#if __XOP__
#include <x86intrin.h>
#elif __AVX__
#include <immintrin.h>
#elif __SSE4_1__
#include <smmintrin.h>
#elif __SSSE3__
#include <tmmintrin.h>
#endif

#include <emmintrin.h>

typedef __m128i vtype;

#define vadd_epi32              _mm_add_epi32
#define vadd_epi64              _mm_add_epi64
#define vand                    _mm_and_si128
#define vandnot                 _mm_andnot_si128
#if __XOP__
#define vcmov                   _mm_cmov_si128
#else
#define vcmov(y, z, x)          vxor(z, vand(x, vxor(y, z)))
#endif
#define vcmpeq_epi8             _mm_cmpeq_epi8
#define vcmpeq_epi32            _mm_cmpeq_epi32
#if __SSE4_1__
#define vcvtsi32                _mm_cvtsi32_si128
#endif
#define vinsert_epi32           _mm_insert_epi32
#define vload(x)                _mm_load_si128((void*)(x))
#define vloadu(x)               _mm_loadu_si128((void*)(x))
#define vmovemask_epi8          _mm_movemask_epi8
#define vor                     _mm_or_si128
#define vpermute4x64_epi64      _mm_permute4x64_epi64
#define vpermute2x128           _mm_permute2x128_si128
#if __XOP__
#define vroti_epi16             _mm_roti_epi16
#define vroti_epi32             _mm_roti_epi32
#define vroti16_epi32           _mm_roti_epi32
#define vroti_epi64             _mm_roti_epi64
#endif
#define vset_epi32              _mm_set_epi32
#define vset1_epi8              _mm_set1_epi8
#define vset1_epi32             _mm_set1_epi32
#define vset1_epi64x            _mm_set1_epi64x
#define vset_epi64x             _mm_set_epi64x
#define vsetzero                _mm_setzero_si128
#if __SSSE3__
#define vshuffle_epi8           _mm_shuffle_epi8
#endif
#define vshuffle_epi32          _mm_shuffle_epi32
#define vshufflehi_epi16        _mm_shufflehi_epi16
#define vshufflelo_epi16        _mm_shufflelo_epi16
#define vslli_epi16             _mm_slli_epi16
#define vslli_epi32             _mm_slli_epi32
#define vslli_epi64             _mm_slli_epi64
#define vsrli_epi16             _mm_srli_epi16
#define vsrli_epi32             _mm_srli_epi32
#define vsrli_epi64             _mm_srli_epi64
#define vstore(x, y)            _mm_store_si128((void*)(x), y)
#define vstoreu(x, y)           _mm_storeu_si128((void*)(x), y)
#define vunpackhi_epi32         _mm_unpackhi_epi32
#define vunpackhi_epi64         _mm_unpackhi_epi64
#define vunpacklo_epi32         _mm_unpacklo_epi32
#define vunpacklo_epi64         _mm_unpacklo_epi64
#define vxor                    _mm_xor_si128

#if !__SSE4_1__ || __clang__
static inline int vtestz_epi32(vtype __X)
{
	uint32_t JTR_ALIGN(SIMD_COEF_32 * 4) words[4];
	vstore(words, __X);
	return !words[0] || !words[1] || !words[2] || !words[3];
}
#else
// This is a modified SSE2 port of Algorithm 6-2 from "Hackers Delight" by
// Henry Warren, ISBN 0-201-91465-4. Returns non-zero if any double word in X
// is zero using a branchless algorithm. -- taviso.
#if !__INTEL_COMPILER && !__llvm__
// This intrinsic is not always available in GCC, so define it here.
static inline int vtestz(vtype __M, vtype __V)
{
	return __builtin_ia32_ptestz128((__v2di)__M, (__v2di)__V);
}
#endif
static inline int vtestz_epi32(vtype __X)
{
	vtype M = vcmpeq_epi32(__X, __X);
	vtype Z = vsrli_epi32(M, 1);
	vtype Y = vandnot(vor(vor(vadd_epi32(vand(__X, Z), Z), __X), Z), M);
	return ! vtestz(Y, M);
}
#endif /* !__SSE4_1__ || __clang__ */

#define vtesteq_epi32(x, y)   \
    (0xffff != vmovemask_epi8(vcmpeq_epi32(vcmpeq_epi32(x, y), vsetzero())))

#if __SSSE3__

#define swap_endian_mask    \
    vset_epi32(0x0c0d0e0f, 0x08090a0b, 0x04050607, 0x00010203)
#define vswap32(n)              (n = vshuffle_epi8(n, swap_endian_mask))

#define swap_endian64_mask  \
    vset_epi64x(0x08090a0b0c0d0e0fULL, 0x0001020304050607ULL)
#define vswap64(n)              (n = vshuffle_epi8(n, swap_endian64_mask))

#else /* Just basic SSE2 */

#define vswap32(n)                                  \
    (n = vxor(                                      \
        vsrli_epi16(                                \
            vroti16_epi32(n,16), 8),                \
            vslli_epi16(vroti16_epi32(n,16), 8)))

#define vswap64(n)                                          \
  {                                                         \
    n = vshufflehi_epi16(vshufflelo_epi16(n, 0xb1), 0xb1);  \
    n = vxor(vslli_epi16(n, 8), vsrli_epi16(n, 8));         \
    n = vshuffle_epi32(n, 0xb1);                            \
  }

#endif /* __SSSE3__ */

#if __SSE4_1__
#define GATHER_4x(x, y, z)                      \
{                                               \
    x = vcvtsi32(   y[z]   );                   \
    x = vinsert_epi32(x, y[z+(1<<6)], 1);       \
    x = vinsert_epi32(x, y[z+(2<<6)], 2);       \
    x = vinsert_epi32(x, y[z+(3<<6)], 3);       \
}
#define GATHER_2x(x, y, z)                      \
{                                               \
    x = vcvtsi32(   y[z]   );                   \
    x = vinsert_epi32(x, y[z+(1<<5)], 1);       \
    x = vinsert_epi32(x, y[z+(2<<5)], 2);       \
    x = vinsert_epi32(x, y[z+(3<<5)], 3);       \
}
#define GATHER(x, y, z)                         \
{                                               \
    x = vcvtsi32(   y[z]   );                   \
    x = vinsert_epi32(x, y[z+(1<<4)], 1);       \
    x = vinsert_epi32(x, y[z+(2<<4)], 2);       \
    x = vinsert_epi32(x, y[z+(3<<4)], 3);       \
}
#endif /* __SSE4_1__ */

#define GATHER64(x,y,z)     { x = vset_epi64x (y[1][z], y[0][z]); }

/******************************** MMX *********************************/

#elif __MMX__
#include <mmintrin.h>

typedef __m64i vtype;

#error MMX intrinsics not implemented (contributions are welcome!)

#endif /* __SIMD__ elif __SIMD__ elif __SIMD__ */

/************************* COMMON STUFF BELOW *************************/

#define MEM_ALIGN_SIMD          (SIMD_COEF_32 * 4)

#if !__XOP__ || __AVX2__ || __MIC__

#if __SSE3__ || __MIC__
#define vslli_epi16a(a, s) vslli_epi16(a, s)
#define vslli_epi32a(a, s) vslli_epi32(a, s)
#define vslli_epi64a(a, s) vslli_epi64(a, s)

#else
#define vslli_epi16a(a, s) ((s) == 1 ?              \
     vadd_epi16((a), (a)) : vslli_epi16((a), (s)))

#define vslli_epi32a(a, s) ((s) == 1 ?              \
     vadd_epi32((a), (a)) : vslli_epi32((a), (s)))

#define vslli_epi64a(a, s) ((s) == 1 ?              \
     vadd_epi64((a), (a)) : vslli_epi64((a), (s)))

#endif /* __SSE3__ || __MIC__ */

// vroti must handle both ROTL and ROTR. If s < 0, then ROTR.
// There's a specialized rotate16 for SSSE3
#define vroti_epi16(a, s)  ((s) < 0 ?                                   \
     vxor(vsrli_epi16((a), ~(s) + 1), vslli_epi16a((a), 16 + (s))) :    \
     vxor(vslli_epi16a((a), (s)), vsrli_epi16((a), 16 - (s))))

#define vroti_epi32(a, s)  ((s) < 0 ?                                   \
     vxor(vsrli_epi32((a), ~(s) + 1), vslli_epi32a((a), 32 + (s))) :    \
     vxor(vslli_epi32a((a), (s)), vsrli_epi32((a), 32 - (s))))

#define vroti_epi64(a, s)  ((s) < 0 ?                                   \
     vxor(vsrli_epi64((a), ~(s) + 1), vslli_epi64a((a), 64 + (s))) :    \
     vxor(vslli_epi64a((a), (s)), vsrli_epi64((a), 64 - (s))))

#if __AVX512__ || __MIC__
#define vroti16_epi32(a,s) vroti_epi32(a, 16)

#elif __AVX2__
#define rot16_mask                                              \
    vset_epi32(0x0d0c0f0e, 0x09080b0a, 0x05040706, 0x01000302,  \
               0x0d0c0f0e, 0x09080b0a, 0x05040706, 0x01000302)
#define vroti16_epi32(a, s)     (vshuffle_epi8((a), rot16_mask))

#elif __SSSE3__
#define rot16_mask  \
        vset_epi32(0x0d0c0f0e, 0x09080b0a, 0x05040706, 0x01000302)
#define vroti16_epi32(a, s)     (vshuffle_epi8((a), rot16_mask))

#else /* just SSE2 */
#define vroti16_epi32(a,s)                                      \
        (vshufflelo_epi16(vshufflehi_epi16((a), 0xb1), 0xb1))

#endif /* __AVX512__ || __MIC__ */

#endif /* !__XOP__ || __AVX2__ || __MIC__ */

#endif /* SIMD_COEF_32 */

#endif /* _SSE_PSEUDO_H */
