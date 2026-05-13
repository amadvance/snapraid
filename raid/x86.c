// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2013 Andrea Mazzoleni

#include "internal.h"
#include "gf.h"
#include "cpu.h"

/*
 * For x86 optimizations you can see:
 *
 * Software optimization resources
 * http://www.agner.org/optimize/
 *
 * x86, x64 Instruction Latency, Memory Latency and CPUID dumps
 * http://users.atw.hu/instlatx64/
 */

#if defined(CONFIG_X86) && defined(CONFIG_SSE2)
/*
 * GEN1 (RAID5 with xor) SSE2 implementation
 *
 * Intentionally don't process more than 64 bytes because 64 is the typical
 * cache block, and processing 128 bytes doesn't increase performance, and in
 * some cases it even decreases it.
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

	/* additional unrolling makes it slower */
	for (i = 0; i < size; i += 64) {
		asm volatile ("movdqa %0,%%xmm0" : : "m" (v[l][i]));
		asm volatile ("movdqa %0,%%xmm1" : : "m" (v[l][i + 16]));
		asm volatile ("movdqa %0,%%xmm2" : : "m" (v[l][i + 32]));
		asm volatile ("movdqa %0,%%xmm3" : : "m" (v[l][i + 48]));
		for (d = l - 1; d >= 0; --d) {
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
#endif

#if defined(CONFIG_X86) && defined(CONFIG_AVX2)
/*
 * GEN1 (RAID5 with xor) AVX2 implementation
 *
 * Intentionally don't process more than 64 bytes because 64 is the typical
 * cache block, and processing 128 bytes doesn't increase performance, and in
 * some cases it even decreases it.
 */
void raid_gen1_avx2(int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	int d, l;
	size_t i;

	l = nd - 1;
	p = v[nd];

	raid_avx_begin();

	/* additional unrolling makes it slower */
	for (i = 0; i < size; i += 64) {
		asm volatile ("vmovdqa %0,%%ymm0" : : "m" (v[l][i]));
		asm volatile ("vmovdqa %0,%%ymm1" : : "m" (v[l][i + 32]));
		for (d = l - 1; d >= 0; --d) {
			asm volatile ("vpxor %0,%%ymm0,%%ymm0" : : "m" (v[d][i]));
			asm volatile ("vpxor %0,%%ymm1,%%ymm1" : : "m" (v[d][i + 32]));
		}
		asm volatile ("vmovntdq %%ymm0,%0" : "=m" (p[i]));
		asm volatile ("vmovntdq %%ymm1,%0" : "=m" (p[i + 32]));
	}

	raid_avx_end();
}
#endif

#if defined(CONFIG_X86_64) && defined(CONFIG_AVX512BW)
/*
 * GEN1 (RAID5 with xor) AVX512BW implementation
 *
 * Note that in true AVX512F would suffice, but we don't want to add
 * specific support for AVX512F because this would be the only function
 * to benefit from that.
 *
 * Intentionally don't process more than 64 bytes because 64 is the typical
 * cache block, and processing 128 bytes doesn't increase performance, and in
 * some cases it even decreases it.
 */
void raid_gen1_avx512bw(int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t**)vv;
	uint8_t *p;
	int d, l;
	size_t i;

	l = nd - 1;
	p = v[nd];

	raid_avx_begin();

	/* additional unrolling makes it slower */
	for (i = 0; i < size; i += 64) {
		asm volatile ("vmovdqa64 %0,%%zmm0" : : "m" (v[l][i]));
		for (d = l - 1; d >= 0; --d) {
			asm volatile ("vpxorq %0,%%zmm0,%%zmm0" : : "m" (v[d][i]));
		}
		asm volatile ("vmovntdq %%zmm0,%0" : "=m" (p[i]));
	}

	raid_avx_end();
}
#endif

#if defined(CONFIG_X86) && defined(CONFIG_SSE2)
static const struct gfconst16 {
	uint8_t poly[16];
	uint8_t low4[16];
} gfconst16 __aligned(16) = {
	{
		RAID_POLY, RAID_POLY, RAID_POLY, RAID_POLY, RAID_POLY, RAID_POLY, RAID_POLY, RAID_POLY,
		RAID_POLY, RAID_POLY, RAID_POLY, RAID_POLY, RAID_POLY, RAID_POLY, RAID_POLY, RAID_POLY
	},
	{
		0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f,
		0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f
	},
};
#endif

#if defined(CONFIG_X86) && defined(CONFIG_SSE2)
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
#endif

#if defined(CONFIG_X86) && defined(CONFIG_AVX2)
/*
 * GEN2 (RAID6 with powers of 2) AVX2 implementation
 */
void raid_gen2_avx2(int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *q;
	int d, l;
	size_t i;

	l = nd - 1;
	p = v[nd];
	q = v[nd + 1];

	raid_avx_begin();

	asm volatile ("vpbroadcastb %0, %%ymm7" : : "m" (gfconst16.poly[0]));
	asm volatile ("vpxor %ymm6,%ymm6,%ymm6");

	for (i = 0; i < size; i += 64) {
		asm volatile ("vmovdqa %0,%%ymm0" : : "m" (v[l][i]));
		asm volatile ("vmovdqa %0,%%ymm1" : : "m" (v[l][i + 32]));
		asm volatile ("vmovdqa %ymm0,%ymm2");
		asm volatile ("vmovdqa %ymm1,%ymm3");
		for (d = l - 1; d >= 0; --d) {
			/* scale by 2 before adding the current disk so the first (last processed) disk with factor 1 avoids doubling */
			asm volatile ("vpcmpgtb %ymm2,%ymm6,%ymm4");
			asm volatile ("vpcmpgtb %ymm3,%ymm6,%ymm5");
			asm volatile ("vpaddb %ymm2,%ymm2,%ymm2");
			asm volatile ("vpaddb %ymm3,%ymm3,%ymm3");
			asm volatile ("vpand %ymm7,%ymm4,%ymm4");
			asm volatile ("vpand %ymm7,%ymm5,%ymm5");
			asm volatile ("vpxor %ymm4,%ymm2,%ymm2");
			asm volatile ("vpxor %ymm5,%ymm3,%ymm3");

			asm volatile ("vmovdqa %0,%%ymm4" : : "m" (v[d][i]));
			asm volatile ("vmovdqa %0,%%ymm5" : : "m" (v[d][i + 32]));
			asm volatile ("vpxor %ymm4,%ymm0,%ymm0");
			asm volatile ("vpxor %ymm5,%ymm1,%ymm1");
			asm volatile ("vpxor %ymm4,%ymm2,%ymm2");
			asm volatile ("vpxor %ymm5,%ymm3,%ymm3");
		}
		asm volatile ("vmovntdq %%ymm0,%0" : "=m" (p[i]));
		asm volatile ("vmovntdq %%ymm1,%0" : "=m" (p[i + 32]));
		asm volatile ("vmovntdq %%ymm2,%0" : "=m" (q[i]));
		asm volatile ("vmovntdq %%ymm3,%0" : "=m" (q[i + 32]));
	}

	raid_avx_end();
}
#endif

#if defined(CONFIG_X86_64) && defined(CONFIG_AVX512BW)
/*
 * GEN2 (RAID6 with powers of 2) AVX512BW implementation
 */
void raid_gen2_avx512bw(int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t**)vv;
	uint8_t *p;
	uint8_t *q;
	int d, l;
	size_t i;

	l = nd - 1;
	p = v[nd];
	q = v[nd + 1];

	raid_avx_begin();

	asm volatile ("vpbroadcastb %0, %%zmm14" : : "m" (gfconst16.poly[0]));
	asm volatile ("vpxorq %zmm6,%zmm6,%zmm6");

	for (i = 0; i < size; i += 64) {
		asm volatile ("vmovdqa64 %0,%%zmm0" : : "m" (v[l][i]));
		asm volatile ("vmovdqa64 %zmm0,%zmm1");
		for (d = l - 1; d >= 0; --d) {
			/* scale by 2 before adding the current disk so the first (last processed) disk with factor 1 avoids doubling */
			asm volatile ("vpcmpgtb %zmm1,%zmm6,%k1");
			asm volatile ("vpmovm2b %k1,%zmm11");
			asm volatile ("vpaddb %zmm1,%zmm1,%zmm1");
			asm volatile ("vpandq %zmm14,%zmm11,%zmm11");
			asm volatile ("vpxorq %zmm11,%zmm1,%zmm1");

			asm volatile ("vmovdqa64 %0,%%zmm4" : : "m" (v[d][i]));
			asm volatile ("vpxorq %zmm4,%zmm0,%zmm0");
			asm volatile ("vpxorq %zmm4,%zmm1,%zmm1");
		}
		asm volatile ("vmovntdq %%zmm0,%0" : "=m" (p[i]));
		asm volatile ("vmovntdq %%zmm1,%0" : "=m" (q[i]));
	}

	raid_avx_end();
}
#endif

#if defined(CONFIG_X86_64) && defined(CONFIG_SSE2)
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

#if defined(CONFIG_X86) && defined(CONFIG_SSSE3)
/*
 * GEN3 (triple parity with Cauchy matrix) SSSE3 implementation
 */
void raid_gen3_ssse3(int nd, size_t size, void **vv)
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

	/* special case with only one data disk */
	if (l == 0) {
		for (i = 0; i < 3; ++i)
			memcpy(v[1 + i], v[0], size);
		return;
	}

	raid_sse_begin();

	/* generic case with at least two data disks */
	asm volatile ("movdqa %0,%%xmm3" : : "m" (gfconst16.poly[0]));
	asm volatile ("movdqa %0,%%xmm7" : : "m" (gfconst16.low4[0]));

	for (i = 0; i < size; i += 16) {
		/* last disk without the multiplication by two */
		asm volatile ("movdqa %0,%%xmm4" : : "m" (v[l][i]));

		asm volatile ("movdqa %xmm4,%xmm0");
		asm volatile ("movdqa %xmm4,%xmm1");

		asm volatile ("movdqa %xmm4,%xmm5");
		asm volatile ("psrlw  $4,%xmm5");
		asm volatile ("pand   %xmm7,%xmm4");
		asm volatile ("pand   %xmm7,%xmm5");

		asm volatile ("movdqa %0,%%xmm2" : : "m" (gfgenpshufb[l][0][0][0]));
		asm volatile ("movdqa %0,%%xmm6" : : "m" (gfgenpshufb[l][0][1][0]));
		asm volatile ("pshufb %xmm4,%xmm2");
		asm volatile ("pshufb %xmm5,%xmm6");
		asm volatile ("pxor   %xmm6,%xmm2");

		/* intermediate disks */
		for (d = l - 1; d > 0; --d) {
			asm volatile ("movdqa %0,%%xmm4" : : "m" (v[d][i]));

			asm volatile ("pxor %xmm5,%xmm5");
			asm volatile ("pcmpgtb %xmm1,%xmm5");
			asm volatile ("paddb %xmm1,%xmm1");
			asm volatile ("pand %xmm3,%xmm5");
			asm volatile ("pxor %xmm5,%xmm1");

			asm volatile ("pxor %xmm4,%xmm0");
			asm volatile ("pxor %xmm4,%xmm1");

			asm volatile ("movdqa %xmm4,%xmm5");
			asm volatile ("psrlw  $4,%xmm5");
			asm volatile ("pand   %xmm7,%xmm4");
			asm volatile ("pand   %xmm7,%xmm5");

			asm volatile ("movdqa %0,%%xmm6" : : "m" (gfgenpshufb[d][0][0][0]));
			asm volatile ("pshufb %xmm4,%xmm6");
			asm volatile ("pxor   %xmm6,%xmm2");
			asm volatile ("movdqa %0,%%xmm6" : : "m" (gfgenpshufb[d][0][1][0]));
			asm volatile ("pshufb %xmm5,%xmm6");
			asm volatile ("pxor   %xmm6,%xmm2");
		}

		/* first disk with all coefficients at 1 */
		asm volatile ("movdqa %0,%%xmm4" : : "m" (v[0][i]));

		asm volatile ("pxor %xmm5,%xmm5");
		asm volatile ("pcmpgtb %xmm1,%xmm5");
		asm volatile ("paddb %xmm1,%xmm1");
		asm volatile ("pand %xmm3,%xmm5");
		asm volatile ("pxor %xmm5,%xmm1");

		asm volatile ("pxor %xmm4,%xmm0");
		asm volatile ("pxor %xmm4,%xmm1");
		asm volatile ("pxor %xmm4,%xmm2");

		asm volatile ("movntdq %%xmm0,%0" : "=m" (p[i]));
		asm volatile ("movntdq %%xmm1,%0" : "=m" (q[i]));
		asm volatile ("movntdq %%xmm2,%0" : "=m" (r[i]));
	}

	raid_sse_end();
}
#endif

#if defined(CONFIG_X86_64) && defined(CONFIG_SSSE3)
/*
 * GEN3 (triple parity with Cauchy matrix) SSSE3 implementation
 *
 * Note that it uses 16 registers, meaning that x64 is required.
 */
