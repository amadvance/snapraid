// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2013 Andrea Mazzoleni

#include "internal.h"
#include "gf.h"
#include "cpu.h"

#ifdef CONFIG_X86

/* ================================================================
 * Optimizations notes
 * ================================================================
 *
 * For x86 optimizations you can see:
 *
 * Software optimization resources
 * http://www.agner.org/optimize/
 *
 * x86, x64 Instruction Latency, Memory Latency and CPUID dumps
 * http://users.atw.hu/instlatx64/
 *
 * Optimization notes:
 * - Cache Line Sizing: Intentionally don't process more than 64 bytes
 *   because 64 is the typical cache block. Processing 128 bytes or more
 *   doesn't increase performance, and in some cases it even decreases it.
 * - Register Pressure: Processing exactly 64 bytes perfectly balances
 *   the SIMD register file. It allows 128-bit architectures (SSE/NEON)
 *   to hold 4 data vectors while keeping all necessary GF constants and
 *   masks in the remaining registers, avoiding slow stack spills.
 * - Write-Combining Buffers: The final write of the P,Q,R,S,T parities
 *   always uses non-temporal stores (movntdq), even for writes smaller
 *   than the cache line, as the CPU has enough write-combining buffers
 *   to aggregate the sequential streams and avoid the RFO (Read-For-Ownership)
 *   memory penalty.
 * - Parity Accumulator Locality: The outer loop iterates by block size,
 *   and the inner loop iterates by disk. This guarantees that the running
 *   parity accumulators never leave the CPU's SIMD registers during the
 *   computation.
 * - D0 Cauchy Bypass: The final disk processed (D0) always has a
 *   coefficient of 1 for all parities. It is handled separately to bypass
 *   the expensive Cauchy matrix lookup and `pshufb` multiplications.
 * - Pipeline Dependency Breaking: Galois Field doubling (Horner's method
 *   multiplication by 2) is strategically placed relative to the XOR
 *   operations to break serial dependency chains. This allows the CPU's
 *   out-of-order execution engine to hide the latency of the GF math
 *   behind memory loads.
 */

/*
 * ================================================================
 * RAID Parity Generation: Q-Parity Horner's Method Implementations
 * ================================================================
 *
 * This file implements different strategies for computing the Q parity.
 * Q parity requires scaling each data disk by a descending power of 2
 * in the Galois Field:
 *
 *     Q = (D_l * 2^l) + (D_{l-1} * 2^{l-1}) + ... + (D_1 * 2^1) + (D_0 * 1)
 *
 * To efficiently calculate this sequentially using SIMD instructions, we
 * use Horner's method. Depending on hardware architecture and whether we
 * need to optimize out the final Cauchy matrix multiplication for D0, the
 * "multiply by 2" operation can be positioned in three distinct ways:
 *
 * -------------------------------------------------------------------------
 * CASE 1: Multiplication BEFORE XOR, handling all disks inside the loop
 * -------------------------------------------------------------------------
 * Used in GEN2 (RAID6) where all disks can be processed uniformly.
 * The accumulator is doubled BEFORE adding the current disk. Because the
 * loop runs all the way down to D0 (d >= 0), the final disk is XORed into
 * the accumulator and the loop terminates. This naturally leaves D0 with
 * a coefficient of 1, as it escapes the doubling step.
 *
 * Simplified Code:
 *   Q = D_l;
 *   for (d = l - 1; d >= 0; --d) {
 *       Q = Q * 2;       // Scale accumulator
 *       Q = Q ^ D_d;     // Add current disk
 *   }
 *
 * -------------------------------------------------------------------------
 * CASE 2: Multiplication BEFORE XOR, handling D0 separately
 * -------------------------------------------------------------------------
 * Used in GEN3/GEN4 when the loop contains expensive operations (like
 * SSSE3 pshufb for Cauchy matrices) that we want to skip for the very
 * last disk (which only requires coefficient 1). The Q-parity logic is
 * identical to CASE 1, but the final iteration for D0 is unrolled.
 *
 * Simplified Code:
 *   Q = D_l;
 *   for (d = l - 1; d > 0; --d) {
 *       Q = Q * 2;       // Scale accumulator
 *       Q = Q ^ D_d;     // Add current disk
 *   }
 *
 *   // Final disk (D0) processed outside the loop to skip Cauchy math
 *   Q = Q * 2;
 *   Q = Q ^ D_0;
 */

