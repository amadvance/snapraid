// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Andrea Mazzoleni

/*
 * Derivative work of the XXH3-128 v8.4 implementation.
 *
 * This file implements the XXH3 long accumulator mode with support for
 * arbitrary input lengths. Unlike the original XXH3, which switches to separate
 * small/midsize routines for len <= 240 bytes, this implementation extends the
 * long mode:
 *   - For len >= 64 bytes, it uses the striped accumulator loop directly.
 *   - For len < 64 bytes, the input is zero-padded to 64 bytes, and the original
 *     length is folded into the finalizer (preventing padding collisions).
 *
 * The motivation for extending the long mode to all lengths rather than using
 * the upstream short/midsize routines is that SnapRAID operates primarily on
 * large data blocks (typically 256 KB) where SIMD-accelerated long mode delivers
 * maximum throughput. Short inputs occur only as rare edge cases (such as
 * empty or small files), and maintaining a single code path reduces algorithmic
 * complexity and defect potential.
 *
 * seed points to 16 bytes, interpreted as two little-endian uint64_t words:
 * seed_lo then seed_hi.
 *   - An all-zero seed uses the unmodified standard XXH3 secret.
 *   - For len > 240, setting seed_lo == seed_hi matches standard
 *     XXH3_128bits_withSeed() with that 64-bit seed value.
 *   - When seed_lo != seed_hi, the 128-bit seed extends the secret by adding
 *     seed_lo to even 64-bit words and subtracting seed_hi from odd words.
 *
 * For len <= 240, the output is not bit-compatible with original xxHash due to
 * using the padded long mode rather than the upstream short/midsize routines.
 */

/*
 * xxHash - Extremely Fast Hash algorithm
 * Development source file for `xxh3`
 * Copyright (C) 2019-2021 Yann Collet
 *
 * BSD 2-Clause License (https://www.opensource.org/licenses/bsd-license.php)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following disclaimer
 *      in the documentation and/or other materials provided with the
 *      distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You can contact the author at:
 *   - xxHash homepage: https://www.xxhash.com
 *   - xxHash source repository: https://github.com/Cyan4973/xxHash
 */

/**
 * Standalone SMHasher3 adapter for cmdline/xxh3.c.
 * Both entry points retain SnapRAID's canonical little-endian reads and output.
 * SMHasher3's 64-bit seed is duplicated into the two 64-bit seed halves.
 */
#include "Platform.h"
#include "Hashlib.h"
#include "Mathmult.h"

/**
 * Change the #if values here to select an implementation manually.
 * SIMD entry points require a compatible CPU; there is no runtime dispatch.
 * The target attributes below preserve the existing SMHasher3 compiler flags.
 */
#if 0
#define XXH3_SNAPRAID_HASH_FN xxh3_128
#define XXH3_SNAPRAID_IMPL "scalar"
#elif 0
#define XXH3_SNAPRAID_HASH_FN xxh3_128_avx512
#define XXH3_SNAPRAID_IMPL "avx512"
#elif 0
#define XXH3_SNAPRAID_HASH_FN xxh3_128_avx2
#define XXH3_SNAPRAID_IMPL "avx2"
#elif defined(__i386__) || defined(__x86_64__)
#define XXH3_SNAPRAID_HASH_FN xxh3_128_sse2
#define XXH3_SNAPRAID_IMPL "sse2"
#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
#define XXH3_SNAPRAID_HASH_FN xxh3_128_neon
#define XXH3_SNAPRAID_IMPL "neon"
#else
#define XXH3_SNAPRAID_HASH_FN xxh3_128
#define XXH3_SNAPRAID_IMPL "scalar"
#endif

static FORCE_INLINE uint64_t xxh3_read64(const uint8_t* ptr)
{
	return COND_BSWAP(GET_U64<false>(ptr, 0), isBE());
}

static FORCE_INLINE void xxh3_write64(uint8_t* ptr, uint64_t value)
{
	PUT_U64<false>(COND_BSWAP(value, isBE()), ptr, 0);
}

#if defined(__i386__) || defined(__x86_64__)
#include <immintrin.h>

#define XXH3_TARGET_SSE2   __attribute__((target("sse2")))
#define XXH3_TARGET_AVX2   __attribute__((target("avx2")))
#define XXH3_TARGET_AVX512 __attribute__((target("avx512f")))
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