void raid_gen3_ssse3ext(int nd, size_t size, void **vv)
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

	/* special case with only one data disk */
	if (l == 0) {
		for (i = 0; i < 3; ++i)
			memcpy(v[1 + i], v[0], size);
		return;
	}

	raid_sse_begin();

	/* generic case with at least two data disks */
	asm volatile ("movdqa %0,%%xmm3" : : "m" (gfconst16.poly[0]));
	asm volatile ("movdqa %0,%%xmm11" : : "m" (gfconst16.low4[0]));

	for (i = 0; i < size; i += 32) {
		/* last disk without the by two multiplication */
		asm volatile ("movdqa %0,%%xmm4" : : "m" (v[l][i]));
		asm volatile ("movdqa %0,%%xmm12" : : "m" (v[l][i + 16]));

		asm volatile ("movdqa %xmm4,%xmm0");
		asm volatile ("movdqa %xmm4,%xmm1");
		asm volatile ("movdqa %xmm12,%xmm8");
		asm volatile ("movdqa %xmm12,%xmm9");

		asm volatile ("movdqa %xmm4,%xmm5");
		asm volatile ("movdqa %xmm12,%xmm13");
		asm volatile ("psrlw  $4,%xmm5");
		asm volatile ("psrlw  $4,%xmm13");
		asm volatile ("pand   %xmm11,%xmm4");
		asm volatile ("pand   %xmm11,%xmm12");
		asm volatile ("pand   %xmm11,%xmm5");
		asm volatile ("pand   %xmm11,%xmm13");

		asm volatile ("movdqa %0,%%xmm2" : : "m" (gfgenpshufb[l][0][0][0]));
		asm volatile ("movdqa %0,%%xmm7" : : "m" (gfgenpshufb[l][0][1][0]));
		asm volatile ("movdqa %xmm2,%xmm10");
		asm volatile ("movdqa %xmm7,%xmm15");
		asm volatile ("pshufb %xmm4,%xmm2");
		asm volatile ("pshufb %xmm12,%xmm10");
		asm volatile ("pshufb %xmm5,%xmm7");
		asm volatile ("pshufb %xmm13,%xmm15");
		asm volatile ("pxor   %xmm7,%xmm2");
		asm volatile ("pxor   %xmm15,%xmm10");

		/* intermediate disks */
		for (d = l - 1; d > 0; --d) {
			asm volatile ("movdqa %0,%%xmm4" : : "m" (v[d][i]));
			asm volatile ("movdqa %0,%%xmm12" : : "m" (v[d][i + 16]));

			asm volatile ("pxor %xmm5,%xmm5");
			asm volatile ("pxor %xmm13,%xmm13");
			asm volatile ("pcmpgtb %xmm1,%xmm5");
			asm volatile ("pcmpgtb %xmm9,%xmm13");
			asm volatile ("paddb %xmm1,%xmm1");
			asm volatile ("paddb %xmm9,%xmm9");
			asm volatile ("pand %xmm3,%xmm5");
			asm volatile ("pand %xmm3,%xmm13");
			asm volatile ("pxor %xmm5,%xmm1");
			asm volatile ("pxor %xmm13,%xmm9");

			asm volatile ("pxor %xmm4,%xmm0");
			asm volatile ("pxor %xmm4,%xmm1");
			asm volatile ("pxor %xmm12,%xmm8");
			asm volatile ("pxor %xmm12,%xmm9");

			asm volatile ("movdqa %xmm4,%xmm5");
			asm volatile ("movdqa %xmm12,%xmm13");
			asm volatile ("psrlw  $4,%xmm5");
			asm volatile ("psrlw  $4,%xmm13");
			asm volatile ("pand   %xmm11,%xmm4");
			asm volatile ("pand   %xmm11,%xmm12");
			asm volatile ("pand   %xmm11,%xmm5");
			asm volatile ("pand   %xmm11,%xmm13");

			asm volatile ("movdqa %0,%%xmm6" : : "m" (gfgenpshufb[d][0][0][0]));
			asm volatile ("movdqa %0,%%xmm7" : : "m" (gfgenpshufb[d][0][1][0]));
			asm volatile ("movdqa %xmm6,%xmm14");
			asm volatile ("movdqa %xmm7,%xmm15");
			asm volatile ("pshufb %xmm4,%xmm6");
			asm volatile ("pshufb %xmm12,%xmm14");
			asm volatile ("pshufb %xmm5,%xmm7");
			asm volatile ("pshufb %xmm13,%xmm15");
			asm volatile ("pxor   %xmm6,%xmm2");
			asm volatile ("pxor   %xmm14,%xmm10");
			asm volatile ("pxor   %xmm7,%xmm2");
			asm volatile ("pxor   %xmm15,%xmm10");
		}

		/* first disk with all coefficients at 1 */
		asm volatile ("movdqa %0,%%xmm4" : : "m" (v[0][i]));
		asm volatile ("movdqa %0,%%xmm12" : : "m" (v[0][i + 16]));

		asm volatile ("pxor %xmm5,%xmm5");
		asm volatile ("pxor %xmm13,%xmm13");
		asm volatile ("pcmpgtb %xmm1,%xmm5");
		asm volatile ("pcmpgtb %xmm9,%xmm13");
		asm volatile ("paddb %xmm1,%xmm1");
		asm volatile ("paddb %xmm9,%xmm9");
		asm volatile ("pand %xmm3,%xmm5");
		asm volatile ("pand %xmm3,%xmm13");
		asm volatile ("pxor %xmm5,%xmm1");
		asm volatile ("pxor %xmm13,%xmm9");

		asm volatile ("pxor %xmm4,%xmm0");
		asm volatile ("pxor %xmm4,%xmm1");
		asm volatile ("pxor %xmm4,%xmm2");
		asm volatile ("pxor %xmm12,%xmm8");
		asm volatile ("pxor %xmm12,%xmm9");
		asm volatile ("pxor %xmm12,%xmm10");

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

#if defined(CONFIG_X86_64) && defined(CONFIG_AVX2)
/*
 * GEN3 (triple parity with Cauchy matrix) AVX2 implementation
 *
 * Note that it uses 16 registers, meaning that x64 is required.
 */
void raid_gen3_avx2ext(int nd, size_t size, void **vv)
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

	/* special case with only one data disk */
	if (l == 0) {
		for (i = 0; i < 3; ++i)
			memcpy(v[1 + i], v[0], size);
		return;
	}

	raid_avx_begin();

	/* generic case with at least two data disks */
	asm volatile ("vpbroadcastb %0, %%ymm3" : : "m" (gfconst16.poly[0]));
	asm volatile ("vpbroadcastb %0, %%ymm11" : : "m" (gfconst16.low4[0]));

	for (i = 0; i < size; i += 64) {
		/* last disk without the by two multiplication */
		asm volatile ("vmovdqa %0,%%ymm4" : : "m" (v[l][i]));
		asm volatile ("vmovdqa %0,%%ymm12" : : "m" (v[l][i + 32]));

		asm volatile ("vmovdqa %ymm4,%ymm0");
		asm volatile ("vmovdqa %ymm4,%ymm1");
		asm volatile ("vmovdqa %ymm12,%ymm8");
		asm volatile ("vmovdqa %ymm12,%ymm9");

		asm volatile ("vpsrlw  $4,%ymm4,%ymm5");
		asm volatile ("vpsrlw  $4,%ymm12,%ymm13");
		asm volatile ("vpand   %ymm11,%ymm4,%ymm4");
		asm volatile ("vpand   %ymm11,%ymm12,%ymm12");
		asm volatile ("vpand   %ymm11,%ymm5,%ymm5");
		asm volatile ("vpand   %ymm11,%ymm13,%ymm13");

		asm volatile ("vbroadcasti128 %0,%%ymm10" : : "m" (gfgenpshufb[l][0][0][0]));
		asm volatile ("vbroadcasti128 %0,%%ymm15" : : "m" (gfgenpshufb[l][0][1][0]));
		asm volatile ("vpshufb %ymm4,%ymm10,%ymm2");
		asm volatile ("vpshufb %ymm12,%ymm10,%ymm10");
		asm volatile ("vpshufb %ymm5,%ymm15,%ymm7");
		asm volatile ("vpshufb %ymm13,%ymm15,%ymm15");
		asm volatile ("vpxor   %ymm7,%ymm2,%ymm2");
		asm volatile ("vpxor   %ymm15,%ymm10,%ymm10");

		/* intermediate disks */
		for (d = l - 1; d > 0; --d) {
			asm volatile ("vmovdqa %0,%%ymm4" : : "m" (v[d][i]));
			asm volatile ("vmovdqa %0,%%ymm12" : : "m" (v[d][i + 32]));

			asm volatile ("vpxor %ymm5,%ymm5,%ymm5");
			asm volatile ("vpxor %ymm13,%ymm13,%ymm13");
			asm volatile ("vpcmpgtb %ymm1,%ymm5,%ymm5");
			asm volatile ("vpcmpgtb %ymm9,%ymm13,%ymm13");
			asm volatile ("vpaddb %ymm1,%ymm1,%ymm1");
			asm volatile ("vpaddb %ymm9,%ymm9,%ymm9");
			asm volatile ("vpand %ymm3,%ymm5,%ymm5");
			asm volatile ("vpand %ymm3,%ymm13,%ymm13");
			asm volatile ("vpxor %ymm5,%ymm1,%ymm1");
			asm volatile ("vpxor %ymm13,%ymm9,%ymm9");

			asm volatile ("vpxor %ymm4,%ymm0,%ymm0");
			asm volatile ("vpxor %ymm4,%ymm1,%ymm1");
			asm volatile ("vpxor %ymm12,%ymm8,%ymm8");
			asm volatile ("vpxor %ymm12,%ymm9,%ymm9");

			asm volatile ("vpsrlw  $4,%ymm4,%ymm5");
			asm volatile ("vpsrlw  $4,%ymm12,%ymm13");
			asm volatile ("vpand   %ymm11,%ymm4,%ymm4");
			asm volatile ("vpand   %ymm11,%ymm12,%ymm12");
			asm volatile ("vpand   %ymm11,%ymm5,%ymm5");
			asm volatile ("vpand   %ymm11,%ymm13,%ymm13");

			asm volatile ("vbroadcasti128 %0,%%ymm14" : : "m" (gfgenpshufb[d][0][0][0]));
			asm volatile ("vbroadcasti128 %0,%%ymm15" : : "m" (gfgenpshufb[d][0][1][0]));
			asm volatile ("vpshufb %ymm4,%ymm14,%ymm6");
			asm volatile ("vpshufb %ymm12,%ymm14,%ymm14");
			asm volatile ("vpshufb %ymm5,%ymm15,%ymm7");
			asm volatile ("vpshufb %ymm13,%ymm15,%ymm15");
			asm volatile ("vpxor   %ymm6,%ymm2,%ymm2");
			asm volatile ("vpxor   %ymm14,%ymm10,%ymm10");
			asm volatile ("vpxor   %ymm7,%ymm2,%ymm2");
			asm volatile ("vpxor   %ymm15,%ymm10,%ymm10");
		}

		/* first disk with all coefficients at 1 */
		asm volatile ("vmovdqa %0,%%ymm4" : : "m" (v[0][i]));
		asm volatile ("vmovdqa %0,%%ymm12" : : "m" (v[0][i + 32]));

		asm volatile ("vpxor %ymm5,%ymm5,%ymm5");
		asm volatile ("vpxor %ymm13,%ymm13,%ymm13");
		asm volatile ("vpcmpgtb %ymm1,%ymm5,%ymm5");
		asm volatile ("vpcmpgtb %ymm9,%ymm13,%ymm13");
		asm volatile ("vpaddb %ymm1,%ymm1,%ymm1");
		asm volatile ("vpaddb %ymm9,%ymm9,%ymm9");
		asm volatile ("vpand %ymm3,%ymm5,%ymm5");
		asm volatile ("vpand %ymm3,%ymm13,%ymm13");
		asm volatile ("vpxor %ymm5,%ymm1,%ymm1");
		asm volatile ("vpxor %ymm13,%ymm9,%ymm9");

		asm volatile ("vpxor %ymm4,%ymm0,%ymm0");
		asm volatile ("vpxor %ymm4,%ymm1,%ymm1");
		asm volatile ("vpxor %ymm4,%ymm2,%ymm2");
		asm volatile ("vpxor %ymm12,%ymm8,%ymm8");
		asm volatile ("vpxor %ymm12,%ymm9,%ymm9");
		asm volatile ("vpxor %ymm12,%ymm10,%ymm10");

		asm volatile ("vmovntdq %%ymm0,%0" : "=m" (p[i]));
		asm volatile ("vmovntdq %%ymm8,%0" : "=m" (p[i + 32]));
		asm volatile ("vmovntdq %%ymm1,%0" : "=m" (q[i]));
		asm volatile ("vmovntdq %%ymm9,%0" : "=m" (q[i + 32]));
		asm volatile ("vmovntdq %%ymm2,%0" : "=m" (r[i]));
		asm volatile ("vmovntdq %%ymm10,%0" : "=m" (r[i + 32]));
	}

	raid_avx_end();
}
#endif

#if defined(CONFIG_X86_64) && defined(CONFIG_AVX512BW)
/*
 * GEN3 (triple parity with Cauchy matrix) AVX512BW implementation
 */
void raid_gen3_avx512bw(int nd, size_t size, void **vv)
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

	/* special case with only one data disk */
	if (l == 0) {
		for (i = 0; i < 3; ++i)
			memcpy(v[1 + i], v[0], size);
		return;
	}

	raid_avx_begin();

	/* generic case with at least two data disks */
	asm volatile ("vpxorq %zmm6,%zmm6,%zmm6");
	asm volatile ("vpbroadcastb %0,%%zmm14" : : "m" (gfconst16.poly[0]));
	asm volatile ("vpbroadcastb %0,%%zmm15" : : "m" (gfconst16.low4[0]));

	for (i = 0; i < size; i += 64) {
		/* last disk */
		asm volatile ("vmovdqa64 %0,%%zmm10" : : "m" (v[l][i]));

		asm volatile ("vmovdqa64 %zmm10,%zmm0");

		asm volatile ("vmovdqa64 %zmm10,%zmm1");
		asm volatile ("vpcmpgtb  %zmm1,%zmm6,%k1");
		asm volatile ("vpmovm2b  %k1,%zmm11");
		asm volatile ("vpaddb    %zmm1,%zmm1,%zmm1");
		asm volatile ("vpandq    %zmm14,%zmm11,%zmm11");
		asm volatile ("vpxorq    %zmm11,%zmm1,%zmm1");

		asm volatile ("vpsrlw    $4,%zmm10,%zmm11");
		asm volatile ("vpandq    %zmm15,%zmm10,%zmm10");
		asm volatile ("vpandq    %zmm15,%zmm11,%zmm11");

		asm volatile ("vbroadcasti32x4 %0,%%zmm2" : : "m" (gfgenpshufb[l][0][0][0]));
		asm volatile ("vbroadcasti32x4 %0,%%zmm13" : : "m" (gfgenpshufb[l][0][1][0]));
		asm volatile ("vpshufb   %zmm10,%zmm2,%zmm2");
		asm volatile ("vpshufb   %zmm11,%zmm13,%zmm13");
		asm volatile ("vpxorq    %zmm13,%zmm2,%zmm2");

		/* intermediate disks */
		for (d = l - 1; d > 0; --d) {
			asm volatile ("vmovdqa64 %0,%%zmm10" : : "m" (v[d][i]));

			asm volatile ("vpxorq    %zmm10,%zmm0,%zmm0");

			asm volatile ("vpxorq    %zmm10,%zmm1,%zmm1");
			asm volatile ("vpcmpgtb  %zmm1,%zmm6,%k1");
			asm volatile ("vpmovm2b  %k1,%zmm11");
			asm volatile ("vpaddb    %zmm1,%zmm1,%zmm1");
			asm volatile ("vpandq    %zmm14,%zmm11,%zmm11");
			asm volatile ("vpxorq    %zmm11,%zmm1,%zmm1");

			asm volatile ("vpsrlw    $4,%zmm10,%zmm11");
			asm volatile ("vpandq    %zmm15,%zmm10,%zmm10");
			asm volatile ("vpandq    %zmm15,%zmm11,%zmm11");

			asm volatile ("vbroadcasti32x4 %0,%%zmm12" : : "m" (gfgenpshufb[d][0][0][0]));
			asm volatile ("vbroadcasti32x4 %0,%%zmm13" : : "m" (gfgenpshufb[d][0][1][0]));
			asm volatile ("vpshufb   %zmm10,%zmm12,%zmm12");
			asm volatile ("vpshufb   %zmm11,%zmm13,%zmm13");
			asm volatile ("vpxorq    %zmm12,%zmm2,%zmm2");
			asm volatile ("vpxorq    %zmm13,%zmm2,%zmm2");
		}

		/* first disk with all coefficients at 1 */
		asm volatile ("vmovdqa64 %0,%%zmm10" : : "m" (v[0][i]));

		asm volatile ("vpxorq    %zmm10,%zmm0,%zmm0");
		asm volatile ("vpxorq    %zmm10,%zmm1,%zmm1");
		asm volatile ("vpxorq    %zmm10,%zmm2,%zmm2");

		asm volatile ("vmovntdq  %%zmm0,%0" : "=m" (p[i]));
		asm volatile ("vmovntdq  %%zmm1,%0" : "=m" (q[i]));
		asm volatile ("vmovntdq  %%zmm2,%0" : "=m" (r[i]));
	}

	raid_avx_end();
}
#endif

#if defined(CONFIG_X86) && defined(CONFIG_SSSE3)
/*
 * GEN4 (quad parity with Cauchy matrix) SSSE3 implementation
 */
void raid_gen4_ssse3(int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *q;
	uint8_t *r;
	uint8_t *s;
	int d, l;
	size_t i;

	l = nd - 1;
	p = v[nd];
	q = v[nd + 1];
	r = v[nd + 2];
	s = v[nd + 3];

	/* special case with only one data disk */
	if (l == 0) {
		for (i = 0; i < 4; ++i)
			memcpy(v[1 + i], v[0], size);
		return;
	}

	raid_sse_begin();

	/* generic case with at least two data disks */
	for (i = 0; i < size; i += 16) {
		/* last disk without the by two multiplication */
		asm volatile ("movdqa %0,%%xmm7" : : "m" (gfconst16.low4[0]));
		asm volatile ("movdqa %0,%%xmm4" : : "m" (v[l][i]));

		asm volatile ("movdqa %xmm4,%xmm0");
		asm volatile ("movdqa %xmm4,%xmm1");

		asm volatile ("movdqa %xmm4,%xmm5");
		asm volatile ("psrlw  $4,%xmm5");
		asm volatile ("pand   %xmm7,%xmm4");
		asm volatile ("pand   %xmm7,%xmm5");

		asm volatile ("movdqa %0,%%xmm2" : : "m" (gfgenpshufb[l][0][0][0]));
		asm volatile ("movdqa %0,%%xmm7" : : "m" (gfgenpshufb[l][0][1][0]));
		asm volatile ("pshufb %xmm4,%xmm2");
		asm volatile ("pshufb %xmm5,%xmm7");
		asm volatile ("pxor   %xmm7,%xmm2");

		asm volatile ("movdqa %0,%%xmm3" : : "m" (gfgenpshufb[l][1][0][0]));
		asm volatile ("movdqa %0,%%xmm7" : : "m" (gfgenpshufb[l][1][1][0]));
		asm volatile ("pshufb %xmm4,%xmm3");
		asm volatile ("pshufb %xmm5,%xmm7");
		asm volatile ("pxor   %xmm7,%xmm3");

		/* intermediate disks */
		for (d = l - 1; d > 0; --d) {
			asm volatile ("movdqa %0,%%xmm7" : : "m" (gfconst16.poly[0]));
			asm volatile ("movdqa %0,%%xmm4" : : "m" (v[d][i]));

			asm volatile ("pxor %xmm5,%xmm5");
			asm volatile ("pcmpgtb %xmm1,%xmm5");
			asm volatile ("paddb %xmm1,%xmm1");
			asm volatile ("pand %xmm7,%xmm5");
			asm volatile ("pxor %xmm5,%xmm1");

			asm volatile ("movdqa %0,%%xmm7" : : "m" (gfconst16.low4[0]));

			asm volatile ("pxor %xmm4,%xmm0");
			asm volatile ("pxor %xmm4,%xmm1");

			asm volatile ("movdqa %xmm4,%xmm5");
			asm volatile ("psrlw  $4,%xmm5");
			asm volatile ("pand   %xmm7,%xmm4");
			asm volatile ("pand   %xmm7,%xmm5");

			asm volatile ("movdqa %0,%%xmm6" : : "m" (gfgenpshufb[d][0][0][0]));
			asm volatile ("movdqa %0,%%xmm7" : : "m" (gfgenpshufb[d][0][1][0]));
			asm volatile ("pshufb %xmm4,%xmm6");
			asm volatile ("pshufb %xmm5,%xmm7");
			asm volatile ("pxor   %xmm6,%xmm2");
			asm volatile ("pxor   %xmm7,%xmm2");

			asm volatile ("movdqa %0,%%xmm6" : : "m" (gfgenpshufb[d][1][0][0]));
			asm volatile ("movdqa %0,%%xmm7" : : "m" (gfgenpshufb[d][1][1][0]));
			asm volatile ("pshufb %xmm4,%xmm6");
			asm volatile ("pshufb %xmm5,%xmm7");
			asm volatile ("pxor   %xmm6,%xmm3");
			asm volatile ("pxor   %xmm7,%xmm3");
		}

		/* first disk with all coefficients at 1 */
		asm volatile ("movdqa %0,%%xmm7" : : "m" (gfconst16.poly[0]));
		asm volatile ("movdqa %0,%%xmm4" : : "m" (v[0][i]));

		asm volatile ("pxor %xmm5,%xmm5");
		asm volatile ("pcmpgtb %xmm1,%xmm5");
		asm volatile ("paddb %xmm1,%xmm1");
		asm volatile ("pand %xmm7,%xmm5");
		asm volatile ("pxor %xmm5,%xmm1");

		asm volatile ("pxor %xmm4,%xmm0");
		asm volatile ("pxor %xmm4,%xmm1");
		asm volatile ("pxor %xmm4,%xmm2");
		asm volatile ("pxor %xmm4,%xmm3");

		asm volatile ("movntdq %%xmm0,%0" : "=m" (p[i]));
		asm volatile ("movntdq %%xmm1,%0" : "=m" (q[i]));
		asm volatile ("movntdq %%xmm2,%0" : "=m" (r[i]));
		asm volatile ("movntdq %%xmm3,%0" : "=m" (s[i]));
	}

	raid_sse_end();
}
#endif

#if defined(CONFIG_X86_64) && defined(CONFIG_SSSE3)
/*
 * GEN4 (quad parity with Cauchy matrix) SSSE3 implementation
 *
 * Note that it uses 16 registers, meaning that x64 is required.
 */
