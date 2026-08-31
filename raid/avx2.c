// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Andrea Mazzoleni

#include "internal.h"
#include "gf.h"
#include "cpu.h"

#ifdef CONFIG_X86
/*
 * Generate one parity block (RAID5 with XOR) using AVX2 implementation.
 *
 * Uses 64-byte chunks across two 32-byte YMM lanes.
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
 * Generate two parity blocks (RAID6 with Cauchy matrix) using AVX2 implementation.
 *
 * Uses Horner's method with 64-byte chunks across two 32-byte YMM lanes.
 */
static __always_inline void raid_gen2_avx2_gen(int nd, size_t size, void **vv, int generator)
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
			if (generator == 3) {
				asm volatile ("vmovdqa %ymm2,%ymm4");
				asm volatile ("vmovdqa %ymm3,%ymm5");
				asm volatile ("vpaddb %ymm2,%ymm2,%ymm2");
				asm volatile ("vpaddb %ymm3,%ymm3,%ymm3");
				asm volatile ("vpxor %ymm4,%ymm2,%ymm2");
				asm volatile ("vpxor %ymm5,%ymm3,%ymm3");
				asm volatile ("vpcmpgtb %ymm4,%ymm6,%ymm4");
				asm volatile ("vpcmpgtb %ymm5,%ymm6,%ymm5");
				asm volatile ("vpand %ymm7,%ymm4,%ymm4");
				asm volatile ("vpand %ymm7,%ymm5,%ymm5");
				asm volatile ("vpxor %ymm4,%ymm2,%ymm2");
				asm volatile ("vpxor %ymm5,%ymm3,%ymm3");
			} else {
				asm volatile ("vpcmpgtb %ymm2,%ymm6,%ymm4");
				asm volatile ("vpcmpgtb %ymm3,%ymm6,%ymm5");
				asm volatile ("vpaddb %ymm2,%ymm2,%ymm2");
				asm volatile ("vpaddb %ymm3,%ymm3,%ymm3");
				asm volatile ("vpand %ymm7,%ymm4,%ymm4");
				asm volatile ("vpand %ymm7,%ymm5,%ymm5");
				asm volatile ("vpxor %ymm4,%ymm2,%ymm2");
				asm volatile ("vpxor %ymm5,%ymm3,%ymm3");
			}

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
 * Generate two parity blocks (RAID6 with Cauchy matrix) using AVX2 extended implementation.
 *
 * Process two data disks at a time and process the two 32-byte lanes
 * sequentially.
 *
 * Note that it uses 16 registers, meaning that x64 is required.
 */
static __always_inline void raid_gen2_avx2ext_gen(int nd, size_t size, void **vv, int generator)
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

	asm volatile ("vpxor %ymm14,%ymm14,%ymm14");
	asm volatile ("vpbroadcastb %0,%%ymm15" : : "m" (gfconst16.poly[0]));

	for (i = 0; i < size; i += 64) {
		asm volatile ("vmovdqa %0,%%ymm0" : : "m" (v[l][i]));
		asm volatile ("vmovdqa %0,%%ymm1" : : "m" (v[l][i + 32]));
		asm volatile ("vmovdqa %ymm0,%ymm2");
		asm volatile ("vmovdqa %ymm1,%ymm3");

		/* process two disks per iteration */
		for (d = l - 1; d >= 1; d -= 2) {
			asm volatile ("vmovdqa %0,%%ymm4" : : "m" (v[d][i]));
			asm volatile ("vmovdqa %0,%%ymm5" : : "m" (v[d][i + 32]));
			asm volatile ("vmovdqa %0,%%ymm6" : : "m" (v[d - 1][i]));
			asm volatile ("vmovdqa %0,%%ymm7" : : "m" (v[d - 1][i + 32]));

			asm volatile ("vpcmpgtb %ymm2,%ymm14,%ymm10");
			asm volatile ("vpaddb %ymm2,%ymm2,%ymm8");
			asm volatile ("vpxor %ymm6,%ymm4,%ymm12");
			asm volatile ("vpand %ymm15,%ymm10,%ymm10");
			asm volatile ("vpxor %ymm10,%ymm8,%ymm8");
			asm volatile ("vpxor %ymm4,%ymm8,%ymm8");
			asm volatile ("vpcmpgtb %ymm8,%ymm14,%ymm10");
			asm volatile ("vpaddb %ymm8,%ymm8,%ymm8");
			asm volatile ("vpxor %ymm12,%ymm0,%ymm0");
			if (generator == 3)
				asm volatile ("vpxor %ymm2,%ymm12,%ymm12");
			asm volatile ("vpand %ymm15,%ymm10,%ymm10");
			asm volatile ("vpxor %ymm10,%ymm8,%ymm8");
			if (generator == 3)
				asm volatile ("vpxor %ymm12,%ymm8,%ymm2");
			else
				asm volatile ("vpxor %ymm6,%ymm8,%ymm2");

			asm volatile ("vpcmpgtb %ymm3,%ymm14,%ymm11");
			asm volatile ("vpaddb %ymm3,%ymm3,%ymm9");
			asm volatile ("vpxor %ymm7,%ymm5,%ymm13");
			asm volatile ("vpand %ymm15,%ymm11,%ymm11");
			asm volatile ("vpxor %ymm11,%ymm9,%ymm9");
			asm volatile ("vpxor %ymm5,%ymm9,%ymm9");
			asm volatile ("vpcmpgtb %ymm9,%ymm14,%ymm11");
			asm volatile ("vpaddb %ymm9,%ymm9,%ymm9");
			asm volatile ("vpxor %ymm13,%ymm1,%ymm1");
			if (generator == 3)
				asm volatile ("vpxor %ymm3,%ymm13,%ymm13");
			asm volatile ("vpand %ymm15,%ymm11,%ymm11");
			asm volatile ("vpxor %ymm11,%ymm9,%ymm9");
			if (generator == 3)
				asm volatile ("vpxor %ymm13,%ymm9,%ymm3");
			else
				asm volatile ("vpxor %ymm7,%ymm9,%ymm3");
		}

		/* single remaining disk */
		if (d == 0) {
			asm volatile ("vmovdqa %0,%%ymm4" : : "m" (v[0][i]));
			asm volatile ("vmovdqa %0,%%ymm5" : : "m" (v[0][i + 32]));

			asm volatile ("vpcmpgtb %ymm2,%ymm14,%ymm10");
			asm volatile ("vpaddb %ymm2,%ymm2,%ymm8");
			asm volatile ("vpxor %ymm4,%ymm0,%ymm0");
			if (generator == 3)
				asm volatile ("vpxor %ymm4,%ymm2,%ymm12");
			asm volatile ("vpand %ymm15,%ymm10,%ymm10");
			asm volatile ("vpxor %ymm10,%ymm8,%ymm8");
			if (generator == 3)
				asm volatile ("vpxor %ymm12,%ymm8,%ymm2");
			else
				asm volatile ("vpxor %ymm4,%ymm8,%ymm2");

			asm volatile ("vpcmpgtb %ymm3,%ymm14,%ymm11");
			asm volatile ("vpaddb %ymm3,%ymm3,%ymm9");
			asm volatile ("vpxor %ymm5,%ymm1,%ymm1");
			if (generator == 3)
				asm volatile ("vpxor %ymm5,%ymm3,%ymm13");
			asm volatile ("vpand %ymm15,%ymm11,%ymm11");
			asm volatile ("vpxor %ymm11,%ymm9,%ymm9");
			if (generator == 3)
				asm volatile ("vpxor %ymm13,%ymm9,%ymm3");
			else
				asm volatile ("vpxor %ymm5,%ymm9,%ymm3");
		}

		asm volatile ("vmovntdq %%ymm0,%0" : "=m" (p[i]));
		asm volatile ("vmovntdq %%ymm1,%0" : "=m" (p[i + 32]));
		asm volatile ("vmovntdq %%ymm2,%0" : "=m" (q[i]));
		asm volatile ("vmovntdq %%ymm3,%0" : "=m" (q[i + 32]));
	}

	raid_avx_end();
}

/*
 * Generate three parity blocks with powers of 2^-1 using AVX2 extended implementation.
 *
 * Note that it uses 16 registers, meaning that x64 is required.
 */
void raid_genz_avx2ext_raid(int nd, size_t size, void **vv)
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

/*
 * Generate three parity blocks with Cauchy matrix using AVX2 extended implementation.
 *
 * Uses the extended register set (16 YMM registers) and processes two disks
 * per iteration across two 32-byte YMM lanes (64 bytes/step).
 *
 * Note that it uses 16 registers, meaning that x64 is required.
 */