/*
 * GEN1 (RAID5 with xor) SSE2 implementation
 */
void raid_gen1_sse2(int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	int d, l;
	size_t i;

	l = nd - 1;
	p = v[nd];

	raid_sse_begin();

	for (i = 0; i < size; i += 64) {
		asm volatile ("movdqa %0,%%xmm0" : : "m" (v[0][i]));
		asm volatile ("movdqa %0,%%xmm1" : : "m" (v[0][i + 16]));
		asm volatile ("movdqa %0,%%xmm2" : : "m" (v[0][i + 32]));
		asm volatile ("movdqa %0,%%xmm3" : : "m" (v[0][i + 48]));

		for (d = 1; d <= l; ++d) {
			asm volatile ("pxor %0,%%xmm0" : : "m" (v[d][i]));
			asm volatile ("pxor %0,%%xmm1" : : "m" (v[d][i + 16]));
			asm volatile ("pxor %0,%%xmm2" : : "m" (v[d][i + 32]));
			asm volatile ("pxor %0,%%xmm3" : : "m" (v[d][i + 48]));
		}

		asm volatile ("movntdq %%xmm0,%0" : "=m" (p[i]));
		asm volatile ("movntdq %%xmm1,%0" : "=m" (p[i + 16]));
		asm volatile ("movntdq %%xmm2,%0" : "=m" (p[i + 32]));
		asm volatile ("movntdq %%xmm3,%0" : "=m" (p[i + 48]));
	}

	raid_sse_end();
}

/*
 * GEN2 (RAID6 with powers of 2) SSE2 implementation
 */
void raid_gen2_sse2(int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *q;
	int d, l;
	size_t i;

	l = nd - 1;
	p = v[nd];
	q = v[nd + 1];

	raid_sse_begin();

	asm volatile ("movdqa %0,%%xmm7" : : "m" (gfconst16.poly[0]));

	for (i = 0; i < size; i += 32) {
		asm volatile ("movdqa %0,%%xmm0" : : "m" (v[l][i]));
		asm volatile ("movdqa %0,%%xmm1" : : "m" (v[l][i + 16]));
		asm volatile ("movdqa %xmm0,%xmm2");
		asm volatile ("movdqa %xmm1,%xmm3");

		for (d = l - 1; d >= 0; --d) {
			/* scale by 2 before adding the current disk so the first (last processed) disk with factor 1 avoids doubling */
			asm volatile ("pxor %xmm4,%xmm4");
			asm volatile ("pxor %xmm5,%xmm5");
			asm volatile ("pcmpgtb %xmm2,%xmm4");
			asm volatile ("pcmpgtb %xmm3,%xmm5");
			asm volatile ("paddb %xmm2,%xmm2");
			asm volatile ("paddb %xmm3,%xmm3");
			asm volatile ("pand %xmm7,%xmm4");
			asm volatile ("pand %xmm7,%xmm5");
			asm volatile ("pxor %xmm4,%xmm2");
			asm volatile ("pxor %xmm5,%xmm3");

			asm volatile ("movdqa %0,%%xmm4" : : "m" (v[d][i]));
			asm volatile ("movdqa %0,%%xmm5" : : "m" (v[d][i + 16]));
			asm volatile ("pxor %xmm4,%xmm0");
			asm volatile ("pxor %xmm5,%xmm1");
			asm volatile ("pxor %xmm4,%xmm2");
			asm volatile ("pxor %xmm5,%xmm3");
		}

		asm volatile ("movntdq %%xmm0,%0" : "=m" (p[i]));
		asm volatile ("movntdq %%xmm1,%0" : "=m" (p[i + 16]));
		asm volatile ("movntdq %%xmm2,%0" : "=m" (q[i]));
		asm volatile ("movntdq %%xmm3,%0" : "=m" (q[i + 16]));
	}

	raid_sse_end();
}