void raid_gen4_ssse3ext(int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *q;
	uint8_t *r;
	uint8_t *s;
	int d, l;
	size_t i;

	l = nd - 1;
	p = v[nd];
	q = v[nd + 1];
	r = v[nd + 2];
	s = v[nd + 3];

	/* special case with only one data disk */
	if (l == 0) {
		for (i = 0; i < 4; ++i)
			memcpy(v[1 + i], v[0], size);
		return;
	}

	raid_sse_begin();

	/* generic case with at least two data disks */
	for (i = 0; i < size; i += 32) {
		/* last disk without the by two multiplication */
		asm volatile ("movdqa %0,%%xmm15" : : "m" (gfconst16.low4[0]));
		asm volatile ("movdqa %0,%%xmm4" : : "m" (v[l][i]));
		asm volatile ("movdqa %0,%%xmm12" : : "m" (v[l][i + 16]));

		asm volatile ("movdqa %xmm4,%xmm0");
		asm volatile ("movdqa %xmm4,%xmm1");
		asm volatile ("movdqa %xmm12,%xmm8");
		asm volatile ("movdqa %xmm12,%xmm9");

		asm volatile ("movdqa %xmm4,%xmm5");
		asm volatile ("movdqa %xmm12,%xmm13");
		asm volatile ("psrlw  $4,%xmm5");
		asm volatile ("psrlw  $4,%xmm13");
		asm volatile ("pand   %xmm15,%xmm4");
		asm volatile ("pand   %xmm15,%xmm12");
		asm volatile ("pand   %xmm15,%xmm5");
		asm volatile ("pand   %xmm15,%xmm13");

		asm volatile ("movdqa %0,%%xmm2" : : "m" (gfgenpshufb[l][0][0][0]));
		asm volatile ("movdqa %0,%%xmm7" : : "m" (gfgenpshufb[l][0][1][0]));
		asm volatile ("movdqa %xmm2,%xmm10");
		asm volatile ("movdqa %xmm7,%xmm15");
		asm volatile ("pshufb %xmm4,%xmm2");
		asm volatile ("pshufb %xmm12,%xmm10");
		asm volatile ("pshufb %xmm5,%xmm7");
		asm volatile ("pshufb %xmm13,%xmm15");
		asm volatile ("pxor   %xmm7,%xmm2");
		asm volatile ("pxor   %xmm15,%xmm10");

		asm volatile ("movdqa %0,%%xmm3" : : "m" (gfgenpshufb[l][1][0][0]));
		asm volatile ("movdqa %0,%%xmm7" : : "m" (gfgenpshufb[l][1][1][0]));
		asm volatile ("movdqa %xmm3,%xmm11");
		asm volatile ("movdqa %xmm7,%xmm15");
		asm volatile ("pshufb %xmm4,%xmm3");
		asm volatile ("pshufb %xmm12,%xmm11");
		asm volatile ("pshufb %xmm5,%xmm7");
		asm volatile ("pshufb %xmm13,%xmm15");
		asm volatile ("pxor   %xmm7,%xmm3");
		asm volatile ("pxor   %xmm15,%xmm11");

		/* intermediate disks */
		for (d = l - 1; d > 0; --d) {
			asm volatile ("movdqa %0,%%xmm7" : : "m" (gfconst16.poly[0]));
			asm volatile ("movdqa %0,%%xmm15" : : "m" (gfconst16.low4[0]));
			asm volatile ("movdqa %0,%%xmm4" : : "m" (v[d][i]));
			asm volatile ("movdqa %0,%%xmm12" : : "m" (v[d][i + 16]));

			asm volatile ("pxor %xmm5,%xmm5");
			asm volatile ("pxor %xmm13,%xmm13");
			asm volatile ("pcmpgtb %xmm1,%xmm5");
			asm volatile ("pcmpgtb %xmm9,%xmm13");
			asm volatile ("paddb %xmm1,%xmm1");
			asm volatile ("paddb %xmm9,%xmm9");
			asm volatile ("pand %xmm7,%xmm5");
			asm volatile ("pand %xmm7,%xmm13");
			asm volatile ("pxor %xmm5,%xmm1");
			asm volatile ("pxor %xmm13,%xmm9");

			asm volatile ("pxor %xmm4,%xmm0");
			asm volatile ("pxor %xmm4,%xmm1");
			asm volatile ("pxor %xmm12,%xmm8");
			asm volatile ("pxor %xmm12,%xmm9");

			asm volatile ("movdqa %xmm4,%xmm5");
			asm volatile ("movdqa %xmm12,%xmm13");
			asm volatile ("psrlw  $4,%xmm5");
			asm volatile ("psrlw  $4,%xmm13");
			asm volatile ("pand   %xmm15,%xmm4");
			asm volatile ("pand   %xmm15,%xmm12");
			asm volatile ("pand   %xmm15,%xmm5");
			asm volatile ("pand   %xmm15,%xmm13");

			asm volatile ("movdqa %0,%%xmm6" : : "m" (gfgenpshufb[d][0][0][0]));
			asm volatile ("movdqa %0,%%xmm7" : : "m" (gfgenpshufb[d][0][1][0]));
			asm volatile ("movdqa %xmm6,%xmm14");
			asm volatile ("movdqa %xmm7,%xmm15");
			asm volatile ("pshufb %xmm4,%xmm6");
			asm volatile ("pshufb %xmm12,%xmm14");
			asm volatile ("pshufb %xmm5,%xmm7");
			asm volatile ("pshufb %xmm13,%xmm15");
			asm volatile ("pxor   %xmm6,%xmm2");
			asm volatile ("pxor   %xmm14,%xmm10");
			asm volatile ("pxor   %xmm7,%xmm2");
			asm volatile ("pxor   %xmm15,%xmm10");

			asm volatile ("movdqa %0,%%xmm6" : : "m" (gfgenpshufb[d][1][0][0]));
			asm volatile ("movdqa %0,%%xmm7" : : "m" (gfgenpshufb[d][1][1][0]));
			asm volatile ("movdqa %xmm6,%xmm14");
			asm volatile ("movdqa %xmm7,%xmm15");
			asm volatile ("pshufb %xmm4,%xmm6");
			asm volatile ("pshufb %xmm12,%xmm14");
			asm volatile ("pshufb %xmm5,%xmm7");
			asm volatile ("pshufb %xmm13,%xmm15");
			asm volatile ("pxor   %xmm6,%xmm3");
			asm volatile ("pxor   %xmm14,%xmm11");
			asm volatile ("pxor   %xmm7,%xmm3");
			asm volatile ("pxor   %xmm15,%xmm11");
		}

		/* first disk with all coefficients at 1 */
		asm volatile ("movdqa %0,%%xmm7" : : "m" (gfconst16.poly[0]));
		asm volatile ("movdqa %0,%%xmm15" : : "m" (gfconst16.low4[0]));
		asm volatile ("movdqa %0,%%xmm4" : : "m" (v[0][i]));
		asm volatile ("movdqa %0,%%xmm12" : : "m" (v[0][i + 16]));

		asm volatile ("pxor %xmm5,%xmm5");
		asm volatile ("pxor %xmm13,%xmm13");
		asm volatile ("pcmpgtb %xmm1,%xmm5");
		asm volatile ("pcmpgtb %xmm9,%xmm13");
		asm volatile ("paddb %xmm1,%xmm1");
		asm volatile ("paddb %xmm9,%xmm9");
		asm volatile ("pand %xmm7,%xmm5");
		asm volatile ("pand %xmm7,%xmm13");
		asm volatile ("pxor %xmm5,%xmm1");
		asm volatile ("pxor %xmm13,%xmm9");

		asm volatile ("pxor %xmm4,%xmm0");
		asm volatile ("pxor %xmm4,%xmm1");
		asm volatile ("pxor %xmm4,%xmm2");
		asm volatile ("pxor %xmm4,%xmm3");
		asm volatile ("pxor %xmm12,%xmm8");
		asm volatile ("pxor %xmm12,%xmm9");
		asm volatile ("pxor %xmm12,%xmm10");
		asm volatile ("pxor %xmm12,%xmm11");

		asm volatile ("movntdq %%xmm0,%0" : "=m" (p[i]));
		asm volatile ("movntdq %%xmm8,%0" : "=m" (p[i + 16]));
		asm volatile ("movntdq %%xmm1,%0" : "=m" (q[i]));
		asm volatile ("movntdq %%xmm9,%0" : "=m" (q[i + 16]));
		asm volatile ("movntdq %%xmm2,%0" : "=m" (r[i]));
		asm volatile ("movntdq %%xmm10,%0" : "=m" (r[i + 16]));
		asm volatile ("movntdq %%xmm3,%0" : "=m" (s[i]));
		asm volatile ("movntdq %%xmm11,%0" : "=m" (s[i + 16]));
	}

	raid_sse_end();
}
#endif

#if defined(CONFIG_X86_64) && defined(CONFIG_AVX2)
/*
 * GEN4 (quad parity with Cauchy matrix) AVX2 implementation
 *
 * Note that it uses 16 registers, meaning that x64 is required.
 */
void raid_gen4_avx2ext(int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *q;
	uint8_t *r;
	uint8_t *s;
	int d, l;
	size_t i;

	l = nd - 1;
	p = v[nd];
	q = v[nd + 1];
	r = v[nd + 2];
	s = v[nd + 3];

	/* special case with only one data disk */
	if (l == 0) {
		for (i = 0; i < 4; ++i)
			memcpy(v[1 + i], v[0], size);
		return;
	}

	raid_avx_begin();

	/* generic case with at least two data disks */
	for (i = 0; i < size; i += 64) {
		/* last disk without the by two multiplication */
		asm volatile ("vpbroadcastb %0,%%ymm15" : : "m" (gfconst16.low4[0]));
		asm volatile ("vmovdqa %0,%%ymm4" : : "m" (v[l][i]));
		asm volatile ("vmovdqa %0,%%ymm12" : : "m" (v[l][i + 32]));

		asm volatile ("vmovdqa %ymm4,%ymm0");
		asm volatile ("vmovdqa %ymm4,%ymm1");
		asm volatile ("vmovdqa %ymm12,%ymm8");
		asm volatile ("vmovdqa %ymm12,%ymm9");

		asm volatile ("vpsrlw  $4,%ymm4,%ymm5");
		asm volatile ("vpsrlw  $4,%ymm12,%ymm13");
		asm volatile ("vpand   %ymm15,%ymm4,%ymm4");
		asm volatile ("vpand   %ymm15,%ymm12,%ymm12");
		asm volatile ("vpand   %ymm15,%ymm5,%ymm5");
		asm volatile ("vpand   %ymm15,%ymm13,%ymm13");

		asm volatile ("vbroadcasti128 %0,%%ymm10" : : "m" (gfgenpshufb[l][0][0][0]));
		asm volatile ("vbroadcasti128 %0,%%ymm15" : : "m" (gfgenpshufb[l][0][1][0]));
		asm volatile ("vpshufb %ymm4,%ymm10,%ymm2");
		asm volatile ("vpshufb %ymm5,%ymm15,%ymm7");
		asm volatile ("vpshufb %ymm12,%ymm10,%ymm10");
		asm volatile ("vpshufb %ymm13,%ymm15,%ymm15");
		asm volatile ("vpxor   %ymm7,%ymm2,%ymm2");
		asm volatile ("vpxor   %ymm15,%ymm10,%ymm10");

		asm volatile ("vbroadcasti128 %0,%%ymm11" : : "m" (gfgenpshufb[l][1][0][0]));
		asm volatile ("vbroadcasti128 %0,%%ymm15" : : "m" (gfgenpshufb[l][1][1][0]));
		asm volatile ("vpshufb %ymm4,%ymm11,%ymm3");
		asm volatile ("vpshufb %ymm5,%ymm15,%ymm7");
		asm volatile ("vpshufb %ymm12,%ymm11,%ymm11");
		asm volatile ("vpshufb %ymm13,%ymm15,%ymm15");
		asm volatile ("vpxor   %ymm7,%ymm3,%ymm3");
		asm volatile ("vpxor   %ymm15,%ymm11,%ymm11");

		/* intermediate disks */
		for (d = l - 1; d > 0; --d) {
			asm volatile ("vpbroadcastb %0,%%ymm7" : : "m" (gfconst16.poly[0]));
			asm volatile ("vpbroadcastb %0,%%ymm15" : : "m" (gfconst16.low4[0]));
			asm volatile ("vmovdqa %0,%%ymm4" : : "m" (v[d][i]));
			asm volatile ("vmovdqa %0,%%ymm12" : : "m" (v[d][i + 32]));

			asm volatile ("vpxor %ymm5,%ymm5,%ymm5");
			asm volatile ("vpxor %ymm13,%ymm13,%ymm13");
			asm volatile ("vpcmpgtb %ymm1,%ymm5,%ymm5");
			asm volatile ("vpcmpgtb %ymm9,%ymm13,%ymm13");
			asm volatile ("vpaddb %ymm1,%ymm1,%ymm1");
			asm volatile ("vpaddb %ymm9,%ymm9,%ymm9");
			asm volatile ("vpand %ymm7,%ymm5,%ymm5");
			asm volatile ("vpand %ymm7,%ymm13,%ymm13");
			asm volatile ("vpxor %ymm5,%ymm1,%ymm1");
			asm volatile ("vpxor %ymm13,%ymm9,%ymm9");

			asm volatile ("vpxor %ymm4,%ymm0,%ymm0");
			asm volatile ("vpxor %ymm4,%ymm1,%ymm1");
			asm volatile ("vpxor %ymm12,%ymm8,%ymm8");
			asm volatile ("vpxor %ymm12,%ymm9,%ymm9");

			asm volatile ("vpsrlw  $4,%ymm4,%ymm5");
			asm volatile ("vpsrlw  $4,%ymm12,%ymm13");
			asm volatile ("vpand   %ymm15,%ymm4,%ymm4");
			asm volatile ("vpand   %ymm15,%ymm12,%ymm12");
			asm volatile ("vpand   %ymm15,%ymm5,%ymm5");
			asm volatile ("vpand   %ymm15,%ymm13,%ymm13");

			asm volatile ("vbroadcasti128 %0,%%ymm14" : : "m" (gfgenpshufb[d][0][0][0]));
			asm volatile ("vbroadcasti128 %0,%%ymm15" : : "m" (gfgenpshufb[d][0][1][0]));
			asm volatile ("vpshufb %ymm4,%ymm14,%ymm6");
			asm volatile ("vpshufb %ymm5,%ymm15,%ymm7");
			asm volatile ("vpshufb %ymm12,%ymm14,%ymm14");
			asm volatile ("vpshufb %ymm13,%ymm15,%ymm15");
			asm volatile ("vpxor   %ymm6,%ymm2,%ymm2");
			asm volatile ("vpxor   %ymm14,%ymm10,%ymm10");
			asm volatile ("vpxor   %ymm7,%ymm2,%ymm2");
			asm volatile ("vpxor   %ymm15,%ymm10,%ymm10");

			asm volatile ("vbroadcasti128 %0,%%ymm14" : : "m" (gfgenpshufb[d][1][0][0]));
			asm volatile ("vbroadcasti128 %0,%%ymm15" : : "m" (gfgenpshufb[d][1][1][0]));
			asm volatile ("vpshufb %ymm4,%ymm14,%ymm6");
			asm volatile ("vpshufb %ymm5,%ymm15,%ymm7");
			asm volatile ("vpshufb %ymm12,%ymm14,%ymm14");
			asm volatile ("vpshufb %ymm13,%ymm15,%ymm15");
			asm volatile ("vpxor   %ymm6,%ymm3,%ymm3");
			asm volatile ("vpxor   %ymm14,%ymm11,%ymm11");
			asm volatile ("vpxor   %ymm7,%ymm3,%ymm3");
			asm volatile ("vpxor   %ymm15,%ymm11,%ymm11");
		}

		/* first disk with all coefficients at 1 */
		asm volatile ("vpbroadcastb %0,%%ymm7" : : "m" (gfconst16.poly[0]));
		asm volatile ("vpbroadcastb %0,%%ymm15" : : "m" (gfconst16.low4[0]));
		asm volatile ("vmovdqa %0,%%ymm4" : : "m" (v[0][i]));
		asm volatile ("vmovdqa %0,%%ymm12" : : "m" (v[0][i + 32]));

		asm volatile ("vpxor %ymm5,%ymm5,%ymm5");
		asm volatile ("vpxor %ymm13,%ymm13,%ymm13");
		asm volatile ("vpcmpgtb %ymm1,%ymm5,%ymm5");
		asm volatile ("vpcmpgtb %ymm9,%ymm13,%ymm13");
		asm volatile ("vpaddb %ymm1,%ymm1,%ymm1");
		asm volatile ("vpaddb %ymm9,%ymm9,%ymm9");
		asm volatile ("vpand %ymm7,%ymm5,%ymm5");
		asm volatile ("vpand %ymm7,%ymm13,%ymm13");
		asm volatile ("vpxor %ymm5,%ymm1,%ymm1");
		asm volatile ("vpxor %ymm13,%ymm9,%ymm9");

		asm volatile ("vpxor %ymm4,%ymm0,%ymm0");
		asm volatile ("vpxor %ymm4,%ymm1,%ymm1");
		asm volatile ("vpxor %ymm4,%ymm2,%ymm2");
		asm volatile ("vpxor %ymm4,%ymm3,%ymm3");
		asm volatile ("vpxor %ymm12,%ymm8,%ymm8");
		asm volatile ("vpxor %ymm12,%ymm9,%ymm9");
		asm volatile ("vpxor %ymm12,%ymm10,%ymm10");
		asm volatile ("vpxor %ymm12,%ymm11,%ymm11");

		asm volatile ("vmovntdq %%ymm0,%0" : "=m" (p[i]));
		asm volatile ("vmovntdq %%ymm8,%0" : "=m" (p[i + 32]));
		asm volatile ("vmovntdq %%ymm1,%0" : "=m" (q[i]));
		asm volatile ("vmovntdq %%ymm9,%0" : "=m" (q[i + 32]));
		asm volatile ("vmovntdq %%ymm2,%0" : "=m" (r[i]));
		asm volatile ("vmovntdq %%ymm10,%0" : "=m" (r[i + 32]));
		asm volatile ("vmovntdq %%ymm3,%0" : "=m" (s[i]));
		asm volatile ("vmovntdq %%ymm11,%0" : "=m" (s[i + 32]));
	}

	raid_avx_end();
}
#endif

#if defined(CONFIG_X86_64) && defined(CONFIG_AVX512BW)
/*
 * GEN4 (quad parity with Cauchy matrix) AVX512BW implementation
 */
void raid_gen4_avx512bw(int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *q;
	uint8_t *r;
	uint8_t *s;
	int d, l;
	size_t i;

	l = nd - 1;
	p = v[nd];
	q = v[nd + 1];
	r = v[nd + 2];
	s = v[nd + 3];

	/* special case with only one data disk */
	if (l == 0) {
		for (i = 0; i < 4; ++i)
			memcpy(v[1 + i], v[0], size);
		return;
	}

	raid_avx_begin();

	/* generic case with at least two data disks */
	asm volatile ("vpxorq %zmm6,%zmm6,%zmm6");
	asm volatile ("vpbroadcastb %0,%%zmm14" : : "m" (gfconst16.poly[0]));
	asm volatile ("vpbroadcastb %0,%%zmm15" : : "m" (gfconst16.low4[0]));

	for (i = 0; i < size; i += 64) {
		/* last disk */
		asm volatile ("vmovdqa64 %0,%%zmm10" : : "m" (v[l][i]));

		asm volatile ("vmovdqa64 %zmm10,%zmm0");

		asm volatile ("vmovdqa64 %zmm10,%zmm1");
		asm volatile ("vpcmpgtb  %zmm1,%zmm6,%k1");
		asm volatile ("vpmovm2b  %k1,%zmm11");
		asm volatile ("vpaddb    %zmm1,%zmm1,%zmm1");
		asm volatile ("vpandq    %zmm14,%zmm11,%zmm11");
		asm volatile ("vpxorq    %zmm11,%zmm1,%zmm1");

		asm volatile ("vpsrlw    $4,%zmm10,%zmm11");
		asm volatile ("vpandq    %zmm15,%zmm10,%zmm10");
		asm volatile ("vpandq    %zmm15,%zmm11,%zmm11");

		asm volatile ("vbroadcasti32x4 %0,%%zmm2" : : "m" (gfgenpshufb[l][0][0][0]));
		asm volatile ("vbroadcasti32x4 %0,%%zmm13" : : "m" (gfgenpshufb[l][0][1][0]));
		asm volatile ("vpshufb   %zmm10,%zmm2,%zmm2");
		asm volatile ("vpshufb   %zmm11,%zmm13,%zmm13");
		asm volatile ("vpxorq    %zmm13,%zmm2,%zmm2");

		asm volatile ("vbroadcasti32x4 %0,%%zmm3" : : "m" (gfgenpshufb[l][1][0][0]));
		asm volatile ("vbroadcasti32x4 %0,%%zmm13" : : "m" (gfgenpshufb[l][1][1][0]));
		asm volatile ("vpshufb   %zmm10,%zmm3,%zmm3");
		asm volatile ("vpshufb   %zmm11,%zmm13,%zmm13");
		asm volatile ("vpxorq    %zmm13,%zmm3,%zmm3");

		/* intermediate disks */
		for (d = l - 1; d > 0; --d) {
			asm volatile ("vmovdqa64 %0,%%zmm10" : : "m" (v[d][i]));

			asm volatile ("vpxorq    %zmm10,%zmm0,%zmm0");

			asm volatile ("vpxorq    %zmm10,%zmm1,%zmm1");
			asm volatile ("vpcmpgtb  %zmm1,%zmm6,%k1");
			asm volatile ("vpmovm2b  %k1,%zmm11");
			asm volatile ("vpaddb    %zmm1,%zmm1,%zmm1");
			asm volatile ("vpandq    %zmm14,%zmm11,%zmm11");
			asm volatile ("vpxorq    %zmm11,%zmm1,%zmm1");

			asm volatile ("vpsrlw    $4,%zmm10,%zmm11");
			asm volatile ("vpandq    %zmm15,%zmm10,%zmm10");
			asm volatile ("vpandq    %zmm15,%zmm11,%zmm11");

			asm volatile ("vbroadcasti32x4 %0,%%zmm12" : : "m" (gfgenpshufb[d][0][0][0]));
			asm volatile ("vbroadcasti32x4 %0,%%zmm13" : : "m" (gfgenpshufb[d][0][1][0]));
			asm volatile ("vpshufb   %zmm10,%zmm12,%zmm12");
			asm volatile ("vpshufb   %zmm11,%zmm13,%zmm13");
			asm volatile ("vpxorq    %zmm12,%zmm2,%zmm2");
			asm volatile ("vpxorq    %zmm13,%zmm2,%zmm2");

			asm volatile ("vbroadcasti32x4 %0,%%zmm12" : : "m" (gfgenpshufb[d][1][0][0]));
			asm volatile ("vbroadcasti32x4 %0,%%zmm13" : : "m" (gfgenpshufb[d][1][1][0]));
			asm volatile ("vpshufb   %zmm10,%zmm12,%zmm12");
			asm volatile ("vpshufb   %zmm11,%zmm13,%zmm13");
			asm volatile ("vpxorq    %zmm12,%zmm3,%zmm3");
			asm volatile ("vpxorq    %zmm13,%zmm3,%zmm3");
		}

		/* first disk with all coefficients at 1 */
		asm volatile ("vmovdqa64 %0,%%zmm10" : : "m" (v[0][i]));

		asm volatile ("vpxorq    %zmm10,%zmm0,%zmm0");
		asm volatile ("vpxorq    %zmm10,%zmm1,%zmm1");
		asm volatile ("vpxorq    %zmm10,%zmm2,%zmm2");
		asm volatile ("vpxorq    %zmm10,%zmm3,%zmm3");

		asm volatile ("vmovntdq  %%zmm0,%0" : "=m" (p[i]));
		asm volatile ("vmovntdq  %%zmm1,%0" : "=m" (q[i]));
		asm volatile ("vmovntdq  %%zmm2,%0" : "=m" (r[i]));
		asm volatile ("vmovntdq  %%zmm3,%0" : "=m" (s[i]));
	}

	raid_avx_end();
}
#endif