static __always_inline void raid_gen3_avx2ext_gen(int nd, size_t size, void **vv, int generator)
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

	raid_avx_begin();

	/* generic case with at least two data disks */
	asm volatile ("vpbroadcastb %0, %%ymm3" : : "m" (gfconst16.poly[0]));
	asm volatile ("vpbroadcastb %0, %%ymm11" : : "m" (gfconst16.low4[0]));

	for (i = 0; i < size; i += 64) {
		/* last disk without the generator multiplication */
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

		asm volatile ("vbroadcasti128 %0,%%ymm10" : : "m" (raid_gfcauchypshufb[l][1][0][0]));
		asm volatile ("vbroadcasti128 %0,%%ymm15" : : "m" (raid_gfcauchypshufb[l][1][1][0]));
		asm volatile ("vpshufb %ymm4,%ymm10,%ymm2");
		asm volatile ("vpshufb %ymm12,%ymm10,%ymm10");
		asm volatile ("vpshufb %ymm5,%ymm15,%ymm7");
		asm volatile ("vpshufb %ymm13,%ymm15,%ymm15");
		asm volatile ("vpxor   %ymm7,%ymm2,%ymm2");
		asm volatile ("vpxor   %ymm15,%ymm10,%ymm10");

		/* process two intermediate disks per iteration */
		for (d = l - 1; d > 1; d -= 2) {
			asm volatile ("vmovdqa %0,%%ymm4" : : "m" (v[d][i]));
			asm volatile ("vmovdqa %0,%%ymm12" : : "m" (v[d][i + 32]));

			asm volatile ("vbroadcasti128 %0,%%ymm14" : : "m" (raid_gfcauchypshufb[d][1][0][0]));
			asm volatile ("vbroadcasti128 %0,%%ymm15" : : "m" (raid_gfcauchypshufb[d][1][1][0]));

			if (generator == 3)
				asm volatile ("vmovdqa %ymm1,%ymm6");
			asm volatile ("vpxor %ymm5,%ymm5,%ymm5");
			asm volatile ("vpcmpgtb %ymm1,%ymm5,%ymm5");
			asm volatile ("vpaddb %ymm1,%ymm1,%ymm1");
			asm volatile ("vpand %ymm3,%ymm5,%ymm5");
			asm volatile ("vpxor %ymm5,%ymm1,%ymm1");
			if (generator == 3)
				asm volatile ("vpxor %ymm6,%ymm1,%ymm1");

			asm volatile ("vpxor %ymm4,%ymm0,%ymm0");
			asm volatile ("vpxor %ymm4,%ymm1,%ymm1");

			asm volatile ("vpsrlw $4,%ymm4,%ymm5");
			asm volatile ("vpand %ymm11,%ymm4,%ymm4");
			asm volatile ("vpand %ymm11,%ymm5,%ymm5");

			asm volatile ("vpshufb %ymm4,%ymm14,%ymm6");
			asm volatile ("vpshufb %ymm5,%ymm15,%ymm7");
			asm volatile ("vpxor %ymm7,%ymm6,%ymm6");
			asm volatile ("vpxor %ymm6,%ymm2,%ymm2");

			if (generator == 3)
				asm volatile ("vmovdqa %ymm9,%ymm6");
			asm volatile ("vpxor %ymm13,%ymm13,%ymm13");
			asm volatile ("vpcmpgtb %ymm9,%ymm13,%ymm13");
			asm volatile ("vpaddb %ymm9,%ymm9,%ymm9");
			asm volatile ("vpand %ymm3,%ymm13,%ymm13");
			asm volatile ("vpxor %ymm13,%ymm9,%ymm9");
			if (generator == 3)
				asm volatile ("vpxor %ymm6,%ymm9,%ymm9");

			asm volatile ("vpxor %ymm12,%ymm8,%ymm8");
			asm volatile ("vpxor %ymm12,%ymm9,%ymm9");

			asm volatile ("vpsrlw $4,%ymm12,%ymm13");
			asm volatile ("vpand %ymm11,%ymm12,%ymm12");
			asm volatile ("vpand %ymm11,%ymm13,%ymm13");

			asm volatile ("vpshufb %ymm12,%ymm14,%ymm6");
			asm volatile ("vpshufb %ymm13,%ymm15,%ymm7");
			asm volatile ("vpxor %ymm7,%ymm6,%ymm6");
			asm volatile ("vpxor %ymm6,%ymm10,%ymm10");

			asm volatile ("vmovdqa %0,%%ymm4" : : "m" (v[d - 1][i]));
			asm volatile ("vmovdqa %0,%%ymm12" : : "m" (v[d - 1][i + 32]));

			asm volatile ("vbroadcasti128 %0,%%ymm14" : : "m" (raid_gfcauchypshufb[d - 1][1][0][0]));
			asm volatile ("vbroadcasti128 %0,%%ymm15" : : "m" (raid_gfcauchypshufb[d - 1][1][1][0]));

			if (generator == 3)
				asm volatile ("vmovdqa %ymm1,%ymm6");
			asm volatile ("vpxor %ymm5,%ymm5,%ymm5");
			asm volatile ("vpcmpgtb %ymm1,%ymm5,%ymm5");
			asm volatile ("vpaddb %ymm1,%ymm1,%ymm1");
			asm volatile ("vpand %ymm3,%ymm5,%ymm5");
			asm volatile ("vpxor %ymm5,%ymm1,%ymm1");
			if (generator == 3)
				asm volatile ("vpxor %ymm6,%ymm1,%ymm1");

			asm volatile ("vpxor %ymm4,%ymm0,%ymm0");
			asm volatile ("vpxor %ymm4,%ymm1,%ymm1");

			asm volatile ("vpsrlw $4,%ymm4,%ymm5");
			asm volatile ("vpand %ymm11,%ymm4,%ymm4");
			asm volatile ("vpand %ymm11,%ymm5,%ymm5");

			asm volatile ("vpshufb %ymm4,%ymm14,%ymm6");
			asm volatile ("vpshufb %ymm5,%ymm15,%ymm7");
			asm volatile ("vpxor %ymm7,%ymm6,%ymm6");
			asm volatile ("vpxor %ymm6,%ymm2,%ymm2");

			if (generator == 3)
				asm volatile ("vmovdqa %ymm9,%ymm6");
			asm volatile ("vpxor %ymm13,%ymm13,%ymm13");
			asm volatile ("vpcmpgtb %ymm9,%ymm13,%ymm13");
			asm volatile ("vpaddb %ymm9,%ymm9,%ymm9");
			asm volatile ("vpand %ymm3,%ymm13,%ymm13");
			asm volatile ("vpxor %ymm13,%ymm9,%ymm9");
			if (generator == 3)
				asm volatile ("vpxor %ymm6,%ymm9,%ymm9");

			asm volatile ("vpxor %ymm12,%ymm8,%ymm8");
			asm volatile ("vpxor %ymm12,%ymm9,%ymm9");

			asm volatile ("vpsrlw $4,%ymm12,%ymm13");
			asm volatile ("vpand %ymm11,%ymm12,%ymm12");
			asm volatile ("vpand %ymm11,%ymm13,%ymm13");

			asm volatile ("vpshufb %ymm12,%ymm14,%ymm6");
			asm volatile ("vpshufb %ymm13,%ymm15,%ymm7");
			asm volatile ("vpxor %ymm7,%ymm6,%ymm6");
			asm volatile ("vpxor %ymm6,%ymm10,%ymm10");
		}

		/* single remaining intermediate disk */
		if (d == 1) {
			asm volatile ("vmovdqa %0,%%ymm4" : : "m" (v[1][i]));
			asm volatile ("vmovdqa %0,%%ymm12" : : "m" (v[1][i + 32]));

			if (generator == 3)
				asm volatile ("vmovdqa %ymm1,%ymm6");
			asm volatile ("vpxor %ymm5,%ymm5,%ymm5");
			asm volatile ("vpcmpgtb %ymm1,%ymm5,%ymm5");
			asm volatile ("vpaddb %ymm1,%ymm1,%ymm1");
			asm volatile ("vpand %ymm3,%ymm5,%ymm5");
			asm volatile ("vpxor %ymm5,%ymm1,%ymm1");
			if (generator == 3)
				asm volatile ("vpxor %ymm6,%ymm1,%ymm1");

			asm volatile ("vpxor %ymm4,%ymm0,%ymm0");
			asm volatile ("vpxor %ymm4,%ymm1,%ymm1");

			asm volatile ("vpsrlw $4,%ymm4,%ymm5");
			asm volatile ("vpand %ymm11,%ymm4,%ymm4");
			asm volatile ("vpand %ymm11,%ymm5,%ymm5");

			asm volatile ("vbroadcasti128 %0,%%ymm14" : : "m" (raid_gfcauchypshufb[1][1][0][0]));
			asm volatile ("vbroadcasti128 %0,%%ymm15" : : "m" (raid_gfcauchypshufb[1][1][1][0]));

			asm volatile ("vpshufb %ymm4,%ymm14,%ymm6");
			asm volatile ("vpshufb %ymm5,%ymm15,%ymm7");
			asm volatile ("vpxor %ymm7,%ymm6,%ymm6");
			asm volatile ("vpxor %ymm6,%ymm2,%ymm2");

			if (generator == 3)
				asm volatile ("vmovdqa %ymm9,%ymm6");
			asm volatile ("vpxor %ymm13,%ymm13,%ymm13");
			asm volatile ("vpcmpgtb %ymm9,%ymm13,%ymm13");
			asm volatile ("vpaddb %ymm9,%ymm9,%ymm9");
			asm volatile ("vpand %ymm3,%ymm13,%ymm13");
			asm volatile ("vpxor %ymm13,%ymm9,%ymm9");
			if (generator == 3)
				asm volatile ("vpxor %ymm6,%ymm9,%ymm9");

			asm volatile ("vpxor %ymm12,%ymm8,%ymm8");
			asm volatile ("vpxor %ymm12,%ymm9,%ymm9");

			asm volatile ("vpsrlw $4,%ymm12,%ymm13");
			asm volatile ("vpand %ymm11,%ymm12,%ymm12");
			asm volatile ("vpand %ymm11,%ymm13,%ymm13");

			asm volatile ("vpshufb %ymm12,%ymm14,%ymm6");
			asm volatile ("vpshufb %ymm13,%ymm15,%ymm7");
			asm volatile ("vpxor %ymm7,%ymm6,%ymm6");
			asm volatile ("vpxor %ymm6,%ymm10,%ymm10");
		}

		/* first disk with all coefficients at 1 */
		asm volatile ("vmovdqa %0,%%ymm4" : : "m" (v[0][i]));
		asm volatile ("vmovdqa %0,%%ymm12" : : "m" (v[0][i + 32]));

		if (generator == 3) {
			asm volatile ("vmovdqa %ymm1,%ymm6");
			asm volatile ("vmovdqa %ymm9,%ymm14");
		}
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
		if (generator == 3) {
			asm volatile ("vpxor %ymm6,%ymm1,%ymm1");
			asm volatile ("vpxor %ymm14,%ymm9,%ymm9");
		}

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

/*
 * Generate four parity blocks with Cauchy matrix using AVX2 extended implementation.
 *
 * Uses the extended register set (16 YMM registers) and processes two disks
 * per iteration across two 32-byte YMM lanes (64 bytes/step).
 *
 * Note that it uses 16 registers, meaning that x64 is required.
 */
static __always_inline void raid_gen4_avx2ext_gen(int nd, size_t size, void **vv, int generator)
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

	raid_avx_begin();

	/* keep the constants resident for the whole function */
	asm volatile ("vpbroadcastb %0,%%ymm6" : : "m" (gfconst16.poly[0]));
	asm volatile ("vpbroadcastb %0,%%ymm7" : : "m" (gfconst16.low4[0]));

	/* generic case with at least two data disks */
	for (i = 0; i < size; i += 64) {
		/* last disk without the generator multiplication */
		asm volatile ("vmovdqa %0,%%ymm4" : : "m" (v[l][i]));
		asm volatile ("vmovdqa %0,%%ymm12" : : "m" (v[l][i + 32]));

		asm volatile ("vmovdqa %ymm4,%ymm0");
		asm volatile ("vmovdqa %ymm4,%ymm1");
		asm volatile ("vmovdqa %ymm12,%ymm8");
		asm volatile ("vmovdqa %ymm12,%ymm9");

		asm volatile ("vpsrlw $4,%ymm4,%ymm5");
		asm volatile ("vpand %ymm7,%ymm4,%ymm4");
		asm volatile ("vpand %ymm7,%ymm5,%ymm5");
		asm volatile ("vpsrlw $4,%ymm12,%ymm13");
		asm volatile ("vpand %ymm7,%ymm12,%ymm12");
		asm volatile ("vpand %ymm7,%ymm13,%ymm13");

		asm volatile ("vbroadcasti128 %0,%%ymm14" : : "m" (raid_gfcauchypshufb[l][1][0][0]));
		asm volatile ("vbroadcasti128 %0,%%ymm15" : : "m" (raid_gfcauchypshufb[l][1][1][0]));
		asm volatile ("vpshufb %ymm4,%ymm14,%ymm2");
		asm volatile ("vpshufb %ymm5,%ymm15,%ymm3");
		asm volatile ("vpxor %ymm3,%ymm2,%ymm2");
		asm volatile ("vpshufb %ymm12,%ymm14,%ymm10");
		asm volatile ("vpshufb %ymm13,%ymm15,%ymm11");
		asm volatile ("vpxor %ymm11,%ymm10,%ymm10");

		asm volatile ("vbroadcasti128 %0,%%ymm14" : : "m" (raid_gfcauchypshufb[l][2][0][0]));
		asm volatile ("vbroadcasti128 %0,%%ymm15" : : "m" (raid_gfcauchypshufb[l][2][1][0]));
		asm volatile ("vpshufb %ymm4,%ymm14,%ymm3");
		asm volatile ("vpshufb %ymm5,%ymm15,%ymm5");
		asm volatile ("vpxor %ymm5,%ymm3,%ymm3");
		asm volatile ("vpshufb %ymm12,%ymm14,%ymm11");
		asm volatile ("vpshufb %ymm13,%ymm15,%ymm13");
		asm volatile ("vpxor %ymm13,%ymm11,%ymm11");

		/* intermediate disks */
		for (d = l - 1; d > 0; --d) {
			if (generator == 3)
				asm volatile ("vmovdqa %ymm1,%ymm4");
			asm volatile ("vpxor %ymm5,%ymm5,%ymm5");
			asm volatile ("vpcmpgtb %ymm1,%ymm5,%ymm5");
			asm volatile ("vpaddb %ymm1,%ymm1,%ymm1");
			asm volatile ("vpand %ymm6,%ymm5,%ymm5");
			asm volatile ("vpxor %ymm5,%ymm1,%ymm1");
			if (generator == 3)
				asm volatile ("vpxor %ymm4,%ymm1,%ymm1");
			asm volatile ("vmovdqa %0,%%ymm4" : : "m" (v[d][i]));
			asm volatile ("vpxor %ymm4,%ymm0,%ymm0");
			asm volatile ("vpxor %ymm4,%ymm1,%ymm1");
			asm volatile ("vpsrlw $4,%ymm4,%ymm5");
			asm volatile ("vpand %ymm7,%ymm4,%ymm4");
			asm volatile ("vpand %ymm7,%ymm5,%ymm5");
			if (generator == 3)
				asm volatile ("vmovdqa %ymm9,%ymm12");
			asm volatile ("vpxor %ymm13,%ymm13,%ymm13");
			asm volatile ("vpcmpgtb %ymm9,%ymm13,%ymm13");
			asm volatile ("vpaddb %ymm9,%ymm9,%ymm9");
			asm volatile ("vpand %ymm6,%ymm13,%ymm13");
			asm volatile ("vpxor %ymm13,%ymm9,%ymm9");
			if (generator == 3)
				asm volatile ("vpxor %ymm12,%ymm9,%ymm9");

			asm volatile ("vmovdqa %0,%%ymm12" : : "m" (v[d][i + 32]));

			asm volatile ("vpxor %ymm12,%ymm8,%ymm8");
			asm volatile ("vpxor %ymm12,%ymm9,%ymm9");
			asm volatile ("vpsrlw $4,%ymm12,%ymm13");
			asm volatile ("vpand %ymm7,%ymm12,%ymm12");
			asm volatile ("vpand %ymm7,%ymm13,%ymm13");

			asm volatile ("vbroadcasti128 %0,%%ymm14" : : "m" (raid_gfcauchypshufb[d][1][0][0]));
			asm volatile ("vpshufb %ymm4,%ymm14,%ymm15");
			asm volatile ("vpshufb %ymm12,%ymm14,%ymm14");
			asm volatile ("vpxor %ymm15,%ymm2,%ymm2");
			asm volatile ("vpxor %ymm14,%ymm10,%ymm10");
			asm volatile ("vbroadcasti128 %0,%%ymm14" : : "m" (raid_gfcauchypshufb[d][1][1][0]));
			asm volatile ("vpshufb %ymm5,%ymm14,%ymm15");
			asm volatile ("vpshufb %ymm13,%ymm14,%ymm14");
			asm volatile ("vpxor %ymm15,%ymm2,%ymm2");
			asm volatile ("vpxor %ymm14,%ymm10,%ymm10");

			asm volatile ("vbroadcasti128 %0,%%ymm14" : : "m" (raid_gfcauchypshufb[d][2][0][0]));
			asm volatile ("vpshufb %ymm4,%ymm14,%ymm15");
			asm volatile ("vpshufb %ymm12,%ymm14,%ymm14");
			asm volatile ("vpxor %ymm15,%ymm3,%ymm3");
			asm volatile ("vpxor %ymm14,%ymm11,%ymm11");
			asm volatile ("vbroadcasti128 %0,%%ymm14" : : "m" (raid_gfcauchypshufb[d][2][1][0]));
			asm volatile ("vpshufb %ymm5,%ymm14,%ymm15");
			asm volatile ("vpshufb %ymm13,%ymm14,%ymm14");
			asm volatile ("vpxor %ymm15,%ymm3,%ymm3");
			asm volatile ("vpxor %ymm14,%ymm11,%ymm11");
		}

		/* first disk with all coefficients at 1 */
		if (generator == 3)
			asm volatile ("vmovdqa %ymm1,%ymm4");
		asm volatile ("vpxor %ymm5,%ymm5,%ymm5");
		asm volatile ("vpcmpgtb %ymm1,%ymm5,%ymm5");
		asm volatile ("vpaddb %ymm1,%ymm1,%ymm1");
		asm volatile ("vpand %ymm6,%ymm5,%ymm5");
		asm volatile ("vpxor %ymm5,%ymm1,%ymm1");
		if (generator == 3)
			asm volatile ("vpxor %ymm4,%ymm1,%ymm1");
		asm volatile ("vmovdqa %0,%%ymm4" : : "m" (v[0][i]));
		asm volatile ("vpxor %ymm4,%ymm0,%ymm0");
		asm volatile ("vpxor %ymm4,%ymm1,%ymm1");
		asm volatile ("vpxor %ymm4,%ymm2,%ymm2");
		asm volatile ("vpxor %ymm4,%ymm3,%ymm3");
		if (generator == 3)
			asm volatile ("vmovdqa %ymm9,%ymm12");
		asm volatile ("vpxor %ymm13,%ymm13,%ymm13");
		asm volatile ("vpcmpgtb %ymm9,%ymm13,%ymm13");
		asm volatile ("vpaddb %ymm9,%ymm9,%ymm9");
		asm volatile ("vpand %ymm6,%ymm13,%ymm13");
		asm volatile ("vpxor %ymm13,%ymm9,%ymm9");
		if (generator == 3)
			asm volatile ("vpxor %ymm12,%ymm9,%ymm9");

		asm volatile ("vmovdqa %0,%%ymm12" : : "m" (v[0][i + 32]));
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

/*
 * Generate N parity blocks with Cauchy matrix using AVX2 extended implementation.
 */
static __always_inline void raid_genX_avx2ext(int nd, size_t size, void **vv, int np, int generator)
{
	uint8_t **v = (uint8_t **)vv;
	size_t i;
	int d, l;

	l = nd - 1;

	/* special case with only one data disk */
	if (l == 0) {
		for (d = 0; d < np; ++d)
			if (v[1 + d] != v[0])
				memcpy(v[1 + d], v[0], size);
		return;
	}

	raid_avx_begin();

	/* generic case with at least two data disks */
	asm volatile ("vpxor %ymm6,%ymm6,%ymm6");
	asm volatile ("vpbroadcastb %0,%%ymm14" : : "m" (gfconst16.poly[0]));
	asm volatile ("vpbroadcastb %0,%%ymm15" : : "m" (gfconst16.low4[0]));

	for (i = 0; i < size; i += 32) {
		/* last disk without the generator multiplication */
		asm volatile ("vmovdqa %0,%%ymm10" : : "m" (v[l][i]));

		asm volatile ("vmovdqa %ymm10,%ymm0");
		asm volatile ("vmovdqa %ymm10,%ymm1");

		asm volatile ("vpsrlw  $4,%ymm10,%ymm11");
		asm volatile ("vpand   %ymm15,%ymm10,%ymm10");
		asm volatile ("vpand   %ymm15,%ymm11,%ymm11");
		if (np >= 3) {
			asm volatile ("vbroadcasti128 %0,%%ymm2" : : "m" (raid_gfcauchypshufb[l][1][0][0]));
			asm volatile ("vbroadcasti128 %0,%%ymm13" : : "m" (raid_gfcauchypshufb[l][1][1][0]));
			asm volatile ("vpshufb %ymm10,%ymm2,%ymm2");
			asm volatile ("vpshufb %ymm11,%ymm13,%ymm13");
			asm volatile ("vpxor   %ymm13,%ymm2,%ymm2");
		}
		if (np >= 4) {
			asm volatile ("vbroadcasti128 %0,%%ymm3" : : "m" (raid_gfcauchypshufb[l][2][0][0]));
			asm volatile ("vbroadcasti128 %0,%%ymm13" : : "m" (raid_gfcauchypshufb[l][2][1][0]));
			asm volatile ("vpshufb %ymm10,%ymm3,%ymm3");
			asm volatile ("vpshufb %ymm11,%ymm13,%ymm13");
			asm volatile ("vpxor   %ymm13,%ymm3,%ymm3");
		}
		if (np >= 5) {
			asm volatile ("vbroadcasti128 %0,%%ymm4" : : "m" (raid_gfcauchypshufb[l][3][0][0]));
			asm volatile ("vbroadcasti128 %0,%%ymm13" : : "m" (raid_gfcauchypshufb[l][3][1][0]));
			asm volatile ("vpshufb %ymm10,%ymm4,%ymm4");
			asm volatile ("vpshufb %ymm11,%ymm13,%ymm13");
			asm volatile ("vpxor   %ymm13,%ymm4,%ymm4");
		}
		if (np >= 6) {
			asm volatile ("vbroadcasti128 %0,%%ymm5" : : "m" (raid_gfcauchypshufb[l][4][0][0]));
			asm volatile ("vbroadcasti128 %0,%%ymm13" : : "m" (raid_gfcauchypshufb[l][4][1][0]));
			asm volatile ("vpshufb %ymm10,%ymm5,%ymm5");
			asm volatile ("vpshufb %ymm11,%ymm13,%ymm13");
			asm volatile ("vpxor   %ymm13,%ymm5,%ymm5");
		}

		/* intermediate disks */
		for (d = l - 1; d > 0; --d) {
			asm volatile ("vmovdqa %0,%%ymm10" : : "m" (v[d][i]));

			if (generator == 3)
				asm volatile ("vmovdqa %ymm1,%ymm12");
			asm volatile ("vpcmpgtb %ymm1,%ymm6,%ymm11");
			asm volatile ("vpaddb %ymm1,%ymm1,%ymm1");
			asm volatile ("vpand %ymm14,%ymm11,%ymm11");
			asm volatile ("vpxor %ymm11,%ymm1,%ymm1");
			if (generator == 3)
				asm volatile ("vpxor %ymm12,%ymm1,%ymm1");

			asm volatile ("vpxor %ymm10,%ymm0,%ymm0");
			asm volatile ("vpxor %ymm10,%ymm1,%ymm1");

			asm volatile ("vpsrlw  $4,%ymm10,%ymm11");
			asm volatile ("vpand   %ymm15,%ymm10,%ymm10");
			asm volatile ("vpand   %ymm15,%ymm11,%ymm11");
			if (np >= 3) {
				asm volatile ("vbroadcasti128 %0,%%ymm12" : : "m" (raid_gfcauchypshufb[d][1][0][0]));
				asm volatile ("vbroadcasti128 %0,%%ymm13" : : "m" (raid_gfcauchypshufb[d][1][1][0]));
				asm volatile ("vpshufb %ymm10,%ymm12,%ymm12");
				asm volatile ("vpshufb %ymm11,%ymm13,%ymm13");
				asm volatile ("vpxor   %ymm12,%ymm2,%ymm2");
				asm volatile ("vpxor   %ymm13,%ymm2,%ymm2");
			}
			if (np >= 4) {
				asm volatile ("vbroadcasti128 %0,%%ymm12" : : "m" (raid_gfcauchypshufb[d][2][0][0]));
				asm volatile ("vbroadcasti128 %0,%%ymm13" : : "m" (raid_gfcauchypshufb[d][2][1][0]));
				asm volatile ("vpshufb %ymm10,%ymm12,%ymm12");
				asm volatile ("vpshufb %ymm11,%ymm13,%ymm13");
				asm volatile ("vpxor   %ymm12,%ymm3,%ymm3");
				asm volatile ("vpxor   %ymm13,%ymm3,%ymm3");
			}
			if (np >= 5) {
				asm volatile ("vbroadcasti128 %0,%%ymm12" : : "m" (raid_gfcauchypshufb[d][3][0][0]));
				asm volatile ("vbroadcasti128 %0,%%ymm13" : : "m" (raid_gfcauchypshufb[d][3][1][0]));
				asm volatile ("vpshufb %ymm10,%ymm12,%ymm12");
				asm volatile ("vpshufb %ymm11,%ymm13,%ymm13");
				asm volatile ("vpxor   %ymm12,%ymm4,%ymm4");
				asm volatile ("vpxor   %ymm13,%ymm4,%ymm4");
			}
			if (np >= 6) {
				asm volatile ("vbroadcasti128 %0,%%ymm12" : : "m" (raid_gfcauchypshufb[d][4][0][0]));
				asm volatile ("vbroadcasti128 %0,%%ymm13" : : "m" (raid_gfcauchypshufb[d][4][1][0]));
				asm volatile ("vpshufb %ymm10,%ymm12,%ymm12");
				asm volatile ("vpshufb %ymm11,%ymm13,%ymm13");
				asm volatile ("vpxor   %ymm12,%ymm5,%ymm5");
				asm volatile ("vpxor   %ymm13,%ymm5,%ymm5");
			}
		}

		/* first disk with all coefficients at 1 */
		asm volatile ("vmovdqa %0,%%ymm10" : : "m" (v[0][i]));

		if (generator == 3)
			asm volatile ("vmovdqa %ymm1,%ymm12");
		asm volatile ("vpcmpgtb %ymm1,%ymm6,%ymm11");
		asm volatile ("vpaddb %ymm1,%ymm1,%ymm1");
		asm volatile ("vpand %ymm14,%ymm11,%ymm11");
		asm volatile ("vpxor %ymm11,%ymm1,%ymm1");
		if (generator == 3)
			asm volatile ("vpxor %ymm12,%ymm1,%ymm1");

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

/*
 * Recover one data failure using selected parity with AVX2.
 *
 * Process 64 bytes per iteration (two 32-byte lanes) using only 8 YMM registers (ymm0..ymm7),
 * sharing table broadcasts across both lanes.
 */
static __always_inline void raid_rec1_avx2_1(int *id, int *ip, int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *pa;
	uint8_t *src[RAID_DATA_MAX];
	const uint8_t *S[RAID_DATA_MAX];
	const uint8_t *R[2];
	uint8_t G, V;
	size_t i;
	int d, s, ns, k;

	/* setup the coefficient and invert it */
	G = A(ip[0], id[0]);
	V = inv(G);

	p = v[nd + ip[0]];
	pa = v[id[0]];

	/* build the compact surviving-data list and its syndrome tables */
	ns = 0;
	k = 0;
	for (d = 0; d < nd; ++d) {
		if (k < 1 && d == id[0]) {
			++k;
			continue;
		}
		src[ns] = v[d];
		S[ns] = &raid_gfmulpshufb[A(ip[0], d)][0][0];
		++ns;
	}
	BUG_ON(k != 1);
	BUG_ON(ns != nd - 1);

	R[0] = &raid_gfmulpshufb[V][0][0];
	R[1] = &raid_gfmulpshufb[V][1][0];

	raid_avx_begin();

	asm volatile ("vpbroadcastb %0,%%ymm7" : : "m" (gfconst16.low4[0]));

	for (i = 0; i < size; i += 64) {
		/* start syndrome with stored parity for both 32-byte lanes */
		asm volatile ("vmovdqa %0,%%ymm0" : : "m" (p[i]));
		asm volatile ("vmovdqa %0,%%ymm1" : : "m" (p[i + 32]));

		for (s = 0; s < ns; ++s) {
			const uint8_t *t = S[s];

			asm volatile ("vmovdqa %0,%%ymm2" : : "m" (src[s][i]));
			asm volatile ("vmovdqa %0,%%ymm3" : : "m" (src[s][i + 32]));

			asm volatile ("vpsrlw $4,%ymm2,%ymm4");
			asm volatile ("vpsrlw $4,%ymm3,%ymm5");
			asm volatile ("vpand %ymm7,%ymm2,%ymm2");
			asm volatile ("vpand %ymm7,%ymm3,%ymm3");
			asm volatile ("vpand %ymm7,%ymm4,%ymm4");
			asm volatile ("vpand %ymm7,%ymm5,%ymm5");

			asm volatile ("vbroadcasti128 %0,%%ymm6" : : "m" (t[0]));
			asm volatile ("vpshufb %ymm2,%ymm6,%ymm2");
			asm volatile ("vpshufb %ymm3,%ymm6,%ymm3");
			asm volatile ("vpxor %ymm2,%ymm0,%ymm0");
			asm volatile ("vpxor %ymm3,%ymm1,%ymm1");

			asm volatile ("vbroadcasti128 %0,%%ymm6" : : "m" (t[16]));
			asm volatile ("vpshufb %ymm4,%ymm6,%ymm4");
			asm volatile ("vpshufb %ymm5,%ymm6,%ymm5");
			asm volatile ("vpxor %ymm4,%ymm0,%ymm0");
			asm volatile ("vpxor %ymm5,%ymm1,%ymm1");
		}

		/* split completed syndrome in ymm0 and ymm1 */
		asm volatile ("vpsrlw $4,%ymm0,%ymm4");
		asm volatile ("vpsrlw $4,%ymm1,%ymm5");
		asm volatile ("vpand %ymm7,%ymm0,%ymm0");
		asm volatile ("vpand %ymm7,%ymm1,%ymm1");
		asm volatile ("vpand %ymm7,%ymm4,%ymm4");
		asm volatile ("vpand %ymm7,%ymm5,%ymm5");

		/* multiply by inverse matrix V (table R) */
		asm volatile ("vbroadcasti128 %0,%%ymm6" : : "m" (R[0][0]));
		asm volatile ("vpshufb %ymm0,%ymm6,%ymm0");
		asm volatile ("vpshufb %ymm1,%ymm6,%ymm1");

		asm volatile ("vbroadcasti128 %0,%%ymm6" : : "m" (R[1][0]));
		asm volatile ("vpshufb %ymm4,%ymm6,%ymm4");
		asm volatile ("vpshufb %ymm5,%ymm6,%ymm5");

		asm volatile ("vpxor %ymm4,%ymm0,%ymm0");
		asm volatile ("vpxor %ymm5,%ymm1,%ymm1");

		asm volatile ("vmovdqa %%ymm0,%0" : "=m" (pa[i]));
		asm volatile ("vmovdqa %%ymm1,%0" : "=m" (pa[i + 32]));
	}

	raid_avx_end();
}

/*
 * Recover failure of one data block using Q with AVX2.
 *
 * Computes Q of all surviving data directly with Horner's method and reconstructs the missing block from Qdelta.
 *
 * Processes two 32-byte lanes per iteration, avoiding raid_delta_gen(), temporary parity buffers, and the extra pass over the data.
 */
static __always_inline void raid_rec1_avx2_q(int *id, int *ip, int nd, size_t size, void **vv)
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

	raid_avx_begin();

	/* keep the inverse coefficient resident */
	asm volatile ("vbroadcasti128 %0,%%ymm2" : : "m" (raid_gfmulpshufb[V][0][0]));
	asm volatile ("vbroadcasti128 %0,%%ymm3" : : "m" (raid_gfmulpshufb[V][1][0]));

	/* keep zero and the active reduction polynomial resident */
	asm volatile ("vpxor %ymm6,%ymm6,%ymm6");
	asm volatile ("vpbroadcastb %0,%%ymm7" : : "m" (gfconst16.poly[0]));

	for (i = 0; i < size; i += 64) {
		/* last disk starts Horner without generator multiplication */
		if (l == id[0]) {
			asm volatile ("vpxor %ymm0,%ymm0,%ymm0");
			asm volatile ("vpxor %ymm1,%ymm1,%ymm1");
		} else {
			asm volatile ("vmovdqa %0,%%ymm0" : : "m" (v[l][i]));
			asm volatile ("vmovdqa %0,%%ymm1" : : "m" (v[l][i + 32]));
		}

		for (d = l - 1; d >= 0; --d) {
			/* multiply both Q lanes by the active generator */
			if (generator == 3) {
				asm volatile ("vmovdqa %ymm0,%ymm4");
				asm volatile ("vmovdqa %ymm1,%ymm5");
				asm volatile ("vpaddb %ymm0,%ymm0,%ymm0");
				asm volatile ("vpaddb %ymm1,%ymm1,%ymm1");
				asm volatile ("vpxor %ymm4,%ymm0,%ymm0");
				asm volatile ("vpxor %ymm5,%ymm1,%ymm1");
				asm volatile ("vpcmpgtb %ymm4,%ymm6,%ymm4");
				asm volatile ("vpcmpgtb %ymm5,%ymm6,%ymm5");
				asm volatile ("vpand %ymm7,%ymm4,%ymm4");
				asm volatile ("vpand %ymm7,%ymm5,%ymm5");
				asm volatile ("vpxor %ymm4,%ymm0,%ymm0");
				asm volatile ("vpxor %ymm5,%ymm1,%ymm1");
			} else {
				asm volatile ("vpcmpgtb %ymm0,%ymm6,%ymm4");
				asm volatile ("vpcmpgtb %ymm1,%ymm6,%ymm5");
				asm volatile ("vpaddb %ymm0,%ymm0,%ymm0");
				asm volatile ("vpaddb %ymm1,%ymm1,%ymm1");
				asm volatile ("vpand %ymm7,%ymm4,%ymm4");
				asm volatile ("vpand %ymm7,%ymm5,%ymm5");
				asm volatile ("vpxor %ymm4,%ymm0,%ymm0");
				asm volatile ("vpxor %ymm5,%ymm1,%ymm1");
			}

			/* missing disk contributes zero */
			if (d == id[0])
				continue;

			asm volatile ("vpxor %0,%%ymm0,%%ymm0" : : "m" (v[d][i]));
			asm volatile ("vpxor %0,%%ymm1,%%ymm1" : : "m" (v[d][i + 32]));
		}

		/* Qdelta = stored Q ^ Q of all surviving data */
		asm volatile ("vpxor %0,%%ymm0,%%ymm0" : : "m" (q[i]));
		asm volatile ("vpxor %0,%%ymm1,%%ymm1" : : "m" (q[i + 32]));

		/* xmm6 is no longer needed as zero, so reuse it as the low-nibble mask */
		asm volatile ("vpbroadcastb %0,%%ymm6" : : "m" (gfconst16.low4[0]));

		/* split both Qdelta lanes into low/high nibbles */
		asm volatile ("vmovdqa %ymm0,%ymm4");
		asm volatile ("vmovdqa %ymm1,%ymm5");
		asm volatile ("vpsrlw $4,%ymm0,%ymm0");
		asm volatile ("vpsrlw $4,%ymm1,%ymm1");
		asm volatile ("vpand %ymm6,%ymm4,%ymm4");
		asm volatile ("vpand %ymm6,%ymm5,%ymm5");
		asm volatile ("vpand %ymm6,%ymm0,%ymm0");
		asm volatile ("vpand %ymm6,%ymm1,%ymm1");

		/* multiply both Qdelta lanes by the inverse coefficient */
		asm volatile ("vpshufb %ymm4,%ymm2,%ymm4");
		asm volatile ("vpshufb %ymm5,%ymm2,%ymm5");
		asm volatile ("vpshufb %ymm0,%ymm3,%ymm0");
		asm volatile ("vpshufb %ymm1,%ymm3,%ymm1");
		asm volatile ("vpxor %ymm0,%ymm4,%ymm4");
		asm volatile ("vpxor %ymm1,%ymm5,%ymm5");

		/* recovery data must remain cacheable */
		asm volatile ("vmovdqa %%ymm4,%0" : "=m" (pa[i]));
		asm volatile ("vmovdqa %%ymm5,%0" : "=m" (pa[i + 32]));

		/* restore zero for the next Horner iteration */
		asm volatile ("vpxor %ymm6,%ymm6,%ymm6");
	}

	raid_avx_end();
}

/*
 * Recover multiple data failures using selected parity blocks with AVX2 optimized for up to four failures.
 *
 * Compute all selected syndromes in one scan of the surviving data, then
 * retain only their low/high nibbles while reconstructing the missing data.
 *
 * If P is available, keep the complete P delta syndrome in ymm6.
 * Reconstruct only nr - 1 missing blocks through the inverse matrix and
 * obtain the last block by XORing the reconstructed blocks out of Pdelta.
 */
static __always_inline void raid_recX_avx2_1234(int nr, int has_p, int *id, int *ip, int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p[RAID_PARITY_MAX];
	uint8_t *pa[RAID_PARITY_MAX];
	uint8_t *src[RAID_DATA_MAX];
	uint8_t G[RAID_PARITY_MAX * RAID_PARITY_MAX];
	uint8_t V[RAID_PARITY_MAX * RAID_PARITY_MAX];
	const uint8_t *S[RAID_DATA_MAX][RAID_PARITY_MAX];
	const uint8_t *R[RAID_PARITY_MAX][RAID_PARITY_MAX];
	uint8_t buffer_low[RAID_PARITY_MAX * 32 + 32];
	uint8_t buffer_high[RAID_PARITY_MAX * 32 + 32];
	uint8_t *pd_low = __align_ptr(buffer_low, 32);
	uint8_t *pd_high = __align_ptr(buffer_high, 32);
	size_t i;
	int d, j, k, s;
	int ns;

	BUG_ON(nr < 1 || nr > 4);

	/* setup the coefficients matrix */
	for (j = 0; j < nr; ++j)
		for (k = 0; k < nr; ++k)
			G[j * nr + k] = A(ip[j], id[k]);

	/* invert it to solve the system of linear equations */
	raid_invert(G, V, nr);

	/* setup selected parity and destination pointers */
	for (j = 0; j < nr; ++j) {
		p[j] = v[nd + ip[j]];
		pa[j] = v[id[j]];
	}

	/* build the compact surviving-data list and its syndrome tables */
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

	/* precompute inverse-matrix multiplication table pointers */
	for (j = 0; j < nr - has_p; ++j)
		for (k = 0; k < nr; ++k)
			R[j][k] = &raid_gfmulpshufb[V[j * nr + k]][0][0];

	raid_avx_begin();

	asm volatile ("vpbroadcastb %0,%%ymm7" : : "m" (gfconst16.low4[0]));

	for (i = 0; i < size; i += 32) {
		/* ymm0..ymm3 are syndrome accumulators */
		asm volatile ("vmovdqa %0,%%ymm0" : : "m" (p[0][i]));
		if (nr >= 2)
			asm volatile ("vmovdqa %0,%%ymm1" : : "m" (p[1][i]));
		if (nr >= 3)
			asm volatile ("vmovdqa %0,%%ymm2" : : "m" (p[2][i]));
		if (nr >= 4)
			asm volatile ("vmovdqa %0,%%ymm3" : : "m" (p[3][i]));

		/* all selected syndromes are accumulated in this single source scan */
		for (s = 0; s < ns; ++s) {
			const uint8_t **t = S[s];

			asm volatile ("vmovdqa %0,%%ymm4" : : "m" (src[s][i]));
			if (has_p) {
				asm volatile ("vpxor %ymm4,%ymm0,%ymm0");

				asm volatile ("vpsrlw $4,%ymm4,%ymm5");
				asm volatile ("vpand %ymm7,%ymm4,%ymm4");
				asm volatile ("vpand %ymm7,%ymm5,%ymm5");
			} else {
				asm volatile ("vpsrlw $4,%ymm4,%ymm5");
				asm volatile ("vpand %ymm7,%ymm4,%ymm4");
				asm volatile ("vpand %ymm7,%ymm5,%ymm5");

				asm volatile ("vbroadcasti128 %0,%%ymm6" : : "m" (t[0][0]));
				asm volatile ("vpshufb %ymm4,%ymm6,%ymm6");
				asm volatile ("vpxor %ymm6,%ymm0,%ymm0");
				asm volatile ("vbroadcasti128 %0,%%ymm6" : : "m" (t[0][16]));
				asm volatile ("vpshufb %ymm5,%ymm6,%ymm6");
				asm volatile ("vpxor %ymm6,%ymm0,%ymm0");
			}

			if (nr >= 2) {
				asm volatile ("vbroadcasti128 %0,%%ymm6" : : "m" (t[1][0]));
				asm volatile ("vpshufb %ymm4,%ymm6,%ymm6");
				asm volatile ("vpxor %ymm6,%ymm1,%ymm1");
				asm volatile ("vbroadcasti128 %0,%%ymm6" : : "m" (t[1][16]));
				asm volatile ("vpshufb %ymm5,%ymm6,%ymm6");
				asm volatile ("vpxor %ymm6,%ymm1,%ymm1");
			}

			if (nr >= 3) {
				asm volatile ("vbroadcasti128 %0,%%ymm6" : : "m" (t[2][0]));
				asm volatile ("vpshufb %ymm4,%ymm6,%ymm6");
				asm volatile ("vpxor %ymm6,%ymm2,%ymm2");
				asm volatile ("vbroadcasti128 %0,%%ymm6" : : "m" (t[2][16]));
				asm volatile ("vpshufb %ymm5,%ymm6,%ymm6");
				asm volatile ("vpxor %ymm6,%ymm2,%ymm2");
			}

			if (nr >= 4) {
				asm volatile ("vbroadcasti128 %0,%%ymm6" : : "m" (t[3][0]));
				asm volatile ("vpshufb %ymm4,%ymm6,%ymm6");
				asm volatile ("vpxor %ymm6,%ymm3,%ymm3");
				asm volatile ("vbroadcasti128 %0,%%ymm6" : : "m" (t[3][16]));
				asm volatile ("vpshufb %ymm5,%ymm6,%ymm6");
				asm volatile ("vpxor %ymm6,%ymm3,%ymm3");
			}
		}

		/* preserve the complete P delta before splitting syndrome 0 */
		if (has_p)
			asm volatile ("vmovdqa %ymm0,%ymm6");

		/* preserve only aligned low/high nibbles */
		asm volatile ("vpsrlw $4,%ymm0,%ymm4");
		asm volatile ("vpand %ymm7,%ymm0,%ymm0");
		asm volatile ("vpand %ymm7,%ymm4,%ymm4");
		asm volatile ("vmovdqa %%ymm0,%0" : "=m" (pd_low[0]));
		asm volatile ("vmovdqa %%ymm4,%0" : "=m" (pd_high[0]));

		if (nr >= 2) {
			asm volatile ("vpsrlw $4,%ymm1,%ymm4");
			asm volatile ("vpand %ymm7,%ymm1,%ymm1");
			asm volatile ("vpand %ymm7,%ymm4,%ymm4");
			asm volatile ("vmovdqa %%ymm1,%0" : "=m" (pd_low[32]));
			asm volatile ("vmovdqa %%ymm4,%0" : "=m" (pd_high[32]));
		}

		if (nr >= 3) {
			asm volatile ("vpsrlw $4,%ymm2,%ymm4");
			asm volatile ("vpand %ymm7,%ymm2,%ymm2");
			asm volatile ("vpand %ymm7,%ymm4,%ymm4");
			asm volatile ("vmovdqa %%ymm2,%0" : "=m" (pd_low[64]));
			asm volatile ("vmovdqa %%ymm4,%0" : "=m" (pd_high[64]));
		}

		if (nr >= 4) {
			asm volatile ("vpsrlw $4,%ymm3,%ymm4");
			asm volatile ("vpand %ymm7,%ymm3,%ymm3");
			asm volatile ("vpand %ymm7,%ymm4,%ymm4");
			asm volatile ("vmovdqa %%ymm3,%0" : "=m" (pd_low[96]));
			asm volatile ("vmovdqa %%ymm4,%0" : "=m" (pd_high[96]));
		}

		/* reconstruct all but the last missing block when P is available */
		for (j = 0; j < nr - has_p; ++j) {
			const uint8_t **t = R[j];

			asm volatile ("vbroadcasti128 %0,%%ymm0" : : "m" (t[0][0]));
			asm volatile ("vbroadcasti128 %0,%%ymm1" : : "m" (t[0][16]));
			asm volatile ("vmovdqa %0,%%ymm4" : : "m" (pd_low[0]));
			asm volatile ("vmovdqa %0,%%ymm5" : : "m" (pd_high[0]));
			asm volatile ("vpshufb %ymm4,%ymm0,%ymm0");
			asm volatile ("vpshufb %ymm5,%ymm1,%ymm1");

			for (k = 1; k < nr; ++k) {
				asm volatile ("vbroadcasti128 %0,%%ymm2" : : "m" (t[k][0]));
				asm volatile ("vbroadcasti128 %0,%%ymm3" : : "m" (t[k][16]));
				asm volatile ("vmovdqa %0,%%ymm4" : : "m" (pd_low[k * 32]));
				asm volatile ("vmovdqa %0,%%ymm5" : : "m" (pd_high[k * 32]));
				asm volatile ("vpshufb %ymm4,%ymm2,%ymm2");
				asm volatile ("vpshufb %ymm5,%ymm3,%ymm3");
				asm volatile ("vpxor %ymm2,%ymm0,%ymm0");
				asm volatile ("vpxor %ymm3,%ymm1,%ymm1");
			}

			asm volatile ("vpxor %ymm1,%ymm0,%ymm0");

			if (has_p)
				asm volatile ("vpxor %ymm0,%ymm6,%ymm6");

			asm volatile ("vmovdqa %%ymm0,%0" : "=m" (pa[j][i]));
		}

		if (has_p)
			asm volatile ("vmovdqa %%ymm6,%0" : "=m" (pa[nr - 1][i]));
	}

	raid_avx_end();
}

/*
 * Recover multiple data failures using selected parity blocks with AVX2 and all syndromes in memory.
 *
 * Compute all selected syndromes in a single pass over the surviving data.
 * Syndrome accumulators are kept in memory to minimize register pressure.
 *
 * If P is available, keep the complete P delta syndrome in ymm6.
 * Reconstruct only nr - 1 missing blocks through the inverse matrix and
 * obtain the last block by XORing the reconstructed blocks out of Pdelta.
 */
static __always_inline void raid_recX_avx2(int nr, int has_p, int *id, int *ip, int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p[RAID_PARITY_MAX];
	uint8_t *pa[RAID_PARITY_MAX];
	uint8_t *src[RAID_DATA_MAX];
	uint8_t G[RAID_PARITY_MAX * RAID_PARITY_MAX];
	uint8_t V[RAID_PARITY_MAX * RAID_PARITY_MAX];
	const uint8_t *S[RAID_DATA_MAX][RAID_PARITY_MAX];
	const uint8_t *R[RAID_PARITY_MAX][RAID_PARITY_MAX];
	uint8_t buffer_low[RAID_PARITY_MAX * 32 + 32];
	uint8_t buffer_high[RAID_PARITY_MAX * 32 + 32];
	uint8_t *pd_low = __align_ptr(buffer_low, 32);
	uint8_t *pd_high = __align_ptr(buffer_high, 32);
	size_t i;
	int d, j, k, s;
	int ns;

	BUG_ON(nr < 1 || nr > RAID_PARITY_MAX);

	/* setup the coefficients matrix */
	for (j = 0; j < nr; ++j)
		for (k = 0; k < nr; ++k)
			G[j * nr + k] = A(ip[j], id[k]);

	/* invert it to solve the system of linear equations */
	raid_invert(G, V, nr);

	/* setup selected parity and destination pointers */
	for (j = 0; j < nr; ++j) {
		p[j] = v[nd + ip[j]];
		pa[j] = v[id[j]];
	}

	/* build the compact surviving-data list and its syndrome tables */
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

	/* precompute inverse-matrix multiplication table pointers */
	for (j = 0; j < nr - has_p; ++j)
		for (k = 0; k < nr; ++k)
			R[j][k] = &raid_gfmulpshufb[V[j * nr + k]][0][0];

	raid_avx_begin();

	asm volatile ("vpbroadcastb %0,%%ymm7" : : "m" (gfconst16.low4[0]));

	for (i = 0; i < size; i += 32) {
		/*
		 * Initialize all syndrome accumulators in memory from the
		 * selected stored parity blocks.
		 *
		 * pd_low[] temporarily contains the complete raw syndromes.
		 */
		for (j = 0; j < nr; ++j) {
			asm volatile ("vmovdqa %0,%%ymm0" : : "m" (p[j][i]));
			asm volatile ("vmovdqa %%ymm0,%0" : "=m" (pd_low[j * 32]));
		}

		/*
		 * Single pass over all surviving data.
		 *
		 * Every src[s][i] is loaded exactly once.
		 */
		for (s = 0; s < ns; ++s) {
			const uint8_t **t = S[s];

			/* original source */
			asm volatile ("vmovdqa %0,%%ymm0" : : "m" (src[s][i]));

			/*
			 * P has coefficient 1.
			 *
			 * Update its memory accumulator before destructively
			 * splitting the source into low/high nibbles.
			 */
			if (has_p) {
				asm volatile ("vpxor %0,%%ymm0,%%ymm4" : : "m" (pd_low[0]));
				asm volatile ("vmovdqa %%ymm4,%0" : "=m" (pd_low[0]));
			}

			/* split source into low/high nibbles */
			asm volatile ("vpsrlw $4,%ymm0,%ymm1");
			asm volatile ("vpand %ymm7,%ymm0,%ymm0");
			asm volatile ("vpand %ymm7,%ymm1,%ymm1");

			/*
			 * Update every non-P syndrome in memory.
			 *
			 * ymm0 = source low
			 * ymm1 = source high
			 * ymm2 = low table / low result
			 * ymm3 = high table / high result
			 */
			for (j = has_p; j < nr; ++j) {
				asm volatile ("vbroadcasti128 %0,%%ymm2" : : "m" (t[j][0]));
				asm volatile ("vbroadcasti128 %0,%%ymm3" : : "m" (t[j][16]));

				asm volatile ("vpshufb %ymm0,%ymm2,%ymm2");
				asm volatile ("vpshufb %ymm1,%ymm3,%ymm3");

				asm volatile ("vpxor %ymm3,%ymm2,%ymm2");

				/*
				 * Syndrome[j] ^= coefficient * source
				 *
				 * Read the accumulator directly as the memory
				 * operand of VPXOR, then write it back.
				 */
				asm volatile ("vpxor %0,%%ymm2,%%ymm2" : : "m" (pd_low[j * 32]));
				asm volatile ("vmovdqa %%ymm2,%0" : "=m" (pd_low[j * 32]));
			}
		}

		/*
		 * Preserve the complete P delta before pd_low[0] is
		 * destructively converted to low/high nibble form.
		 */
		if (has_p)
			asm volatile ("vmovdqa %0,%%ymm6" : : "m" (pd_low[0]));

		/*
		 * All survivor reads for this chunk are now complete.
		 *
		 * Convert each raw syndrome once to low/high nibble form.
		 * pd_low[] is overwritten in place.
		 */
		for (k = 0; k < nr; ++k) {
			asm volatile ("vmovdqa %0,%%ymm0" : : "m" (pd_low[k * 32]));
			asm volatile ("vpsrlw $4,%ymm0,%ymm1");
			asm volatile ("vpand %ymm7,%ymm0,%ymm0");
			asm volatile ("vpand %ymm7,%ymm1,%ymm1");
			asm volatile ("vmovdqa %%ymm0,%0" : "=m" (pd_low[k * 32]));
			asm volatile ("vmovdqa %%ymm1,%0" : "=m" (pd_high[k * 32]));
		}

		/*
		 * Reconstruct all but the last missing block when P is
		 * available.
		 *
		 * Keep independent low/high accumulation chains:
		 *
		 * ymm0 = low accumulator
		 * ymm1 = high accumulator
		 * ymm2 = low table/result
		 * ymm3 = high table/result
		 * ymm4 = syndrome low
		 * ymm5 = syndrome high
		 * ymm6 = remaining P delta
		 */
		for (j = 0; j < nr - has_p; ++j) {
			const uint8_t **t = R[j];

			/* coefficient 0 initializes the accumulators */
			asm volatile ("vbroadcasti128 %0,%%ymm0" : : "m" (t[0][0]));
			asm volatile ("vbroadcasti128 %0,%%ymm1" : : "m" (t[0][16]));

			asm volatile ("vmovdqa %0,%%ymm4" : : "m" (pd_low[0]));
			asm volatile ("vmovdqa %0,%%ymm5" : : "m" (pd_high[0]));

			asm volatile ("vpshufb %ymm4,%ymm0,%ymm0");
			asm volatile ("vpshufb %ymm5,%ymm1,%ymm1");

			for (k = 1; k < nr; ++k) {
				asm volatile ("vbroadcasti128 %0,%%ymm2" : : "m" (t[k][0]));
				asm volatile ("vbroadcasti128 %0,%%ymm3" : : "m" (t[k][16]));

				asm volatile ("vmovdqa %0,%%ymm4" : : "m" (pd_low[k * 32]));
				asm volatile ("vmovdqa %0,%%ymm5" : : "m" (pd_high[k * 32]));

				asm volatile ("vpshufb %ymm4,%ymm2,%ymm2");
				asm volatile ("vpshufb %ymm5,%ymm3,%ymm3");

				asm volatile ("vpxor %ymm2,%ymm0,%ymm0");
				asm volatile ("vpxor %ymm3,%ymm1,%ymm1");
			}

			asm volatile ("vpxor %ymm1,%ymm0,%ymm0");

			if (has_p)
				asm volatile ("vpxor %ymm0,%ymm6,%ymm6");

			asm volatile ("vmovdqa %%ymm0,%0" : "=m" (pa[j][i]));
		}

		if (has_p)
			asm volatile ("vmovdqa %%ymm6,%0" : "=m" (pa[nr - 1][i]));
	}

	raid_avx_end();
}

#ifdef CONFIG_X86_64
/*
 * Recover failure of one data block using Q with AVX2 extended.
 *
 * Computes Q of all surviving data directly with Horner's method and reconstructs the missing block from Qdelta.
 *
 * Processes two 32-byte lanes and two data disks per Horner iteration, avoiding raid_delta_gen(), temporary parity buffers, and the extra pass over the data.
 *
 * Note that it uses 16 registers, meaning that x64 is required.
 */
static __always_inline void raid_rec1_avx2ext_q(int *id, int *ip, int nd, size_t size, void **vv)
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

	raid_avx_begin();

	/* keep the inverse coefficient, zero, and active reduction polynomial resident */
	asm volatile ("vbroadcasti128 %0,%%ymm12" : : "m" (raid_gfmulpshufb[V][0][0]));
	asm volatile ("vbroadcasti128 %0,%%ymm13" : : "m" (raid_gfmulpshufb[V][1][0]));
	asm volatile ("vpxor %ymm14,%ymm14,%ymm14");
	asm volatile ("vpbroadcastb %0,%%ymm15" : : "m" (gfconst16.poly[0]));

	for (i = 0; i < size; i += 64) {
		/* last disk starts Horner without generator multiplication */
		if (l == id[0]) {
			asm volatile ("vpxor %ymm0,%ymm0,%ymm0");
			asm volatile ("vpxor %ymm1,%ymm1,%ymm1");
		} else {
			asm volatile ("vmovdqa %0,%%ymm0" : : "m" (v[l][i]));
			asm volatile ("vmovdqa %0,%%ymm1" : : "m" (v[l][i + 32]));
		}

		/* process two original disk positions per iteration */
		for (d = l - 1; d >= 1; d -= 2) {
			/* load D[d], substituting zero if this is the missing disk */
			if (d == id[0]) {
				asm volatile ("vmovdqa %ymm14,%ymm2");
				asm volatile ("vmovdqa %ymm14,%ymm3");
			} else {
				asm volatile ("vmovdqa %0,%%ymm2" : : "m" (v[d][i]));
				asm volatile ("vmovdqa %0,%%ymm3" : : "m" (v[d][i + 32]));
			}

			/* load D[d-1], substituting zero if this is the missing disk */
			if (d - 1 == id[0]) {
				asm volatile ("vmovdqa %ymm14,%ymm4");
				asm volatile ("vmovdqa %ymm14,%ymm5");
			} else {
				asm volatile ("vmovdqa %0,%%ymm4" : : "m" (v[d - 1][i]));
				asm volatile ("vmovdqa %0,%%ymm5" : : "m" (v[d - 1][i + 32]));
			}

			/*
			 * First lane.
			 *
			 * For generator 2:
			 *     Q = 2 * (2 * Q ^ D[d]) ^ D[d-1]
			 *
			 * For generator 3 the algebra is rearranged to reduce the dependency chain, matching the extended GEN2 approach.
			 */
			asm volatile ("vpcmpgtb %ymm0,%ymm14,%ymm8");
			asm volatile ("vpaddb %ymm0,%ymm0,%ymm6");
			asm volatile ("vpxor %ymm4,%ymm2,%ymm10");
			asm volatile ("vpand %ymm15,%ymm8,%ymm8");
			asm volatile ("vpxor %ymm8,%ymm6,%ymm6");
			asm volatile ("vpxor %ymm2,%ymm6,%ymm6");
			asm volatile ("vpcmpgtb %ymm6,%ymm14,%ymm8");
			asm volatile ("vpaddb %ymm6,%ymm6,%ymm6");
			if (generator == 3)
				asm volatile ("vpxor %ymm0,%ymm10,%ymm10");
			asm volatile ("vpand %ymm15,%ymm8,%ymm8");
			asm volatile ("vpxor %ymm8,%ymm6,%ymm6");
			if (generator == 3)
				asm volatile ("vpxor %ymm10,%ymm6,%ymm0");
			else
				asm volatile ("vpxor %ymm4,%ymm6,%ymm0");

			/* second lane */
			asm volatile ("vpcmpgtb %ymm1,%ymm14,%ymm9");
			asm volatile ("vpaddb %ymm1,%ymm1,%ymm7");
			asm volatile ("vpxor %ymm5,%ymm3,%ymm11");
			asm volatile ("vpand %ymm15,%ymm9,%ymm9");
			asm volatile ("vpxor %ymm9,%ymm7,%ymm7");
			asm volatile ("vpxor %ymm3,%ymm7,%ymm7");
			asm volatile ("vpcmpgtb %ymm7,%ymm14,%ymm9");
			asm volatile ("vpaddb %ymm7,%ymm7,%ymm7");
			if (generator == 3)
				asm volatile ("vpxor %ymm1,%ymm11,%ymm11");
			asm volatile ("vpand %ymm15,%ymm9,%ymm9");
			asm volatile ("vpxor %ymm9,%ymm7,%ymm7");
			if (generator == 3)
				asm volatile ("vpxor %ymm11,%ymm7,%ymm1");
			else
				asm volatile ("vpxor %ymm5,%ymm7,%ymm1");
		}

		/* one original disk position remains */
		if (d == 0) {
			if (generator == 3) {
				asm volatile ("vmovdqa %ymm0,%ymm10");
				asm volatile ("vmovdqa %ymm1,%ymm11");
				asm volatile ("vpcmpgtb %ymm0,%ymm14,%ymm8");
				asm volatile ("vpcmpgtb %ymm1,%ymm14,%ymm9");
				asm volatile ("vpaddb %ymm0,%ymm0,%ymm0");
				asm volatile ("vpaddb %ymm1,%ymm1,%ymm1");
				asm volatile ("vpand %ymm15,%ymm8,%ymm8");
				asm volatile ("vpand %ymm15,%ymm9,%ymm9");
				asm volatile ("vpxor %ymm8,%ymm0,%ymm0");
				asm volatile ("vpxor %ymm9,%ymm1,%ymm1");
				asm volatile ("vpxor %ymm10,%ymm0,%ymm0");
				asm volatile ("vpxor %ymm11,%ymm1,%ymm1");
			} else {
				asm volatile ("vpcmpgtb %ymm0,%ymm14,%ymm8");
				asm volatile ("vpcmpgtb %ymm1,%ymm14,%ymm9");
				asm volatile ("vpaddb %ymm0,%ymm0,%ymm0");
				asm volatile ("vpaddb %ymm1,%ymm1,%ymm1");
				asm volatile ("vpand %ymm15,%ymm8,%ymm8");
				asm volatile ("vpand %ymm15,%ymm9,%ymm9");
				asm volatile ("vpxor %ymm8,%ymm0,%ymm0");
				asm volatile ("vpxor %ymm9,%ymm1,%ymm1");
			}

			if (id[0] != 0) {
				asm volatile ("vpxor %0,%%ymm0,%%ymm0" : : "m" (v[0][i]));
				asm volatile ("vpxor %0,%%ymm1,%%ymm1" : : "m" (v[0][i + 32]));
			}
		}

		/* Qdelta = stored Q ^ Q of all surviving data */
		asm volatile ("vpxor %0,%%ymm0,%%ymm0" : : "m" (q[i]));
		asm volatile ("vpxor %0,%%ymm1,%%ymm1" : : "m" (q[i + 32]));

		/* low-nibble mask */
		asm volatile ("vpbroadcastb %0,%%ymm10" : : "m" (gfconst16.low4[0]));

		/* split both Qdelta lanes into low/high nibbles */
		asm volatile ("vmovdqa %ymm0,%ymm2");
		asm volatile ("vmovdqa %ymm1,%ymm3");
		asm volatile ("vpsrlw $4,%ymm0,%ymm0");
		asm volatile ("vpsrlw $4,%ymm1,%ymm1");
		asm volatile ("vpand %ymm10,%ymm2,%ymm2");
		asm volatile ("vpand %ymm10,%ymm3,%ymm3");
		asm volatile ("vpand %ymm10,%ymm0,%ymm0");
		asm volatile ("vpand %ymm10,%ymm1,%ymm1");

		/* low-nibble products */
		asm volatile ("vpshufb %ymm2,%ymm12,%ymm6");
		asm volatile ("vpshufb %ymm3,%ymm12,%ymm7");

		/* high-nibble products */
		asm volatile ("vpshufb %ymm0,%ymm13,%ymm8");
		asm volatile ("vpshufb %ymm1,%ymm13,%ymm9");
		asm volatile ("vpxor %ymm8,%ymm6,%ymm6");
		asm volatile ("vpxor %ymm9,%ymm7,%ymm7");

		/* recovery data must remain cacheable */
		asm volatile ("vmovdqa %%ymm6,%0" : "=m" (pa[i]));
		asm volatile ("vmovdqa %%ymm7,%0" : "=m" (pa[i + 32]));
	}

	raid_avx_end();
}

/*
 * Recover multiple data failures using selected parity blocks with AVX2 extended optimized for up to two failures.
 *
 * Process 64 bytes at a time as two independent 32-byte lanes.
 * Multiplication tables are broadcast once and shared between both lanes.
 *
 * If P is available, keep the complete P delta syndrome in ymm6.
 * Reconstruct only nr - 1 missing blocks through the inverse matrix and
 * obtain the last block by XORing the reconstructed blocks out of Pdelta.
 *
 * Note that it uses 16 registers, meaning that x64 is required.
 */
static __always_inline void raid_recX_avx2ext_12(int nr, int has_p, int *id, int *ip, int nd, size_t size, void **vv)
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

	BUG_ON(nr < 1 || nr > 2);

	/* setup the coefficients matrix */
	for (j = 0; j < nr; ++j)
		for (k = 0; k < nr; ++k)
			G[j * nr + k] = A(ip[j], id[k]);

	/* invert it to solve the system of linear equations */
	raid_invert(G, V, nr);

	/* setup selected parity and destination pointers */
	for (j = 0; j < nr; ++j) {
		p[j] = v[nd + ip[j]];
		pa[j] = v[id[j]];
	}

	/* build the compact surviving-data list and its syndrome tables */
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

	/* precompute inverse-matrix multiplication table pointers for required outputs */
	for (j = 0; j < nr - has_p; ++j)
		for (k = 0; k < nr; ++k)
			R[j][k] = &raid_gfmulpshufb[V[j * nr + k]][0][0];

	raid_avx_begin();

	for (i = 0; i < size; i += 64) {
		asm volatile ("vpbroadcastb %0,%%ymm15" : : "m" (gfconst16.low4[0]));

		/*
		 * Start each syndrome with the corresponding stored parity.
		 *
		 * During syndrome computation:
		 *
		 *   ymm0/ymm1   syndrome 0, first/second 32 bytes
		 *   ymm2/ymm3   syndrome 1, first/second 32 bytes
		 *
		 *   ymm6/ymm7   source low, first/second 32 bytes
		 *   ymm8/ymm9   source high, first/second 32 bytes
		 *   ymm12/ymm13 multiplication table / temporary
		 *   ymm15       low-nibble mask
		 */
		asm volatile ("vmovdqa %0,%%ymm0" : : "m" (p[0][i]));
		asm volatile ("vmovdqa %0,%%ymm1" : : "m" (p[0][i + 32]));

		if (nr >= 2) {
			asm volatile ("vmovdqa %0,%%ymm2" : : "m" (p[1][i]));
			asm volatile ("vmovdqa %0,%%ymm3" : : "m" (p[1][i + 32]));
		}

		/* add all surviving data contributions */
		for (s = 0; s < ns; ++s) {
			const uint8_t **t = S[s];

			asm volatile ("vmovdqa %0,%%ymm6" : : "m" (src[s][i]));
			asm volatile ("vmovdqa %0,%%ymm7" : : "m" (src[s][i + 32]));

			/*
			 * P has coefficient 1 for every data disk.
			 * Do the XOR before destroying the original source
			 * while splitting it into low/high nibbles.
			 */
			if (has_p) {
				asm volatile ("vpxor %ymm6,%ymm0,%ymm0");
				asm volatile ("vpxor %ymm7,%ymm1,%ymm1");

				/* split both 32-byte source lanes */
				asm volatile ("vpsrlw $4,%ymm6,%ymm8");
				asm volatile ("vpsrlw $4,%ymm7,%ymm9");
				asm volatile ("vpand %ymm15,%ymm6,%ymm6");
				asm volatile ("vpand %ymm15,%ymm7,%ymm7");
				asm volatile ("vpand %ymm15,%ymm8,%ymm8");
				asm volatile ("vpand %ymm15,%ymm9,%ymm9");
			} else {
				/* split both 32-byte source lanes */
				asm volatile ("vpsrlw $4,%ymm6,%ymm8");
				asm volatile ("vpsrlw $4,%ymm7,%ymm9");
				asm volatile ("vpand %ymm15,%ymm6,%ymm6");
				asm volatile ("vpand %ymm15,%ymm7,%ymm7");
				asm volatile ("vpand %ymm15,%ymm8,%ymm8");
				asm volatile ("vpand %ymm15,%ymm9,%ymm9");

				/*
				 * Syndrome 0.
				 *
				 * Share each table broadcast between the two
				 * 32-byte lanes.
				 */
				asm volatile ("vbroadcasti128 %0,%%ymm12" : : "m" (t[0][0]));
				asm volatile ("vmovdqa %ymm12,%ymm13");
				asm volatile ("vpshufb %ymm6,%ymm12,%ymm12");
				asm volatile ("vpshufb %ymm7,%ymm13,%ymm13");
				asm volatile ("vpxor %ymm12,%ymm0,%ymm0");
				asm volatile ("vpxor %ymm13,%ymm1,%ymm1");

				asm volatile ("vbroadcasti128 %0,%%ymm12" : : "m" (t[0][16]));
				asm volatile ("vmovdqa %ymm12,%ymm13");
				asm volatile ("vpshufb %ymm8,%ymm12,%ymm12");
				asm volatile ("vpshufb %ymm9,%ymm13,%ymm13");
				asm volatile ("vpxor %ymm12,%ymm0,%ymm0");
				asm volatile ("vpxor %ymm13,%ymm1,%ymm1");
			}

			/* syndrome 1 */
			if (nr >= 2) {
				asm volatile ("vbroadcasti128 %0,%%ymm12" : : "m" (t[1][0]));
				asm volatile ("vmovdqa %ymm12,%ymm13");
				asm volatile ("vpshufb %ymm6,%ymm12,%ymm12");
				asm volatile ("vpshufb %ymm7,%ymm13,%ymm13");
				asm volatile ("vpxor %ymm12,%ymm2,%ymm2");
				asm volatile ("vpxor %ymm13,%ymm3,%ymm3");

				asm volatile ("vbroadcasti128 %0,%%ymm12" : : "m" (t[1][16]));
				asm volatile ("vmovdqa %ymm12,%ymm13");
				asm volatile ("vpshufb %ymm8,%ymm12,%ymm12");
				asm volatile ("vpshufb %ymm9,%ymm13,%ymm13");
				asm volatile ("vpxor %ymm12,%ymm2,%ymm2");
				asm volatile ("vpxor %ymm13,%ymm3,%ymm3");
			}
		}

		/* preserve raw P delta in ymm4/ymm5 before splitting syndrome 0 */
		if (has_p) {
			asm volatile ("vmovdqa %ymm0,%ymm4");
			asm volatile ("vmovdqa %ymm1,%ymm5");
		}

		/*
		 * Split all completed syndromes.
		 *
		 * After this:
		 *
		 *   syndrome 0:
		 *     ymm0 / ymm6   first  32 bytes low/high
		 *     ymm1 / ymm7   second 32 bytes low/high
		 *
		 *   syndrome 1:
		 *     ymm2 / ymm8   first  32 bytes low/high
		 *     ymm3 / ymm9   second 32 bytes low/high
		 */
		asm volatile ("vpsrlw $4,%ymm0,%ymm6");
		asm volatile ("vpsrlw $4,%ymm1,%ymm7");
		asm volatile ("vpand %ymm15,%ymm0,%ymm0");
		asm volatile ("vpand %ymm15,%ymm1,%ymm1");
		asm volatile ("vpand %ymm15,%ymm6,%ymm6");
		asm volatile ("vpand %ymm15,%ymm7,%ymm7");

		if (nr >= 2) {
			asm volatile ("vpsrlw $4,%ymm2,%ymm8");
			asm volatile ("vpsrlw $4,%ymm3,%ymm9");
			asm volatile ("vpand %ymm15,%ymm2,%ymm2");
			asm volatile ("vpand %ymm15,%ymm3,%ymm3");
			asm volatile ("vpand %ymm15,%ymm8,%ymm8");
			asm volatile ("vpand %ymm15,%ymm9,%ymm9");
		}

		/*
		 * Reconstruct missing data blocks through the inverse matrix.
		 * If P is available, reconstruct only nr - 1 blocks.
		 *
		 * ymm12/ymm13 are a shared multiplication table pair.
		 * ymm14/ymm15 accumulate the two 32-byte output lanes.
		 *
		 * At this point ymm15 is no longer needed as the nibble mask,
		 * so all 16 YMM registers are available.
		 */
		for (j = 0; j < nr - has_p; ++j) {
			const uint8_t **t = R[j];

			/*
			 * First coefficient.
			 * Initialize both output accumulators from the low
			 * nibble products.
			 */
			asm volatile ("vbroadcasti128 %0,%%ymm12" : : "m" (t[0][0]));
			asm volatile ("vmovdqa %ymm12,%ymm13");
			asm volatile ("vpshufb %ymm0,%ymm12,%ymm12");
			asm volatile ("vpshufb %ymm1,%ymm13,%ymm13");
			asm volatile ("vmovdqa %ymm12,%ymm14");
			asm volatile ("vmovdqa %ymm13,%ymm15");

			asm volatile ("vbroadcasti128 %0,%%ymm12" : : "m" (t[0][16]));
			asm volatile ("vmovdqa %ymm12,%ymm13");
			asm volatile ("vpshufb %ymm6,%ymm12,%ymm12");
			asm volatile ("vpshufb %ymm7,%ymm13,%ymm13");
			asm volatile ("vpxor %ymm12,%ymm14,%ymm14");
			asm volatile ("vpxor %ymm13,%ymm15,%ymm15");

			/* second coefficient */
			if (nr >= 2) {
				asm volatile ("vbroadcasti128 %0,%%ymm12" : : "m" (t[1][0]));
				asm volatile ("vmovdqa %ymm12,%ymm13");
				asm volatile ("vpshufb %ymm2,%ymm12,%ymm12");
				asm volatile ("vpshufb %ymm3,%ymm13,%ymm13");
				asm volatile ("vpxor %ymm12,%ymm14,%ymm14");
				asm volatile ("vpxor %ymm13,%ymm15,%ymm15");

				asm volatile ("vbroadcasti128 %0,%%ymm12" : : "m" (t[1][16]));
				asm volatile ("vmovdqa %ymm12,%ymm13");
				asm volatile ("vpshufb %ymm8,%ymm12,%ymm12");
				asm volatile ("vpshufb %ymm9,%ymm13,%ymm13");
				asm volatile ("vpxor %ymm12,%ymm14,%ymm14");
				asm volatile ("vpxor %ymm13,%ymm15,%ymm15");
			}

			asm volatile ("vmovdqa %%ymm14,%0" : "=m" (pa[j][i]));
			asm volatile ("vmovdqa %%ymm15,%0" : "=m" (pa[j][i + 32]));
		}

		/* derive the final missing block from raw Pdelta when P is available */
		if (has_p) {
			if (nr >= 2) {
				asm volatile ("vpxor %0,%%ymm4,%%ymm4" : : "m" (pa[0][i]));
				asm volatile ("vpxor %0,%%ymm5,%%ymm5" : : "m" (pa[0][i + 32]));
				asm volatile ("vmovdqa %%ymm4,%0" : "=m" (pa[1][i]));
				asm volatile ("vmovdqa %%ymm5,%0" : "=m" (pa[1][i + 32]));
			} else {
				asm volatile ("vmovdqa %%ymm4,%0" : "=m" (pa[0][i]));
				asm volatile ("vmovdqa %%ymm5,%0" : "=m" (pa[0][i + 32]));
			}
		}
	}

	raid_avx_end();
}

/*
 * Recover multiple data failures using selected parity blocks with AVX2 extended optimized for up to three failures.
 *
 * Process 64 bytes at a time as two independent 32-byte lanes.
 * Multiplication tables are broadcast once and shared between both lanes.
 *
 * Note that it uses 16 registers, meaning that x64 is required.
 */
static __always_inline void raid_recX_avx2ext_123(int nr, int has_p, int *id, int *ip, int nd, size_t size, void **vv)
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

	/* setup the coefficients matrix */
	for (j = 0; j < nr; ++j)
		for (k = 0; k < nr; ++k)
			G[j * nr + k] = A(ip[j], id[k]);

	/* invert it to solve the system of linear equations */
	raid_invert(G, V, nr);

	/* setup selected parity and destination pointers */
	for (j = 0; j < nr; ++j) {
		p[j] = v[nd + ip[j]];
		pa[j] = v[id[j]];
	}

	/* build the compact surviving-data list and its syndrome tables */
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

	/* precompute inverse-matrix multiplication table pointers */
	for (j = 0; j < nr; ++j)
		for (k = 0; k < nr; ++k)
			R[j][k] = &raid_gfmulpshufb[V[j * nr + k]][0][0];

	raid_avx_begin();

	for (i = 0; i < size; i += 64) {
		asm volatile ("vpbroadcastb %0,%%ymm15" : : "m" (gfconst16.low4[0]));

		/*
		 * Start each syndrome with the corresponding stored parity.
		 *
		 * During syndrome computation:
		 *
		 *   ymm0/ymm1   syndrome 0, first/second 32 bytes
		 *   ymm2/ymm3   syndrome 1, first/second 32 bytes
		 *   ymm4/ymm5   syndrome 2, first/second 32 bytes
		 *
		 *   ymm6/ymm7   source low, first/second 32 bytes
		 *   ymm8/ymm9   source high, first/second 32 bytes
		 *   ymm12/ymm13 multiplication table / temporary
		 *   ymm15       low-nibble mask
		 */
		asm volatile ("vmovdqa %0,%%ymm0" : : "m" (p[0][i]));
		asm volatile ("vmovdqa %0,%%ymm1" : : "m" (p[0][i + 32]));

		if (nr >= 2) {
			asm volatile ("vmovdqa %0,%%ymm2" : : "m" (p[1][i]));
			asm volatile ("vmovdqa %0,%%ymm3" : : "m" (p[1][i + 32]));
		}

		if (nr >= 3) {
			asm volatile ("vmovdqa %0,%%ymm4" : : "m" (p[2][i]));
			asm volatile ("vmovdqa %0,%%ymm5" : : "m" (p[2][i + 32]));
		}

		/* add all surviving data contributions */
		for (s = 0; s < ns; ++s) {
			const uint8_t **t = S[s];

			asm volatile ("vmovdqa %0,%%ymm6" : : "m" (src[s][i]));
			asm volatile ("vmovdqa %0,%%ymm7" : : "m" (src[s][i + 32]));

			/*
			 * P has coefficient 1 for every data disk.
			 * Do the XOR before destroying the original source
			 * while splitting it into low/high nibbles.
			 */
			if (has_p) {
				asm volatile ("vpxor %ymm6,%ymm0,%ymm0");
				asm volatile ("vpxor %ymm7,%ymm1,%ymm1");

				/* split both 32-byte source lanes */
				asm volatile ("vpsrlw $4,%ymm6,%ymm8");
				asm volatile ("vpsrlw $4,%ymm7,%ymm9");
				asm volatile ("vpand %ymm15,%ymm6,%ymm6");
				asm volatile ("vpand %ymm15,%ymm7,%ymm7");
				asm volatile ("vpand %ymm15,%ymm8,%ymm8");
				asm volatile ("vpand %ymm15,%ymm9,%ymm9");
			} else {
				/* split both 32-byte source lanes */
				asm volatile ("vpsrlw $4,%ymm6,%ymm8");
				asm volatile ("vpsrlw $4,%ymm7,%ymm9");
				asm volatile ("vpand %ymm15,%ymm6,%ymm6");
				asm volatile ("vpand %ymm15,%ymm7,%ymm7");
				asm volatile ("vpand %ymm15,%ymm8,%ymm8");
				asm volatile ("vpand %ymm15,%ymm9,%ymm9");

				/*
				 * Syndrome 0.
				 *
				 * Share each table broadcast between the two
				 * 32-byte lanes.
				 */
				asm volatile ("vbroadcasti128 %0,%%ymm12" : : "m" (t[0][0]));
				asm volatile ("vmovdqa %ymm12,%ymm13");
				asm volatile ("vpshufb %ymm6,%ymm12,%ymm12");
				asm volatile ("vpshufb %ymm7,%ymm13,%ymm13");
				asm volatile ("vpxor %ymm12,%ymm0,%ymm0");
				asm volatile ("vpxor %ymm13,%ymm1,%ymm1");

				asm volatile ("vbroadcasti128 %0,%%ymm12" : : "m" (t[0][16]));
				asm volatile ("vmovdqa %ymm12,%ymm13");
				asm volatile ("vpshufb %ymm8,%ymm12,%ymm12");
				asm volatile ("vpshufb %ymm9,%ymm13,%ymm13");
				asm volatile ("vpxor %ymm12,%ymm0,%ymm0");
				asm volatile ("vpxor %ymm13,%ymm1,%ymm1");
			}

			/* syndrome 1 */
			if (nr >= 2) {
				asm volatile ("vbroadcasti128 %0,%%ymm12" : : "m" (t[1][0]));
				asm volatile ("vmovdqa %ymm12,%ymm13");
				asm volatile ("vpshufb %ymm6,%ymm12,%ymm12");
				asm volatile ("vpshufb %ymm7,%ymm13,%ymm13");
				asm volatile ("vpxor %ymm12,%ymm2,%ymm2");
				asm volatile ("vpxor %ymm13,%ymm3,%ymm3");

				asm volatile ("vbroadcasti128 %0,%%ymm12" : : "m" (t[1][16]));
				asm volatile ("vmovdqa %ymm12,%ymm13");
				asm volatile ("vpshufb %ymm8,%ymm12,%ymm12");
				asm volatile ("vpshufb %ymm9,%ymm13,%ymm13");
				asm volatile ("vpxor %ymm12,%ymm2,%ymm2");
				asm volatile ("vpxor %ymm13,%ymm3,%ymm3");
			}

			/* syndrome 2 */
			if (nr >= 3) {
				asm volatile ("vbroadcasti128 %0,%%ymm12" : : "m" (t[2][0]));
				asm volatile ("vmovdqa %ymm12,%ymm13");
				asm volatile ("vpshufb %ymm6,%ymm12,%ymm12");
				asm volatile ("vpshufb %ymm7,%ymm13,%ymm13");
				asm volatile ("vpxor %ymm12,%ymm4,%ymm4");
				asm volatile ("vpxor %ymm13,%ymm5,%ymm5");

				asm volatile ("vbroadcasti128 %0,%%ymm12" : : "m" (t[2][16]));
				asm volatile ("vmovdqa %ymm12,%ymm13");
				asm volatile ("vpshufb %ymm8,%ymm12,%ymm12");
				asm volatile ("vpshufb %ymm9,%ymm13,%ymm13");
				asm volatile ("vpxor %ymm12,%ymm4,%ymm4");
				asm volatile ("vpxor %ymm13,%ymm5,%ymm5");
			}
		}

		/*
		 * Split all completed syndromes.
		 *
		 * After this:
		 *
		 *   syndrome 0:
		 *     ymm0 / ymm6   first  32 bytes low/high
		 *     ymm1 / ymm7   second 32 bytes low/high
		 *
		 *   syndrome 1:
		 *     ymm2 / ymm8   first  32 bytes low/high
		 *     ymm3 / ymm9   second 32 bytes low/high
		 *
		 *   syndrome 2:
		 *     ymm4 / ymm10  first  32 bytes low/high
		 *     ymm5 / ymm11  second 32 bytes low/high
		 */
		asm volatile ("vpsrlw $4,%ymm0,%ymm6");
		asm volatile ("vpsrlw $4,%ymm1,%ymm7");
		asm volatile ("vpand %ymm15,%ymm0,%ymm0");
		asm volatile ("vpand %ymm15,%ymm1,%ymm1");
		asm volatile ("vpand %ymm15,%ymm6,%ymm6");
		asm volatile ("vpand %ymm15,%ymm7,%ymm7");

		if (nr >= 2) {
			asm volatile ("vpsrlw $4,%ymm2,%ymm8");
			asm volatile ("vpsrlw $4,%ymm3,%ymm9");
			asm volatile ("vpand %ymm15,%ymm2,%ymm2");
			asm volatile ("vpand %ymm15,%ymm3,%ymm3");
			asm volatile ("vpand %ymm15,%ymm8,%ymm8");
			asm volatile ("vpand %ymm15,%ymm9,%ymm9");
		}

		if (nr >= 3) {
			asm volatile ("vpsrlw $4,%ymm4,%ymm10");
			asm volatile ("vpsrlw $4,%ymm5,%ymm11");
			asm volatile ("vpand %ymm15,%ymm4,%ymm4");
			asm volatile ("vpand %ymm15,%ymm5,%ymm5");
			asm volatile ("vpand %ymm15,%ymm10,%ymm10");
			asm volatile ("vpand %ymm15,%ymm11,%ymm11");
		}

		/*
		 * Reconstruct every missing data block.
		 *
		 * ymm12/ymm13 are a shared multiplication table pair.
		 * ymm14/ymm15 accumulate the two 32-byte output lanes.
		 *
		 * At this point ymm15 is no longer needed as the nibble mask,
		 * so all 16 YMM registers are available.
		 */
		for (j = 0; j < nr; ++j) {
			const uint8_t **t = R[j];

			/*
			 * First coefficient.
			 * Initialize both output accumulators from the low
			 * nibble products.
			 */
			asm volatile ("vbroadcasti128 %0,%%ymm12" : : "m" (t[0][0]));
			asm volatile ("vmovdqa %ymm12,%ymm13");
			asm volatile ("vpshufb %ymm0,%ymm12,%ymm12");
			asm volatile ("vpshufb %ymm1,%ymm13,%ymm13");
			asm volatile ("vmovdqa %ymm12,%ymm14");
			asm volatile ("vmovdqa %ymm13,%ymm15");

			asm volatile ("vbroadcasti128 %0,%%ymm12" : : "m" (t[0][16]));
			asm volatile ("vmovdqa %ymm12,%ymm13");
			asm volatile ("vpshufb %ymm6,%ymm12,%ymm12");
			asm volatile ("vpshufb %ymm7,%ymm13,%ymm13");
			asm volatile ("vpxor %ymm12,%ymm14,%ymm14");
			asm volatile ("vpxor %ymm13,%ymm15,%ymm15");

			/* second coefficient */
			if (nr >= 2) {
				asm volatile ("vbroadcasti128 %0,%%ymm12" : : "m" (t[1][0]));
				asm volatile ("vmovdqa %ymm12,%ymm13");
				asm volatile ("vpshufb %ymm2,%ymm12,%ymm12");
				asm volatile ("vpshufb %ymm3,%ymm13,%ymm13");
				asm volatile ("vpxor %ymm12,%ymm14,%ymm14");
				asm volatile ("vpxor %ymm13,%ymm15,%ymm15");

				asm volatile ("vbroadcasti128 %0,%%ymm12" : : "m" (t[1][16]));
				asm volatile ("vmovdqa %ymm12,%ymm13");
				asm volatile ("vpshufb %ymm8,%ymm12,%ymm12");
				asm volatile ("vpshufb %ymm9,%ymm13,%ymm13");
				asm volatile ("vpxor %ymm12,%ymm14,%ymm14");
				asm volatile ("vpxor %ymm13,%ymm15,%ymm15");
			}

			/* third coefficient */
			if (nr >= 3) {
				asm volatile ("vbroadcasti128 %0,%%ymm12" : : "m" (t[2][0]));
				asm volatile ("vmovdqa %ymm12,%ymm13");
				asm volatile ("vpshufb %ymm4,%ymm12,%ymm12");
				asm volatile ("vpshufb %ymm5,%ymm13,%ymm13");
				asm volatile ("vpxor %ymm12,%ymm14,%ymm14");
				asm volatile ("vpxor %ymm13,%ymm15,%ymm15");

				asm volatile ("vbroadcasti128 %0,%%ymm12" : : "m" (t[2][16]));
				asm volatile ("vmovdqa %ymm12,%ymm13");
				asm volatile ("vpshufb %ymm10,%ymm12,%ymm12");
				asm volatile ("vpshufb %ymm11,%ymm13,%ymm13");
				asm volatile ("vpxor %ymm12,%ymm14,%ymm14");
				asm volatile ("vpxor %ymm13,%ymm15,%ymm15");
			}

			asm volatile ("vmovdqa %%ymm14,%0" : "=m" (pa[j][i]));
			asm volatile ("vmovdqa %%ymm15,%0" : "=m" (pa[j][i + 32]));
		}
	}

	raid_avx_end();
}

