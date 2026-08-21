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
 * - Pipeline Dependency Breaking: The Q Horner transition, normally
 *   multiplication by 2 and multiplication by 3 at G23 boundaries, is
 *   strategically placed relative to the XOR operations to break serial
 *   dependency chains. This allows the CPU's
 *   out-of-order execution engine to hide the latency of the GF math
 *   behind memory loads.
 */

/*
 * ================================================================
 * RAID Parity Generation: Q-Parity Horner's Method Implementations
 * ================================================================
 *
 * This file implements different strategies for computing the Q parity.
 * Q parity uses coefficients connected by cheap GF transitions. RAID mode
 * always uses multiplication by 2. AES mode also uses multiplication by 2,
 * except for the four G23 coset transitions that use multiplication by 3.
 *
 *     Q = (D_l * q[l]) + ... + (D_1 * q[1]) + (D_0 * q[0])
 *
 * To efficiently calculate this sequentially using SIMD instructions, we
 * use Horner's method. Depending on hardware architecture and whether we
 * need to optimize out the final Cauchy matrix multiplication for D0, the
 * scaling operation can be positioned in distinct ways:
 *
 * -------------------------------------------------------------------------
 * CASE 1: Multiplication BEFORE XOR, handling all disks inside the loop
 * -------------------------------------------------------------------------
 * Used in GEN2 where all disks can be processed uniformly.
 * The accumulator is scaled by the active Q transition BEFORE adding the current disk.
 * Because the loop runs all the way down to D0 (d >= 0), the final disk is XORed
 * into the accumulator and the loop terminates. Since D0 is added after the
 * final scaling operation, its coefficient is naturally 1.
 *
 * Simplified Code:
 *   Q = D_l;
 *   for (d = l - 1; d >= 0; --d) {
 *       Q = Q * step[d]; // Scale by 2, or by 3 at a G23 transition
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
 *       Q = Q * step[d]; // Scale by 2, or by 3 at a G23 transition
 *       Q = Q ^ D_d;     // Add current disk
 *   }
 *
 *   // Final disk (D0) processed outside the loop to skip Cauchy math.
 *   // D0 is below every G23 boundary, so this transition is always x2.
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
 * GEN2 Cauchy SSE2 implementation using only Q *= 2.
 */
static __always_inline void raid_gen2_sse2_x2(int nd, size_t size, void **vv)
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
			/* scale Q before adding the current disk */
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

/*
 * GEN2 Cauchy SSE2 implementation using the AES G23 Q recurrence.
 */