#if defined(CONFIG_X86) && defined(CONFIG_SSSE3)
/*
 * GEN5 (penta parity with Cauchy matrix) SSSE3 implementation
 */
void raid_gen5_ssse3(int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *q;
	uint8_t *r;
	uint8_t *s;
	uint8_t *t;
	int d, l;
	size_t i;
	uint8_t buffer[16+16];
	uint8_t *pd = __align_ptr(buffer, 16);

	l = nd - 1;
	p = v[nd];
	q = v[nd + 1];
	r = v[nd + 2];
	s = v[nd + 3];
	t = v[nd + 4];

	/* special case with only one data disk */
	if (l == 0) {
		for (i = 0; i < 5; ++i)
			memcpy(v[1 + i], v[0], size);
		return;
	}

	raid_sse_begin();

	/* generic case with at least two data disks */
	for (i = 0; i < size; i += 16) {
		/* last disk without the by two multiplication */
		asm volatile ("movdqa %0,%%xmm4" : : "m" (v[l][i]));

		asm volatile ("movdqa %xmm4,%xmm0");
		asm volatile ("movdqa %%xmm4,%0" : "=m" (pd[0]));

		asm volatile ("movdqa %0,%%xmm7" : : "m" (gfconst16.low4[0]));
		asm volatile ("movdqa %xmm4,%xmm5");
		asm volatile ("psrlw  $4,%xmm5");
		asm volatile ("pand   %xmm7,%xmm4");
		asm volatile ("pand   %xmm7,%xmm5");

		asm volatile ("movdqa %0,%%xmm1" : : "m" (gfgenpshufb[l][0][0][0]));
		asm volatile ("movdqa %0,%%xmm7" : : "m" (gfgenpshufb[l][0][1][0]));
		asm volatile ("pshufb %xmm4,%xmm1");
		asm volatile ("pshufb %xmm5,%xmm7");
		asm volatile ("pxor   %xmm7,%xmm1");

		asm volatile ("movdqa %0,%%xmm2" : : "m" (gfgenpshufb[l][1][0][0]));
		asm volatile ("movdqa %0,%%xmm7" : : "m" (gfgenpshufb[l][1][1][0]));
		asm volatile ("pshufb %xmm4,%xmm2");
		asm volatile ("pshufb %xmm5,%xmm7");
		asm volatile ("pxor   %xmm7,%xmm2");

		asm volatile ("movdqa %0,%%xmm3" : : "m" (gfgenpshufb[l][2][0][0]));
		asm volatile ("movdqa %0,%%xmm7" : : "m" (gfgenpshufb[l][2][1][0]));
		asm volatile ("pshufb %xmm4,%xmm3");
		asm volatile ("pshufb %xmm5,%xmm7");
		asm volatile ("pxor   %xmm7,%xmm3");

		/* intermediate disks */
		for (d = l - 1; d > 0; --d) {
			asm volatile ("movdqa %0,%%xmm4" : : "m" (v[d][i]));
			asm volatile ("movdqa %0,%%xmm6" : : "m" (pd[0]));
			asm volatile ("movdqa %0,%%xmm7" : : "m" (gfconst16.poly[0]));

			asm volatile ("pxor %xmm5,%xmm5");
			asm volatile ("pcmpgtb %xmm0,%xmm5");
			asm volatile ("paddb %xmm0,%xmm0");
			asm volatile ("pand %xmm7,%xmm5");
			asm volatile ("pxor %xmm5,%xmm0");

			asm volatile ("pxor %xmm4,%xmm0");
			asm volatile ("pxor %xmm4,%xmm6");
			asm volatile ("movdqa %%xmm6,%0" : "=m" (pd[0]));

			asm volatile ("movdqa %0,%%xmm7" : : "m" (gfconst16.low4[0]));
			asm volatile ("movdqa %xmm4,%xmm5");
			asm volatile ("psrlw  $4,%xmm5");
			asm volatile ("pand   %xmm7,%xmm4");
			asm volatile ("pand   %xmm7,%xmm5");

			asm volatile ("movdqa %0,%%xmm6" : : "m" (gfgenpshufb[d][0][0][0]));
			asm volatile ("movdqa %0,%%xmm7" : : "m" (gfgenpshufb[d][0][1][0]));
			asm volatile ("pshufb %xmm4,%xmm6");
			asm volatile ("pshufb %xmm5,%xmm7");
			asm volatile ("pxor   %xmm6,%xmm1");
			asm volatile ("pxor   %xmm7,%xmm1");

			asm volatile ("movdqa %0,%%xmm6" : : "m" (gfgenpshufb[d][1][0][0]));
			asm volatile ("movdqa %0,%%xmm7" : : "m" (gfgenpshufb[d][1][1][0]));
			asm volatile ("pshufb %xmm4,%xmm6");
			asm volatile ("pshufb %xmm5,%xmm7");
			asm volatile ("pxor   %xmm6,%xmm2");
			asm volatile ("pxor   %xmm7,%xmm2");

			asm volatile ("movdqa %0,%%xmm6" : : "m" (gfgenpshufb[d][2][0][0]));
			asm volatile ("movdqa %0,%%xmm7" : : "m" (gfgenpshufb[d][2][1][0]));
			asm volatile ("pshufb %xmm4,%xmm6");
			asm volatile ("pshufb %xmm5,%xmm7");
			asm volatile ("pxor   %xmm6,%xmm3");
			asm volatile ("pxor   %xmm7,%xmm3");
		}

		/* first disk with all coefficients at 1 */
		asm volatile ("movdqa %0,%%xmm4" : : "m" (v[0][i]));
		asm volatile ("movdqa %0,%%xmm6" : : "m" (pd[0]));
		asm volatile ("movdqa %0,%%xmm7" : : "m" (gfconst16.poly[0]));

		asm volatile ("pxor %xmm5,%xmm5");
		asm volatile ("pcmpgtb %xmm0,%xmm5");
		asm volatile ("paddb %xmm0,%xmm0");
		asm volatile ("pand %xmm7,%xmm5");
		asm volatile ("pxor %xmm5,%xmm0");

		asm volatile ("pxor %xmm4,%xmm0");
		asm volatile ("pxor %xmm4,%xmm1");
		asm volatile ("pxor %xmm4,%xmm2");
		asm volatile ("pxor %xmm4,%xmm3");
		asm volatile ("pxor %xmm4,%xmm6");

		asm volatile ("movntdq %%xmm6,%0" : "=m" (p[i]));
		asm volatile ("movntdq %%xmm0,%0" : "=m" (q[i]));
		asm volatile ("movntdq %%xmm1,%0" : "=m" (r[i]));
		asm volatile ("movntdq %%xmm2,%0" : "=m" (s[i]));
		asm volatile ("movntdq %%xmm3,%0" : "=m" (t[i]));
	}

	raid_sse_end();
}
#endif

#if defined(CONFIG_X86_64) && defined(CONFIG_SSSE3)
/*
 * GEN5 (penta parity with Cauchy matrix) SSSE3 implementation
 *
 * Note that it uses 16 registers, meaning that x64 is required.
 */
void raid_gen5_ssse3ext(int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *q;
	uint8_t *r;
	uint8_t *s;
	uint8_t *t;
	int d, l;
	size_t i;

	l = nd - 1;
	p = v[nd];
	q = v[nd + 1];
	r = v[nd + 2];
	s = v[nd + 3];
	t = v[nd + 4];

	/* special case with only one data disk */
	if (l == 0) {
		for (i = 0; i < 5; ++i)
			memcpy(v[1 + i], v[0], size);
		return;
	}

	raid_sse_begin();

	/* generic case with at least two data disks */
	asm volatile ("movdqa %0,%%xmm14" : : "m" (gfconst16.poly[0]));
	asm volatile ("movdqa %0,%%xmm15" : : "m" (gfconst16.low4[0]));

	for (i = 0; i < size; i += 16) {
		/* last disk without the by two multiplication */
		asm volatile ("movdqa %0,%%xmm10" : : "m" (v[l][i]));

		asm volatile ("movdqa %xmm10,%xmm0");
		asm volatile ("movdqa %xmm10,%xmm1");

		asm volatile ("movdqa %xmm10,%xmm11");
		asm volatile ("psrlw  $4,%xmm11");
		asm volatile ("pand   %xmm15,%xmm10");
		asm volatile ("pand   %xmm15,%xmm11");

		asm volatile ("movdqa %0,%%xmm2" : : "m" (gfgenpshufb[l][0][0][0]));
		asm volatile ("movdqa %0,%%xmm13" : : "m" (gfgenpshufb[l][0][1][0]));
		asm volatile ("pshufb %xmm10,%xmm2");
		asm volatile ("pshufb %xmm11,%xmm13");
		asm volatile ("pxor   %xmm13,%xmm2");

		asm volatile ("movdqa %0,%%xmm3" : : "m" (gfgenpshufb[l][1][0][0]));
		asm volatile ("movdqa %0,%%xmm13" : : "m" (gfgenpshufb[l][1][1][0]));
		asm volatile ("pshufb %xmm10,%xmm3");
		asm volatile ("pshufb %xmm11,%xmm13");
		asm volatile ("pxor   %xmm13,%xmm3");

		asm volatile ("movdqa %0,%%xmm4" : : "m" (gfgenpshufb[l][2][0][0]));
		asm volatile ("movdqa %0,%%xmm13" : : "m" (gfgenpshufb[l][2][1][0]));
		asm volatile ("pshufb %xmm10,%xmm4");
		asm volatile ("pshufb %xmm11,%xmm13");
		asm volatile ("pxor   %xmm13,%xmm4");

		/* intermediate disks */
		for (d = l - 1; d > 0; --d) {
			asm volatile ("movdqa %0,%%xmm10" : : "m" (v[d][i]));

			asm volatile ("pxor %xmm11,%xmm11");
			asm volatile ("pcmpgtb %xmm1,%xmm11");
			asm volatile ("paddb %xmm1,%xmm1");
			asm volatile ("pand %xmm14,%xmm11");
			asm volatile ("pxor %xmm11,%xmm1");

			asm volatile ("pxor %xmm10,%xmm0");
			asm volatile ("pxor %xmm10,%xmm1");

			asm volatile ("movdqa %xmm10,%xmm11");
			asm volatile ("psrlw  $4,%xmm11");
			asm volatile ("pand   %xmm15,%xmm10");
			asm volatile ("pand   %xmm15,%xmm11");

			asm volatile ("movdqa %0,%%xmm12" : : "m" (gfgenpshufb[d][0][0][0]));
			asm volatile ("movdqa %0,%%xmm13" : : "m" (gfgenpshufb[d][0][1][0]));
			asm volatile ("pshufb %xmm10,%xmm12");
			asm volatile ("pshufb %xmm11,%xmm13");
			asm volatile ("pxor   %xmm12,%xmm2");
			asm volatile ("pxor   %xmm13,%xmm2");

			asm volatile ("movdqa %0,%%xmm12" : : "m" (gfgenpshufb[d][1][0][0]));
			asm volatile ("movdqa %0,%%xmm13" : : "m" (gfgenpshufb[d][1][1][0]));
			asm volatile ("pshufb %xmm10,%xmm12");
			asm volatile ("pshufb %xmm11,%xmm13");
			asm volatile ("pxor   %xmm12,%xmm3");
			asm volatile ("pxor   %xmm13,%xmm3");

			asm volatile ("movdqa %0,%%xmm12" : : "m" (gfgenpshufb[d][2][0][0]));
			asm volatile ("movdqa %0,%%xmm13" : : "m" (gfgenpshufb[d][2][1][0]));
			asm volatile ("pshufb %xmm10,%xmm12");
			asm volatile ("pshufb %xmm11,%xmm13");
			asm volatile ("pxor   %xmm12,%xmm4");
			asm volatile ("pxor   %xmm13,%xmm4");
		}

		/* first disk with all coefficients at 1 */
		asm volatile ("movdqa %0,%%xmm10" : : "m" (v[0][i]));

		asm volatile ("pxor %xmm11,%xmm11");
		asm volatile ("pcmpgtb %xmm1,%xmm11");
		asm volatile ("paddb %xmm1,%xmm1");
		asm volatile ("pand %xmm14,%xmm11");
		asm volatile ("pxor %xmm11,%xmm1");

		asm volatile ("pxor %xmm10,%xmm0");
		asm volatile ("pxor %xmm10,%xmm1");
		asm volatile ("pxor %xmm10,%xmm2");
		asm volatile ("pxor %xmm10,%xmm3");
		asm volatile ("pxor %xmm10,%xmm4");

		asm volatile ("movntdq %%xmm0,%0" : "=m" (p[i]));
		asm volatile ("movntdq %%xmm1,%0" : "=m" (q[i]));
		asm volatile ("movntdq %%xmm2,%0" : "=m" (r[i]));
		asm volatile ("movntdq %%xmm3,%0" : "=m" (s[i]));
		asm volatile ("movntdq %%xmm4,%0" : "=m" (t[i]));
	}

	raid_sse_end();
}
#endif

#if defined(CONFIG_X86_64) && defined(CONFIG_AVX2)
/*
 * GEN5 (penta parity with Cauchy matrix) AVX2 implementation
 *
 * Note that it uses 16 registers, meaning that x64 is required.
 */
void raid_gen5_avx2ext(int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *q;
	uint8_t *r;
	uint8_t *s;
	uint8_t *t;
	int d, l;
	size_t i;

	l = nd - 1;
	p = v[nd];
	q = v[nd + 1];
	r = v[nd + 2];
	s = v[nd + 3];
	t = v[nd + 4];

	/* special case with only one data disk */
	if (l == 0) {
		for (i = 0; i < 5; ++i)
			memcpy(v[1 + i], v[0], size);
		return;
	}

	raid_avx_begin();

	/* generic case with at least two data disks */
	asm volatile ("vpxor %ymm6,%ymm6,%ymm6");
	asm volatile ("vpbroadcastb %0,%%ymm14" : : "m" (gfconst16.poly[0]));
	asm volatile ("vpbroadcastb %0,%%ymm15" : : "m" (gfconst16.low4[0]));

	for (i = 0; i < size; i += 32) {
		/* last disk without the by two multiplication */
		asm volatile ("vmovdqa %0,%%ymm10" : : "m" (v[l][i]));

		asm volatile ("vmovdqa %ymm10,%ymm0");
		asm volatile ("vmovdqa %ymm10,%ymm1");

		asm volatile ("vpsrlw  $4,%ymm10,%ymm11");
		asm volatile ("vpand   %ymm15,%ymm10,%ymm10");
		asm volatile ("vpand   %ymm15,%ymm11,%ymm11");

		asm volatile ("vbroadcasti128 %0,%%ymm2" : : "m" (gfgenpshufb[l][0][0][0]));
		asm volatile ("vbroadcasti128 %0,%%ymm13" : : "m" (gfgenpshufb[l][0][1][0]));
		asm volatile ("vpshufb %ymm10,%ymm2,%ymm2");
		asm volatile ("vpshufb %ymm11,%ymm13,%ymm13");
		asm volatile ("vpxor   %ymm13,%ymm2,%ymm2");

		asm volatile ("vbroadcasti128 %0,%%ymm3" : : "m" (gfgenpshufb[l][1][0][0]));
		asm volatile ("vbroadcasti128 %0,%%ymm13" : : "m" (gfgenpshufb[l][1][1][0]));
		asm volatile ("vpshufb %ymm10,%ymm3,%ymm3");
		asm volatile ("vpshufb %ymm11,%ymm13,%ymm13");
		asm volatile ("vpxor   %ymm13,%ymm3,%ymm3");

		asm volatile ("vbroadcasti128 %0,%%ymm4" : : "m" (gfgenpshufb[l][2][0][0]));
		asm volatile ("vbroadcasti128 %0,%%ymm13" : : "m" (gfgenpshufb[l][2][1][0]));
		asm volatile ("vpshufb %ymm10,%ymm4,%ymm4");
		asm volatile ("vpshufb %ymm11,%ymm13,%ymm13");
		asm volatile ("vpxor   %ymm13,%ymm4,%ymm4");

		/* intermediate disks */
		for (d = l - 1; d > 0; --d) {
			asm volatile ("vmovdqa %0,%%ymm10" : : "m" (v[d][i]));

			asm volatile ("vpcmpgtb %ymm1,%ymm6,%ymm11");
			asm volatile ("vpaddb %ymm1,%ymm1,%ymm1");
			asm volatile ("vpand %ymm14,%ymm11,%ymm11");
			asm volatile ("vpxor %ymm11,%ymm1,%ymm1");

			asm volatile ("vpxor %ymm10,%ymm0,%ymm0");
			asm volatile ("vpxor %ymm10,%ymm1,%ymm1");

			asm volatile ("vpsrlw  $4,%ymm10,%ymm11");
			asm volatile ("vpand   %ymm15,%ymm10,%ymm10");
			asm volatile ("vpand   %ymm15,%ymm11,%ymm11");

			asm volatile ("vbroadcasti128 %0,%%ymm12" : : "m" (gfgenpshufb[d][0][0][0]));
			asm volatile ("vbroadcasti128 %0,%%ymm13" : : "m" (gfgenpshufb[d][0][1][0]));
			asm volatile ("vpshufb %ymm10,%ymm12,%ymm12");
			asm volatile ("vpshufb %ymm11,%ymm13,%ymm13");
			asm volatile ("vpxor   %ymm12,%ymm2,%ymm2");
			asm volatile ("vpxor   %ymm13,%ymm2,%ymm2");

			asm volatile ("vbroadcasti128 %0,%%ymm12" : : "m" (gfgenpshufb[d][1][0][0]));
			asm volatile ("vbroadcasti128 %0,%%ymm13" : : "m" (gfgenpshufb[d][1][1][0]));
			asm volatile ("vpshufb %ymm10,%ymm12,%ymm12");
			asm volatile ("vpshufb %ymm11,%ymm13,%ymm13");
			asm volatile ("vpxor   %ymm12,%ymm3,%ymm3");
			asm volatile ("vpxor   %ymm13,%ymm3,%ymm3");

			asm volatile ("vbroadcasti128 %0,%%ymm12" : : "m" (gfgenpshufb[d][2][0][0]));
			asm volatile ("vbroadcasti128 %0,%%ymm13" : : "m" (gfgenpshufb[d][2][1][0]));
			asm volatile ("vpshufb %ymm10,%ymm12,%ymm12");
			asm volatile ("vpshufb %ymm11,%ymm13,%ymm13");
			asm volatile ("vpxor   %ymm12,%ymm4,%ymm4");
			asm volatile ("vpxor   %ymm13,%ymm4,%ymm4");
		}

		/* first disk with all coefficients at 1 */
		asm volatile ("vmovdqa %0,%%ymm10" : : "m" (v[0][i]));

		asm volatile ("vpcmpgtb %ymm1,%ymm6,%ymm11");
		asm volatile ("vpaddb %ymm1,%ymm1,%ymm1");
		asm volatile ("vpand %ymm14,%ymm11,%ymm11");
		asm volatile ("vpxor %ymm11,%ymm1,%ymm1");

		asm volatile ("vpxor %ymm10,%ymm0,%ymm0");
		asm volatile ("vpxor %ymm10,%ymm1,%ymm1");
		asm volatile ("vpxor %ymm10,%ymm2,%ymm2");
		asm volatile ("vpxor %ymm10,%ymm3,%ymm3");
		asm volatile ("vpxor %ymm10,%ymm4,%ymm4");

		asm volatile ("vmovntdq %%ymm0,%0" : "=m" (p[i]));
		asm volatile ("vmovntdq %%ymm1,%0" : "=m" (q[i]));
		asm volatile ("vmovntdq %%ymm2,%0" : "=m" (r[i]));
		asm volatile ("vmovntdq %%ymm3,%0" : "=m" (s[i]));
		asm volatile ("vmovntdq %%ymm4,%0" : "=m" (t[i]));
	}

	raid_avx_end();
}
#endif