/*
 * Recover multiple data failures using selected parity blocks with AVX2 extended.
 *
 * Compute only the selected syndromes, keeping them in registers.
 * This avoids raid_delta_gen(), temporary syndrome buffers, and the
 * generation of unused parity rows.
 *
 * If P is available, preserve the complete P delta syndrome and
 * reconstruct only nr - 1 missing blocks through the inverse matrix.
 * The last missing block is obtained by XORing the reconstructed blocks
 * out of Pdelta.
 */
static __always_inline void raid_recX_avx2ext(int nr, int has_p, int *id, int *ip, int nd, size_t size, void **vv)
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

	/* setup the coefficients matrix */
	for (j = 0; j < nr; ++j)
		for (k = 0; k < nr; ++k)
			G[j * nr + k] = A(ip[j], id[k]);

	/* invert it to solve the system of linear equations */
	raid_invert(G, V, nr);

	/* setup selected parity and destination pointers */
	for (j = 0; j < nr; ++j) {
		p[j] = v[nd + ip[j]];
		pa[j] = v[id[j]];
	}

	/* build the compact surviving-data list and its syndrome tables */
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
	 * If P is available, the last inverse-matrix row is not needed.
	 * The corresponding missing block is reconstructed by XOR.
	 */
	for (j = 0; j < nr - has_p; ++j)
		for (k = 0; k < nr; ++k)
			R[j][k] = &raid_gfmulpshufb[V[j * nr + k]][0][0];

	raid_avx_begin();

	for (i = 0; i < size; i += 32) {
		asm volatile ("vpbroadcastb %0,%%ymm15" : : "m" (gfconst16.low4[0]));
		/*
		 * Start each syndrome with the corresponding stored parity.
		 *
		 * During syndrome computation:
		 *
		 *   ymm0   syndrome 0
		 *   ymm2   syndrome 1
		 *   ymm4   syndrome 2
		 *   ymm6   syndrome 3
		 *   ymm8   syndrome 4
		 *   ymm10  syndrome 5
		 *
		 *   ymm12  source / source low nibble
		 *   ymm13  source high nibble
		 *   ymm14  multiplication table / temporary
		 *   ymm15  low-nibble mask
		 */
		asm volatile ("vmovdqa %0,%%ymm0" : : "m" (p[0][i]));

		if (nr >= 2)
			asm volatile ("vmovdqa %0,%%ymm2" : : "m" (p[1][i]));

		if (nr >= 3)
			asm volatile ("vmovdqa %0,%%ymm4" : : "m" (p[2][i]));

		if (nr >= 4)
			asm volatile ("vmovdqa %0,%%ymm6" : : "m" (p[3][i]));

		if (nr >= 5)
			asm volatile ("vmovdqa %0,%%ymm8" : : "m" (p[4][i]));

		if (nr >= 6)
			asm volatile ("vmovdqa %0,%%ymm10" : : "m" (p[5][i]));

		/* add all surviving data contributions */
		for (s = 0; s < ns; ++s) {
			const uint8_t **t = S[s];

			asm volatile ("vmovdqa %0,%%ymm12" : : "m" (src[s][i]));

			/*
			 * P has coefficient 1 for every data disk.
			 *
			 * Keep the simple XOR fast path when P is the
			 * first selected parity. Otherwise syndrome 0
			 * uses the normal vpshufb multiplication.
			 */
			if (has_p) {
				asm volatile ("vpxor %ymm12,%ymm0,%ymm0");

				asm volatile ("vpsrlw $4,%ymm12,%ymm13");
				asm volatile ("vpand %ymm15,%ymm12,%ymm12");
				asm volatile ("vpand %ymm15,%ymm13,%ymm13");
			} else {
				asm volatile ("vpsrlw $4,%ymm12,%ymm13");
				asm volatile ("vpand %ymm15,%ymm12,%ymm12");
				asm volatile ("vpand %ymm15,%ymm13,%ymm13");

				asm volatile ("vbroadcasti128 %0,%%ymm14" : : "m" (t[0][0]));
				asm volatile ("vpshufb %ymm12,%ymm14,%ymm14");
				asm volatile ("vpxor %ymm14,%ymm0,%ymm0");

				asm volatile ("vbroadcasti128 %0,%%ymm14" : : "m" (t[0][16]));
				asm volatile ("vpshufb %ymm13,%ymm14,%ymm14");
				asm volatile ("vpxor %ymm14,%ymm0,%ymm0");
			}

			if (nr >= 2) {
				asm volatile ("vbroadcasti128 %0,%%ymm14" : : "m" (t[1][0]));
				asm volatile ("vpshufb %ymm12,%ymm14,%ymm14");
				asm volatile ("vpxor %ymm14,%ymm2,%ymm2");
				asm volatile ("vbroadcasti128 %0,%%ymm14" : : "m" (t[1][16]));
				asm volatile ("vpshufb %ymm13,%ymm14,%ymm14");
				asm volatile ("vpxor %ymm14,%ymm2,%ymm2");
			}

			if (nr >= 3) {
				asm volatile ("vbroadcasti128 %0,%%ymm14" : : "m" (t[2][0]));
				asm volatile ("vpshufb %ymm12,%ymm14,%ymm14");
				asm volatile ("vpxor %ymm14,%ymm4,%ymm4");
				asm volatile ("vbroadcasti128 %0,%%ymm14" : : "m" (t[2][16]));
				asm volatile ("vpshufb %ymm13,%ymm14,%ymm14");
				asm volatile ("vpxor %ymm14,%ymm4,%ymm4");
			}

			if (nr >= 4) {
				asm volatile ("vbroadcasti128 %0,%%ymm14" : : "m" (t[3][0]));
				asm volatile ("vpshufb %ymm12,%ymm14,%ymm14");
				asm volatile ("vpxor %ymm14,%ymm6,%ymm6");
				asm volatile ("vbroadcasti128 %0,%%ymm14" : : "m" (t[3][16]));
				asm volatile ("vpshufb %ymm13,%ymm14,%ymm14");
				asm volatile ("vpxor %ymm14,%ymm6,%ymm6");
			}

			if (nr >= 5) {
				asm volatile ("vbroadcasti128 %0,%%ymm14" : : "m" (t[4][0]));
				asm volatile ("vpshufb %ymm12,%ymm14,%ymm14");
				asm volatile ("vpxor %ymm14,%ymm8,%ymm8");
				asm volatile ("vbroadcasti128 %0,%%ymm14" : : "m" (t[4][16]));
				asm volatile ("vpshufb %ymm13,%ymm14,%ymm14");
				asm volatile ("vpxor %ymm14,%ymm8,%ymm8");
			}

			if (nr >= 6) {
				asm volatile ("vbroadcasti128 %0,%%ymm14" : : "m" (t[5][0]));
				asm volatile ("vpshufb %ymm12,%ymm14,%ymm14");
				asm volatile ("vpxor %ymm14,%ymm10,%ymm10");
				asm volatile ("vbroadcasti128 %0,%%ymm14" : : "m" (t[5][16]));
				asm volatile ("vpshufb %ymm13,%ymm14,%ymm14");
				asm volatile ("vpxor %ymm14,%ymm10,%ymm10");
			}
		}

		/*
		 * Preserve the complete P delta before splitting syndrome 0.
		 *
		 * ymm14 is no longer needed by syndrome generation and is not
		 * used by the split. After the split, Pdelta is moved to ymm15,
		 * freeing ymm14 again for reconstruction.
		 */
		if (has_p)
			asm volatile ("vmovdqa %ymm0,%ymm14");

		/*
		 * Split every completed syndrome once.
		 *
		 * After this:
		 *
		 *   ymm0 /ymm1   delta 0 low/high
		 *   ymm2 /ymm3   delta 1 low/high
		 *   ymm4 /ymm5   delta 2 low/high
		 *   ymm6 /ymm7   delta 3 low/high
		 *   ymm8 /ymm9   delta 4 low/high
		 *   ymm10/ymm11  delta 5 low/high
		 */
		asm volatile ("vpsrlw $4,%ymm0,%ymm1");
		asm volatile ("vpand %ymm15,%ymm0,%ymm0");
		asm volatile ("vpand %ymm15,%ymm1,%ymm1");

		if (nr >= 2) {
			asm volatile ("vpsrlw $4,%ymm2,%ymm3");
			asm volatile ("vpand %ymm15,%ymm2,%ymm2");
			asm volatile ("vpand %ymm15,%ymm3,%ymm3");
		}

		if (nr >= 3) {
			asm volatile ("vpsrlw $4,%ymm4,%ymm5");
			asm volatile ("vpand %ymm15,%ymm4,%ymm4");
			asm volatile ("vpand %ymm15,%ymm5,%ymm5");
		}

		if (nr >= 4) {
			asm volatile ("vpsrlw $4,%ymm6,%ymm7");
			asm volatile ("vpand %ymm15,%ymm6,%ymm6");
			asm volatile ("vpand %ymm15,%ymm7,%ymm7");
		}

		if (nr >= 5) {
			asm volatile ("vpsrlw $4,%ymm8,%ymm9");
			asm volatile ("vpand %ymm15,%ymm8,%ymm8");
			asm volatile ("vpand %ymm15,%ymm9,%ymm9");
		}

		if (nr >= 6) {
			asm volatile ("vpsrlw $4,%ymm10,%ymm11");
			asm volatile ("vpand %ymm15,%ymm10,%ymm10");
			asm volatile ("vpand %ymm15,%ymm11,%ymm11");
		}

		/*
		 * The nibble mask is no longer needed.
		 *
		 * Move the complete P delta from the temporary register to
		 * ymm15, which remains the P accumulator for reconstruction.
		 * ymm14 is now free again as the multiplication temporary.
		 */
		if (has_p)
			asm volatile ("vmovdqa %ymm14,%ymm15");

		/*
		 * Reconstruct the missing data blocks.
		 *
		 * If P is available, only nr - 1 outputs are reconstructed
		 * through the inverse matrix. Each reconstructed output is
		 * XORed out of ymm15. The remaining value is the final
		 * missing block.
		 *
		 *   ymm12 = low accumulator
		 *   ymm13 = high accumulator
		 *   ymm14 = multiplication table / result
		 *   ymm15 = remaining Pdelta, if has_p
		 */
		for (j = 0; j < nr - has_p; ++j) {
			const uint8_t **t = R[j];

			/* first coefficient initializes the low/high accumulators */
			asm volatile ("vbroadcasti128 %0,%%ymm12" : : "m" (t[0][0]));
			asm volatile ("vbroadcasti128 %0,%%ymm13" : : "m" (t[0][16]));
			asm volatile ("vpshufb %ymm0,%ymm12,%ymm12");
			asm volatile ("vpshufb %ymm1,%ymm13,%ymm13");

			if (nr >= 2) {
				asm volatile ("vbroadcasti128 %0,%%ymm14" : : "m" (t[1][0]));
				asm volatile ("vpshufb %ymm2,%ymm14,%ymm14");
				asm volatile ("vpxor %ymm14,%ymm12,%ymm12");
				asm volatile ("vbroadcasti128 %0,%%ymm14" : : "m" (t[1][16]));
				asm volatile ("vpshufb %ymm3,%ymm14,%ymm14");
				asm volatile ("vpxor %ymm14,%ymm13,%ymm13");
			}

			if (nr >= 3) {
				asm volatile ("vbroadcasti128 %0,%%ymm14" : : "m" (t[2][0]));
				asm volatile ("vpshufb %ymm4,%ymm14,%ymm14");
				asm volatile ("vpxor %ymm14,%ymm12,%ymm12");
				asm volatile ("vbroadcasti128 %0,%%ymm14" : : "m" (t[2][16]));
				asm volatile ("vpshufb %ymm5,%ymm14,%ymm14");
				asm volatile ("vpxor %ymm14,%ymm13,%ymm13");
			}

			if (nr >= 4) {
				asm volatile ("vbroadcasti128 %0,%%ymm14" : : "m" (t[3][0]));
				asm volatile ("vpshufb %ymm6,%ymm14,%ymm14");
				asm volatile ("vpxor %ymm14,%ymm12,%ymm12");
				asm volatile ("vbroadcasti128 %0,%%ymm14" : : "m" (t[3][16]));
				asm volatile ("vpshufb %ymm7,%ymm14,%ymm14");
				asm volatile ("vpxor %ymm14,%ymm13,%ymm13");
			}

			if (nr >= 5) {
				asm volatile ("vbroadcasti128 %0,%%ymm14" : : "m" (t[4][0]));
				asm volatile ("vpshufb %ymm8,%ymm14,%ymm14");
				asm volatile ("vpxor %ymm14,%ymm12,%ymm12");
				asm volatile ("vbroadcasti128 %0,%%ymm14" : : "m" (t[4][16]));
				asm volatile ("vpshufb %ymm9,%ymm14,%ymm14");
				asm volatile ("vpxor %ymm14,%ymm13,%ymm13");
			}

			if (nr >= 6) {
				asm volatile ("vbroadcasti128 %0,%%ymm14" : : "m" (t[5][0]));
				asm volatile ("vpshufb %ymm10,%ymm14,%ymm14");
				asm volatile ("vpxor %ymm14,%ymm12,%ymm12");
				asm volatile ("vbroadcasti128 %0,%%ymm14" : : "m" (t[5][16]));
				asm volatile ("vpshufb %ymm11,%ymm14,%ymm14");
				asm volatile ("vpxor %ymm14,%ymm13,%ymm13");
			}

			asm volatile ("vpxor %ymm13,%ymm12,%ymm12");

			if (has_p)
				asm volatile ("vpxor %ymm12,%ymm15,%ymm15");

			asm volatile ("vmovdqa %%ymm12,%0" : "=m" (pa[j][i]));
		}

		if (has_p)
			asm volatile ("vmovdqa %%ymm15,%0" : "=m" (pa[nr - 1][i]));
	}

	raid_avx_end();
}
#endif

