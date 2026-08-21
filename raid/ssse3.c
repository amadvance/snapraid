// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2013 Andrea Mazzoleni

#include "internal.h"
#include "gf.h"
#include "cpu.h"

#ifdef CONFIG_X86
/*
 * GEN3 (triple parity with Cauchy matrix) SSSE3 implementation
 */
static __always_inline void raid_gen3_ssse3_gen(int nd, size_t size, void **vv, int g23)
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
	int g23_start = raid_g23_count(l - 1);

	raid_sse_begin();

	/* generic case with at least two data disks */
	asm volatile ("movdqa %0,%%xmm3" : : "m" (gfconst16.poly[0]));
	asm volatile ("movdqa %0,%%xmm7" : : "m" (gfconst16.low4[0]));

	for (i = 0; i < size; i += 16) {
		int g23_count = g23_start;
		int g23_x3;

		/* last disk without the generator multiplication */
		asm volatile ("movdqa %0,%%xmm4" : : "m" (v[l][i]));

		asm volatile ("movdqa %xmm4,%xmm0");
		asm volatile ("movdqa %xmm4,%xmm1");

		asm volatile ("movdqa %xmm4,%xmm5");
		asm volatile ("psrlw  $4,%xmm5");
		asm volatile ("pand   %xmm7,%xmm4");
		asm volatile ("pand   %xmm7,%xmm5");

		asm volatile ("movdqa %0,%%xmm2" : : "m" (raid_gfcauchypshufb[l][1][0][0]));
		asm volatile ("movdqa %0,%%xmm6" : : "m" (raid_gfcauchypshufb[l][1][1][0]));
		asm volatile ("pshufb %xmm4,%xmm2");
		asm volatile ("pshufb %xmm5,%xmm6");
		asm volatile ("pxor   %xmm6,%xmm2");

		/* intermediate disks */
		for (d = l - 1; d > 0; --d) {
			g23_x3 = 0;
			if (g23) {
				g23_x3 = --g23_count == 0;
				if (g23_x3)
					g23_count = 51;
			}

			asm volatile ("movdqa %0,%%xmm4" : : "m" (v[d][i]));

			if (g23 && g23_x3)
				asm volatile ("movdqa %xmm1,%xmm6");
			asm volatile ("pxor %xmm5,%xmm5");
			asm volatile ("pcmpgtb %xmm1,%xmm5");
			asm volatile ("paddb %xmm1,%xmm1");
			asm volatile ("pand %xmm3,%xmm5");
			asm volatile ("pxor %xmm5,%xmm1");
			if (g23 && g23_x3)
				asm volatile ("pxor %xmm6,%xmm1");

			asm volatile ("pxor %xmm4,%xmm0");
			asm volatile ("pxor %xmm4,%xmm1");

			asm volatile ("movdqa %xmm4,%xmm5");
			asm volatile ("psrlw  $4,%xmm5");
			asm volatile ("pand   %xmm7,%xmm4");
			asm volatile ("pand   %xmm7,%xmm5");

			asm volatile ("movdqa %0,%%xmm6" : : "m" (raid_gfcauchypshufb[d][1][0][0]));
			asm volatile ("pshufb %xmm4,%xmm6");
			asm volatile ("pxor   %xmm6,%xmm2");
			asm volatile ("movdqa %0,%%xmm6" : : "m" (raid_gfcauchypshufb[d][1][1][0]));
			asm volatile ("pshufb %xmm5,%xmm6");
			asm volatile ("pxor   %xmm6,%xmm2");
		}

		/* first disk with all coefficients at 1 */
		asm volatile ("movdqa %0,%%xmm4" : : "m" (v[0][i]));

		g23_x3 = 0;
		if (g23) {
			g23_x3 = --g23_count == 0;
			if (g23_x3)
				g23_count = 51;
		}

		if (g23 && g23_x3)
			asm volatile ("movdqa %xmm1,%xmm6");
		asm volatile ("pxor %xmm5,%xmm5");
		asm volatile ("pcmpgtb %xmm1,%xmm5");
		asm volatile ("paddb %xmm1,%xmm1");
		asm volatile ("pand %xmm3,%xmm5");
		asm volatile ("pxor %xmm5,%xmm1");
		if (g23 && g23_x3)
			asm volatile ("pxor %xmm6,%xmm1");

		asm volatile ("pxor %xmm4,%xmm0");
		asm volatile ("pxor %xmm4,%xmm1");
		asm volatile ("pxor %xmm4,%xmm2");

		asm volatile ("movntdq %%xmm0,%0" : "=m" (p[i]));
		asm volatile ("movntdq %%xmm1,%0" : "=m" (q[i]));
		asm volatile ("movntdq %%xmm2,%0" : "=m" (r[i]));
	}

	raid_sse_end();
}

void raid_gen3_ssse3_raid(int nd, size_t size, void **vv)
{
	raid_gen3_ssse3_gen(nd, size, vv, 0);
}

void raid_gen3_ssse3_aes(int nd, size_t size, void **vv)
{
	raid_gen3_ssse3_gen(nd, size, vv, 1);
}

#ifdef CONFIG_X86_64
/*
 * GEN3 (triple parity with Cauchy matrix) SSSE3 implementation
 *
 * Note that it uses 16 registers, meaning that x64 is required.
 */