#if defined(CONFIG_X86_64) && defined(CONFIG_AVX512BW)
/*
 * GEN5 (penta parity with Cauchy matrix) AVX512BW implementation
 */
void raid_gen5_avx512bw(int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *q;
	uint8_t *r;
	uint8_t *s;
	uint8_t *t;
	int d, l;
	size_t i;

	l = nd - 1;
	p = v[nd];
	q = v[nd + 1];
	r = v[nd + 2];
	s = v[nd + 3];
	t = v[nd + 4];

	/* special case with only one data disk */
	if (l == 0) {
		for (i = 0; i < 5; ++i)
			memcpy(v[1 + i], v[0], size);
		return;
	}

	raid_avx_begin();

	/* generic case with at least two data disks */
	asm volatile ("vpxorq %zmm6,%zmm6,%zmm6");
	asm volatile ("vpbroadcastb %0,%%zmm14" : : "m" (gfconst16.poly[0]));
	asm volatile ("vpbroadcastb %0,%%zmm15" : : "m" (gfconst16.low4[0]));

	for (i = 0; i < size; i += 64) {
		/* last disk */
		asm volatile ("vmovdqa64 %0,%%zmm10" : : "m" (v[l][i]));

		asm volatile ("vmovdqa64 %zmm10,%zmm0");

		asm volatile ("vmovdqa64 %zmm10,%zmm1");
		asm volatile ("vpcmpgtb  %zmm1,%zmm6,%k1");
		asm volatile ("vpmovm2b  %k1,%zmm11");
		asm volatile ("vpaddb    %zmm1,%zmm1,%zmm1");
		asm volatile ("vpandq    %zmm14,%zmm11,%zmm11");
		asm volatile ("vpxorq    %zmm11,%zmm1,%zmm1");

		asm volatile ("vpsrlw    $4,%zmm10,%zmm11");
		asm volatile ("vpandq    %zmm15,%zmm10,%zmm10");
		asm volatile ("vpandq    %zmm15,%zmm11,%zmm11");

		asm volatile ("vbroadcasti32x4 %0,%%zmm2" : : "m" (gfgenpshufb[l][0][0][0]));
		asm volatile ("vbroadcasti32x4 %0,%%zmm13" : : "m" (gfgenpshufb[l][0][1][0]));
		asm volatile ("vpshufb   %zmm10,%zmm2,%zmm2");
		asm volatile ("vpshufb   %zmm11,%zmm13,%zmm13");
		asm volatile ("vpxorq    %zmm13,%zmm2,%zmm2");

		asm volatile ("vbroadcasti32x4 %0,%%zmm3" : : "m" (gfgenpshufb[l][1][0][0]));
		asm volatile ("vbroadcasti32x4 %0,%%zmm13" : : "m" (gfgenpshufb[l][1][1][0]));
		asm volatile ("vpshufb   %zmm10,%zmm3,%zmm3");
		asm volatile ("vpshufb   %zmm11,%zmm13,%zmm13");
		asm volatile ("vpxorq    %zmm13,%zmm3,%zmm3");

		asm volatile ("vbroadcasti32x4 %0,%%zmm4" : : "m" (gfgenpshufb[l][2][0][0]));
		asm volatile ("vbroadcasti32x4 %0,%%zmm13" : : "m" (gfgenpshufb[l][2][1][0]));
		asm volatile ("vpshufb   %zmm10,%zmm4,%zmm4");
		asm volatile ("vpshufb   %zmm11,%zmm13,%zmm13");
		asm volatile ("vpxorq    %zmm13,%zmm4,%zmm4");

		/* intermediate disks */
		for (d = l - 1; d > 0; --d) {
			asm volatile ("vmovdqa64 %0,%%zmm10" : : "m" (v[d][i]));

			asm volatile ("vpxorq    %zmm10,%zmm0,%zmm0");

			asm volatile ("vpxorq    %zmm10,%zmm1,%zmm1");
			asm volatile ("vpcmpgtb  %zmm1,%zmm6,%k1");
			asm volatile ("vpmovm2b  %k1,%zmm11");
			asm volatile ("vpaddb    %zmm1,%zmm1,%zmm1");
			asm volatile ("vpandq    %zmm14,%zmm11,%zmm11");
			asm volatile ("vpxorq    %zmm11,%zmm1,%zmm1");

			asm volatile ("vpsrlw    $4,%zmm10,%zmm11");
			asm volatile ("vpandq    %zmm15,%zmm10,%zmm10");
			asm volatile ("vpandq    %zmm15,%zmm11,%zmm11");

			asm volatile ("vbroadcasti32x4 %0,%%zmm12" : : "m" (gfgenpshufb[d][0][0][0]));
			asm volatile ("vbroadcasti32x4 %0,%%zmm13" : : "m" (gfgenpshufb[d][0][1][0]));
			asm volatile ("vpshufb   %zmm10,%zmm12,%zmm12");
			asm volatile ("vpshufb   %zmm11,%zmm13,%zmm13");
			asm volatile ("vpxorq    %zmm12,%zmm2,%zmm2");
			asm volatile ("vpxorq    %zmm13,%zmm2,%zmm2");

			asm volatile ("vbroadcasti32x4 %0,%%zmm12" : : "m" (gfgenpshufb[d][1][0][0]));
			asm volatile ("vbroadcasti32x4 %0,%%zmm13" : : "m" (gfgenpshufb[d][1][1][0]));
			asm volatile ("vpshufb   %zmm10,%zmm12,%zmm12");
			asm volatile ("vpshufb   %zmm11,%zmm13,%zmm13");
			asm volatile ("vpxorq    %zmm12,%zmm3,%zmm3");
			asm volatile ("vpxorq    %zmm13,%zmm3,%zmm3");

			asm volatile ("vbroadcasti32x4 %0,%%zmm12" : : "m" (gfgenpshufb[d][2][0][0]));
			asm volatile ("vbroadcasti32x4 %0,%%zmm13" : : "m" (gfgenpshufb[d][2][1][0]));
			asm volatile ("vpshufb   %zmm10,%zmm12,%zmm12");
			asm volatile ("vpshufb   %zmm11,%zmm13,%zmm13");
			asm volatile ("vpxorq    %zmm12,%zmm4,%zmm4");
			asm volatile ("vpxorq    %zmm13,%zmm4,%zmm4");
		}

		/* first disk with all coefficients at 1 */
		asm volatile ("vmovdqa64 %0,%%zmm10" : : "m" (v[0][i]));

		asm volatile ("vpxorq    %zmm10,%zmm0,%zmm0");
		asm volatile ("vpxorq    %zmm10,%zmm1,%zmm1");
		asm volatile ("vpxorq    %zmm10,%zmm2,%zmm2");
		asm volatile ("vpxorq    %zmm10,%zmm3,%zmm3");
		asm volatile ("vpxorq    %zmm10,%zmm4,%zmm4");

		asm volatile ("vmovntdq  %%zmm0,%0" : "=m" (p[i]));
		asm volatile ("vmovntdq  %%zmm1,%0" : "=m" (q[i]));
		asm volatile ("vmovntdq  %%zmm2,%0" : "=m" (r[i]));
		asm volatile ("vmovntdq  %%zmm3,%0" : "=m" (s[i]));
		asm volatile ("vmovntdq  %%zmm4,%0" : "=m" (t[i]));
	}

	raid_avx_end();
}
#endif

#if defined(CONFIG_X86) && defined(CONFIG_SSSE3)
/*
 * GEN6 (hexa parity with Cauchy matrix) SSSE3 implementation
 */
void raid_gen6_ssse3(int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *q;
	uint8_t *r;
	uint8_t *s;
	uint8_t *t;
	uint8_t *u;
	int d, l;
	size_t i;
	uint8_t buffer[2*16+16];
	uint8_t *pd = __align_ptr(buffer, 16);

	l = nd - 1;
	p = v[nd];
	q = v[nd + 1];
	r = v[nd + 2];
	s = v[nd + 3];
	t = v[nd + 4];
	u = v[nd + 5];

	/* special case with only one data disk */
	if (l == 0) {
		for (i = 0; i < 6; ++i)
			memcpy(v[1 + i], v[0], size);
		return;
	}

	raid_sse_begin();

	/* generic case with at least two data disks */
	for (i = 0; i < size; i += 16) {
		/* last disk without the by two multiplication */
		asm volatile ("movdqa %0,%%xmm4" : : "m" (v[l][i]));

		asm volatile ("movdqa %%xmm4,%0" : "=m" (pd[0]));
		asm volatile ("movdqa %%xmm4,%0" : "=m" (pd[16]));

		asm volatile ("movdqa %0,%%xmm7" : : "m" (gfconst16.low4[0]));
		asm volatile ("movdqa %xmm4,%xmm5");
		asm volatile ("psrlw  $4,%xmm5");
		asm volatile ("pand   %xmm7,%xmm4");
		asm volatile ("pand   %xmm7,%xmm5");

		asm volatile ("movdqa %0,%%xmm0" : : "m" (gfgenpshufb[l][0][0][0]));
		asm volatile ("movdqa %0,%%xmm7" : : "m" (gfgenpshufb[l][0][1][0]));
		asm volatile ("pshufb %xmm4,%xmm0");
		asm volatile ("pshufb %xmm5,%xmm7");
		asm volatile ("pxor   %xmm7,%xmm0");

		asm volatile ("movdqa %0,%%xmm1" : : "m" (gfgenpshufb[l][1][0][0]));
		asm volatile ("movdqa %0,%%xmm7" : : "m" (gfgenpshufb[l][1][1][0]));
		asm volatile ("pshufb %xmm4,%xmm1");
		asm volatile ("pshufb %xmm5,%xmm7");
		asm volatile ("pxor   %xmm7,%xmm1");

		asm volatile ("movdqa %0,%%xmm2" : : "m" (gfgenpshufb[l][2][0][0]));
		asm volatile ("movdqa %0,%%xmm7" : : "m" (gfgenpshufb[l][2][1][0]));
		asm volatile ("pshufb %xmm4,%xmm2");
		asm volatile ("pshufb %xmm5,%xmm7");
		asm volatile ("pxor   %xmm7,%xmm2");

		asm volatile ("movdqa %0,%%xmm3" : : "m" (gfgenpshufb[l][3][0][0]));
		asm volatile ("movdqa %0,%%xmm7" : : "m" (gfgenpshufb[l][3][1][0]));
		asm volatile ("pshufb %xmm4,%xmm3");
		asm volatile ("pshufb %xmm5,%xmm7");
		asm volatile ("pxor   %xmm7,%xmm3");

		/* intermediate disks */
		for (d = l - 1; d > 0; --d) {
			asm volatile ("movdqa %0,%%xmm5" : : "m" (pd[0]));
			asm volatile ("movdqa %0,%%xmm6" : : "m" (pd[16]));
			asm volatile ("movdqa %0,%%xmm7" : : "m" (gfconst16.poly[0]));

			asm volatile ("pxor %xmm4,%xmm4");
			asm volatile ("pcmpgtb %xmm6,%xmm4");
			asm volatile ("paddb %xmm6,%xmm6");
			asm volatile ("pand %xmm7,%xmm4");
			asm volatile ("pxor %xmm4,%xmm6");

			asm volatile ("movdqa %0,%%xmm4" : : "m" (v[d][i]));

			asm volatile ("pxor %xmm4,%xmm5");
			asm volatile ("pxor %xmm4,%xmm6");
			asm volatile ("movdqa %%xmm5,%0" : "=m" (pd[0]));
			asm volatile ("movdqa %%xmm6,%0" : "=m" (pd[16]));

			asm volatile ("movdqa %0,%%xmm7" : : "m" (gfconst16.low4[0]));
			asm volatile ("movdqa %xmm4,%xmm5");
			asm volatile ("psrlw  $4,%xmm5");
			asm volatile ("pand   %xmm7,%xmm4");
			asm volatile ("pand   %xmm7,%xmm5");

			asm volatile ("movdqa %0,%%xmm6" : : "m" (gfgenpshufb[d][0][0][0]));
			asm volatile ("movdqa %0,%%xmm7" : : "m" (gfgenpshufb[d][0][1][0]));
			asm volatile ("pshufb %xmm4,%xmm6");
			asm volatile ("pshufb %xmm5,%xmm7");
			asm volatile ("pxor   %xmm6,%xmm0");
			asm volatile ("pxor   %xmm7,%xmm0");

			asm volatile ("movdqa %0,%%xmm6" : : "m" (gfgenpshufb[d][1][0][0]));
			asm volatile ("movdqa %0,%%xmm7" : : "m" (gfgenpshufb[d][1][1][0]));
			asm volatile ("pshufb %xmm4,%xmm6");
			asm volatile ("pshufb %xmm5,%xmm7");
			asm volatile ("pxor   %xmm6,%xmm1");
			asm volatile ("pxor   %xmm7,%xmm1");

			asm volatile ("movdqa %0,%%xmm6" : : "m" (gfgenpshufb[d][2][0][0]));
			asm volatile ("movdqa %0,%%xmm7" : : "m" (gfgenpshufb[d][2][1][0]));
			asm volatile ("pshufb %xmm4,%xmm6");
			asm volatile ("pshufb %xmm5,%xmm7");
			asm volatile ("pxor   %xmm6,%xmm2");
			asm volatile ("pxor   %xmm7,%xmm2");

			asm volatile ("movdqa %0,%%xmm6" : : "m" (gfgenpshufb[d][3][0][0]));
			asm volatile ("movdqa %0,%%xmm7" : : "m" (gfgenpshufb[d][3][1][0]));
			asm volatile ("pshufb %xmm4,%xmm6");
			asm volatile ("pshufb %xmm5,%xmm7");
			asm volatile ("pxor   %xmm6,%xmm3");
			asm volatile ("pxor   %xmm7,%xmm3");
		}

		/* first disk with all coefficients at 1 */
		asm volatile ("movdqa %0,%%xmm5" : : "m" (pd[0]));
		asm volatile ("movdqa %0,%%xmm6" : : "m" (pd[16]));
		asm volatile ("movdqa %0,%%xmm7" : : "m" (gfconst16.poly[0]));

		asm volatile ("pxor %xmm4,%xmm4");
		asm volatile ("pcmpgtb %xmm6,%xmm4");
		asm volatile ("paddb %xmm6,%xmm6");
		asm volatile ("pand %xmm7,%xmm4");
		asm volatile ("pxor %xmm4,%xmm6");

		asm volatile ("movdqa %0,%%xmm4" : : "m" (v[0][i]));
		asm volatile ("pxor %xmm4,%xmm0");
		asm volatile ("pxor %xmm4,%xmm1");
		asm volatile ("pxor %xmm4,%xmm2");
		asm volatile ("pxor %xmm4,%xmm3");
		asm volatile ("pxor %xmm4,%xmm5");
		asm volatile ("pxor %xmm4,%xmm6");

		asm volatile ("movntdq %%xmm5,%0" : "=m" (p[i]));
		asm volatile ("movntdq %%xmm6,%0" : "=m" (q[i]));
		asm volatile ("movntdq %%xmm0,%0" : "=m" (r[i]));
		asm volatile ("movntdq %%xmm1,%0" : "=m" (s[i]));
		asm volatile ("movntdq %%xmm2,%0" : "=m" (t[i]));
		asm volatile ("movntdq %%xmm3,%0" : "=m" (u[i]));
	}

	raid_sse_end();
}
#endif

#if defined(CONFIG_X86_64) && defined(CONFIG_SSSE3)
/*
 * GEN6 (hexa parity with Cauchy matrix) SSSE3 implementation
 *
 * Note that it uses 16 registers, meaning that x64 is required.
 */