#define XXH3_STRIPE_LEN 64
#define XXH3_SECRET_SIZE 192
#define XXH3_SECRET_CONSUME_RATE 8
#define XXH3_ACC_NB 8
#define XXH3_STRIPES_PER_BLOCK 16
#define XXH3_BLOCK_LEN 1024
#define XXH3_LAST_SECRET_OFFSET 121
#define XXH3_MERGE_OFFSET 11

#define XXH_PRIME32_1 0x9E3779B1U
#define XXH_PRIME32_2 0x85EBCA77U
#define XXH_PRIME32_3 0xC2B2AE3DU

#define XXH_PRIME64_1 0x9E3779B185EBCA87ULL
#define XXH_PRIME64_2 0xC2B2AE3D27D4EB4FULL
#define XXH_PRIME64_3 0x165667B19E3779F9ULL
#define XXH_PRIME64_4 0x85EBCA77C2B2AE63ULL
#define XXH_PRIME64_5 0x27D4EB2F165667C5ULL

#define XXH_PRIME_MX1 0x165667919E3779F9ULL

#define XXH3_INIT_ACC_VALUES \
	{ XXH_PRIME32_3, XXH_PRIME64_1, XXH_PRIME64_2, XXH_PRIME64_3, \
	  XXH_PRIME64_4, XXH_PRIME32_2, XXH_PRIME64_5, XXH_PRIME32_1 }

alignas(64) static const uint8_t xxh3_secret[XXH3_SECRET_SIZE] = {
	0xb8, 0xfe, 0x6c, 0x39, 0x23, 0xa4, 0x4b, 0xbe, 0x7c, 0x01, 0x81, 0x2c, 0xf7, 0x21, 0xad, 0x1c,
	0xde, 0xd4, 0x6d, 0xe9, 0x83, 0x90, 0x97, 0xdb, 0x72, 0x40, 0xa4, 0xa4, 0xb7, 0xb3, 0x67, 0x1f,
	0xcb, 0x79, 0xe6, 0x4e, 0xcc, 0xc0, 0xe5, 0x78, 0x82, 0x5a, 0xd0, 0x7d, 0xcc, 0xff, 0x72, 0x21,
	0xb8, 0x08, 0x46, 0x74, 0xf7, 0x43, 0x24, 0x8e, 0xe0, 0x35, 0x90, 0xe6, 0x81, 0x3a, 0x26, 0x4c,
	0x3c, 0x28, 0x52, 0xbb, 0x91, 0xc3, 0x00, 0xcb, 0x88, 0xd0, 0x65, 0x8b, 0x1b, 0x53, 0x2e, 0xa3,
	0x71, 0x64, 0x48, 0x97, 0xa2, 0x0d, 0xf9, 0x4e, 0x38, 0x19, 0xef, 0x46, 0xa9, 0xde, 0xac, 0xd8,
	0xa8, 0xfa, 0x76, 0x3f, 0xe3, 0x9c, 0x34, 0x3f, 0xf9, 0xdc, 0xbb, 0xc7, 0xc7, 0x0b, 0x4f, 0x1d,
	0x8a, 0x51, 0xe0, 0x4b, 0xcd, 0xb4, 0x59, 0x31, 0xc8, 0x9f, 0x7e, 0xc9, 0xd9, 0x78, 0x73, 0x64,
	0xea, 0xc5, 0xac, 0x83, 0x34, 0xd3, 0xeb, 0xc3, 0xc5, 0x81, 0xa0, 0xff, 0xfa, 0x13, 0x63, 0xeb,
	0x17, 0x0d, 0xdd, 0x51, 0xb7, 0xf0, 0xda, 0x49, 0xd3, 0x16, 0x55, 0x26, 0x29, 0xd4, 0x68, 0x9e,
	0x2b, 0x16, 0xbe, 0x58, 0x7d, 0x47, 0xa1, 0xfc, 0x8f, 0xf8, 0xb8, 0xd1, 0x7a, 0xd0, 0x31, 0xce,
	0x45, 0xcb, 0x3a, 0x8f, 0x95, 0x16, 0x04, 0x28, 0xaf, 0xd7, 0xfb, 0xca, 0xbb, 0x4b, 0x40, 0x7e
};

static FORCE_INLINE uint64_t xxh3_xorshift64(uint64_t x, unsigned shift)
{
	return x ^ (x >> shift);
}

