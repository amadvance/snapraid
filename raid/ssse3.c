// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Andrea Mazzoleni

#include "internal.h"
#include "gf.h"
#include "cpu.h"

#ifdef CONFIG_X86
/*
 * GEN3 (triple parity with Cauchy matrix) SSSE3 implementation
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
 * GEN4 (quad parity with Cauchy matrix) SSSE3 implementation
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
 * GEN3 (triple parity with Cauchy matrix) SSSE3 implementation
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
 * GEN4 (quad parity with Cauchy matrix) SSSE3 implementation
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
 * GENX SSSE3ext implementation
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
 * RAID recovering for one disk SSSE3 implementation
 */
static __always_inline void raid_rec1_ssse3_delta(int *id, int *ip, int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *pa;
	uint8_t G;
	uint8_t V;
	size_t i;

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
	asm volatile ("movdqa %0,%%xmm4" : : "m" (raid_gfmulpshufb[V][0][0]));
	asm volatile ("movdqa %0,%%xmm5" : : "m" (raid_gfmulpshufb[V][1][0]));

	for (i = 0; i < size; i += 16) {
		asm volatile ("movdqa %0,%%xmm0" : : "m" (p[i]));
		asm volatile ("movdqa %0,%%xmm1" : : "m" (pa[i]));
		asm volatile ("movdqa %xmm4,%xmm2");
		asm volatile ("movdqa %xmm5,%xmm3");
		asm volatile ("pxor %xmm0,%xmm1");
		asm volatile ("movdqa %xmm1,%xmm0");
		asm volatile ("psrlw $4,%xmm1");
		asm volatile ("pand %xmm7,%xmm0");
		asm volatile ("pand %xmm7,%xmm1");
		asm volatile ("pshufb %xmm0,%xmm2");
		asm volatile ("pshufb %xmm1,%xmm3");
		asm volatile ("pxor %xmm3,%xmm2");
		asm volatile ("movdqa %%xmm2,%0" : "=m" (pa[i]));
	}

	raid_sse_end();
}

/*
 * RAID recovering for two disks SSSE3 implementation
 */