void raid_gen6_ssse3ext(int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *q;
	uint8_t *r;
	uint8_t *s;
	uint8_t *t;
	uint8_t *u;
	int d, l;
	size_t i;

	l = nd - 1;
	p = v[nd];
	q = v[nd + 1];
	r = v[nd + 2];
	s = v[nd + 3];
	t = v[nd + 4];
	u = v[nd + 5];

	/* special case with only one data disk */
	if (l == 0) {
		for (i = 0; i < 6; ++i)
			memcpy(v[1 + i], v[0], size);
		return;
	}

	raid_sse_begin();

	/* generic case with at least two data disks */
	asm volatile ("movdqa %0,%%xmm14" : : "m" (gfconst16.poly[0]));
	asm volatile ("movdqa %0,%%xmm15" : : "m" (gfconst16.low4[0]));

	for (i = 0; i < size; i += 16) {
		/* last disk without the by two multiplication */
		asm volatile ("movdqa %0,%%xmm10" : : "m" (v[l][i]));

		asm volatile ("movdqa %xmm10,%xmm0");
		asm volatile ("movdqa %xmm10,%xmm1");

		asm volatile ("movdqa %xmm10,%xmm11");
		asm volatile ("psrlw  $4,%xmm11");
		asm volatile ("pand   %xmm15,%xmm10");
		asm volatile ("pand   %xmm15,%xmm11");

		asm volatile ("movdqa %0,%%xmm2" : : "m" (gfgenpshufb[l][0][0][0]));
		asm volatile ("movdqa %0,%%xmm13" : : "m" (gfgenpshufb[l][0][1][0]));
		asm volatile ("pshufb %xmm10,%xmm2");
		asm volatile ("pshufb %xmm11,%xmm13");
		asm volatile ("pxor   %xmm13,%xmm2");

		asm volatile ("movdqa %0,%%xmm3" : : "m" (gfgenpshufb[l][1][0][0]));
		asm volatile ("movdqa %0,%%xmm13" : : "m" (gfgenpshufb[l][1][1][0]));
		asm volatile ("pshufb %xmm10,%xmm3");
		asm volatile ("pshufb %xmm11,%xmm13");
		asm volatile ("pxor   %xmm13,%xmm3");

		asm volatile ("movdqa %0,%%xmm4" : : "m" (gfgenpshufb[l][2][0][0]));
		asm volatile ("movdqa %0,%%xmm13" : : "m" (gfgenpshufb[l][2][1][0]));
		asm volatile ("pshufb %xmm10,%xmm4");
		asm volatile ("pshufb %xmm11,%xmm13");
		asm volatile ("pxor   %xmm13,%xmm4");

		asm volatile ("movdqa %0,%%xmm5" : : "m" (gfgenpshufb[l][3][0][0]));
		asm volatile ("movdqa %0,%%xmm13" : : "m" (gfgenpshufb[l][3][1][0]));
		asm volatile ("pshufb %xmm10,%xmm5");
		asm volatile ("pshufb %xmm11,%xmm13");
		asm volatile ("pxor   %xmm13,%xmm5");

		/* intermediate disks */
		for (d = l - 1; d > 0; --d) {
			asm volatile ("movdqa %0,%%xmm10" : : "m" (v[d][i]));

			asm volatile ("pxor %xmm11,%xmm11");
			asm volatile ("pcmpgtb %xmm1,%xmm11");
			asm volatile ("paddb %xmm1,%xmm1");
			asm volatile ("pand %xmm14,%xmm11");
			asm volatile ("pxor %xmm11,%xmm1");

			asm volatile ("pxor %xmm10,%xmm0");
			asm volatile ("pxor %xmm10,%xmm1");

			asm volatile ("movdqa %xmm10,%xmm11");
			asm volatile ("psrlw  $4,%xmm11");
			asm volatile ("pand   %xmm15,%xmm10");
			asm volatile ("pand   %xmm15,%xmm11");

			asm volatile ("movdqa %0,%%xmm12" : : "m" (gfgenpshufb[d][0][0][0]));
			asm volatile ("movdqa %0,%%xmm13" : : "m" (gfgenpshufb[d][0][1][0]));
			asm volatile ("pshufb %xmm10,%xmm12");
			asm volatile ("pshufb %xmm11,%xmm13");
			asm volatile ("pxor   %xmm12,%xmm2");
			asm volatile ("pxor   %xmm13,%xmm2");

			asm volatile ("movdqa %0,%%xmm12" : : "m" (gfgenpshufb[d][1][0][0]));
			asm volatile ("movdqa %0,%%xmm13" : : "m" (gfgenpshufb[d][1][1][0]));
			asm volatile ("pshufb %xmm10,%xmm12");
			asm volatile ("pshufb %xmm11,%xmm13");
			asm volatile ("pxor   %xmm12,%xmm3");
			asm volatile ("pxor   %xmm13,%xmm3");

			asm volatile ("movdqa %0,%%xmm12" : : "m" (gfgenpshufb[d][2][0][0]));
			asm volatile ("movdqa %0,%%xmm13" : : "m" (gfgenpshufb[d][2][1][0]));
			asm volatile ("pshufb %xmm10,%xmm12");
			asm volatile ("pshufb %xmm11,%xmm13");
			asm volatile ("pxor   %xmm12,%xmm4");
			asm volatile ("pxor   %xmm13,%xmm4");

			asm volatile ("movdqa %0,%%xmm12" : : "m" (gfgenpshufb[d][3][0][0]));
			asm volatile ("movdqa %0,%%xmm13" : : "m" (gfgenpshufb[d][3][1][0]));
			asm volatile ("pshufb %xmm10,%xmm12");
			asm volatile ("pshufb %xmm11,%xmm13");
			asm volatile ("pxor   %xmm12,%xmm5");
			asm volatile ("pxor   %xmm13,%xmm5");
		}

		/* first disk with all coefficients at 1 */
		asm volatile ("movdqa %0,%%xmm10" : : "m" (v[0][i]));

		asm volatile ("pxor %xmm11,%xmm11");
		asm volatile ("pcmpgtb %xmm1,%xmm11");
		asm volatile ("paddb %xmm1,%xmm1");
		asm volatile ("pand %xmm14,%xmm11");
		asm volatile ("pxor %xmm11,%xmm1");

		asm volatile ("pxor %xmm10,%xmm0");
		asm volatile ("pxor %xmm10,%xmm1");
		asm volatile ("pxor %xmm10,%xmm2");
		asm volatile ("pxor %xmm10,%xmm3");
		asm volatile ("pxor %xmm10,%xmm4");
		asm volatile ("pxor %xmm10,%xmm5");

		asm volatile ("movntdq %%xmm0,%0" : "=m" (p[i]));
		asm volatile ("movntdq %%xmm1,%0" : "=m" (q[i]));
		asm volatile ("movntdq %%xmm2,%0" : "=m" (r[i]));
		asm volatile ("movntdq %%xmm3,%0" : "=m" (s[i]));
		asm volatile ("movntdq %%xmm4,%0" : "=m" (t[i]));
		asm volatile ("movntdq %%xmm5,%0" : "=m" (u[i]));
	}

	raid_sse_end();
}
#endif

#if defined(CONFIG_X86_64) && defined(CONFIG_AVX2)
/*
 * GEN6 (hexa parity with Cauchy matrix) AVX2 implementation
 *
 * Note that it uses 16 registers, meaning that x64 is required.
 */
void raid_gen6_avx2ext(int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *q;
	uint8_t *r;
	uint8_t *s;
	uint8_t *t;
	uint8_t *u;
	int d, l;
	size_t i;

	l = nd - 1;
	p = v[nd];
	q = v[nd + 1];
	r = v[nd + 2];
	s = v[nd + 3];
	t = v[nd + 4];
	u = v[nd + 5];

	/* special case with only one data disk */
	if (l == 0) {
		for (i = 0; i < 6; ++i)
			memcpy(v[1 + i], v[0], size);
		return;
	}

	raid_avx_begin();

	/*
	 * Register mapping:
	 * 
	 * 0,1,2,3,4,5 PARITY
	 * 6 ZERO
         * 10,11 MUL BY 2
	 * 12,13 PSHUFB
	 * 14 POLY
	 * 15 LOW4
	 */

	/* generic case with at least two data disks */
	asm volatile ("vpxor %ymm6,%ymm6,%ymm6");
	asm volatile ("vpbroadcastb %0,%%ymm14" : : "m" (gfconst16.poly[0]));
	asm volatile ("vpbroadcastb %0,%%ymm15" : : "m" (gfconst16.low4[0]));

	for (i = 0; i < size; i += 32) {
		/* last disk without the by two multiplication */
		asm volatile ("vmovdqa %0,%%ymm10" : : "m" (v[l][i]));

		asm volatile ("vmovdqa %ymm10,%ymm0");
		asm volatile ("vmovdqa %ymm10,%ymm1");

		asm volatile ("vpsrlw  $4,%ymm10,%ymm11");
		asm volatile ("vpand   %ymm15,%ymm10,%ymm10");
		asm volatile ("vpand   %ymm15,%ymm11,%ymm11");

		asm volatile ("vbroadcasti128 %0,%%ymm2" : : "m" (gfgenpshufb[l][0][0][0]));
		asm volatile ("vbroadcasti128 %0,%%ymm13" : : "m" (gfgenpshufb[l][0][1][0]));
		asm volatile ("vpshufb %ymm10,%ymm2,%ymm2");
		asm volatile ("vpshufb %ymm11,%ymm13,%ymm13");
		asm volatile ("vpxor   %ymm13,%ymm2,%ymm2");

		asm volatile ("vbroadcasti128 %0,%%ymm3" : : "m" (gfgenpshufb[l][1][0][0]));
		asm volatile ("vbroadcasti128 %0,%%ymm13" : : "m" (gfgenpshufb[l][1][1][0]));
		asm volatile ("vpshufb %ymm10,%ymm3,%ymm3");
		asm volatile ("vpshufb %ymm11,%ymm13,%ymm13");
		asm volatile ("vpxor   %ymm13,%ymm3,%ymm3");

		asm volatile ("vbroadcasti128 %0,%%ymm4" : : "m" (gfgenpshufb[l][2][0][0]));
		asm volatile ("vbroadcasti128 %0,%%ymm13" : : "m" (gfgenpshufb[l][2][1][0]));
		asm volatile ("vpshufb %ymm10,%ymm4,%ymm4");
		asm volatile ("vpshufb %ymm11,%ymm13,%ymm13");
		asm volatile ("vpxor   %ymm13,%ymm4,%ymm4");

		asm volatile ("vbroadcasti128 %0,%%ymm5" : : "m" (gfgenpshufb[l][3][0][0]));
		asm volatile ("vbroadcasti128 %0,%%ymm13" : : "m" (gfgenpshufb[l][3][1][0]));
		asm volatile ("vpshufb %ymm10,%ymm5,%ymm5");
		asm volatile ("vpshufb %ymm11,%ymm13,%ymm13");
		asm volatile ("vpxor   %ymm13,%ymm5,%ymm5");

		/* intermediate disks */
		for (d = l - 1; d > 0; --d) {
			asm volatile ("vmovdqa %0,%%ymm10" : : "m" (v[d][i]));

			asm volatile ("vpcmpgtb %ymm1,%ymm6,%ymm11");
			asm volatile ("vpaddb %ymm1,%ymm1,%ymm1");
			asm volatile ("vpand %ymm14,%ymm11,%ymm11");
			asm volatile ("vpxor %ymm11,%ymm1,%ymm1");

			asm volatile ("vpxor %ymm10,%ymm0,%ymm0");
			asm volatile ("vpxor %ymm10,%ymm1,%ymm1");

			asm volatile ("vpsrlw  $4,%ymm10,%ymm11");
			asm volatile ("vpand   %ymm15,%ymm10,%ymm10");
			asm volatile ("vpand   %ymm15,%ymm11,%ymm11");

			asm volatile ("vbroadcasti128 %0,%%ymm12" : : "m" (gfgenpshufb[d][0][0][0]));
			asm volatile ("vbroadcasti128 %0,%%ymm13" : : "m" (gfgenpshufb[d][0][1][0]));
			asm volatile ("vpshufb %ymm10,%ymm12,%ymm12");
			asm volatile ("vpshufb %ymm11,%ymm13,%ymm13");
			asm volatile ("vpxor   %ymm12,%ymm2,%ymm2");
			asm volatile ("vpxor   %ymm13,%ymm2,%ymm2");

			asm volatile ("vbroadcasti128 %0,%%ymm12" : : "m" (gfgenpshufb[d][1][0][0]));
			asm volatile ("vbroadcasti128 %0,%%ymm13" : : "m" (gfgenpshufb[d][1][1][0]));
			asm volatile ("vpshufb %ymm10,%ymm12,%ymm12");
			asm volatile ("vpshufb %ymm11,%ymm13,%ymm13");
			asm volatile ("vpxor   %ymm12,%ymm3,%ymm3");
			asm volatile ("vpxor   %ymm13,%ymm3,%ymm3");

			asm volatile ("vbroadcasti128 %0,%%ymm12" : : "m" (gfgenpshufb[d][2][0][0]));
			asm volatile ("vbroadcasti128 %0,%%ymm13" : : "m" (gfgenpshufb[d][2][1][0]));
			asm volatile ("vpshufb %ymm10,%ymm12,%ymm12");
			asm volatile ("vpshufb %ymm11,%ymm13,%ymm13");
			asm volatile ("vpxor   %ymm12,%ymm4,%ymm4");
			asm volatile ("vpxor   %ymm13,%ymm4,%ymm4");

			asm volatile ("vbroadcasti128 %0,%%ymm12" : : "m" (gfgenpshufb[d][3][0][0]));
			asm volatile ("vbroadcasti128 %0,%%ymm13" : : "m" (gfgenpshufb[d][3][1][0]));
			asm volatile ("vpshufb %ymm10,%ymm12,%ymm12");
			asm volatile ("vpshufb %ymm11,%ymm13,%ymm13");
			asm volatile ("vpxor   %ymm12,%ymm5,%ymm5");
			asm volatile ("vpxor   %ymm13,%ymm5,%ymm5");
		}

		/* first disk with all coefficients at 1 */
		asm volatile ("vmovdqa %0,%%ymm10" : : "m" (v[0][i]));

		asm volatile ("vpcmpgtb %ymm1,%ymm6,%ymm11");
		asm volatile ("vpaddb %ymm1,%ymm1,%ymm1");
		asm volatile ("vpand %ymm14,%ymm11,%ymm11");
		asm volatile ("vpxor %ymm11,%ymm1,%ymm1");

		asm volatile ("vpxor %ymm10,%ymm0,%ymm0");
		asm volatile ("vpxor %ymm10,%ymm1,%ymm1");
		asm volatile ("vpxor %ymm10,%ymm2,%ymm2");
		asm volatile ("vpxor %ymm10,%ymm3,%ymm3");
		asm volatile ("vpxor %ymm10,%ymm4,%ymm4");
		asm volatile ("vpxor %ymm10,%ymm5,%ymm5");

		asm volatile ("vmovntdq %%ymm0,%0" : "=m" (p[i]));
		asm volatile ("vmovntdq %%ymm1,%0" : "=m" (q[i]));
		asm volatile ("vmovntdq %%ymm2,%0" : "=m" (r[i]));
		asm volatile ("vmovntdq %%ymm3,%0" : "=m" (s[i]));
		asm volatile ("vmovntdq %%ymm4,%0" : "=m" (t[i]));
		asm volatile ("vmovntdq %%ymm5,%0" : "=m" (u[i]));
	}

	raid_avx_end();
}
#endif

#if defined(CONFIG_X86_64) && defined(CONFIG_AVX512BW)
/*
 * GEN6 (hexa parity with Cauchy matrix) AVX512BW implementation
 */
void raid_gen6_avx512bw(int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *q;
	uint8_t *r;
	uint8_t *s;
	uint8_t *t;
	uint8_t *u;
	int d, l;
	size_t i;

	l = nd - 1;
	p = v[nd];
	q = v[nd + 1];
	r = v[nd + 2];
	s = v[nd + 3];
	t = v[nd + 4];
	u = v[nd + 5];

	/* special case with only one data disk */
	if (l == 0) {
		for (i = 0; i < 6; ++i)
			memcpy(v[1 + i], v[0], size);
		return;
	}

	raid_avx_begin();

	/*
	 * Register mapping:
	 * 
	 * 0,1,2,3,4,5 PARITY
	 * 6 ZERO
         * 10,11 MUL BY 2
	 * 12,13 PSHUFB
	 * 14 POLY
	 * 15 LOW4
	 */

	/* generic case with at least two data disks */
	asm volatile ("vpxorq %zmm6,%zmm6,%zmm6");
	asm volatile ("vpbroadcastb %0,%%zmm14" : : "m" (gfconst16.poly[0]));
	asm volatile ("vpbroadcastb %0,%%zmm15" : : "m" (gfconst16.low4[0]));

	for (i = 0; i < size; i += 64) {
		/* last disk without the by two multiplication */
		asm volatile ("vmovdqa64 %0,%%zmm10" : : "m" (v[l][i]));

		asm volatile ("vmovdqa64 %zmm10,%zmm0");

		asm volatile ("vmovdqa64 %zmm10,%zmm1");
		asm volatile ("vpcmpgtb  %zmm1,%zmm6,%k1");
		asm volatile ("vpmovm2b  %k1,%zmm11");
		asm volatile ("vpaddb    %zmm1,%zmm1,%zmm1");
		asm volatile ("vpandq    %zmm14,%zmm11,%zmm11");
		asm volatile ("vpxorq    %zmm11,%zmm1,%zmm1");

		asm volatile ("vpsrlw    $4,%zmm10,%zmm11");
		asm volatile ("vpandq    %zmm15,%zmm10,%zmm10");
		asm volatile ("vpandq    %zmm15,%zmm11,%zmm11");

		asm volatile ("vbroadcasti32x4 %0,%%zmm2" : : "m" (gfgenpshufb[l][0][0][0]));
		asm volatile ("vbroadcasti32x4 %0,%%zmm13" : : "m" (gfgenpshufb[l][0][1][0]));
		asm volatile ("vpshufb   %zmm10,%zmm2,%zmm2");
		asm volatile ("vpshufb   %zmm11,%zmm13,%zmm13");
		asm volatile ("vpxorq    %zmm13,%zmm2,%zmm2");

		asm volatile ("vbroadcasti32x4 %0,%%zmm3" : : "m" (gfgenpshufb[l][1][0][0]));
		asm volatile ("vbroadcasti32x4 %0,%%zmm13" : : "m" (gfgenpshufb[l][1][1][0]));
		asm volatile ("vpshufb   %zmm10,%zmm3,%zmm3");
		asm volatile ("vpshufb   %zmm11,%zmm13,%zmm13");
		asm volatile ("vpxorq    %zmm13,%zmm3,%zmm3");

		asm volatile ("vbroadcasti32x4 %0,%%zmm4" : : "m" (gfgenpshufb[l][2][0][0]));
		asm volatile ("vbroadcasti32x4 %0,%%zmm13" : : "m" (gfgenpshufb[l][2][1][0]));
		asm volatile ("vpshufb   %zmm10,%zmm4,%zmm4");
		asm volatile ("vpshufb   %zmm11,%zmm13,%zmm13");
		asm volatile ("vpxorq    %zmm13,%zmm4,%zmm4");

		asm volatile ("vbroadcasti32x4 %0,%%zmm5" : : "m" (gfgenpshufb[l][3][0][0]));
		asm volatile ("vbroadcasti32x4 %0,%%zmm13" : : "m" (gfgenpshufb[l][3][1][0]));
		asm volatile ("vpshufb   %zmm10,%zmm5,%zmm5");
		asm volatile ("vpshufb   %zmm11,%zmm13,%zmm13");
		asm volatile ("vpxorq    %zmm13,%zmm5,%zmm5");

		/* intermediate disks */
		for (d = l - 1; d > 0; --d) {
			asm volatile ("vmovdqa64 %0,%%zmm10" : : "m" (v[d][i]));

			asm volatile ("vpxorq    %zmm10,%zmm0,%zmm0");

			asm volatile ("vpxorq    %zmm10,%zmm1,%zmm1");
			asm volatile ("vpcmpgtb  %zmm1,%zmm6,%k1");
			asm volatile ("vpmovm2b  %k1,%zmm11");
			asm volatile ("vpaddb    %zmm1,%zmm1,%zmm1");
			asm volatile ("vpandq    %zmm14,%zmm11,%zmm11");
			asm volatile ("vpxorq    %zmm11,%zmm1,%zmm1");


			asm volatile ("vpsrlw    $4,%zmm10,%zmm11");
			asm volatile ("vpandq    %zmm15,%zmm10,%zmm10");
			asm volatile ("vpandq    %zmm15,%zmm11,%zmm11");

			asm volatile ("vbroadcasti32x4 %0,%%zmm12" : : "m" (gfgenpshufb[d][0][0][0]));
			asm volatile ("vbroadcasti32x4 %0,%%zmm13" : : "m" (gfgenpshufb[d][0][1][0]));
			asm volatile ("vpshufb   %zmm10,%zmm12,%zmm12");
			asm volatile ("vpshufb   %zmm11,%zmm13,%zmm13");
			asm volatile ("vpxorq    %zmm12,%zmm2,%zmm2");
			asm volatile ("vpxorq    %zmm13,%zmm2,%zmm2");

			asm volatile ("vbroadcasti32x4 %0,%%zmm12" : : "m" (gfgenpshufb[d][1][0][0]));
			asm volatile ("vbroadcasti32x4 %0,%%zmm13" : : "m" (gfgenpshufb[d][1][1][0]));
			asm volatile ("vpshufb   %zmm10,%zmm12,%zmm12");
			asm volatile ("vpshufb   %zmm11,%zmm13,%zmm13");
			asm volatile ("vpxorq    %zmm12,%zmm3,%zmm3");
			asm volatile ("vpxorq    %zmm13,%zmm3,%zmm3");

			asm volatile ("vbroadcasti32x4 %0,%%zmm12" : : "m" (gfgenpshufb[d][2][0][0]));
			asm volatile ("vbroadcasti32x4 %0,%%zmm13" : : "m" (gfgenpshufb[d][2][1][0]));
			asm volatile ("vpshufb   %zmm10,%zmm12,%zmm12");
			asm volatile ("vpshufb   %zmm11,%zmm13,%zmm13");
			asm volatile ("vpxorq    %zmm12,%zmm4,%zmm4");
			asm volatile ("vpxorq    %zmm13,%zmm4,%zmm4");

			asm volatile ("vbroadcasti32x4 %0,%%zmm12" : : "m" (gfgenpshufb[d][3][0][0]));
			asm volatile ("vbroadcasti32x4 %0,%%zmm13" : : "m" (gfgenpshufb[d][3][1][0]));
			asm volatile ("vpshufb   %zmm10,%zmm12,%zmm12");
			asm volatile ("vpshufb   %zmm11,%zmm13,%zmm13");
			asm volatile ("vpxorq    %zmm12,%zmm5,%zmm5");
			asm volatile ("vpxorq    %zmm13,%zmm5,%zmm5");
		}

		/* first disk with all coefficients at 1 */
		asm volatile ("vmovdqa64 %0,%%zmm10" : : "m" (v[0][i]));

		asm volatile ("vpxorq    %zmm10,%zmm0,%zmm0");
		asm volatile ("vpxorq    %zmm10,%zmm1,%zmm1");
		asm volatile ("vpxorq    %zmm10,%zmm2,%zmm2");
		asm volatile ("vpxorq    %zmm10,%zmm3,%zmm3");
		asm volatile ("vpxorq    %zmm10,%zmm4,%zmm4");
		asm volatile ("vpxorq    %zmm10,%zmm5,%zmm5");

		asm volatile ("vmovntdq  %%zmm0,%0" : "=m" (p[i]));
		asm volatile ("vmovntdq  %%zmm1,%0" : "=m" (q[i]));
		asm volatile ("vmovntdq  %%zmm2,%0" : "=m" (r[i]));
		asm volatile ("vmovntdq  %%zmm3,%0" : "=m" (s[i]));
		asm volatile ("vmovntdq  %%zmm4,%0" : "=m" (t[i]));
		asm volatile ("vmovntdq  %%zmm5,%0" : "=m" (u[i]));
	}

	raid_avx_end();
}
#endif