static FORCE_INLINE uint64_t xxh3_avalanche(uint64_t x)
{
	x = xxh3_xorshift64(x, 37);
	x *= XXH_PRIME_MX1;
	return xxh3_xorshift64(x, 32);
}

static FORCE_INLINE uint64_t xxh3_mul128_fold64(uint64_t a, uint64_t b)
{
	MathMult::mult64_128(a, b, a, b);
	return a ^ b;
}

/*
 * 128-bit seed extension.
 *
 * Standard XXH3 with a 64-bit seed transforms every 16-byte secret pair as:
 *
 *     secret[even] += seed;
 *     secret[odd]  -= seed;
 *
 * Here the 16-byte seed is split into two little-endian 64-bit words and the
 * same construction is extended naturally to:
 *
 *     secret[even] += seed_lo;
 *     secret[odd]  -= seed_hi;
 *
 * Therefore both halves of the seed affect every 16-byte secret pair.
 */
static void xxh3_make_seeded_secret(uint8_t* out, uint64_t seed_lo, uint64_t seed_hi)
{
	for (size_t i = 0; i < XXH3_SECRET_SIZE; i += 16) {
		xxh3_write64(out + i, xxh3_read64(xxh3_secret + i) + seed_lo);
		xxh3_write64(out + i + 8, xxh3_read64(xxh3_secret + i + 8) - seed_hi);
	}
}

static FORCE_INLINE const uint8_t* xxh3_select_secret(const uint8_t* seed, uint8_t* custom)
{
	uint64_t seed_lo = xxh3_read64(seed);
	uint64_t seed_hi = xxh3_read64(seed + 8);

	if ((seed_lo | seed_hi) == 0)
		return xxh3_secret;

	xxh3_make_seeded_secret(custom, seed_lo, seed_hi);
	return custom;
}

static FORCE_INLINE uint64_t xxh3_mix2accs(const uint64_t* acc, const uint8_t* secret)
{
	return xxh3_mul128_fold64(acc[0] ^ xxh3_read64(secret), acc[1] ^ xxh3_read64(secret + 8));
}

static FORCE_INLINE uint64_t xxh3_merge_accs(const uint64_t* acc, const uint8_t* secret, uint64_t start)
{
	uint64_t h = start;
	for (unsigned i = 0; i < 4; ++i)
		h += xxh3_mix2accs(acc + 2 * i, secret + 16 * i);
	return xxh3_avalanche(h);
}

static FORCE_INLINE void xxh3_finalize128(const uint64_t* acc, const uint8_t* secret, size_t len, uint8_t* out)
{
	uint64_t low64 = xxh3_merge_accs(acc, secret + XXH3_MERGE_OFFSET, (uint64_t)len * XXH_PRIME64_1);
	uint64_t high64 = xxh3_merge_accs(acc, secret + XXH3_SECRET_SIZE - XXH3_STRIPE_LEN - XXH3_MERGE_OFFSET, ~((uint64_t)len * XXH_PRIME64_2));
	xxh3_write64(out, low64);
	xxh3_write64(out + 8, high64);
}

static FORCE_INLINE void xxh3_acc512_c(uint64_t* acc, const uint8_t* input, const uint8_t* secret)
{
	for (unsigned lane = 0; lane < XXH3_ACC_NB; ++lane) {
		uint64_t data = xxh3_read64(input + 8 * lane);
		uint64_t keyed = data ^ xxh3_read64(secret + 8 * lane);
		uint64_t product = (uint64_t)(uint32_t)keyed * (uint32_t)(keyed >> 32);

		acc[lane ^ 1] += data; /* exchange adjacent 64-bit lanes */
		acc[lane] += product;
	}
}

static FORCE_INLINE void xxh3_scramble_c(uint64_t* acc, const uint8_t* secret)
{
	for (unsigned lane = 0; lane < XXH3_ACC_NB; ++lane) {
		uint64_t x = xxh3_xorshift64(acc[lane], 47);
		x ^= xxh3_read64(secret + 8 * lane);
		acc[lane] = x * XXH_PRIME32_1;
	}
}