static __always_inline void raid_rec2_ssse3_delta(int *id, int *ip, int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p[2];
	uint8_t *pa[2];
	uint8_t G[2 * 2];
	uint8_t V[2 * 2];
	size_t i;
	int j, k;

	/* setup the coefficients matrix */
	for (j = 0; j < 2; ++j)
		for (k = 0; k < 2; ++k)
			G[j * 2 + k] = A(ip[j], id[k]);

	/* invert it to solve the system of linear equations */
	raid_invert(G, V, 2);

	/* compute delta parity */
	raid_delta_gen(2, id, ip, nd, size, vv);

	for (j = 0; j < 2; ++j) {
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
		asm volatile ("pxor %xmm2,%xmm0");
		asm volatile ("pxor %xmm3,%xmm1");

		asm volatile ("pxor %xmm6,%xmm6");

		asm volatile ("movdqa %0,%%xmm2" : : "m" (raid_gfmulpshufb[V[0]][0][0]));
		asm volatile ("movdqa %0,%%xmm3" : : "m" (raid_gfmulpshufb[V[0]][1][0]));
		asm volatile ("movdqa %xmm0,%xmm4");
		asm volatile ("movdqa %xmm0,%xmm5");
		asm volatile ("psrlw $4,%xmm5");
		asm volatile ("pand %xmm7,%xmm4");
		asm volatile ("pand %xmm7,%xmm5");
		asm volatile ("pshufb %xmm4,%xmm2");
		asm volatile ("pshufb %xmm5,%xmm3");
		asm volatile ("pxor %xmm2,%xmm6");
		asm volatile ("pxor %xmm3,%xmm6");

		asm volatile ("movdqa %0,%%xmm2" : : "m" (raid_gfmulpshufb[V[1]][0][0]));
		asm volatile ("movdqa %0,%%xmm3" : : "m" (raid_gfmulpshufb[V[1]][1][0]));
		asm volatile ("movdqa %xmm1,%xmm4");
		asm volatile ("movdqa %xmm1,%xmm5");
		asm volatile ("psrlw $4,%xmm5");
		asm volatile ("pand %xmm7,%xmm4");
		asm volatile ("pand %xmm7,%xmm5");
		asm volatile ("pshufb %xmm4,%xmm2");
		asm volatile ("pshufb %xmm5,%xmm3");
		asm volatile ("pxor %xmm2,%xmm6");
		asm volatile ("pxor %xmm3,%xmm6");

		asm volatile ("movdqa %%xmm6,%0" : "=m" (pa[0][i]));

		asm volatile ("pxor %xmm6,%xmm6");

		asm volatile ("movdqa %0,%%xmm2" : : "m" (raid_gfmulpshufb[V[2]][0][0]));
		asm volatile ("movdqa %0,%%xmm3" : : "m" (raid_gfmulpshufb[V[2]][1][0]));
		asm volatile ("movdqa %xmm0,%xmm4");
		asm volatile ("movdqa %xmm0,%xmm5");
		asm volatile ("psrlw $4,%xmm5");
		asm volatile ("pand %xmm7,%xmm4");
		asm volatile ("pand %xmm7,%xmm5");
		asm volatile ("pshufb %xmm4,%xmm2");
		asm volatile ("pshufb %xmm5,%xmm3");
		asm volatile ("pxor %xmm2,%xmm6");
		asm volatile ("pxor %xmm3,%xmm6");

		asm volatile ("movdqa %0,%%xmm2" : : "m" (raid_gfmulpshufb[V[3]][0][0]));
		asm volatile ("movdqa %0,%%xmm3" : : "m" (raid_gfmulpshufb[V[3]][1][0]));
		asm volatile ("movdqa %xmm1,%xmm4");
		asm volatile ("movdqa %xmm1,%xmm5");
		asm volatile ("psrlw $4,%xmm5");
		asm volatile ("pand %xmm7,%xmm4");
		asm volatile ("pand %xmm7,%xmm5");
		asm volatile ("pshufb %xmm4,%xmm2");
		asm volatile ("pshufb %xmm5,%xmm3");
		asm volatile ("pxor %xmm2,%xmm6");
		asm volatile ("pxor %xmm3,%xmm6");

		asm volatile ("movdqa %%xmm6,%0" : "=m" (pa[1][i]));
	}

	raid_sse_end();
}

/*
 * Recover failure of two data blocks using P and Q SSSE3 implementation.
 */
