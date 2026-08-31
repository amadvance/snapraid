// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Andrea Mazzoleni

#include "internal.h"
#include "gf.h"
#include "cpu.h"

#ifdef CONFIG_X86
/*
 * Generate three parity blocks with Cauchy matrix using SSSE3 implementation.
 */
static __always_inline void raid_gen3_ssse3_gen(int nd, size_t size, void **vv, int generator)
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
			if (v[1 + i] != v[0])
				memcpy(v[1 + i], v[0], size);
		return;
	}

	raid_sse_begin();

	/* generic case with at least two data disks */
	asm volatile ("movdqa %0,%%xmm3" : : "m" (gfconst16.poly[0]));
	asm volatile ("movdqa %0,%%xmm7" : : "m" (gfconst16.low4[0]));

	for (i = 0; i < size; i += 16) {
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
			asm volatile ("movdqa %0,%%xmm4" : : "m" (v[d][i]));

			if (generator == 3)
				asm volatile ("movdqa %xmm1,%xmm6");
			asm volatile ("pxor %xmm5,%xmm5");
			asm volatile ("pcmpgtb %xmm1,%xmm5");
			asm volatile ("paddb %xmm1,%xmm1");
			asm volatile ("pand %xmm3,%xmm5");
			asm volatile ("pxor %xmm5,%xmm1");
			if (generator == 3)
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

		if (generator == 3)
			asm volatile ("movdqa %xmm1,%xmm6");
		asm volatile ("pxor %xmm5,%xmm5");
		asm volatile ("pcmpgtb %xmm1,%xmm5");
		asm volatile ("paddb %xmm1,%xmm1");
		asm volatile ("pand %xmm3,%xmm5");
		asm volatile ("pxor %xmm5,%xmm1");
		if (generator == 3)
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

/*
 * Generate four parity blocks with Cauchy matrix using SSSE3 implementation.
 */
static __always_inline void raid_gen4_ssse3_gen(int nd, size_t size, void **vv, int generator)
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
			if (v[1 + i] != v[0])
				memcpy(v[1 + i], v[0], size);
		return;
	}

	raid_sse_begin();

	/* generic case with at least two data disks */
	for (i = 0; i < size; i += 16) {
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
			asm volatile ("movdqa %0,%%xmm7" : : "m" (gfconst16.poly[0]));
			asm volatile ("movdqa %0,%%xmm4" : : "m" (v[d][i]));

			if (generator == 3)
				asm volatile ("movdqa %xmm1,%xmm6");
			asm volatile ("pxor %xmm5,%xmm5");
			asm volatile ("pcmpgtb %xmm1,%xmm5");
			asm volatile ("paddb %xmm1,%xmm1");
			asm volatile ("pand %xmm7,%xmm5");
			asm volatile ("pxor %xmm5,%xmm1");
			if (generator == 3)
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

		if (generator == 3)
			asm volatile ("movdqa %xmm1,%xmm6");
		asm volatile ("pxor %xmm5,%xmm5");
		asm volatile ("pcmpgtb %xmm1,%xmm5");
		asm volatile ("paddb %xmm1,%xmm1");
		asm volatile ("pand %xmm7,%xmm5");
		asm volatile ("pxor %xmm5,%xmm1");
		if (generator == 3)
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

/*
 * Generate five parity blocks with Cauchy matrix using SSSE3 implementation.
 *
 * Uses an aligned stack buffer for parity accumulators under 8-register pressure.
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
			if (v[1 + i] != v[0])
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

/*
 * Generate six parity blocks with Cauchy matrix using SSSE3 implementation.
 *
 * Uses an aligned stack buffer for parity accumulators under 8-register pressure.
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
			if (v[1 + i] != v[0])
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
 * Generate three parity blocks with Cauchy matrix using SSSE3 extended implementation.
 *
 * Uses the extended register set (16 XMM registers) and processes two disks
 * per iteration across two 16-byte XMM lanes (32 bytes/step).
 *
 * Note that it uses 16 registers, meaning that x64 is required.
 */
static __always_inline void raid_gen3_ssse3ext_gen(int nd, size_t size, void **vv, int generator)
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
			if (v[1 + i] != v[0])
				memcpy(v[1 + i], v[0], size);
		return;
	}

	raid_sse_begin();

	/* generic case with at least two data disks */
	asm volatile ("movdqa %0,%%xmm3" : : "m" (gfconst16.poly[0]));
	asm volatile ("movdqa %0,%%xmm11" : : "m" (gfconst16.low4[0]));

	for (i = 0; i < size; i += 32) {
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
			asm volatile ("movdqa %0,%%xmm4" : : "m" (v[d][i]));
			asm volatile ("movdqa %0,%%xmm12" : : "m" (v[d][i + 16]));

			asm volatile ("movdqa %0,%%xmm7" : : "m" (raid_gfcauchypshufb[d][1][0][0]));
			asm volatile ("movdqa %0,%%xmm15" : : "m" (raid_gfcauchypshufb[d][1][1][0]));

			if (generator == 3) {
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
			if (generator == 3) {
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

			if (generator == 3) {
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
			if (generator == 3) {
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

		/* single remaining intermediate disk */
		if (d == 1) {
			asm volatile ("movdqa %0,%%xmm4" : : "m" (v[1][i]));
			asm volatile ("movdqa %0,%%xmm12" : : "m" (v[1][i + 16]));

			if (generator == 3) {
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
			if (generator == 3) {
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

		if (generator == 3) {
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
		if (generator == 3) {
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

/*
 * Generate four parity blocks with Cauchy matrix using SSSE3 extended implementation.
 *
 * Uses the extended register set (16 XMM registers) and processes two disks
 * per iteration across two 16-byte XMM lanes (32 bytes/step).
 *
 * Note that it uses 16 registers, meaning that x64 is required.
 */
static __always_inline void raid_gen4_ssse3ext_gen(int nd, size_t size, void **vv, int generator)
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
			if (v[1 + i] != v[0])
				memcpy(v[1 + i], v[0], size);
		return;
	}

	raid_sse_begin();

	/* generic case with at least two data disks */
	for (i = 0; i < size; i += 32) {
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
			asm volatile ("movdqa %0,%%xmm7" : : "m" (gfconst16.poly[0]));
			asm volatile ("movdqa %0,%%xmm15" : : "m" (gfconst16.low4[0]));
			asm volatile ("movdqa %0,%%xmm4" : : "m" (v[d][i]));
			asm volatile ("movdqa %0,%%xmm12" : : "m" (v[d][i + 16]));

			if (generator == 3) {
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
			if (generator == 3) {
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

		if (generator == 3) {
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
		if (generator == 3) {
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

/*
 * Generate N parity blocks with Cauchy matrix using SSSE3 extended implementation.
 */
static __always_inline void raid_genX_ssse3ext(int nd, size_t size, void **vv, int np, int generator)
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
			if (v[1 + d] != v[0])
				memcpy(v[1 + d], v[0], size);
		return;
	}

	raid_sse_begin();

	/* generic case with at least two data disks */
	asm volatile ("movdqa %0,%%xmm14" : : "m" (gfconst16.poly[0]));
	asm volatile ("movdqa %0,%%xmm15" : : "m" (gfconst16.low4[0]));

	for (i = 0; i < size; i += 16) {
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
			asm volatile ("movdqa %0,%%xmm10" : : "m" (v[d][i]));

			if (generator == 3)
				asm volatile ("movdqa %xmm1,%xmm12");
			asm volatile ("pxor %xmm11,%xmm11");
			asm volatile ("pcmpgtb %xmm1,%xmm11");
			asm volatile ("paddb %xmm1,%xmm1");
			asm volatile ("pand %xmm14,%xmm11");
			asm volatile ("pxor %xmm11,%xmm1");
			if (generator == 3)
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

		if (generator == 3)
			asm volatile ("movdqa %xmm1,%xmm12");
		asm volatile ("pxor %xmm11,%xmm11");
		asm volatile ("pcmpgtb %xmm1,%xmm11");
		asm volatile ("paddb %xmm1,%xmm1");
		asm volatile ("pand %xmm14,%xmm11");
		asm volatile ("pxor %xmm11,%xmm1");
		if (generator == 3)
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

#endif

/*
 * Recover failure of one data block using selected parity with SSSE3, optimized for one failure.
 *
 * Computes the selected syndrome in a single survivor scan and reconstructs
 * the missing block directly, avoiding raid_delta_gen() and the extra pass.
 */
static __always_inline void raid_rec1_ssse3_1(int *id, int *ip, int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *pa;
	uint8_t *src[RAID_DATA_MAX];
	const uint8_t *S[RAID_DATA_MAX];
	uint8_t G;
	uint8_t V;
	size_t i;
	int d, s;
	int ns;

	G = A(ip[0], id[0]);
	V = inv(G);

	p = v[nd + ip[0]];
	pa = v[id[0]];

	ns = 0;
	for (d = 0; d < nd; ++d) {
		if (d == id[0])
			continue;

		src[ns] = v[d];
		S[ns] = &raid_gfmulpshufb[A(ip[0], d)][0][0];
		++ns;
	}

	BUG_ON(ns != nd - 1);

	raid_sse_begin();

	asm volatile ("movdqa %0,%%xmm7" : : "m" (gfconst16.low4[0]));
	asm volatile ("movdqa %0,%%xmm2" : : "m" (raid_gfmulpshufb[V][0][0]));
	asm volatile ("movdqa %0,%%xmm3" : : "m" (raid_gfmulpshufb[V][1][0]));

	for (i = 0; i < size; i += 16) {
		/* start from the selected parity block */
		asm volatile ("movdqa %0,%%xmm0" : : "m" (p[i]));

		/* compute only the selected syndrome */
		for (s = 0; s < ns; ++s) {
			const uint8_t *t = S[s];

			asm volatile ("movdqa %0,%%xmm4" : : "m" (src[s][i]));
			asm volatile ("movdqa %xmm4,%xmm5");
			asm volatile ("psrlw $4,%xmm5");
			asm volatile ("pand %xmm7,%xmm4");
			asm volatile ("pand %xmm7,%xmm5");

			asm volatile ("movdqa %0,%%xmm6" : : "m" (t[0]));
			asm volatile ("pshufb %xmm4,%xmm6");
			asm volatile ("pxor %xmm6,%xmm0");

			asm volatile ("movdqa %0,%%xmm6" : : "m" (t[16]));
			asm volatile ("pshufb %xmm5,%xmm6");
			asm volatile ("pxor %xmm6,%xmm0");
		}

		/* multiply the syndrome by the inverse coefficient */
		asm volatile ("movdqa %xmm0,%xmm4");
		asm volatile ("movdqa %xmm0,%xmm5");
		asm volatile ("psrlw $4,%xmm5");
		asm volatile ("pand %xmm7,%xmm4");
		asm volatile ("pand %xmm7,%xmm5");

		asm volatile ("movdqa %xmm2,%xmm6");
		asm volatile ("movdqa %xmm3,%xmm1");
		asm volatile ("pshufb %xmm4,%xmm6");
		asm volatile ("pshufb %xmm5,%xmm1");
		asm volatile ("pxor %xmm1,%xmm6");

		/* recovery data must remain cacheable */
		asm volatile ("movdqa %%xmm6,%0" : "=m" (pa[i]));
	}

	raid_sse_end();
}

/*
 * Recover failure of one data block using Q with SSSE3.
 *
 * Computes Q of all surviving data directly with Horner's method and
 * reconstructs the missing block from Qdelta.
 *
 * Processes two 16-byte lanes per iteration, avoiding raid_delta_gen(),
 * temporary parity buffers, and the extra pass over the data.
 */
static __always_inline void raid_rec1_ssse3_q(int *id, int *ip, int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *q;
	uint8_t *pa;
	uint8_t V;
	int generator;
	int l;
	int d;
	size_t i;

	BUG_ON(ip[0] != 1);

	V = inv(A(1, id[0]));

	generator = powgen(1);
	BUG_ON(generator != 2 && generator != 3);

	l = nd - 1;

	q = v[nd + 1];
	pa = v[id[0]];

	raid_sse_begin();

	/* keep the inverse coefficient resident */
	asm volatile ("movdqa %0,%%xmm2" : : "m" (raid_gfmulpshufb[V][0][0]));
	asm volatile ("movdqa %0,%%xmm3" : : "m" (raid_gfmulpshufb[V][1][0]));

	/* keep the active reduction polynomial resident */
	asm volatile ("movdqa %0,%%xmm7" : : "m" (gfconst16.poly[0]));

	for (i = 0; i < size; i += 32) {
		/* last disk starts Horner without generator multiplication */
		if (l == id[0]) {
			asm volatile ("pxor %xmm0,%xmm0");
			asm volatile ("pxor %xmm1,%xmm1");
		} else {
			asm volatile ("movdqa %0,%%xmm0" : : "m" (v[l][i]));
			asm volatile ("movdqa %0,%%xmm1" : : "m" (v[l][i + 16]));
		}

		for (d = l - 1; d >= 0; --d) {
			/* multiply both Q lanes by the active generator */
			if (generator == 3) {
				/* lane 0 */
				asm volatile ("movdqa %xmm0,%xmm4");
				asm volatile ("pxor %xmm5,%xmm5");
				asm volatile ("pcmpgtb %xmm0,%xmm5");
				asm volatile ("paddb %xmm0,%xmm0");
				asm volatile ("pand %xmm7,%xmm5");
				asm volatile ("pxor %xmm5,%xmm0");
				asm volatile ("pxor %xmm4,%xmm0");

				/* lane 1 */
				asm volatile ("movdqa %xmm1,%xmm4");
				asm volatile ("pxor %xmm5,%xmm5");
				asm volatile ("pcmpgtb %xmm1,%xmm5");
				asm volatile ("paddb %xmm1,%xmm1");
				asm volatile ("pand %xmm7,%xmm5");
				asm volatile ("pxor %xmm5,%xmm1");
				asm volatile ("pxor %xmm4,%xmm1");
			} else {
				/* multiply both lanes by 2 in parallel */
				asm volatile ("pxor %xmm4,%xmm4");
				asm volatile ("pxor %xmm5,%xmm5");
				asm volatile ("pcmpgtb %xmm0,%xmm4");
				asm volatile ("pcmpgtb %xmm1,%xmm5");
				asm volatile ("paddb %xmm0,%xmm0");
				asm volatile ("paddb %xmm1,%xmm1");
				asm volatile ("pand %xmm7,%xmm4");
				asm volatile ("pand %xmm7,%xmm5");
				asm volatile ("pxor %xmm4,%xmm0");
				asm volatile ("pxor %xmm5,%xmm1");
			}

			/* missing disk contributes zero */
			if (d == id[0])
				continue;

			asm volatile ("pxor %0,%%xmm0" : : "m" (v[d][i]));
			asm volatile ("pxor %0,%%xmm1" : : "m" (v[d][i + 16]));
		}

		/* Qdelta = stored Q ^ Q of all surviving data */
		asm volatile ("pxor %0,%%xmm0" : : "m" (q[i]));
		asm volatile ("pxor %0,%%xmm1" : : "m" (q[i + 16]));

		/* low-nibble mask */
		asm volatile ("movdqa %0,%%xmm6" : : "m" (gfconst16.low4[0]));

		/* split both Qdelta lanes into low/high nibbles */
		asm volatile ("movdqa %xmm0,%xmm4");
		asm volatile ("movdqa %xmm1,%xmm5");
		asm volatile ("psrlw $4,%xmm0");
		asm volatile ("psrlw $4,%xmm1");
		asm volatile ("pand %xmm6,%xmm4");
		asm volatile ("pand %xmm6,%xmm5");
		asm volatile ("pand %xmm6,%xmm0");
		asm volatile ("pand %xmm6,%xmm1");

		/* low-nibble products */
		asm volatile ("movdqa %xmm2,%xmm6");
		asm volatile ("movdqa %xmm2,%xmm7");
		asm volatile ("pshufb %xmm4,%xmm6");
		asm volatile ("pshufb %xmm5,%xmm7");

		/* high-nibble products */
		asm volatile ("movdqa %xmm3,%xmm4");
		asm volatile ("movdqa %xmm3,%xmm5");
		asm volatile ("pshufb %xmm0,%xmm4");
		asm volatile ("pshufb %xmm1,%xmm5");
		asm volatile ("pxor %xmm4,%xmm6");
		asm volatile ("pxor %xmm5,%xmm7");

		/* recovery data must remain cacheable */
		asm volatile ("movdqa %%xmm6,%0" : "=m" (pa[i]));
		asm volatile ("movdqa %%xmm7,%0" : "=m" (pa[i + 16]));

		/* restore the active reduction polynomial */
		asm volatile ("movdqa %0,%%xmm7" : : "m" (gfconst16.poly[0]));
	}

	raid_sse_end();
}

/*
 * Recover failure of two data blocks using selected parity with SSSE3,
 * optimized specifically for two failures.
 *
 * Computes only the two selected syndromes in a single survivor scan and
 * reconstructs the missing blocks directly, avoiding raid_delta_gen(),
 * temporary syndrome buffers, and the generation of unused parity rows.
 *
 * has_p is expected to be a compile-time constant after inlining.
 *
 * If P is available, reconstructs only the first missing block through the
 * inverse matrix and obtains the second one by XORing it out of Pdelta.
 */
static __always_inline void raid_rec2_ssse3_2(int has_p, int *id, int *ip, int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p[2];
	uint8_t *pa[2];
	uint8_t *src[RAID_DATA_MAX];
	const uint8_t *S0[RAID_DATA_MAX];
	const uint8_t *S1[RAID_DATA_MAX];
	const uint8_t *R[4];
	uint8_t G[2 * 2];
	uint8_t V[2 * 2];
	size_t i;
	int d, s;
	int ns;

	/* setup and invert the 2x2 coefficients matrix */
	G[0] = A(ip[0], id[0]);
	G[1] = A(ip[0], id[1]);
	G[2] = A(ip[1], id[0]);
	G[3] = A(ip[1], id[1]);
	raid_invert(G, V, 2);

	p[0] = v[nd + ip[0]];
	p[1] = v[nd + ip[1]];
	pa[0] = v[id[0]];
	pa[1] = v[id[1]];

	/* collect surviving data blocks and their selected coefficients */
	ns = 0;
	for (d = 0; d < nd; ++d) {
		if (d == id[0] || d == id[1])
			continue;

		src[ns] = v[d];

		if (!has_p)
			S0[ns] = &raid_gfmulpshufb[A(ip[0], d)][0][0];

		S1[ns] = &raid_gfmulpshufb[A(ip[1], d)][0][0];
		++ns;
	}

	BUG_ON(ns != nd - 2);

	/* inverse-matrix multiplication tables */
	R[0] = &raid_gfmulpshufb[V[0]][0][0];
	R[1] = &raid_gfmulpshufb[V[1]][0][0];

	if (!has_p) {
		R[2] = &raid_gfmulpshufb[V[2]][0][0];
		R[3] = &raid_gfmulpshufb[V[3]][0][0];
	}

	raid_sse_begin();

	asm volatile ("movdqa %0,%%xmm7" : : "m" (gfconst16.low4[0]));

	for (i = 0; i < size; i += 16) {
		/* start from the two selected parity blocks */
		asm volatile ("movdqa %0,%%xmm0" : : "m" (p[0][i]));
		asm volatile ("movdqa %0,%%xmm1" : : "m" (p[1][i]));

		/* compute only the two selected syndromes */
		for (s = 0; s < ns; ++s) {
			asm volatile ("movdqa %0,%%xmm4" : : "m" (src[s][i]));

			/* P has coefficient 1 */
			if (has_p)
				asm volatile ("pxor %xmm4,%xmm0");

			/* split source into low/high nibbles once */
			asm volatile ("movdqa %xmm4,%xmm5");
			asm volatile ("psrlw $4,%xmm5");
			asm volatile ("pand %xmm7,%xmm4");
			asm volatile ("pand %xmm7,%xmm5");

			if (!has_p) {
				const uint8_t *t = S0[s];

				asm volatile ("movdqa %0,%%xmm6" : : "m" (t[0]));
				asm volatile ("pshufb %xmm4,%xmm6");
				asm volatile ("pxor %xmm6,%xmm0");

				asm volatile ("movdqa %0,%%xmm6" : : "m" (t[16]));
				asm volatile ("pshufb %xmm5,%xmm6");
				asm volatile ("pxor %xmm6,%xmm0");
			}

			{
				const uint8_t *t = S1[s];

				asm volatile ("movdqa %0,%%xmm6" : : "m" (t[0]));
				asm volatile ("pshufb %xmm4,%xmm6");
				asm volatile ("pxor %xmm6,%xmm1");

				asm volatile ("movdqa %0,%%xmm6" : : "m" (t[16]));
				asm volatile ("pshufb %xmm5,%xmm6");
				asm volatile ("pxor %xmm6,%xmm1");
			}
		}

		/*
		 * Keep the complete P delta before splitting the syndromes.
		 * xmm6 is no longer needed by the survivor scan.
		 */
		if (has_p)
			asm volatile ("movdqa %xmm0,%xmm6");

		/*
		 * xmm0 = syndrome 0 low
		 * xmm2 = syndrome 0 high
		 * xmm1 = syndrome 1 low
		 * xmm3 = syndrome 1 high
		 */
		asm volatile ("movdqa %xmm0,%xmm2");
		asm volatile ("movdqa %xmm1,%xmm3");
		asm volatile ("psrlw $4,%xmm2");
		asm volatile ("psrlw $4,%xmm3");
		asm volatile ("pand %xmm7,%xmm0");
		asm volatile ("pand %xmm7,%xmm2");
		asm volatile ("pand %xmm7,%xmm1");
		asm volatile ("pand %xmm7,%xmm3");

		/* pa[0] = V[0] * syndrome0 ^ V[1] * syndrome1 */
		asm volatile ("movdqa %0,%%xmm4" : : "m" (R[0][0]));
		asm volatile ("movdqa %0,%%xmm5" : : "m" (R[0][16]));
		asm volatile ("pshufb %xmm0,%xmm4");
		asm volatile ("pshufb %xmm2,%xmm5");
		asm volatile ("pxor %xmm5,%xmm4");

		asm volatile ("movdqa %0,%%xmm5" : : "m" (R[1][0]));
		asm volatile ("pshufb %xmm1,%xmm5");
		asm volatile ("pxor %xmm5,%xmm4");

		asm volatile ("movdqa %0,%%xmm5" : : "m" (R[1][16]));
		asm volatile ("pshufb %xmm3,%xmm5");
		asm volatile ("pxor %xmm5,%xmm4");

		if (has_p) {
			/*
			 * Pdelta = pa[0] ^ pa[1].
			 * xmm4 is pa[0], so obtain pa[1] directly.
			 */
			asm volatile ("pxor %xmm4,%xmm6");

			/* recovery data must remain cacheable */
			asm volatile ("movdqa %%xmm4,%0" : "=m" (pa[0][i]));
			asm volatile ("movdqa %%xmm6,%0" : "=m" (pa[1][i]));
		} else {
			/* recovery data must remain cacheable */
			asm volatile ("movdqa %%xmm4,%0" : "=m" (pa[0][i]));

			/* pa[1] = V[2] * syndrome0 ^ V[3] * syndrome1 */
			asm volatile ("movdqa %0,%%xmm4" : : "m" (R[2][0]));
			asm volatile ("movdqa %0,%%xmm5" : : "m" (R[2][16]));
			asm volatile ("pshufb %xmm0,%xmm4");
			asm volatile ("pshufb %xmm2,%xmm5");
			asm volatile ("pxor %xmm5,%xmm4");

			asm volatile ("movdqa %0,%%xmm5" : : "m" (R[3][0]));
			asm volatile ("pshufb %xmm1,%xmm5");
			asm volatile ("pxor %xmm5,%xmm4");

			asm volatile ("movdqa %0,%%xmm5" : : "m" (R[3][16]));
			asm volatile ("pshufb %xmm3,%xmm5");
			asm volatile ("pxor %xmm5,%xmm4");

			/* recovery data must remain cacheable */
			asm volatile ("movdqa %%xmm4,%0" : "=m" (pa[1][i]));
		}
	}

	raid_sse_end();
}

/*
 * Recover failure of two data blocks using P and Q with SSSE3.
 *
 * Computes Pdelta and Qdelta directly in a single survivor scan.
 * Q is computed with Horner's method using the active field generator.
 *
 * Processes two 16-byte lanes per iteration, avoiding raid_delta_gen(),
 * temporary parity buffers, and the second pass over the data.
 */
static __always_inline void raid_rec2of2_ssse3(int *id, int *ip, int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *q;
	uint8_t *pa;
	uint8_t *qa;
	const uint8_t *R[RAID_PARITY_MAX][RAID_PARITY_MAX];
	uint8_t C[2];
	int generator;
	int l;
	int d;
	size_t i;

	BUG_ON(ip[0] != 0 || ip[1] != 1);

	/* get multiplication coefficients */
	C[0] = inv(powgen(id[1] - id[0]) ^ 1);
	C[1] = inv(powgen(id[0]) ^ powgen(id[1]));

	R[0][0] = &raid_gfmulpshufb[C[0]][0][0];
	R[0][1] = &raid_gfmulpshufb[C[1]][0][0];

	generator = powgen(1);
	BUG_ON(generator != 2 && generator != 3);

	l = nd - 1;

	p = v[nd];
	q = v[nd + 1];
	pa = v[id[0]];
	qa = v[id[1]];

	raid_sse_begin();

	/*
	 * During the survivor scan:
	 *
	 * xmm0  Pdelta lane 0
	 * xmm1  Pdelta lane 1
	 * xmm2  Qa lane 0
	 * xmm3  Qa lane 1
	 * xmm4-xmm6 temporaries
	 * xmm7  active reduction polynomial
	 */
	asm volatile ("movdqa %0,%%xmm7" : : "m" (gfconst16.poly[0]));

	for (i = 0; i < size; i += 32) {
		/* Pdelta starts directly from stored P */
		asm volatile ("movdqa %0,%%xmm0" : : "m" (p[i]));
		asm volatile ("movdqa %0,%%xmm1" : : "m" (p[i + 16]));

		/* Qa starts at zero for both lanes */
		asm volatile ("pxor %xmm2,%xmm2");
		asm volatile ("pxor %xmm3,%xmm3");

		/* last disk starts Horner without generator multiplication */
		if (l != id[0] && l != id[1]) {
			asm volatile ("movdqa %0,%%xmm4" : : "m" (v[l][i]));
			asm volatile ("movdqa %0,%%xmm5" : : "m" (v[l][i + 16]));
			asm volatile ("pxor %xmm4,%xmm0");
			asm volatile ("pxor %xmm5,%xmm1");
			asm volatile ("pxor %xmm4,%xmm2");
			asm volatile ("pxor %xmm5,%xmm3");
		}

		for (d = l - 1; d >= 0; --d) {
			/* multiply both Qa lanes by the active generator */
			if (generator == 3) {
				/* lane 0: Qa = 3 * Qa */
				asm volatile ("movdqa %xmm2,%xmm4");
				asm volatile ("pxor %xmm5,%xmm5");
				asm volatile ("pcmpgtb %xmm2,%xmm5");
				asm volatile ("paddb %xmm2,%xmm2");
				asm volatile ("pand %xmm7,%xmm5");
				asm volatile ("pxor %xmm5,%xmm2");
				asm volatile ("pxor %xmm4,%xmm2");

				/* lane 1: Qa = 3 * Qa */
				asm volatile ("movdqa %xmm3,%xmm4");
				asm volatile ("pxor %xmm5,%xmm5");
				asm volatile ("pcmpgtb %xmm3,%xmm5");
				asm volatile ("paddb %xmm3,%xmm3");
				asm volatile ("pand %xmm7,%xmm5");
				asm volatile ("pxor %xmm5,%xmm3");
				asm volatile ("pxor %xmm4,%xmm3");
			} else {
				/* multiply both lanes by 2 in parallel */
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
			}

			/* missing disks contribute zero */
			if (d == id[0] || d == id[1])
				continue;

			asm volatile ("movdqa %0,%%xmm4" : : "m" (v[d][i]));
			asm volatile ("movdqa %0,%%xmm5" : : "m" (v[d][i + 16]));

			/* Pdelta */
			asm volatile ("pxor %xmm4,%xmm0");
			asm volatile ("pxor %xmm5,%xmm1");

			/* Horner contribution to Qa */
			asm volatile ("pxor %xmm4,%xmm2");
			asm volatile ("pxor %xmm5,%xmm3");
		}

		/* Qdelta = Q ^ Qa */
		asm volatile ("pxor %0,%%xmm2" : : "m" (q[i]));
		asm volatile ("pxor %0,%%xmm3" : : "m" (q[i + 16]));

		/*
		 * The survivor scan is complete, so xmm7 can now hold
		 * the low-nibble mask instead of the polynomial.
		 */
		asm volatile ("movdqa %0,%%xmm7" : : "m" (gfconst16.low4[0]));

		/*
		 * Lane 0.
		 *
		 * xmm0 = Pdelta and must remain intact until Dx is computed.
		 * xmm2 = Qdelta and may be destroyed during reconstruction.
		 */

		/* split Pdelta */
		asm volatile ("movdqa %xmm0,%xmm4");
		asm volatile ("movdqa %xmm0,%xmm5");
		asm volatile ("psrlw $4,%xmm5");
		asm volatile ("pand %xmm7,%xmm4");
		asm volatile ("pand %xmm7,%xmm5");

		/* Dy = C0 * Pdelta */
		asm volatile ("movdqa %0,%%xmm6" : : "m" (R[0][0][0]));
		asm volatile ("pshufb %xmm4,%xmm6");
		asm volatile ("movdqa %0,%%xmm4" : : "m" (R[0][0][16]));
		asm volatile ("pshufb %xmm5,%xmm4");
		asm volatile ("pxor %xmm4,%xmm6");

		/* split Qdelta */
		asm volatile ("movdqa %xmm2,%xmm4");
		asm volatile ("movdqa %xmm2,%xmm5");
		asm volatile ("psrlw $4,%xmm5");
		asm volatile ("pand %xmm7,%xmm4");
		asm volatile ("pand %xmm7,%xmm5");

		/* Dy ^= C1 * Qdelta */
		asm volatile ("movdqa %0,%%xmm2" : : "m" (R[0][1][0]));
		asm volatile ("pshufb %xmm4,%xmm2");
		asm volatile ("pxor %xmm2,%xmm6");
		asm volatile ("movdqa %0,%%xmm2" : : "m" (R[0][1][16]));
		asm volatile ("pshufb %xmm5,%xmm2");
		asm volatile ("pxor %xmm2,%xmm6");

		/* Dx = Pdelta ^ Dy */
		asm volatile ("pxor %xmm6,%xmm0");

		/* recovery data must remain cacheable */
		asm volatile ("movdqa %%xmm0,%0" : "=m" (pa[i]));
		asm volatile ("movdqa %%xmm6,%0" : "=m" (qa[i]));

		/*
		 * Lane 1.
		 *
		 * xmm1 = Pdelta and must remain intact until Dx is computed.
		 * xmm3 = Qdelta and may be destroyed during reconstruction.
		 */

		/* split Pdelta */
		asm volatile ("movdqa %xmm1,%xmm4");
		asm volatile ("movdqa %xmm1,%xmm5");
		asm volatile ("psrlw $4,%xmm5");
		asm volatile ("pand %xmm7,%xmm4");
		asm volatile ("pand %xmm7,%xmm5");

		/* Dy = C0 * Pdelta */
		asm volatile ("movdqa %0,%%xmm6" : : "m" (R[0][0][0]));
		asm volatile ("pshufb %xmm4,%xmm6");
		asm volatile ("movdqa %0,%%xmm4" : : "m" (R[0][0][16]));
		asm volatile ("pshufb %xmm5,%xmm4");
		asm volatile ("pxor %xmm4,%xmm6");

		/* split Qdelta */
		asm volatile ("movdqa %xmm3,%xmm4");
		asm volatile ("movdqa %xmm3,%xmm5");
		asm volatile ("psrlw $4,%xmm5");
		asm volatile ("pand %xmm7,%xmm4");
		asm volatile ("pand %xmm7,%xmm5");

		/* Dy ^= C1 * Qdelta */
		asm volatile ("movdqa %0,%%xmm3" : : "m" (R[0][1][0]));
		asm volatile ("pshufb %xmm4,%xmm3");
		asm volatile ("pxor %xmm3,%xmm6");
		asm volatile ("movdqa %0,%%xmm3" : : "m" (R[0][1][16]));
		asm volatile ("pshufb %xmm5,%xmm3");
		asm volatile ("pxor %xmm3,%xmm6");

		/* Dx = Pdelta ^ Dy */
		asm volatile ("pxor %xmm6,%xmm1");

		/* recovery data must remain cacheable */
		asm volatile ("movdqa %%xmm1,%0" : "=m" (pa[i + 16]));
		asm volatile ("movdqa %%xmm6,%0" : "=m" (qa[i + 16]));

		/*
		 * Restore the polynomial for the next 32-byte iteration.
		 */
		asm volatile ("movdqa %0,%%xmm7" : : "m" (gfconst16.poly[0]));
	}

	raid_sse_end();
}

/*
 * Recover multiple data failures using selected parity blocks with SSSE3 optimized for up to four failures.
 *
 * If P is available, keep the complete P delta syndrome in xmm6.
 * Reconstruct only nr - 1 missing blocks through the inverse matrix and
 * obtain the last block by XORing the reconstructed blocks out of Pdelta.
 *
 * After the survivor scan xmm6 is no longer needed, leaving it available
 * for the P delta accumulator.
 */
static __always_inline void raid_recX_ssse3_1234(int nr, int has_p, int *id, int *ip, int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p[RAID_PARITY_MAX];
	uint8_t *pa[RAID_PARITY_MAX];
	uint8_t *src[RAID_DATA_MAX];
	uint8_t G[RAID_PARITY_MAX * RAID_PARITY_MAX];
	uint8_t V[RAID_PARITY_MAX * RAID_PARITY_MAX];
	const uint8_t *S[RAID_DATA_MAX][RAID_PARITY_MAX];
	const uint8_t *R[RAID_PARITY_MAX][RAID_PARITY_MAX];
	uint8_t buffer_low[RAID_PARITY_MAX * 16 + 16];
	uint8_t buffer_high[RAID_PARITY_MAX * 16 + 16];
	uint8_t *pd_low = __align_ptr(buffer_low, 16);
	uint8_t *pd_high = __align_ptr(buffer_high, 16);
	size_t i;
	int d, j, k, s;
	int ns;

	BUG_ON(nr < 1 || nr > 4);

	for (j = 0; j < nr; ++j)
		for (k = 0; k < nr; ++k)
			G[j * nr + k] = A(ip[j], id[k]);

	raid_invert(G, V, nr);

	for (j = 0; j < nr; ++j) {
		p[j] = v[nd + ip[j]];
		pa[j] = v[id[j]];
	}

	ns = 0;
	k = 0;

	for (d = 0; d < nd; ++d) {
		if (k < nr && d == id[k]) {
			++k;
			continue;
		}

		src[ns] = v[d];

		for (j = 0; j < nr; ++j)
			S[ns][j] = &raid_gfmulpshufb[A(ip[j], d)][0][0];

		++ns;
	}

	BUG_ON(k != nr);
	BUG_ON(ns != nd - nr);

	for (j = 0; j < nr - has_p; ++j)
		for (k = 0; k < nr; ++k)
			R[j][k] = &raid_gfmulpshufb[V[j * nr + k]][0][0];

	raid_sse_begin();

	asm volatile ("movdqa %0,%%xmm7" : : "m" (gfconst16.low4[0]));

	for (i = 0; i < size; i += 16) {
		asm volatile ("movdqa %0,%%xmm0" : : "m" (p[0][i]));

		if (nr >= 2)
			asm volatile ("movdqa %0,%%xmm1" : : "m" (p[1][i]));

		if (nr >= 3)
			asm volatile ("movdqa %0,%%xmm2" : : "m" (p[2][i]));

		if (nr >= 4)
			asm volatile ("movdqa %0,%%xmm3" : : "m" (p[3][i]));

		for (s = 0; s < ns; ++s) {
			const uint8_t **t = S[s];

			asm volatile ("movdqa %0,%%xmm4" : : "m" (src[s][i]));

			if (has_p) {
				asm volatile ("pxor %xmm4,%xmm0");

				asm volatile ("movdqa %xmm4,%xmm5");
				asm volatile ("psrlw $4,%xmm5");
				asm volatile ("pand %xmm7,%xmm4");
				asm volatile ("pand %xmm7,%xmm5");
			} else {
				asm volatile ("movdqa %xmm4,%xmm5");
				asm volatile ("psrlw $4,%xmm5");
				asm volatile ("pand %xmm7,%xmm4");
				asm volatile ("pand %xmm7,%xmm5");

				asm volatile ("movdqa %0,%%xmm6" : : "m" (t[0][0]));
				asm volatile ("pshufb %xmm4,%xmm6");
				asm volatile ("pxor %xmm6,%xmm0");
				asm volatile ("movdqa %0,%%xmm6" : : "m" (t[0][16]));
				asm volatile ("pshufb %xmm5,%xmm6");
				asm volatile ("pxor %xmm6,%xmm0");
			}

			if (nr >= 2) {
				asm volatile ("movdqa %0,%%xmm6" : : "m" (t[1][0]));
				asm volatile ("pshufb %xmm4,%xmm6");
				asm volatile ("pxor %xmm6,%xmm1");
				asm volatile ("movdqa %0,%%xmm6" : : "m" (t[1][16]));
				asm volatile ("pshufb %xmm5,%xmm6");
				asm volatile ("pxor %xmm6,%xmm1");
			}

			if (nr >= 3) {
				asm volatile ("movdqa %0,%%xmm6" : : "m" (t[2][0]));
				asm volatile ("pshufb %xmm4,%xmm6");
				asm volatile ("pxor %xmm6,%xmm2");
				asm volatile ("movdqa %0,%%xmm6" : : "m" (t[2][16]));
				asm volatile ("pshufb %xmm5,%xmm6");
				asm volatile ("pxor %xmm6,%xmm2");
			}

			if (nr >= 4) {
				asm volatile ("movdqa %0,%%xmm6" : : "m" (t[3][0]));
				asm volatile ("pshufb %xmm4,%xmm6");
				asm volatile ("pxor %xmm6,%xmm3");
				asm volatile ("movdqa %0,%%xmm6" : : "m" (t[3][16]));
				asm volatile ("pshufb %xmm5,%xmm6");
				asm volatile ("pxor %xmm6,%xmm3");
			}
		}

		/* preserve the complete P delta before splitting syndrome 0 */
		if (has_p)
			asm volatile ("movdqa %xmm0,%xmm6");

		/*
		 * Split completed syndromes once and store the low/high nibbles.
		 * The survivor scan is already complete.
		 */

		asm volatile ("movdqa %xmm0,%xmm4");
		asm volatile ("movdqa %xmm0,%xmm5");
		asm volatile ("psrlw $4,%xmm5");
		asm volatile ("pand %xmm7,%xmm4");
		asm volatile ("pand %xmm7,%xmm5");
		asm volatile ("movdqa %%xmm4,%0" : "=m" (pd_low[0]));
		asm volatile ("movdqa %%xmm5,%0" : "=m" (pd_high[0]));

		if (nr >= 2) {
			asm volatile ("movdqa %xmm1,%xmm4");
			asm volatile ("movdqa %xmm1,%xmm5");
			asm volatile ("psrlw $4,%xmm5");
			asm volatile ("pand %xmm7,%xmm4");
			asm volatile ("pand %xmm7,%xmm5");
			asm volatile ("movdqa %%xmm4,%0" : "=m" (pd_low[16]));
			asm volatile ("movdqa %%xmm5,%0" : "=m" (pd_high[16]));
		}

		if (nr >= 3) {
			asm volatile ("movdqa %xmm2,%xmm4");
			asm volatile ("movdqa %xmm2,%xmm5");
			asm volatile ("psrlw $4,%xmm5");
			asm volatile ("pand %xmm7,%xmm4");
			asm volatile ("pand %xmm7,%xmm5");
			asm volatile ("movdqa %%xmm4,%0" : "=m" (pd_low[32]));
			asm volatile ("movdqa %%xmm5,%0" : "=m" (pd_high[32]));
		}

		if (nr >= 4) {
			asm volatile ("movdqa %xmm3,%xmm4");
			asm volatile ("movdqa %xmm3,%xmm5");
			asm volatile ("psrlw $4,%xmm5");
			asm volatile ("pand %xmm7,%xmm4");
			asm volatile ("pand %xmm7,%xmm5");
			asm volatile ("movdqa %%xmm4,%0" : "=m" (pd_low[48]));
			asm volatile ("movdqa %%xmm5,%0" : "=m" (pd_high[48]));
		}

		/*
		 * Reconstruct all but the last missing block when P is
		 * available. xmm6 keeps the remaining P delta.
		 */
		for (j = 0; j < nr - has_p; ++j) {
			const uint8_t **t = R[j];

			asm volatile ("movdqa %0,%%xmm0" : : "m" (t[0][0]));
			asm volatile ("movdqa %0,%%xmm1" : : "m" (t[0][16]));
			asm volatile ("movdqa %0,%%xmm4" : : "m" (pd_low[0]));
			asm volatile ("movdqa %0,%%xmm5" : : "m" (pd_high[0]));
			asm volatile ("pshufb %xmm4,%xmm0");
			asm volatile ("pshufb %xmm5,%xmm1");

			for (k = 1; k < nr; ++k) {
				asm volatile ("movdqa %0,%%xmm2" : : "m" (t[k][0]));
				asm volatile ("movdqa %0,%%xmm3" : : "m" (t[k][16]));
				asm volatile ("movdqa %0,%%xmm4" : : "m" (pd_low[k * 16]));
				asm volatile ("movdqa %0,%%xmm5" : : "m" (pd_high[k * 16]));
				asm volatile ("pshufb %xmm4,%xmm2");
				asm volatile ("pshufb %xmm5,%xmm3");
				asm volatile ("pxor %xmm2,%xmm0");
				asm volatile ("pxor %xmm3,%xmm1");
			}

			asm volatile ("pxor %xmm1,%xmm0");

			if (has_p)
				asm volatile ("pxor %xmm0,%xmm6");

			asm volatile ("movdqa %%xmm0,%0" : "=m" (pa[j][i]));
		}

		if (has_p)
			asm volatile ("movdqa %%xmm6,%0" : "=m" (pa[nr - 1][i]));
	}

	raid_sse_end();
}

/*
 * Recover multiple data failures using selected parity blocks with SSSE3.
 *
 * If P is available, keep the complete P delta syndrome in xmm6.
 * Reconstruct only nr - 1 missing blocks through the inverse matrix and
 * obtain the last block by XORing the reconstructed blocks out of Pdelta.
 *
 * The completed syndromes are kept in temporary memory, leaving xmm6
 * available for the P delta accumulator during reconstruction.
 */
static __always_inline void raid_recX_ssse3(int nr, int has_p, int *id, int *ip, int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p[RAID_PARITY_MAX];
	uint8_t *pa[RAID_PARITY_MAX];
	uint8_t *src[RAID_DATA_MAX];
	uint8_t G[RAID_PARITY_MAX * RAID_PARITY_MAX];
	uint8_t V[RAID_PARITY_MAX * RAID_PARITY_MAX];
	const uint8_t *S[RAID_DATA_MAX][RAID_PARITY_MAX];
	const uint8_t *R[RAID_PARITY_MAX][RAID_PARITY_MAX];
	uint8_t buffer_low[RAID_PARITY_MAX * 16 + 16];
	uint8_t buffer_high[RAID_PARITY_MAX * 16 + 16];
	uint8_t *pd_low = __align_ptr(buffer_low, 16);
	uint8_t *pd_high = __align_ptr(buffer_high, 16);
	size_t i;
	int d, j, k, s;
	int ns;

	BUG_ON(nr < 1 || nr > RAID_PARITY_MAX);

	for (j = 0; j < nr; ++j)
		for (k = 0; k < nr; ++k)
			G[j * nr + k] = A(ip[j], id[k]);

	raid_invert(G, V, nr);

	for (j = 0; j < nr; ++j) {
		p[j] = v[nd + ip[j]];
		pa[j] = v[id[j]];
	}

	ns = 0;
	k = 0;

	for (d = 0; d < nd; ++d) {
		if (k < nr && d == id[k]) {
			++k;
			continue;
		}

		src[ns] = v[d];

		for (j = 0; j < nr; ++j)
			S[ns][j] = &raid_gfmulpshufb[A(ip[j], d)][0][0];

		++ns;
	}

	BUG_ON(k != nr);
	BUG_ON(ns != nd - nr);

	for (j = 0; j < nr - has_p; ++j)
		for (k = 0; k < nr; ++k)
			R[j][k] = &raid_gfmulpshufb[V[j * nr + k]][0][0];

	raid_sse_begin();

	asm volatile ("movdqa %0,%%xmm7" : : "m" (gfconst16.low4[0]));

	for (i = 0; i < size; i += 16) {
		/* pd_low[] temporarily contains the complete raw syndrome */
		for (j = 0; j < nr; ++j) {
			asm volatile ("movdqa %0,%%xmm0" : : "m" (p[j][i]));
			asm volatile ("movdqa %%xmm0,%0" : "=m" (pd_low[j * 16]));
		}

		/* single survivor scan */
		for (s = 0; s < ns; ++s) {
			const uint8_t **t = S[s];

			asm volatile ("movdqa %0,%%xmm0" : : "m" (src[s][i]));

			/* p must use the original source before nibble splitting */
			if (has_p) {
				asm volatile ("movdqa %xmm0,%xmm4");
				asm volatile ("pxor %0,%%xmm4" : : "m" (pd_low[0]));
				asm volatile ("movdqa %%xmm4,%0" : "=m" (pd_low[0]));
			}

			asm volatile ("movdqa %xmm0,%xmm1");
			asm volatile ("psrlw $4,%xmm1");
			asm volatile ("pand %xmm7,%xmm0");
			asm volatile ("pand %xmm7,%xmm1");

			for (j = has_p; j < nr; ++j) {
				asm volatile ("movdqa %0,%%xmm2" : : "m" (t[j][0]));
				asm volatile ("movdqa %0,%%xmm3" : : "m" (t[j][16]));
				asm volatile ("pshufb %xmm0,%xmm2");
				asm volatile ("pshufb %xmm1,%xmm3");
				asm volatile ("pxor %xmm3,%xmm2");
				asm volatile ("pxor %0,%%xmm2" : : "m" (pd_low[j * 16]));
				asm volatile ("movdqa %%xmm2,%0" : "=m" (pd_low[j * 16]));
			}
		}

		/*
		 * Preserve the complete P delta before pd_low[0] is converted
		 * to its low-nibble representation.
		 */
		if (has_p)
			asm volatile ("movdqa %0,%%xmm6" : : "m" (pd_low[0]));

		/* convert all completed raw syndromes to low/high nibble form */
		for (k = 0; k < nr; ++k) {
			asm volatile ("movdqa %0,%%xmm0" : : "m" (pd_low[k * 16]));
			asm volatile ("movdqa %xmm0,%xmm1");
			asm volatile ("psrlw $4,%xmm1");
			asm volatile ("pand %xmm7,%xmm0");
			asm volatile ("pand %xmm7,%xmm1");
			asm volatile ("movdqa %%xmm0,%0" : "=m" (pd_low[k * 16]));
			asm volatile ("movdqa %%xmm1,%0" : "=m" (pd_high[k * 16]));
		}

		/*
		 * Reconstruct all but the last missing block when P is
		 * available. xmm6 keeps the remaining P delta.
		 */
		for (j = 0; j < nr - has_p; ++j) {
			const uint8_t **t = R[j];

			asm volatile ("movdqa %0,%%xmm0" : : "m" (t[0][0]));
			asm volatile ("movdqa %0,%%xmm1" : : "m" (t[0][16]));
			asm volatile ("movdqa %0,%%xmm4" : : "m" (pd_low[0]));
			asm volatile ("movdqa %0,%%xmm5" : : "m" (pd_high[0]));
			asm volatile ("pshufb %xmm4,%xmm0");
			asm volatile ("pshufb %xmm5,%xmm1");

			for (k = 1; k < nr; ++k) {
				asm volatile ("movdqa %0,%%xmm2" : : "m" (t[k][0]));
				asm volatile ("movdqa %0,%%xmm3" : : "m" (t[k][16]));
				asm volatile ("movdqa %0,%%xmm4" : : "m" (pd_low[k * 16]));
				asm volatile ("movdqa %0,%%xmm5" : : "m" (pd_high[k * 16]));
				asm volatile ("pshufb %xmm4,%xmm2");
				asm volatile ("pshufb %xmm5,%xmm3");
				asm volatile ("pxor %xmm2,%xmm0");
				asm volatile ("pxor %xmm3,%xmm1");
			}

			asm volatile ("pxor %xmm1,%xmm0");

			if (has_p)
				asm volatile ("pxor %xmm0,%xmm6");

			asm volatile ("movdqa %%xmm0,%0" : "=m" (pa[j][i]));
		}

		if (has_p)
			asm volatile ("movdqa %%xmm6,%0" : "=m" (pa[nr - 1][i]));
	}

	raid_sse_end();
}

#ifdef CONFIG_X86_64
/*
 * Recover failure of one data block using selected parity with SSSE3 extended,
 * optimized specifically for one failure.
 *
 * Computes only the selected syndrome in a single survivor scan and processes
 * two 16-byte lanes per iteration to expose instruction-level parallelism.
 */
static __always_inline void raid_rec1_ssse3ext_1(int *id, int *ip, int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *pa;
	uint8_t *src[RAID_DATA_MAX];
	const uint8_t *S[RAID_DATA_MAX];
	uint8_t G;
	uint8_t V;
	size_t i;
	int d, s;
	int ns;

	G = A(ip[0], id[0]);
	V = inv(G);

	p = v[nd + ip[0]];
	pa = v[id[0]];

	ns = 0;
	for (d = 0; d < nd; ++d) {
		if (d == id[0])
			continue;

		src[ns] = v[d];
		S[ns] = &raid_gfmulpshufb[A(ip[0], d)][0][0];
		++ns;
	}

	BUG_ON(ns != nd - 1);

	raid_sse_begin();

	asm volatile ("movdqa %0,%%xmm15" : : "m" (gfconst16.low4[0]));

	/* keep the inverse coefficient resident */
	asm volatile ("movdqa %0,%%xmm12" : : "m" (raid_gfmulpshufb[V][0][0]));
	asm volatile ("movdqa %0,%%xmm13" : : "m" (raid_gfmulpshufb[V][1][0]));

	for (i = 0; i < size; i += 32) {
		/* start both lanes from the selected parity block */
		asm volatile ("movdqa %0,%%xmm0" : : "m" (p[i]));
		asm volatile ("movdqa %0,%%xmm1" : : "m" (p[i + 16]));

		/* compute only the selected syndrome */
		for (s = 0; s < ns; ++s) {
			const uint8_t *t = S[s];

			asm volatile ("movdqa %0,%%xmm4" : : "m" (src[s][i]));
			asm volatile ("movdqa %0,%%xmm5" : : "m" (src[s][i + 16]));

			/* split both source lanes into low/high nibbles */
			asm volatile ("movdqa %xmm4,%xmm6");
			asm volatile ("movdqa %xmm5,%xmm7");
			asm volatile ("psrlw $4,%xmm6");
			asm volatile ("psrlw $4,%xmm7");
			asm volatile ("pand %xmm15,%xmm4");
			asm volatile ("pand %xmm15,%xmm5");
			asm volatile ("pand %xmm15,%xmm6");
			asm volatile ("pand %xmm15,%xmm7");

			/* low-nibble products, both lanes */
			asm volatile ("movdqa %0,%%xmm8" : : "m" (t[0]));
			asm volatile ("movdqa %xmm8,%xmm9");
			asm volatile ("pshufb %xmm4,%xmm8");
			asm volatile ("pshufb %xmm5,%xmm9");
			asm volatile ("pxor %xmm8,%xmm0");
			asm volatile ("pxor %xmm9,%xmm1");

			/* high-nibble products, both lanes */
			asm volatile ("movdqa %0,%%xmm10" : : "m" (t[16]));
			asm volatile ("movdqa %xmm10,%xmm11");
			asm volatile ("pshufb %xmm6,%xmm10");
			asm volatile ("pshufb %xmm7,%xmm11");
			asm volatile ("pxor %xmm10,%xmm0");
			asm volatile ("pxor %xmm11,%xmm1");
		}

		/* split both completed syndromes */
		asm volatile ("movdqa %xmm0,%xmm4");
		asm volatile ("movdqa %xmm1,%xmm5");
		asm volatile ("movdqa %xmm0,%xmm6");
		asm volatile ("movdqa %xmm1,%xmm7");
		asm volatile ("psrlw $4,%xmm6");
		asm volatile ("psrlw $4,%xmm7");
		asm volatile ("pand %xmm15,%xmm4");
		asm volatile ("pand %xmm15,%xmm5");
		asm volatile ("pand %xmm15,%xmm6");
		asm volatile ("pand %xmm15,%xmm7");

		/* multiply both lanes by the inverse coefficient */
		asm volatile ("movdqa %xmm12,%xmm8");
		asm volatile ("movdqa %xmm12,%xmm9");
		asm volatile ("pshufb %xmm4,%xmm8");
		asm volatile ("pshufb %xmm5,%xmm9");

		asm volatile ("movdqa %xmm13,%xmm10");
		asm volatile ("movdqa %xmm13,%xmm11");
		asm volatile ("pshufb %xmm6,%xmm10");
		asm volatile ("pshufb %xmm7,%xmm11");
		asm volatile ("pxor %xmm10,%xmm8");
		asm volatile ("pxor %xmm11,%xmm9");

		/* recovery data must remain cacheable */
		asm volatile ("movdqa %%xmm8,%0" : "=m" (pa[i]));
		asm volatile ("movdqa %%xmm9,%0" : "=m" (pa[i + 16]));
	}

	raid_sse_end();
}

/*
 * Recover failure of one data block using Q with SSSE3 extended.
 *
 * Computes Q of all surviving data directly with Horner's method and
 * reconstructs the missing block from Qdelta.
 *
 * Processes four 16-byte lanes per iteration, avoiding raid_delta_gen(),
 * temporary parity buffers, and the extra pass over the data.
 */
static __always_inline void raid_rec1_ssse3ext_q(int *id, int *ip, int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *q;
	uint8_t *pa;
	uint8_t V;
	int generator;
	int l;
	int d;
	size_t i;

	BUG_ON(ip[0] != 1);

	V = inv(A(1, id[0]));

	generator = powgen(1);
	BUG_ON(generator != 2 && generator != 3);

	l = nd - 1;

	q = v[nd + 1];
	pa = v[id[0]];

	raid_sse_begin();

	/*
	 * xmm0-xmm3   Q accumulators
	 * xmm4-xmm11  temporaries
	 * xmm12       inverse coefficient low table
	 * xmm13       inverse coefficient high table
	 * xmm14       active reduction polynomial
	 * xmm15       low-nibble mask
	 */
	asm volatile ("movdqa %0,%%xmm12" : : "m" (raid_gfmulpshufb[V][0][0]));
	asm volatile ("movdqa %0,%%xmm13" : : "m" (raid_gfmulpshufb[V][1][0]));
	asm volatile ("movdqa %0,%%xmm14" : : "m" (gfconst16.poly[0]));
	asm volatile ("movdqa %0,%%xmm15" : : "m" (gfconst16.low4[0]));

	for (i = 0; i < size; i += 64) {
		/* last disk starts Horner without generator multiplication */
		if (l == id[0]) {
			asm volatile ("pxor %xmm0,%xmm0");
			asm volatile ("pxor %xmm1,%xmm1");
			asm volatile ("pxor %xmm2,%xmm2");
			asm volatile ("pxor %xmm3,%xmm3");
		} else {
			asm volatile ("movdqa %0,%%xmm0" : : "m" (v[l][i]));
			asm volatile ("movdqa %0,%%xmm1" : : "m" (v[l][i + 16]));
			asm volatile ("movdqa %0,%%xmm2" : : "m" (v[l][i + 32]));
			asm volatile ("movdqa %0,%%xmm3" : : "m" (v[l][i + 48]));
		}

		for (d = l - 1; d >= 0; --d) {
			/* multiply all four Q lanes by the active generator */
			if (generator == 3) {
				asm volatile ("movdqa %xmm0,%xmm8");
				asm volatile ("movdqa %xmm1,%xmm9");
				asm volatile ("movdqa %xmm2,%xmm10");
				asm volatile ("movdqa %xmm3,%xmm11");

				asm volatile ("pxor %xmm4,%xmm4");
				asm volatile ("pxor %xmm5,%xmm5");
				asm volatile ("pxor %xmm6,%xmm6");
				asm volatile ("pxor %xmm7,%xmm7");

				asm volatile ("pcmpgtb %xmm0,%xmm4");
				asm volatile ("pcmpgtb %xmm1,%xmm5");
				asm volatile ("pcmpgtb %xmm2,%xmm6");
				asm volatile ("pcmpgtb %xmm3,%xmm7");

				asm volatile ("paddb %xmm0,%xmm0");
				asm volatile ("paddb %xmm1,%xmm1");
				asm volatile ("paddb %xmm2,%xmm2");
				asm volatile ("paddb %xmm3,%xmm3");

				asm volatile ("pand %xmm14,%xmm4");
				asm volatile ("pand %xmm14,%xmm5");
				asm volatile ("pand %xmm14,%xmm6");
				asm volatile ("pand %xmm14,%xmm7");

				asm volatile ("pxor %xmm4,%xmm0");
				asm volatile ("pxor %xmm5,%xmm1");
				asm volatile ("pxor %xmm6,%xmm2");
				asm volatile ("pxor %xmm7,%xmm3");

				asm volatile ("pxor %xmm8,%xmm0");
				asm volatile ("pxor %xmm9,%xmm1");
				asm volatile ("pxor %xmm10,%xmm2");
				asm volatile ("pxor %xmm11,%xmm3");
			} else {
				asm volatile ("pxor %xmm4,%xmm4");
				asm volatile ("pxor %xmm5,%xmm5");
				asm volatile ("pxor %xmm6,%xmm6");
				asm volatile ("pxor %xmm7,%xmm7");

				asm volatile ("pcmpgtb %xmm0,%xmm4");
				asm volatile ("pcmpgtb %xmm1,%xmm5");
				asm volatile ("pcmpgtb %xmm2,%xmm6");
				asm volatile ("pcmpgtb %xmm3,%xmm7");

				asm volatile ("paddb %xmm0,%xmm0");
				asm volatile ("paddb %xmm1,%xmm1");
				asm volatile ("paddb %xmm2,%xmm2");
				asm volatile ("paddb %xmm3,%xmm3");

				asm volatile ("pand %xmm14,%xmm4");
				asm volatile ("pand %xmm14,%xmm5");
				asm volatile ("pand %xmm14,%xmm6");
				asm volatile ("pand %xmm14,%xmm7");

				asm volatile ("pxor %xmm4,%xmm0");
				asm volatile ("pxor %xmm5,%xmm1");
				asm volatile ("pxor %xmm6,%xmm2");
				asm volatile ("pxor %xmm7,%xmm3");
			}

			/* missing disk contributes zero */
			if (d == id[0])
				continue;

			asm volatile ("pxor %0,%%xmm0" : : "m" (v[d][i]));
			asm volatile ("pxor %0,%%xmm1" : : "m" (v[d][i + 16]));
			asm volatile ("pxor %0,%%xmm2" : : "m" (v[d][i + 32]));
			asm volatile ("pxor %0,%%xmm3" : : "m" (v[d][i + 48]));
		}

		/* Qdelta = stored Q ^ Q of all surviving data */
		asm volatile ("pxor %0,%%xmm0" : : "m" (q[i]));
		asm volatile ("pxor %0,%%xmm1" : : "m" (q[i + 16]));
		asm volatile ("pxor %0,%%xmm2" : : "m" (q[i + 32]));
		asm volatile ("pxor %0,%%xmm3" : : "m" (q[i + 48]));

		/* keep low nibbles in xmm4-xmm7 and high nibbles in xmm0-xmm3 */
		asm volatile ("movdqa %xmm0,%xmm4");
		asm volatile ("movdqa %xmm1,%xmm5");
		asm volatile ("movdqa %xmm2,%xmm6");
		asm volatile ("movdqa %xmm3,%xmm7");

		asm volatile ("psrlw $4,%xmm0");
		asm volatile ("psrlw $4,%xmm1");
		asm volatile ("psrlw $4,%xmm2");
		asm volatile ("psrlw $4,%xmm3");

		asm volatile ("pand %xmm15,%xmm4");
		asm volatile ("pand %xmm15,%xmm5");
		asm volatile ("pand %xmm15,%xmm6");
		asm volatile ("pand %xmm15,%xmm7");

		asm volatile ("pand %xmm15,%xmm0");
		asm volatile ("pand %xmm15,%xmm1");
		asm volatile ("pand %xmm15,%xmm2");
		asm volatile ("pand %xmm15,%xmm3");

		/* low-nibble products */
		asm volatile ("movdqa %xmm12,%xmm8");
		asm volatile ("movdqa %xmm12,%xmm9");
		asm volatile ("movdqa %xmm12,%xmm10");
		asm volatile ("movdqa %xmm12,%xmm11");

		asm volatile ("pshufb %xmm4,%xmm8");
		asm volatile ("pshufb %xmm5,%xmm9");
		asm volatile ("pshufb %xmm6,%xmm10");
		asm volatile ("pshufb %xmm7,%xmm11");

		/* high-nibble products */
		asm volatile ("movdqa %xmm13,%xmm4");
		asm volatile ("movdqa %xmm13,%xmm5");
		asm volatile ("movdqa %xmm13,%xmm6");
		asm volatile ("movdqa %xmm13,%xmm7");

		asm volatile ("pshufb %xmm0,%xmm4");
		asm volatile ("pshufb %xmm1,%xmm5");
		asm volatile ("pshufb %xmm2,%xmm6");
		asm volatile ("pshufb %xmm3,%xmm7");

		asm volatile ("pxor %xmm4,%xmm8");
		asm volatile ("pxor %xmm5,%xmm9");
		asm volatile ("pxor %xmm6,%xmm10");
		asm volatile ("pxor %xmm7,%xmm11");

		/* recovery data must remain cacheable */
		asm volatile ("movdqa %%xmm8,%0" : "=m" (pa[i]));
		asm volatile ("movdqa %%xmm9,%0" : "=m" (pa[i + 16]));
		asm volatile ("movdqa %%xmm10,%0" : "=m" (pa[i + 32]));
		asm volatile ("movdqa %%xmm11,%0" : "=m" (pa[i + 48]));
	}

	raid_sse_end();
}

/*
 * Recover failure of two data blocks using selected parity with SSSE3 extended,
 * optimized specifically for two failures.
 *
 * Computes only the two selected syndromes in a single survivor scan and
 * processes two 16-byte lanes per iteration.
 *
 * Keeps the first inverse-matrix row resident for the complete recovery loop.
 * When P is available this is the only inverse row required, because the
 * second missing block is obtained directly from Pdelta. Without P, the
 * second inverse row is loaded only during reconstruction and each table
 * load is shared between both lanes.
 *
 * has_p is expected to be a compile-time constant after inlining.
 *
 * Note that it uses 16 registers, meaning that x64 is required.
 */
static __always_inline void raid_rec2_ssse3ext_2(int has_p, int *id, int *ip, int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p[2];
	uint8_t *pa[2];
	uint8_t *src[RAID_DATA_MAX];
	const uint8_t *S0[RAID_DATA_MAX];
	const uint8_t *S1[RAID_DATA_MAX];
	uint8_t G[2 * 2];
	uint8_t V[2 * 2];
	size_t i;
	int d, s;
	int ns;

	/* setup and invert the 2x2 coefficients matrix */
	G[0] = A(ip[0], id[0]);
	G[1] = A(ip[0], id[1]);
	G[2] = A(ip[1], id[0]);
	G[3] = A(ip[1], id[1]);
	raid_invert(G, V, 2);

	p[0] = v[nd + ip[0]];
	p[1] = v[nd + ip[1]];
	pa[0] = v[id[0]];
	pa[1] = v[id[1]];

	/* collect surviving data blocks and selected coefficient tables */
	ns = 0;
	for (d = 0; d < nd; ++d) {
		if (d == id[0] || d == id[1])
			continue;

		src[ns] = v[d];

		if (!has_p)
			S0[ns] = &raid_gfmulpshufb[A(ip[0], d)][0][0];

		S1[ns] = &raid_gfmulpshufb[A(ip[1], d)][0][0];

		++ns;
	}

	BUG_ON(ns != nd - 2);

	raid_sse_begin();

	/*
	 * Register allocation during the survivor scan:
	 *
	 * xmm0/xmm1   syndrome 0, lanes 0/1
	 * xmm2/xmm3   syndrome 1, lanes 0/1
	 * xmm4/xmm5   source low nibbles
	 * xmm6/xmm7   source high nibbles
	 * xmm8-xmm11  resident first inverse-matrix row
	 * xmm12/xmm13 multiplication table/results for both lanes
	 * xmm14       low-nibble mask
	 * xmm15       temporary
	 */

	/* keep the first inverse-matrix row resident */
	asm volatile ("movdqa %0,%%xmm8" : : "m" (raid_gfmulpshufb[V[0]][0][0]));
	asm volatile ("movdqa %0,%%xmm9" : : "m" (raid_gfmulpshufb[V[0]][1][0]));
	asm volatile ("movdqa %0,%%xmm10" : : "m" (raid_gfmulpshufb[V[1]][0][0]));
	asm volatile ("movdqa %0,%%xmm11" : : "m" (raid_gfmulpshufb[V[1]][1][0]));

	for (i = 0; i < size; i += 32) {
		/* xmm14 is reused during reconstruction, therefore restore the mask for each iteration */
		asm volatile ("movdqa %0,%%xmm14" : : "m" (gfconst16.low4[0]));

		/* start both syndrome lanes from the selected stored parity blocks */
		asm volatile ("movdqa %0,%%xmm0" : : "m" (p[0][i]));
		asm volatile ("movdqa %0,%%xmm1" : : "m" (p[0][i + 16]));
		asm volatile ("movdqa %0,%%xmm2" : : "m" (p[1][i]));
		asm volatile ("movdqa %0,%%xmm3" : : "m" (p[1][i + 16]));

		/* compute both selected syndromes in a single survivor scan */
		for (s = 0; s < ns; ++s) {
			asm volatile ("movdqa %0,%%xmm4" : : "m" (src[s][i]));
			asm volatile ("movdqa %0,%%xmm5" : : "m" (src[s][i + 16]));

			/* P has coefficient 1 and must use the original source before nibble splitting */
			if (has_p) {
				asm volatile ("pxor %xmm4,%xmm0");
				asm volatile ("pxor %xmm5,%xmm1");
			}

			/* split both source lanes into low/high nibbles */
			asm volatile ("movdqa %xmm4,%xmm6");
			asm volatile ("movdqa %xmm5,%xmm7");
			asm volatile ("psrlw $4,%xmm6");
			asm volatile ("psrlw $4,%xmm7");
			asm volatile ("pand %xmm14,%xmm4");
			asm volatile ("pand %xmm14,%xmm5");
			asm volatile ("pand %xmm14,%xmm6");
			asm volatile ("pand %xmm14,%xmm7");

			/* syndrome 0 only needs multiplication when it is not P */
			if (!has_p) {
				const uint8_t *t = S0[s];

				asm volatile ("movdqa %0,%%xmm12" : : "m" (t[0]));
				asm volatile ("movdqa %xmm12,%xmm13");
				asm volatile ("pshufb %xmm4,%xmm12");
				asm volatile ("pshufb %xmm5,%xmm13");
				asm volatile ("pxor %xmm12,%xmm0");
				asm volatile ("pxor %xmm13,%xmm1");

				asm volatile ("movdqa %0,%%xmm12" : : "m" (t[16]));
				asm volatile ("movdqa %xmm12,%xmm13");
				asm volatile ("pshufb %xmm6,%xmm12");
				asm volatile ("pshufb %xmm7,%xmm13");
				asm volatile ("pxor %xmm12,%xmm0");
				asm volatile ("pxor %xmm13,%xmm1");
			}

			/* syndrome 1 */
			{
				const uint8_t *t = S1[s];

				asm volatile ("movdqa %0,%%xmm12" : : "m" (t[0]));
				asm volatile ("movdqa %xmm12,%xmm13");
				asm volatile ("pshufb %xmm4,%xmm12");
				asm volatile ("pshufb %xmm5,%xmm13");
				asm volatile ("pxor %xmm12,%xmm2");
				asm volatile ("pxor %xmm13,%xmm3");

				asm volatile ("movdqa %0,%%xmm12" : : "m" (t[16]));
				asm volatile ("movdqa %xmm12,%xmm13");
				asm volatile ("pshufb %xmm6,%xmm12");
				asm volatile ("pshufb %xmm7,%xmm13");
				asm volatile ("pxor %xmm12,%xmm2");
				asm volatile ("pxor %xmm13,%xmm3");
			}
		}

		/* preserve the complete Pdelta for both lanes */
		if (has_p) {
			asm volatile ("movdqa %xmm0,%xmm6");
			asm volatile ("movdqa %xmm1,%xmm7");
		}

		/*
		 * Split the completed syndromes:
		 *
		 * xmm0/xmm1   syndrome 0 low, lanes 0/1
		 * xmm4/xmm5   syndrome 0 high, lanes 0/1
		 * xmm2/xmm3   syndrome 1 low, lanes 0/1
		 * xmm12/xmm13 syndrome 1 high, lanes 0/1
		 */
		asm volatile ("movdqa %xmm0,%xmm4");
		asm volatile ("movdqa %xmm1,%xmm5");
		asm volatile ("movdqa %xmm2,%xmm12");
		asm volatile ("movdqa %xmm3,%xmm13");
		asm volatile ("psrlw $4,%xmm4");
		asm volatile ("psrlw $4,%xmm5");
		asm volatile ("psrlw $4,%xmm12");
		asm volatile ("psrlw $4,%xmm13");
		asm volatile ("pand %xmm14,%xmm0");
		asm volatile ("pand %xmm14,%xmm1");
		asm volatile ("pand %xmm14,%xmm2");
		asm volatile ("pand %xmm14,%xmm3");
		asm volatile ("pand %xmm14,%xmm4");
		asm volatile ("pand %xmm14,%xmm5");
		asm volatile ("pand %xmm14,%xmm12");
		asm volatile ("pand %xmm14,%xmm13");

		if (has_p) {
			/*
			 * Reconstruct pa[0] for both lanes.
			 *
			 * The syndrome nibbles can be destroyed because the second
			 * missing block is obtained directly from Pdelta.
			 */

			/* V[0] * syndrome0 low */
			asm volatile ("movdqa %xmm8,%xmm14");
			asm volatile ("movdqa %xmm8,%xmm15");
			asm volatile ("pshufb %xmm0,%xmm14");
			asm volatile ("pshufb %xmm1,%xmm15");
			asm volatile ("movdqa %xmm14,%xmm0");
			asm volatile ("movdqa %xmm15,%xmm1");

			/* V[0] * syndrome0 high */
			asm volatile ("movdqa %xmm9,%xmm14");
			asm volatile ("movdqa %xmm9,%xmm15");
			asm volatile ("pshufb %xmm4,%xmm14");
			asm volatile ("pshufb %xmm5,%xmm15");
			asm volatile ("pxor %xmm14,%xmm0");
			asm volatile ("pxor %xmm15,%xmm1");

			/* V[1] * syndrome1 low */
			asm volatile ("movdqa %xmm10,%xmm14");
			asm volatile ("movdqa %xmm10,%xmm15");
			asm volatile ("pshufb %xmm2,%xmm14");
			asm volatile ("pshufb %xmm3,%xmm15");
			asm volatile ("pxor %xmm14,%xmm0");
			asm volatile ("pxor %xmm15,%xmm1");

			/* V[1] * syndrome1 high */
			asm volatile ("movdqa %xmm11,%xmm14");
			asm volatile ("movdqa %xmm11,%xmm15");
			asm volatile ("pshufb %xmm12,%xmm14");
			asm volatile ("pshufb %xmm13,%xmm15");
			asm volatile ("pxor %xmm14,%xmm0");
			asm volatile ("pxor %xmm15,%xmm1");

			/* pa[1] = Pdelta ^ pa[0] */
			asm volatile ("pxor %xmm0,%xmm6");
			asm volatile ("pxor %xmm1,%xmm7");

			/* recovery data must remain cacheable */
			asm volatile ("movdqa %%xmm0,%0" : "=m" (pa[0][i]));
			asm volatile ("movdqa %%xmm1,%0" : "=m" (pa[0][i + 16]));
			asm volatile ("movdqa %%xmm6,%0" : "=m" (pa[1][i]));
			asm volatile ("movdqa %%xmm7,%0" : "=m" (pa[1][i + 16]));
		} else {
			/*
			 * Reconstruct pa[0] using the resident first inverse row.
			 * Keep all syndrome nibbles intact because they are needed
			 * again for pa[1].
			 */

			asm volatile ("movdqa %xmm8,%xmm6");
			asm volatile ("movdqa %xmm8,%xmm7");
			asm volatile ("pshufb %xmm0,%xmm6");
			asm volatile ("pshufb %xmm1,%xmm7");

			asm volatile ("movdqa %xmm9,%xmm14");
			asm volatile ("movdqa %xmm9,%xmm15");
			asm volatile ("pshufb %xmm4,%xmm14");
			asm volatile ("pshufb %xmm5,%xmm15");
			asm volatile ("pxor %xmm14,%xmm6");
			asm volatile ("pxor %xmm15,%xmm7");

			asm volatile ("movdqa %xmm10,%xmm14");
			asm volatile ("movdqa %xmm10,%xmm15");
			asm volatile ("pshufb %xmm2,%xmm14");
			asm volatile ("pshufb %xmm3,%xmm15");
			asm volatile ("pxor %xmm14,%xmm6");
			asm volatile ("pxor %xmm15,%xmm7");

			asm volatile ("movdqa %xmm11,%xmm14");
			asm volatile ("movdqa %xmm11,%xmm15");
			asm volatile ("pshufb %xmm12,%xmm14");
			asm volatile ("pshufb %xmm13,%xmm15");
			asm volatile ("pxor %xmm14,%xmm6");
			asm volatile ("pxor %xmm15,%xmm7");

			/* recovery data must remain cacheable */
			asm volatile ("movdqa %%xmm6,%0" : "=m" (pa[0][i]));
			asm volatile ("movdqa %%xmm7,%0" : "=m" (pa[0][i + 16]));

			/*
			 * Reconstruct pa[1].
			 *
			 * The second inverse row is not kept resident. Each table is
			 * loaded once for the 32-byte iteration and shared by both lanes.
			 */

			/* V[2] * syndrome0 low */
			asm volatile ("movdqa %0,%%xmm14" : : "m" (raid_gfmulpshufb[V[2]][0][0]));
			asm volatile ("movdqa %xmm14,%xmm15");
			asm volatile ("pshufb %xmm0,%xmm14");
			asm volatile ("pshufb %xmm1,%xmm15");
			asm volatile ("movdqa %xmm14,%xmm6");
			asm volatile ("movdqa %xmm15,%xmm7");

			/* V[2] * syndrome0 high */
			asm volatile ("movdqa %0,%%xmm14" : : "m" (raid_gfmulpshufb[V[2]][1][0]));
			asm volatile ("movdqa %xmm14,%xmm15");
			asm volatile ("pshufb %xmm4,%xmm14");
			asm volatile ("pshufb %xmm5,%xmm15");
			asm volatile ("pxor %xmm14,%xmm6");
			asm volatile ("pxor %xmm15,%xmm7");

			/* V[3] * syndrome1 low */
			asm volatile ("movdqa %0,%%xmm14" : : "m" (raid_gfmulpshufb[V[3]][0][0]));
			asm volatile ("movdqa %xmm14,%xmm15");
			asm volatile ("pshufb %xmm2,%xmm14");
			asm volatile ("pshufb %xmm3,%xmm15");
			asm volatile ("pxor %xmm14,%xmm6");
			asm volatile ("pxor %xmm15,%xmm7");

			/* V[3] * syndrome1 high */
			asm volatile ("movdqa %0,%%xmm14" : : "m" (raid_gfmulpshufb[V[3]][1][0]));
			asm volatile ("movdqa %xmm14,%xmm15");
			asm volatile ("pshufb %xmm12,%xmm14");
			asm volatile ("pshufb %xmm13,%xmm15");
			asm volatile ("pxor %xmm14,%xmm6");
			asm volatile ("pxor %xmm15,%xmm7");

			/* recovery data must remain cacheable */
			asm volatile ("movdqa %%xmm6,%0" : "=m" (pa[1][i]));
			asm volatile ("movdqa %%xmm7,%0" : "=m" (pa[1][i + 16]));
		}
	}

	raid_sse_end();
}

/*
 * Recover failure of two data blocks using P and Q with SSSE3 extended.
 *
 * Computes Pdelta and Qdelta directly in a single survivor scan.
 * Q is computed with Horner's method using the active field generator.
 *
 * Processes four 16-byte lanes per iteration and keeps all four
 * multiplication tables used by the analytical solution resident.
 */
static __always_inline void raid_rec2of2_ssse3ext(int *id, int *ip, int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *q;
	uint8_t *pa;
	uint8_t *qa;
	const uint8_t *R[RAID_PARITY_MAX][RAID_PARITY_MAX];
	uint8_t C[2];
	int generator;
	int l;
	int d;
	size_t i;

	BUG_ON(ip[0] != 0 || ip[1] != 1);

	/* get multiplication coefficients */
	C[0] = inv(powgen(id[1] - id[0]) ^ 1);
	C[1] = inv(powgen(id[0]) ^ powgen(id[1]));

	R[0][0] = &raid_gfmulpshufb[C[0]][0][0];
	R[0][1] = &raid_gfmulpshufb[C[1]][0][0];

	generator = powgen(1);
	BUG_ON(generator != 2 && generator != 3);

	l = nd - 1;

	p = v[nd];
	q = v[nd + 1];
	pa = v[id[0]];
	qa = v[id[1]];

	raid_sse_begin();

	/*
	 * During the survivor scan:
	 *
	 * xmm0-xmm3  Pdelta lanes
	 * xmm4-xmm7  Qa lanes
	 * xmm8-xmm10 temporaries
	 * xmm11      active reduction polynomial
	 * xmm12-xmm15 analytical multiplication tables
	 */
	asm volatile ("movdqa %0,%%xmm11" : : "m" (gfconst16.poly[0]));

	/* keep the analytical multiplication tables resident */
	asm volatile ("movdqa %0,%%xmm12" : : "m" (R[0][0][0]));
	asm volatile ("movdqa %0,%%xmm13" : : "m" (R[0][0][16]));
	asm volatile ("movdqa %0,%%xmm14" : : "m" (R[0][1][0]));
	asm volatile ("movdqa %0,%%xmm15" : : "m" (R[0][1][16]));

	for (i = 0; i < size; i += 64) {
		/* Pdelta starts directly from stored P */
		asm volatile ("movdqa %0,%%xmm0" : : "m" (p[i]));
		asm volatile ("movdqa %0,%%xmm1" : : "m" (p[i + 16]));
		asm volatile ("movdqa %0,%%xmm2" : : "m" (p[i + 32]));
		asm volatile ("movdqa %0,%%xmm3" : : "m" (p[i + 48]));

		/* Qa starts at zero for all four lanes */
		asm volatile ("pxor %xmm4,%xmm4");
		asm volatile ("pxor %xmm5,%xmm5");
		asm volatile ("pxor %xmm6,%xmm6");
		asm volatile ("pxor %xmm7,%xmm7");

		/* last disk starts Horner without generator multiplication */
		if (l != id[0] && l != id[1]) {
			asm volatile ("movdqa %0,%%xmm8" : : "m" (v[l][i]));
			asm volatile ("pxor %xmm8,%xmm0");
			asm volatile ("pxor %xmm8,%xmm4");

			asm volatile ("movdqa %0,%%xmm8" : : "m" (v[l][i + 16]));
			asm volatile ("pxor %xmm8,%xmm1");
			asm volatile ("pxor %xmm8,%xmm5");

			asm volatile ("movdqa %0,%%xmm8" : : "m" (v[l][i + 32]));
			asm volatile ("pxor %xmm8,%xmm2");
			asm volatile ("pxor %xmm8,%xmm6");

			asm volatile ("movdqa %0,%%xmm8" : : "m" (v[l][i + 48]));
			asm volatile ("pxor %xmm8,%xmm3");
			asm volatile ("pxor %xmm8,%xmm7");
		}

		for (d = l - 1; d >= 0; --d) {
			/* multiply all four Qa lanes by the active generator */
			if (generator == 3) {
				/* lane 0 */
				asm volatile ("movdqa %xmm4,%xmm8");
				asm volatile ("pxor %xmm9,%xmm9");
				asm volatile ("pcmpgtb %xmm4,%xmm9");
				asm volatile ("paddb %xmm4,%xmm4");
				asm volatile ("pand %xmm11,%xmm9");
				asm volatile ("pxor %xmm9,%xmm4");
				asm volatile ("pxor %xmm8,%xmm4");

				/* lane 1 */
				asm volatile ("movdqa %xmm5,%xmm8");
				asm volatile ("pxor %xmm9,%xmm9");
				asm volatile ("pcmpgtb %xmm5,%xmm9");
				asm volatile ("paddb %xmm5,%xmm5");
				asm volatile ("pand %xmm11,%xmm9");
				asm volatile ("pxor %xmm9,%xmm5");
				asm volatile ("pxor %xmm8,%xmm5");

				/* lane 2 */
				asm volatile ("movdqa %xmm6,%xmm8");
				asm volatile ("pxor %xmm9,%xmm9");
				asm volatile ("pcmpgtb %xmm6,%xmm9");
				asm volatile ("paddb %xmm6,%xmm6");
				asm volatile ("pand %xmm11,%xmm9");
				asm volatile ("pxor %xmm9,%xmm6");
				asm volatile ("pxor %xmm8,%xmm6");

				/* lane 3 */
				asm volatile ("movdqa %xmm7,%xmm8");
				asm volatile ("pxor %xmm9,%xmm9");
				asm volatile ("pcmpgtb %xmm7,%xmm9");
				asm volatile ("paddb %xmm7,%xmm7");
				asm volatile ("pand %xmm11,%xmm9");
				asm volatile ("pxor %xmm9,%xmm7");
				asm volatile ("pxor %xmm8,%xmm7");
			} else {
				/* lanes 0 and 1 */
				asm volatile ("pxor %xmm8,%xmm8");
				asm volatile ("pxor %xmm9,%xmm9");
				asm volatile ("pcmpgtb %xmm4,%xmm8");
				asm volatile ("pcmpgtb %xmm5,%xmm9");
				asm volatile ("paddb %xmm4,%xmm4");
				asm volatile ("paddb %xmm5,%xmm5");
				asm volatile ("pand %xmm11,%xmm8");
				asm volatile ("pand %xmm11,%xmm9");
				asm volatile ("pxor %xmm8,%xmm4");
				asm volatile ("pxor %xmm9,%xmm5");

				/* lanes 2 and 3 */
				asm volatile ("pxor %xmm8,%xmm8");
				asm volatile ("pxor %xmm9,%xmm9");
				asm volatile ("pcmpgtb %xmm6,%xmm8");
				asm volatile ("pcmpgtb %xmm7,%xmm9");
				asm volatile ("paddb %xmm6,%xmm6");
				asm volatile ("paddb %xmm7,%xmm7");
				asm volatile ("pand %xmm11,%xmm8");
				asm volatile ("pand %xmm11,%xmm9");
				asm volatile ("pxor %xmm8,%xmm6");
				asm volatile ("pxor %xmm9,%xmm7");
			}

			/* missing disks contribute zero */
			if (d == id[0] || d == id[1])
				continue;

			/* lane 0 */
			asm volatile ("movdqa %0,%%xmm8" : : "m" (v[d][i]));
			asm volatile ("pxor %xmm8,%xmm0");
			asm volatile ("pxor %xmm8,%xmm4");

			/* lane 1 */
			asm volatile ("movdqa %0,%%xmm8" : : "m" (v[d][i + 16]));
			asm volatile ("pxor %xmm8,%xmm1");
			asm volatile ("pxor %xmm8,%xmm5");

			/* lane 2 */
			asm volatile ("movdqa %0,%%xmm8" : : "m" (v[d][i + 32]));
			asm volatile ("pxor %xmm8,%xmm2");
			asm volatile ("pxor %xmm8,%xmm6");

			/* lane 3 */
			asm volatile ("movdqa %0,%%xmm8" : : "m" (v[d][i + 48]));
			asm volatile ("pxor %xmm8,%xmm3");
			asm volatile ("pxor %xmm8,%xmm7");
		}

		/* Qdelta = Q ^ Qa */
		asm volatile ("pxor %0,%%xmm4" : : "m" (q[i]));
		asm volatile ("pxor %0,%%xmm5" : : "m" (q[i + 16]));
		asm volatile ("pxor %0,%%xmm6" : : "m" (q[i + 32]));
		asm volatile ("pxor %0,%%xmm7" : : "m" (q[i + 48]));

		/* the polynomial is no longer needed during reconstruction */
		asm volatile ("movdqa %0,%%xmm11" : : "m" (gfconst16.low4[0]));

		/* lane 0: split Pdelta */
		asm volatile ("movdqa %xmm0,%xmm8");
		asm volatile ("movdqa %xmm0,%xmm9");
		asm volatile ("psrlw $4,%xmm9");
		asm volatile ("pand %xmm11,%xmm8");
		asm volatile ("pand %xmm11,%xmm9");

		/* lane 0: Dy = C0 * Pdelta */
		asm volatile ("movdqa %xmm12,%xmm10");
		asm volatile ("pshufb %xmm8,%xmm10");
		asm volatile ("movdqa %xmm13,%xmm8");
		asm volatile ("pshufb %xmm9,%xmm8");
		asm volatile ("pxor %xmm8,%xmm10");

		/* lane 0: split Qdelta */
		asm volatile ("movdqa %xmm4,%xmm8");
		asm volatile ("movdqa %xmm4,%xmm9");
		asm volatile ("psrlw $4,%xmm9");
		asm volatile ("pand %xmm11,%xmm8");
		asm volatile ("pand %xmm11,%xmm9");

		/* lane 0: Dy ^= C1 * Qdelta */
		asm volatile ("movdqa %xmm14,%xmm4");
		asm volatile ("pshufb %xmm8,%xmm4");
		asm volatile ("pxor %xmm4,%xmm10");
		asm volatile ("movdqa %xmm15,%xmm4");
		asm volatile ("pshufb %xmm9,%xmm4");
		asm volatile ("pxor %xmm4,%xmm10");

		/* lane 0: Dx = Pdelta ^ Dy */
		asm volatile ("pxor %xmm10,%xmm0");

		/* recovery data must remain cacheable */
		asm volatile ("movdqa %%xmm0,%0" : "=m" (pa[i]));
		asm volatile ("movdqa %%xmm10,%0" : "=m" (qa[i]));

		/* lane 1: split Pdelta */
		asm volatile ("movdqa %xmm1,%xmm8");
		asm volatile ("movdqa %xmm1,%xmm9");
		asm volatile ("psrlw $4,%xmm9");
		asm volatile ("pand %xmm11,%xmm8");
		asm volatile ("pand %xmm11,%xmm9");

		/* lane 1: Dy = C0 * Pdelta */
		asm volatile ("movdqa %xmm12,%xmm10");
		asm volatile ("pshufb %xmm8,%xmm10");
		asm volatile ("movdqa %xmm13,%xmm8");
		asm volatile ("pshufb %xmm9,%xmm8");
		asm volatile ("pxor %xmm8,%xmm10");

		/* lane 1: split Qdelta */
		asm volatile ("movdqa %xmm5,%xmm8");
		asm volatile ("movdqa %xmm5,%xmm9");
		asm volatile ("psrlw $4,%xmm9");
		asm volatile ("pand %xmm11,%xmm8");
		asm volatile ("pand %xmm11,%xmm9");

		/* lane 1: Dy ^= C1 * Qdelta */
		asm volatile ("movdqa %xmm14,%xmm5");
		asm volatile ("pshufb %xmm8,%xmm5");
		asm volatile ("pxor %xmm5,%xmm10");
		asm volatile ("movdqa %xmm15,%xmm5");
		asm volatile ("pshufb %xmm9,%xmm5");
		asm volatile ("pxor %xmm5,%xmm10");

		/* lane 1: Dx = Pdelta ^ Dy */
		asm volatile ("pxor %xmm10,%xmm1");

		/* recovery data must remain cacheable */
		asm volatile ("movdqa %%xmm1,%0" : "=m" (pa[i + 16]));
		asm volatile ("movdqa %%xmm10,%0" : "=m" (qa[i + 16]));

		/* lane 2: split Pdelta */
		asm volatile ("movdqa %xmm2,%xmm8");
		asm volatile ("movdqa %xmm2,%xmm9");
		asm volatile ("psrlw $4,%xmm9");
		asm volatile ("pand %xmm11,%xmm8");
		asm volatile ("pand %xmm11,%xmm9");

		/* lane 2: Dy = C0 * Pdelta */
		asm volatile ("movdqa %xmm12,%xmm10");
		asm volatile ("pshufb %xmm8,%xmm10");
		asm volatile ("movdqa %xmm13,%xmm8");
		asm volatile ("pshufb %xmm9,%xmm8");
		asm volatile ("pxor %xmm8,%xmm10");

		/* lane 2: split Qdelta */
		asm volatile ("movdqa %xmm6,%xmm8");
		asm volatile ("movdqa %xmm6,%xmm9");
		asm volatile ("psrlw $4,%xmm9");
		asm volatile ("pand %xmm11,%xmm8");
		asm volatile ("pand %xmm11,%xmm9");

		/* lane 2: Dy ^= C1 * Qdelta */
		asm volatile ("movdqa %xmm14,%xmm6");
		asm volatile ("pshufb %xmm8,%xmm6");
		asm volatile ("pxor %xmm6,%xmm10");
		asm volatile ("movdqa %xmm15,%xmm6");
		asm volatile ("pshufb %xmm9,%xmm6");
		asm volatile ("pxor %xmm6,%xmm10");

		/* lane 2: Dx = Pdelta ^ Dy */
		asm volatile ("pxor %xmm10,%xmm2");

		/* recovery data must remain cacheable */
		asm volatile ("movdqa %%xmm2,%0" : "=m" (pa[i + 32]));
		asm volatile ("movdqa %%xmm10,%0" : "=m" (qa[i + 32]));

		/* lane 3: split Pdelta */
		asm volatile ("movdqa %xmm3,%xmm8");
		asm volatile ("movdqa %xmm3,%xmm9");
		asm volatile ("psrlw $4,%xmm9");
		asm volatile ("pand %xmm11,%xmm8");
		asm volatile ("pand %xmm11,%xmm9");

		/* lane 3: Dy = C0 * Pdelta */
		asm volatile ("movdqa %xmm12,%xmm10");
		asm volatile ("pshufb %xmm8,%xmm10");
		asm volatile ("movdqa %xmm13,%xmm8");
		asm volatile ("pshufb %xmm9,%xmm8");
		asm volatile ("pxor %xmm8,%xmm10");

		/* lane 3: split Qdelta */
		asm volatile ("movdqa %xmm7,%xmm8");
		asm volatile ("movdqa %xmm7,%xmm9");
		asm volatile ("psrlw $4,%xmm9");
		asm volatile ("pand %xmm11,%xmm8");
		asm volatile ("pand %xmm11,%xmm9");

		/* lane 3: Dy ^= C1 * Qdelta */
		asm volatile ("movdqa %xmm14,%xmm7");
		asm volatile ("pshufb %xmm8,%xmm7");
		asm volatile ("pxor %xmm7,%xmm10");
		asm volatile ("movdqa %xmm15,%xmm7");
		asm volatile ("pshufb %xmm9,%xmm7");
		asm volatile ("pxor %xmm7,%xmm10");

		/* lane 3: Dx = Pdelta ^ Dy */
		asm volatile ("pxor %xmm10,%xmm3");

		/* recovery data must remain cacheable */
		asm volatile ("movdqa %%xmm3,%0" : "=m" (pa[i + 48]));
		asm volatile ("movdqa %%xmm10,%0" : "=m" (qa[i + 48]));

		/* restore the polynomial for the next 64-byte iteration */
		asm volatile ("movdqa %0,%%xmm11" : : "m" (gfconst16.poly[0]));
	}

	raid_sse_end();
}

/*
 * Recover multiple data failures using selected parity blocks with SSSE3 extended optimized for up to three failures.
 *
 * If P is available, keep the complete P delta syndrome in xmm6.
 * Reconstruct only nr - 1 missing blocks through the inverse matrix and
 * obtain the last block by XORing the reconstructed blocks out of Pdelta.
 */
static __always_inline void raid_recX_ssse3ext_123(int nr, int has_p, int *id, int *ip, int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p[RAID_PARITY_MAX];
	uint8_t *pa[RAID_PARITY_MAX];
	uint8_t *src[RAID_DATA_MAX];
	uint8_t G[RAID_PARITY_MAX * RAID_PARITY_MAX];
	uint8_t V[RAID_PARITY_MAX * RAID_PARITY_MAX];
	const uint8_t *S[RAID_DATA_MAX][RAID_PARITY_MAX];
	const uint8_t *R[RAID_PARITY_MAX][RAID_PARITY_MAX];
	size_t i;
	int d, j, k, s;
	int ns;

	BUG_ON(nr < 1 || nr > 3);

	for (j = 0; j < nr; ++j)
		for (k = 0; k < nr; ++k)
			G[j * nr + k] = A(ip[j], id[k]);

	raid_invert(G, V, nr);

	for (j = 0; j < nr; ++j) {
		p[j] = v[nd + ip[j]];
		pa[j] = v[id[j]];
	}

	ns = 0;
	k = 0;

	for (d = 0; d < nd; ++d) {
		if (k < nr && d == id[k]) {
			++k;
			continue;
		}

		src[ns] = v[d];

		for (j = 0; j < nr; ++j)
			S[ns][j] = &raid_gfmulpshufb[A(ip[j], d)][0][0];

		++ns;
	}

	BUG_ON(k != nr);
	BUG_ON(ns != nd - nr);

	for (j = 0; j < nr; ++j)
		for (k = 0; k < nr; ++k)
			R[j][k] = &raid_gfmulpshufb[V[j * nr + k]][0][0];

	raid_sse_begin();

	for (i = 0; i < size; i += 32) {
		/*
		 * Xmm15 is later reused by reconstruction, therefore reload the
		 * nibble mask on every iteration.
		 */
		asm volatile ("movdqa %0,%%xmm15" : : "m" (gfconst16.low4[0]));

		asm volatile ("movdqa %0,%%xmm0" : : "m" (p[0][i]));
		asm volatile ("movdqa %0,%%xmm1" : : "m" (p[0][i + 16]));

		if (nr >= 2) {
			asm volatile ("movdqa %0,%%xmm2" : : "m" (p[1][i]));
			asm volatile ("movdqa %0,%%xmm3" : : "m" (p[1][i + 16]));
		}

		if (nr >= 3) {
			asm volatile ("movdqa %0,%%xmm4" : : "m" (p[2][i]));
			asm volatile ("movdqa %0,%%xmm5" : : "m" (p[2][i + 16]));
		}

		for (s = 0; s < ns; ++s) {
			const uint8_t **t = S[s];

			asm volatile ("movdqa %0,%%xmm6" : : "m" (src[s][i]));
			asm volatile ("movdqa %0,%%xmm7" : : "m" (src[s][i + 16]));

			if (has_p) {
				asm volatile ("pxor %xmm6,%xmm0");
				asm volatile ("pxor %xmm7,%xmm1");

				asm volatile ("movdqa %xmm6,%xmm8");
				asm volatile ("movdqa %xmm7,%xmm9");
				asm volatile ("psrlw $4,%xmm8");
				asm volatile ("psrlw $4,%xmm9");
				asm volatile ("pand %xmm15,%xmm6");
				asm volatile ("pand %xmm15,%xmm7");
				asm volatile ("pand %xmm15,%xmm8");
				asm volatile ("pand %xmm15,%xmm9");
			} else {
				asm volatile ("movdqa %xmm6,%xmm8");
				asm volatile ("movdqa %xmm7,%xmm9");
				asm volatile ("psrlw $4,%xmm8");
				asm volatile ("psrlw $4,%xmm9");
				asm volatile ("pand %xmm15,%xmm6");
				asm volatile ("pand %xmm15,%xmm7");
				asm volatile ("pand %xmm15,%xmm8");
				asm volatile ("pand %xmm15,%xmm9");

				asm volatile ("movdqa %0,%%xmm10" : : "m" (t[0][0]));
				asm volatile ("movdqa %0,%%xmm11" : : "m" (t[0][16]));
				asm volatile ("movdqa %xmm10,%xmm12");
				asm volatile ("movdqa %xmm11,%xmm13");
				asm volatile ("pshufb %xmm6,%xmm10");
				asm volatile ("pshufb %xmm8,%xmm11");
				asm volatile ("pxor %xmm11,%xmm10");
				asm volatile ("pxor %xmm10,%xmm0");
				asm volatile ("pshufb %xmm7,%xmm12");
				asm volatile ("pshufb %xmm9,%xmm13");
				asm volatile ("pxor %xmm13,%xmm12");
				asm volatile ("pxor %xmm12,%xmm1");
			}

			if (nr >= 2) {
				asm volatile ("movdqa %0,%%xmm10" : : "m" (t[1][0]));
				asm volatile ("movdqa %0,%%xmm11" : : "m" (t[1][16]));
				asm volatile ("movdqa %xmm10,%xmm12");
				asm volatile ("movdqa %xmm11,%xmm13");
				asm volatile ("pshufb %xmm6,%xmm10");
				asm volatile ("pshufb %xmm8,%xmm11");
				asm volatile ("pxor %xmm11,%xmm10");
				asm volatile ("pxor %xmm10,%xmm2");
				asm volatile ("pshufb %xmm7,%xmm12");
				asm volatile ("pshufb %xmm9,%xmm13");
				asm volatile ("pxor %xmm13,%xmm12");
				asm volatile ("pxor %xmm12,%xmm3");
			}

			if (nr >= 3) {
				asm volatile ("movdqa %0,%%xmm10" : : "m" (t[2][0]));
				asm volatile ("movdqa %0,%%xmm11" : : "m" (t[2][16]));
				asm volatile ("movdqa %xmm10,%xmm12");
				asm volatile ("movdqa %xmm11,%xmm13");
				asm volatile ("pshufb %xmm6,%xmm10");
				asm volatile ("pshufb %xmm8,%xmm11");
				asm volatile ("pxor %xmm11,%xmm10");
				asm volatile ("pxor %xmm10,%xmm4");
				asm volatile ("pshufb %xmm7,%xmm12");
				asm volatile ("pshufb %xmm9,%xmm13");
				asm volatile ("pxor %xmm13,%xmm12");
				asm volatile ("pxor %xmm12,%xmm5");
			}
		}

		if (nr >= 3) {
			asm volatile ("movdqa %xmm4,%xmm8");
			asm volatile ("movdqa %xmm4,%xmm9");
			asm volatile ("psrlw $4,%xmm9");
			asm volatile ("pand %xmm15,%xmm8");
			asm volatile ("pand %xmm15,%xmm9");

			asm volatile ("movdqa %xmm5,%xmm10");
			asm volatile ("movdqa %xmm5,%xmm11");
			asm volatile ("psrlw $4,%xmm11");
			asm volatile ("pand %xmm15,%xmm10");
			asm volatile ("pand %xmm15,%xmm11");
		}

		if (nr >= 2) {
			asm volatile ("movdqa %xmm2,%xmm4");
			asm volatile ("movdqa %xmm2,%xmm5");
			asm volatile ("psrlw $4,%xmm5");
			asm volatile ("pand %xmm15,%xmm4");
			asm volatile ("pand %xmm15,%xmm5");

			asm volatile ("movdqa %xmm3,%xmm6");
			asm volatile ("movdqa %xmm3,%xmm7");
			asm volatile ("psrlw $4,%xmm7");
			asm volatile ("pand %xmm15,%xmm6");
			asm volatile ("pand %xmm15,%xmm7");
		}

		asm volatile ("movdqa %xmm1,%xmm2");
		asm volatile ("movdqa %xmm1,%xmm3");
		asm volatile ("psrlw $4,%xmm3");
		asm volatile ("pand %xmm15,%xmm2");
		asm volatile ("pand %xmm15,%xmm3");

		asm volatile ("movdqa %xmm0,%xmm1");
		asm volatile ("psrlw $4,%xmm1");
		asm volatile ("pand %xmm15,%xmm0");
		asm volatile ("pand %xmm15,%xmm1");

		for (j = 0; j < nr; ++j) {
			const uint8_t **t = R[j];

			asm volatile ("pxor %xmm12,%xmm12");
			asm volatile ("pxor %xmm13,%xmm13");

			/* coefficient 0 */
			asm volatile ("movdqa %0,%%xmm14" : : "m" (t[0][0]));
			asm volatile ("movdqa %xmm14,%xmm15");
			asm volatile ("pshufb %xmm0,%xmm14");
			asm volatile ("pshufb %xmm2,%xmm15");
			asm volatile ("pxor %xmm14,%xmm12");
			asm volatile ("pxor %xmm15,%xmm13");

			asm volatile ("movdqa %0,%%xmm14" : : "m" (t[0][16]));
			asm volatile ("movdqa %xmm14,%xmm15");
			asm volatile ("pshufb %xmm1,%xmm14");
			asm volatile ("pshufb %xmm3,%xmm15");
			asm volatile ("pxor %xmm14,%xmm12");
			asm volatile ("pxor %xmm15,%xmm13");

			if (nr >= 2) {
				asm volatile ("movdqa %0,%%xmm14" : : "m" (t[1][0]));
				asm volatile ("movdqa %xmm14,%xmm15");
				asm volatile ("pshufb %xmm4,%xmm14");
				asm volatile ("pshufb %xmm6,%xmm15");
				asm volatile ("pxor %xmm14,%xmm12");
				asm volatile ("pxor %xmm15,%xmm13");

				asm volatile ("movdqa %0,%%xmm14" : : "m" (t[1][16]));
				asm volatile ("movdqa %xmm14,%xmm15");
				asm volatile ("pshufb %xmm5,%xmm14");
				asm volatile ("pshufb %xmm7,%xmm15");
				asm volatile ("pxor %xmm14,%xmm12");
				asm volatile ("pxor %xmm15,%xmm13");
			}

			if (nr >= 3) {
				asm volatile ("movdqa %0,%%xmm14" : : "m" (t[2][0]));
				asm volatile ("movdqa %xmm14,%xmm15");
				asm volatile ("pshufb %xmm8,%xmm14");
				asm volatile ("pshufb %xmm10,%xmm15");
				asm volatile ("pxor %xmm14,%xmm12");
				asm volatile ("pxor %xmm15,%xmm13");

				asm volatile ("movdqa %0,%%xmm14" : : "m" (t[2][16]));
				asm volatile ("movdqa %xmm14,%xmm15");
				asm volatile ("pshufb %xmm9,%xmm14");
				asm volatile ("pshufb %xmm11,%xmm15");
				asm volatile ("pxor %xmm14,%xmm12");
				asm volatile ("pxor %xmm15,%xmm13");
			}

			asm volatile ("movdqa %%xmm12,%0" : "=m" (pa[j][i]));
			asm volatile ("movdqa %%xmm13,%0" : "=m" (pa[j][i + 16]));
		}
	}

	raid_sse_end();
}

/*
 * Recover multiple data failures using selected parity blocks with SSSE3 extended optimized for up to five failures.
 *
 * If P is available, keep the complete P delta syndrome in xmm10.
 * Reconstruct only nr - 1 missing blocks through the inverse matrix and
 * obtain the last block by XORing the reconstructed blocks out of Pdelta.
 *
 * With at most five failures the low/high syndrome pairs use xmm0..xmm9,
 * leaving xmm10 available for the P delta accumulator.
 */
static __always_inline void raid_recX_ssse3ext_12345(int nr, int has_p, int *id, int *ip, int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p[RAID_PARITY_MAX];
	uint8_t *pa[RAID_PARITY_MAX];
	uint8_t *src[RAID_DATA_MAX];
	uint8_t G[RAID_PARITY_MAX * RAID_PARITY_MAX];
	uint8_t V[RAID_PARITY_MAX * RAID_PARITY_MAX];
	const uint8_t *S[RAID_DATA_MAX][RAID_PARITY_MAX];
	const uint8_t *R[RAID_PARITY_MAX][RAID_PARITY_MAX];
	size_t i;
	int d, j, k, s;
	int ns;

	BUG_ON(nr < 1 || nr > 5);

	for (j = 0; j < nr; ++j)
		for (k = 0; k < nr; ++k)
			G[j * nr + k] = A(ip[j], id[k]);

	raid_invert(G, V, nr);

	for (j = 0; j < nr; ++j) {
		p[j] = v[nd + ip[j]];
		pa[j] = v[id[j]];
	}

	ns = 0;
	k = 0;

	for (d = 0; d < nd; ++d) {
		if (k < nr && d == id[k]) {
			++k;
			continue;
		}

		src[ns] = v[d];

		for (j = 0; j < nr; ++j)
			S[ns][j] = &raid_gfmulpshufb[A(ip[j], d)][0][0];

		++ns;
	}

	BUG_ON(k != nr);
	BUG_ON(ns != nd - nr);

	/*
	 * If P is available the last inverse-matrix row isn't needed.
	 * The corresponding missing block is obtained by XOR.
	 */
	for (j = 0; j < nr - has_p; ++j)
		for (k = 0; k < nr; ++k)
			R[j][k] = &raid_gfmulpshufb[V[j * nr + k]][0][0];

	raid_sse_begin();

	for (i = 0; i < size; i += 16) {
		/*
		 * xmm15 is reused by reconstruction, therefore reload the
		 * nibble mask on every iteration.
		 */
		asm volatile ("movdqa %0,%%xmm15" : : "m" (gfconst16.low4[0]));

		/*
		 * Raw syndrome accumulators:
		 *
		 *   xmm0  syndrome 0
		 *   xmm1  syndrome 1
		 *   xmm2  syndrome 2
		 *   xmm3  syndrome 3
		 *   xmm4  syndrome 4
		 *
		 * xmm10 remains free for Pdelta.
		 */
		asm volatile ("movdqa %0,%%xmm0" : : "m" (p[0][i]));

		if (nr >= 2)
			asm volatile ("movdqa %0,%%xmm1" : : "m" (p[1][i]));

		if (nr >= 3)
			asm volatile ("movdqa %0,%%xmm2" : : "m" (p[2][i]));

		if (nr >= 4)
			asm volatile ("movdqa %0,%%xmm3" : : "m" (p[3][i]));

		if (nr >= 5)
			asm volatile ("movdqa %0,%%xmm4" : : "m" (p[4][i]));

		/* add all surviving data contributions */
		for (s = 0; s < ns; ++s) {
			const uint8_t **t = S[s];

			asm volatile ("movdqa %0,%%xmm6" : : "m" (src[s][i]));

			/*
			 * P has coefficient 1 for every data disk.
			 * XOR the original source before splitting it.
			 */
			if (has_p) {
				asm volatile ("pxor %xmm6,%xmm0");

				asm volatile ("movdqa %xmm6,%xmm7");
				asm volatile ("psrlw $4,%xmm7");
				asm volatile ("pand %xmm15,%xmm6");
				asm volatile ("pand %xmm15,%xmm7");
			} else {
				asm volatile ("movdqa %xmm6,%xmm7");
				asm volatile ("psrlw $4,%xmm7");
				asm volatile ("pand %xmm15,%xmm6");
				asm volatile ("pand %xmm15,%xmm7");

				asm volatile ("movdqa %0,%%xmm8" : : "m" (t[0][0]));
				asm volatile ("movdqa %0,%%xmm9" : : "m" (t[0][16]));
				asm volatile ("pshufb %xmm6,%xmm8");
				asm volatile ("pshufb %xmm7,%xmm9");
				asm volatile ("pxor %xmm8,%xmm0");
				asm volatile ("pxor %xmm9,%xmm0");
			}

			if (nr >= 2) {
				asm volatile ("movdqa %0,%%xmm8" : : "m" (t[1][0]));
				asm volatile ("movdqa %0,%%xmm9" : : "m" (t[1][16]));
				asm volatile ("pshufb %xmm6,%xmm8");
				asm volatile ("pshufb %xmm7,%xmm9");
				asm volatile ("pxor %xmm8,%xmm1");
				asm volatile ("pxor %xmm9,%xmm1");
			}

			if (nr >= 3) {
				asm volatile ("movdqa %0,%%xmm8" : : "m" (t[2][0]));
				asm volatile ("movdqa %0,%%xmm9" : : "m" (t[2][16]));
				asm volatile ("pshufb %xmm6,%xmm8");
				asm volatile ("pshufb %xmm7,%xmm9");
				asm volatile ("pxor %xmm8,%xmm2");
				asm volatile ("pxor %xmm9,%xmm2");
			}

			if (nr >= 4) {
				asm volatile ("movdqa %0,%%xmm8" : : "m" (t[3][0]));
				asm volatile ("movdqa %0,%%xmm9" : : "m" (t[3][16]));
				asm volatile ("pshufb %xmm6,%xmm8");
				asm volatile ("pshufb %xmm7,%xmm9");
				asm volatile ("pxor %xmm8,%xmm3");
				asm volatile ("pxor %xmm9,%xmm3");
			}

			if (nr >= 5) {
				asm volatile ("movdqa %0,%%xmm8" : : "m" (t[4][0]));
				asm volatile ("movdqa %0,%%xmm9" : : "m" (t[4][16]));
				asm volatile ("pshufb %xmm6,%xmm8");
				asm volatile ("pshufb %xmm7,%xmm9");
				asm volatile ("pxor %xmm8,%xmm4");
				asm volatile ("pxor %xmm9,%xmm4");
			}
		}

		/*
		 * Preserve the complete P delta before splitting syndrome 0.
		 *
		 * xmm10 = M0 ^ M1 ^ ... ^ M(nr-1)
		 */
		if (has_p)
			asm volatile ("movdqa %xmm0,%xmm10");

		/*
		 * Expand backwards to low/high syndrome pairs:
		 *
		 *   xmm0/xmm1  syndrome 0 low/high
		 *   xmm2/xmm3  syndrome 1 low/high
		 *   xmm4/xmm5  syndrome 2 low/high
		 *   xmm6/xmm7  syndrome 3 low/high
		 *   xmm8/xmm9  syndrome 4 low/high
		 *
		 * xmm10 remains the complete remaining P delta.
		 */
		if (nr >= 5) {
			asm volatile ("movdqa %xmm4,%xmm8");
			asm volatile ("movdqa %xmm4,%xmm9");
			asm volatile ("psrlw $4,%xmm9");
			asm volatile ("pand %xmm15,%xmm8");
			asm volatile ("pand %xmm15,%xmm9");
		}

		if (nr >= 4) {
			asm volatile ("movdqa %xmm3,%xmm6");
			asm volatile ("movdqa %xmm3,%xmm7");
			asm volatile ("psrlw $4,%xmm7");
			asm volatile ("pand %xmm15,%xmm6");
			asm volatile ("pand %xmm15,%xmm7");
		}

		if (nr >= 3) {
			asm volatile ("movdqa %xmm2,%xmm4");
			asm volatile ("movdqa %xmm2,%xmm5");
			asm volatile ("psrlw $4,%xmm5");
			asm volatile ("pand %xmm15,%xmm4");
			asm volatile ("pand %xmm15,%xmm5");
		}

		if (nr >= 2) {
			asm volatile ("movdqa %xmm1,%xmm2");
			asm volatile ("movdqa %xmm1,%xmm3");
			asm volatile ("psrlw $4,%xmm3");
			asm volatile ("pand %xmm15,%xmm2");
			asm volatile ("pand %xmm15,%xmm3");
		}

		asm volatile ("movdqa %xmm0,%xmm1");
		asm volatile ("psrlw $4,%xmm1");
		asm volatile ("pand %xmm15,%xmm0");
		asm volatile ("pand %xmm15,%xmm1");

		/*
		 * Reconstruct all but the last missing block when P is
		 * available.
		 *
		 * xmm12/xmm13 = low/high output accumulators
		 * xmm14/xmm15 = multiplication temporaries
		 * xmm10       = remaining P delta
		 */
		for (j = 0; j < nr - has_p; ++j) {
			const uint8_t **t = R[j];

			/* coefficient 0 initializes both accumulators */
			asm volatile ("movdqa %0,%%xmm12" : : "m" (t[0][0]));
			asm volatile ("movdqa %0,%%xmm13" : : "m" (t[0][16]));
			asm volatile ("pshufb %xmm0,%xmm12");
			asm volatile ("pshufb %xmm1,%xmm13");

			if (nr >= 2) {
				asm volatile ("movdqa %0,%%xmm14" : : "m" (t[1][0]));
				asm volatile ("movdqa %0,%%xmm15" : : "m" (t[1][16]));
				asm volatile ("pshufb %xmm2,%xmm14");
				asm volatile ("pshufb %xmm3,%xmm15");
				asm volatile ("pxor %xmm14,%xmm12");
				asm volatile ("pxor %xmm15,%xmm13");
			}

			if (nr >= 3) {
				asm volatile ("movdqa %0,%%xmm14" : : "m" (t[2][0]));
				asm volatile ("movdqa %0,%%xmm15" : : "m" (t[2][16]));
				asm volatile ("pshufb %xmm4,%xmm14");
				asm volatile ("pshufb %xmm5,%xmm15");
				asm volatile ("pxor %xmm14,%xmm12");
				asm volatile ("pxor %xmm15,%xmm13");
			}

			if (nr >= 4) {
				asm volatile ("movdqa %0,%%xmm14" : : "m" (t[3][0]));
				asm volatile ("movdqa %0,%%xmm15" : : "m" (t[3][16]));
				asm volatile ("pshufb %xmm6,%xmm14");
				asm volatile ("pshufb %xmm7,%xmm15");
				asm volatile ("pxor %xmm14,%xmm12");
				asm volatile ("pxor %xmm15,%xmm13");
			}

			if (nr >= 5) {
				asm volatile ("movdqa %0,%%xmm14" : : "m" (t[4][0]));
				asm volatile ("movdqa %0,%%xmm15" : : "m" (t[4][16]));
				asm volatile ("pshufb %xmm8,%xmm14");
				asm volatile ("pshufb %xmm9,%xmm15");
				asm volatile ("pxor %xmm14,%xmm12");
				asm volatile ("pxor %xmm15,%xmm13");
			}

			asm volatile ("pxor %xmm13,%xmm12");

			/*
			 * Remove the reconstructed block from Pdelta.
			 * After nr - 1 iterations xmm10 contains the final
			 * missing block.
			 */
			if (has_p)
				asm volatile ("pxor %xmm12,%xmm10");

			asm volatile ("movdqa %%xmm12,%0" : "=m" (pa[j][i]));
		}

		if (has_p)
			asm volatile ("movdqa %%xmm10,%0" : "=m" (pa[nr - 1][i]));
	}

	raid_sse_end();
}

/*
 * Recover multiple data failures using selected parity blocks with SSSE3 extended.
 *
 * This avoids raid_delta_gen(), temporary syndrome buffers, and the
 * generation of unused parity rows.
 *
 * If P is available, preserve the complete P delta syndrome and
 * reconstruct only nr - 1 missing blocks through the inverse matrix.
 * The last missing block is obtained by XORing the reconstructed blocks
 * out of Pdelta.
 */
static __always_inline void raid_recX_ssse3ext(int nr, int has_p, int *id, int *ip, int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p[RAID_PARITY_MAX];
	uint8_t *pa[RAID_PARITY_MAX];
	uint8_t *src[RAID_DATA_MAX];
	uint8_t G[RAID_PARITY_MAX * RAID_PARITY_MAX];
	uint8_t V[RAID_PARITY_MAX * RAID_PARITY_MAX];
	const uint8_t *S[RAID_DATA_MAX][RAID_PARITY_MAX];
	const uint8_t *R[RAID_PARITY_MAX][RAID_PARITY_MAX];
	size_t i;
	int d, j, k, s;
	int ns;

	BUG_ON(nr < 1 || nr > RAID_PARITY_MAX);

	for (j = 0; j < nr; ++j)
		for (k = 0; k < nr; ++k)
			G[j * nr + k] = A(ip[j], id[k]);

	raid_invert(G, V, nr);

	for (j = 0; j < nr; ++j) {
		p[j] = v[nd + ip[j]];
		pa[j] = v[id[j]];
	}

	ns = 0;
	k = 0;

	for (d = 0; d < nd; ++d) {
		if (k < nr && d == id[k]) {
			++k;
			continue;
		}

		src[ns] = v[d];

		for (j = 0; j < nr; ++j)
			S[ns][j] = &raid_gfmulpshufb[A(ip[j], d)][0][0];

		++ns;
	}

	BUG_ON(k != nr);
	BUG_ON(ns != nd - nr);

	for (j = 0; j < nr; ++j)
		for (k = 0; k < nr; ++k)
			R[j][k] = &raid_gfmulpshufb[V[j * nr + k]][0][0];

	raid_sse_begin();

	for (i = 0; i < size; i += 16) {
		/*
		 * Xmm15 is later reused by reconstruction, therefore reload the
		 * nibble mask on every iteration.
		 */
		asm volatile ("movdqa %0,%%xmm15" : : "m" (gfconst16.low4[0]));

		asm volatile ("movdqa %0,%%xmm0" : : "m" (p[0][i]));

		if (nr >= 2)
			asm volatile ("movdqa %0,%%xmm1" : : "m" (p[1][i]));

		if (nr >= 3)
			asm volatile ("movdqa %0,%%xmm2" : : "m" (p[2][i]));

		if (nr >= 4)
			asm volatile ("movdqa %0,%%xmm3" : : "m" (p[3][i]));

		if (nr >= 5)
			asm volatile ("movdqa %0,%%xmm4" : : "m" (p[4][i]));

		if (nr >= 6)
			asm volatile ("movdqa %0,%%xmm5" : : "m" (p[5][i]));

		for (s = 0; s < ns; ++s) {
			const uint8_t **t = S[s];

			asm volatile ("movdqa %0,%%xmm6" : : "m" (src[s][i]));

			if (has_p) {
				asm volatile ("pxor %xmm6,%xmm0");

				asm volatile ("movdqa %xmm6,%xmm7");
				asm volatile ("psrlw $4,%xmm7");
				asm volatile ("pand %xmm15,%xmm6");
				asm volatile ("pand %xmm15,%xmm7");
			} else {
				asm volatile ("movdqa %xmm6,%xmm7");
				asm volatile ("psrlw $4,%xmm7");
				asm volatile ("pand %xmm15,%xmm6");
				asm volatile ("pand %xmm15,%xmm7");

				asm volatile ("movdqa %0,%%xmm8" : : "m" (t[0][0]));
				asm volatile ("movdqa %0,%%xmm9" : : "m" (t[0][16]));
				asm volatile ("pshufb %xmm6,%xmm8");
				asm volatile ("pshufb %xmm7,%xmm9");
				asm volatile ("pxor %xmm8,%xmm0");
				asm volatile ("pxor %xmm9,%xmm0");
			}

			if (nr >= 2) {
				asm volatile ("movdqa %0,%%xmm8" : : "m" (t[1][0]));
				asm volatile ("movdqa %0,%%xmm9" : : "m" (t[1][16]));
				asm volatile ("pshufb %xmm6,%xmm8");
				asm volatile ("pshufb %xmm7,%xmm9");
				asm volatile ("pxor %xmm8,%xmm1");
				asm volatile ("pxor %xmm9,%xmm1");
			}

			if (nr >= 3) {
				asm volatile ("movdqa %0,%%xmm8" : : "m" (t[2][0]));
				asm volatile ("movdqa %0,%%xmm9" : : "m" (t[2][16]));
				asm volatile ("pshufb %xmm6,%xmm8");
				asm volatile ("pshufb %xmm7,%xmm9");
				asm volatile ("pxor %xmm8,%xmm2");
				asm volatile ("pxor %xmm9,%xmm2");
			}

			if (nr >= 4) {
				asm volatile ("movdqa %0,%%xmm8" : : "m" (t[3][0]));
				asm volatile ("movdqa %0,%%xmm9" : : "m" (t[3][16]));
				asm volatile ("pshufb %xmm6,%xmm8");
				asm volatile ("pshufb %xmm7,%xmm9");
				asm volatile ("pxor %xmm8,%xmm3");
				asm volatile ("pxor %xmm9,%xmm3");
			}

			if (nr >= 5) {
				asm volatile ("movdqa %0,%%xmm8" : : "m" (t[4][0]));
				asm volatile ("movdqa %0,%%xmm9" : : "m" (t[4][16]));
				asm volatile ("pshufb %xmm6,%xmm8");
				asm volatile ("pshufb %xmm7,%xmm9");
				asm volatile ("pxor %xmm8,%xmm4");
				asm volatile ("pxor %xmm9,%xmm4");
			}

			if (nr >= 6) {
				asm volatile ("movdqa %0,%%xmm8" : : "m" (t[5][0]));
				asm volatile ("movdqa %0,%%xmm9" : : "m" (t[5][16]));
				asm volatile ("pshufb %xmm6,%xmm8");
				asm volatile ("pshufb %xmm7,%xmm9");
				asm volatile ("pxor %xmm8,%xmm5");
				asm volatile ("pxor %xmm9,%xmm5");
			}
		}

		/* expand backwards to low/high pairs */
		if (nr >= 6) {
			asm volatile ("movdqa %xmm5,%xmm10");
			asm volatile ("movdqa %xmm5,%xmm11");
			asm volatile ("psrlw $4,%xmm11");
			asm volatile ("pand %xmm15,%xmm10");
			asm volatile ("pand %xmm15,%xmm11");
		}

		if (nr >= 5) {
			asm volatile ("movdqa %xmm4,%xmm8");
			asm volatile ("movdqa %xmm4,%xmm9");
			asm volatile ("psrlw $4,%xmm9");
			asm volatile ("pand %xmm15,%xmm8");
			asm volatile ("pand %xmm15,%xmm9");
		}

		if (nr >= 4) {
			asm volatile ("movdqa %xmm3,%xmm6");
			asm volatile ("movdqa %xmm3,%xmm7");
			asm volatile ("psrlw $4,%xmm7");
			asm volatile ("pand %xmm15,%xmm6");
			asm volatile ("pand %xmm15,%xmm7");
		}

		if (nr >= 3) {
			asm volatile ("movdqa %xmm2,%xmm4");
			asm volatile ("movdqa %xmm2,%xmm5");
			asm volatile ("psrlw $4,%xmm5");
			asm volatile ("pand %xmm15,%xmm4");
			asm volatile ("pand %xmm15,%xmm5");
		}

		if (nr >= 2) {
			asm volatile ("movdqa %xmm1,%xmm2");
			asm volatile ("movdqa %xmm1,%xmm3");
			asm volatile ("psrlw $4,%xmm3");
			asm volatile ("pand %xmm15,%xmm2");
			asm volatile ("pand %xmm15,%xmm3");
		}

		asm volatile ("movdqa %xmm0,%xmm1");
		asm volatile ("psrlw $4,%xmm1");
		asm volatile ("pand %xmm15,%xmm0");
		asm volatile ("pand %xmm15,%xmm1");

		for (j = 0; j < nr; ++j) {
			const uint8_t **t = R[j];

			asm volatile ("movdqa %0,%%xmm12" : : "m" (t[0][0]));
			asm volatile ("movdqa %0,%%xmm13" : : "m" (t[0][16]));
			asm volatile ("pshufb %xmm0,%xmm12");
			asm volatile ("pshufb %xmm1,%xmm13");

			if (nr >= 2) {
				asm volatile ("movdqa %0,%%xmm14" : : "m" (t[1][0]));
				asm volatile ("movdqa %0,%%xmm15" : : "m" (t[1][16]));
				asm volatile ("pshufb %xmm2,%xmm14");
				asm volatile ("pshufb %xmm3,%xmm15");
				asm volatile ("pxor %xmm14,%xmm12");
				asm volatile ("pxor %xmm15,%xmm13");
			}

			if (nr >= 3) {
				asm volatile ("movdqa %0,%%xmm14" : : "m" (t[2][0]));
				asm volatile ("movdqa %0,%%xmm15" : : "m" (t[2][16]));
				asm volatile ("pshufb %xmm4,%xmm14");
				asm volatile ("pshufb %xmm5,%xmm15");
				asm volatile ("pxor %xmm14,%xmm12");
				asm volatile ("pxor %xmm15,%xmm13");
			}

			if (nr >= 4) {
				asm volatile ("movdqa %0,%%xmm14" : : "m" (t[3][0]));
				asm volatile ("movdqa %0,%%xmm15" : : "m" (t[3][16]));
				asm volatile ("pshufb %xmm6,%xmm14");
				asm volatile ("pshufb %xmm7,%xmm15");
				asm volatile ("pxor %xmm14,%xmm12");
				asm volatile ("pxor %xmm15,%xmm13");
			}

			if (nr >= 5) {
				asm volatile ("movdqa %0,%%xmm14" : : "m" (t[4][0]));
				asm volatile ("movdqa %0,%%xmm15" : : "m" (t[4][16]));
				asm volatile ("pshufb %xmm8,%xmm14");
				asm volatile ("pshufb %xmm9,%xmm15");
				asm volatile ("pxor %xmm14,%xmm12");
				asm volatile ("pxor %xmm15,%xmm13");
			}

			if (nr >= 6) {
				asm volatile ("movdqa %0,%%xmm14" : : "m" (t[5][0]));
				asm volatile ("movdqa %0,%%xmm15" : : "m" (t[5][16]));
				asm volatile ("pshufb %xmm10,%xmm14");
				asm volatile ("pshufb %xmm11,%xmm15");
				asm volatile ("pxor %xmm14,%xmm12");
				asm volatile ("pxor %xmm15,%xmm13");
			}

			asm volatile ("pxor %xmm13,%xmm12");
			asm volatile ("movdqa %%xmm12,%0" : "=m" (pa[j][i]));
		}
	}

	raid_sse_end();
}

#endif

void raid_gen3_ssse3_raid(int nd, size_t size, void **vv)
{
	raid_gen3_ssse3_gen(nd, size, vv, 2);
}

void raid_gen3_ssse3_aes(int nd, size_t size, void **vv)
{
	raid_gen3_ssse3_gen(nd, size, vv, 3);
}

void raid_gen4_ssse3_raid(int nd, size_t size, void **vv)
{
	raid_gen4_ssse3_gen(nd, size, vv, 2);
}

void raid_gen4_ssse3_aes(int nd, size_t size, void **vv)
{
	raid_gen4_ssse3_gen(nd, size, vv, 3);
}

#ifdef CONFIG_X86_64
void raid_gen3_ssse3ext_raid(int nd, size_t size, void **vv)
{
	raid_gen3_ssse3ext_gen(nd, size, vv, 2);
}

void raid_gen3_ssse3ext_aes(int nd, size_t size, void **vv)
{
	raid_gen3_ssse3ext_gen(nd, size, vv, 3);
}

void raid_gen4_ssse3ext_raid(int nd, size_t size, void **vv)
{
	raid_gen4_ssse3ext_gen(nd, size, vv, 2);
}

void raid_gen4_ssse3ext_aes(int nd, size_t size, void **vv)
{
	raid_gen4_ssse3ext_gen(nd, size, vv, 3);
}

void raid_gen5_ssse3ext_raid(int nd, size_t size, void **vv)
{
	raid_genX_ssse3ext(nd, size, vv, 5, 2);
}

void raid_gen5_ssse3ext_aes(int nd, size_t size, void **vv)
{
	raid_genX_ssse3ext(nd, size, vv, 5, 3);
}

void raid_gen6_ssse3ext_raid(int nd, size_t size, void **vv)
{
	raid_genX_ssse3ext(nd, size, vv, 6, 2);
}

void raid_gen6_ssse3ext_aes(int nd, size_t size, void **vv)
{
	raid_genX_ssse3ext(nd, size, vv, 6, 3);
}

#endif

void raid_rec1_ssse3(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 1);

	/* if recovering with P uses the delta function */
	if (ip[0] == 0) {
		raid_rec1of1(id, nd, size, vv);
		return;
	}

	/* if recovering with Q use the specialized function */
	if (ip[0] == 1) {
		raid_rec1_ssse3_q(id, ip, nd, size, vv);
		return;
	}

	raid_rec1_ssse3_1(id, ip, nd, size, vv);
}

void raid_rec2_ssse3(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 2);

	/* if recovering with P,Q use the specialized function */
	if (ip[0] == 0 && ip[1] == 1) {
		raid_rec2of2_ssse3(id, ip, nd, size, vv);
		return;
	}

	if (ip[0] == 0)
		raid_rec2_ssse3_2(1, id, ip, nd, size, vv);
	else
		raid_rec2_ssse3_2(0, id, ip, nd, size, vv);
}

void raid_rec3_ssse3(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 3);
	if (ip[0] == 0)
		raid_recX_ssse3_1234(3, 1, id, ip, nd, size, vv);
	else
		raid_recX_ssse3_1234(3, 0, id, ip, nd, size, vv);
}

void raid_rec4_ssse3(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 4);
	if (ip[0] == 0)
		raid_recX_ssse3_1234(4, 1, id, ip, nd, size, vv);
	else
		raid_recX_ssse3_1234(4, 0, id, ip, nd, size, vv);
}

void raid_rec5_ssse3(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 5);
	if (ip[0] == 0)
		raid_recX_ssse3(5, 1, id, ip, nd, size, vv);
	else
		raid_recX_ssse3(5, 0, id, ip, nd, size, vv);
}

void raid_rec6_ssse3(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 6);
	if (ip[0] == 0)
		raid_recX_ssse3(6, 1, id, ip, nd, size, vv);
	else
		raid_recX_ssse3(6, 0, id, ip, nd, size, vv);
}

#ifdef CONFIG_X86_64
void raid_rec1_ssse3ext(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 1);

	/* if recovering with P uses the delta function */
	if (ip[0] == 0) {
		raid_rec1of1(id, nd, size, vv);
		return;
	}

	/* if recovering with Q use the specialized function */
	if (ip[0] == 1) {
		raid_rec1_ssse3ext_q(id, ip, nd, size, vv);
		return;
	}

	raid_rec1_ssse3ext_1(id, ip, nd, size, vv);
}

void raid_rec2_ssse3ext(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 2);

	/* if recovering with P,Q use the specialized function */
	if (ip[0] == 0 && ip[1] == 1) {
		raid_rec2of2_ssse3ext(id, ip, nd, size, vv);
		return;
	}

	if (ip[0] == 0)
		raid_rec2_ssse3ext_2(1, id, ip, nd, size, vv);
	else
		raid_rec2_ssse3ext_2(0, id, ip, nd, size, vv);
}

void raid_rec3_ssse3ext(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 3);
	if (ip[0] == 0)
		raid_recX_ssse3ext_123(3, 1, id, ip, nd, size, vv);
	else
		raid_recX_ssse3ext_123(3, 0, id, ip, nd, size, vv);
}

void raid_rec4_ssse3ext(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 4);
	if (ip[0] == 0)
		raid_recX_ssse3ext_12345(4, 1, id, ip, nd, size, vv);
	else
		raid_recX_ssse3ext_12345(4, 0, id, ip, nd, size, vv);
}

void raid_rec5_ssse3ext(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 5);
	if (ip[0] == 0)
		raid_recX_ssse3ext_12345(5, 1, id, ip, nd, size, vv);
	else
		raid_recX_ssse3ext_12345(5, 0, id, ip, nd, size, vv);
}

void raid_rec6_ssse3ext(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 6);
	if (ip[0] == 0)
		raid_recX_ssse3ext(6, 1, id, ip, nd, size, vv);
	else
		raid_recX_ssse3ext(6, 0, id, ip, nd, size, vv);
}
#endif

void raid_register_ssse3(void)
{
	if (raid_cpu_has_ssse3()) {
		raid_gen_register(RAID_ALGO_CAUCHY_PAR3, "ssse3", raid_gen3_ssse3_raid, RAID_POLY_RAID);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR3, "ssse3", raid_gen3_ssse3_aes, RAID_POLY_AES);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR4, "ssse3", raid_gen4_ssse3_raid, RAID_POLY_RAID);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR4, "ssse3", raid_gen4_ssse3_aes, RAID_POLY_AES);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR5, "ssse3", raid_gen5_ssse3_raid, RAID_POLY_RAID);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR6, "ssse3", raid_gen6_ssse3_raid, RAID_POLY_RAID);

		raid_rec_register(RAID_ALGO_CAUCHY_PAR1, "ssse3", raid_rec1_ssse3, RAID_POLY_ANY);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR2, "ssse3", raid_rec2_ssse3, RAID_POLY_ANY);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR3, "ssse3", raid_rec3_ssse3, RAID_POLY_ANY);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR4, "ssse3", raid_rec4_ssse3, RAID_POLY_ANY);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR5, "ssse3", raid_rec5_ssse3, RAID_POLY_ANY);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR6, "ssse3", raid_rec6_ssse3, RAID_POLY_ANY);

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

			raid_rec_register(RAID_ALGO_CAUCHY_PAR1, "ssse3e", raid_rec1_ssse3ext, RAID_POLY_ANY);
			raid_rec_register(RAID_ALGO_CAUCHY_PAR2, "ssse3e", raid_rec2_ssse3ext, RAID_POLY_ANY);
			raid_rec_register(RAID_ALGO_CAUCHY_PAR3, "ssse3e", raid_rec3_ssse3ext, RAID_POLY_ANY);
			raid_rec_register(RAID_ALGO_CAUCHY_PAR4, "ssse3e", raid_rec4_ssse3ext, RAID_POLY_ANY);
			raid_rec_register(RAID_ALGO_CAUCHY_PAR5, "ssse3e", raid_rec5_ssse3ext, RAID_POLY_ANY);
			raid_rec_register(RAID_ALGO_CAUCHY_PAR6, "ssse3e", raid_rec6_ssse3ext, RAID_POLY_ANY);
		}
#endif
	}
}

#endif
