/*
 * Copyright (C) 2013 Andrea Mazzoleni
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __RAID_INTERNAL_H
#define __RAID_INTERNAL_H

/*
 * Supported instruction sets.
 *
 * It may happen that the assembler is too old to support
 * all instructions, even if the architecture supports them.
 * These defines allow to exclude from the build the not supported ones.
 *
 * If in your project you use a predefined assembler, you can define them
 * using fixed values, instead of using the HAVE_* defines.
 */
#if HAVE_CONFIG_H

/* Includes the project configuration for HAVE_* defines */
#include "config.h"

/* If the compiler supports assembly */
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
#endif

/* Enables SSE2, SSSE3, AVX2 only if the assembler supports it */
#if HAVE_SSE2
#define CONFIG_SSE2 1
#endif
#if HAVE_SSSE3
#define CONFIG_SSSE3 1
#endif
#if HAVE_AVX2
#define CONFIG_AVX2 1
#endif

#else /* if HAVE_CONFIG_H is not defined */

/* Assume that assembly is always supported */
#if defined(__i386__)
#define CONFIG_X86 1
#define CONFIG_X86_32 1
#endif

#if defined(__x86_64__)
#define CONFIG_X86 1
#define CONFIG_X86_64 1
#endif

/* Assumes that the assembler supports everything */
#ifdef CONFIG_X86
#define CONFIG_SSE2 1
#define CONFIG_SSSE3 1
#define CONFIG_AVX2 1
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
 * These are intended to provide access for testing.
 */