static __always_inline void raid_rec2of2_ssse3(int *id, int *ip, int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *pa;
	uint8_t *q;
	uint8_t *qa;
	uint8_t C[2];
	size_t i;

	/* get multiplication coefficients */
	C[0] = inv(powgen(id[1] - id[0]) ^ 1);
	C[1] = inv(powgen(id[0]) ^ powgen(id[1]));

	/* compute delta parity */
	raid_delta_gen(2, id, ip, nd, size, vv);

	p = v[nd];
	q = v[nd + 1];
	pa = v[id[0]];
	qa = v[id[1]];

	raid_sse_begin();

	asm volatile ("movdqa %0,%%xmm7" : : "m" (gfconst16.low4[0]));

	for (i = 0; i < size; i += 16) {
		/* Pd */
		asm volatile ("movdqa %0,%%xmm0" : : "m" (p[i]));
		asm volatile ("pxor %0,%%xmm0" : : "m" (pa[i]));

		/* Qd */
		asm volatile ("movdqa %0,%%xmm1" : : "m" (q[i]));
		asm volatile ("pxor %0,%%xmm1" : : "m" (qa[i]));

		/* split Pd and Qd into low/high nibbles */
		asm volatile ("movdqa %xmm0,%xmm4");
		asm volatile ("movdqa %xmm1,%xmm5");
		asm volatile ("movdqa %xmm0,%xmm2");
		asm volatile ("movdqa %xmm1,%xmm3");
		asm volatile ("psrlw $4,%xmm2");
		asm volatile ("psrlw $4,%xmm3");
		asm volatile ("pand %xmm7,%xmm4");
		asm volatile ("pand %xmm7,%xmm5");
		asm volatile ("pand %xmm7,%xmm2");
		asm volatile ("pand %xmm7,%xmm3");

		/* C0 * Pd */
		asm volatile ("movdqa %0,%%xmm6" : : "m" (raid_gfmulpshufb[C[0]][0][0]));
		asm volatile ("pshufb %xmm4,%xmm6");

		asm volatile ("movdqa %0,%%xmm4" : : "m" (raid_gfmulpshufb[C[0]][1][0]));
		asm volatile ("pshufb %xmm2,%xmm4");
		asm volatile ("pxor %xmm4,%xmm6");

		/* C1 * Qd */
		asm volatile ("movdqa %0,%%xmm4" : : "m" (raid_gfmulpshufb[C[1]][0][0]));
		asm volatile ("pshufb %xmm5,%xmm4");
		asm volatile ("pxor %xmm4,%xmm6");

		asm volatile ("movdqa %0,%%xmm4" : : "m" (raid_gfmulpshufb[C[1]][1][0]));
		asm volatile ("pshufb %xmm3,%xmm4");
		asm volatile ("pxor %xmm4,%xmm6");

		/* xmm6 = Dy, xmm0 = Pd, so Dx = Pd ^ Dy */
		asm volatile ("pxor %xmm6,%xmm0");

		asm volatile ("movdqa %%xmm0,%0" : "=m" (pa[i]));
		asm volatile ("movdqa %%xmm6,%0" : "=m" (qa[i]));
	}

	raid_sse_end();
}

/*
 * RAID recovering SSSE3 implementation optimized for up to four failures.
 *
 * If P is available, keep the complete P delta syndrome in xmm6.
 * Reconstruct only nr - 1 missing blocks through the inverse matrix and
 * obtain the last block by XORing the reconstructed blocks out of Pdelta.
 *
 * After the survivor scan xmm6 is no longer needed, leaving it available
 * for the P delta accumulator.
 */
static __always_inline void raid_recX_ssse3_1234(int nr, int *id, int *ip, int nd, size_t size, void **vv)
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
	int has_p;

	BUG_ON(nr < 1 || nr > 4);

	for (j = 0; j < nr; ++j)
		for (k = 0; k < nr; ++k)
			G[j * nr + k] = A(ip[j], id[k]);

	raid_invert(G, V, nr);

	for (j = 0; j < nr; ++j) {
		p[j] = v[nd + ip[j]];
		pa[j] = v[id[j]];
	}

	has_p = ip[0] == 0;

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
 * RAID recovering SSSE3 implementation.
 *
 * If P is available, keep the complete P delta syndrome in xmm6.
 * Reconstruct only nr - 1 missing blocks through the inverse matrix and
 * obtain the last block by XORing the reconstructed blocks out of Pdelta.
 *
 * The completed syndromes are kept in temporary memory, leaving xmm6
 * available for the P delta accumulator during reconstruction.
 */
static __always_inline void raid_recX_ssse3(int nr, int *id, int *ip, int nd, size_t size, void **vv)
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
	int has_p;

	BUG_ON(nr < 1 || nr > RAID_PARITY_MAX);

	for (j = 0; j < nr; ++j)
		for (k = 0; k < nr; ++k)
			G[j * nr + k] = A(ip[j], id[k]);

	raid_invert(G, V, nr);

	for (j = 0; j < nr; ++j) {
		p[j] = v[nd + ip[j]];
		pa[j] = v[id[j]];
	}

	has_p = ip[0] == 0;

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
 * RAID recovering for one disk SSSE3 extended implementation.
 *
 * Process two 16-byte blocks per iteration, grouping equal operations across
 * both lanes to expose instruction-level parallelism.
 *
 * A four-way 64-byte unroll was tested but is slower than this two-way 32-byte
 * implementation.
 */