void raid_gen2_avx2_raid(int nd, size_t size, void **vv)
{
	raid_gen2_avx2_gen(nd, size, vv, 2);
}

void raid_gen2_avx2_aes(int nd, size_t size, void **vv)
{
	raid_gen2_avx2_gen(nd, size, vv, 3);
}

#ifdef CONFIG_X86_64
void raid_gen2_avx2ext_raid(int nd, size_t size, void **vv)
{
	raid_gen2_avx2ext_gen(nd, size, vv, 2);
}

void raid_gen2_avx2ext_aes(int nd, size_t size, void **vv)
{
	raid_gen2_avx2ext_gen(nd, size, vv, 3);
}

void raid_gen3_avx2ext_raid(int nd, size_t size, void **vv)
{
	raid_gen3_avx2ext_gen(nd, size, vv, 2);
}

void raid_gen3_avx2ext_aes(int nd, size_t size, void **vv)
{
	raid_gen3_avx2ext_gen(nd, size, vv, 3);
}

void raid_gen4_avx2ext_raid(int nd, size_t size, void **vv)
{
	raid_gen4_avx2ext_gen(nd, size, vv, 2);
}

void raid_gen4_avx2ext_aes(int nd, size_t size, void **vv)
{
	raid_gen4_avx2ext_gen(nd, size, vv, 3);
}

