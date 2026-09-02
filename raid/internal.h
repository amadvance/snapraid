// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2013 Andrea Mazzoleni

#ifndef __RAID_INTERNAL_H
#define __RAID_INTERNAL_H

#if HAVE_CONFIG_H
/* Includes the project configuration for HAVE_* defines */
#include "config.h"
#else
/* Assume that assembly is always supported */
#define HAVE_ASSEMBLY 1
#endif

/* If the platforms supports assembly */
#if HAVE_ASSEMBLY
/* Autodetect from the compiler */
#if defined(__i386__)
#define CONFIG_X86 1
#define CONFIG_X86_32 1
#endif
#if defined(__x86_64__)
#define CONFIG_X86 1
#define CONFIG_X86_64 1
#endif
#if defined(__aarch64__)
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#define CONFIG_NEON 1
#endif
#endif
#if defined(__arm__)
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#define CONFIG_NEON32 1
#endif
#endif
#endif

/*
 * Includes anything required for compatibility.
 */
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * Inverse assert used to check internal invariants and API preconditions.
 * It is intentionally based on assert() so that all checks can be disabled
 * with NDEBUG. When disabled, callers must satisfy all documented preconditions.
 */
#define BUG_ON(a) assert(!(a))

/*
 * Forced inline.
 */
#ifndef __always_inline
#define __always_inline inline __attribute__((always_inline))
#endif

/*
 * Forced alignment.
 */
#ifndef __aligned
#define __aligned(a) __attribute__((aligned(a)))
#endif

/*
 * Align a pointer at the specified size.
 */
static __always_inline void * __align_ptr(void *ptr, uintptr_t size)
{
	uintptr_t offset = (uintptr_t)ptr;

	offset = (offset + size - 1U) & ~(size - 1U);

	return (void *)offset;
}

/*
 * Includes the main interface headers.
 */
#include "raid.h"
#include "helper.h"

/*
 * Internal functions.
 *
 * These are intended to provide external access for testing.
 */