static __always_inline void raid_rec1_ssse3ext_delta(int *id, int *ip, int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *pa;
	uint8_t G;
	uint8_t V;
	size_t i;

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
	asm volatile ("movdqa %0,%%xmm4" : : "m" (raid_gfmulpshufb[V][0][0]));
	asm volatile ("movdqa %0,%%xmm5" : : "m" (raid_gfmulpshufb[V][1][0]));

	for (i = 0; i < size; i += 32) {
		asm volatile ("movdqa %0,%%xmm0" : : "m" (p[i]));
		asm volatile ("movdqa %0,%%xmm1" : : "m" (p[i + 16]));

		asm volatile ("movdqa %0,%%xmm8" : : "m" (pa[i]));
		asm volatile ("movdqa %0,%%xmm9" : : "m" (pa[i + 16]));

		asm volatile ("pxor %xmm8,%xmm0");
		asm volatile ("pxor %xmm9,%xmm1");

		asm volatile ("movdqa %xmm0,%xmm8");
		asm volatile ("movdqa %xmm1,%xmm9");

		asm volatile ("psrlw $4,%xmm8");
		asm volatile ("psrlw $4,%xmm9");

		asm volatile ("pand %xmm7,%xmm0");
		asm volatile ("pand %xmm7,%xmm1");

		asm volatile ("pand %xmm7,%xmm8");
		asm volatile ("pand %xmm7,%xmm9");

		asm volatile ("movdqa %xmm4,%xmm10");
		asm volatile ("movdqa %xmm4,%xmm11");

		asm volatile ("pshufb %xmm0,%xmm10");
		asm volatile ("pshufb %xmm1,%xmm11");

		asm volatile ("movdqa %xmm5,%xmm0");
		asm volatile ("movdqa %xmm5,%xmm1");

		asm volatile ("pshufb %xmm8,%xmm0");
		asm volatile ("pshufb %xmm9,%xmm1");

		asm volatile ("pxor %xmm10,%xmm0");
		asm volatile ("pxor %xmm11,%xmm1");

		asm volatile ("movdqa %%xmm0,%0" : "=m" (pa[i]));
		asm volatile ("movdqa %%xmm1,%0" : "=m" (pa[i + 16]));
	}

	raid_sse_end();
}

/*
 * RAID recovering for two disks SSSE3 extended implementation
 */
static __always_inline void raid_rec2_ssse3ext_delta(int *id, int *ip, int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p[2];
	uint8_t *pa[2];
	uint8_t G[2 * 2];
	uint8_t V[2 * 2];
	size_t i;
	int j, k;

	/* setup the coefficients matrix */
	for (j = 0; j < 2; ++j)
		for (k = 0; k < 2; ++k)
			G[j * 2 + k] = A(ip[j], id[k]);

	/* invert it to solve the system of linear equations */
	raid_invert(G, V, 2);

	/* compute delta parity */
	raid_delta_gen(2, id, ip, nd, size, vv);

	for (j = 0; j < 2; ++j) {
		p[j] = v[nd + ip[j]];
		pa[j] = v[id[j]];
	}

	raid_sse_begin();

	asm volatile ("movdqa %0,%%xmm6" : : "m" (gfconst16.low4[0]));

	/* the inverse matrix V[] is constant for the whole recovery */
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

		/* pa[0] = V[0] * d0 ^ V[1] * d1 */
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

		/* pa[1] = V[2] * d0 ^ V[3] * d1 */
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

	raid_sse_end();
}

/*
 * Recover failure of two data blocks using P and Q SSSE3 extended implementation.
 */