void raid_gen5_avx2ext_raid(int nd, size_t size, void **vv)
{
	raid_genX_avx2ext(nd, size, vv, 5, 2);
}

void raid_gen5_avx2ext_aes(int nd, size_t size, void **vv)
{
	raid_genX_avx2ext(nd, size, vv, 5, 3);
}

void raid_gen6_avx2ext_raid(int nd, size_t size, void **vv)
{
	raid_genX_avx2ext(nd, size, vv, 6, 2);
}

void raid_gen6_avx2ext_aes(int nd, size_t size, void **vv)
{
	raid_genX_avx2ext(nd, size, vv, 6, 3);
}

#endif

void raid_rec1_avx2(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 1);

	/* if recovering with P uses the delta function */
	if (ip[0] == 0) {
		raid_rec1of1(id, nd, size, vv);
		return;
	}

	/* if recovering with Q use the specialized function */
	if (ip[0] == 1) {
		raid_rec1_avx2_q(id, ip, nd, size, vv);
		return;
	}

	raid_rec1_avx2_1(id, ip, nd, size, vv);
}

void raid_rec2_avx2(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 2);
	if (ip[0] == 0)
		raid_recX_avx2_1234(2, 1, id, ip, nd, size, vv);
	else
		raid_recX_avx2_1234(2, 0, id, ip, nd, size, vv);
}