static FORCE_INLINE void xxh3_short_core(const void* input, size_t len, const uint8_t* secret, uint8_t* out)
{
	alignas(64) uint8_t buf[XXH3_STRIPE_LEN];
	alignas(64) uint64_t acc[XXH3_ACC_NB] = XXH3_INIT_ACC_VALUES;

	memset(buf, 0, XXH3_STRIPE_LEN);
	if (len > 0)
		memcpy(buf, input, len);

	xxh3_acc512_c(acc, buf, secret + XXH3_LAST_SECRET_OFFSET);
	xxh3_finalize128(acc, secret, len, out);
}

/*
 * With the 192-byte default secret:
 *   stripes per block = (192 - 64) / 8 = 16
 *   block size        = 16 * 64 = 1024 bytes
 */
static FORCE_INLINE void xxh3_long_core_c(const void* input, size_t len, const uint8_t* secret, uint8_t* out)
{
	const uint8_t* p = static_cast<const uint8_t*>(input);
	alignas(64) uint64_t acc[XXH3_ACC_NB] = XXH3_INIT_ACC_VALUES;
	size_t blocks = (len - 1) / XXH3_BLOCK_LEN;

	for (size_t b = 0; b < blocks; ++b) {
		const uint8_t* block = p + b * XXH3_BLOCK_LEN;
		for (size_t s = 0; s < XXH3_STRIPES_PER_BLOCK; ++s)
			xxh3_acc512_c(acc, block + s * XXH3_STRIPE_LEN, secret + s * XXH3_SECRET_CONSUME_RATE);
		xxh3_scramble_c(acc, secret + XXH3_SECRET_SIZE - XXH3_STRIPE_LEN);
	}

	size_t stripes = ((len - 1) - blocks * XXH3_BLOCK_LEN) / XXH3_STRIPE_LEN;
	const uint8_t* block = p + blocks * XXH3_BLOCK_LEN;
	for (size_t s = 0; s < stripes; ++s)
		xxh3_acc512_c(acc, block + s * XXH3_STRIPE_LEN, secret + s * XXH3_SECRET_CONSUME_RATE);

	xxh3_acc512_c(acc, p + len - XXH3_STRIPE_LEN, secret + XXH3_LAST_SECRET_OFFSET);
	xxh3_finalize128(acc, secret, len, out);
}

/*
 * seed points to 16 bytes: seed_lo then seed_hi, both little-endian.
 * out receives low64 then high64, both little-endian.
 *
 * No CPU detection is performed. The caller must invoke only a SIMD entry
 * point supported by the current CPU.
 */
static void xxh3_128(const void* bytes, size_t len, const uint8_t* seed, uint8_t* out)
{
	alignas(64) uint8_t custom[XXH3_SECRET_SIZE];
	const uint8_t* secret;

	secret = xxh3_select_secret(seed, custom);

	if (unlikely(len < XXH3_STRIPE_LEN))
		xxh3_short_core(bytes, len, secret, out);
	else
		xxh3_long_core_c(bytes, len, secret, out);
}

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
static FORCE_INLINE uint64x2_t xxh3_vld1q_u64(const void* ptr)
{
	return vreinterpretq_u64_u8(vld1q_u8((const uint8_t*)ptr));
}

static FORCE_INLINE uint64x2_t xxh3_vmlal_low_u32(uint64x2_t acc, uint32x4_t lhs, uint32x4_t rhs)
{
	return vmlal_u32(acc, vget_low_u32(lhs), vget_low_u32(rhs));
}

static FORCE_INLINE uint64x2_t xxh3_vmlal_high_u32(uint64x2_t acc, uint32x4_t lhs, uint32x4_t rhs)
{
#if defined(__aarch64__)
	return vmlal_high_u32(acc, lhs, rhs);
#else
	return vmlal_u32(acc, vget_high_u32(lhs), vget_high_u32(rhs));
#endif
}