#ifdef CONFIG_X86_64
/*
 * GEN2 (RAID6 with powers of 2) SSE2 implementation
 *
 * Note that it uses 16 registers, meaning that x64 is required.
 */
void raid_gen2_sse2ext(int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *q;
	int d, l;
	size_t i;

	l = nd - 1;
	p = v[nd];
	q = v[nd + 1];

	raid_sse_begin();

	asm volatile ("movdqa %0,%%xmm15" : : "m" (gfconst16.poly[0]));

	for (i = 0; i < size; i += 64) {
		asm volatile ("movdqa %0,%%xmm0" : : "m" (v[l][i]));
		asm volatile ("movdqa %0,%%xmm1" : : "m" (v[l][i + 16]));
		asm volatile ("movdqa %0,%%xmm2" : : "m" (v[l][i + 32]));
		asm volatile ("movdqa %0,%%xmm3" : : "m" (v[l][i + 48]));
		asm volatile ("movdqa %xmm0,%xmm4");
		asm volatile ("movdqa %xmm1,%xmm5");
		asm volatile ("movdqa %xmm2,%xmm6");
		asm volatile ("movdqa %xmm3,%xmm7");

		for (d = l - 1; d >= 0; --d) {
			/* scale by 2 before adding the current disk so the first (last processed) disk with factor 1 avoids doubling */
			asm volatile ("pxor %xmm8,%xmm8");
			asm volatile ("pxor %xmm9,%xmm9");
			asm volatile ("pxor %xmm10,%xmm10");
			asm volatile ("pxor %xmm11,%xmm11");
			asm volatile ("pcmpgtb %xmm4,%xmm8");
			asm volatile ("pcmpgtb %xmm5,%xmm9");
			asm volatile ("pcmpgtb %xmm6,%xmm10");
			asm volatile ("pcmpgtb %xmm7,%xmm11");
			asm volatile ("paddb %xmm4,%xmm4");
			asm volatile ("paddb %xmm5,%xmm5");
			asm volatile ("paddb %xmm6,%xmm6");
			asm volatile ("paddb %xmm7,%xmm7");
			asm volatile ("pand %xmm15,%xmm8");
			asm volatile ("pand %xmm15,%xmm9");
			asm volatile ("pand %xmm15,%xmm10");
			asm volatile ("pand %xmm15,%xmm11");
			asm volatile ("pxor %xmm8,%xmm4");
			asm volatile ("pxor %xmm9,%xmm5");
			asm volatile ("pxor %xmm10,%xmm6");
			asm volatile ("pxor %xmm11,%xmm7");

			asm volatile ("movdqa %0,%%xmm8" : : "m" (v[d][i]));
			asm volatile ("movdqa %0,%%xmm9" : : "m" (v[d][i + 16]));
			asm volatile ("movdqa %0,%%xmm10" : : "m" (v[d][i + 32]));
			asm volatile ("movdqa %0,%%xmm11" : : "m" (v[d][i + 48]));
			asm volatile ("pxor %xmm8,%xmm0");
			asm volatile ("pxor %xmm9,%xmm1");
			asm volatile ("pxor %xmm10,%xmm2");
			asm volatile ("pxor %xmm11,%xmm3");
			asm volatile ("pxor %xmm8,%xmm4");
			asm volatile ("pxor %xmm9,%xmm5");
			asm volatile ("pxor %xmm10,%xmm6");
			asm volatile ("pxor %xmm11,%xmm7");
		}

		asm volatile ("movntdq %%xmm0,%0" : "=m" (p[i]));
		asm volatile ("movntdq %%xmm1,%0" : "=m" (p[i + 16]));
		asm volatile ("movntdq %%xmm2,%0" : "=m" (p[i + 32]));
		asm volatile ("movntdq %%xmm3,%0" : "=m" (p[i + 48]));
		asm volatile ("movntdq %%xmm4,%0" : "=m" (q[i]));
		asm volatile ("movntdq %%xmm5,%0" : "=m" (q[i + 16]));
		asm volatile ("movntdq %%xmm6,%0" : "=m" (q[i + 32]));
		asm volatile ("movntdq %%xmm7,%0" : "=m" (q[i + 48]));
	}

	raid_sse_end();
}
#endif