static __always_inline void raid_gen3_ssse3ext_gen(int nd, size_t size, void **vv, int g23)
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
	int g23_start = raid_g23_count(l - 1);

	raid_sse_begin();

	/* generic case with at least two data disks */
	asm volatile ("movdqa %0,%%xmm3" : : "m" (gfconst16.poly[0]));
	asm volatile ("movdqa %0,%%xmm11" : : "m" (gfconst16.low4[0]));

	for (i = 0; i < size; i += 32) {
		int g23_count = g23_start;
		int g23_x3_d;
		int g23_x3_d1;

		/* last disk without the generator multiplication */
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

		asm volatile ("movdqa %0,%%xmm2" : : "m" (raid_gfcauchypshufb[l][1][0][0]));
		asm volatile ("movdqa %0,%%xmm7" : : "m" (raid_gfcauchypshufb[l][1][1][0]));
		asm volatile ("movdqa %xmm2,%xmm10");
		asm volatile ("movdqa %xmm7,%xmm15");
		asm volatile ("pshufb %xmm4,%xmm2");
		asm volatile ("pshufb %xmm12,%xmm10");
		asm volatile ("pshufb %xmm5,%xmm7");
		asm volatile ("pshufb %xmm13,%xmm15");
		asm volatile ("pxor   %xmm7,%xmm2");
		asm volatile ("pxor   %xmm15,%xmm10");

		/* process two intermediate disks per iteration */
		for (d = l - 1; d > 1; d -= 2) {
			g23_x3_d = 0;
			g23_x3_d1 = 0;
			if (g23) {
				g23_x3_d = --g23_count == 0;
				if (g23_x3_d)
					g23_count = 51;
				g23_x3_d1 = --g23_count == 0;
				if (g23_x3_d1)
					g23_count = 51;
			}

			/* disk d */
			asm volatile ("movdqa %0,%%xmm4" : : "m" (v[d][i]));
			asm volatile ("movdqa %0,%%xmm12" : : "m" (v[d][i + 16]));

			asm volatile ("movdqa %0,%%xmm7" : : "m" (raid_gfcauchypshufb[d][1][0][0]));
			asm volatile ("movdqa %0,%%xmm15" : : "m" (raid_gfcauchypshufb[d][1][1][0]));

			if (g23 && g23_x3_d) {
				asm volatile ("movdqa %xmm1,%xmm6");
				asm volatile ("movdqa %xmm9,%xmm14");
			}
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
			if (g23 && g23_x3_d) {
				asm volatile ("pxor %xmm6,%xmm1");
				asm volatile ("pxor %xmm14,%xmm9");
			}

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

			asm volatile ("movdqa %xmm7,%xmm6");
			asm volatile ("movdqa %xmm15,%xmm14");
			asm volatile ("pshufb %xmm4,%xmm7");
			asm volatile ("pshufb %xmm12,%xmm6");
			asm volatile ("pshufb %xmm5,%xmm15");
			asm volatile ("pshufb %xmm13,%xmm14");
			asm volatile ("pxor   %xmm7,%xmm2");
			asm volatile ("pxor   %xmm6,%xmm10");
			asm volatile ("pxor   %xmm15,%xmm2");
			asm volatile ("pxor   %xmm14,%xmm10");

			asm volatile ("movdqa %0,%%xmm4" : : "m" (v[d - 1][i]));
			asm volatile ("movdqa %0,%%xmm12" : : "m" (v[d - 1][i + 16]));

			asm volatile ("movdqa %0,%%xmm7" : : "m" (raid_gfcauchypshufb[d - 1][1][0][0]));
			asm volatile ("movdqa %0,%%xmm15" : : "m" (raid_gfcauchypshufb[d - 1][1][1][0]));

			if (g23 && g23_x3_d1) {
				asm volatile ("movdqa %xmm1,%xmm6");
				asm volatile ("movdqa %xmm9,%xmm14");
			}
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
			if (g23 && g23_x3_d1) {
				asm volatile ("pxor %xmm6,%xmm1");
				asm volatile ("pxor %xmm14,%xmm9");
			}

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

			asm volatile ("movdqa %xmm7,%xmm6");
			asm volatile ("movdqa %xmm15,%xmm14");
			asm volatile ("pshufb %xmm4,%xmm7");
			asm volatile ("pshufb %xmm12,%xmm6");
			asm volatile ("pshufb %xmm5,%xmm15");
			asm volatile ("pshufb %xmm13,%xmm14");
			asm volatile ("pxor   %xmm7,%xmm2");
			asm volatile ("pxor   %xmm6,%xmm10");
			asm volatile ("pxor   %xmm15,%xmm2");
			asm volatile ("pxor   %xmm14,%xmm10");
		}

		if (d == 1) {
			g23_x3_d = 0;
			if (g23) {
				g23_x3_d = --g23_count == 0;
				if (g23_x3_d)
					g23_count = 51;
			}

			asm volatile ("movdqa %0,%%xmm4" : : "m" (v[1][i]));
			asm volatile ("movdqa %0,%%xmm12" : : "m" (v[1][i + 16]));

			if (g23 && g23_x3_d) {
				asm volatile ("movdqa %xmm1,%xmm6");
				asm volatile ("movdqa %xmm9,%xmm14");
			}
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
			if (g23 && g23_x3_d) {
				asm volatile ("pxor %xmm6,%xmm1");
				asm volatile ("pxor %xmm14,%xmm9");
			}

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

			asm volatile ("movdqa %0,%%xmm6" : : "m" (raid_gfcauchypshufb[1][1][0][0]));
			asm volatile ("movdqa %0,%%xmm7" : : "m" (raid_gfcauchypshufb[1][1][1][0]));
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

		g23_x3_d = 0;
		if (g23) {
			g23_x3_d = --g23_count == 0;
			if (g23_x3_d)
				g23_count = 51;
		}

		if (g23 && g23_x3_d) {
			asm volatile ("movdqa %xmm1,%xmm6");
			asm volatile ("movdqa %xmm9,%xmm14");
		}
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
		if (g23 && g23_x3_d) {
			asm volatile ("pxor %xmm6,%xmm1");
			asm volatile ("pxor %xmm14,%xmm9");
		}

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

void raid_gen3_ssse3ext_raid(int nd, size_t size, void **vv)
{
	raid_gen3_ssse3ext_gen(nd, size, vv, 0);
}

void raid_gen3_ssse3ext_aes(int nd, size_t size, void **vv)
{
	raid_gen3_ssse3ext_gen(nd, size, vv, 1);
}
#endif

/*
 * GEN4 (quad parity with Cauchy matrix) SSSE3 implementation
 */
static __always_inline void raid_gen4_ssse3_gen(int nd, size_t size, void **vv, int g23)
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
	int g23_start = raid_g23_count(l - 1);

	raid_sse_begin();

	/* generic case with at least two data disks */
	for (i = 0; i < size; i += 16) {
		int g23_count = g23_start;
		int g23_x3;

		/* last disk without the generator multiplication */
		asm volatile ("movdqa %0,%%xmm7" : : "m" (gfconst16.low4[0]));
		asm volatile ("movdqa %0,%%xmm4" : : "m" (v[l][i]));

		asm volatile ("movdqa %xmm4,%xmm0");
		asm volatile ("movdqa %xmm4,%xmm1");

		asm volatile ("movdqa %xmm4,%xmm5");
		asm volatile ("psrlw  $4,%xmm5");
		asm volatile ("pand   %xmm7,%xmm4");
		asm volatile ("pand   %xmm7,%xmm5");

		asm volatile ("movdqa %0,%%xmm2" : : "m" (raid_gfcauchypshufb[l][1][0][0]));
		asm volatile ("movdqa %0,%%xmm7" : : "m" (raid_gfcauchypshufb[l][1][1][0]));
		asm volatile ("pshufb %xmm4,%xmm2");
		asm volatile ("pshufb %xmm5,%xmm7");
		asm volatile ("pxor   %xmm7,%xmm2");

		asm volatile ("movdqa %0,%%xmm3" : : "m" (raid_gfcauchypshufb[l][2][0][0]));
		asm volatile ("movdqa %0,%%xmm7" : : "m" (raid_gfcauchypshufb[l][2][1][0]));
		asm volatile ("pshufb %xmm4,%xmm3");
		asm volatile ("pshufb %xmm5,%xmm7");
		asm volatile ("pxor   %xmm7,%xmm3");

		/* intermediate disks */
		for (d = l - 1; d > 0; --d) {
			g23_x3 = 0;
			if (g23) {
				g23_x3 = --g23_count == 0;
				if (g23_x3)
					g23_count = 51;
			}

			asm volatile ("movdqa %0,%%xmm7" : : "m" (gfconst16.poly[0]));
			asm volatile ("movdqa %0,%%xmm4" : : "m" (v[d][i]));

			if (g23 && g23_x3)
				asm volatile ("movdqa %xmm1,%xmm6");
			asm volatile ("pxor %xmm5,%xmm5");
			asm volatile ("pcmpgtb %xmm1,%xmm5");
			asm volatile ("paddb %xmm1,%xmm1");
			asm volatile ("pand %xmm7,%xmm5");
			asm volatile ("pxor %xmm5,%xmm1");
			if (g23 && g23_x3)
				asm volatile ("pxor %xmm6,%xmm1");

			asm volatile ("movdqa %0,%%xmm7" : : "m" (gfconst16.low4[0]));

			asm volatile ("pxor %xmm4,%xmm0");
			asm volatile ("pxor %xmm4,%xmm1");

			asm volatile ("movdqa %xmm4,%xmm5");
			asm volatile ("psrlw  $4,%xmm5");
			asm volatile ("pand   %xmm7,%xmm4");
			asm volatile ("pand   %xmm7,%xmm5");

			asm volatile ("movdqa %0,%%xmm6" : : "m" (raid_gfcauchypshufb[d][1][0][0]));
			asm volatile ("movdqa %0,%%xmm7" : : "m" (raid_gfcauchypshufb[d][1][1][0]));
			asm volatile ("pshufb %xmm4,%xmm6");
			asm volatile ("pshufb %xmm5,%xmm7");
			asm volatile ("pxor   %xmm6,%xmm2");
			asm volatile ("pxor   %xmm7,%xmm2");

			asm volatile ("movdqa %0,%%xmm6" : : "m" (raid_gfcauchypshufb[d][2][0][0]));
			asm volatile ("movdqa %0,%%xmm7" : : "m" (raid_gfcauchypshufb[d][2][1][0]));
			asm volatile ("pshufb %xmm4,%xmm6");
			asm volatile ("pshufb %xmm5,%xmm7");
			asm volatile ("pxor   %xmm6,%xmm3");
			asm volatile ("pxor   %xmm7,%xmm3");
		}

		/* first disk with all coefficients at 1 */
		asm volatile ("movdqa %0,%%xmm7" : : "m" (gfconst16.poly[0]));
		asm volatile ("movdqa %0,%%xmm4" : : "m" (v[0][i]));

		g23_x3 = 0;
		if (g23) {
			g23_x3 = --g23_count == 0;
			if (g23_x3)
				g23_count = 51;
		}

		if (g23 && g23_x3)
			asm volatile ("movdqa %xmm1,%xmm6");
		asm volatile ("pxor %xmm5,%xmm5");
		asm volatile ("pcmpgtb %xmm1,%xmm5");
		asm volatile ("paddb %xmm1,%xmm1");
		asm volatile ("pand %xmm7,%xmm5");
		asm volatile ("pxor %xmm5,%xmm1");
		if (g23 && g23_x3)
			asm volatile ("pxor %xmm6,%xmm1");

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

void raid_gen4_ssse3_raid(int nd, size_t size, void **vv)
{
	raid_gen4_ssse3_gen(nd, size, vv, 0);
}

void raid_gen4_ssse3_aes(int nd, size_t size, void **vv)
{
	raid_gen4_ssse3_gen(nd, size, vv, 1);
}

#ifdef CONFIG_X86_64
/*
 * GEN4 (quad parity with Cauchy matrix) SSSE3 implementation
 *
 * Note that it uses 16 registers, meaning that x64 is required.
 */
static __always_inline void raid_gen4_ssse3ext_gen(int nd, size_t size, void **vv, int g23)
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
	int g23_start = raid_g23_count(l - 1);

	raid_sse_begin();

	/* generic case with at least two data disks */
	for (i = 0; i < size; i += 32) {
		int g23_count = g23_start;
		int g23_x3;

		/* last disk without the generator multiplication */
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

		asm volatile ("movdqa %0,%%xmm2" : : "m" (raid_gfcauchypshufb[l][1][0][0]));
		asm volatile ("movdqa %0,%%xmm7" : : "m" (raid_gfcauchypshufb[l][1][1][0]));
		asm volatile ("movdqa %xmm2,%xmm10");
		asm volatile ("movdqa %xmm7,%xmm15");
		asm volatile ("pshufb %xmm4,%xmm2");
		asm volatile ("pshufb %xmm12,%xmm10");
		asm volatile ("pshufb %xmm5,%xmm7");
		asm volatile ("pshufb %xmm13,%xmm15");
		asm volatile ("pxor   %xmm7,%xmm2");
		asm volatile ("pxor   %xmm15,%xmm10");

		asm volatile ("movdqa %0,%%xmm3" : : "m" (raid_gfcauchypshufb[l][2][0][0]));
		asm volatile ("movdqa %0,%%xmm7" : : "m" (raid_gfcauchypshufb[l][2][1][0]));
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
			g23_x3 = 0;
			if (g23) {
				g23_x3 = --g23_count == 0;
				if (g23_x3)
					g23_count = 51;
			}

			asm volatile ("movdqa %0,%%xmm7" : : "m" (gfconst16.poly[0]));
			asm volatile ("movdqa %0,%%xmm15" : : "m" (gfconst16.low4[0]));
			asm volatile ("movdqa %0,%%xmm4" : : "m" (v[d][i]));
			asm volatile ("movdqa %0,%%xmm12" : : "m" (v[d][i + 16]));

			if (g23 && g23_x3) {
				asm volatile ("movdqa %xmm1,%xmm6");
				asm volatile ("movdqa %xmm9,%xmm14");
			}
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
			if (g23 && g23_x3) {
				asm volatile ("pxor %xmm6,%xmm1");
				asm volatile ("pxor %xmm14,%xmm9");
			}

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

			asm volatile ("movdqa %0,%%xmm6" : : "m" (raid_gfcauchypshufb[d][1][0][0]));
			asm volatile ("movdqa %0,%%xmm7" : : "m" (raid_gfcauchypshufb[d][1][1][0]));
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

			asm volatile ("movdqa %0,%%xmm6" : : "m" (raid_gfcauchypshufb[d][2][0][0]));
			asm volatile ("movdqa %0,%%xmm7" : : "m" (raid_gfcauchypshufb[d][2][1][0]));
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

		g23_x3 = 0;
		if (g23) {
			g23_x3 = --g23_count == 0;
			if (g23_x3)
				g23_count = 51;
		}

		if (g23 && g23_x3) {
			asm volatile ("movdqa %xmm1,%xmm6");
			asm volatile ("movdqa %xmm9,%xmm14");
		}
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
		if (g23 && g23_x3) {
			asm volatile ("pxor %xmm6,%xmm1");
			asm volatile ("pxor %xmm14,%xmm9");
		}

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

void raid_gen4_ssse3ext_raid(int nd, size_t size, void **vv)
{
	raid_gen4_ssse3ext_gen(nd, size, vv, 0);
}

void raid_gen4_ssse3ext_aes(int nd, size_t size, void **vv)
{
	raid_gen4_ssse3ext_gen(nd, size, vv, 1);
}
#endif

/*
 * GEN5 (penta parity with Cauchy matrix) SSSE3 implementation
 */
void raid_gen5_ssse3_raid(int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *q;
	uint8_t *r;
	uint8_t *s;
	uint8_t *t;
	int d, l;
	size_t i;
	uint8_t buffer[16 + 16];
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
		/* last disk without the generator multiplication */
		asm volatile ("movdqa %0,%%xmm4" : : "m" (v[l][i]));

		asm volatile ("movdqa %xmm4,%xmm0");
		asm volatile ("movdqa %%xmm4,%0" : "=m" (pd[0]));

		asm volatile ("movdqa %0,%%xmm7" : : "m" (gfconst16.low4[0]));
		asm volatile ("movdqa %xmm4,%xmm5");
		asm volatile ("psrlw  $4,%xmm5");
		asm volatile ("pand   %xmm7,%xmm4");
		asm volatile ("pand   %xmm7,%xmm5");

		asm volatile ("movdqa %0,%%xmm1" : : "m" (raid_gfcauchypshufb[l][1][0][0]));
		asm volatile ("movdqa %0,%%xmm7" : : "m" (raid_gfcauchypshufb[l][1][1][0]));
		asm volatile ("pshufb %xmm4,%xmm1");
		asm volatile ("pshufb %xmm5,%xmm7");
		asm volatile ("pxor   %xmm7,%xmm1");

		asm volatile ("movdqa %0,%%xmm2" : : "m" (raid_gfcauchypshufb[l][2][0][0]));
		asm volatile ("movdqa %0,%%xmm7" : : "m" (raid_gfcauchypshufb[l][2][1][0]));
		asm volatile ("pshufb %xmm4,%xmm2");
		asm volatile ("pshufb %xmm5,%xmm7");
		asm volatile ("pxor   %xmm7,%xmm2");

		asm volatile ("movdqa %0,%%xmm3" : : "m" (raid_gfcauchypshufb[l][3][0][0]));
		asm volatile ("movdqa %0,%%xmm7" : : "m" (raid_gfcauchypshufb[l][3][1][0]));
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

			asm volatile ("movdqa %0,%%xmm6" : : "m" (raid_gfcauchypshufb[d][1][0][0]));
			asm volatile ("movdqa %0,%%xmm7" : : "m" (raid_gfcauchypshufb[d][1][1][0]));
			asm volatile ("pshufb %xmm4,%xmm6");
			asm volatile ("pshufb %xmm5,%xmm7");
			asm volatile ("pxor   %xmm6,%xmm1");
			asm volatile ("pxor   %xmm7,%xmm1");

			asm volatile ("movdqa %0,%%xmm6" : : "m" (raid_gfcauchypshufb[d][2][0][0]));
			asm volatile ("movdqa %0,%%xmm7" : : "m" (raid_gfcauchypshufb[d][2][1][0]));
			asm volatile ("pshufb %xmm4,%xmm6");
			asm volatile ("pshufb %xmm5,%xmm7");
			asm volatile ("pxor   %xmm6,%xmm2");
			asm volatile ("pxor   %xmm7,%xmm2");

			asm volatile ("movdqa %0,%%xmm6" : : "m" (raid_gfcauchypshufb[d][3][0][0]));
			asm volatile ("movdqa %0,%%xmm7" : : "m" (raid_gfcauchypshufb[d][3][1][0]));
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

#ifdef CONFIG_X86_64
/*
 * GENX SSSE3ext implementation
 */
static __always_inline void raid_genX_ssse3ext(int nd, size_t size, void **vv, int np, int g23)
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
	if (np >= 6)
		u = v[nd + 5];

	/* special case with only one data disk */
	if (l == 0) {
		for (d = 0; d < np; ++d)
			memcpy(v[1 + d], v[0], size);
		return;
	}
	int g23_start = raid_g23_count(l - 1);

	raid_sse_begin();

	/* generic case with at least two data disks */
	asm volatile ("movdqa %0,%%xmm14" : : "m" (gfconst16.poly[0]));
	asm volatile ("movdqa %0,%%xmm15" : : "m" (gfconst16.low4[0]));

	for (i = 0; i < size; i += 16) {
		int g23_count = g23_start;
		int g23_x3;

		/* last disk without the generator multiplication */
		asm volatile ("movdqa %0,%%xmm10" : : "m" (v[l][i]));

		asm volatile ("movdqa %xmm10,%xmm0");
		asm volatile ("movdqa %xmm10,%xmm1");

		asm volatile ("movdqa %xmm10,%xmm11");
		asm volatile ("psrlw  $4,%xmm11");
		asm volatile ("pand   %xmm15,%xmm10");
		asm volatile ("pand   %xmm15,%xmm11");

		if (np >= 3) {
			asm volatile ("movdqa %0,%%xmm2" : : "m" (raid_gfcauchypshufb[l][1][0][0]));
			asm volatile ("movdqa %0,%%xmm13" : : "m" (raid_gfcauchypshufb[l][1][1][0]));
			asm volatile ("pshufb %xmm10,%xmm2");
			asm volatile ("pshufb %xmm11,%xmm13");
			asm volatile ("pxor   %xmm13,%xmm2");
		}

		if (np >= 4) {
			asm volatile ("movdqa %0,%%xmm3" : : "m" (raid_gfcauchypshufb[l][2][0][0]));
			asm volatile ("movdqa %0,%%xmm13" : : "m" (raid_gfcauchypshufb[l][2][1][0]));
			asm volatile ("pshufb %xmm10,%xmm3");
			asm volatile ("pshufb %xmm11,%xmm13");
			asm volatile ("pxor   %xmm13,%xmm3");
		}

		if (np >= 5) {
			asm volatile ("movdqa %0,%%xmm4" : : "m" (raid_gfcauchypshufb[l][3][0][0]));
			asm volatile ("movdqa %0,%%xmm13" : : "m" (raid_gfcauchypshufb[l][3][1][0]));
			asm volatile ("pshufb %xmm10,%xmm4");
			asm volatile ("pshufb %xmm11,%xmm13");
			asm volatile ("pxor   %xmm13,%xmm4");
		}

		if (np >= 6) {
			asm volatile ("movdqa %0,%%xmm5" : : "m" (raid_gfcauchypshufb[l][4][0][0]));
			asm volatile ("movdqa %0,%%xmm13" : : "m" (raid_gfcauchypshufb[l][4][1][0]));
			asm volatile ("pshufb %xmm10,%xmm5");
			asm volatile ("pshufb %xmm11,%xmm13");
			asm volatile ("pxor   %xmm13,%xmm5");
		}

		/* intermediate disks */
		for (d = l - 1; d > 0; --d) {
			g23_x3 = 0;
			if (g23) {
				g23_x3 = --g23_count == 0;
				if (g23_x3)
					g23_count = 51;
			}

			asm volatile ("movdqa %0,%%xmm10" : : "m" (v[d][i]));

			if (g23 && g23_x3)
				asm volatile ("movdqa %xmm1,%xmm12");
			asm volatile ("pxor %xmm11,%xmm11");
			asm volatile ("pcmpgtb %xmm1,%xmm11");
			asm volatile ("paddb %xmm1,%xmm1");
			asm volatile ("pand %xmm14,%xmm11");
			asm volatile ("pxor %xmm11,%xmm1");
			if (g23 && g23_x3)
				asm volatile ("pxor %xmm12,%xmm1");

			asm volatile ("pxor %xmm10,%xmm0");
			asm volatile ("pxor %xmm10,%xmm1");

			asm volatile ("movdqa %xmm10,%xmm11");
			asm volatile ("psrlw  $4,%xmm11");
			asm volatile ("pand   %xmm15,%xmm10");
			asm volatile ("pand   %xmm15,%xmm11");

			if (np >= 3) {
				asm volatile ("movdqa %0,%%xmm12" : : "m" (raid_gfcauchypshufb[d][1][0][0]));
				asm volatile ("movdqa %0,%%xmm13" : : "m" (raid_gfcauchypshufb[d][1][1][0]));
				asm volatile ("pshufb %xmm10,%xmm12");
				asm volatile ("pshufb %xmm11,%xmm13");
				asm volatile ("pxor   %xmm12,%xmm2");
				asm volatile ("pxor   %xmm13,%xmm2");
			}

			if (np >= 4) {
				asm volatile ("movdqa %0,%%xmm12" : : "m" (raid_gfcauchypshufb[d][2][0][0]));
				asm volatile ("movdqa %0,%%xmm13" : : "m" (raid_gfcauchypshufb[d][2][1][0]));
				asm volatile ("pshufb %xmm10,%xmm12");
				asm volatile ("pshufb %xmm11,%xmm13");
				asm volatile ("pxor   %xmm12,%xmm3");
				asm volatile ("pxor   %xmm13,%xmm3");
			}

			if (np >= 5) {
				asm volatile ("movdqa %0,%%xmm12" : : "m" (raid_gfcauchypshufb[d][3][0][0]));
				asm volatile ("movdqa %0,%%xmm13" : : "m" (raid_gfcauchypshufb[d][3][1][0]));
				asm volatile ("pshufb %xmm10,%xmm12");
				asm volatile ("pshufb %xmm11,%xmm13");
				asm volatile ("pxor   %xmm12,%xmm4");
				asm volatile ("pxor   %xmm13,%xmm4");
			}

			if (np >= 6) {
				asm volatile ("movdqa %0,%%xmm12" : : "m" (raid_gfcauchypshufb[d][4][0][0]));
				asm volatile ("movdqa %0,%%xmm13" : : "m" (raid_gfcauchypshufb[d][4][1][0]));
				asm volatile ("pshufb %xmm10,%xmm12");
				asm volatile ("pshufb %xmm11,%xmm13");
				asm volatile ("pxor   %xmm12,%xmm5");
				asm volatile ("pxor   %xmm13,%xmm5");
			}
		}

		/* first disk with all coefficients at 1 */
		asm volatile ("movdqa %0,%%xmm10" : : "m" (v[0][i]));

		g23_x3 = 0;
		if (g23) {
			g23_x3 = --g23_count == 0;
			if (g23_x3)
				g23_count = 51;
		}

		if (g23 && g23_x3)
			asm volatile ("movdqa %xmm1,%xmm12");
		asm volatile ("pxor %xmm11,%xmm11");
		asm volatile ("pcmpgtb %xmm1,%xmm11");
		asm volatile ("paddb %xmm1,%xmm1");
		asm volatile ("pand %xmm14,%xmm11");
		asm volatile ("pxor %xmm11,%xmm1");
		if (g23 && g23_x3)
			asm volatile ("pxor %xmm12,%xmm1");

		asm volatile ("pxor %xmm10,%xmm0");
		asm volatile ("pxor %xmm10,%xmm1");
		if (np >= 3)
			asm volatile ("pxor %xmm10,%xmm2");
		if (np >= 4)
			asm volatile ("pxor %xmm10,%xmm3");
		if (np >= 5)
			asm volatile ("pxor %xmm10,%xmm4");
		if (np >= 6)
			asm volatile ("pxor %xmm10,%xmm5");

		asm volatile ("movntdq %%xmm0,%0" : "=m" (p[i]));
		asm volatile ("movntdq %%xmm1,%0" : "=m" (q[i]));
		if (np >= 3)
			asm volatile ("movntdq %%xmm2,%0" : "=m" (r[i]));
		if (np >= 4)
			asm volatile ("movntdq %%xmm3,%0" : "=m" (s[i]));
		if (np >= 5)
			asm volatile ("movntdq %%xmm4,%0" : "=m" (t[i]));
		if (np >= 6)
			asm volatile ("movntdq %%xmm5,%0" : "=m" (u[i]));
	}

	raid_sse_end();
}

/*
 * GEN5 (penta parity with Cauchy matrix) SSSE3 implementation
 *
 * Note that it uses 16 registers, meaning that x64 is required.
 */
void raid_gen5_ssse3ext_raid(int nd, size_t size, void **vv)
{
	raid_genX_ssse3ext(nd, size, vv, 5, 0);
}

void raid_gen5_ssse3ext_aes(int nd, size_t size, void **vv)
{
	raid_genX_ssse3ext(nd, size, vv, 5, 1);
}
#endif

/*
 * GEN6 (hexa parity with Cauchy matrix) SSSE3 implementation
 */
void raid_gen6_ssse3_raid(int nd, size_t size, void **vv)
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
	uint8_t buffer[2 * 16 + 16];
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
		/* last disk without the generator multiplication */
		asm volatile ("movdqa %0,%%xmm4" : : "m" (v[l][i]));

		asm volatile ("movdqa %%xmm4,%0" : "=m" (pd[0]));
		asm volatile ("movdqa %%xmm4,%0" : "=m" (pd[16]));

		asm volatile ("movdqa %0,%%xmm7" : : "m" (gfconst16.low4[0]));
		asm volatile ("movdqa %xmm4,%xmm5");
		asm volatile ("psrlw  $4,%xmm5");
		asm volatile ("pand   %xmm7,%xmm4");
		asm volatile ("pand   %xmm7,%xmm5");

		asm volatile ("movdqa %0,%%xmm0" : : "m" (raid_gfcauchypshufb[l][1][0][0]));
		asm volatile ("movdqa %0,%%xmm7" : : "m" (raid_gfcauchypshufb[l][1][1][0]));
		asm volatile ("pshufb %xmm4,%xmm0");
		asm volatile ("pshufb %xmm5,%xmm7");
		asm volatile ("pxor   %xmm7,%xmm0");

		asm volatile ("movdqa %0,%%xmm1" : : "m" (raid_gfcauchypshufb[l][2][0][0]));
		asm volatile ("movdqa %0,%%xmm7" : : "m" (raid_gfcauchypshufb[l][2][1][0]));
		asm volatile ("pshufb %xmm4,%xmm1");
		asm volatile ("pshufb %xmm5,%xmm7");
		asm volatile ("pxor   %xmm7,%xmm1");

		asm volatile ("movdqa %0,%%xmm2" : : "m" (raid_gfcauchypshufb[l][3][0][0]));
		asm volatile ("movdqa %0,%%xmm7" : : "m" (raid_gfcauchypshufb[l][3][1][0]));
		asm volatile ("pshufb %xmm4,%xmm2");
		asm volatile ("pshufb %xmm5,%xmm7");
		asm volatile ("pxor   %xmm7,%xmm2");

		asm volatile ("movdqa %0,%%xmm3" : : "m" (raid_gfcauchypshufb[l][4][0][0]));
		asm volatile ("movdqa %0,%%xmm7" : : "m" (raid_gfcauchypshufb[l][4][1][0]));
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

			asm volatile ("movdqa %0,%%xmm6" : : "m" (raid_gfcauchypshufb[d][1][0][0]));
			asm volatile ("movdqa %0,%%xmm7" : : "m" (raid_gfcauchypshufb[d][1][1][0]));
			asm volatile ("pshufb %xmm4,%xmm6");
			asm volatile ("pshufb %xmm5,%xmm7");
			asm volatile ("pxor   %xmm6,%xmm0");
			asm volatile ("pxor   %xmm7,%xmm0");

			asm volatile ("movdqa %0,%%xmm6" : : "m" (raid_gfcauchypshufb[d][2][0][0]));
			asm volatile ("movdqa %0,%%xmm7" : : "m" (raid_gfcauchypshufb[d][2][1][0]));
			asm volatile ("pshufb %xmm4,%xmm6");
			asm volatile ("pshufb %xmm5,%xmm7");
			asm volatile ("pxor   %xmm6,%xmm1");
			asm volatile ("pxor   %xmm7,%xmm1");

			asm volatile ("movdqa %0,%%xmm6" : : "m" (raid_gfcauchypshufb[d][3][0][0]));
			asm volatile ("movdqa %0,%%xmm7" : : "m" (raid_gfcauchypshufb[d][3][1][0]));
			asm volatile ("pshufb %xmm4,%xmm6");
			asm volatile ("pshufb %xmm5,%xmm7");
			asm volatile ("pxor   %xmm6,%xmm2");
			asm volatile ("pxor   %xmm7,%xmm2");

			asm volatile ("movdqa %0,%%xmm6" : : "m" (raid_gfcauchypshufb[d][4][0][0]));
			asm volatile ("movdqa %0,%%xmm7" : : "m" (raid_gfcauchypshufb[d][4][1][0]));
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

#ifdef CONFIG_X86_64
/*
 * GEN6 (hexa parity with Cauchy matrix) SSSE3 implementation
 *
 * Note that it uses 16 registers, meaning that x64 is required.
 */
void raid_gen6_ssse3ext_raid(int nd, size_t size, void **vv)
{
	raid_genX_ssse3ext(nd, size, vv, 6, 0);
}

void raid_gen6_ssse3ext_aes(int nd, size_t size, void **vv)
{
	raid_genX_ssse3ext(nd, size, vv, 6, 1);
}
#endif

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

#ifdef CONFIG_X86_64
	asm volatile ("movdqa %0,%%xmm7" : : "m" (gfconst16.low4[0]));
	asm volatile ("movdqa %0,%%xmm4" : : "m" (raid_gfmulpshufb[V][0][0]));
	asm volatile ("movdqa %0,%%xmm5" : : "m" (raid_gfmulpshufb[V][1][0]));

	for (i = 0; i < size; i += 64) {
		asm volatile ("movdqa %0,%%xmm0" : : "m" (p[i]));
		asm volatile ("movdqa %0,%%xmm1" : : "m" (p[i + 16]));
		asm volatile ("movdqa %0,%%xmm2" : : "m" (p[i + 32]));
		asm volatile ("movdqa %0,%%xmm3" : : "m" (p[i + 48]));

		asm volatile ("movdqa %0,%%xmm8" : : "m" (pa[i]));
		asm volatile ("movdqa %0,%%xmm9" : : "m" (pa[i + 16]));
		asm volatile ("movdqa %0,%%xmm10" : : "m" (pa[i + 32]));
		asm volatile ("movdqa %0,%%xmm11" : : "m" (pa[i + 48]));

		asm volatile ("pxor %xmm8,%xmm0");
		asm volatile ("pxor %xmm9,%xmm1");
		asm volatile ("pxor %xmm10,%xmm2");
		asm volatile ("pxor %xmm11,%xmm3");

		asm volatile ("movdqa %xmm0,%xmm8");
		asm volatile ("movdqa %xmm1,%xmm9");
		asm volatile ("movdqa %xmm2,%xmm10");
		asm volatile ("movdqa %xmm3,%xmm11");

		asm volatile ("psrlw $4,%xmm8");
		asm volatile ("psrlw $4,%xmm9");
		asm volatile ("psrlw $4,%xmm10");
		asm volatile ("psrlw $4,%xmm11");

		asm volatile ("pand %xmm7,%xmm0");
		asm volatile ("pand %xmm7,%xmm1");
		asm volatile ("pand %xmm7,%xmm2");
		asm volatile ("pand %xmm7,%xmm3");

		asm volatile ("pand %xmm7,%xmm8");
		asm volatile ("pand %xmm7,%xmm9");
		asm volatile ("pand %xmm7,%xmm10");
		asm volatile ("pand %xmm7,%xmm11");

		asm volatile ("movdqa %xmm4,%xmm12");
		asm volatile ("movdqa %xmm4,%xmm13");
		asm volatile ("movdqa %xmm4,%xmm14");
		asm volatile ("movdqa %xmm4,%xmm15");

		asm volatile ("pshufb %xmm0,%xmm12");
		asm volatile ("pshufb %xmm1,%xmm13");
		asm volatile ("pshufb %xmm2,%xmm14");
		asm volatile ("pshufb %xmm3,%xmm15");

		asm volatile ("movdqa %xmm5,%xmm0");
		asm volatile ("movdqa %xmm5,%xmm1");
		asm volatile ("movdqa %xmm5,%xmm2");
		asm volatile ("movdqa %xmm5,%xmm3");

		asm volatile ("pshufb %xmm8,%xmm0");
		asm volatile ("pshufb %xmm9,%xmm1");
		asm volatile ("pshufb %xmm10,%xmm2");
		asm volatile ("pshufb %xmm11,%xmm3");

		asm volatile ("pxor %xmm12,%xmm0");
		asm volatile ("pxor %xmm13,%xmm1");
		asm volatile ("pxor %xmm14,%xmm2");
		asm volatile ("pxor %xmm15,%xmm3");

		asm volatile ("movdqa %%xmm0,%0" : "=m" (pa[i]));
		asm volatile ("movdqa %%xmm1,%0" : "=m" (pa[i + 16]));
		asm volatile ("movdqa %%xmm2,%0" : "=m" (pa[i + 32]));
		asm volatile ("movdqa %%xmm3,%0" : "=m" (pa[i + 48]));
	}
#else /* CONFIG_X86_32 */
	asm volatile ("movdqa %0,%%xmm7" : : "m" (gfconst16.low4[0]));
	asm volatile ("movdqa %0,%%xmm4" : : "m" (raid_gfmulpshufb[V][0][0]));
	asm volatile ("movdqa %0,%%xmm5" : : "m" (raid_gfmulpshufb[V][1][0]));

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
#endif

	raid_sse_end();
}

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

#ifdef CONFIG_X86_64
	asm volatile ("movdqa %0,%%xmm6" : : "m" (gfconst16.low4[0]));

	/* the inverse matrix V[] is constant for the whole recovery. */
	asm volatile ("movdqa %0,%%xmm8" : : "m" (raid_gfmulpshufb[V[0]][0][0]));
	asm volatile ("movdqa %0,%%xmm9" : : "m" (raid_gfmulpshufb[V[0]][1][0]));
	asm volatile ("movdqa %0,%%xmm10" : : "m" (raid_gfmulpshufb[V[1]][0][0]));
	asm volatile ("movdqa %0,%%xmm11" : : "m" (raid_gfmulpshufb[V[1]][1][0]));
	asm volatile ("movdqa %0,%%xmm12" : : "m" (raid_gfmulpshufb[V[2]][0][0]));
	asm volatile ("movdqa %0,%%xmm13" : : "m" (raid_gfmulpshufb[V[2]][1][0]));
	asm volatile ("movdqa %0,%%xmm14" : : "m" (raid_gfmulpshufb[V[3]][0][0]));
	asm volatile ("movdqa %0,%%xmm15" : : "m" (raid_gfmulpshufb[V[3]][1][0]));

	for (i = 0; i < size; i += 16) {
		/* d0 = p[0] ^ pa[0] */
		asm volatile ("movdqa %0,%%xmm0" : : "m" (p[0][i]));
		asm volatile ("movdqa %0,%%xmm4" : : "m" (pa[0][i]));
		asm volatile ("pxor %xmm4,%xmm0");

		/* d1 = p[1] ^ pa[1] */
		asm volatile ("movdqa %0,%%xmm1" : : "m" (p[1][i]));
		asm volatile ("movdqa %0,%%xmm4" : : "m" (pa[1][i]));
		asm volatile ("pxor %xmm4,%xmm1");

		/*
		 * Split both deltas into low/high nibbles once.
		 *
		 * xmm0 = d0 low
		 * xmm2 = d0 high
		 * xmm1 = d1 low
		 * xmm3 = d1 high
		 */
		asm volatile ("movdqa %xmm0,%xmm2");
		asm volatile ("movdqa %xmm1,%xmm3");
		asm volatile ("psrlw $4,%xmm2");
		asm volatile ("psrlw $4,%xmm3");
		asm volatile ("pand %xmm6,%xmm0");
		asm volatile ("pand %xmm6,%xmm2");
		asm volatile ("pand %xmm6,%xmm1");
		asm volatile ("pand %xmm6,%xmm3");

		/*
		 * pa[0] = V[0] * d0 ^ V[1] * d1
		 */
		asm volatile ("movdqa %xmm8,%xmm4");
		asm volatile ("pshufb %xmm0,%xmm4");
		asm volatile ("movdqa %xmm9,%xmm5");
		asm volatile ("pshufb %xmm2,%xmm5");
		asm volatile ("pxor %xmm5,%xmm4");

		asm volatile ("movdqa %xmm10,%xmm5");
		asm volatile ("pshufb %xmm1,%xmm5");
		asm volatile ("pxor %xmm5,%xmm4");
		asm volatile ("movdqa %xmm11,%xmm5");
		asm volatile ("pshufb %xmm3,%xmm5");
		asm volatile ("pxor %xmm5,%xmm4");

		asm volatile ("movdqa %%xmm4,%0" : "=m" (pa[0][i]));

		/*
		 * pa[1] = V[2] * d0 ^ V[3] * d1
		 */
		asm volatile ("movdqa %xmm12,%xmm4");
		asm volatile ("pshufb %xmm0,%xmm4");
		asm volatile ("movdqa %xmm13,%xmm5");
		asm volatile ("pshufb %xmm2,%xmm5");
		asm volatile ("pxor %xmm5,%xmm4");

		asm volatile ("movdqa %xmm14,%xmm5");
		asm volatile ("pshufb %xmm1,%xmm5");
		asm volatile ("pxor %xmm5,%xmm4");
		asm volatile ("movdqa %xmm15,%xmm5");
		asm volatile ("pshufb %xmm3,%xmm5");
		asm volatile ("pxor %xmm5,%xmm4");

		asm volatile ("movdqa %%xmm4,%0" : "=m" (pa[1][i]));
	}
#else /* CONFIG_X86_32 */
	asm volatile ("movdqa %0,%%xmm7" : : "m" (gfconst16.low4[0]));

	for (i = 0; i < size; i += 16) {
		asm volatile ("movdqa %0,%%xmm0" : : "m" (p[0][i]));
		asm volatile ("movdqa %0,%%xmm2" : : "m" (pa[0][i]));
		asm volatile ("movdqa %0,%%xmm1" : : "m" (p[1][i]));
		asm volatile ("movdqa %0,%%xmm3" : : "m" (pa[1][i]));
		asm volatile ("pxor   %xmm2,%xmm0");
		asm volatile ("pxor   %xmm3,%xmm1");

		asm volatile ("pxor %xmm6,%xmm6");

		asm volatile ("movdqa %0,%%xmm2" : : "m" (raid_gfmulpshufb[V[0]][0][0]));
		asm volatile ("movdqa %0,%%xmm3" : : "m" (raid_gfmulpshufb[V[0]][1][0]));
		asm volatile ("movdqa %xmm0,%xmm4");
		asm volatile ("movdqa %xmm0,%xmm5");
		asm volatile ("psrlw  $4,%xmm5");
		asm volatile ("pand   %xmm7,%xmm4");
		asm volatile ("pand   %xmm7,%xmm5");
		asm volatile ("pshufb %xmm4,%xmm2");
		asm volatile ("pshufb %xmm5,%xmm3");
		asm volatile ("pxor   %xmm2,%xmm6");
		asm volatile ("pxor   %xmm3,%xmm6");

		asm volatile ("movdqa %0,%%xmm2" : : "m" (raid_gfmulpshufb[V[1]][0][0]));
		asm volatile ("movdqa %0,%%xmm3" : : "m" (raid_gfmulpshufb[V[1]][1][0]));
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

		asm volatile ("movdqa %0,%%xmm2" : : "m" (raid_gfmulpshufb[V[2]][0][0]));
		asm volatile ("movdqa %0,%%xmm3" : : "m" (raid_gfmulpshufb[V[2]][1][0]));
		asm volatile ("movdqa %xmm0,%xmm4");
		asm volatile ("movdqa %xmm0,%xmm5");
		asm volatile ("psrlw  $4,%xmm5");
		asm volatile ("pand   %xmm7,%xmm4");
		asm volatile ("pand   %xmm7,%xmm5");
		asm volatile ("pshufb %xmm4,%xmm2");
		asm volatile ("pshufb %xmm5,%xmm3");
		asm volatile ("pxor   %xmm2,%xmm6");
		asm volatile ("pxor   %xmm3,%xmm6");

		asm volatile ("movdqa %0,%%xmm2" : : "m" (raid_gfmulpshufb[V[3]][0][0]));
		asm volatile ("movdqa %0,%%xmm3" : : "m" (raid_gfmulpshufb[V[3]][1][0]));
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
#endif

	raid_sse_end();
}

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

#ifdef CONFIG_X86_64
	const uint8_t *T[RAID_PARITY_MAX * RAID_PARITY_MAX];

	/* precompute shuffle table pointers */
	for (j = 0; j < N * N; ++j)
		T[j] = &raid_gfmulpshufb[V[j]][0][0];

	asm volatile ("movdqa %0,%%xmm15" : : "m" (gfconst16.low4[0]));

	for (i = 0; i < size; i += 16) {
		/* delta */
		asm volatile (
			"movq 0(%2), %%rax\n"
			"movq 0(%3), %%rbx\n"
			"movdqa (%%rax, %1), %%xmm12\n"
			"movdqa (%%rbx, %1), %%xmm13\n"
			"pxor %%xmm13, %%xmm12\n"
			"movdqa %%xmm12, %%xmm1\n"
			"psrlw $4, %%xmm1\n"
			"pand %%xmm15, %%xmm12\n"
			"pand %%xmm15, %%xmm1\n"
			"movdqa %%xmm12, %%xmm0\n"
			"cmpq $1, %0\n"
			"jbe 1f\n"

			"movq 8(%2), %%rax\n"
			"movq 8(%3), %%rbx\n"
			"movdqa (%%rax, %1), %%xmm12\n"
			"movdqa (%%rbx, %1), %%xmm13\n"
			"pxor %%xmm13, %%xmm12\n"
			"movdqa %%xmm12, %%xmm3\n"
			"psrlw $4, %%xmm3\n"
			"pand %%xmm15, %%xmm12\n"
			"pand %%xmm15, %%xmm3\n"
			"movdqa %%xmm12, %%xmm2\n"
			"cmpq $2, %0\n"
			"jbe 1f\n"

			"movq 16(%2), %%rax\n"
			"movq 16(%3), %%rbx\n"
			"movdqa (%%rax, %1), %%xmm12\n"
			"movdqa (%%rbx, %1), %%xmm13\n"
			"pxor %%xmm13, %%xmm12\n"
			"movdqa %%xmm12, %%xmm5\n"
			"psrlw $4, %%xmm5\n"
			"pand %%xmm15, %%xmm12\n"
			"pand %%xmm15, %%xmm5\n"
			"movdqa %%xmm12, %%xmm4\n"
			"cmpq $3, %0\n"
			"jbe 1f\n"

			"movq 24(%2), %%rax\n"
			"movq 24(%3), %%rbx\n"
			"movdqa (%%rax, %1), %%xmm12\n"
			"movdqa (%%rbx, %1), %%xmm13\n"
			"pxor %%xmm13, %%xmm12\n"
			"movdqa %%xmm12, %%xmm7\n"
			"psrlw $4, %%xmm7\n"
			"pand %%xmm15, %%xmm12\n"
			"pand %%xmm15, %%xmm7\n"
			"movdqa %%xmm12, %%xmm6\n"
			"cmpq $4, %0\n"
			"jbe 1f\n"

			"movq 32(%2), %%rax\n"
			"movq 32(%3), %%rbx\n"
			"movdqa (%%rax, %1), %%xmm12\n"
			"movdqa (%%rbx, %1), %%xmm13\n"
			"pxor %%xmm13, %%xmm12\n"
			"movdqa %%xmm12, %%xmm9\n"
			"psrlw $4, %%xmm9\n"
			"pand %%xmm15, %%xmm12\n"
			"pand %%xmm15, %%xmm9\n"
			"movdqa %%xmm12, %%xmm8\n"
			"cmpq $5, %0\n"
			"jbe 1f\n"

			"movq 40(%2), %%rax\n"
			"movq 40(%3), %%rbx\n"
			"movdqa (%%rax, %1), %%xmm12\n"
			"movdqa (%%rbx, %1), %%xmm13\n"
			"pxor %%xmm13, %%xmm12\n"
			"movdqa %%xmm12, %%xmm11\n"
			"psrlw $4, %%xmm11\n"
			"pand %%xmm15, %%xmm12\n"
			"pand %%xmm15, %%xmm11\n"
			"movdqa %%xmm12, %%xmm10\n"

			"1:\n"
			:
			: "r" ((uint64_t)N), "r" (i), "r" (p), "r" (pa)
			: "rax", "rbx", "cc", "memory"
		);

		/* reconstruct */
		for (j = 0; j < N; ++j) {
			asm volatile (
				"movq 0(%2), %%rcx\n"
				"movdqa 0(%%rcx), %%xmm13\n"
				"movdqa 16(%%rcx), %%xmm14\n"
				"pshufb %%xmm0, %%xmm13\n"
				"pshufb %%xmm1, %%xmm14\n"
				"cmpq $1, %0\n"
				"jbe 1f\n"

				"movq 8(%2), %%rcx\n"
				"movdqa 0(%%rcx), %%xmm12\n"
				"pshufb %%xmm2, %%xmm12\n"
				"pxor %%xmm12, %%xmm13\n"
				"movdqa 16(%%rcx), %%xmm12\n"
				"pshufb %%xmm3, %%xmm12\n"
				"pxor %%xmm12, %%xmm14\n"
				"cmpq $2, %0\n"
				"jbe 1f\n"

				"movq 16(%2), %%rcx\n"
				"movdqa 0(%%rcx), %%xmm12\n"
				"pshufb %%xmm4, %%xmm12\n"
				"pxor %%xmm12, %%xmm13\n"
				"movdqa 16(%%rcx), %%xmm12\n"
				"pshufb %%xmm5, %%xmm12\n"
				"pxor %%xmm12, %%xmm14\n"
				"cmpq $3, %0\n"
				"jbe 1f\n"

				"movq 24(%2), %%rcx\n"
				"movdqa 0(%%rcx), %%xmm12\n"
				"pshufb %%xmm6, %%xmm12\n"
				"pxor %%xmm12, %%xmm13\n"
				"movdqa 16(%%rcx), %%xmm12\n"
				"pshufb %%xmm7, %%xmm12\n"
				"pxor %%xmm12, %%xmm14\n"
				"cmpq $4, %0\n"
				"jbe 1f\n"

				"movq 32(%2), %%rcx\n"
				"movdqa 0(%%rcx), %%xmm12\n"
				"pshufb %%xmm8, %%xmm12\n"
				"pxor %%xmm12, %%xmm13\n"
				"movdqa 16(%%rcx), %%xmm12\n"
				"pshufb %%xmm9, %%xmm12\n"
				"pxor %%xmm12, %%xmm14\n"
				"cmpq $5, %0\n"
				"jbe 1f\n"

				"movq 40(%2), %%rcx\n"
				"movdqa 0(%%rcx), %%xmm12\n"
				"pshufb %%xmm10, %%xmm12\n"
				"pxor %%xmm12, %%xmm13\n"
				"movdqa 16(%%rcx), %%xmm12\n"
				"pshufb %%xmm11, %%xmm12\n"
				"pxor %%xmm12, %%xmm14\n"

				"1:\n"
				"pxor %%xmm14, %%xmm13\n"
				"movq %3, %%rax\n"
				"movdqa %%xmm13, (%%rax, %1)\n"
				:
				: "r" ((uint64_t)N), "r" (i), "r" (&T[j * N]), "r" (pa[j])
				: "rax", "rcx", "cc", "memory"
			);
		}
	}
#else /* CONFIG_X86_32 */
	uint8_t buffer_low[RAID_PARITY_MAX * 16 + 16];
	uint8_t buffer_high[RAID_PARITY_MAX * 16 + 16];
	uint8_t *pd_low = __align_ptr(buffer_low, 16);
	uint8_t *pd_high = __align_ptr(buffer_high, 16);

	asm volatile ("movdqa %0,%%xmm7" : : "m" (gfconst16.low4[0]));

	for (i = 0; i < size; i += 16) {
		/* delta */
		for (j = 0; j < N; ++j) {
			asm volatile (
				"movdqa %2, %%xmm0\n"
				"movdqa %3, %%xmm1\n"
				"pxor   %%xmm1, %%xmm0\n"
				"movdqa %%xmm0, %%xmm1\n"
				"psrlw  $4, %%xmm1\n"
				"pand   %%xmm7, %%xmm0\n"
				"pand   %%xmm7, %%xmm1\n"
				"movdqa %%xmm0, %0\n"
				"movdqa %%xmm1, %1\n"
				: "=m" (pd_low[j * 16]), "=m" (pd_high[j * 16])
				: "m" (p[j][i]), "m" (pa[j][i])
			);
		}

		/* reconstruct */
		for (j = 0; j < N; ++j) {
			asm volatile (
				"pxor %%xmm0, %%xmm0\n"
				"pxor %%xmm1, %%xmm1\n"
			);

			for (k = 0; k < N; ++k) {
				uint8_t m = V[j * N + k];

				asm volatile (
					"movdqa %0, %%xmm2\n"
					"movdqa %1, %%xmm3\n"
					"movdqa %2, %%xmm4\n"
					"movdqa %3, %%xmm5\n"
					"pshufb %%xmm4, %%xmm2\n"
					"pshufb %%xmm5, %%xmm3\n"
					"pxor   %%xmm2, %%xmm0\n"
					"pxor   %%xmm3, %%xmm1\n"
					:
					: "m" (raid_gfmulpshufb[m][0][0]), "m" (raid_gfmulpshufb[m][1][0]),
					"m" (pd_low[k * 16]), "m" (pd_high[k * 16])
				);
			}

			asm volatile (
				"pxor   %%xmm1, %%xmm0\n"
				"movdqa %%xmm0, %0\n"
				: "=m" (pa[j][i])
			);
		}
	}
#endif

	raid_sse_end();
}

void raid_register_ssse3(void)
{
	if (raid_cpu_has_ssse3()) {
		raid_gen_register(RAID_ALGO_CAUCHY_PAR3, "ssse3", raid_gen3_ssse3_raid, RAID_POLY_RAID);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR3, "ssse3", raid_gen3_ssse3_aes, RAID_POLY_AES);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR4, "ssse3", raid_gen4_ssse3_raid, RAID_POLY_RAID);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR4, "ssse3", raid_gen4_ssse3_aes, RAID_POLY_AES);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR5, "ssse3", raid_gen5_ssse3_raid, RAID_POLY_RAID);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR6, "ssse3", raid_gen6_ssse3_raid, RAID_POLY_RAID);