static FORCE_INLINE void xxh3_acc512_neon(uint64_t* acc, const uint8_t* input, const uint8_t* secret)
{
	for (unsigned i = 0; i < 4; i += 2) {
		uint64x2_t data_vec_1 = xxh3_vld1q_u64(input + i * 16);
		uint64x2_t data_vec_2 = xxh3_vld1q_u64(input + (i + 1) * 16);
		uint64x2_t key_vec_1 = xxh3_vld1q_u64(secret + i * 16);
		uint64x2_t key_vec_2 = xxh3_vld1q_u64(secret + (i + 1) * 16);

		uint64x2_t data_swap_1 = vextq_u64(data_vec_1, data_vec_1, 1);
		uint64x2_t data_swap_2 = vextq_u64(data_vec_2, data_vec_2, 1);

		uint64x2_t data_key_1 = veorq_u64(data_vec_1, key_vec_1);
		uint64x2_t data_key_2 = veorq_u64(data_vec_2, key_vec_2);

		uint32x4x2_t unzipped = vuzpq_u32(
			vreinterpretq_u32_u64(data_key_1),
			vreinterpretq_u32_u64(data_key_2)
		);
		uint32x4_t data_key_lo = unzipped.val[0];
		uint32x4_t data_key_hi = unzipped.val[1];

		uint64x2_t sum_1 = xxh3_vmlal_low_u32(data_swap_1, data_key_lo, data_key_hi);
		uint64x2_t sum_2 = xxh3_vmlal_high_u32(data_swap_2, data_key_lo, data_key_hi);

		/* typed loads and stores preserve scalar lane order on big-endian ARM */
		vst1q_u64(acc + i * 2, vaddq_u64(vld1q_u64(acc + i * 2), sum_1));
		vst1q_u64(acc + (i + 1) * 2, vaddq_u64(vld1q_u64(acc + (i + 1) * 2), sum_2));
	}
}

static FORCE_INLINE void xxh3_scramble_neon(uint64_t* acc, const uint8_t* secret)
{
	const uint32x2_t prime_lo = vdup_n_u32(XXH_PRIME32_1);
	const uint32x4_t prime_hi = vreinterpretq_u32_u64(vdupq_n_u64((uint64_t)XXH_PRIME32_1 << 32));

	for (unsigned i = 0; i < 4; ++i) {
		uint64x2_t acc_vec = vld1q_u64(acc + i * 2);
		uint64x2_t shifted = vshrq_n_u64(acc_vec, 47);
		uint64x2_t data_vec = veorq_u64(acc_vec, shifted);

		uint64x2_t key_vec = xxh3_vld1q_u64(secret + i * 16);
		uint64x2_t data_key = veorq_u64(data_vec, key_vec);

		uint32x4_t prod_hi = vmulq_u32(vreinterpretq_u32_u64(data_key), prime_hi);
		uint32x2_t data_key_lo = vmovn_u64(data_key);
		vst1q_u64(acc + i * 2, vmlal_u32(vreinterpretq_u64_u32(prod_hi), data_key_lo, prime_lo));
	}
}

static FORCE_INLINE void xxh3_long_core_neon(const void* input, size_t len, const uint8_t* secret, uint8_t* out)
{
	const uint8_t* p = static_cast<const uint8_t*>(input);
	alignas(64) uint64_t acc[XXH3_ACC_NB] = XXH3_INIT_ACC_VALUES;
	size_t blocks = (len - 1) / XXH3_BLOCK_LEN;

	for (size_t b = 0; b < blocks; ++b) {
		const uint8_t* block = p + b * XXH3_BLOCK_LEN;
		for (size_t s = 0; s < XXH3_STRIPES_PER_BLOCK; ++s)
			xxh3_acc512_neon(acc, block + s * XXH3_STRIPE_LEN, secret + s * XXH3_SECRET_CONSUME_RATE);
		xxh3_scramble_neon(acc, secret + XXH3_SECRET_SIZE - XXH3_STRIPE_LEN);
	}

	size_t stripes = ((len - 1) - blocks * XXH3_BLOCK_LEN) / XXH3_STRIPE_LEN;
	const uint8_t* block = p + blocks * XXH3_BLOCK_LEN;
	for (size_t s = 0; s < stripes; ++s)
		xxh3_acc512_neon(acc, block + s * XXH3_STRIPE_LEN, secret + s * XXH3_SECRET_CONSUME_RATE);

	xxh3_acc512_neon(acc, p + len - XXH3_STRIPE_LEN, secret + XXH3_LAST_SECRET_OFFSET);
	xxh3_finalize128(acc, secret, len, out);
}

static void xxh3_128_neon(const void* bytes, size_t len, const uint8_t* seed, uint8_t* out)
{
	alignas(64) uint8_t custom[XXH3_SECRET_SIZE];
	const uint8_t* secret;

	secret = xxh3_select_secret(seed, custom);

	if (unlikely(len < XXH3_STRIPE_LEN))
		xxh3_short_core(bytes, len, secret, out);
	else
		xxh3_long_core_neon(bytes, len, secret, out);
}
#endif