/*
 * GENz (triple parity with powers of 2^-1) SSE2 implementation
 */
void raid_genz_sse2(int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *q;
	uint8_t *r;
	int d, l;
	size_t i;

	l = nd - 1;
	p = v[nd];
	q = v[nd + 1];
	r = v[nd + 2];

	raid_sse_begin();

	asm volatile ("movdqa %0,%%xmm7" : : "m" (gfconst16.poly[0]));
	asm volatile ("movdqa %0,%%xmm3" : : "m" (gfconst16.half[0]));
	asm volatile ("movdqa %0,%%xmm6" : : "m" (gfconst16.low7[0]));

	for (i = 0; i < size; i += 16) {
		asm volatile ("movdqa %0,%%xmm0" : : "m" (v[l][i]));
		asm volatile ("movdqa %xmm0,%xmm1");
		asm volatile ("movdqa %xmm0,%xmm2");
		for (d = l - 1; d >= 0; --d) {
			asm volatile ("pxor %xmm4,%xmm4");
			asm volatile ("pcmpgtb %xmm1,%xmm4");
			asm volatile ("paddb %xmm1,%xmm1");
			asm volatile ("pand %xmm7,%xmm4");
			asm volatile ("pxor %xmm4,%xmm1");

			asm volatile ("movdqa %xmm2,%xmm4");
			asm volatile ("pxor %xmm5,%xmm5");
			asm volatile ("psllw $7,%xmm4");
			asm volatile ("psrlw $1,%xmm2");
			asm volatile ("pcmpgtb %xmm4,%xmm5");
			asm volatile ("pand %xmm6,%xmm2");
			asm volatile ("pand %xmm3,%xmm5");
			asm volatile ("pxor %xmm5,%xmm2");

			asm volatile ("movdqa %0,%%xmm4" : : "m" (v[d][i]));
			asm volatile ("pxor %xmm4,%xmm0");
			asm volatile ("pxor %xmm4,%xmm1");
			asm volatile ("pxor %xmm4,%xmm2");
		}
		asm volatile ("movntdq %%xmm0,%0" : "=m" (p[i]));
		asm volatile ("movntdq %%xmm1,%0" : "=m" (q[i]));
		asm volatile ("movntdq %%xmm2,%0" : "=m" (r[i]));
	}

	raid_sse_end();
}

#ifdef CONFIG_X86_64
/*
 * GENz (triple parity with powers of 2^-1) SSE2 implementation
 *
 * Note that it uses 16 registers, meaning that x64 is required.
 */