static __always_inline void raid_rec2of2_ssse3ext(int *id, int *ip, int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *pa;
	uint8_t *q;
	uint8_t *qa;
	uint8_t C[2];
	size_t i;

	/* get multiplication coefficients */
	C[0] = inv(powgen(id[1] - id[0]) ^ 1);
	C[1] = inv(powgen(id[0]) ^ powgen(id[1]));

	/* compute delta parity */
	raid_delta_gen(2, id, ip, nd, size, vv);

	p = v[nd];
	q = v[nd + 1];
	pa = v[id[0]];
	qa = v[id[1]];

	raid_sse_begin();

	asm volatile ("movdqa %0,%%xmm7" : : "m" (gfconst16.low4[0]));

	/* keep the four multiplication tables resident */
	asm volatile ("movdqa %0,%%xmm8" : : "m" (raid_gfmulpshufb[C[0]][0][0]));
	asm volatile ("movdqa %0,%%xmm9" : : "m" (raid_gfmulpshufb[C[0]][1][0]));
	asm volatile ("movdqa %0,%%xmm10" : : "m" (raid_gfmulpshufb[C[1]][0][0]));
	asm volatile ("movdqa %0,%%xmm11" : : "m" (raid_gfmulpshufb[C[1]][1][0]));

	for (i = 0; i < size; i += 16) {
		/* Pd */
		asm volatile ("movdqa %0,%%xmm0" : : "m" (p[i]));
		asm volatile ("pxor %0,%%xmm0" : : "m" (pa[i]));

		/* Qd */
		asm volatile ("movdqa %0,%%xmm1" : : "m" (q[i]));
		asm volatile ("pxor %0,%%xmm1" : : "m" (qa[i]));

		/* split Pd and Qd */
		asm volatile ("movdqa %xmm0,%xmm4");
		asm volatile ("movdqa %xmm1,%xmm5");
		asm volatile ("movdqa %xmm0,%xmm2");
		asm volatile ("movdqa %xmm1,%xmm3");
		asm volatile ("psrlw $4,%xmm2");
		asm volatile ("psrlw $4,%xmm3");
		asm volatile ("pand %xmm7,%xmm4");
		asm volatile ("pand %xmm7,%xmm5");
		asm volatile ("pand %xmm7,%xmm2");
		asm volatile ("pand %xmm7,%xmm3");

		/* C0 * Pd */
		asm volatile ("movdqa %xmm8,%xmm6");
		asm volatile ("pshufb %xmm4,%xmm6");
		asm volatile ("movdqa %xmm9,%xmm12");
		asm volatile ("pshufb %xmm2,%xmm12");
		asm volatile ("pxor %xmm12,%xmm6");

		/* C1 * Qd */
		asm volatile ("movdqa %xmm10,%xmm12");
		asm volatile ("pshufb %xmm5,%xmm12");
		asm volatile ("pxor %xmm12,%xmm6");
		asm volatile ("movdqa %xmm11,%xmm12");
		asm volatile ("pshufb %xmm3,%xmm12");
		asm volatile ("pxor %xmm12,%xmm6");

		/* Dy = xmm6, Dx = Pd ^ Dy */
		asm volatile ("pxor %xmm6,%xmm0");

		asm volatile ("movdqa %%xmm0,%0" : "=m" (pa[i]));
		asm volatile ("movdqa %%xmm6,%0" : "=m" (qa[i]));
	}

	raid_sse_end();
}

static __always_inline void raid_recX_ssse3ext_123(int nr, int *id, int *ip, int nd, size_t size, void **vv)
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
	int has_p;

	BUG_ON(nr < 1 || nr > 3);

	for (j = 0; j < nr; ++j)
		for (k = 0; k < nr; ++k)
			G[j * nr + k] = A(ip[j], id[k]);

	raid_invert(G, V, nr);

	for (j = 0; j < nr; ++j) {
		p[j] = v[nd + ip[j]];
		pa[j] = v[id[j]];
	}

	has_p = ip[0] == 0;

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
 * RAID recovering SSSE3 extended implementation optimized for up to five
 * failures.
 *
 * If P is available, keep the complete P delta syndrome in xmm10.
 * Reconstruct only nr - 1 missing blocks through the inverse matrix and
 * obtain the last block by XORing the reconstructed blocks out of Pdelta.
 *
 * With at most five failures the low/high syndrome pairs use xmm0..xmm9,
 * leaving xmm10 available for the P delta accumulator.
 */