#if defined(CONFIG_X86) && defined(CONFIG_SSSE3)
/*
 * RAID recovering for one disk SSSE3 implementation
 */
void raid_rec1_ssse3(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *pa;
	uint8_t G;
	uint8_t V;
	size_t i;

	(void)nr; /* unused, it's always 1 */

	/* if it's RAID5 uses the faster function */
	if (ip[0] == 0) {
		raid_rec1of1(id, nd, size, vv);
		return;
	}

	/* setup the coefficients matrix */
	G = A(ip[0], id[0]);

	/* invert it to solve the system of linear equations */
	V = inv(G);

	/* compute delta parity */
	raid_delta_gen(1, id, ip, nd, size, vv);

	p = v[nd + ip[0]];
	pa = v[id[0]];

	raid_sse_begin();

	asm volatile ("movdqa %0,%%xmm7" : : "m" (gfconst16.low4[0]));
	asm volatile ("movdqa %0,%%xmm4" : : "m" (gfmulpshufb[V][0][0]));
	asm volatile ("movdqa %0,%%xmm5" : : "m" (gfmulpshufb[V][1][0]));

	for (i = 0; i < size; i += 16) {
		asm volatile ("movdqa %0,%%xmm0" : : "m" (p[i]));
		asm volatile ("movdqa %0,%%xmm1" : : "m" (pa[i]));
		asm volatile ("movdqa %xmm4,%xmm2");
		asm volatile ("movdqa %xmm5,%xmm3");
		asm volatile ("pxor   %xmm0,%xmm1");
		asm volatile ("movdqa %xmm1,%xmm0");
		asm volatile ("psrlw  $4,%xmm1");
		asm volatile ("pand   %xmm7,%xmm0");
		asm volatile ("pand   %xmm7,%xmm1");
		asm volatile ("pshufb %xmm0,%xmm2");
		asm volatile ("pshufb %xmm1,%xmm3");
		asm volatile ("pxor   %xmm3,%xmm2");
		asm volatile ("movdqa %%xmm2,%0" : "=m" (pa[i]));
	}

	raid_sse_end();
}
#endif

#if defined(CONFIG_X86) && defined(CONFIG_SSSE3)
/*
 * RAID recovering for two disks SSSE3 implementation
 */
void raid_rec2_ssse3(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	const int N = 2;
	uint8_t *p[N];
	uint8_t *pa[N];
	uint8_t G[N * N];
	uint8_t V[N * N];
	size_t i;
	int j, k;

	(void)nr; /* unused, it's always 2 */

	/* setup the coefficients matrix */
	for (j = 0; j < N; ++j)
		for (k = 0; k < N; ++k)
			G[j * N + k] = A(ip[j], id[k]);

	/* invert it to solve the system of linear equations */
	raid_invert(G, V, N);

	/* compute delta parity */
	raid_delta_gen(N, id, ip, nd, size, vv);

	for (j = 0; j < N; ++j) {
		p[j] = v[nd + ip[j]];
		pa[j] = v[id[j]];
	}

	raid_sse_begin();

	asm volatile ("movdqa %0,%%xmm7" : : "m" (gfconst16.low4[0]));

	for (i = 0; i < size; i += 16) {
		asm volatile ("movdqa %0,%%xmm0" : : "m" (p[0][i]));
		asm volatile ("movdqa %0,%%xmm2" : : "m" (pa[0][i]));
		asm volatile ("movdqa %0,%%xmm1" : : "m" (p[1][i]));
		asm volatile ("movdqa %0,%%xmm3" : : "m" (pa[1][i]));
		asm volatile ("pxor   %xmm2,%xmm0");
		asm volatile ("pxor   %xmm3,%xmm1");

		asm volatile ("pxor %xmm6,%xmm6");

		asm volatile ("movdqa %0,%%xmm2" : : "m" (gfmulpshufb[V[0]][0][0]));
		asm volatile ("movdqa %0,%%xmm3" : : "m" (gfmulpshufb[V[0]][1][0]));
		asm volatile ("movdqa %xmm0,%xmm4");
		asm volatile ("movdqa %xmm0,%xmm5");
		asm volatile ("psrlw  $4,%xmm5");
		asm volatile ("pand   %xmm7,%xmm4");
		asm volatile ("pand   %xmm7,%xmm5");
		asm volatile ("pshufb %xmm4,%xmm2");
		asm volatile ("pshufb %xmm5,%xmm3");
		asm volatile ("pxor   %xmm2,%xmm6");
		asm volatile ("pxor   %xmm3,%xmm6");

		asm volatile ("movdqa %0,%%xmm2" : : "m" (gfmulpshufb[V[1]][0][0]));
		asm volatile ("movdqa %0,%%xmm3" : : "m" (gfmulpshufb[V[1]][1][0]));
		asm volatile ("movdqa %xmm1,%xmm4");
		asm volatile ("movdqa %xmm1,%xmm5");
		asm volatile ("psrlw  $4,%xmm5");
		asm volatile ("pand   %xmm7,%xmm4");
		asm volatile ("pand   %xmm7,%xmm5");
		asm volatile ("pshufb %xmm4,%xmm2");
		asm volatile ("pshufb %xmm5,%xmm3");
		asm volatile ("pxor   %xmm2,%xmm6");
		asm volatile ("pxor   %xmm3,%xmm6");

		asm volatile ("movdqa %%xmm6,%0" : "=m" (pa[0][i]));

		asm volatile ("pxor %xmm6,%xmm6");

		asm volatile ("movdqa %0,%%xmm2" : : "m" (gfmulpshufb[V[2]][0][0]));
		asm volatile ("movdqa %0,%%xmm3" : : "m" (gfmulpshufb[V[2]][1][0]));
		asm volatile ("movdqa %xmm0,%xmm4");
		asm volatile ("movdqa %xmm0,%xmm5");
		asm volatile ("psrlw  $4,%xmm5");
		asm volatile ("pand   %xmm7,%xmm4");
		asm volatile ("pand   %xmm7,%xmm5");
		asm volatile ("pshufb %xmm4,%xmm2");
		asm volatile ("pshufb %xmm5,%xmm3");
		asm volatile ("pxor   %xmm2,%xmm6");
		asm volatile ("pxor   %xmm3,%xmm6");

		asm volatile ("movdqa %0,%%xmm2" : : "m" (gfmulpshufb[V[3]][0][0]));
		asm volatile ("movdqa %0,%%xmm3" : : "m" (gfmulpshufb[V[3]][1][0]));
		asm volatile ("movdqa %xmm1,%xmm4");
		asm volatile ("movdqa %xmm1,%xmm5");
		asm volatile ("psrlw  $4,%xmm5");
		asm volatile ("pand   %xmm7,%xmm4");
		asm volatile ("pand   %xmm7,%xmm5");
		asm volatile ("pshufb %xmm4,%xmm2");
		asm volatile ("pshufb %xmm5,%xmm3");
		asm volatile ("pxor   %xmm2,%xmm6");
		asm volatile ("pxor   %xmm3,%xmm6");

		asm volatile ("movdqa %%xmm6,%0" : "=m" (pa[1][i]));
	}

	raid_sse_end();
}
#endif

#if defined(CONFIG_X86) && defined(CONFIG_SSSE3)
/*
 * RAID recovering SSSE3 implementation
 */
void raid_recX_ssse3(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	int N = nr;
	uint8_t *p[RAID_PARITY_MAX];
	uint8_t *pa[RAID_PARITY_MAX];
	uint8_t G[RAID_PARITY_MAX * RAID_PARITY_MAX];
	uint8_t V[RAID_PARITY_MAX * RAID_PARITY_MAX];
	uint8_t buffer[RAID_PARITY_MAX*16+16];
	uint8_t *pd = __align_ptr(buffer, 16);
	size_t i;
	int j, k;

	/* setup the coefficients matrix */
	for (j = 0; j < N; ++j)
		for (k = 0; k < N; ++k)
			G[j * N + k] = A(ip[j], id[k]);

	/* invert it to solve the system of linear equations */
	raid_invert(G, V, N);

	/* compute delta parity */
	raid_delta_gen(N, id, ip, nd, size, vv);

	for (j = 0; j < N; ++j) {
		p[j] = v[nd + ip[j]];
		pa[j] = v[id[j]];
	}

	raid_sse_begin();

	asm volatile ("movdqa %0,%%xmm7" : : "m" (gfconst16.low4[0]));

	for (i = 0; i < size; i += 16) {
		/* delta */
		for (j = 0; j < N; ++j) {
			asm volatile ("movdqa %0,%%xmm0" : : "m" (p[j][i]));
			asm volatile ("movdqa %0,%%xmm1" : : "m" (pa[j][i]));
			asm volatile ("pxor   %xmm1,%xmm0");
			asm volatile ("movdqa %%xmm0,%0" : "=m" (pd[j*16]));
		}

		/* reconstruct */
		for (j = 0; j < N; ++j) {
			asm volatile ("pxor %xmm0,%xmm0");
			asm volatile ("pxor %xmm1,%xmm1");

			for (k = 0; k < N; ++k) {
				uint8_t m = V[j * N + k];

				asm volatile ("movdqa %0,%%xmm2" : : "m" (gfmulpshufb[m][0][0]));
				asm volatile ("movdqa %0,%%xmm3" : : "m" (gfmulpshufb[m][1][0]));
				asm volatile ("movdqa %0,%%xmm4" : : "m" (pd[k*16]));
				asm volatile ("movdqa %xmm4,%xmm5");
				asm volatile ("psrlw  $4,%xmm5");
				asm volatile ("pand   %xmm7,%xmm4");
				asm volatile ("pand   %xmm7,%xmm5");
				asm volatile ("pshufb %xmm4,%xmm2");
				asm volatile ("pshufb %xmm5,%xmm3");
				asm volatile ("pxor   %xmm2,%xmm0");
				asm volatile ("pxor   %xmm3,%xmm1");
			}

			asm volatile ("pxor %xmm1,%xmm0");
			asm volatile ("movdqa %%xmm0,%0" : "=m" (pa[j][i]));
		}
	}

	raid_sse_end();
}
#endif

#if defined(CONFIG_X86) && defined(CONFIG_AVX2)
/*
 * RAID recovering for one disk AVX2 implementation
 */
void raid_rec1_avx2(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *pa;
	uint8_t G;
	uint8_t V;
	size_t i;

	(void)nr; /* unused, it's always 1 */

	/* if it's RAID5 uses the faster function */
	if (ip[0] == 0) {
		raid_rec1of1(id, nd, size, vv);
		return;
	}

	/* setup the coefficients matrix */
	G = A(ip[0], id[0]);

	/* invert it to solve the system of linear equations */
	V = inv(G);

	/* compute delta parity */
	raid_delta_gen(1, id, ip, nd, size, vv);

	p = v[nd + ip[0]];
	pa = v[id[0]];

	raid_avx_begin();

	asm volatile ("vpbroadcastb %0,%%ymm7" : : "m" (gfconst16.low4[0]));
	asm volatile ("vbroadcasti128 %0,%%ymm4" : : "m" (gfmulpshufb[V][0][0]));
	asm volatile ("vbroadcasti128 %0,%%ymm5" : : "m" (gfmulpshufb[V][1][0]));

	for (i = 0; i < size; i += 32) {
		asm volatile ("vmovdqa %0,%%ymm0" : : "m" (p[i]));
		asm volatile ("vmovdqa %0,%%ymm1" : : "m" (pa[i]));
		asm volatile ("vpxor   %ymm1,%ymm0,%ymm0");
		asm volatile ("vpsrlw  $4,%ymm0,%ymm1");
		asm volatile ("vpand   %ymm7,%ymm0,%ymm0");
		asm volatile ("vpand   %ymm7,%ymm1,%ymm1");
		asm volatile ("vpshufb %ymm0,%ymm4,%ymm2");
		asm volatile ("vpshufb %ymm1,%ymm5,%ymm3");
		asm volatile ("vpxor   %ymm3,%ymm2,%ymm2");
		asm volatile ("vmovdqa %%ymm2,%0" : "=m" (pa[i]));
	}

	raid_avx_end();
}
#endif

#if defined(CONFIG_X86) && defined(CONFIG_AVX2)
/*
 * RAID recovering for two disks AVX2 implementation
 */
void raid_rec2_avx2(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	const int N = 2;
	uint8_t *p[N];
	uint8_t *pa[N];
	uint8_t G[N * N];
	uint8_t V[N * N];
	size_t i;
	int j, k;

	(void)nr; /* unused, it's always 2 */

	/* setup the coefficients matrix */
	for (j = 0; j < N; ++j)
		for (k = 0; k < N; ++k)
			G[j * N + k] = A(ip[j], id[k]);

	/* invert it to solve the system of linear equations */
	raid_invert(G, V, N);

	/* compute delta parity */
	raid_delta_gen(N, id, ip, nd, size, vv);

	for (j = 0; j < N; ++j) {
		p[j] = v[nd + ip[j]];
		pa[j] = v[id[j]];
	}

	raid_avx_begin();

	asm volatile ("vpbroadcastb %0,%%ymm7" : : "m" (gfconst16.low4[0]));

	for (i = 0; i < size; i += 32) {
		asm volatile ("vmovdqa %0,%%ymm0" : : "m" (p[0][i]));
		asm volatile ("vmovdqa %0,%%ymm2" : : "m" (pa[0][i]));
		asm volatile ("vmovdqa %0,%%ymm1" : : "m" (p[1][i]));
		asm volatile ("vmovdqa %0,%%ymm3" : : "m" (pa[1][i]));
		asm volatile ("vpxor   %ymm2,%ymm0,%ymm0");
		asm volatile ("vpxor   %ymm3,%ymm1,%ymm1");

		asm volatile ("vpxor %ymm6,%ymm6,%ymm6");

		asm volatile ("vbroadcasti128 %0,%%ymm2" : : "m" (gfmulpshufb[V[0]][0][0]));
		asm volatile ("vbroadcasti128 %0,%%ymm3" : : "m" (gfmulpshufb[V[0]][1][0]));
		asm volatile ("vpsrlw  $4,%ymm0,%ymm5");
		asm volatile ("vpand   %ymm7,%ymm0,%ymm4");
		asm volatile ("vpand   %ymm7,%ymm5,%ymm5");
		asm volatile ("vpshufb %ymm4,%ymm2,%ymm2");
		asm volatile ("vpshufb %ymm5,%ymm3,%ymm3");
		asm volatile ("vpxor   %ymm2,%ymm6,%ymm6");
		asm volatile ("vpxor   %ymm3,%ymm6,%ymm6");

		asm volatile ("vbroadcasti128 %0,%%ymm2" : : "m" (gfmulpshufb[V[1]][0][0]));
		asm volatile ("vbroadcasti128 %0,%%ymm3" : : "m" (gfmulpshufb[V[1]][1][0]));
		asm volatile ("vpsrlw  $4,%ymm1,%ymm5");
		asm volatile ("vpand   %ymm7,%ymm1,%ymm4");
		asm volatile ("vpand   %ymm7,%ymm5,%ymm5");
		asm volatile ("vpshufb %ymm4,%ymm2,%ymm2");
		asm volatile ("vpshufb %ymm5,%ymm3,%ymm3");
		asm volatile ("vpxor   %ymm2,%ymm6,%ymm6");
		asm volatile ("vpxor   %ymm3,%ymm6,%ymm6");

		asm volatile ("vmovdqa %%ymm6,%0" : "=m" (pa[0][i]));

		asm volatile ("vpxor %ymm6,%ymm6,%ymm6");

		asm volatile ("vbroadcasti128 %0,%%ymm2" : : "m" (gfmulpshufb[V[2]][0][0]));
		asm volatile ("vbroadcasti128 %0,%%ymm3" : : "m" (gfmulpshufb[V[2]][1][0]));
		asm volatile ("vpsrlw  $4,%ymm0,%ymm5");
		asm volatile ("vpand   %ymm7,%ymm0,%ymm4");
		asm volatile ("vpand   %ymm7,%ymm5,%ymm5");
		asm volatile ("vpshufb %ymm4,%ymm2,%ymm2");
		asm volatile ("vpshufb %ymm5,%ymm3,%ymm3");
		asm volatile ("vpxor   %ymm2,%ymm6,%ymm6");
		asm volatile ("vpxor   %ymm3,%ymm6,%ymm6");

		asm volatile ("vbroadcasti128 %0,%%ymm2" : : "m" (gfmulpshufb[V[3]][0][0]));
		asm volatile ("vbroadcasti128 %0,%%ymm3" : : "m" (gfmulpshufb[V[3]][1][0]));
		asm volatile ("vpsrlw  $4,%ymm1,%ymm5");
		asm volatile ("vpand   %ymm7,%ymm1,%ymm4");
		asm volatile ("vpand   %ymm7,%ymm5,%ymm5");
		asm volatile ("vpshufb %ymm4,%ymm2,%ymm2");
		asm volatile ("vpshufb %ymm5,%ymm3,%ymm3");
		asm volatile ("vpxor   %ymm2,%ymm6,%ymm6");
		asm volatile ("vpxor   %ymm3,%ymm6,%ymm6");

		asm volatile ("vmovdqa %%ymm6,%0" : "=m" (pa[1][i]));
	}

	raid_avx_end();
}
#endif