void raid_genz_sse2ext(int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *q;
	uint8_t *r;
	int d, l;
	size_t i;

	l = nd - 1;
	p = v[nd];
	q = v[nd + 1];
	r = v[nd + 2];

	raid_sse_begin();

	asm volatile ("movdqa %0,%%xmm7" : : "m" (gfconst16.poly[0]));
	asm volatile ("movdqa %0,%%xmm3" : : "m" (gfconst16.half[0]));
	asm volatile ("movdqa %0,%%xmm11" : : "m" (gfconst16.low7[0]));

	for (i = 0; i < size; i += 32) {
		asm volatile ("movdqa %0,%%xmm0" : : "m" (v[l][i]));
		asm volatile ("movdqa %0,%%xmm8" : : "m" (v[l][i + 16]));
		asm volatile ("movdqa %xmm0,%xmm1");
		asm volatile ("movdqa %xmm8,%xmm9");
		asm volatile ("movdqa %xmm0,%xmm2");
		asm volatile ("movdqa %xmm8,%xmm10");
		for (d = l - 1; d >= 0; --d) {
			asm volatile ("movdqa %xmm2,%xmm6");
			asm volatile ("movdqa %xmm10,%xmm14");
			asm volatile ("pxor %xmm4,%xmm4");
			asm volatile ("pxor %xmm12,%xmm12");
			asm volatile ("pxor %xmm5,%xmm5");
			asm volatile ("pxor %xmm13,%xmm13");
			asm volatile ("psllw $7,%xmm6");
			asm volatile ("psllw $7,%xmm14");
			asm volatile ("psrlw $1,%xmm2");
			asm volatile ("psrlw $1,%xmm10");
			asm volatile ("pcmpgtb %xmm1,%xmm4");
			asm volatile ("pcmpgtb %xmm9,%xmm12");
			asm volatile ("pcmpgtb %xmm6,%xmm5");
			asm volatile ("pcmpgtb %xmm14,%xmm13");
			asm volatile ("paddb %xmm1,%xmm1");
			asm volatile ("paddb %xmm9,%xmm9");
			asm volatile ("pand %xmm11,%xmm2");
			asm volatile ("pand %xmm11,%xmm10");
			asm volatile ("pand %xmm7,%xmm4");
			asm volatile ("pand %xmm7,%xmm12");
			asm volatile ("pand %xmm3,%xmm5");
			asm volatile ("pand %xmm3,%xmm13");
			asm volatile ("pxor %xmm4,%xmm1");
			asm volatile ("pxor %xmm12,%xmm9");
			asm volatile ("pxor %xmm5,%xmm2");
			asm volatile ("pxor %xmm13,%xmm10");

			asm volatile ("movdqa %0,%%xmm4" : : "m" (v[d][i]));
			asm volatile ("movdqa %0,%%xmm12" : : "m" (v[d][i + 16]));
			asm volatile ("pxor %xmm4,%xmm0");
			asm volatile ("pxor %xmm4,%xmm1");
			asm volatile ("pxor %xmm4,%xmm2");
			asm volatile ("pxor %xmm12,%xmm8");
			asm volatile ("pxor %xmm12,%xmm9");
			asm volatile ("pxor %xmm12,%xmm10");
		}
		asm volatile ("movntdq %%xmm0,%0" : "=m" (p[i]));
		asm volatile ("movntdq %%xmm8,%0" : "=m" (p[i + 16]));
		asm volatile ("movntdq %%xmm1,%0" : "=m" (q[i]));
		asm volatile ("movntdq %%xmm9,%0" : "=m" (q[i + 16]));
		asm volatile ("movntdq %%xmm2,%0" : "=m" (r[i]));
		asm volatile ("movntdq %%xmm10,%0" : "=m" (r[i + 16]));
	}

	raid_sse_end();
}
#endif

void raid_register_sse2(void)
{
	if (raid_cpu_has_sse2()) {
		raid_gen_register(RAID_ALGO_CAUCHY_PAR1, "sse2", raid_gen1_sse2);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR2, "sse2", raid_gen2_sse2);
#ifdef CONFIG_X86_64
		if (!raid_cpu_has_slow_extendedreg())
			raid_gen_register(RAID_ALGO_CAUCHY_PAR2, "sse2e", raid_gen2_sse2ext);
		/* note that raid_cpu_has_slow_extendedreg() doesn't affect vandermonde */
		raid_gen_register(RAID_ALGO_VANDERMONDE_PAR3, "sse2e", raid_genz_sse2ext);
#else
		raid_gen_register(RAID_ALGO_VANDERMONDE_PAR3, "sse2", raid_genz_sse2);
#endif
	}
}

#endif /* CONFIG_X86 */