void raid_gen_ref(int nd, int np, size_t size, void **vv);
void raid_invert(uint8_t *M, uint8_t *V, int n);
void raid_delta_gen(int nr, int *id, int *ip, int nd, size_t size, void **v);
void raid_rec1of1(int *id, int nd, size_t size, void **v);
void raid_gen1_int32(int nd, size_t size, void **vv, int streaming);
void raid_gen1_int64(int nd, size_t size, void **vv, int streaming);
void raid_gen1_sse2(int nd, size_t size, void **vv, int streaming);
void raid_gen1_avx2(int nd, size_t size, void **vv, int streaming);
void raid_gen1_avx512bw(int nd, size_t size, void **vv, int streaming);
void raid_gen2_int8(int nd, size_t size, void **vv, int streaming);
void raid_gen2_int32_raid(int nd, size_t size, void **vv, int streaming);
void raid_gen2_int32_aes(int nd, size_t size, void **vv, int streaming);
void raid_gen2_int64_raid(int nd, size_t size, void **vv, int streaming);
void raid_gen2_int64_aes(int nd, size_t size, void **vv, int streaming);
void raid_gen2_sse2_raid(int nd, size_t size, void **vv, int streaming);
void raid_gen2_sse2_aes(int nd, size_t size, void **vv, int streaming);
void raid_gen2_avx2_raid(int nd, size_t size, void **vv, int streaming);
void raid_gen2_avx2_aes(int nd, size_t size, void **vv, int streaming);
void raid_gen2_sse2ext_raid(int nd, size_t size, void **vv, int streaming);
void raid_gen2_sse2ext_aes(int nd, size_t size, void **vv, int streaming);
void raid_gen2_avx2ext_raid(int nd, size_t size, void **vv, int streaming);
void raid_gen2_avx2ext_aes(int nd, size_t size, void **vv, int streaming);
void raid_gen2_avx512bw(int nd, size_t size, void **vv, int streaming);
void raid_gen2_avx2gfni_raid(int nd, size_t size, void **vv, int streaming);
void raid_gen2_avx512gfni_raid(int nd, size_t size, void **vv, int streaming);
void raid_gen2_avx2gfni_aes(int nd, size_t size, void **vv, int streaming);
void raid_gen2_avx512gfni_aes(int nd, size_t size, void **vv, int streaming);
void raid_genz_int32_raid(int nd, size_t size, void **vv, int streaming);
void raid_genz_int64_raid(int nd, size_t size, void **vv, int streaming);
void raid_genz_sse2_raid(int nd, size_t size, void **vv, int streaming);
void raid_genz_sse2ext_raid(int nd, size_t size, void **vv, int streaming);
void raid_genz_avx2ext_raid(int nd, size_t size, void **vv, int streaming);
void raid_gen3_int8(int nd, size_t size, void **vv, int streaming);
void raid_gen3_ssse3_raid(int nd, size_t size, void **vv, int streaming);
void raid_gen3_ssse3_aes(int nd, size_t size, void **vv, int streaming);
void raid_gen3_ssse3ext_raid(int nd, size_t size, void **vv, int streaming);
void raid_gen3_ssse3ext_aes(int nd, size_t size, void **vv, int streaming);
void raid_gen3_avx2ext_raid(int nd, size_t size, void **vv, int streaming);
void raid_gen3_avx2ext_aes(int nd, size_t size, void **vv, int streaming);
void raid_gen3_avx512bw(int nd, size_t size, void **vv, int streaming);
void raid_gen3_avx2gfni_raid(int nd, size_t size, void **vv, int streaming);
void raid_gen3_avx512gfni_raid(int nd, size_t size, void **vv, int streaming);
void raid_gen3_avx2gfni_aes(int nd, size_t size, void **vv, int streaming);
void raid_gen3_avx512gfni_aes(int nd, size_t size, void **vv, int streaming);
void raid_gen4_int8(int nd, size_t size, void **vv, int streaming);
void raid_gen4_ssse3_raid(int nd, size_t size, void **vv, int streaming);
void raid_gen4_ssse3_aes(int nd, size_t size, void **vv, int streaming);
void raid_gen4_ssse3ext_raid(int nd, size_t size, void **vv, int streaming);
void raid_gen4_ssse3ext_aes(int nd, size_t size, void **vv, int streaming);
void raid_gen4_avx2ext_raid(int nd, size_t size, void **vv, int streaming);
void raid_gen4_avx2ext_aes(int nd, size_t size, void **vv, int streaming);
void raid_gen4_avx512bw(int nd, size_t size, void **vv, int streaming);
void raid_gen4_avx2gfni_raid(int nd, size_t size, void **vv, int streaming);
void raid_gen4_avx512gfni_raid(int nd, size_t size, void **vv, int streaming);
void raid_gen4_avx2gfni_aes(int nd, size_t size, void **vv, int streaming);
void raid_gen4_avx512gfni_aes(int nd, size_t size, void **vv, int streaming);
void raid_gen5_int8(int nd, size_t size, void **vv, int streaming);
void raid_gen5_ssse3_raid(int nd, size_t size, void **vv, int streaming);
void raid_gen5_ssse3ext_raid(int nd, size_t size, void **vv, int streaming);
void raid_gen5_ssse3ext_aes(int nd, size_t size, void **vv, int streaming);
void raid_gen5_avx2ext_raid(int nd, size_t size, void **vv, int streaming);
void raid_gen5_avx2ext_aes(int nd, size_t size, void **vv, int streaming);
void raid_gen5_avx512bw(int nd, size_t size, void **vv, int streaming);
void raid_gen5_avx2gfni_raid(int nd, size_t size, void **vv, int streaming);
void raid_gen5_avx512gfni_raid(int nd, size_t size, void **vv, int streaming);
void raid_gen5_avx2gfni_aes(int nd, size_t size, void **vv, int streaming);
void raid_gen5_avx512gfni_aes(int nd, size_t size, void **vv, int streaming);
void raid_gen6_int8(int nd, size_t size, void **vv, int streaming);
void raid_gen6_ssse3_raid(int nd, size_t size, void **vv, int streaming);
void raid_gen6_ssse3ext_raid(int nd, size_t size, void **vv, int streaming);
void raid_gen6_ssse3ext_aes(int nd, size_t size, void **vv, int streaming);
void raid_gen6_avx2ext_raid(int nd, size_t size, void **vv, int streaming);
void raid_gen6_avx2ext_aes(int nd, size_t size, void **vv, int streaming);
void raid_gen6_avx512bw(int nd, size_t size, void **vv, int streaming);
void raid_gen6_avx2gfni_raid(int nd, size_t size, void **vv, int streaming);
void raid_gen6_avx512gfni_raid(int nd, size_t size, void **vv, int streaming);
void raid_gen6_avx2gfni_aes(int nd, size_t size, void **vv, int streaming);
void raid_gen6_avx512gfni_aes(int nd, size_t size, void **vv, int streaming);
void raid_rec1_int8(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec2_int8(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec3_int8(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec4_int8(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec5_int8(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec6_int8(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec1_ssse3(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec2_ssse3(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec3_ssse3(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec4_ssse3(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec5_ssse3(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec6_ssse3(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec1_ssse3ext(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec2_ssse3ext(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec3_ssse3ext(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec4_ssse3ext(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec5_ssse3ext(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec6_ssse3ext(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec1_avx2(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec2_avx2(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec3_avx2(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec4_avx2(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec5_avx2(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec6_avx2(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec1_avx2ext(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec2_avx2ext(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec3_avx2ext(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec4_avx2ext(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec5_avx2ext(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec6_avx2ext(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec1_avx512bw(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec2_avx512bw(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec3_avx512bw(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec4_avx512bw(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec5_avx512bw(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec6_avx512bw(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec1_avx2gfni_raid(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec2_avx2gfni_raid(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec3_avx2gfni_raid(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec4_avx2gfni_raid(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec5_avx2gfni_raid(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec6_avx2gfni_raid(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec1_avx2gfni_aes(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec2_avx2gfni_aes(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec3_avx2gfni_aes(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec4_avx2gfni_aes(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec5_avx2gfni_aes(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec6_avx2gfni_aes(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec1_avx512gfni_raid(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec2_avx512gfni_raid(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec3_avx512gfni_raid(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec4_avx512gfni_raid(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec5_avx512gfni_raid(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec6_avx512gfni_raid(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec1_avx512gfni_aes(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec2_avx512gfni_aes(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec3_avx512gfni_aes(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec4_avx512gfni_aes(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec5_avx512gfni_aes(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec6_avx512gfni_aes(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_gen1_neon(int nd, size_t size, void **vv, int streaming);
void raid_gen2_neon_raid(int nd, size_t size, void **vv, int streaming);
void raid_gen2_neon_aes(int nd, size_t size, void **vv, int streaming);
void raid_genz_neon_raid(int nd, size_t size, void **vv, int streaming);
void raid_gen3_neon_raid(int nd, size_t size, void **vv, int streaming);
void raid_gen3_neon_aes(int nd, size_t size, void **vv, int streaming);
void raid_gen4_neon_raid(int nd, size_t size, void **vv, int streaming);
void raid_gen4_neon_aes(int nd, size_t size, void **vv, int streaming);
void raid_gen5_neon_raid(int nd, size_t size, void **vv, int streaming);
void raid_gen5_neon_aes(int nd, size_t size, void **vv, int streaming);
void raid_gen6_neon_raid(int nd, size_t size, void **vv, int streaming);
void raid_gen6_neon_aes(int nd, size_t size, void **vv, int streaming);
void raid_rec1_neon(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec2_neon(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec3_neon(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec4_neon(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec5_neon(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec6_neon(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_gen1_neon32(int nd, size_t size, void **vv, int streaming);
void raid_gen2_neon32_raid(int nd, size_t size, void **vv, int streaming);
void raid_gen2_neon32_aes(int nd, size_t size, void **vv, int streaming);
void raid_genz_neon32_raid(int nd, size_t size, void **vv, int streaming);
void raid_gen3_neon32_raid(int nd, size_t size, void **vv, int streaming);
void raid_gen3_neon32_aes(int nd, size_t size, void **vv, int streaming);
void raid_gen4_neon32_raid(int nd, size_t size, void **vv, int streaming);
void raid_gen4_neon32_aes(int nd, size_t size, void **vv, int streaming);
void raid_gen5_neon32_raid(int nd, size_t size, void **vv, int streaming);
void raid_gen5_neon32_aes(int nd, size_t size, void **vv, int streaming);
void raid_gen6_neon32_raid(int nd, size_t size, void **vv, int streaming);
void raid_gen6_neon32_aes(int nd, size_t size, void **vv, int streaming);
void raid_rec1_neon32(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec2_neon32(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec3_neon32(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec4_neon32(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec5_neon32(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec6_neon32(int nr, int *id, int *ip, int nd, size_t size, void **vv);

/*
 * Functions for parity computation.
 *
 * These functions compute the parity blocks from the provided data.
 *
 * The number of parities to compute is implicit in the position in the
 * forwarder vector. Position at index #i, computes (#i+1) parities.
 *
 * All these functions give the guarantee that parities are written
 * in order. First parity P, then parity Q, and so on.
 * This allows to specify the same memory buffer for multiple parities
 * knowing that you'll get the latest written one.
 * This characteristic is used by the raid_delta_gen() function to
 * avoid damaging unused parities during recovery.
 *
 * @nd Number of data blocks
 * @size Size of the blocks pointed to by @vv. It must be a multiple of 64.
 * @vv Vector of pointers to the blocks of data and parity.
 *   It has (@nd + #parities) elements. The starting elements are the blocks
 *   for data, following with the parity blocks.
 *   Each block has @size bytes.
 * @streaming Nonzero for streaming/non-temporal stores where supported, 0 for cached stores.
 */
typedef void (raid_gen_fn)(int nd, size_t size, void **vv, int streaming);

/*
 * Functions for data recovery.
 *
 * These functions recover data blocks using the specified parity
 * to recompute the missing data.
 *
 * Note that the format of vectors @id/@ip is different than raid_rec().
 * For example, in the vector @ip the first parity is represented with the
 * value 0 and not @nd.
 *
 * @nr Number of failed data blocks to recover.
 * @id[] Vector of @nr indexes of the data blocks to recover.
 *   The indexes start from 0. They must be in order.
 * @ip[] Vector of @nr indexes of the parity blocks to use in the recovering.
 *   The indexes start from 0. They must be in order.
 * @nd Number of data blocks.
 * @np Number of parity blocks.
 * @size Size of the blocks pointed by @vv. It must be a multiple of 64.
 * @vv Vector of pointers to the blocks of data and parity.
 *   It has (@nd + @np) elements. The starting elements are the blocks
 *   for data, following with the parity blocks.
 *   Each block has @size bytes.
 */
typedef void (raid_rec_fn)(int nr, int *id, int *ip, int nd, size_t size, void **vv);

/**
 * Algorithm indexes
 *
 * To be used with the register and tag functions.
 */
#define RAID_ALGO_CAUCHY_PAR1 0
#define RAID_ALGO_CAUCHY_PAR2 1
#define RAID_ALGO_CAUCHY_PAR3 2
#define RAID_ALGO_CAUCHY_PAR4 3
#define RAID_ALGO_CAUCHY_PAR5 4
#define RAID_ALGO_CAUCHY_PAR6 5
#define RAID_ALGO_VANDERMONDE_PAR3 6
#define RAID_ALGO_MAX 7

/**
 * Register functions for parity computation and data recovery.
 *
 * Each call overwrites the previous setting. Thus, call it from the
 * slowest to the fastest.
 *
 * @na Algo code of the function. One of RAID_ALGO_*.
 * @tag Descriptive short tag of the implementation, like "sse2", "avx2",...
 * @fn Function to register.
 * @poly Polynomial choice (RAID_POLY_ANY, RAID_POLY_RAID, RAID_POLY_AES).
 */
void raid_gen_register(int na, const char *tag, raid_gen_fn *fn, uint8_t poly);
void raid_rec_register(int na, const char *tag, raid_rec_fn *fn, uint8_t poly);

/**
 * Set functions for data recovery.
 *
 * Intended only for testing the recovery function forcing a specific
 * parity generation for the delta step.
 *
 * Each call overwrites the previous setting.
 *
 * @np Number of parities.
 * @fn Function to register.
 */
void raid_gen_force(int np, raid_gen_fn *fn);

/**
 * Register all the functions based on integer variables.
 */
void raid_register_int(void);

/**
 * Register all the functions based on x86 instructions.
 */
void raid_register_sse2(void);
void raid_register_ssse3(void);
void raid_register_avx2(void);
void raid_register_avx512(void);

/**
 * Register all the functions based on GFNI instructions.
 */
void raid_register_avx2gfni(void);
void raid_register_avx512gfni(void);

/*
 * Tag functions.
 *
 * Given the specified algo code, return the tag of the registered function.
 */
const char * raid_gen_tag(int na);
const char * raid_rec_tag(int na);

#if defined(CONFIG_X86) || defined(CONFIG_NEON) || defined(CONFIG_NEON32)
struct gfconst16 {
	uint8_t poly[16];
	uint8_t low4[16];
	uint8_t half[16];
	uint8_t low7[16];
};
extern struct gfconst16 gfconst16 __aligned(16);
#endif

/*
 * Tables.
 */
extern const uint8_t raid_gfmul_raid[256][256] __aligned(256);
extern const uint8_t raid_gfexp_raid[256] __aligned(256);
extern const uint8_t raid_gfinv_raid[256] __aligned(256);
extern const uint8_t raid_gfvandermonde_raid[3][256] __aligned(256);
extern const uint8_t raid_gfcauchy_raid[6][256] __aligned(256);
extern const uint8_t raid_gfaffine_raid[256][8] __aligned(256);
#ifdef CONFIG_X86_64
extern const uint8_t raid_gfcauchyaffine_raid[251][5][8] __aligned(256);
#endif
#if defined(CONFIG_X86) || defined(CONFIG_NEON) || defined(CONFIG_NEON32)
extern const uint8_t raid_gfcauchypshufb_raid[251][5][2][16] __aligned(256);
extern const uint8_t raid_gfmulpshufb_raid[256][2][16] __aligned(256);
#endif

extern const uint8_t raid_gfmul_aes[256][256] __aligned(256);
extern const uint8_t raid_gfexp_aes[256] __aligned(256);
extern const uint8_t raid_gfinv_aes[256] __aligned(256);
extern const uint8_t raid_gfcauchy_aes[6][256] __aligned(256);
#if defined(CONFIG_X86) || defined(CONFIG_NEON) || defined(CONFIG_NEON32)
extern const uint8_t raid_gfcauchypshufb_aes[251][5][2][16] __aligned(256);
extern const uint8_t raid_gfmulpshufb_aes[256][2][16] __aligned(256);
#endif

extern const uint8_t(*raid_gfmul)[256];
extern const uint8_t *raid_gfexp;
extern const uint8_t *raid_gfinv;
extern const uint8_t(*raid_gfvandermonde)[256];
extern const uint8_t(*raid_gfcauchy)[256];
#if defined(CONFIG_X86) || defined(CONFIG_NEON) || defined(CONFIG_NEON32)
extern const uint8_t(*raid_gfcauchypshufb)[5][2][16];
extern const uint8_t(*raid_gfmulpshufb)[2][16];
#endif
extern const uint8_t(*raid_gfgen)[256];

extern uint8_t raid_poly_byte;
extern uint8_t raid_inv2_byte;
extern uint32_t raid_poly_32;
extern uint64_t raid_poly_64;
extern uint32_t raid_inv2_32;
extern uint64_t raid_inv2_64;
extern int raid_mode_active;

#include "gf.h"

/*
 * Assembler blocks.
 */
#ifdef CONFIG_X86
static __always_inline void raid_sse_begin(void)
{
}

static __always_inline void raid_sse_end(int streaming)
{
	/*
	 * Non-temporal streaming stores, like MOVNTDQ, use a weak memory model.
	 * To ensure that other processors correctly observe data written with
	 * streaming stores, execute a store-store memory barrier. Cached stores
	 * maintain normal cache-coherent ordering and do not require sfence.
	 */
	if (streaming)
		asm volatile ("sfence" : : : "memory");

	/*
	 * Clobbers registers used in the asm code.
	 * This is required because in the Windows ABI, registers xmm6-xmm15
	 * must be preserved by the callee.
	 *
	 * This clobber list forces the compiler to save any register that
	 * needs to be preserved.
	 *
	 * We check for __SSE2__ because we require that the compiler supports
	 * SSE2 registers in the clobber list. If the compiler doesn't support
	 * SSE2 registers, we can clobber them freely.
	 *
	 * Registers ymm and zmm don't have this requirement.
	 *
	 * The inner asm statements intentionally keep SIMD state in fixed
	 * registers across adjacent asm blocks. This is not a strictly
	 * conforming extended-assembly pattern, but it is a well-established
	 * technique also used by the Linux RAID and OpenZFS RAIDZ SIMD
	 * implementations.
	 *
	 * The inner loops use only scalar integer variables for loop counters
	 * and pointers, which GCC and Clang allocate to general-purpose
	 * registers. Functions are also dispatched through pointers, preventing
	 * inlining. The dummy clobbers below therefore force GCC and Clang to
	 * save and restore callee-saved registers when required by the ABI.
	 */
#ifdef __SSE2__
	asm volatile ("" : : : "%xmm0", "%xmm1", "%xmm2", "%xmm3");
	asm volatile ("" : : : "%xmm4", "%xmm5", "%xmm6", "%xmm7");
#ifdef CONFIG_X86_64
	asm volatile ("" : : : "%xmm8", "%xmm9", "%xmm10", "%xmm11");
	asm volatile ("" : : : "%xmm12", "%xmm13", "%xmm14", "%xmm15");
#endif
#endif
}

static __always_inline void raid_avx_begin(void)
{
	raid_sse_begin();
}

static __always_inline void raid_avx_end(int streaming)
{
	raid_sse_end(streaming);

	/*
	 * Clear the upper parts of the vector registers before returning.
	 * This avoids AVX-to-legacy-SSE transition penalties on processors
	 * where mixing AVX and legacy SSE instructions incurs a cost.
	 */
	asm volatile ("vzeroupper" : : : "memory");
}
#endif /* CONFIG_X86 */

#ifdef CONFIG_NEON
static __always_inline void raid_neon_begin(void)
{
}

static __always_inline void raid_neon_end(void)
{
	/*
	 * Compiler memory barrier to ensure memory operations are not
	 * reordered across the end of SIMD operations.
	 */
	asm volatile ("" : : : "memory");

	/*
	 * Clobbers registers used in NEON asm operations.
	 *
	 * The inner asm statements intentionally keep NEON state in fixed
	 * registers across adjacent asm blocks. This is not a strictly
	 * conforming extended-assembly pattern, but it is a well-established
	 * technique also used by the Linux RAID and OpenZFS RAIDZ SIMD
	 * implementations.
	 *
	 * The inner loops use only scalar integer variables for loop counters
	 * and pointers, which GCC and Clang allocate to general-purpose
	 * registers. Functions are also dispatched through pointers, preventing
	 * inlining. The dummy clobbers below therefore force GCC and Clang to
	 * save and restore the AAPCS64 callee-saved NEON registers v8-v15.
	 */
	asm volatile ("" : : : "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7");
	asm volatile ("" : : : "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15");
	asm volatile ("" : : : "v16", "v17", "v18", "v19", "v20", "v21", "v22", "v23");
	asm volatile ("" : : : "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31");
}

void raid_register_neon(void);
#endif

#ifdef CONFIG_NEON32
static __always_inline void raid_neon32_begin(void)
{
}

static __always_inline void raid_neon32_end(void)
{
	/*
	 * Compiler memory barrier to ensure memory operations are not
	 * reordered across the end of SIMD operations.
	 */
	asm volatile ("" : : : "memory");

	/*
	 * Clobbers registers used in AArch32 NEON asm operations.
	 *
	 * The inner asm statements intentionally keep NEON state in fixed
	 * registers across adjacent asm blocks. This is not a strictly
	 * conforming extended-assembly pattern, but it is a well-established
	 * technique also used by the Linux RAID and OpenZFS RAIDZ SIMD
	 * implementations.
	 *
	 * The inner loops use only scalar integer variables for loop counters
	 * and pointers, which GCC and Clang allocate to general-purpose
	 * registers. Functions are also dispatched through pointers, preventing
	 * inlining. The dummy clobbers below therefore force GCC and Clang to
	 * save and restore the AAPCS32 callee-saved NEON registers when
	 * required.
	 */
	asm volatile ("" : : : "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7");
	asm volatile ("" : : : "d8", "d9", "d10", "d11", "d12", "d13", "d14", "d15");
	asm volatile ("" : : : "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23");
	asm volatile ("" : : : "d24", "d25", "d26", "d27", "d28", "d29", "d30", "d31");
}

void raid_register_neon32(void);
#endif

#endif