#if defined(CONFIG_X86) && defined(CONFIG_AVX2)
/*
 * RAID recovering AVX2 implementation
 */
void raid_recX_avx2(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	int N = nr;
	uint8_t *p[RAID_PARITY_MAX];
	uint8_t *pa[RAID_PARITY_MAX];
	uint8_t G[RAID_PARITY_MAX * RAID_PARITY_MAX];
	uint8_t V[RAID_PARITY_MAX * RAID_PARITY_MAX];
	uint8_t buffer[RAID_PARITY_MAX*32+32];
	uint8_t *pd = __align_ptr(buffer, 32);
	size_t i;
	int j, k;

	/* setup the coefficients matrix */
	for (j = 0; j < N; ++j)
		for (k = 0; k < N; ++k)
			G[j * N + k] = A(ip[j], id[k]);

	/* invert it to solve the system of linear equations */
	raid_invert(G, V, N);

	/* compute delta parity */
	raid_delta_gen(N, id, ip, nd, size, vv);

	for (j = 0; j < N; ++j) {
		p[j] = v[nd + ip[j]];
		pa[j] = v[id[j]];
	}

	raid_avx_begin();

	asm volatile ("vpbroadcastb %0,%%ymm7" : : "m" (gfconst16.low4[0]));

	for (i = 0; i < size; i += 32) {
		/* delta */
		for (j = 0; j < N; ++j) {
			asm volatile ("vmovdqa %0,%%ymm0" : : "m" (p[j][i]));
			asm volatile ("vmovdqa %0,%%ymm1" : : "m" (pa[j][i]));
			asm volatile ("vpxor   %ymm1,%ymm0,%ymm0");
			asm volatile ("vmovdqa %%ymm0,%0" : "=m" (pd[j*32]));
		}

		/* reconstruct */
		for (j = 0; j < N; ++j) {
			asm volatile ("vpxor %ymm0,%ymm0,%ymm0");
			asm volatile ("vpxor %ymm1,%ymm1,%ymm1");

			for (k = 0; k < N; ++k) {
				uint8_t m = V[j * N + k];

				asm volatile ("vbroadcasti128 %0,%%ymm2" : : "m" (gfmulpshufb[m][0][0]));
				asm volatile ("vbroadcasti128 %0,%%ymm3" : : "m" (gfmulpshufb[m][1][0]));
				asm volatile ("vmovdqa %0,%%ymm4" : : "m" (pd[k*32]));
				asm volatile ("vpsrlw  $4,%ymm4,%ymm5");
				asm volatile ("vpand   %ymm7,%ymm4,%ymm4");
				asm volatile ("vpand   %ymm7,%ymm5,%ymm5");
				asm volatile ("vpshufb %ymm4,%ymm2,%ymm2");
				asm volatile ("vpshufb %ymm5,%ymm3,%ymm3");
				asm volatile ("vpxor   %ymm2,%ymm0,%ymm0");
				asm volatile ("vpxor   %ymm3,%ymm1,%ymm1");
			}

			asm volatile ("vpxor %ymm1,%ymm0,%ymm0");
			asm volatile ("vmovdqa %%ymm0,%0" : "=m" (pa[j][i]));
		}
	}

	raid_avx_end();
}
#endif

#if defined(CONFIG_X86_64) && defined(CONFIG_AVX512BW)
/*
 * RAID recovering for one disk AVX512BW implementation
 */
void raid_rec1_avx512bw(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p, *pa;
	uint8_t G, V;
	size_t i;

	(void)nr;

	if (ip[0] == 0) {
		raid_rec1of1(id, nd, size, vv);
		return;
	}

	G = A(ip[0], id[0]);
	V = inv(G);

	raid_delta_gen(1, id, ip, nd, size, vv);

	p = v[nd + ip[0]];
	pa = v[id[0]];

	raid_avx_begin();

	asm volatile ("vpbroadcastb %0,%%zmm7" : : "m" (gfconst16.low4[0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm4" : : "m" (gfmulpshufb[V][0][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm5" : : "m" (gfmulpshufb[V][1][0]));

	for (i = 0; i < size; i += 64) {
		asm volatile ("vmovdqa64 %0,%%zmm0" : : "m" (p[i]));
		asm volatile ("vmovdqa64 %0,%%zmm1" : : "m" (pa[i]));
		asm volatile ("vpxord   %zmm1,%zmm0,%zmm0");
		asm volatile ("vpsrlw   $4,%zmm0,%zmm1");
		asm volatile ("vpandd   %zmm7,%zmm0,%zmm0");
		asm volatile ("vpandd   %zmm7,%zmm1,%zmm1");
		asm volatile ("vpshufb  %zmm0,%zmm4,%zmm2");
		asm volatile ("vpshufb  %zmm1,%zmm5,%zmm3");
		asm volatile ("vpxord   %zmm3,%zmm2,%zmm2");
		asm volatile ("vmovdqa64 %%zmm2, %0" : "=m" (pa[i]));
	}

	raid_avx_end();
}
#endif

#if defined(CONFIG_X86_64) && defined(CONFIG_AVX512BW)
/*
 * RAID recovering for two disks AVX512BW implementation
 */
void raid_rec2_avx512bw(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	const int N = 2;
	uint8_t *p[N], *pa[N];
	uint8_t G[N * N], V[N * N];
	size_t i;
	int j, k;

	(void)nr;

	for (j = 0; j < N; ++j)
		for (k = 0; k < N; ++k)
			G[j * N + k] = A(ip[j], id[k]);

	raid_invert(G, V, N);
	raid_delta_gen(N, id, ip, nd, size, vv);

	for (j = 0; j < N; ++j) {
		p[j] = v[nd + ip[j]];
		pa[j] = v[id[j]];
	}

	raid_avx_begin();

	asm volatile ("vpbroadcastb %0,%%zmm7" : : "m" (gfconst16.low4[0]));

	for (i = 0; i < size; i += 64) {
		asm volatile ("vmovdqa64 %0,%%zmm0" : : "m" (p[0][i]));
		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (pa[0][i]));
		asm volatile ("vmovdqa64 %0,%%zmm1" : : "m" (p[1][i]));
		asm volatile ("vmovdqa64 %0,%%zmm3" : : "m" (pa[1][i]));
		asm volatile ("vpxord   %zmm2,%zmm0,%zmm0");
		asm volatile ("vpxord   %zmm3,%zmm1,%zmm1");

		asm volatile ("vpxord %zmm6,%zmm6,%zmm6");

		asm volatile ("vbroadcasti32x4 %0,%%zmm2" : : "m" (gfmulpshufb[V[0]][0][0]));
		asm volatile ("vbroadcasti32x4 %0,%%zmm3" : : "m" (gfmulpshufb[V[0]][1][0]));
		asm volatile ("vbroadcasti32x4 %0,%%zmm10" : : "m" (gfmulpshufb[V[1]][0][0]));
		asm volatile ("vbroadcasti32x4 %0,%%zmm11" : : "m" (gfmulpshufb[V[1]][1][0]));
		asm volatile ("vpsrlw  $4,%zmm0,%zmm5");
		asm volatile ("vpsrlw  $4,%zmm1,%zmm13");
		asm volatile ("vpandd  %zmm7,%zmm0,%zmm4");
		asm volatile ("vpandd  %zmm7,%zmm5,%zmm5");
		asm volatile ("vpandd  %zmm7,%zmm1,%zmm12");
		asm volatile ("vpandd  %zmm7,%zmm13,%zmm13");
		asm volatile ("vpshufb %zmm4,%zmm2,%zmm2");
		asm volatile ("vpshufb %zmm5,%zmm3,%zmm3");
		asm volatile ("vpshufb %zmm12,%zmm10,%zmm10");
		asm volatile ("vpshufb %zmm13,%zmm11,%zmm11");
		asm volatile ("vpxord  %zmm2,%zmm6,%zmm6");
		asm volatile ("vpxord  %zmm3,%zmm6,%zmm6");
		asm volatile ("vpxord  %zmm10,%zmm6,%zmm6");
		asm volatile ("vpxord  %zmm11,%zmm6,%zmm6");

		asm volatile ("vmovdqa64 %%zmm6,%0" : "=m" (pa[0][i]));

		asm volatile ("vpxord %zmm6,%zmm6,%zmm6");

		asm volatile ("vbroadcasti32x4 %0,%%zmm2" : : "m" (gfmulpshufb[V[2]][0][0]));
		asm volatile ("vbroadcasti32x4 %0,%%zmm3" : : "m" (gfmulpshufb[V[2]][1][0]));
		asm volatile ("vbroadcasti32x4 %0,%%zmm10" : : "m" (gfmulpshufb[V[3]][0][0]));
		asm volatile ("vbroadcasti32x4 %0,%%zmm11" : : "m" (gfmulpshufb[V[3]][1][0]));
		asm volatile ("vpsrlw  $4,%zmm0,%zmm5");
		asm volatile ("vpsrlw  $4,%zmm1,%zmm13");
		asm volatile ("vpandd  %zmm7,%zmm0,%zmm4");
		asm volatile ("vpandd  %zmm7,%zmm5,%zmm5");
		asm volatile ("vpandd  %zmm7,%zmm1,%zmm12");
		asm volatile ("vpandd  %zmm7,%zmm13,%zmm13");
		asm volatile ("vpshufb %zmm4,%zmm2,%zmm2");
		asm volatile ("vpshufb %zmm5,%zmm3,%zmm3");
		asm volatile ("vpshufb %zmm12,%zmm10,%zmm10");
		asm volatile ("vpshufb %zmm13,%zmm11,%zmm11");
		asm volatile ("vpxord  %zmm2,%zmm6,%zmm6");
		asm volatile ("vpxord  %zmm3,%zmm6,%zmm6");
		asm volatile ("vpxord  %zmm10,%zmm6,%zmm6");
		asm volatile ("vpxord  %zmm11,%zmm6,%zmm6");

		asm volatile ("vmovdqa64 %%zmm6,%0" : "=m" (pa[1][i]));
	}

	raid_avx_end();
}
#endif

#if defined(CONFIG_X86_64) && defined(CONFIG_AVX512BW)
/*
 * RAID recovering AVX512BW implementation
 */
void raid_recX_avx512bw(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	int N = nr;
	uint8_t *p[RAID_PARITY_MAX];
	uint8_t *pa[RAID_PARITY_MAX];
	uint8_t G[RAID_PARITY_MAX * RAID_PARITY_MAX];
	uint8_t V[RAID_PARITY_MAX * RAID_PARITY_MAX];
	uint8_t buffer[RAID_PARITY_MAX*64+64];
	uint8_t *pd = __align_ptr(buffer, 64);
	size_t i;
	int j, k;

	/* setup the coefficients matrix */
	for (j = 0; j < N; ++j)
		for (k = 0; k < N; ++k)
			G[j * N + k] = A(ip[j], id[k]);

	/* invert it to solve the system of linear equations */
	raid_invert(G, V, N);

	/* compute delta parity */
	raid_delta_gen(N, id, ip, nd, size, vv);

	for (j = 0; j < N; ++j) {
		p[j] = v[nd + ip[j]];
		pa[j] = v[id[j]];
	}

	raid_avx_begin();

	asm volatile ("vpbroadcastb %0,%%zmm7" : : "m" (gfconst16.low4[0]));

	for (i = 0; i < size; i += 64) {
		/* delta */
		for (j = 0; j < N; ++j) {
			asm volatile ("vmovdqa64 %0,%%zmm0" : : "m" (p[j][i]));
			asm volatile ("vmovdqa64 %0,%%zmm1" : : "m" (pa[j][i]));
			asm volatile ("vpxorq    %zmm1,%zmm0,%zmm0");
			asm volatile ("vmovdqa64 %%zmm0,%0" : "=m" (pd[j*64]));
		}
		/* reconstruct */
		for (j = 0; j < N; ++j) {
			asm volatile ("vpxorq %zmm0,%zmm0,%zmm0");
			asm volatile ("vpxorq %zmm1,%zmm1,%zmm1");
			for (k = 0; k < N; ++k) {
				uint8_t m = V[j * N + k];
				asm volatile ("vbroadcasti32x4 %0,%%zmm2" : : "m" (gfmulpshufb[m][0][0]));
				asm volatile ("vbroadcasti32x4 %0,%%zmm3" : : "m" (gfmulpshufb[m][1][0]));
				asm volatile ("vmovdqa64 %0,%%zmm4" : : "m" (pd[k*64]));
				asm volatile ("vpsrlw    $4,%zmm4,%zmm5");
				asm volatile ("vpandq    %zmm7,%zmm4,%zmm4");
				asm volatile ("vpandq    %zmm7,%zmm5,%zmm5");
				asm volatile ("vpshufb   %zmm4,%zmm2,%zmm2");
				asm volatile ("vpshufb   %zmm5,%zmm3,%zmm3");
				asm volatile ("vpxorq    %zmm2,%zmm0,%zmm0");
				asm volatile ("vpxorq    %zmm3,%zmm1,%zmm1");
			}
			asm volatile ("vpxorq    %zmm1,%zmm0,%zmm0");
			asm volatile ("vmovdqa64 %%zmm0,%0" : "=m" (pa[j][i]));
		}
	}

	raid_avx_end();
}
#endif

#if defined(CONFIG_X86) && defined(CONFIG_SSE2)
void raid_register_x86(void)
{
#ifdef CONFIG_SSE2
	if (raid_cpu_has_sse2()) {
		raid_gen_register(RAID_ALGO_CAUCHY_PAR1, "sse2", raid_gen1_sse2);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR2, "sse2", raid_gen2_sse2);
#ifdef CONFIG_X86_64
		if (!raid_cpu_has_slowextendedreg())
			raid_gen_register(RAID_ALGO_CAUCHY_PAR2, "sse2e", raid_gen2_sse2ext);
		/* note that raid_cpu_has_slowextendedreg() doesn't affect vandermonde */
		raid_gen_register(RAID_ALGO_VANDERMONDE_PAR3, "sse2e", raid_genz_sse2ext);
#else
		raid_gen_register(RAID_ALGO_VANDERMONDE_PAR3, "sse2", raid_genz_sse2);
#endif
	}
#endif

#ifdef CONFIG_SSSE3
	if (raid_cpu_has_ssse3()) {
		raid_gen_register(RAID_ALGO_CAUCHY_PAR3, "ssse3", raid_gen3_ssse3);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR4, "ssse3", raid_gen4_ssse3);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR5, "ssse3", raid_gen5_ssse3);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR6, "ssse3", raid_gen6_ssse3);
#ifdef CONFIG_X86_64
		if (!raid_cpu_has_slowextendedreg()) {
			raid_gen_register(RAID_ALGO_CAUCHY_PAR3, "ssse3e", raid_gen3_ssse3ext);
			raid_gen_register(RAID_ALGO_CAUCHY_PAR4, "ssse3e", raid_gen4_ssse3ext);
			raid_gen_register(RAID_ALGO_CAUCHY_PAR5, "ssse3e", raid_gen5_ssse3ext);
			raid_gen_register(RAID_ALGO_CAUCHY_PAR6, "ssse3e", raid_gen6_ssse3ext);
		}
#endif

		raid_rec_register(RAID_ALGO_CAUCHY_PAR1, "ssse3", raid_rec1_ssse3);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR2, "ssse3", raid_rec2_ssse3);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR3, "ssse3", raid_recX_ssse3);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR4, "ssse3", raid_recX_ssse3);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR5, "ssse3", raid_recX_ssse3);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR6, "ssse3", raid_recX_ssse3);
	}
#endif

#ifdef CONFIG_AVX2
	if (raid_cpu_has_avx2()) {
		raid_gen_register(RAID_ALGO_CAUCHY_PAR1, "avx2", raid_gen1_avx2);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR2, "avx2", raid_gen2_avx2);
#ifdef CONFIG_X86_64
		raid_gen_register(RAID_ALGO_CAUCHY_PAR3, "avx2e", raid_gen3_avx2ext);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR4, "avx2e", raid_gen4_avx2ext);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR5, "avx2e", raid_gen5_avx2ext);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR6, "avx2e", raid_gen6_avx2ext);
		raid_gen_register(RAID_ALGO_VANDERMONDE_PAR3, "avx2e", raid_genz_avx2ext);
#endif

		raid_rec_register(RAID_ALGO_CAUCHY_PAR1, "avx2", raid_rec1_avx2);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR2, "avx2", raid_rec2_avx2);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR3, "avx2", raid_recX_avx2);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR4, "avx2", raid_recX_avx2);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR5, "avx2", raid_recX_avx2);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR6, "avx2", raid_recX_avx2);
	}
#endif

#ifdef CONFIG_AVX512BW
#ifdef CONFIG_X86_64
	if (raid_cpu_has_avx512bw()) {
		raid_gen_register(RAID_ALGO_CAUCHY_PAR1, "avx512", raid_gen1_avx512bw);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR2, "avx512", raid_gen2_avx512bw);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR3, "avx512", raid_gen3_avx512bw);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR4, "avx512", raid_gen4_avx512bw);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR5, "avx512", raid_gen5_avx512bw);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR6, "avx512", raid_gen6_avx512bw);

		raid_rec_register(RAID_ALGO_CAUCHY_PAR1, "avx512", raid_rec1_avx512bw);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR2, "avx512", raid_rec2_avx512bw);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR3, "avx512", raid_recX_avx512bw);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR4, "avx512", raid_recX_avx512bw);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR5, "avx512", raid_recX_avx512bw);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR6, "avx512", raid_recX_avx512bw);
	}
#endif
#endif
}
#endif