void raid_rec3_avx2(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 3);
	if (ip[0] == 0)
		raid_recX_avx2_1234(3, 1, id, ip, nd, size, vv);
	else
		raid_recX_avx2_1234(3, 0, id, ip, nd, size, vv);
}

void raid_rec4_avx2(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 4);
	if (ip[0] == 0)
		raid_recX_avx2_1234(4, 1, id, ip, nd, size, vv);
	else
		raid_recX_avx2_1234(4, 0, id, ip, nd, size, vv);
}

void raid_rec5_avx2(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 5);
	if (ip[0] == 0)
		raid_recX_avx2(5, 1, id, ip, nd, size, vv);
	else
		raid_recX_avx2(5, 0, id, ip, nd, size, vv);
}

void raid_rec6_avx2(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 6);
	if (ip[0] == 0)
		raid_recX_avx2(6, 1, id, ip, nd, size, vv);
	else
		raid_recX_avx2(6, 0, id, ip, nd, size, vv);
}

#ifdef CONFIG_X86_64
void raid_rec1_avx2ext(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 1);

	/* if recovering with P uses the delta function */
	if (ip[0] == 0) {
		raid_rec1of1(id, nd, size, vv);
		return;
	}

	/* if recovering with Q use the specialized function */
	if (ip[0] == 1) {
		raid_rec1_avx2ext_q(id, ip, nd, size, vv);
		return;
	}

	raid_rec1_avx2_1(id, ip, nd, size, vv);
}