int raid_selftest(void);
void raid_gen_ref(int nd, int np, size_t size, void **vv);
void raid_invert(uint8_t *M, uint8_t *V, int n);
void raid_delta_gen(int nr, int *id, int *ip, int nd, size_t size, void **v);
void raid_rec1of1(int *id, int nd, size_t size, void **v);
void raid_rec2of2_int8(int *id, int *ip, int nd, size_t size, void **vv);
void raid_gen1_int32(int nd, size_t size, void **vv);
void raid_gen1_int64(int nd, size_t size, void **vv);
void raid_gen1_sse2(int nd, size_t size, void **vv);
void raid_gen1_avx2(int nd, size_t size, void **vv);
void raid_gen2_int32(int nd, size_t size, void **vv);
void raid_gen2_int64(int nd, size_t size, void **vv);
void raid_gen2_sse2(int nd, size_t size, void **vv);
void raid_gen2_avx2(int nd, size_t size, void **vv);
void raid_gen2_sse2ext(int nd, size_t size, void **vv);
void raid_genz_int32(int nd, size_t size, void **vv);
void raid_genz_int64(int nd, size_t size, void **vv);
void raid_genz_sse2(int nd, size_t size, void **vv);
void raid_genz_sse2ext(int nd, size_t size, void **vv);
void raid_genz_avx2ext(int nd, size_t size, void **vv);
void raid_gen3_int8(int nd, size_t size, void **vv);
void raid_gen3_ssse3(int nd, size_t size, void **vv);
void raid_gen3_ssse3ext(int nd, size_t size, void **vv);
void raid_gen3_avx2ext(int nd, size_t size, void **vv);
void raid_gen4_int8(int nd, size_t size, void **vv);
void raid_gen4_ssse3(int nd, size_t size, void **vv);
void raid_gen4_ssse3ext(int nd, size_t size, void **vv);
void raid_gen4_avx2ext(int nd, size_t size, void **vv);
void raid_gen5_int8(int nd, size_t size, void **vv);
void raid_gen5_ssse3(int nd, size_t size, void **vv);
void raid_gen5_ssse3ext(int nd, size_t size, void **vv);
void raid_gen5_avx2ext(int nd, size_t size, void **vv);
void raid_gen6_int8(int nd, size_t size, void **vv);
void raid_gen6_ssse3(int nd, size_t size, void **vv);
void raid_gen6_ssse3ext(int nd, size_t size, void **vv);
void raid_gen6_avx2ext(int nd, size_t size, void **vv);
void raid_rec1_int8(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec2_int8(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_recX_int8(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec1_ssse3(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec2_ssse3(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_recX_ssse3(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec1_avx2(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec2_avx2(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_recX_avx2(int nr, int *id, int *ip, int nd, size_t size, void **vv);

/*
 * Internal naming.
 *
 * These are intented to provide access for testing.
 */
const char *raid_gen1_tag(void);
const char *raid_gen2_tag(void);
const char *raid_genz_tag(void);
const char *raid_gen3_tag(void);
const char *raid_gen4_tag(void);
const char *raid_gen5_tag(void);
const char *raid_gen6_tag(void);
const char *raid_rec1_tag(void);
const char *raid_rec2_tag(void);
const char *raid_recX_tag(void);

/*
 * Internal forwarders.
 */
extern void (*raid_gen3_ptr)(int nd, size_t size, void **vv);
extern void (*raid_genz_ptr)(int nd, size_t size, void **vv);
extern void (*raid_gen_ptr[RAID_PARITY_MAX])(
	int nd, size_t size, void **vv);
extern void (*raid_rec_ptr[RAID_PARITY_MAX])(
	int nr, int *id, int *ip, int nd, size_t size, void **vv);

/*
 * Tables.
 */
extern const uint8_t raid_gfmul[256][256] __aligned(256);
extern const uint8_t raid_gfexp[256] __aligned(256);
extern const uint8_t raid_gfinv[256] __aligned(256);
extern const uint8_t raid_gfvandermonde[3][256] __aligned(256);
extern const uint8_t raid_gfcauchy[6][256] __aligned(256);
extern const uint8_t raid_gfcauchypshufb[251][4][2][16] __aligned(256);
extern const uint8_t raid_gfmulpshufb[256][2][16] __aligned(256);
extern const uint8_t (*raid_gfgen)[256];
#define gfmul raid_gfmul
#define gfexp raid_gfexp
#define gfinv raid_gfinv
#define gfvandermonde raid_gfvandermonde
#define gfcauchy raid_gfcauchy
#define gfgenpshufb raid_gfcauchypshufb
#define gfmulpshufb raid_gfmulpshufb
#define gfgen raid_gfgen

/*
 * Assembler blocks.
 */
#ifdef CONFIG_X86
#ifdef CONFIG_SSE2
static __always_inline void raid_sse_begin(void)
{
}

static __always_inline void raid_sse_end(void)
{
	/* SSE and AVX code uses non-temporal writes, like MOVNTDQ, */
	/* that use a weak memory model. To ensure that other processors */
	/* see correctly the data written, we use a store-store memory */
	/* barrier at the end of the asm code */
	asm volatile ("sfence" : : : "memory");

	/* clobbers registers used in the asm code */
	/* this is required because in the Windows ABI, */
	/* registers xmm6-xmm15 should be kept by the callee. */
	/* this clobber list force the compiler to save any */
	/* register that needs to be saved */
	/* we check for __SSE2_ because we require that the */
	/* compiler supports SSE2 registers in the clobber list */
#ifdef __SSE2__
	asm volatile ("" : : : "%xmm0", "%xmm1", "%xmm2", "%xmm3");
	asm volatile ("" : : : "%xmm4", "%xmm5", "%xmm6", "%xmm7");
#ifdef CONFIG_X86_64
	asm volatile ("" : : : "%xmm8", "%xmm9", "%xmm10", "%xmm11");
	asm volatile ("" : : : "%xmm12", "%xmm13", "%xmm14", "%xmm15");
#endif
#endif
}
#endif

#ifdef CONFIG_AVX2
static __always_inline void raid_avx_begin(void)
{
	raid_sse_begin();
}

static __always_inline void raid_avx_end(void)
{
	raid_sse_end();

	/* reset the upper part of the ymm registers */
	/* to avoid the 70 clocks penality on the next */
	/* xmm register use */
	asm volatile ("vzeroupper" : : : "memory");
}
#endif
#endif /* CONFIG_X86 */

#endif

