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

/**
 * Enable C-base SIMD-accelerated vector optimizations on:
 * - x86/x64 (SSE2)
 * - ARM (NEON)
 * - RISC-V (Vector Extension)
 */
#if defined(__SSE2__) || defined(__ARM_NEON) || defined(__riscv_vector)
#if defined(__has_attribute)
#if __has_attribute(__vector_size__)
#define CONFIG_VEC128 1
#endif
#endif
#endif

/**
 * Enable Assembly-base SIMD-accelerated vector optimizations on:
 * - x86_32 (32 bit)
 * - x86_64 (64 bit)
 */
#if HAVE_ASSEMBLY
#if defined(__i386__)
#define CONFIG_X86 1
#define CONFIG_X86_32 1
#endif
#if defined(__x86_64__)
#define CONFIG_X86 1
#define CONFIG_X86_64 1
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
 * Inverse assert.
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
static __always_inline void *__align_ptr(void *ptr, uintptr_t size)
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
void raid_rec2of2_int8(int *id, int *ip, int nd, size_t size, void **vv);
void raid_gen1_int32(int nd, size_t size, void **vv);
void raid_gen1_int64(int nd, size_t size, void **vv);
void raid_gen1_vec128(int nd, size_t size, void **vv);
void raid_gen1_sse2(int nd, size_t size, void **vv);
void raid_gen1_avx2(int nd, size_t size, void **vv);
void raid_gen1_avx512bw(int nd, size_t size, void **vv);
void raid_gen2_int32(int nd, size_t size, void **vv);
void raid_gen2_int64(int nd, size_t size, void **vv);
void raid_gen2_vec128(int nd, size_t size, void **vv);
void raid_gen2_sse2(int nd, size_t size, void **vv);
void raid_gen2_avx2(int nd, size_t size, void **vv);
void raid_gen2_sse2ext(int nd, size_t size, void **vv);
void raid_gen2_avx512bw(int nd, size_t size, void **vv);
void raid_gen2_avx2gfni(int nd, size_t size, void **vv);
void raid_gen2_avx512gfni(int nd, size_t size, void **vv);
void raid_genz_int32(int nd, size_t size, void **vv);
void raid_genz_int64(int nd, size_t size, void **vv);
void raid_genz_vec128(int nd, size_t size, void **vv);
void raid_genz_sse2(int nd, size_t size, void **vv);
void raid_genz_sse2ext(int nd, size_t size, void **vv);
void raid_genz_avx2ext(int nd, size_t size, void **vv);
void raid_gen3_int8(int nd, size_t size, void **vv);
void raid_gen3_ssse3(int nd, size_t size, void **vv);
void raid_gen3_ssse3ext(int nd, size_t size, void **vv);
void raid_gen3_avx2ext(int nd, size_t size, void **vv);
void raid_gen3_avx512bw(int nd, size_t size, void **vv);
void raid_gen3_avx2gfni(int nd, size_t size, void **vv);
void raid_gen3_avx512gfni(int nd, size_t size, void **vv);
void raid_gen4_int8(int nd, size_t size, void **vv);
void raid_gen4_ssse3(int nd, size_t size, void **vv);
void raid_gen4_ssse3ext(int nd, size_t size, void **vv);
void raid_gen4_avx2ext(int nd, size_t size, void **vv);
void raid_gen4_avx512bw(int nd, size_t size, void **vv);
void raid_gen4_avx2gfni(int nd, size_t size, void **vv);
void raid_gen4_avx512gfni(int nd, size_t size, void **vv);
void raid_gen5_int8(int nd, size_t size, void **vv);
void raid_gen5_ssse3(int nd, size_t size, void **vv);
void raid_gen5_ssse3ext(int nd, size_t size, void **vv);
void raid_gen5_avx2ext(int nd, size_t size, void **vv);
void raid_gen5_avx512bw(int nd, size_t size, void **vv);
void raid_gen5_avx2gfni(int nd, size_t size, void **vv);
void raid_gen5_avx512gfni(int nd, size_t size, void **vv);
void raid_gen6_int8(int nd, size_t size, void **vv);
void raid_gen6_ssse3(int nd, size_t size, void **vv);
void raid_gen6_ssse3ext(int nd, size_t size, void **vv);
void raid_gen6_avx2ext(int nd, size_t size, void **vv);
void raid_gen6_avx512bw(int nd, size_t size, void **vv);
void raid_gen6_avx2gfni(int nd, size_t size, void **vv);
void raid_gen6_avx512gfni(int nd, size_t size, void **vv);
void raid_rec1_int8(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec2_int8(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_recX_int8(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec1_ssse3(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec2_ssse3(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_recX_ssse3(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec1_avx2(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec2_avx2(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_recX_avx2(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec1_avx512bw(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec2_avx512bw(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_recX_avx512bw(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec1_avx2gfni(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec2_avx2gfni(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_recX_avx2gfni(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec1_avx512gfni(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec2_avx512gfni(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_recX_avx512gfni(int nr, int *id, int *ip, int nd, size_t size, void **vv);

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
 * avoid to damage unused parities in recovering.
 *
 * @nd Number of data blocks
 * @size Size of the blocks pointed by @vv. It must be a multiple of 64.
 * @vv Vector of pointers to the blocks of data and parity.
 *   It has (@nd + #parities) elements. The starting elements are the blocks
 *   for data, following with the parity blocks.
 *   Each block has @size bytes.
 */
typedef void (raid_gen_fn)(int nd, size_t size, void **vv);

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
 */
void raid_gen_register(int na, const char *tag, raid_gen_fn *fn);
void raid_rec_register(int na, const char *tag, raid_rec_fn *fn);

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
 * Register all the functions based on x86 intructions.
 */
void raid_register_x86(void);

/**
 * Register all the functions based on GFNI intructions.
 */
void raid_register_avx2gfni(void);
void raid_register_avx512gfni(void);

/*
 * Tag functions.
 *
 * Given the specified algo code, return the tag of the registered function.
 */
const char *raid_gen_tag(int na);
const char *raid_rec_tag(int na);

/**
 * Basic functionality self test.
 *
 * Returns 0 on success.
 */
int raid_selftest(void);

/*
 * Tables.
 */
extern const uint8_t raid_gfmul[256][256] __aligned(256);
extern const uint8_t raid_gfexp[256] __aligned(256);
extern const uint8_t raid_gfinv[256] __aligned(256);
extern const uint8_t raid_gfvandermonde[3][256] __aligned(256);
extern const uint8_t raid_gfcauchy[6][256] __aligned(256);
extern const uint8_t raid_gfcauchypshufb[251][5][2][16] __aligned(256);
extern const uint8_t raid_gfmulpshufb[256][2][16] __aligned(256);
extern const uint8_t raid_gfcauchycoeff[251][4][16] __aligned(256);
extern const uint8_t (*raid_gfgen)[256];
#define gfmul raid_gfmul
#define gfexp raid_gfexp
#define gfinv raid_gfinv
#define gfvandermonde raid_gfvandermonde
#define gfcauchy raid_gfcauchy
#define gfgenpshufb raid_gfcauchypshufb
#define gfmulpshufb raid_gfmulpshufb
#define gfgencoeff raid_gfcauchycoeff
#define gfgen raid_gfgen

/*
 * Assembler blocks.
 */
#ifdef CONFIG_X86
static __always_inline void raid_sse_begin(void)
{
}

static __always_inline void raid_sse_end(void)
{
	/*
	 * SSE and AVX code uses non-temporal writes, like MOVNTDQ,
	 * that use a weak memory model. To ensure that other processors
	 * see correctly the data written, we use a store-store memory
	 * barrier at the end of the asm code
	 */
	asm volatile ("sfence" : : : "memory");

	/*
	 * Clobbers registers used in the asm code
	 * this is required because in the Windows ABI,
	 * registers xmm6-xmm15 should be kept by the callee.
	 *
	 * This clobber list forces the compiler to save any
	 * register that needs to be saved.
	 *
	 * We check for __SSE2__ because we require that the
	 * compiler supports SSE2 registers in the clobber list.
	 * If the compiler doesn't support SSE2 registers, we can
	 * clobber them freely.
	 *
	 * Registers ymm and zmm don't have this requirement.
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

static __always_inline void raid_avx_end(void)
{
	raid_sse_end();

	/*
	 * Reset the upper part of the ymm registers
	 * to avoid the 70 clocks penalty on the next
	 * xmm register use
	 */
	asm volatile ("vzeroupper" : : : "memory");
}
#endif /* CONFIG_X86 */

#endif