void raid_rec2_avx2ext(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 2);
	if (ip[0] == 0)
		raid_recX_avx2ext_12(2, 1, id, ip, nd, size, vv);
	else
		raid_recX_avx2ext_12(2, 0, id, ip, nd, size, vv);
}

void raid_rec3_avx2ext(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 3);
	if (ip[0] == 0)
		raid_recX_avx2ext_123(3, 1, id, ip, nd, size, vv);
	else
		raid_recX_avx2ext_123(3, 0, id, ip, nd, size, vv);
}

void raid_rec4_avx2ext(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 4);
	if (ip[0] == 0)
		raid_recX_avx2ext(4, 1, id, ip, nd, size, vv);
	else
		raid_recX_avx2ext(4, 0, id, ip, nd, size, vv);
}

void raid_rec5_avx2ext(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 5);
	if (ip[0] == 0)
		raid_recX_avx2ext(5, 1, id, ip, nd, size, vv);
	else
		raid_recX_avx2ext(5, 0, id, ip, nd, size, vv);
}

void raid_rec6_avx2ext(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 6);
	if (ip[0] == 0)
		raid_recX_avx2ext(6, 1, id, ip, nd, size, vv);
	else
		raid_recX_avx2ext(6, 0, id, ip, nd, size, vv);
}
#endif