#if defined(__i386__) || defined(__x86_64__)
static XXH3_TARGET_SSE2 FORCE_INLINE void xxh3_acc512_sse2(uint64_t* acc, const uint8_t* input, const uint8_t* secret)
{
	__m128i* a = (__m128i*)(void*)acc;

	for (unsigned i = 0; i < 4; ++i) {
		__m128i data = _mm_loadu_si128((const __m128i*)(const void*)(input + 16 * i));
		__m128i key = _mm_loadu_si128((const __m128i*)(const void*)(secret + 16 * i));
		__m128i mix = _mm_xor_si128(data, key);
		__m128i hi32 = _mm_srli_epi64(mix, 32);
		__m128i prod = _mm_mul_epu32(mix, hi32);
		__m128i swap = _mm_shuffle_epi32(data, _MM_SHUFFLE(1, 0, 3, 2));
		a[i] = _mm_add_epi64(_mm_add_epi64(a[i], swap), prod);
	}
}

static XXH3_TARGET_SSE2 FORCE_INLINE void xxh3_scramble_sse2(uint64_t* acc, const uint8_t* secret)
{
	__m128i* a = (__m128i*)(void*)acc;
	const __m128i prime = _mm_set1_epi32((int)XXH_PRIME32_1);

	for (unsigned i = 0; i < 4; ++i) {
		__m128i x = a[i];
		__m128i key = _mm_loadu_si128((const __m128i*)(const void*)(secret + 16 * i));
		x = _mm_xor_si128(x, _mm_srli_epi64(x, 47));
		x = _mm_xor_si128(x, key);

		/* 64-bit x * 32-bit prime, modulo 2^64, using SSE2 32x32 multiplies. */
		__m128i lo = _mm_mul_epu32(x, prime);
		__m128i hi = _mm_mul_epu32(_mm_srli_epi64(x, 32), prime);
		a[i] = _mm_add_epi64(lo, _mm_slli_epi64(hi, 32));
	}
}

static XXH3_TARGET_AVX2 FORCE_INLINE void xxh3_acc512_avx2(uint64_t* acc, const uint8_t* input, const uint8_t* secret)
{
	__m256i* a = (__m256i*)(void*)acc;

	for (unsigned i = 0; i < 2; ++i) {
		__m256i data = _mm256_loadu_si256((const __m256i*)(const void*)(input + 32 * i));
		__m256i key = _mm256_loadu_si256((const __m256i*)(const void*)(secret + 32 * i));
		__m256i mix = _mm256_xor_si256(data, key);
		__m256i hi32 = _mm256_srli_epi64(mix, 32);
		__m256i prod = _mm256_mul_epu32(mix, hi32);
		__m256i swap = _mm256_shuffle_epi32(data, _MM_SHUFFLE(1, 0, 3, 2));
		a[i] = _mm256_add_epi64(_mm256_add_epi64(a[i], swap), prod);
	}
}

static XXH3_TARGET_AVX2 FORCE_INLINE void xxh3_scramble_avx2(uint64_t* acc, const uint8_t* secret)
{
	__m256i* a = (__m256i*)(void*)acc;
	const __m256i prime = _mm256_set1_epi32((int)XXH_PRIME32_1);

	for (unsigned i = 0; i < 2; ++i) {
		__m256i x = a[i];
		__m256i key = _mm256_loadu_si256((const __m256i*)(const void*)(secret + 32 * i));
		x = _mm256_xor_si256(x, _mm256_srli_epi64(x, 47));
		x = _mm256_xor_si256(x, key);

		__m256i lo = _mm256_mul_epu32(x, prime);
		__m256i hi = _mm256_mul_epu32(_mm256_srli_epi64(x, 32), prime);
		a[i] = _mm256_add_epi64(lo, _mm256_slli_epi64(hi, 32));
	}
}

static XXH3_TARGET_AVX512 FORCE_INLINE void xxh3_acc512_avx512(uint64_t* acc, const uint8_t* input, const uint8_t* secret)
{
	__m512i* a = (__m512i*)(void*)acc;
	__m512i data = _mm512_loadu_si512((const void*)input);
	__m512i key = _mm512_loadu_si512((const void*)secret);
	__m512i mix = _mm512_xor_si512(data, key);
	__m512i prod = _mm512_mul_epu32(mix, _mm512_srli_epi64(mix, 32));
	__m512i swap = _mm512_shuffle_epi32(data, _MM_PERM_BADC);
	*a = _mm512_add_epi64(_mm512_add_epi64(*a, swap), prod);
}