static __always_inline void raid_gen2_sse2_g23(int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *q;
	int d, l;
	size_t i;

	l = nd - 1;
	p = v[nd];
	q = v[nd + 1];

	int g23_boundary = raid_g23_boundary(l - 1);

	raid_sse_begin();

	asm volatile ("movdqa %0,%%xmm7" : : "m" (gfconst16.poly[0]));

	for (i = 0; i < size; i += 32) {
		int boundary = g23_boundary;

		asm volatile ("movdqa %0,%%xmm0" : : "m" (v[l][i]));
		asm volatile ("movdqa %0,%%xmm1" : : "m" (v[l][i + 16]));
		asm volatile ("movdqa %xmm0,%xmm2");
		asm volatile ("movdqa %xmm1,%xmm3");

		d = l - 1;

		for (;;) {
			/*
			 * All transitions above 'boundary' are x2.
			 */
			for (; d > boundary; --d) {
				/* scale Q before adding the current disk */
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

			/*
			 * boundary == -1 identifies the final x2-only segment.
			 * At this point all remaining disks have been consumed.
			 */
			if (boundary < 0)
				break;

			/*
			 * d == boundary.
			 *
			 * Perform the fixed G23 x3 transition and then add
			 * the current disk to P and Q.
			 */
			asm volatile ("movdqa %xmm2,%xmm4");
			asm volatile ("movdqa %xmm3,%xmm5");
			asm volatile ("pxor %xmm6,%xmm6");
			asm volatile ("pcmpgtb %xmm4,%xmm6");
			asm volatile ("paddb %xmm2,%xmm2");
			asm volatile ("paddb %xmm3,%xmm3");
			asm volatile ("pxor %xmm4,%xmm2");
			asm volatile ("pxor %xmm4,%xmm4");
			asm volatile ("pcmpgtb %xmm5,%xmm4");
			asm volatile ("pand %xmm7,%xmm6");
			asm volatile ("pand %xmm7,%xmm4");
			asm volatile ("pxor %xmm6,%xmm2");
			asm volatile ("pxor %xmm4,%xmm3");
			asm volatile ("pxor %xmm5,%xmm3");

			asm volatile ("movdqa %0,%%xmm4" : : "m" (v[d][i]));
			asm volatile ("movdqa %0,%%xmm5" : : "m" (v[d][i + 16]));
			asm volatile ("pxor %xmm4,%xmm0");
			asm volatile ("pxor %xmm5,%xmm1");
			asm volatile ("pxor %xmm4,%xmm2");
			asm volatile ("pxor %xmm5,%xmm3");

			--d;

			/*
			 * G23 boundaries are exactly 51 transitions apart.
			 * After d = 50 there are no more x3 transitions.
			 */
			boundary -= 51;
			if (boundary < 50)
				boundary = -1;
		}

		asm volatile ("movntdq %%xmm0,%0" : "=m" (p[i]));
		asm volatile ("movntdq %%xmm1,%0" : "=m" (p[i + 16]));
		asm volatile ("movntdq %%xmm2,%0" : "=m" (q[i]));
		asm volatile ("movntdq %%xmm3,%0" : "=m" (q[i + 16]));
	}

	raid_sse_end();
}

void raid_gen2_sse2_raid(int nd, size_t size, void **vv)
{
	raid_gen2_sse2_x2(nd, size, vv);
}

void raid_gen2_sse2_aes(int nd, size_t size, void **vv)
{
	if (raid_g23_x2_only(nd))
		raid_gen2_sse2_x2(nd, size, vv);
	else
		raid_gen2_sse2_g23(nd, size, vv);
}

#ifdef CONFIG_X86_64
/*
 * GEN2 Cauchy SSE2 implementation using only Q *= 2.
 *
 * Process two data disks at a time and process the four 16-byte lanes
 * sequentially.
 *
 * Note that it uses 16 registers, meaning that x64 is required.
 */
static __always_inline void raid_gen2_sse2ext_x2(int nd, size_t size, void **vv)
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

		/* process two disks per iteration */
		for (d = l - 1; d >= 1; d -= 2) {
			/*
			 * Lane 0.
			 *
			 * Compute both x2 Horner transitions and update P
			 * once with D[d] ^ D[d - 1].
			 */
			asm volatile ("movdqa %0,%%xmm8" : : "m" (v[d][i]));
			asm volatile ("movdqa %0,%%xmm9" : : "m" (v[d - 1][i]));

			asm volatile ("pxor %xmm11,%xmm11");
			asm volatile ("pcmpgtb %xmm4,%xmm11");
			asm volatile ("paddb %xmm4,%xmm4");
			asm volatile ("pand %xmm15,%xmm11");
			asm volatile ("pxor %xmm11,%xmm4");
			asm volatile ("pxor %xmm8,%xmm4");

			asm volatile ("pxor %xmm9,%xmm8");
			asm volatile ("pxor %xmm8,%xmm0");

			asm volatile ("pxor %xmm11,%xmm11");
			asm volatile ("pcmpgtb %xmm4,%xmm11");
			asm volatile ("paddb %xmm4,%xmm4");
			asm volatile ("pand %xmm15,%xmm11");
			asm volatile ("pxor %xmm11,%xmm4");
			asm volatile ("pxor %xmm9,%xmm4");

			/* lane 1 */
			asm volatile ("movdqa %0,%%xmm8" : : "m" (v[d][i + 16]));
			asm volatile ("movdqa %0,%%xmm9" : : "m" (v[d - 1][i + 16]));

			asm volatile ("pxor %xmm11,%xmm11");
			asm volatile ("pcmpgtb %xmm5,%xmm11");
			asm volatile ("paddb %xmm5,%xmm5");
			asm volatile ("pand %xmm15,%xmm11");
			asm volatile ("pxor %xmm11,%xmm5");
			asm volatile ("pxor %xmm8,%xmm5");

			asm volatile ("pxor %xmm9,%xmm8");
			asm volatile ("pxor %xmm8,%xmm1");

			asm volatile ("pxor %xmm11,%xmm11");
			asm volatile ("pcmpgtb %xmm5,%xmm11");
			asm volatile ("paddb %xmm5,%xmm5");
			asm volatile ("pand %xmm15,%xmm11");
			asm volatile ("pxor %xmm11,%xmm5");
			asm volatile ("pxor %xmm9,%xmm5");

			/* lane 2 */
			asm volatile ("movdqa %0,%%xmm8" : : "m" (v[d][i + 32]));
			asm volatile ("movdqa %0,%%xmm9" : : "m" (v[d - 1][i + 32]));

			asm volatile ("pxor %xmm11,%xmm11");
			asm volatile ("pcmpgtb %xmm6,%xmm11");
			asm volatile ("paddb %xmm6,%xmm6");
			asm volatile ("pand %xmm15,%xmm11");
			asm volatile ("pxor %xmm11,%xmm6");
			asm volatile ("pxor %xmm8,%xmm6");

			asm volatile ("pxor %xmm9,%xmm8");
			asm volatile ("pxor %xmm8,%xmm2");

			asm volatile ("pxor %xmm11,%xmm11");
			asm volatile ("pcmpgtb %xmm6,%xmm11");
			asm volatile ("paddb %xmm6,%xmm6");
			asm volatile ("pand %xmm15,%xmm11");
			asm volatile ("pxor %xmm11,%xmm6");
			asm volatile ("pxor %xmm9,%xmm6");

			/* lane 3 */
			asm volatile ("movdqa %0,%%xmm8" : : "m" (v[d][i + 48]));
			asm volatile ("movdqa %0,%%xmm9" : : "m" (v[d - 1][i + 48]));

			asm volatile ("pxor %xmm11,%xmm11");
			asm volatile ("pcmpgtb %xmm7,%xmm11");
			asm volatile ("paddb %xmm7,%xmm7");
			asm volatile ("pand %xmm15,%xmm11");
			asm volatile ("pxor %xmm11,%xmm7");
			asm volatile ("pxor %xmm8,%xmm7");

			asm volatile ("pxor %xmm9,%xmm8");
			asm volatile ("pxor %xmm8,%xmm3");

			asm volatile ("pxor %xmm11,%xmm11");
			asm volatile ("pcmpgtb %xmm7,%xmm11");
			asm volatile ("paddb %xmm7,%xmm7");
			asm volatile ("pand %xmm15,%xmm11");
			asm volatile ("pxor %xmm11,%xmm7");
			asm volatile ("pxor %xmm9,%xmm7");
		}

		/* single remaining disk */
		if (d == 0) {
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

			asm volatile ("movdqa %0,%%xmm8" : : "m" (v[0][i]));
			asm volatile ("movdqa %0,%%xmm9" : : "m" (v[0][i + 16]));
			asm volatile ("movdqa %0,%%xmm10" : : "m" (v[0][i + 32]));
			asm volatile ("movdqa %0,%%xmm11" : : "m" (v[0][i + 48]));
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

/*
 * GEN2 Cauchy SSE2 implementation using the AES G23 Q recurrence.
 *
 * Process two data disks at a time without crossing G23 boundaries.
 *
 * Note that it uses 16 registers, meaning that x64 is required.
 */
static __always_inline void raid_gen2_sse2ext_g23(int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *q;
	int d, l;
	size_t i;

	l = nd - 1;
	p = v[nd];
	q = v[nd + 1];

	int g23_boundary = raid_g23_boundary(l - 1);

	raid_sse_begin();

	asm volatile ("movdqa %0,%%xmm15" : : "m" (gfconst16.poly[0]));

	for (i = 0; i < size; i += 64) {
		int boundary = g23_boundary;

		asm volatile ("movdqa %0,%%xmm0" : : "m" (v[l][i]));
		asm volatile ("movdqa %0,%%xmm1" : : "m" (v[l][i + 16]));
		asm volatile ("movdqa %0,%%xmm2" : : "m" (v[l][i + 32]));
		asm volatile ("movdqa %0,%%xmm3" : : "m" (v[l][i + 48]));
		asm volatile ("movdqa %xmm0,%xmm4");
		asm volatile ("movdqa %xmm1,%xmm5");
		asm volatile ("movdqa %xmm2,%xmm6");
		asm volatile ("movdqa %xmm3,%xmm7");

		d = l - 1;

		for (;;) {
			/*
			 * All transitions above 'boundary' are x2.
			 * Process two disks per iteration without crossing
			 * the next G23 boundary.
			 */
			for (; d >= boundary + 2; d -= 2) {
				/*
				 * Lane 0.
				 *
				 * Q = 2 * Q + D[d]
				 * Q = 2 * Q + D[d - 1]
				 *
				 * Compute D[d] ^ D[d - 1] independently and use
				 * it to update P with a single dependent XOR.
				 */
				asm volatile ("movdqa %0,%%xmm8" : : "m" (v[d][i]));
				asm volatile ("movdqa %0,%%xmm9" : : "m" (v[d - 1][i]));

				asm volatile ("pxor %xmm11,%xmm11");
				asm volatile ("pcmpgtb %xmm4,%xmm11");
				asm volatile ("paddb %xmm4,%xmm4");
				asm volatile ("pand %xmm15,%xmm11");
				asm volatile ("pxor %xmm11,%xmm4");
				asm volatile ("pxor %xmm8,%xmm4");

				asm volatile ("pxor %xmm9,%xmm8");
				asm volatile ("pxor %xmm8,%xmm0");

				asm volatile ("pxor %xmm11,%xmm11");
				asm volatile ("pcmpgtb %xmm4,%xmm11");
				asm volatile ("paddb %xmm4,%xmm4");
				asm volatile ("pand %xmm15,%xmm11");
				asm volatile ("pxor %xmm11,%xmm4");
				asm volatile ("pxor %xmm9,%xmm4");

				/* lane 1 */
				asm volatile ("movdqa %0,%%xmm8" : : "m" (v[d][i + 16]));
				asm volatile ("movdqa %0,%%xmm9" : : "m" (v[d - 1][i + 16]));

				asm volatile ("pxor %xmm11,%xmm11");
				asm volatile ("pcmpgtb %xmm5,%xmm11");
				asm volatile ("paddb %xmm5,%xmm5");
				asm volatile ("pand %xmm15,%xmm11");
				asm volatile ("pxor %xmm11,%xmm5");
				asm volatile ("pxor %xmm8,%xmm5");

				asm volatile ("pxor %xmm9,%xmm8");
				asm volatile ("pxor %xmm8,%xmm1");

				asm volatile ("pxor %xmm11,%xmm11");
				asm volatile ("pcmpgtb %xmm5,%xmm11");
				asm volatile ("paddb %xmm5,%xmm5");
				asm volatile ("pand %xmm15,%xmm11");
				asm volatile ("pxor %xmm11,%xmm5");
				asm volatile ("pxor %xmm9,%xmm5");

				/* lane 2 */
				asm volatile ("movdqa %0,%%xmm8" : : "m" (v[d][i + 32]));
				asm volatile ("movdqa %0,%%xmm9" : : "m" (v[d - 1][i + 32]));

				asm volatile ("pxor %xmm11,%xmm11");
				asm volatile ("pcmpgtb %xmm6,%xmm11");
				asm volatile ("paddb %xmm6,%xmm6");
				asm volatile ("pand %xmm15,%xmm11");
				asm volatile ("pxor %xmm11,%xmm6");
				asm volatile ("pxor %xmm8,%xmm6");

				asm volatile ("pxor %xmm9,%xmm8");
				asm volatile ("pxor %xmm8,%xmm2");

				asm volatile ("pxor %xmm11,%xmm11");
				asm volatile ("pcmpgtb %xmm6,%xmm11");
				asm volatile ("paddb %xmm6,%xmm6");
				asm volatile ("pand %xmm15,%xmm11");
				asm volatile ("pxor %xmm11,%xmm6");
				asm volatile ("pxor %xmm9,%xmm6");

				/* lane 3 */
				asm volatile ("movdqa %0,%%xmm8" : : "m" (v[d][i + 48]));
				asm volatile ("movdqa %0,%%xmm9" : : "m" (v[d - 1][i + 48]));

				asm volatile ("pxor %xmm11,%xmm11");
				asm volatile ("pcmpgtb %xmm7,%xmm11");
				asm volatile ("paddb %xmm7,%xmm7");
				asm volatile ("pand %xmm15,%xmm11");
				asm volatile ("pxor %xmm11,%xmm7");
				asm volatile ("pxor %xmm8,%xmm7");

				asm volatile ("pxor %xmm9,%xmm8");
				asm volatile ("pxor %xmm8,%xmm3");

				asm volatile ("pxor %xmm11,%xmm11");
				asm volatile ("pcmpgtb %xmm7,%xmm11");
				asm volatile ("paddb %xmm7,%xmm7");
				asm volatile ("pand %xmm15,%xmm11");
				asm volatile ("pxor %xmm11,%xmm7");
				asm volatile ("pxor %xmm9,%xmm7");
			}

			/*
			 * The first x2 segment can have odd length.
			 * Consume its last x2 transition separately.
			 */
			if (d > boundary) {
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

				--d;
			}

			/*
			 * boundary == -1 identifies the final x2-only segment.
			 * At this point all remaining disks have been consumed.
			 */
			if (boundary < 0)
				break;

			/*
			 * d == boundary.
			 *
			 * Perform the fixed G23 x3 transition and then add
			 * the current disk to P and Q.
			 */
			asm volatile ("movdqa %xmm4,%xmm8");
			asm volatile ("movdqa %xmm5,%xmm9");
			asm volatile ("movdqa %xmm6,%xmm10");
			asm volatile ("movdqa %xmm7,%xmm11");
			asm volatile ("pxor %xmm12,%xmm12");
			asm volatile ("pxor %xmm13,%xmm13");
			asm volatile ("pxor %xmm14,%xmm14");
			asm volatile ("pcmpgtb %xmm8,%xmm12");
			asm volatile ("pcmpgtb %xmm9,%xmm13");
			asm volatile ("pcmpgtb %xmm10,%xmm14");
			asm volatile ("paddb %xmm4,%xmm4");
			asm volatile ("paddb %xmm5,%xmm5");
			asm volatile ("paddb %xmm6,%xmm6");
			asm volatile ("paddb %xmm7,%xmm7");
			asm volatile ("pxor %xmm8,%xmm4");
			asm volatile ("pxor %xmm8,%xmm8");
			asm volatile ("pcmpgtb %xmm11,%xmm8");
			asm volatile ("pand %xmm15,%xmm12");
			asm volatile ("pand %xmm15,%xmm13");
			asm volatile ("pand %xmm15,%xmm14");
			asm volatile ("pand %xmm15,%xmm8");
			asm volatile ("pxor %xmm12,%xmm4");
			asm volatile ("pxor %xmm13,%xmm5");
			asm volatile ("pxor %xmm14,%xmm6");
			asm volatile ("pxor %xmm8,%xmm7");
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

			--d;

			/*
			 * G23 boundaries are exactly 51 transitions apart.
			 * After d = 50 there are no more x3 transitions.
			 */
			boundary -= 51;
			if (boundary < 50)
				boundary = -1;
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

void raid_gen2_sse2ext_raid(int nd, size_t size, void **vv)
{
	raid_gen2_sse2ext_x2(nd, size, vv);
}

void raid_gen2_sse2ext_aes(int nd, size_t size, void **vv)
{
	if (raid_g23_x2_only(nd))
		raid_gen2_sse2ext_x2(nd, size, vv);
	else
		raid_gen2_sse2ext_g23(nd, size, vv);
}
#endif

/*
 * GENz (triple parity with powers of 2^-1) SSE2 implementation
 */
void raid_genz_sse2_raid(int nd, size_t size, void **vv)
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
void raid_genz_sse2ext_raid(int nd, size_t size, void **vv)
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
		raid_gen_register(RAID_ALGO_CAUCHY_PAR1, "sse2", raid_gen1_sse2, RAID_POLY_ANY);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR2, "sse2", raid_gen2_sse2_raid, RAID_POLY_RAID);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR2, "sse2", raid_gen2_sse2_aes, RAID_POLY_AES);
#ifdef CONFIG_X86_64
		if (!raid_cpu_has_slow_extendedreg()) {
			raid_gen_register(RAID_ALGO_CAUCHY_PAR2, "sse2e", raid_gen2_sse2ext_raid, RAID_POLY_RAID);
			raid_gen_register(RAID_ALGO_CAUCHY_PAR2, "sse2e", raid_gen2_sse2ext_aes, RAID_POLY_AES);
		}
		/* note that raid_cpu_has_slow_extendedreg() doesn't affect vandermonde */
		raid_gen_register(RAID_ALGO_VANDERMONDE_PAR3, "sse2e", raid_genz_sse2ext_raid, RAID_POLY_RAID);
#else
		raid_gen_register(RAID_ALGO_VANDERMONDE_PAR3, "sse2", raid_genz_sse2_raid, RAID_POLY_RAID);
#endif
	}
}
#endif
