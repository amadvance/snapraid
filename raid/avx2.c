// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Andrea Mazzoleni

#include "internal.h"
#include "gf.h"
#include "cpu.h"

#ifdef CONFIG_X86

/*
 * GEN1 (RAID5 with xor) AVX2 implementation
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

	for (i = 0; i < size; i += 64) {
		asm volatile ("vmovdqa %0,%%ymm0" : : "m" (v[0][i]));
		asm volatile ("vmovdqa %0,%%ymm1" : : "m" (v[0][i + 32]));

		for (d = 1; d <= l; ++d) {
			asm volatile ("vpxor %0,%%ymm0,%%ymm0" : : "m" (v[d][i]));
			asm volatile ("vpxor %0,%%ymm1,%%ymm1" : : "m" (v[d][i + 32]));
		}

		asm volatile ("vmovntdq %%ymm0,%0" : "=m" (p[i]));
		asm volatile ("vmovntdq %%ymm1,%0" : "=m" (p[i + 32]));
	}

	raid_avx_end();
}

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

#ifdef CONFIG_X86_64
/*
 * GENz (triple parity with powers of 2^-1) AVX2 implementation
 *
 * Note that it uses 16 registers, meaning that x64 is required.
 */
void raid_genz_avx2ext(int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t**)vv;
	uint8_t *p;
	uint8_t *q;
	uint8_t *r;
	int d, l;
	size_t i;

	l = nd - 1;
	p = v[nd];
	q = v[nd + 1];
	r = v[nd + 2];

	raid_avx_begin();

	asm volatile ("vpbroadcastb %0,%%ymm7" : : "m" (gfconst16.poly[0]));
	asm volatile ("vpbroadcastb %0,%%ymm3" : : "m" (gfconst16.half[0]));
	asm volatile ("vpbroadcastb %0,%%ymm11" : : "m" (gfconst16.low7[0]));
	asm volatile ("vpxor %ymm15,%ymm15,%ymm15");

	for (i = 0; i < size; i += 64) {
		asm volatile ("vmovdqa %0,%%ymm0" : : "m" (v[l][i]));
		asm volatile ("vmovdqa %0,%%ymm8" : : "m" (v[l][i + 32]));
		asm volatile ("vmovdqa %ymm0,%ymm1");
		asm volatile ("vmovdqa %ymm8,%ymm9");
		asm volatile ("vmovdqa %ymm0,%ymm2");
		asm volatile ("vmovdqa %ymm8,%ymm10");
		for (d = l - 1; d >= 0; --d) {
			asm volatile ("vpsllw $7,%ymm2,%ymm6");
			asm volatile ("vpsllw $7,%ymm10,%ymm14");
			asm volatile ("vpsrlw $1,%ymm2,%ymm2");
			asm volatile ("vpsrlw $1,%ymm10,%ymm10");
			asm volatile ("vpcmpgtb %ymm1,%ymm15,%ymm4");
			asm volatile ("vpcmpgtb %ymm9,%ymm15,%ymm12");
			asm volatile ("vpcmpgtb %ymm6,%ymm15,%ymm5");
			asm volatile ("vpcmpgtb %ymm14,%ymm15,%ymm13");
			asm volatile ("vpaddb %ymm1,%ymm1,%ymm1");
			asm volatile ("vpaddb %ymm9,%ymm9,%ymm9");
			asm volatile ("vpand %ymm11,%ymm2,%ymm2");
			asm volatile ("vpand %ymm11,%ymm10,%ymm10");
			asm volatile ("vpand %ymm7,%ymm4,%ymm4");
			asm volatile ("vpand %ymm7,%ymm12,%ymm12");
			asm volatile ("vpand %ymm3,%ymm5,%ymm5");
			asm volatile ("vpand %ymm3,%ymm13,%ymm13");
			asm volatile ("vpxor %ymm4,%ymm1,%ymm1");
			asm volatile ("vpxor %ymm12,%ymm9,%ymm9");
			asm volatile ("vpxor %ymm5,%ymm2,%ymm2");
			asm volatile ("vpxor %ymm13,%ymm10,%ymm10");

			asm volatile ("vmovdqa %0,%%ymm4" : : "m" (v[d][i]));
			asm volatile ("vmovdqa %0,%%ymm12" : : "m" (v[d][i + 32]));
			asm volatile ("vpxor %ymm4,%ymm0,%ymm0");
			asm volatile ("vpxor %ymm4,%ymm1,%ymm1");
			asm volatile ("vpxor %ymm4,%ymm2,%ymm2");
			asm volatile ("vpxor %ymm12,%ymm8,%ymm8");
			asm volatile ("vpxor %ymm12,%ymm9,%ymm9");
			asm volatile ("vpxor %ymm12,%ymm10,%ymm10");
		}
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

#ifdef CONFIG_X86_64
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

		asm volatile ("vbroadcasti128 %0,%%ymm10" : : "m" (gfgenpshufb[l][1][0][0]));
		asm volatile ("vbroadcasti128 %0,%%ymm15" : : "m" (gfgenpshufb[l][1][1][0]));
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

			asm volatile ("vbroadcasti128 %0,%%ymm14" : : "m" (gfgenpshufb[d][1][0][0]));
			asm volatile ("vbroadcasti128 %0,%%ymm15" : : "m" (gfgenpshufb[d][1][1][0]));
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

#ifdef CONFIG_X86_64
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

		asm volatile ("vbroadcasti128 %0,%%ymm10" : : "m" (gfgenpshufb[l][1][0][0]));
		asm volatile ("vbroadcasti128 %0,%%ymm15" : : "m" (gfgenpshufb[l][1][1][0]));
		asm volatile ("vpshufb %ymm4,%ymm10,%ymm2");
		asm volatile ("vpshufb %ymm5,%ymm15,%ymm7");
		asm volatile ("vpshufb %ymm12,%ymm10,%ymm10");
		asm volatile ("vpshufb %ymm13,%ymm15,%ymm15");
		asm volatile ("vpxor   %ymm7,%ymm2,%ymm2");
		asm volatile ("vpxor   %ymm15,%ymm10,%ymm10");

		asm volatile ("vbroadcasti128 %0,%%ymm11" : : "m" (gfgenpshufb[l][2][0][0]));
		asm volatile ("vbroadcasti128 %0,%%ymm15" : : "m" (gfgenpshufb[l][2][1][0]));
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

			asm volatile ("vbroadcasti128 %0,%%ymm14" : : "m" (gfgenpshufb[d][1][0][0]));
			asm volatile ("vbroadcasti128 %0,%%ymm15" : : "m" (gfgenpshufb[d][1][1][0]));
			asm volatile ("vpshufb %ymm4,%ymm14,%ymm6");
			asm volatile ("vpshufb %ymm5,%ymm15,%ymm7");
			asm volatile ("vpshufb %ymm12,%ymm14,%ymm14");
			asm volatile ("vpshufb %ymm13,%ymm15,%ymm15");
			asm volatile ("vpxor   %ymm6,%ymm2,%ymm2");
			asm volatile ("vpxor   %ymm14,%ymm10,%ymm10");
			asm volatile ("vpxor   %ymm7,%ymm2,%ymm2");
			asm volatile ("vpxor   %ymm15,%ymm10,%ymm10");

			asm volatile ("vbroadcasti128 %0,%%ymm14" : : "m" (gfgenpshufb[d][2][0][0]));
			asm volatile ("vbroadcasti128 %0,%%ymm15" : : "m" (gfgenpshufb[d][2][1][0]));
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

#ifdef CONFIG_X86_64
/*
 * GENX AVX2EXT implementation
 */
static __always_inline void raid_genX_avx2ext(int nd, size_t size, void **vv, int np)
{
	uint8_t **v = (uint8_t **)vv;
	size_t i;
	int d, l;

	l = nd - 1;

	/* special case with only one data disk */
	if (l == 0) {
		for (d = 0; d < np; ++d)
			memcpy(v[1 + d], v[0], size);
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

		if (np >= 3) {
			asm volatile ("vbroadcasti128 %0,%%ymm2" : : "m" (gfgenpshufb[l][1][0][0]));
			asm volatile ("vbroadcasti128 %0,%%ymm13" : : "m" (gfgenpshufb[l][1][1][0]));
			asm volatile ("vpshufb %ymm10,%ymm2,%ymm2");
			asm volatile ("vpshufb %ymm11,%ymm13,%ymm13");
			asm volatile ("vpxor   %ymm13,%ymm2,%ymm2");
		}

		if (np >= 4) {
			asm volatile ("vbroadcasti128 %0,%%ymm3" : : "m" (gfgenpshufb[l][2][0][0]));
			asm volatile ("vbroadcasti128 %0,%%ymm13" : : "m" (gfgenpshufb[l][2][1][0]));
			asm volatile ("vpshufb %ymm10,%ymm3,%ymm3");
			asm volatile ("vpshufb %ymm11,%ymm13,%ymm13");
			asm volatile ("vpxor   %ymm13,%ymm3,%ymm3");
		}

		if (np >= 5) {
			asm volatile ("vbroadcasti128 %0,%%ymm4" : : "m" (gfgenpshufb[l][3][0][0]));
			asm volatile ("vbroadcasti128 %0,%%ymm13" : : "m" (gfgenpshufb[l][3][1][0]));
			asm volatile ("vpshufb %ymm10,%ymm4,%ymm4");
			asm volatile ("vpshufb %ymm11,%ymm13,%ymm13");
			asm volatile ("vpxor   %ymm13,%ymm4,%ymm4");
		}

		if (np >= 6) {
			asm volatile ("vbroadcasti128 %0,%%ymm5" : : "m" (gfgenpshufb[l][4][0][0]));
			asm volatile ("vbroadcasti128 %0,%%ymm13" : : "m" (gfgenpshufb[l][4][1][0]));
			asm volatile ("vpshufb %ymm10,%ymm5,%ymm5");
			asm volatile ("vpshufb %ymm11,%ymm13,%ymm13");
			asm volatile ("vpxor   %ymm13,%ymm5,%ymm5");
		}

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

			if (np >= 3) {
				asm volatile ("vbroadcasti128 %0,%%ymm12" : : "m" (gfgenpshufb[d][1][0][0]));
				asm volatile ("vbroadcasti128 %0,%%ymm13" : : "m" (gfgenpshufb[d][1][1][0]));
				asm volatile ("vpshufb %ymm10,%ymm12,%ymm12");
				asm volatile ("vpshufb %ymm11,%ymm13,%ymm13");
				asm volatile ("vpxor   %ymm12,%ymm2,%ymm2");
				asm volatile ("vpxor   %ymm13,%ymm2,%ymm2");
			}

			if (np >= 4) {
				asm volatile ("vbroadcasti128 %0,%%ymm12" : : "m" (gfgenpshufb[d][2][0][0]));
				asm volatile ("vbroadcasti128 %0,%%ymm13" : : "m" (gfgenpshufb[d][2][1][0]));
				asm volatile ("vpshufb %ymm10,%ymm12,%ymm12");
				asm volatile ("vpshufb %ymm11,%ymm13,%ymm13");
				asm volatile ("vpxor   %ymm12,%ymm3,%ymm3");
				asm volatile ("vpxor   %ymm13,%ymm3,%ymm3");
			}

			if (np >= 5) {
				asm volatile ("vbroadcasti128 %0,%%ymm12" : : "m" (gfgenpshufb[d][3][0][0]));
				asm volatile ("vbroadcasti128 %0,%%ymm13" : : "m" (gfgenpshufb[d][3][1][0]));
				asm volatile ("vpshufb %ymm10,%ymm12,%ymm12");
				asm volatile ("vpshufb %ymm11,%ymm13,%ymm13");
				asm volatile ("vpxor   %ymm12,%ymm4,%ymm4");
				asm volatile ("vpxor   %ymm13,%ymm4,%ymm4");
			}

			if (np >= 6) {
				asm volatile ("vbroadcasti128 %0,%%ymm12" : : "m" (gfgenpshufb[d][4][0][0]));
				asm volatile ("vbroadcasti128 %0,%%ymm13" : : "m" (gfgenpshufb[d][4][1][0]));
				asm volatile ("vpshufb %ymm10,%ymm12,%ymm12");
				asm volatile ("vpshufb %ymm11,%ymm13,%ymm13");
				asm volatile ("vpxor   %ymm12,%ymm5,%ymm5");
				asm volatile ("vpxor   %ymm13,%ymm5,%ymm5");
			}
		}

		/* first disk with all coefficients at 1 */
		asm volatile ("vmovdqa %0,%%ymm10" : : "m" (v[0][i]));

		asm volatile ("vpcmpgtb %ymm1,%ymm6,%ymm11");
		asm volatile ("vpaddb %ymm1,%ymm1,%ymm1");
		asm volatile ("vpand %ymm14,%ymm11,%ymm11");
		asm volatile ("vpxor %ymm11,%ymm1,%ymm1");

		asm volatile ("vpxor %ymm10,%ymm0,%ymm0");
		asm volatile ("vpxor %ymm10,%ymm1,%ymm1");
		if (np >= 3)
			asm volatile ("vpxor %ymm10,%ymm2,%ymm2");
		if (np >= 4)
			asm volatile ("vpxor %ymm10,%ymm3,%ymm3");
		if (np >= 5)
			asm volatile ("vpxor %ymm10,%ymm4,%ymm4");
		if (np >= 6)
			asm volatile ("vpxor %ymm10,%ymm5,%ymm5");

		asm volatile ("vmovntdq %%ymm0,%0" : "=m" (v[nd][i]));
		asm volatile ("vmovntdq %%ymm1,%0" : "=m" (v[nd + 1][i]));
		if (np >= 3)
			asm volatile ("vmovntdq %%ymm2,%0" : "=m" (v[nd + 2][i]));
		if (np >= 4)
			asm volatile ("vmovntdq %%ymm3,%0" : "=m" (v[nd + 3][i]));
		if (np >= 5)
			asm volatile ("vmovntdq %%ymm4,%0" : "=m" (v[nd + 4][i]));
		if (np >= 6)
			asm volatile ("vmovntdq %%ymm5,%0" : "=m" (v[nd + 5][i]));
	}

	raid_avx_end();
}
#endif

#ifdef CONFIG_X86_64
/*
 * GEN5 (penta parity with Cauchy matrix) AVX2 implementation
 *
 * Note that it uses 16 registers, meaning that x64 is required.
 */
void raid_gen5_avx2ext(int nd, size_t size, void **vv)
{
	raid_genX_avx2ext(nd, size, vv, 5);
}
#endif

#ifdef CONFIG_X86_64
/*
 * GEN6 (hexa parity with Cauchy matrix) AVX2 implementation
 *
 * Note that it uses 16 registers, meaning that x64 is required.
 */
void raid_gen6_avx2ext(int nd, size_t size, void **vv)
{
	raid_genX_avx2ext(nd, size, vv, 6);
}
#endif

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

void raid_register_avx2(void)
{
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
}
#endif