static XXH3_TARGET_AVX512 FORCE_INLINE void xxh3_scramble_avx512(uint64_t* acc, const uint8_t* secret)
{
	__m512i* a = (__m512i*)(void*)acc;
	const __m512i prime = _mm512_set1_epi32((int)XXH_PRIME32_1);
	__m512i x = *a;
	__m512i key = _mm512_loadu_si512((const void*)secret);
	__m512i shifted = _mm512_srli_epi64(x, 47);

	x = _mm512_ternarylogic_epi32(key, x, shifted, 0x96);

	__m512i lo = _mm512_mul_epu32(x, prime);
	__m512i hi = _mm512_mul_epu32(_mm512_srli_epi64(x, 32), prime);
	*a = _mm512_add_epi64(lo, _mm512_slli_epi64(hi, 32));
}

static XXH3_TARGET_SSE2 FORCE_INLINE void xxh3_long_core_sse2(const void* input, size_t len, const uint8_t* secret, uint8_t* out)
{
	const uint8_t* p = static_cast<const uint8_t*>(input);
	alignas(64) uint64_t acc[XXH3_ACC_NB] = XXH3_INIT_ACC_VALUES;
	size_t blocks = (len - 1) / XXH3_BLOCK_LEN;

	for (size_t b = 0; b < blocks; ++b) {
		const uint8_t* block = p + b * XXH3_BLOCK_LEN;
		for (size_t s = 0; s < XXH3_STRIPES_PER_BLOCK; ++s)
			xxh3_acc512_sse2(acc, block + s * XXH3_STRIPE_LEN, secret + s * XXH3_SECRET_CONSUME_RATE);
		xxh3_scramble_sse2(acc, secret + XXH3_SECRET_SIZE - XXH3_STRIPE_LEN);
	}

	size_t stripes = ((len - 1) - blocks * XXH3_BLOCK_LEN) / XXH3_STRIPE_LEN;
	const uint8_t* block = p + blocks * XXH3_BLOCK_LEN;
	for (size_t s = 0; s < stripes; ++s)
		xxh3_acc512_sse2(acc, block + s * XXH3_STRIPE_LEN, secret + s * XXH3_SECRET_CONSUME_RATE);

	xxh3_acc512_sse2(acc, p + len - XXH3_STRIPE_LEN, secret + XXH3_LAST_SECRET_OFFSET);
	xxh3_finalize128(acc, secret, len, out);
}

static XXH3_TARGET_AVX2 FORCE_INLINE void xxh3_long_core_avx2(const void* input, size_t len, const uint8_t* secret, uint8_t* out)
{
	const uint8_t* p = static_cast<const uint8_t*>(input);
	alignas(64) uint64_t acc[XXH3_ACC_NB] = XXH3_INIT_ACC_VALUES;
	size_t blocks = (len - 1) / XXH3_BLOCK_LEN;

	for (size_t b = 0; b < blocks; ++b) {
		const uint8_t* block = p + b * XXH3_BLOCK_LEN;
		for (size_t s = 0; s < XXH3_STRIPES_PER_BLOCK; ++s)
			xxh3_acc512_avx2(acc, block + s * XXH3_STRIPE_LEN, secret + s * XXH3_SECRET_CONSUME_RATE);
		xxh3_scramble_avx2(acc, secret + XXH3_SECRET_SIZE - XXH3_STRIPE_LEN);
	}

	size_t stripes = ((len - 1) - blocks * XXH3_BLOCK_LEN) / XXH3_STRIPE_LEN;
	const uint8_t* block = p + blocks * XXH3_BLOCK_LEN;
	for (size_t s = 0; s < stripes; ++s)
		xxh3_acc512_avx2(acc, block + s * XXH3_STRIPE_LEN, secret + s * XXH3_SECRET_CONSUME_RATE);

	xxh3_acc512_avx2(acc, p + len - XXH3_STRIPE_LEN, secret + XXH3_LAST_SECRET_OFFSET);
	xxh3_finalize128(acc, secret, len, out);
}