static __always_inline void raid_recX_ssse3ext_12345(int nr, int *id, int *ip, int nd, size_t size, void **vv)
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
	int has_p;

	BUG_ON(nr < 1 || nr > 5);

	for (j = 0; j < nr; ++j)
		for (k = 0; k < nr; ++k)
			G[j * nr + k] = A(ip[j], id[k]);

	raid_invert(G, V, nr);

	for (j = 0; j < nr; ++j) {
		p[j] = v[nd + ip[j]];
		pa[j] = v[id[j]];
	}

	/* ip[] is ordered. If P is available, it is always ip[0] */
	has_p = ip[0] == 0;

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

static __always_inline void raid_recX_ssse3ext(int nr, int *id, int *ip, int nd, size_t size, void **vv)
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
	int has_p;

	BUG_ON(nr < 1 || nr > RAID_PARITY_MAX);

	for (j = 0; j < nr; ++j)
		for (k = 0; k < nr; ++k)
			G[j * nr + k] = A(ip[j], id[k]);

	raid_invert(G, V, nr);

	for (j = 0; j < nr; ++j) {
		p[j] = v[nd + ip[j]];
		pa[j] = v[id[j]];
	}

	has_p = ip[0] == 0;

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

/*
 * GEN5 (penta parity with Cauchy matrix) SSSE3 implementation
 *
 * Note that it uses 16 registers, meaning that x64 is required.
 */
void raid_gen5_ssse3ext_raid(int nd, size_t size, void **vv)
{
	raid_genX_ssse3ext(nd, size, vv, 5, 2);
}

void raid_gen5_ssse3ext_aes(int nd, size_t size, void **vv)
{
	raid_genX_ssse3ext(nd, size, vv, 5, 3);
}

/*
 * GEN6 (hexa parity with Cauchy matrix) SSSE3 implementation
 *
 * Note that it uses 16 registers, meaning that x64 is required.
 */
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

	raid_rec1_ssse3_delta(id, ip, nd, size, vv);
}

void raid_rec2_ssse3(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 2);

	/* if recovering with P,Q use the specialized function */
	if (ip[0] == 0 && ip[1] == 1) {
		raid_rec2of2_ssse3(id, ip, nd, size, vv);
		return;
	}

	raid_rec2_ssse3_delta(id, ip, nd, size, vv);
}

void raid_rec3_ssse3(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 3);
	raid_recX_ssse3_1234(3, id, ip, nd, size, vv);
}

void raid_rec4_ssse3(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 4);
	raid_recX_ssse3_1234(4, id, ip, nd, size, vv);
}

void raid_rec5_ssse3(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 5);
	raid_recX_ssse3(5, id, ip, nd, size, vv);
}

void raid_rec6_ssse3(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 6);
	raid_recX_ssse3(6, id, ip, nd, size, vv);
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

	raid_rec1_ssse3ext_delta(id, ip, nd, size, vv);
}

void raid_rec2_ssse3ext(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 2);

	/* if recovering with P,Q use the specialized function */
	if (ip[0] == 0 && ip[1] == 1) {
		raid_rec2of2_ssse3ext(id, ip, nd, size, vv);
		return;
	}

	raid_rec2_ssse3ext_delta(id, ip, nd, size, vv);
}

void raid_rec3_ssse3ext(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 3);
	raid_recX_ssse3ext_123(3, id, ip, nd, size, vv);
}

void raid_rec4_ssse3ext(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 4);
	raid_recX_ssse3ext_12345(4, id, ip, nd, size, vv);
}

void raid_rec5_ssse3ext(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 5);
	raid_recX_ssse3ext_12345(5, id, ip, nd, size, vv);
}

void raid_rec6_ssse3ext(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 6);
	raid_recX_ssse3ext(6, id, ip, nd, size, vv);
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