void raid_register_avx2(void)
{
	if (raid_cpu_has_avx2()) {
		raid_gen_register(RAID_ALGO_CAUCHY_PAR1, "avx2", raid_gen1_avx2, RAID_POLY_ANY);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR2, "avx2", raid_gen2_avx2_raid, RAID_POLY_RAID);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR2, "avx2", raid_gen2_avx2_aes, RAID_POLY_AES);
#ifdef CONFIG_X86_64
		raid_gen_register(RAID_ALGO_CAUCHY_PAR2, "avx2e", raid_gen2_avx2ext_raid, RAID_POLY_RAID);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR2, "avx2e", raid_gen2_avx2ext_aes, RAID_POLY_AES);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR3, "avx2e", raid_gen3_avx2ext_raid, RAID_POLY_RAID);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR3, "avx2e", raid_gen3_avx2ext_aes, RAID_POLY_AES);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR4, "avx2e", raid_gen4_avx2ext_raid, RAID_POLY_RAID);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR4, "avx2e", raid_gen4_avx2ext_aes, RAID_POLY_AES);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR5, "avx2e", raid_gen5_avx2ext_raid, RAID_POLY_RAID);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR5, "avx2e", raid_gen5_avx2ext_aes, RAID_POLY_AES);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR6, "avx2e", raid_gen6_avx2ext_raid, RAID_POLY_RAID);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR6, "avx2e", raid_gen6_avx2ext_aes, RAID_POLY_AES);
		raid_gen_register(RAID_ALGO_VANDERMONDE_PAR3, "avx2e", raid_genz_avx2ext_raid, RAID_POLY_RAID);
#endif

		raid_rec_register(RAID_ALGO_CAUCHY_PAR1, "avx2", raid_rec1_avx2, RAID_POLY_ANY);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR2, "avx2", raid_rec2_avx2, RAID_POLY_ANY);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR3, "avx2", raid_rec3_avx2, RAID_POLY_ANY);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR4, "avx2", raid_rec4_avx2, RAID_POLY_ANY);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR5, "avx2", raid_rec5_avx2, RAID_POLY_ANY);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR6, "avx2", raid_rec6_avx2, RAID_POLY_ANY);
#ifdef CONFIG_X86_64
		raid_rec_register(RAID_ALGO_CAUCHY_PAR1, "avx2e", raid_rec1_avx2ext, RAID_POLY_ANY);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR2, "avx2e", raid_rec2_avx2ext, RAID_POLY_ANY);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR3, "avx2e", raid_rec3_avx2ext, RAID_POLY_ANY);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR4, "avx2e", raid_rec4_avx2ext, RAID_POLY_ANY);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR5, "avx2e", raid_rec5_avx2ext, RAID_POLY_ANY);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR6, "avx2e", raid_rec6_avx2ext, RAID_POLY_ANY);

#endif
	}
}

#endif