static XXH3_TARGET_AVX512 FORCE_INLINE void xxh3_long_core_avx512(const void* input, size_t len, const uint8_t* secret, uint8_t* out)
{
	const uint8_t* p = static_cast<const uint8_t*>(input);
	alignas(64) uint64_t acc[XXH3_ACC_NB] = XXH3_INIT_ACC_VALUES;
	size_t blocks = (len - 1) / XXH3_BLOCK_LEN;

	for (size_t b = 0; b < blocks; ++b) {
		const uint8_t* block = p + b * XXH3_BLOCK_LEN;
		for (size_t s = 0; s < XXH3_STRIPES_PER_BLOCK; ++s)
			xxh3_acc512_avx512(acc, block + s * XXH3_STRIPE_LEN, secret + s * XXH3_SECRET_CONSUME_RATE);
		xxh3_scramble_avx512(acc, secret + XXH3_SECRET_SIZE - XXH3_STRIPE_LEN);
	}

	size_t stripes = ((len - 1) - blocks * XXH3_BLOCK_LEN) / XXH3_STRIPE_LEN;
	const uint8_t* block = p + blocks * XXH3_BLOCK_LEN;
	for (size_t s = 0; s < stripes; ++s)
		xxh3_acc512_avx512(acc, block + s * XXH3_STRIPE_LEN, secret + s * XXH3_SECRET_CONSUME_RATE);

	xxh3_acc512_avx512(acc, p + len - XXH3_STRIPE_LEN, secret + XXH3_LAST_SECRET_OFFSET);
	xxh3_finalize128(acc, secret, len, out);
}

static XXH3_TARGET_SSE2 void xxh3_128_sse2(const void* bytes, size_t len, const uint8_t* seed, uint8_t* out)
{
	alignas(64) uint8_t custom[XXH3_SECRET_SIZE];
	const uint8_t* secret;

	secret = xxh3_select_secret(seed, custom);

	if (unlikely(len < XXH3_STRIPE_LEN))
		xxh3_short_core(bytes, len, secret, out);
	else
		xxh3_long_core_sse2(bytes, len, secret, out);
}

static XXH3_TARGET_AVX2 void xxh3_128_avx2(const void* bytes, size_t len, const uint8_t* seed, uint8_t* out)
{
	alignas(64) uint8_t custom[XXH3_SECRET_SIZE];
	const uint8_t* secret;

	secret = xxh3_select_secret(seed, custom);

	if (unlikely(len < XXH3_STRIPE_LEN))
		xxh3_short_core(bytes, len, secret, out);
	else
		xxh3_long_core_avx2(bytes, len, secret, out);
}

static XXH3_TARGET_AVX512 void xxh3_128_avx512(const void* bytes, size_t len, const uint8_t* seed, uint8_t* out)
{
	alignas(64) uint8_t custom[XXH3_SECRET_SIZE];
	const uint8_t* secret;

	secret = xxh3_select_secret(seed, custom);

	if (unlikely(len < XXH3_STRIPE_LEN))
		xxh3_short_core(bytes, len, secret, out);
	else
		xxh3_long_core_avx512(bytes, len, secret, out);
}
#endif

static void xxh3_snapraid(const void* in, size_t len, seed_t seed, void* out)
{
	uint8_t seed_bytes[16];

	/* duplicate the seed, matching the other SnapRAID adapters */
	xxh3_write64(seed_bytes, seed);
	xxh3_write64(seed_bytes + 8, seed);
	XXH3_SNAPRAID_HASH_FN(in, len, seed_bytes, static_cast<uint8_t*>(out));
}

REGISTER_FAMILY(xxh3_snapraid,
	$.src_url = "https://github.com/amadvance/snapraid/",
		$.src_status = HashFamilyInfo::SRC_ACTIVE
);

REGISTER_HASH(XXH3_128_snapraid,
	$.desc = "XXH3-128, SnapRAID long mode for all input lengths",
		$.impl = XXH3_SNAPRAID_IMPL,
		$.hash_flags = FLAG_HASH_ENDIAN_INDEPENDENT,
		$.impl_flags = FLAG_IMPL_MULTIPLY_64_128 |
		FLAG_IMPL_CANONICAL_BOTH |
		FLAG_IMPL_LICENSE_GPL3,
		$.bits = 128,
		$.verification_LE = 0xCFAD116F,
		$.verification_BE = 0xCFAD116F,
		$.hashfn_native = xxh3_snapraid,
		$.hashfn_bswap = xxh3_snapraid
);