#ifdef CONFIG_X86_64
		if (!raid_cpu_has_slow_extendedreg()) {
			raid_gen_register(RAID_ALGO_CAUCHY_PAR3, "ssse3e", raid_gen3_ssse3ext_raid, RAID_POLY_RAID);
			raid_gen_register(RAID_ALGO_CAUCHY_PAR3, "ssse3e", raid_gen3_ssse3ext_aes, RAID_POLY_AES);
			raid_gen_register(RAID_ALGO_CAUCHY_PAR4, "ssse3e", raid_gen4_ssse3ext_raid, RAID_POLY_RAID);
			raid_gen_register(RAID_ALGO_CAUCHY_PAR4, "ssse3e", raid_gen4_ssse3ext_aes, RAID_POLY_AES);
			raid_gen_register(RAID_ALGO_CAUCHY_PAR5, "ssse3e", raid_gen5_ssse3ext_raid, RAID_POLY_RAID);
			raid_gen_register(RAID_ALGO_CAUCHY_PAR5, "ssse3e", raid_gen5_ssse3ext_aes, RAID_POLY_AES);
			raid_gen_register(RAID_ALGO_CAUCHY_PAR6, "ssse3e", raid_gen6_ssse3ext_raid, RAID_POLY_RAID);
			raid_gen_register(RAID_ALGO_CAUCHY_PAR6, "ssse3e", raid_gen6_ssse3ext_aes, RAID_POLY_AES);
		}
#endif

		raid_rec_register(RAID_ALGO_CAUCHY_PAR1, "ssse3", raid_rec1_ssse3, RAID_POLY_ANY);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR2, "ssse3", raid_rec2_ssse3, RAID_POLY_ANY);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR3, "ssse3", raid_recX_ssse3, RAID_POLY_ANY);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR4, "ssse3", raid_recX_ssse3, RAID_POLY_ANY);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR5, "ssse3", raid_recX_ssse3, RAID_POLY_ANY);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR6, "ssse3", raid_recX_ssse3, RAID_POLY_ANY);
	}
}
#endif
