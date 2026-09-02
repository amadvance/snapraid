// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Andrea Mazzoleni

#include "internal.h"
#include "gf.h"
#include "cpu.h"

#ifdef CONFIG_X86_64
/*
 * Generate N parity blocks for the RAID polynomial using direct Cauchy coefficients with AVX2 GFNI.
 */
static __always_inline void raid_genX_avx2gfni_raid(int nd, size_t size, void **vv, int np, int streaming)
{
	uint8_t **v = (uint8_t **)vv;
	size_t i;
	int d;

	if (nd == 1) {
		for (d = 0; d < np; ++d)
			if (v[1 + d] != v[0])
				memcpy(v[1 + d], v[0], size);
		return;
	}

	raid_avx_begin();

	for (i = 0; i < size; i += 64) {
		/* first disk with all coefficients at 1 */
		asm volatile ("vmovdqa %0,%%ymm0" : : "m" (v[0][i]));
		asm volatile ("vmovdqa %0,%%ymm1" : : "m" (v[0][i + 32]));

		asm volatile ("vmovdqa %ymm0,%ymm2");
		asm volatile ("vmovdqa %ymm1,%ymm3");
		if (np >= 3) {
			asm volatile ("vmovdqa %ymm0,%ymm4");
			asm volatile ("vmovdqa %ymm1,%ymm5");
		}
		if (np >= 4) {
			asm volatile ("vmovdqa %ymm0,%ymm6");
			asm volatile ("vmovdqa %ymm1,%ymm7");
		}
		if (np >= 5) {
			asm volatile ("vmovdqa %ymm0,%ymm8");
			asm volatile ("vmovdqa %ymm1,%ymm9");
		}
		if (np >= 6) {
			asm volatile ("vmovdqa %ymm0,%ymm10");
			asm volatile ("vmovdqa %ymm1,%ymm11");
		}

		/* all other disks use their direct Cauchy coefficients */
		for (d = 1; d < nd; ++d) {
			asm volatile ("vmovdqa %0,%%ymm12" : : "m" (v[d][i]));
			asm volatile ("vmovdqa %0,%%ymm13" : : "m" (v[d][i + 32]));

			asm volatile ("vpxor %ymm12,%ymm0,%ymm0");
			asm volatile ("vpxor %ymm13,%ymm1,%ymm1");

			asm volatile ("vpbroadcastq %0,%%ymm14" : : "m" (raid_gfcauchyaffine_raid[d][0][0]));
			asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm12,%ymm15");
			asm volatile ("vpxor %ymm15,%ymm2,%ymm2");
			asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm13,%ymm15");
			asm volatile ("vpxor %ymm15,%ymm3,%ymm3");

			if (np >= 3) {
				asm volatile ("vpbroadcastq %0,%%ymm14" : : "m" (raid_gfcauchyaffine_raid[d][1][0]));
				asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm12,%ymm15");
				asm volatile ("vpxor %ymm15,%ymm4,%ymm4");
				asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm13,%ymm15");
				asm volatile ("vpxor %ymm15,%ymm5,%ymm5");
			}
			if (np >= 4) {
				asm volatile ("vpbroadcastq %0,%%ymm14" : : "m" (raid_gfcauchyaffine_raid[d][2][0]));
				asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm12,%ymm15");
				asm volatile ("vpxor %ymm15,%ymm6,%ymm6");
				asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm13,%ymm15");
				asm volatile ("vpxor %ymm15,%ymm7,%ymm7");
			}
			if (np >= 5) {
				asm volatile ("vpbroadcastq %0,%%ymm14" : : "m" (raid_gfcauchyaffine_raid[d][3][0]));
				asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm12,%ymm15");
				asm volatile ("vpxor %ymm15,%ymm8,%ymm8");
				asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm13,%ymm15");
				asm volatile ("vpxor %ymm15,%ymm9,%ymm9");
			}
			if (np >= 6) {
				asm volatile ("vpbroadcastq %0,%%ymm14" : : "m" (raid_gfcauchyaffine_raid[d][4][0]));
				asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm12,%ymm15");
				asm volatile ("vpxor %ymm15,%ymm10,%ymm10");
				asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm13,%ymm15");
				asm volatile ("vpxor %ymm15,%ymm11,%ymm11");
			}
		}

		if (streaming) {
			asm volatile ("vmovntdq %%ymm0,%0" : "=m" (v[nd][i]));
			asm volatile ("vmovntdq %%ymm1,%0" : "=m" (v[nd][i + 32]));
			asm volatile ("vmovntdq %%ymm2,%0" : "=m" (v[nd + 1][i]));
			asm volatile ("vmovntdq %%ymm3,%0" : "=m" (v[nd + 1][i + 32]));
			if (np >= 3) {
				asm volatile ("vmovntdq %%ymm4,%0" : "=m" (v[nd + 2][i]));
				asm volatile ("vmovntdq %%ymm5,%0" : "=m" (v[nd + 2][i + 32]));
			}
			if (np >= 4) {
				asm volatile ("vmovntdq %%ymm6,%0" : "=m" (v[nd + 3][i]));
				asm volatile ("vmovntdq %%ymm7,%0" : "=m" (v[nd + 3][i + 32]));
			}
			if (np >= 5) {
				asm volatile ("vmovntdq %%ymm8,%0" : "=m" (v[nd + 4][i]));
				asm volatile ("vmovntdq %%ymm9,%0" : "=m" (v[nd + 4][i + 32]));
			}
			if (np >= 6) {
				asm volatile ("vmovntdq %%ymm10,%0" : "=m" (v[nd + 5][i]));
				asm volatile ("vmovntdq %%ymm11,%0" : "=m" (v[nd + 5][i + 32]));
			}
		} else {
			asm volatile ("vmovdqa %%ymm0,%0" : "=m" (v[nd][i]));
			asm volatile ("vmovdqa %%ymm1,%0" : "=m" (v[nd][i + 32]));
			asm volatile ("vmovdqa %%ymm2,%0" : "=m" (v[nd + 1][i]));
			asm volatile ("vmovdqa %%ymm3,%0" : "=m" (v[nd + 1][i + 32]));
			if (np >= 3) {
				asm volatile ("vmovdqa %%ymm4,%0" : "=m" (v[nd + 2][i]));
				asm volatile ("vmovdqa %%ymm5,%0" : "=m" (v[nd + 2][i + 32]));
			}
			if (np >= 4) {
				asm volatile ("vmovdqa %%ymm6,%0" : "=m" (v[nd + 3][i]));
				asm volatile ("vmovdqa %%ymm7,%0" : "=m" (v[nd + 3][i + 32]));
			}
			if (np >= 5) {
				asm volatile ("vmovdqa %%ymm8,%0" : "=m" (v[nd + 4][i]));
				asm volatile ("vmovdqa %%ymm9,%0" : "=m" (v[nd + 4][i + 32]));
			}
			if (np >= 6) {
				asm volatile ("vmovdqa %%ymm10,%0" : "=m" (v[nd + 5][i]));
				asm volatile ("vmovdqa %%ymm11,%0" : "=m" (v[nd + 5][i + 32]));
			}
		}
	}

	raid_avx_end(streaming);
}

/*
 * Generate two parity blocks (RAID6 with powers of 2) using AVX512 GFNI implementation.
 *
 * Preloads up to 28 Q affine matrix coefficients in ZMM registers (zmm4..zmm31)
 * with an unrolled disk loop and early exits.
 */
static __always_inline void raid_gen2_avx512gfni_raid_gen(int nd, size_t size, void **vv, int streaming)
{
	uint8_t **v = (uint8_t **)vv;
	size_t i;
	int d;

	if (nd == 1) {
		if (v[1] != v[0])
			memcpy(v[1], v[0], size);
		if (v[2] != v[0])
			memcpy(v[2], v[0], size);
		return;
	}

	raid_avx_begin();

	/* preload as many Q coefficient matrices as possible */
	asm volatile ("vpbroadcastq %0,%%zmm4" : : "m" (raid_gfcauchyaffine_raid[1][0][0]));
	asm volatile ("vpbroadcastq %0,%%zmm5" : : "m" (raid_gfcauchyaffine_raid[2][0][0]));
	asm volatile ("vpbroadcastq %0,%%zmm6" : : "m" (raid_gfcauchyaffine_raid[3][0][0]));
	asm volatile ("vpbroadcastq %0,%%zmm7" : : "m" (raid_gfcauchyaffine_raid[4][0][0]));
	asm volatile ("vpbroadcastq %0,%%zmm8" : : "m" (raid_gfcauchyaffine_raid[5][0][0]));
	asm volatile ("vpbroadcastq %0,%%zmm9" : : "m" (raid_gfcauchyaffine_raid[6][0][0]));
	asm volatile ("vpbroadcastq %0,%%zmm10" : : "m" (raid_gfcauchyaffine_raid[7][0][0]));
	asm volatile ("vpbroadcastq %0,%%zmm11" : : "m" (raid_gfcauchyaffine_raid[8][0][0]));
	asm volatile ("vpbroadcastq %0,%%zmm12" : : "m" (raid_gfcauchyaffine_raid[9][0][0]));
	asm volatile ("vpbroadcastq %0,%%zmm13" : : "m" (raid_gfcauchyaffine_raid[10][0][0]));
	asm volatile ("vpbroadcastq %0,%%zmm14" : : "m" (raid_gfcauchyaffine_raid[11][0][0]));
	asm volatile ("vpbroadcastq %0,%%zmm15" : : "m" (raid_gfcauchyaffine_raid[12][0][0]));
	asm volatile ("vpbroadcastq %0,%%zmm16" : : "m" (raid_gfcauchyaffine_raid[13][0][0]));
	asm volatile ("vpbroadcastq %0,%%zmm17" : : "m" (raid_gfcauchyaffine_raid[14][0][0]));
	asm volatile ("vpbroadcastq %0,%%zmm18" : : "m" (raid_gfcauchyaffine_raid[15][0][0]));
	asm volatile ("vpbroadcastq %0,%%zmm19" : : "m" (raid_gfcauchyaffine_raid[16][0][0]));
	asm volatile ("vpbroadcastq %0,%%zmm20" : : "m" (raid_gfcauchyaffine_raid[17][0][0]));
	asm volatile ("vpbroadcastq %0,%%zmm21" : : "m" (raid_gfcauchyaffine_raid[18][0][0]));
	asm volatile ("vpbroadcastq %0,%%zmm22" : : "m" (raid_gfcauchyaffine_raid[19][0][0]));
	asm volatile ("vpbroadcastq %0,%%zmm23" : : "m" (raid_gfcauchyaffine_raid[20][0][0]));
	asm volatile ("vpbroadcastq %0,%%zmm24" : : "m" (raid_gfcauchyaffine_raid[21][0][0]));
	asm volatile ("vpbroadcastq %0,%%zmm25" : : "m" (raid_gfcauchyaffine_raid[22][0][0]));
	asm volatile ("vpbroadcastq %0,%%zmm26" : : "m" (raid_gfcauchyaffine_raid[23][0][0]));
	asm volatile ("vpbroadcastq %0,%%zmm27" : : "m" (raid_gfcauchyaffine_raid[24][0][0]));
	asm volatile ("vpbroadcastq %0,%%zmm28" : : "m" (raid_gfcauchyaffine_raid[25][0][0]));
	asm volatile ("vpbroadcastq %0,%%zmm29" : : "m" (raid_gfcauchyaffine_raid[26][0][0]));
	asm volatile ("vpbroadcastq %0,%%zmm30" : : "m" (raid_gfcauchyaffine_raid[27][0][0]));
	asm volatile ("vpbroadcastq %0,%%zmm31" : : "m" (raid_gfcauchyaffine_raid[28][0][0]));

	for (i = 0; i < size; i += 64) {
		asm volatile ("vmovdqa64 %0,%%zmm0" : : "m" (v[0][i]));
		asm volatile ("vmovdqa64 %zmm0,%zmm1");

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[1][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8affineqb $0,%zmm4,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 2)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[2][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8affineqb $0,%zmm5,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 3)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[3][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8affineqb $0,%zmm6,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 4)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[4][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8affineqb $0,%zmm7,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 5)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[5][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8affineqb $0,%zmm8,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 6)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[6][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8affineqb $0,%zmm9,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 7)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[7][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8affineqb $0,%zmm10,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 8)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[8][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8affineqb $0,%zmm11,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 9)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[9][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8affineqb $0,%zmm12,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 10)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[10][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8affineqb $0,%zmm13,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 11)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[11][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8affineqb $0,%zmm14,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 12)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[12][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8affineqb $0,%zmm15,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 13)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[13][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8affineqb $0,%zmm16,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 14)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[14][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8affineqb $0,%zmm17,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 15)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[15][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8affineqb $0,%zmm18,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 16)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[16][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8affineqb $0,%zmm19,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 17)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[17][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8affineqb $0,%zmm20,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 18)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[18][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8affineqb $0,%zmm21,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 19)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[19][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8affineqb $0,%zmm22,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 20)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[20][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8affineqb $0,%zmm23,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 21)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[21][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8affineqb $0,%zmm24,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 22)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[22][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8affineqb $0,%zmm25,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 23)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[23][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8affineqb $0,%zmm26,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 24)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[24][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8affineqb $0,%zmm27,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 25)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[25][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8affineqb $0,%zmm28,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 26)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[26][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8affineqb $0,%zmm29,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 27)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[27][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8affineqb $0,%zmm30,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 28)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[28][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8affineqb $0,%zmm31,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 29)
			goto store;

		/*
		 * No more registers are available for resident coefficient
		 * tables. Process D29 and later with the normal loop.
		 */
		for (d = 29; d < nd; ++d) {
			asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[d][i]));
			asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
			asm volatile ("vpbroadcastq %0,%%zmm3" : : "m" (raid_gfcauchyaffine_raid[d][0][0]));
			asm volatile ("vgf2p8affineqb $0,%zmm3,%zmm2,%zmm3");
			asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		}

store:
		if (streaming) {
			asm volatile ("vmovntdq %%zmm0,%0" : "=m" (v[nd][i]));
			asm volatile ("vmovntdq %%zmm1,%0" : "=m" (v[nd + 1][i]));
		} else {
			asm volatile ("vmovdqa64 %%zmm0,%0" : "=m" (v[nd][i]));
			asm volatile ("vmovdqa64 %%zmm1,%0" : "=m" (v[nd + 1][i]));
		}
	}

	raid_avx_end(streaming);
}

/*
 * Generate three parity blocks with Cauchy matrix using AVX512 GFNI implementation.
 *
 * Preloads up to 13 pairs of Q and R affine matrix coefficients in ZMM
 * registers (zmm6..zmm31) with an unrolled disk loop and early exits.
 */
static __always_inline void raid_gen3_avx512gfni_raid_gen(int nd, size_t size, void **vv, int streaming)
{
	uint8_t **v = (uint8_t **)vv;
	size_t i;
	int d;

	if (nd == 1) {
		if (v[1] != v[0])
			memcpy(v[1], v[0], size);
		if (v[2] != v[0])
			memcpy(v[2], v[0], size);
		if (v[3] != v[0])
			memcpy(v[3], v[0], size);
		return;
	}

	raid_avx_begin();

	/* preload as many Q and R coefficient matrices as possible */
	asm volatile ("vpbroadcastq %0,%%zmm6" : : "m" (raid_gfcauchyaffine_raid[1][0][0]));
	asm volatile ("vpbroadcastq %0,%%zmm7" : : "m" (raid_gfcauchyaffine_raid[1][1][0]));
	asm volatile ("vpbroadcastq %0,%%zmm8" : : "m" (raid_gfcauchyaffine_raid[2][0][0]));
	asm volatile ("vpbroadcastq %0,%%zmm9" : : "m" (raid_gfcauchyaffine_raid[2][1][0]));
	asm volatile ("vpbroadcastq %0,%%zmm10" : : "m" (raid_gfcauchyaffine_raid[3][0][0]));
	asm volatile ("vpbroadcastq %0,%%zmm11" : : "m" (raid_gfcauchyaffine_raid[3][1][0]));
	asm volatile ("vpbroadcastq %0,%%zmm12" : : "m" (raid_gfcauchyaffine_raid[4][0][0]));
	asm volatile ("vpbroadcastq %0,%%zmm13" : : "m" (raid_gfcauchyaffine_raid[4][1][0]));
	asm volatile ("vpbroadcastq %0,%%zmm14" : : "m" (raid_gfcauchyaffine_raid[5][0][0]));
	asm volatile ("vpbroadcastq %0,%%zmm15" : : "m" (raid_gfcauchyaffine_raid[5][1][0]));
	asm volatile ("vpbroadcastq %0,%%zmm16" : : "m" (raid_gfcauchyaffine_raid[6][0][0]));
	asm volatile ("vpbroadcastq %0,%%zmm17" : : "m" (raid_gfcauchyaffine_raid[6][1][0]));
	asm volatile ("vpbroadcastq %0,%%zmm18" : : "m" (raid_gfcauchyaffine_raid[7][0][0]));
	asm volatile ("vpbroadcastq %0,%%zmm19" : : "m" (raid_gfcauchyaffine_raid[7][1][0]));
	asm volatile ("vpbroadcastq %0,%%zmm20" : : "m" (raid_gfcauchyaffine_raid[8][0][0]));
	asm volatile ("vpbroadcastq %0,%%zmm21" : : "m" (raid_gfcauchyaffine_raid[8][1][0]));
	asm volatile ("vpbroadcastq %0,%%zmm22" : : "m" (raid_gfcauchyaffine_raid[9][0][0]));
	asm volatile ("vpbroadcastq %0,%%zmm23" : : "m" (raid_gfcauchyaffine_raid[9][1][0]));
	asm volatile ("vpbroadcastq %0,%%zmm24" : : "m" (raid_gfcauchyaffine_raid[10][0][0]));
	asm volatile ("vpbroadcastq %0,%%zmm25" : : "m" (raid_gfcauchyaffine_raid[10][1][0]));
	asm volatile ("vpbroadcastq %0,%%zmm26" : : "m" (raid_gfcauchyaffine_raid[11][0][0]));
	asm volatile ("vpbroadcastq %0,%%zmm27" : : "m" (raid_gfcauchyaffine_raid[11][1][0]));
	asm volatile ("vpbroadcastq %0,%%zmm28" : : "m" (raid_gfcauchyaffine_raid[12][0][0]));
	asm volatile ("vpbroadcastq %0,%%zmm29" : : "m" (raid_gfcauchyaffine_raid[12][1][0]));
	asm volatile ("vpbroadcastq %0,%%zmm30" : : "m" (raid_gfcauchyaffine_raid[13][0][0]));
	asm volatile ("vpbroadcastq %0,%%zmm31" : : "m" (raid_gfcauchyaffine_raid[13][1][0]));

	for (i = 0; i < size; i += 64) {
		asm volatile ("vmovdqa64 %0,%%zmm0" : : "m" (v[0][i]));
		asm volatile ("vmovdqa64 %zmm0,%zmm1");
		asm volatile ("vmovdqa64 %zmm0,%zmm2");

		asm volatile ("vmovdqa64 %0,%%zmm3" : : "m" (v[1][i]));
		asm volatile ("vpxorq %zmm3,%zmm0,%zmm0");
		asm volatile ("vgf2p8affineqb $0,%zmm6,%zmm3,%zmm4");
		asm volatile ("vpxorq %zmm4,%zmm1,%zmm1");
		asm volatile ("vgf2p8affineqb $0,%zmm7,%zmm3,%zmm5");
		asm volatile ("vpxorq %zmm5,%zmm2,%zmm2");
		if (nd == 2)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm3" : : "m" (v[2][i]));
		asm volatile ("vpxorq %zmm3,%zmm0,%zmm0");
		asm volatile ("vgf2p8affineqb $0,%zmm8,%zmm3,%zmm4");
		asm volatile ("vpxorq %zmm4,%zmm1,%zmm1");
		asm volatile ("vgf2p8affineqb $0,%zmm9,%zmm3,%zmm5");
		asm volatile ("vpxorq %zmm5,%zmm2,%zmm2");
		if (nd == 3)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm3" : : "m" (v[3][i]));
		asm volatile ("vpxorq %zmm3,%zmm0,%zmm0");
		asm volatile ("vgf2p8affineqb $0,%zmm10,%zmm3,%zmm4");
		asm volatile ("vpxorq %zmm4,%zmm1,%zmm1");
		asm volatile ("vgf2p8affineqb $0,%zmm11,%zmm3,%zmm5");
		asm volatile ("vpxorq %zmm5,%zmm2,%zmm2");
		if (nd == 4)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm3" : : "m" (v[4][i]));
		asm volatile ("vpxorq %zmm3,%zmm0,%zmm0");
		asm volatile ("vgf2p8affineqb $0,%zmm12,%zmm3,%zmm4");
		asm volatile ("vpxorq %zmm4,%zmm1,%zmm1");
		asm volatile ("vgf2p8affineqb $0,%zmm13,%zmm3,%zmm5");
		asm volatile ("vpxorq %zmm5,%zmm2,%zmm2");
		if (nd == 5)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm3" : : "m" (v[5][i]));
		asm volatile ("vpxorq %zmm3,%zmm0,%zmm0");
		asm volatile ("vgf2p8affineqb $0,%zmm14,%zmm3,%zmm4");
		asm volatile ("vpxorq %zmm4,%zmm1,%zmm1");
		asm volatile ("vgf2p8affineqb $0,%zmm15,%zmm3,%zmm5");
		asm volatile ("vpxorq %zmm5,%zmm2,%zmm2");
		if (nd == 6)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm3" : : "m" (v[6][i]));
		asm volatile ("vpxorq %zmm3,%zmm0,%zmm0");
		asm volatile ("vgf2p8affineqb $0,%zmm16,%zmm3,%zmm4");
		asm volatile ("vpxorq %zmm4,%zmm1,%zmm1");
		asm volatile ("vgf2p8affineqb $0,%zmm17,%zmm3,%zmm5");
		asm volatile ("vpxorq %zmm5,%zmm2,%zmm2");
		if (nd == 7)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm3" : : "m" (v[7][i]));
		asm volatile ("vpxorq %zmm3,%zmm0,%zmm0");
		asm volatile ("vgf2p8affineqb $0,%zmm18,%zmm3,%zmm4");
		asm volatile ("vpxorq %zmm4,%zmm1,%zmm1");
		asm volatile ("vgf2p8affineqb $0,%zmm19,%zmm3,%zmm5");
		asm volatile ("vpxorq %zmm5,%zmm2,%zmm2");
		if (nd == 8)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm3" : : "m" (v[8][i]));
		asm volatile ("vpxorq %zmm3,%zmm0,%zmm0");
		asm volatile ("vgf2p8affineqb $0,%zmm20,%zmm3,%zmm4");
		asm volatile ("vpxorq %zmm4,%zmm1,%zmm1");
		asm volatile ("vgf2p8affineqb $0,%zmm21,%zmm3,%zmm5");
		asm volatile ("vpxorq %zmm5,%zmm2,%zmm2");
		if (nd == 9)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm3" : : "m" (v[9][i]));
		asm volatile ("vpxorq %zmm3,%zmm0,%zmm0");
		asm volatile ("vgf2p8affineqb $0,%zmm22,%zmm3,%zmm4");
		asm volatile ("vpxorq %zmm4,%zmm1,%zmm1");
		asm volatile ("vgf2p8affineqb $0,%zmm23,%zmm3,%zmm5");
		asm volatile ("vpxorq %zmm5,%zmm2,%zmm2");
		if (nd == 10)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm3" : : "m" (v[10][i]));
		asm volatile ("vpxorq %zmm3,%zmm0,%zmm0");
		asm volatile ("vgf2p8affineqb $0,%zmm24,%zmm3,%zmm4");
		asm volatile ("vpxorq %zmm4,%zmm1,%zmm1");
		asm volatile ("vgf2p8affineqb $0,%zmm25,%zmm3,%zmm5");
		asm volatile ("vpxorq %zmm5,%zmm2,%zmm2");
		if (nd == 11)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm3" : : "m" (v[11][i]));
		asm volatile ("vpxorq %zmm3,%zmm0,%zmm0");
		asm volatile ("vgf2p8affineqb $0,%zmm26,%zmm3,%zmm4");
		asm volatile ("vpxorq %zmm4,%zmm1,%zmm1");
		asm volatile ("vgf2p8affineqb $0,%zmm27,%zmm3,%zmm5");
		asm volatile ("vpxorq %zmm5,%zmm2,%zmm2");
		if (nd == 12)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm3" : : "m" (v[12][i]));
		asm volatile ("vpxorq %zmm3,%zmm0,%zmm0");
		asm volatile ("vgf2p8affineqb $0,%zmm28,%zmm3,%zmm4");
		asm volatile ("vpxorq %zmm4,%zmm1,%zmm1");
		asm volatile ("vgf2p8affineqb $0,%zmm29,%zmm3,%zmm5");
		asm volatile ("vpxorq %zmm5,%zmm2,%zmm2");
		if (nd == 13)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm3" : : "m" (v[13][i]));
		asm volatile ("vpxorq %zmm3,%zmm0,%zmm0");
		asm volatile ("vgf2p8affineqb $0,%zmm30,%zmm3,%zmm4");
		asm volatile ("vpxorq %zmm4,%zmm1,%zmm1");
		asm volatile ("vgf2p8affineqb $0,%zmm31,%zmm3,%zmm5");
		asm volatile ("vpxorq %zmm5,%zmm2,%zmm2");
		if (nd == 14)
			goto store;

		/*
		 * No more registers are available for resident coefficient
		 * tables. Process D14 and later with the normal loop.
		 */
		for (d = 14; d < nd; ++d) {
			asm volatile ("vmovdqa64 %0,%%zmm3" : : "m" (v[d][i]));
			asm volatile ("vpxorq %zmm3,%zmm0,%zmm0");

			asm volatile ("vpbroadcastq %0,%%zmm4" : : "m" (raid_gfcauchyaffine_raid[d][0][0]));
			asm volatile ("vgf2p8affineqb $0,%zmm4,%zmm3,%zmm4");
			asm volatile ("vpxorq %zmm4,%zmm1,%zmm1");

			asm volatile ("vpbroadcastq %0,%%zmm5" : : "m" (raid_gfcauchyaffine_raid[d][1][0]));
			asm volatile ("vgf2p8affineqb $0,%zmm5,%zmm3,%zmm5");
			asm volatile ("vpxorq %zmm5,%zmm2,%zmm2");
		}

store:
		if (streaming) {
			asm volatile ("vmovntdq %%zmm0,%0" : "=m" (v[nd][i]));
			asm volatile ("vmovntdq %%zmm1,%0" : "=m" (v[nd + 1][i]));
			asm volatile ("vmovntdq %%zmm2,%0" : "=m" (v[nd + 2][i]));
		} else {
			asm volatile ("vmovdqa64 %%zmm0,%0" : "=m" (v[nd][i]));
			asm volatile ("vmovdqa64 %%zmm1,%0" : "=m" (v[nd + 1][i]));
			asm volatile ("vmovdqa64 %%zmm2,%0" : "=m" (v[nd + 2][i]));
		}
	}

	raid_avx_end(streaming);
}

/*
 * Generate N parity blocks for the RAID polynomial using direct Cauchy coefficients with AVX512 GFNI.
 */
static __always_inline void raid_genX_avx512gfni_raid(int nd, size_t size, void **vv, int np, int streaming)
{
	uint8_t **v = (uint8_t **)vv;
	size_t i;
	int d;

	if (nd == 1) {
		for (d = 0; d < np; ++d)
			if (v[1 + d] != v[0])
				memcpy(v[1 + d], v[0], size);
		return;
	}

	raid_avx_begin();

	for (i = 0; i < size; i += 64) {
		/* first disk with all coefficients at 1 */
		asm volatile ("vmovdqa64 %0,%%zmm0" : : "m" (v[0][i]));

		asm volatile ("vmovdqa64 %zmm0,%zmm1");
		if (np >= 3)
			asm volatile ("vmovdqa64 %zmm0,%zmm2");
		if (np >= 4)
			asm volatile ("vmovdqa64 %zmm0,%zmm3");
		if (np >= 5)
			asm volatile ("vmovdqa64 %zmm0,%zmm4");
		if (np >= 6)
			asm volatile ("vmovdqa64 %zmm0,%zmm5");

		/* all other disks use their direct Cauchy coefficients */
		for (d = 1; d < nd; ++d) {
			asm volatile ("vmovdqa64 %0,%%zmm6" : : "m" (v[d][i]));

			asm volatile ("vpbroadcastq %0,%%zmm7" : : "m" (raid_gfcauchyaffine_raid[d][0][0]));
			if (np >= 3)
				asm volatile ("vpbroadcastq %0,%%zmm8" : : "m" (raid_gfcauchyaffine_raid[d][1][0]));
			if (np >= 4)
				asm volatile ("vpbroadcastq %0,%%zmm9" : : "m" (raid_gfcauchyaffine_raid[d][2][0]));
			if (np >= 5)
				asm volatile ("vpbroadcastq %0,%%zmm10" : : "m" (raid_gfcauchyaffine_raid[d][3][0]));
			if (np >= 6)
				asm volatile ("vpbroadcastq %0,%%zmm11" : : "m" (raid_gfcauchyaffine_raid[d][4][0]));

			asm volatile ("vgf2p8affineqb $0,%zmm7,%zmm6,%zmm12");
			if (np >= 3)
				asm volatile ("vgf2p8affineqb $0,%zmm8,%zmm6,%zmm13");
			if (np >= 4)
				asm volatile ("vgf2p8affineqb $0,%zmm9,%zmm6,%zmm14");
			if (np >= 5)
				asm volatile ("vgf2p8affineqb $0,%zmm10,%zmm6,%zmm15");
			if (np >= 6)
				asm volatile ("vgf2p8affineqb $0,%zmm11,%zmm6,%zmm16");

			asm volatile ("vpxorq %zmm6,%zmm0,%zmm0");
			asm volatile ("vpxorq %zmm12,%zmm1,%zmm1");
			if (np >= 3)
				asm volatile ("vpxorq %zmm13,%zmm2,%zmm2");
			if (np >= 4)
				asm volatile ("vpxorq %zmm14,%zmm3,%zmm3");
			if (np >= 5)
				asm volatile ("vpxorq %zmm15,%zmm4,%zmm4");
			if (np >= 6)
				asm volatile ("vpxorq %zmm16,%zmm5,%zmm5");
		}

		if (streaming) {
			asm volatile ("vmovntdq %%zmm0,%0" : "=m" (v[nd][i]));
			asm volatile ("vmovntdq %%zmm1,%0" : "=m" (v[nd + 1][i]));
			if (np >= 3)
				asm volatile ("vmovntdq %%zmm2,%0" : "=m" (v[nd + 2][i]));
			if (np >= 4)
				asm volatile ("vmovntdq %%zmm3,%0" : "=m" (v[nd + 3][i]));
			if (np >= 5)
				asm volatile ("vmovntdq %%zmm4,%0" : "=m" (v[nd + 4][i]));
			if (np >= 6)
				asm volatile ("vmovntdq %%zmm5,%0" : "=m" (v[nd + 5][i]));
		} else {
			asm volatile ("vmovdqa64 %%zmm0,%0" : "=m" (v[nd][i]));
			asm volatile ("vmovdqa64 %%zmm1,%0" : "=m" (v[nd + 1][i]));
			if (np >= 3)
				asm volatile ("vmovdqa64 %%zmm2,%0" : "=m" (v[nd + 2][i]));
			if (np >= 4)
				asm volatile ("vmovdqa64 %%zmm3,%0" : "=m" (v[nd + 3][i]));
			if (np >= 5)
				asm volatile ("vmovdqa64 %%zmm4,%0" : "=m" (v[nd + 4][i]));
			if (np >= 6)
				asm volatile ("vmovdqa64 %%zmm5,%0" : "=m" (v[nd + 5][i]));
		}
	}

	raid_avx_end(streaming);
}

/*
 * Generate N parity blocks for the AES polynomial using direct Cauchy coefficients with AVX2 GFNI.
 */
static __always_inline void raid_genX_avx2gfni_aes(int nd, size_t size, void **vv, int np, int streaming)
{
	uint8_t **v = (uint8_t **)vv;
	size_t i;
	int d;

	if (nd == 1) {
		for (d = 0; d < np; ++d)
			if (v[1 + d] != v[0])
				memcpy(v[1 + d], v[0], size);
		return;
	}

	raid_avx_begin();

	for (i = 0; i < size; i += 64) {
		/* first disk with all coefficients at 1 */
		asm volatile ("vmovdqa %0,%%ymm0" : : "m" (v[0][i]));
		asm volatile ("vmovdqa %0,%%ymm1" : : "m" (v[0][i + 32]));

		asm volatile ("vmovdqa %ymm0,%ymm2");
		asm volatile ("vmovdqa %ymm1,%ymm3");
		if (np >= 3) {
			asm volatile ("vmovdqa %ymm0,%ymm4");
			asm volatile ("vmovdqa %ymm1,%ymm5");
		}
		if (np >= 4) {
			asm volatile ("vmovdqa %ymm0,%ymm6");
			asm volatile ("vmovdqa %ymm1,%ymm7");
		}
		if (np >= 5) {
			asm volatile ("vmovdqa %ymm0,%ymm8");
			asm volatile ("vmovdqa %ymm1,%ymm9");
		}
		if (np >= 6) {
			asm volatile ("vmovdqa %ymm0,%ymm10");
			asm volatile ("vmovdqa %ymm1,%ymm11");
		}

		/* all other disks use their direct Cauchy coefficients */
		for (d = 1; d < nd; ++d) {
			asm volatile ("vmovdqa %0,%%ymm12" : : "m" (v[d][i]));
			asm volatile ("vmovdqa %0,%%ymm13" : : "m" (v[d][i + 32]));

			asm volatile ("vpxor %ymm12,%ymm0,%ymm0");
			asm volatile ("vpxor %ymm13,%ymm1,%ymm1");

			asm volatile ("vpbroadcastb %0,%%ymm14" : : "m" (raid_gfcauchy_aes[1][d]));
			asm volatile ("vgf2p8mulb %ymm12,%ymm14,%ymm15");
			asm volatile ("vpxor %ymm15,%ymm2,%ymm2");
			asm volatile ("vgf2p8mulb %ymm13,%ymm14,%ymm15");
			asm volatile ("vpxor %ymm15,%ymm3,%ymm3");

			if (np >= 3) {
				asm volatile ("vpbroadcastb %0,%%ymm14" : : "m" (raid_gfcauchy_aes[2][d]));
				asm volatile ("vgf2p8mulb %ymm12,%ymm14,%ymm15");
				asm volatile ("vpxor %ymm15,%ymm4,%ymm4");
				asm volatile ("vgf2p8mulb %ymm13,%ymm14,%ymm15");
				asm volatile ("vpxor %ymm15,%ymm5,%ymm5");
			}

			if (np >= 4) {
				asm volatile ("vpbroadcastb %0,%%ymm14" : : "m" (raid_gfcauchy_aes[3][d]));
				asm volatile ("vgf2p8mulb %ymm12,%ymm14,%ymm15");
				asm volatile ("vpxor %ymm15,%ymm6,%ymm6");
				asm volatile ("vgf2p8mulb %ymm13,%ymm14,%ymm15");
				asm volatile ("vpxor %ymm15,%ymm7,%ymm7");
			}

			if (np >= 5) {
				asm volatile ("vpbroadcastb %0,%%ymm14" : : "m" (raid_gfcauchy_aes[4][d]));
				asm volatile ("vgf2p8mulb %ymm12,%ymm14,%ymm15");
				asm volatile ("vpxor %ymm15,%ymm8,%ymm8");
				asm volatile ("vgf2p8mulb %ymm13,%ymm14,%ymm15");
				asm volatile ("vpxor %ymm15,%ymm9,%ymm9");
			}

			if (np >= 6) {
				asm volatile ("vpbroadcastb %0,%%ymm14" : : "m" (raid_gfcauchy_aes[5][d]));
				asm volatile ("vgf2p8mulb %ymm12,%ymm14,%ymm15");
				asm volatile ("vpxor %ymm15,%ymm10,%ymm10");
				asm volatile ("vgf2p8mulb %ymm13,%ymm14,%ymm15");
				asm volatile ("vpxor %ymm15,%ymm11,%ymm11");
			}
		}

		if (streaming) {
			asm volatile ("vmovntdq %%ymm0,%0" : "=m" (v[nd][i]));
			asm volatile ("vmovntdq %%ymm1,%0" : "=m" (v[nd][i + 32]));
			asm volatile ("vmovntdq %%ymm2,%0" : "=m" (v[nd + 1][i]));
			asm volatile ("vmovntdq %%ymm3,%0" : "=m" (v[nd + 1][i + 32]));

			if (np >= 3) {
				asm volatile ("vmovntdq %%ymm4,%0" : "=m" (v[nd + 2][i]));
				asm volatile ("vmovntdq %%ymm5,%0" : "=m" (v[nd + 2][i + 32]));
			}

			if (np >= 4) {
				asm volatile ("vmovntdq %%ymm6,%0" : "=m" (v[nd + 3][i]));
				asm volatile ("vmovntdq %%ymm7,%0" : "=m" (v[nd + 3][i + 32]));
			}

			if (np >= 5) {
				asm volatile ("vmovntdq %%ymm8,%0" : "=m" (v[nd + 4][i]));
				asm volatile ("vmovntdq %%ymm9,%0" : "=m" (v[nd + 4][i + 32]));
			}

			if (np >= 6) {
				asm volatile ("vmovntdq %%ymm10,%0" : "=m" (v[nd + 5][i]));
				asm volatile ("vmovntdq %%ymm11,%0" : "=m" (v[nd + 5][i + 32]));
			}
		} else {
			asm volatile ("vmovdqa %%ymm0,%0" : "=m" (v[nd][i]));
			asm volatile ("vmovdqa %%ymm1,%0" : "=m" (v[nd][i + 32]));
			asm volatile ("vmovdqa %%ymm2,%0" : "=m" (v[nd + 1][i]));
			asm volatile ("vmovdqa %%ymm3,%0" : "=m" (v[nd + 1][i + 32]));

			if (np >= 3) {
				asm volatile ("vmovdqa %%ymm4,%0" : "=m" (v[nd + 2][i]));
				asm volatile ("vmovdqa %%ymm5,%0" : "=m" (v[nd + 2][i + 32]));
			}

			if (np >= 4) {
				asm volatile ("vmovdqa %%ymm6,%0" : "=m" (v[nd + 3][i]));
				asm volatile ("vmovdqa %%ymm7,%0" : "=m" (v[nd + 3][i + 32]));
			}

			if (np >= 5) {
				asm volatile ("vmovdqa %%ymm8,%0" : "=m" (v[nd + 4][i]));
				asm volatile ("vmovdqa %%ymm9,%0" : "=m" (v[nd + 4][i + 32]));
			}

			if (np >= 6) {
				asm volatile ("vmovdqa %%ymm10,%0" : "=m" (v[nd + 5][i]));
				asm volatile ("vmovdqa %%ymm11,%0" : "=m" (v[nd + 5][i + 32]));
			}
		}
	}

	raid_avx_end(streaming);
}

/*
 * Generate two parity blocks (RAID6 with powers of 3) using AVX512 GFNI implementation.
 *
 * Preloads up to 28 Q multiplier coefficients in ZMM registers (zmm4..zmm31)
 * with an unrolled disk loop and early exits.
 */
static __always_inline void raid_gen2_avx512gfni_aes_gen(int nd, size_t size, void **vv, int streaming)
{
	uint8_t **v = (uint8_t **)vv;
	size_t i;
	int d;

	if (nd == 1) {
		if (v[1] != v[0])
			memcpy(v[1], v[0], size);
		if (v[2] != v[0])
			memcpy(v[2], v[0], size);
		return;
	}

	raid_avx_begin();

	/* preload as many Q coefficient multipliers as possible */
	asm volatile ("vpbroadcastb %0,%%zmm4" : : "m" (raid_gfcauchy_aes[1][1]));
	asm volatile ("vpbroadcastb %0,%%zmm5" : : "m" (raid_gfcauchy_aes[1][2]));
	asm volatile ("vpbroadcastb %0,%%zmm6" : : "m" (raid_gfcauchy_aes[1][3]));
	asm volatile ("vpbroadcastb %0,%%zmm7" : : "m" (raid_gfcauchy_aes[1][4]));
	asm volatile ("vpbroadcastb %0,%%zmm8" : : "m" (raid_gfcauchy_aes[1][5]));
	asm volatile ("vpbroadcastb %0,%%zmm9" : : "m" (raid_gfcauchy_aes[1][6]));
	asm volatile ("vpbroadcastb %0,%%zmm10" : : "m" (raid_gfcauchy_aes[1][7]));
	asm volatile ("vpbroadcastb %0,%%zmm11" : : "m" (raid_gfcauchy_aes[1][8]));
	asm volatile ("vpbroadcastb %0,%%zmm12" : : "m" (raid_gfcauchy_aes[1][9]));
	asm volatile ("vpbroadcastb %0,%%zmm13" : : "m" (raid_gfcauchy_aes[1][10]));
	asm volatile ("vpbroadcastb %0,%%zmm14" : : "m" (raid_gfcauchy_aes[1][11]));
	asm volatile ("vpbroadcastb %0,%%zmm15" : : "m" (raid_gfcauchy_aes[1][12]));
	asm volatile ("vpbroadcastb %0,%%zmm16" : : "m" (raid_gfcauchy_aes[1][13]));
	asm volatile ("vpbroadcastb %0,%%zmm17" : : "m" (raid_gfcauchy_aes[1][14]));
	asm volatile ("vpbroadcastb %0,%%zmm18" : : "m" (raid_gfcauchy_aes[1][15]));
	asm volatile ("vpbroadcastb %0,%%zmm19" : : "m" (raid_gfcauchy_aes[1][16]));
	asm volatile ("vpbroadcastb %0,%%zmm20" : : "m" (raid_gfcauchy_aes[1][17]));
	asm volatile ("vpbroadcastb %0,%%zmm21" : : "m" (raid_gfcauchy_aes[1][18]));
	asm volatile ("vpbroadcastb %0,%%zmm22" : : "m" (raid_gfcauchy_aes[1][19]));
	asm volatile ("vpbroadcastb %0,%%zmm23" : : "m" (raid_gfcauchy_aes[1][20]));
	asm volatile ("vpbroadcastb %0,%%zmm24" : : "m" (raid_gfcauchy_aes[1][21]));
	asm volatile ("vpbroadcastb %0,%%zmm25" : : "m" (raid_gfcauchy_aes[1][22]));
	asm volatile ("vpbroadcastb %0,%%zmm26" : : "m" (raid_gfcauchy_aes[1][23]));
	asm volatile ("vpbroadcastb %0,%%zmm27" : : "m" (raid_gfcauchy_aes[1][24]));
	asm volatile ("vpbroadcastb %0,%%zmm28" : : "m" (raid_gfcauchy_aes[1][25]));
	asm volatile ("vpbroadcastb %0,%%zmm29" : : "m" (raid_gfcauchy_aes[1][26]));
	asm volatile ("vpbroadcastb %0,%%zmm30" : : "m" (raid_gfcauchy_aes[1][27]));
	asm volatile ("vpbroadcastb %0,%%zmm31" : : "m" (raid_gfcauchy_aes[1][28]));

	for (i = 0; i < size; i += 64) {
		asm volatile ("vmovdqa64 %0,%%zmm0" : : "m" (v[0][i]));
		asm volatile ("vmovdqa64 %zmm0,%zmm1");

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[1][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8mulb %zmm4,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 2)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[2][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8mulb %zmm5,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 3)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[3][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8mulb %zmm6,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 4)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[4][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8mulb %zmm7,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 5)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[5][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8mulb %zmm8,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 6)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[6][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8mulb %zmm9,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 7)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[7][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8mulb %zmm10,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 8)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[8][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8mulb %zmm11,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 9)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[9][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8mulb %zmm12,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 10)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[10][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8mulb %zmm13,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 11)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[11][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8mulb %zmm14,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 12)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[12][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8mulb %zmm15,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 13)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[13][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8mulb %zmm16,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 14)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[14][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8mulb %zmm17,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 15)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[15][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8mulb %zmm18,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 16)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[16][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8mulb %zmm19,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 17)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[17][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8mulb %zmm20,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 18)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[18][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8mulb %zmm21,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 19)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[19][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8mulb %zmm22,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 20)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[20][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8mulb %zmm23,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 21)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[21][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8mulb %zmm24,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 22)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[22][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8mulb %zmm25,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 23)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[23][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8mulb %zmm26,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 24)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[24][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8mulb %zmm27,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 25)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[25][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8mulb %zmm28,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 26)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[26][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8mulb %zmm29,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 27)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[27][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8mulb %zmm30,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 28)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[28][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vgf2p8mulb %zmm31,%zmm2,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		if (nd == 29)
			goto store;

		/*
		 * No more registers are available for resident coefficient
		 * tables. Process D29 and later with the normal loop.
		 */
		for (d = 29; d < nd; ++d) {
			asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[d][i]));
			asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
			asm volatile ("vpbroadcastb %0,%%zmm3" : : "m" (raid_gfcauchy_aes[1][d]));
			asm volatile ("vgf2p8mulb %zmm3,%zmm2,%zmm3");
			asm volatile ("vpxorq %zmm3,%zmm1,%zmm1");
		}

store:
		if (streaming) {
			asm volatile ("vmovntdq %%zmm0,%0" : "=m" (v[nd][i]));
			asm volatile ("vmovntdq %%zmm1,%0" : "=m" (v[nd + 1][i]));
		} else {
			asm volatile ("vmovdqa64 %%zmm0,%0" : "=m" (v[nd][i]));
			asm volatile ("vmovdqa64 %%zmm1,%0" : "=m" (v[nd + 1][i]));
		}
	}

	raid_avx_end(streaming);
}

/*
 * Generate three parity blocks with Cauchy matrix using AVX512 GFNI implementation.
 *
 * Preloads up to 13 pairs of Q and R multiplier coefficients in ZMM
 * registers (zmm6..zmm31) with an unrolled disk loop and early exits.
 */
static __always_inline void raid_gen3_avx512gfni_aes_gen(int nd, size_t size, void **vv, int streaming)
{
	uint8_t **v = (uint8_t **)vv;
	size_t i;
	int d;

	if (nd == 1) {
		if (v[1] != v[0])
			memcpy(v[1], v[0], size);
		if (v[2] != v[0])
			memcpy(v[2], v[0], size);
		if (v[3] != v[0])
			memcpy(v[3], v[0], size);
		return;
	}

	raid_avx_begin();

	/* preload as many Q and R coefficient multipliers as possible */
	asm volatile ("vpbroadcastb %0,%%zmm6" : : "m" (raid_gfcauchy_aes[1][1]));
	asm volatile ("vpbroadcastb %0,%%zmm7" : : "m" (raid_gfcauchy_aes[2][1]));
	asm volatile ("vpbroadcastb %0,%%zmm8" : : "m" (raid_gfcauchy_aes[1][2]));
	asm volatile ("vpbroadcastb %0,%%zmm9" : : "m" (raid_gfcauchy_aes[2][2]));
	asm volatile ("vpbroadcastb %0,%%zmm10" : : "m" (raid_gfcauchy_aes[1][3]));
	asm volatile ("vpbroadcastb %0,%%zmm11" : : "m" (raid_gfcauchy_aes[2][3]));
	asm volatile ("vpbroadcastb %0,%%zmm12" : : "m" (raid_gfcauchy_aes[1][4]));
	asm volatile ("vpbroadcastb %0,%%zmm13" : : "m" (raid_gfcauchy_aes[2][4]));
	asm volatile ("vpbroadcastb %0,%%zmm14" : : "m" (raid_gfcauchy_aes[1][5]));
	asm volatile ("vpbroadcastb %0,%%zmm15" : : "m" (raid_gfcauchy_aes[2][5]));
	asm volatile ("vpbroadcastb %0,%%zmm16" : : "m" (raid_gfcauchy_aes[1][6]));
	asm volatile ("vpbroadcastb %0,%%zmm17" : : "m" (raid_gfcauchy_aes[2][6]));
	asm volatile ("vpbroadcastb %0,%%zmm18" : : "m" (raid_gfcauchy_aes[1][7]));
	asm volatile ("vpbroadcastb %0,%%zmm19" : : "m" (raid_gfcauchy_aes[2][7]));
	asm volatile ("vpbroadcastb %0,%%zmm20" : : "m" (raid_gfcauchy_aes[1][8]));
	asm volatile ("vpbroadcastb %0,%%zmm21" : : "m" (raid_gfcauchy_aes[2][8]));
	asm volatile ("vpbroadcastb %0,%%zmm22" : : "m" (raid_gfcauchy_aes[1][9]));
	asm volatile ("vpbroadcastb %0,%%zmm23" : : "m" (raid_gfcauchy_aes[2][9]));
	asm volatile ("vpbroadcastb %0,%%zmm24" : : "m" (raid_gfcauchy_aes[1][10]));
	asm volatile ("vpbroadcastb %0,%%zmm25" : : "m" (raid_gfcauchy_aes[2][10]));
	asm volatile ("vpbroadcastb %0,%%zmm26" : : "m" (raid_gfcauchy_aes[1][11]));
	asm volatile ("vpbroadcastb %0,%%zmm27" : : "m" (raid_gfcauchy_aes[2][11]));
	asm volatile ("vpbroadcastb %0,%%zmm28" : : "m" (raid_gfcauchy_aes[1][12]));
	asm volatile ("vpbroadcastb %0,%%zmm29" : : "m" (raid_gfcauchy_aes[2][12]));
	asm volatile ("vpbroadcastb %0,%%zmm30" : : "m" (raid_gfcauchy_aes[1][13]));
	asm volatile ("vpbroadcastb %0,%%zmm31" : : "m" (raid_gfcauchy_aes[2][13]));

	for (i = 0; i < size; i += 64) {
		asm volatile ("vmovdqa64 %0,%%zmm0" : : "m" (v[0][i]));
		asm volatile ("vmovdqa64 %zmm0,%zmm1");
		asm volatile ("vmovdqa64 %zmm0,%zmm2");

		asm volatile ("vmovdqa64 %0,%%zmm3" : : "m" (v[1][i]));
		asm volatile ("vpxorq %zmm3,%zmm0,%zmm0");
		asm volatile ("vgf2p8mulb %zmm6,%zmm3,%zmm4");
		asm volatile ("vpxorq %zmm4,%zmm1,%zmm1");
		asm volatile ("vgf2p8mulb %zmm7,%zmm3,%zmm5");
		asm volatile ("vpxorq %zmm5,%zmm2,%zmm2");
		if (nd == 2)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm3" : : "m" (v[2][i]));
		asm volatile ("vpxorq %zmm3,%zmm0,%zmm0");
		asm volatile ("vgf2p8mulb %zmm8,%zmm3,%zmm4");
		asm volatile ("vpxorq %zmm4,%zmm1,%zmm1");
		asm volatile ("vgf2p8mulb %zmm9,%zmm3,%zmm5");
		asm volatile ("vpxorq %zmm5,%zmm2,%zmm2");
		if (nd == 3)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm3" : : "m" (v[3][i]));
		asm volatile ("vpxorq %zmm3,%zmm0,%zmm0");
		asm volatile ("vgf2p8mulb %zmm10,%zmm3,%zmm4");
		asm volatile ("vpxorq %zmm4,%zmm1,%zmm1");
		asm volatile ("vgf2p8mulb %zmm11,%zmm3,%zmm5");
		asm volatile ("vpxorq %zmm5,%zmm2,%zmm2");
		if (nd == 4)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm3" : : "m" (v[4][i]));
		asm volatile ("vpxorq %zmm3,%zmm0,%zmm0");
		asm volatile ("vgf2p8mulb %zmm12,%zmm3,%zmm4");
		asm volatile ("vpxorq %zmm4,%zmm1,%zmm1");
		asm volatile ("vgf2p8mulb %zmm13,%zmm3,%zmm5");
		asm volatile ("vpxorq %zmm5,%zmm2,%zmm2");
		if (nd == 5)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm3" : : "m" (v[5][i]));
		asm volatile ("vpxorq %zmm3,%zmm0,%zmm0");
		asm volatile ("vgf2p8mulb %zmm14,%zmm3,%zmm4");
		asm volatile ("vpxorq %zmm4,%zmm1,%zmm1");
		asm volatile ("vgf2p8mulb %zmm15,%zmm3,%zmm5");
		asm volatile ("vpxorq %zmm5,%zmm2,%zmm2");
		if (nd == 6)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm3" : : "m" (v[6][i]));
		asm volatile ("vpxorq %zmm3,%zmm0,%zmm0");
		asm volatile ("vgf2p8mulb %zmm16,%zmm3,%zmm4");
		asm volatile ("vpxorq %zmm4,%zmm1,%zmm1");
		asm volatile ("vgf2p8mulb %zmm17,%zmm3,%zmm5");
		asm volatile ("vpxorq %zmm5,%zmm2,%zmm2");
		if (nd == 7)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm3" : : "m" (v[7][i]));
		asm volatile ("vpxorq %zmm3,%zmm0,%zmm0");
		asm volatile ("vgf2p8mulb %zmm18,%zmm3,%zmm4");
		asm volatile ("vpxorq %zmm4,%zmm1,%zmm1");
		asm volatile ("vgf2p8mulb %zmm19,%zmm3,%zmm5");
		asm volatile ("vpxorq %zmm5,%zmm2,%zmm2");
		if (nd == 8)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm3" : : "m" (v[8][i]));
		asm volatile ("vpxorq %zmm3,%zmm0,%zmm0");
		asm volatile ("vgf2p8mulb %zmm20,%zmm3,%zmm4");
		asm volatile ("vpxorq %zmm4,%zmm1,%zmm1");
		asm volatile ("vgf2p8mulb %zmm21,%zmm3,%zmm5");
		asm volatile ("vpxorq %zmm5,%zmm2,%zmm2");
		if (nd == 9)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm3" : : "m" (v[9][i]));
		asm volatile ("vpxorq %zmm3,%zmm0,%zmm0");
		asm volatile ("vgf2p8mulb %zmm22,%zmm3,%zmm4");
		asm volatile ("vpxorq %zmm4,%zmm1,%zmm1");
		asm volatile ("vgf2p8mulb %zmm23,%zmm3,%zmm5");
		asm volatile ("vpxorq %zmm5,%zmm2,%zmm2");
		if (nd == 10)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm3" : : "m" (v[10][i]));
		asm volatile ("vpxorq %zmm3,%zmm0,%zmm0");
		asm volatile ("vgf2p8mulb %zmm24,%zmm3,%zmm4");
		asm volatile ("vpxorq %zmm4,%zmm1,%zmm1");
		asm volatile ("vgf2p8mulb %zmm25,%zmm3,%zmm5");
		asm volatile ("vpxorq %zmm5,%zmm2,%zmm2");
		if (nd == 11)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm3" : : "m" (v[11][i]));
		asm volatile ("vpxorq %zmm3,%zmm0,%zmm0");
		asm volatile ("vgf2p8mulb %zmm26,%zmm3,%zmm4");
		asm volatile ("vpxorq %zmm4,%zmm1,%zmm1");
		asm volatile ("vgf2p8mulb %zmm27,%zmm3,%zmm5");
		asm volatile ("vpxorq %zmm5,%zmm2,%zmm2");
		if (nd == 12)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm3" : : "m" (v[12][i]));
		asm volatile ("vpxorq %zmm3,%zmm0,%zmm0");
		asm volatile ("vgf2p8mulb %zmm28,%zmm3,%zmm4");
		asm volatile ("vpxorq %zmm4,%zmm1,%zmm1");
		asm volatile ("vgf2p8mulb %zmm29,%zmm3,%zmm5");
		asm volatile ("vpxorq %zmm5,%zmm2,%zmm2");
		if (nd == 13)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm3" : : "m" (v[13][i]));
		asm volatile ("vpxorq %zmm3,%zmm0,%zmm0");
		asm volatile ("vgf2p8mulb %zmm30,%zmm3,%zmm4");
		asm volatile ("vpxorq %zmm4,%zmm1,%zmm1");
		asm volatile ("vgf2p8mulb %zmm31,%zmm3,%zmm5");
		asm volatile ("vpxorq %zmm5,%zmm2,%zmm2");
		if (nd == 14)
			goto store;

		/*
		 * No more registers are available for resident coefficient
		 * tables. Process D14 and later with the normal loop.
		 */
		for (d = 14; d < nd; ++d) {
			asm volatile ("vmovdqa64 %0,%%zmm3" : : "m" (v[d][i]));
			asm volatile ("vpxorq %zmm3,%zmm0,%zmm0");

			asm volatile ("vpbroadcastb %0,%%zmm4" : : "m" (raid_gfcauchy_aes[1][d]));
			asm volatile ("vgf2p8mulb %zmm4,%zmm3,%zmm4");
			asm volatile ("vpxorq %zmm4,%zmm1,%zmm1");

			asm volatile ("vpbroadcastb %0,%%zmm5" : : "m" (raid_gfcauchy_aes[2][d]));
			asm volatile ("vgf2p8mulb %zmm5,%zmm3,%zmm5");
			asm volatile ("vpxorq %zmm5,%zmm2,%zmm2");
		}

store:
		if (streaming) {
			asm volatile ("vmovntdq %%zmm0,%0" : "=m" (v[nd][i]));
			asm volatile ("vmovntdq %%zmm1,%0" : "=m" (v[nd + 1][i]));
			asm volatile ("vmovntdq %%zmm2,%0" : "=m" (v[nd + 2][i]));
		} else {
			asm volatile ("vmovdqa64 %%zmm0,%0" : "=m" (v[nd][i]));
			asm volatile ("vmovdqa64 %%zmm1,%0" : "=m" (v[nd + 1][i]));
			asm volatile ("vmovdqa64 %%zmm2,%0" : "=m" (v[nd + 2][i]));
		}
	}

	raid_avx_end(streaming);
}

/*
 * Generate N parity blocks for the AES polynomial using direct Cauchy coefficients with AVX512 GFNI.
 */
static __always_inline void raid_genX_avx512gfni_aes(int nd, size_t size, void **vv, int np, int streaming)
{
	uint8_t **v = (uint8_t **)vv;
	size_t i;
	int d;

	if (nd == 1) {
		for (d = 0; d < np; ++d)
			if (v[1 + d] != v[0])
				memcpy(v[1 + d], v[0], size);
		return;
	}

	raid_avx_begin();

	for (i = 0; i < size; i += 64) {
		/* first disk with all coefficients at 1 */
		asm volatile ("vmovdqa64 %0,%%zmm0" : : "m" (v[0][i]));

		asm volatile ("vmovdqa64 %zmm0,%zmm1");
		if (np >= 3)
			asm volatile ("vmovdqa64 %zmm0,%zmm2");
		if (np >= 4)
			asm volatile ("vmovdqa64 %zmm0,%zmm3");
		if (np >= 5)
			asm volatile ("vmovdqa64 %zmm0,%zmm4");
		if (np >= 6)
			asm volatile ("vmovdqa64 %zmm0,%zmm5");

		/* all other disks use their direct Cauchy coefficients */
		for (d = 1; d < nd; ++d) {
			asm volatile ("vmovdqa64 %0,%%zmm6" : : "m" (v[d][i]));

			asm volatile ("vpbroadcastb %0,%%zmm7" : : "m" (raid_gfcauchy_aes[1][d]));
			if (np >= 3)
				asm volatile ("vpbroadcastb %0,%%zmm8" : : "m" (raid_gfcauchy_aes[2][d]));
			if (np >= 4)
				asm volatile ("vpbroadcastb %0,%%zmm9" : : "m" (raid_gfcauchy_aes[3][d]));
			if (np >= 5)
				asm volatile ("vpbroadcastb %0,%%zmm10" : : "m" (raid_gfcauchy_aes[4][d]));
			if (np >= 6)
				asm volatile ("vpbroadcastb %0,%%zmm11" : : "m" (raid_gfcauchy_aes[5][d]));

			asm volatile ("vgf2p8mulb %zmm6,%zmm7,%zmm12");
			if (np >= 3)
				asm volatile ("vgf2p8mulb %zmm6,%zmm8,%zmm13");
			if (np >= 4)
				asm volatile ("vgf2p8mulb %zmm6,%zmm9,%zmm14");
			if (np >= 5)
				asm volatile ("vgf2p8mulb %zmm6,%zmm10,%zmm15");
			if (np >= 6)
				asm volatile ("vgf2p8mulb %zmm6,%zmm11,%zmm16");

			asm volatile ("vpxorq %zmm6,%zmm0,%zmm0");
			asm volatile ("vpxorq %zmm12,%zmm1,%zmm1");
			if (np >= 3)
				asm volatile ("vpxorq %zmm13,%zmm2,%zmm2");
			if (np >= 4)
				asm volatile ("vpxorq %zmm14,%zmm3,%zmm3");
			if (np >= 5)
				asm volatile ("vpxorq %zmm15,%zmm4,%zmm4");
			if (np >= 6)
				asm volatile ("vpxorq %zmm16,%zmm5,%zmm5");
		}

		if (streaming) {
			asm volatile ("vmovntdq %%zmm0,%0" : "=m" (v[nd][i]));
			asm volatile ("vmovntdq %%zmm1,%0" : "=m" (v[nd + 1][i]));
			if (np >= 3)
				asm volatile ("vmovntdq %%zmm2,%0" : "=m" (v[nd + 2][i]));
			if (np >= 4)
				asm volatile ("vmovntdq %%zmm3,%0" : "=m" (v[nd + 3][i]));
			if (np >= 5)
				asm volatile ("vmovntdq %%zmm4,%0" : "=m" (v[nd + 4][i]));
			if (np >= 6)
				asm volatile ("vmovntdq %%zmm5,%0" : "=m" (v[nd + 5][i]));
		} else {
			asm volatile ("vmovdqa64 %%zmm0,%0" : "=m" (v[nd][i]));
			asm volatile ("vmovdqa64 %%zmm1,%0" : "=m" (v[nd + 1][i]));
			if (np >= 3)
				asm volatile ("vmovdqa64 %%zmm2,%0" : "=m" (v[nd + 2][i]));
			if (np >= 4)
				asm volatile ("vmovdqa64 %%zmm3,%0" : "=m" (v[nd + 3][i]));
			if (np >= 5)
				asm volatile ("vmovdqa64 %%zmm4,%0" : "=m" (v[nd + 4][i]));
			if (np >= 6)
				asm volatile ("vmovdqa64 %%zmm5,%0" : "=m" (v[nd + 5][i]));
		}
	}

	raid_avx_end(streaming);
}


/*
 * Recover multiple data failures using selected parity blocks with AVX2 GFNI.
 *
 * Compute only the selected syndromes, keeping them in registers.
 * This avoids raid_delta_gen(), temporary syndrome buffers, recomputation of
 * parity blocks, and generation of unused parity rows.
 *
 * If recovering a single failure from P, the P delta directly contains
 * the missing block and no inverse-matrix multiplication is required.
 */
static __always_inline void raid_recX_avx2gfni_raid(int nr, int has_p, int *id, int *ip, int nd, size_t size, void **vv)
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
			S[ns][j] = raid_gfaffine_raid[A(ip[j], d)];

		++ns;
	}

	BUG_ON(k != nr);
	BUG_ON(ns != nd - nr);

	for (j = 0; j < nr; ++j)
		for (k = 0; k < nr; ++k)
			R[j][k] = raid_gfaffine_raid[V[j * nr + k]];

	raid_avx_begin();

	for (i = 0; i < size; i += 64) {
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

		if (nr >= 4) {
			asm volatile ("vmovdqa %0,%%ymm6" : : "m" (p[3][i]));
			asm volatile ("vmovdqa %0,%%ymm7" : : "m" (p[3][i + 32]));
		}

		if (nr >= 5) {
			asm volatile ("vmovdqa %0,%%ymm8" : : "m" (p[4][i]));
			asm volatile ("vmovdqa %0,%%ymm9" : : "m" (p[4][i + 32]));
		}

		if (nr >= 6) {
			asm volatile ("vmovdqa %0,%%ymm10" : : "m" (p[5][i]));
			asm volatile ("vmovdqa %0,%%ymm11" : : "m" (p[5][i + 32]));
		}

		for (s = 0; s < ns; ++s) {
			const uint8_t **t = S[s];

			asm volatile ("vmovdqa %0,%%ymm12" : : "m" (src[s][i]));
			asm volatile ("vmovdqa %0,%%ymm13" : : "m" (src[s][i + 32]));

			if (has_p) {
				asm volatile ("vpxor %ymm12,%ymm0,%ymm0");
				asm volatile ("vpxor %ymm13,%ymm1,%ymm1");
			} else {
				asm volatile ("vpbroadcastq %0,%%ymm14" : : "m" (t[0][0]));
				asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm12,%ymm15");
				asm volatile ("vpxor %ymm15,%ymm0,%ymm0");
				asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm13,%ymm15");
				asm volatile ("vpxor %ymm15,%ymm1,%ymm1");
			}

			if (nr >= 2) {
				asm volatile ("vpbroadcastq %0,%%ymm14" : : "m" (t[1][0]));
				asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm12,%ymm15");
				asm volatile ("vpxor %ymm15,%ymm2,%ymm2");
				asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm13,%ymm15");
				asm volatile ("vpxor %ymm15,%ymm3,%ymm3");
			}

			if (nr >= 3) {
				asm volatile ("vpbroadcastq %0,%%ymm14" : : "m" (t[2][0]));
				asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm12,%ymm15");
				asm volatile ("vpxor %ymm15,%ymm4,%ymm4");
				asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm13,%ymm15");
				asm volatile ("vpxor %ymm15,%ymm5,%ymm5");
			}

			if (nr >= 4) {
				asm volatile ("vpbroadcastq %0,%%ymm14" : : "m" (t[3][0]));
				asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm12,%ymm15");
				asm volatile ("vpxor %ymm15,%ymm6,%ymm6");
				asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm13,%ymm15");
				asm volatile ("vpxor %ymm15,%ymm7,%ymm7");
			}

			if (nr >= 5) {
				asm volatile ("vpbroadcastq %0,%%ymm14" : : "m" (t[4][0]));
				asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm12,%ymm15");
				asm volatile ("vpxor %ymm15,%ymm8,%ymm8");
				asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm13,%ymm15");
				asm volatile ("vpxor %ymm15,%ymm9,%ymm9");
			}

			if (nr >= 6) {
				asm volatile ("vpbroadcastq %0,%%ymm14" : : "m" (t[5][0]));
				asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm12,%ymm15");
				asm volatile ("vpxor %ymm15,%ymm10,%ymm10");
				asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm13,%ymm15");
				asm volatile ("vpxor %ymm15,%ymm11,%ymm11");
			}
		}

		/*
		 * With a single failure recovered from P, ymm0/ymm1
		 * already contain the missing data. Multiplication by
		 * the inverse coefficient 1 would be redundant.
		 */
		if (nr == 1 && has_p) {
			asm volatile ("vmovdqa %%ymm0,%0" : "=m" (pa[0][i]));
			asm volatile ("vmovdqa %%ymm1,%0" : "=m" (pa[0][i + 32]));
		} else {
			for (j = 0; j < nr; ++j) {
				const uint8_t **t = R[j];

				asm volatile ("vpbroadcastq %0,%%ymm14" : : "m" (t[0][0]));
				asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm0,%ymm12");
				asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm1,%ymm13");

				if (nr >= 2) {
					asm volatile ("vpbroadcastq %0,%%ymm14" : : "m" (t[1][0]));
					asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm2,%ymm15");
					asm volatile ("vpxor %ymm15,%ymm12,%ymm12");
					asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm3,%ymm15");
					asm volatile ("vpxor %ymm15,%ymm13,%ymm13");
				}

				if (nr >= 3) {
					asm volatile ("vpbroadcastq %0,%%ymm14" : : "m" (t[2][0]));
					asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm4,%ymm15");
					asm volatile ("vpxor %ymm15,%ymm12,%ymm12");
					asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm5,%ymm15");
					asm volatile ("vpxor %ymm15,%ymm13,%ymm13");
				}

				if (nr >= 4) {
					asm volatile ("vpbroadcastq %0,%%ymm14" : : "m" (t[3][0]));
					asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm6,%ymm15");
					asm volatile ("vpxor %ymm15,%ymm12,%ymm12");
					asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm7,%ymm15");
					asm volatile ("vpxor %ymm15,%ymm13,%ymm13");
				}

				if (nr >= 5) {
					asm volatile ("vpbroadcastq %0,%%ymm14" : : "m" (t[4][0]));
					asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm8,%ymm15");
					asm volatile ("vpxor %ymm15,%ymm12,%ymm12");
					asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm9,%ymm15");
					asm volatile ("vpxor %ymm15,%ymm13,%ymm13");
				}

				if (nr >= 6) {
					asm volatile ("vpbroadcastq %0,%%ymm14" : : "m" (t[5][0]));
					asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm10,%ymm15");
					asm volatile ("vpxor %ymm15,%ymm12,%ymm12");
					asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm11,%ymm15");
					asm volatile ("vpxor %ymm15,%ymm13,%ymm13");
				}

				asm volatile ("vmovdqa %%ymm12,%0" : "=m" (pa[j][i]));
				asm volatile ("vmovdqa %%ymm13,%0" : "=m" (pa[j][i + 32]));
			}
		}
	}

	raid_avx_end(0);
}

/*
 * Recover multiple data failures using selected parity blocks with AVX512 GFNI.
 *
 * Compute only the selected syndromes, keeping them in registers.
 * This avoids raid_delta_gen(), temporary syndrome buffers, recomputation of
 * parity blocks, and generation of unused parity rows.
 *
 * If P is available, reconstruct only nr - 1 missing blocks through the
 * inverse matrix and derive the last missing block from the P delta by XOR.
 */
static __always_inline void raid_recX_avx512gfni_raid(int nr, int has_p, int *id, int *ip, int nd, size_t size, void **vv)
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
			S[ns][j] = raid_gfaffine_raid[A(ip[j], d)];

		++ns;
	}

	BUG_ON(k != nr);
	BUG_ON(ns != nd - nr);

	/* the last inverse row isn't needed when P is available */
	for (j = 0; j < nr - has_p; ++j)
		for (k = 0; k < nr; ++k)
			R[j][k] = raid_gfaffine_raid[V[j * nr + k]];

	raid_avx_begin();

	for (i = 0; i < size; i += 64) {
		asm volatile ("vmovdqa64 %0,%%zmm0" : : "m" (p[0][i]));
		if (nr >= 2)
			asm volatile ("vmovdqa64 %0,%%zmm1" : : "m" (p[1][i]));
		if (nr >= 3)
			asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (p[2][i]));
		if (nr >= 4)
			asm volatile ("vmovdqa64 %0,%%zmm3" : : "m" (p[3][i]));
		if (nr >= 5)
			asm volatile ("vmovdqa64 %0,%%zmm4" : : "m" (p[4][i]));
		if (nr >= 6)
			asm volatile ("vmovdqa64 %0,%%zmm5" : : "m" (p[5][i]));

		for (s = 0; s < ns; ++s) {
			const uint8_t **t = S[s];

			asm volatile ("vmovdqa64 %0,%%zmm6" : : "m" (src[s][i]));

			if (has_p) {
				asm volatile ("vpxorq %zmm6,%zmm0,%zmm0");
			} else {
				asm volatile ("vpbroadcastq %0,%%zmm7" : : "m" (t[0][0]));
				asm volatile ("vgf2p8affineqb $0,%zmm7,%zmm6,%zmm8");
				asm volatile ("vpxorq %zmm8,%zmm0,%zmm0");
			}

			if (nr >= 2) {
				asm volatile ("vpbroadcastq %0,%%zmm7" : : "m" (t[1][0]));
				asm volatile ("vgf2p8affineqb $0,%zmm7,%zmm6,%zmm8");
				asm volatile ("vpxorq %zmm8,%zmm1,%zmm1");
			}

			if (nr >= 3) {
				asm volatile ("vpbroadcastq %0,%%zmm7" : : "m" (t[2][0]));
				asm volatile ("vgf2p8affineqb $0,%zmm7,%zmm6,%zmm8");
				asm volatile ("vpxorq %zmm8,%zmm2,%zmm2");
			}

			if (nr >= 4) {
				asm volatile ("vpbroadcastq %0,%%zmm7" : : "m" (t[3][0]));
				asm volatile ("vgf2p8affineqb $0,%zmm7,%zmm6,%zmm8");
				asm volatile ("vpxorq %zmm8,%zmm3,%zmm3");
			}

			if (nr >= 5) {
				asm volatile ("vpbroadcastq %0,%%zmm7" : : "m" (t[4][0]));
				asm volatile ("vgf2p8affineqb $0,%zmm7,%zmm6,%zmm8");
				asm volatile ("vpxorq %zmm8,%zmm4,%zmm4");
			}

			if (nr >= 6) {
				asm volatile ("vpbroadcastq %0,%%zmm7" : : "m" (t[5][0]));
				asm volatile ("vgf2p8affineqb $0,%zmm7,%zmm6,%zmm8");
				asm volatile ("vpxorq %zmm8,%zmm5,%zmm5");
			}
		}

		/* preserve the complete P delta */
		if (has_p)
			asm volatile ("vmovdqa64 %zmm0,%zmm30");

		/*
		 * If P is available, reconstruct only nr - 1 blocks through
		 * the inverse matrix. The final block remains in zmm30.
		 */
		for (j = 0; j < nr - has_p; ++j) {
			const uint8_t **t = R[j];

			asm volatile ("vpbroadcastq %0,%%zmm6" : : "m" (t[0][0]));
			asm volatile ("vgf2p8affineqb $0,%zmm6,%zmm0,%zmm7");

			if (nr >= 2) {
				asm volatile ("vpbroadcastq %0,%%zmm6" : : "m" (t[1][0]));
				asm volatile ("vgf2p8affineqb $0,%zmm6,%zmm1,%zmm8");
				asm volatile ("vpxorq %zmm8,%zmm7,%zmm7");
			}

			if (nr >= 3) {
				asm volatile ("vpbroadcastq %0,%%zmm6" : : "m" (t[2][0]));
				asm volatile ("vgf2p8affineqb $0,%zmm6,%zmm2,%zmm8");
				asm volatile ("vpxorq %zmm8,%zmm7,%zmm7");
			}

			if (nr >= 4) {
				asm volatile ("vpbroadcastq %0,%%zmm6" : : "m" (t[3][0]));
				asm volatile ("vgf2p8affineqb $0,%zmm6,%zmm3,%zmm8");
				asm volatile ("vpxorq %zmm8,%zmm7,%zmm7");
			}

			if (nr >= 5) {
				asm volatile ("vpbroadcastq %0,%%zmm6" : : "m" (t[4][0]));
				asm volatile ("vgf2p8affineqb $0,%zmm6,%zmm4,%zmm8");
				asm volatile ("vpxorq %zmm8,%zmm7,%zmm7");
			}

			if (nr >= 6) {
				asm volatile ("vpbroadcastq %0,%%zmm6" : : "m" (t[5][0]));
				asm volatile ("vgf2p8affineqb $0,%zmm6,%zmm5,%zmm8");
				asm volatile ("vpxorq %zmm8,%zmm7,%zmm7");
			}

			if (has_p)
				asm volatile ("vpxorq %zmm7,%zmm30,%zmm30");

			asm volatile ("vmovdqa64 %%zmm7,%0" : "=m" (pa[j][i]));
		}

		if (has_p)
			asm volatile ("vmovdqa64 %%zmm30,%0" : "=m" (pa[nr - 1][i]));
	}

	raid_avx_end(0);
}

/*
 * Recover multiple data failures for the AES polynomial using selected parity blocks with AVX2 GFNI.
 *
 * Compute only the selected syndromes, keeping them in registers.
 * This avoids raid_delta_gen(), temporary syndrome buffers, recomputation of
 * parity blocks, and generation of unused parity rows.
 *
 * If recovering a single failure from P, the P delta directly contains
 * the missing block and no inverse-matrix multiplication is required.
 */
static __always_inline void raid_recX_avx2gfni_aes(int nr, int has_p, int *id, int *ip, int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p[RAID_PARITY_MAX];
	uint8_t *pa[RAID_PARITY_MAX];
	uint8_t *src[RAID_DATA_MAX];
	uint8_t G[RAID_PARITY_MAX * RAID_PARITY_MAX];
	uint8_t V[RAID_PARITY_MAX * RAID_PARITY_MAX];
	uint8_t S[RAID_DATA_MAX][RAID_PARITY_MAX];
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
			S[ns][j] = A(ip[j], d);

		++ns;
	}

	BUG_ON(k != nr);
	BUG_ON(ns != nd - nr);

	raid_avx_begin();

	for (i = 0; i < size; i += 64) {
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

		if (nr >= 4) {
			asm volatile ("vmovdqa %0,%%ymm6" : : "m" (p[3][i]));
			asm volatile ("vmovdqa %0,%%ymm7" : : "m" (p[3][i + 32]));
		}

		if (nr >= 5) {
			asm volatile ("vmovdqa %0,%%ymm8" : : "m" (p[4][i]));
			asm volatile ("vmovdqa %0,%%ymm9" : : "m" (p[4][i + 32]));
		}

		if (nr >= 6) {
			asm volatile ("vmovdqa %0,%%ymm10" : : "m" (p[5][i]));
			asm volatile ("vmovdqa %0,%%ymm11" : : "m" (p[5][i + 32]));
		}

		for (s = 0; s < ns; ++s) {
			asm volatile ("vmovdqa %0,%%ymm12" : : "m" (src[s][i]));
			asm volatile ("vmovdqa %0,%%ymm13" : : "m" (src[s][i + 32]));

			if (has_p) {
				asm volatile ("vpxor %ymm12,%ymm0,%ymm0");
				asm volatile ("vpxor %ymm13,%ymm1,%ymm1");
			} else {
				asm volatile ("vpbroadcastb %0,%%ymm14" : : "m" (S[s][0]));
				asm volatile ("vgf2p8mulb %ymm14,%ymm12,%ymm15");
				asm volatile ("vpxor %ymm15,%ymm0,%ymm0");
				asm volatile ("vgf2p8mulb %ymm14,%ymm13,%ymm15");
				asm volatile ("vpxor %ymm15,%ymm1,%ymm1");
			}

			if (nr >= 2) {
				asm volatile ("vpbroadcastb %0,%%ymm14" : : "m" (S[s][1]));
				asm volatile ("vgf2p8mulb %ymm14,%ymm12,%ymm15");
				asm volatile ("vpxor %ymm15,%ymm2,%ymm2");
				asm volatile ("vgf2p8mulb %ymm14,%ymm13,%ymm15");
				asm volatile ("vpxor %ymm15,%ymm3,%ymm3");
			}

			if (nr >= 3) {
				asm volatile ("vpbroadcastb %0,%%ymm14" : : "m" (S[s][2]));
				asm volatile ("vgf2p8mulb %ymm14,%ymm12,%ymm15");
				asm volatile ("vpxor %ymm15,%ymm4,%ymm4");
				asm volatile ("vgf2p8mulb %ymm14,%ymm13,%ymm15");
				asm volatile ("vpxor %ymm15,%ymm5,%ymm5");
			}

			if (nr >= 4) {
				asm volatile ("vpbroadcastb %0,%%ymm14" : : "m" (S[s][3]));
				asm volatile ("vgf2p8mulb %ymm14,%ymm12,%ymm15");
				asm volatile ("vpxor %ymm15,%ymm6,%ymm6");
				asm volatile ("vgf2p8mulb %ymm14,%ymm13,%ymm15");
				asm volatile ("vpxor %ymm15,%ymm7,%ymm7");
			}

			if (nr >= 5) {
				asm volatile ("vpbroadcastb %0,%%ymm14" : : "m" (S[s][4]));
				asm volatile ("vgf2p8mulb %ymm14,%ymm12,%ymm15");
				asm volatile ("vpxor %ymm15,%ymm8,%ymm8");
				asm volatile ("vgf2p8mulb %ymm14,%ymm13,%ymm15");
				asm volatile ("vpxor %ymm15,%ymm9,%ymm9");
			}

			if (nr >= 6) {
				asm volatile ("vpbroadcastb %0,%%ymm14" : : "m" (S[s][5]));
				asm volatile ("vgf2p8mulb %ymm14,%ymm12,%ymm15");
				asm volatile ("vpxor %ymm15,%ymm10,%ymm10");
				asm volatile ("vgf2p8mulb %ymm14,%ymm13,%ymm15");
				asm volatile ("vpxor %ymm15,%ymm11,%ymm11");
			}
		}

		/*
		 * Pdelta is the missing block directly for rec1of1.
		 * Do not multiply it by V[0] == 1.
		 */
		if (nr == 1 && has_p) {
			asm volatile ("vmovdqa %%ymm0,%0" : "=m" (pa[0][i]));
			asm volatile ("vmovdqa %%ymm1,%0" : "=m" (pa[0][i + 32]));
		} else {
			for (j = 0; j < nr; ++j) {
				asm volatile ("vpbroadcastb %0,%%ymm14" : : "m" (V[j * nr]));
				asm volatile ("vgf2p8mulb %ymm14,%ymm0,%ymm12");
				asm volatile ("vgf2p8mulb %ymm14,%ymm1,%ymm13");

				if (nr >= 2) {
					asm volatile ("vpbroadcastb %0,%%ymm14" : : "m" (V[j * nr + 1]));
					asm volatile ("vgf2p8mulb %ymm14,%ymm2,%ymm15");
					asm volatile ("vpxor %ymm15,%ymm12,%ymm12");
					asm volatile ("vgf2p8mulb %ymm14,%ymm3,%ymm15");
					asm volatile ("vpxor %ymm15,%ymm13,%ymm13");
				}

				if (nr >= 3) {
					asm volatile ("vpbroadcastb %0,%%ymm14" : : "m" (V[j * nr + 2]));
					asm volatile ("vgf2p8mulb %ymm14,%ymm4,%ymm15");
					asm volatile ("vpxor %ymm15,%ymm12,%ymm12");
					asm volatile ("vgf2p8mulb %ymm14,%ymm5,%ymm15");
					asm volatile ("vpxor %ymm15,%ymm13,%ymm13");
				}

				if (nr >= 4) {
					asm volatile ("vpbroadcastb %0,%%ymm14" : : "m" (V[j * nr + 3]));
					asm volatile ("vgf2p8mulb %ymm14,%ymm6,%ymm15");
					asm volatile ("vpxor %ymm15,%ymm12,%ymm12");
					asm volatile ("vgf2p8mulb %ymm14,%ymm7,%ymm15");
					asm volatile ("vpxor %ymm15,%ymm13,%ymm13");
				}

				if (nr >= 5) {
					asm volatile ("vpbroadcastb %0,%%ymm14" : : "m" (V[j * nr + 4]));
					asm volatile ("vgf2p8mulb %ymm14,%ymm8,%ymm15");
					asm volatile ("vpxor %ymm15,%ymm12,%ymm12");
					asm volatile ("vgf2p8mulb %ymm14,%ymm9,%ymm15");
					asm volatile ("vpxor %ymm15,%ymm13,%ymm13");
				}

				if (nr >= 6) {
					asm volatile ("vpbroadcastb %0,%%ymm14" : : "m" (V[j * nr + 5]));
					asm volatile ("vgf2p8mulb %ymm14,%ymm10,%ymm15");
					asm volatile ("vpxor %ymm15,%ymm12,%ymm12");
					asm volatile ("vgf2p8mulb %ymm14,%ymm11,%ymm15");
					asm volatile ("vpxor %ymm15,%ymm13,%ymm13");
				}

				asm volatile ("vmovdqa %%ymm12,%0" : "=m" (pa[j][i]));
				asm volatile ("vmovdqa %%ymm13,%0" : "=m" (pa[j][i + 32]));
			}
		}
	}

	raid_avx_end(0);
}

/*
 * Recover multiple data failures for the AES polynomial using selected parity blocks with AVX512 GFNI.
 *
 * Compute only the selected syndromes, keeping them in registers.
 * This avoids raid_delta_gen(), temporary syndrome buffers, recomputation of
 * parity blocks, and generation of unused parity rows.
 *
 * If P is available, reconstruct only nr - 1 missing blocks through the
 * inverse matrix and derive the last missing block from the P delta by XOR.
 */
static __always_inline void raid_recX_avx512gfni_aes(int nr, int has_p, int *id, int *ip, int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p[RAID_PARITY_MAX];
	uint8_t *pa[RAID_PARITY_MAX];
	uint8_t *src[RAID_DATA_MAX];
	uint8_t G[RAID_PARITY_MAX * RAID_PARITY_MAX];
	uint8_t V[RAID_PARITY_MAX * RAID_PARITY_MAX];
	uint8_t S[RAID_DATA_MAX][RAID_PARITY_MAX];
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
			S[ns][j] = A(ip[j], d);

		++ns;
	}

	BUG_ON(k != nr);
	BUG_ON(ns != nd - nr);

	raid_avx_begin();

	for (i = 0; i < size; i += 64) {
		asm volatile ("vmovdqa64 %0,%%zmm0" : : "m" (p[0][i]));
		if (nr >= 2)
			asm volatile ("vmovdqa64 %0,%%zmm1" : : "m" (p[1][i]));
		if (nr >= 3)
			asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (p[2][i]));
		if (nr >= 4)
			asm volatile ("vmovdqa64 %0,%%zmm3" : : "m" (p[3][i]));
		if (nr >= 5)
			asm volatile ("vmovdqa64 %0,%%zmm4" : : "m" (p[4][i]));
		if (nr >= 6)
			asm volatile ("vmovdqa64 %0,%%zmm5" : : "m" (p[5][i]));

		for (s = 0; s < ns; ++s) {
			asm volatile ("vmovdqa64 %0,%%zmm6" : : "m" (src[s][i]));

			if (has_p) {
				asm volatile ("vpxorq %zmm6,%zmm0,%zmm0");
			} else {
				asm volatile ("vpbroadcastb %0,%%zmm7" : : "m" (S[s][0]));
				asm volatile ("vgf2p8mulb %zmm7,%zmm6,%zmm8");
				asm volatile ("vpxorq %zmm8,%zmm0,%zmm0");
			}

			if (nr >= 2) {
				asm volatile ("vpbroadcastb %0,%%zmm7" : : "m" (S[s][1]));
				asm volatile ("vgf2p8mulb %zmm7,%zmm6,%zmm8");
				asm volatile ("vpxorq %zmm8,%zmm1,%zmm1");
			}

			if (nr >= 3) {
				asm volatile ("vpbroadcastb %0,%%zmm7" : : "m" (S[s][2]));
				asm volatile ("vgf2p8mulb %zmm7,%zmm6,%zmm8");
				asm volatile ("vpxorq %zmm8,%zmm2,%zmm2");
			}

			if (nr >= 4) {
				asm volatile ("vpbroadcastb %0,%%zmm7" : : "m" (S[s][3]));
				asm volatile ("vgf2p8mulb %zmm7,%zmm6,%zmm8");
				asm volatile ("vpxorq %zmm8,%zmm3,%zmm3");
			}

			if (nr >= 5) {
				asm volatile ("vpbroadcastb %0,%%zmm7" : : "m" (S[s][4]));
				asm volatile ("vgf2p8mulb %zmm7,%zmm6,%zmm8");
				asm volatile ("vpxorq %zmm8,%zmm4,%zmm4");
			}

			if (nr >= 6) {
				asm volatile ("vpbroadcastb %0,%%zmm7" : : "m" (S[s][5]));
				asm volatile ("vgf2p8mulb %zmm7,%zmm6,%zmm8");
				asm volatile ("vpxorq %zmm8,%zmm5,%zmm5");
			}
		}

		/* preserve the complete P delta */
		if (has_p)
			asm volatile ("vmovdqa64 %zmm0,%zmm30");

		for (j = 0; j < nr - has_p; ++j) {
			asm volatile ("vpbroadcastb %0,%%zmm6" : : "m" (V[j * nr]));
			asm volatile ("vgf2p8mulb %zmm6,%zmm0,%zmm7");

			if (nr >= 2) {
				asm volatile ("vpbroadcastb %0,%%zmm6" : : "m" (V[j * nr + 1]));
				asm volatile ("vgf2p8mulb %zmm6,%zmm1,%zmm8");
				asm volatile ("vpxorq %zmm8,%zmm7,%zmm7");
			}

			if (nr >= 3) {
				asm volatile ("vpbroadcastb %0,%%zmm6" : : "m" (V[j * nr + 2]));
				asm volatile ("vgf2p8mulb %zmm6,%zmm2,%zmm8");
				asm volatile ("vpxorq %zmm8,%zmm7,%zmm7");
			}

			if (nr >= 4) {
				asm volatile ("vpbroadcastb %0,%%zmm6" : : "m" (V[j * nr + 3]));
				asm volatile ("vgf2p8mulb %zmm6,%zmm3,%zmm8");
				asm volatile ("vpxorq %zmm8,%zmm7,%zmm7");
			}

			if (nr >= 5) {
				asm volatile ("vpbroadcastb %0,%%zmm6" : : "m" (V[j * nr + 4]));
				asm volatile ("vgf2p8mulb %zmm6,%zmm4,%zmm8");
				asm volatile ("vpxorq %zmm8,%zmm7,%zmm7");
			}

			if (nr >= 6) {
				asm volatile ("vpbroadcastb %0,%%zmm6" : : "m" (V[j * nr + 5]));
				asm volatile ("vgf2p8mulb %zmm6,%zmm5,%zmm8");
				asm volatile ("vpxorq %zmm8,%zmm7,%zmm7");
			}

			if (has_p)
				asm volatile ("vpxorq %zmm7,%zmm30,%zmm30");

			asm volatile ("vmovdqa64 %%zmm7,%0" : "=m" (pa[j][i]));
		}

		if (has_p)
			asm volatile ("vmovdqa64 %%zmm30,%0" : "=m" (pa[nr - 1][i]));
	}

	raid_avx_end(0);
}

void raid_gen2_avx2gfni_raid(int nd, size_t size, void **vv, int streaming)
{
	if (streaming)
		raid_genX_avx2gfni_raid(nd, size, vv, 2, 1);
	else
		raid_genX_avx2gfni_raid(nd, size, vv, 2, 0);
}

void raid_gen2_avx512gfni_raid(int nd, size_t size, void **vv, int streaming)
{
	if (streaming)
		raid_gen2_avx512gfni_raid_gen(nd, size, vv, 1);
	else
		raid_gen2_avx512gfni_raid_gen(nd, size, vv, 0);
}

void raid_gen3_avx2gfni_raid(int nd, size_t size, void **vv, int streaming)
{
	if (streaming)
		raid_genX_avx2gfni_raid(nd, size, vv, 3, 1);
	else
		raid_genX_avx2gfni_raid(nd, size, vv, 3, 0);
}

void raid_gen3_avx512gfni_raid(int nd, size_t size, void **vv, int streaming)
{
	if (streaming)
		raid_gen3_avx512gfni_raid_gen(nd, size, vv, 1);
	else
		raid_gen3_avx512gfni_raid_gen(nd, size, vv, 0);
}

void raid_gen4_avx2gfni_raid(int nd, size_t size, void **vv, int streaming)
{
	if (streaming)
		raid_genX_avx2gfni_raid(nd, size, vv, 4, 1);
	else
		raid_genX_avx2gfni_raid(nd, size, vv, 4, 0);
}

void raid_gen4_avx512gfni_raid(int nd, size_t size, void **vv, int streaming)
{
	if (streaming)
		raid_genX_avx512gfni_raid(nd, size, vv, 4, 1);
	else
		raid_genX_avx512gfni_raid(nd, size, vv, 4, 0);
}

void raid_gen5_avx2gfni_raid(int nd, size_t size, void **vv, int streaming)
{
	if (streaming)
		raid_genX_avx2gfni_raid(nd, size, vv, 5, 1);
	else
		raid_genX_avx2gfni_raid(nd, size, vv, 5, 0);
}

void raid_gen5_avx512gfni_raid(int nd, size_t size, void **vv, int streaming)
{
	if (streaming)
		raid_genX_avx512gfni_raid(nd, size, vv, 5, 1);
	else
		raid_genX_avx512gfni_raid(nd, size, vv, 5, 0);
}

void raid_gen6_avx2gfni_raid(int nd, size_t size, void **vv, int streaming)
{
	if (streaming)
		raid_genX_avx2gfni_raid(nd, size, vv, 6, 1);
	else
		raid_genX_avx2gfni_raid(nd, size, vv, 6, 0);
}

void raid_gen6_avx512gfni_raid(int nd, size_t size, void **vv, int streaming)
{
	if (streaming)
		raid_genX_avx512gfni_raid(nd, size, vv, 6, 1);
	else
		raid_genX_avx512gfni_raid(nd, size, vv, 6, 0);
}

void raid_gen2_avx2gfni_aes(int nd, size_t size, void **vv, int streaming)
{
	if (streaming)
		raid_genX_avx2gfni_aes(nd, size, vv, 2, 1);
	else
		raid_genX_avx2gfni_aes(nd, size, vv, 2, 0);
}

void raid_gen2_avx512gfni_aes(int nd, size_t size, void **vv, int streaming)
{
	if (streaming)
		raid_gen2_avx512gfni_aes_gen(nd, size, vv, 1);
	else
		raid_gen2_avx512gfni_aes_gen(nd, size, vv, 0);
}

void raid_gen3_avx2gfni_aes(int nd, size_t size, void **vv, int streaming)
{
	if (streaming)
		raid_genX_avx2gfni_aes(nd, size, vv, 3, 1);
	else
		raid_genX_avx2gfni_aes(nd, size, vv, 3, 0);
}

void raid_gen3_avx512gfni_aes(int nd, size_t size, void **vv, int streaming)
{
	if (streaming)
		raid_gen3_avx512gfni_aes_gen(nd, size, vv, 1);
	else
		raid_gen3_avx512gfni_aes_gen(nd, size, vv, 0);
}

void raid_gen4_avx2gfni_aes(int nd, size_t size, void **vv, int streaming)
{
	if (streaming)
		raid_genX_avx2gfni_aes(nd, size, vv, 4, 1);
	else
		raid_genX_avx2gfni_aes(nd, size, vv, 4, 0);
}

void raid_gen4_avx512gfni_aes(int nd, size_t size, void **vv, int streaming)
{
	if (streaming)
		raid_genX_avx512gfni_aes(nd, size, vv, 4, 1);
	else
		raid_genX_avx512gfni_aes(nd, size, vv, 4, 0);
}

void raid_gen5_avx2gfni_aes(int nd, size_t size, void **vv, int streaming)
{
	if (streaming)
		raid_genX_avx2gfni_aes(nd, size, vv, 5, 1);
	else
		raid_genX_avx2gfni_aes(nd, size, vv, 5, 0);
}

void raid_gen5_avx512gfni_aes(int nd, size_t size, void **vv, int streaming)
{
	if (streaming)
		raid_genX_avx512gfni_aes(nd, size, vv, 5, 1);
	else
		raid_genX_avx512gfni_aes(nd, size, vv, 5, 0);
}

void raid_gen6_avx2gfni_aes(int nd, size_t size, void **vv, int streaming)
{
	if (streaming)
		raid_genX_avx2gfni_aes(nd, size, vv, 6, 1);
	else
		raid_genX_avx2gfni_aes(nd, size, vv, 6, 0);
}

void raid_gen6_avx512gfni_aes(int nd, size_t size, void **vv, int streaming)
{
	if (streaming)
		raid_genX_avx512gfni_aes(nd, size, vv, 6, 1);
	else
		raid_genX_avx512gfni_aes(nd, size, vv, 6, 0);
}

void raid_rec1_avx2gfni_raid(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 1);

	/* if recovering with P, use a custom XOR-only path with temporal stores */
	if (ip[0] == 0) {
		raid_rec1of1(id, nd, size, vv);
		return;
	}

	raid_recX_avx2gfni_raid(1, 0, id, ip, nd, size, vv);
}

void raid_rec2_avx2gfni_raid(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 2);
	if (ip[0] == 0)
		raid_recX_avx2gfni_raid(2, 1, id, ip, nd, size, vv);
	else
		raid_recX_avx2gfni_raid(2, 0, id, ip, nd, size, vv);
}

void raid_rec3_avx2gfni_raid(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 3);
	if (ip[0] == 0)
		raid_recX_avx2gfni_raid(3, 1, id, ip, nd, size, vv);
	else
		raid_recX_avx2gfni_raid(3, 0, id, ip, nd, size, vv);
}

void raid_rec4_avx2gfni_raid(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 4);
	if (ip[0] == 0)
		raid_recX_avx2gfni_raid(4, 1, id, ip, nd, size, vv);
	else
		raid_recX_avx2gfni_raid(4, 0, id, ip, nd, size, vv);
}

void raid_rec5_avx2gfni_raid(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 5);
	if (ip[0] == 0)
		raid_recX_avx2gfni_raid(5, 1, id, ip, nd, size, vv);
	else
		raid_recX_avx2gfni_raid(5, 0, id, ip, nd, size, vv);
}

void raid_rec6_avx2gfni_raid(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 6);
	if (ip[0] == 0)
		raid_recX_avx2gfni_raid(6, 1, id, ip, nd, size, vv);
	else
		raid_recX_avx2gfni_raid(6, 0, id, ip, nd, size, vv);
}

void raid_rec1_avx512gfni_raid(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 1);

	/* if recovering with P, use a custom XOR-only path with temporal stores */
	if (ip[0] == 0) {
		raid_rec1of1(id, nd, size, vv);
		return;
	}

	raid_recX_avx512gfni_raid(1, 0, id, ip, nd, size, vv);
}

void raid_rec2_avx512gfni_raid(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 2);
	if (ip[0] == 0)
		raid_recX_avx512gfni_raid(2, 1, id, ip, nd, size, vv);
	else
		raid_recX_avx512gfni_raid(2, 0, id, ip, nd, size, vv);
}

void raid_rec3_avx512gfni_raid(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 3);
	if (ip[0] == 0)
		raid_recX_avx512gfni_raid(3, 1, id, ip, nd, size, vv);
	else
		raid_recX_avx512gfni_raid(3, 0, id, ip, nd, size, vv);
}

void raid_rec4_avx512gfni_raid(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 4);
	if (ip[0] == 0)
		raid_recX_avx512gfni_raid(4, 1, id, ip, nd, size, vv);
	else
		raid_recX_avx512gfni_raid(4, 0, id, ip, nd, size, vv);
}

void raid_rec5_avx512gfni_raid(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 5);
	if (ip[0] == 0)
		raid_recX_avx512gfni_raid(5, 1, id, ip, nd, size, vv);
	else
		raid_recX_avx512gfni_raid(5, 0, id, ip, nd, size, vv);
}

void raid_rec6_avx512gfni_raid(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 6);
	if (ip[0] == 0)
		raid_recX_avx512gfni_raid(6, 1, id, ip, nd, size, vv);
	else
		raid_recX_avx512gfni_raid(6, 0, id, ip, nd, size, vv);
}

void raid_rec1_avx2gfni_aes(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 1);

	/* if recovering with P, use a custom XOR-only path with temporal stores */
	if (ip[0] == 0) {
		raid_rec1of1(id, nd, size, vv);
		return;
	}

	raid_recX_avx2gfni_aes(1, 0, id, ip, nd, size, vv);
}

void raid_rec2_avx2gfni_aes(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 2);
	if (ip[0] == 0)
		raid_recX_avx2gfni_aes(2, 1, id, ip, nd, size, vv);
	else
		raid_recX_avx2gfni_aes(2, 0, id, ip, nd, size, vv);
}

void raid_rec3_avx2gfni_aes(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 3);
	if (ip[0] == 0)
		raid_recX_avx2gfni_aes(3, 1, id, ip, nd, size, vv);
	else
		raid_recX_avx2gfni_aes(3, 0, id, ip, nd, size, vv);
}

void raid_rec4_avx2gfni_aes(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 4);
	if (ip[0] == 0)
		raid_recX_avx2gfni_aes(4, 1, id, ip, nd, size, vv);
	else
		raid_recX_avx2gfni_aes(4, 0, id, ip, nd, size, vv);
}

void raid_rec5_avx2gfni_aes(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 5);
	if (ip[0] == 0)
		raid_recX_avx2gfni_aes(5, 1, id, ip, nd, size, vv);
	else
		raid_recX_avx2gfni_aes(5, 0, id, ip, nd, size, vv);
}

void raid_rec6_avx2gfni_aes(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 6);
	if (ip[0] == 0)
		raid_recX_avx2gfni_aes(6, 1, id, ip, nd, size, vv);
	else
		raid_recX_avx2gfni_aes(6, 0, id, ip, nd, size, vv);
}

void raid_rec1_avx512gfni_aes(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 1);

	/* if recovering with P, use a custom XOR-only path with temporal stores */
	if (ip[0] == 0) {
		raid_rec1of1(id, nd, size, vv);
		return;
	}

	raid_recX_avx512gfni_aes(1, 0, id, ip, nd, size, vv);
}

void raid_rec2_avx512gfni_aes(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 2);
	if (ip[0] == 0)
		raid_recX_avx512gfni_aes(2, 1, id, ip, nd, size, vv);
	else
		raid_recX_avx512gfni_aes(2, 0, id, ip, nd, size, vv);
}

void raid_rec3_avx512gfni_aes(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 3);
	if (ip[0] == 0)
		raid_recX_avx512gfni_aes(3, 1, id, ip, nd, size, vv);
	else
		raid_recX_avx512gfni_aes(3, 0, id, ip, nd, size, vv);
}

void raid_rec4_avx512gfni_aes(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 4);
	if (ip[0] == 0)
		raid_recX_avx512gfni_aes(4, 1, id, ip, nd, size, vv);
	else
		raid_recX_avx512gfni_aes(4, 0, id, ip, nd, size, vv);
}

void raid_rec5_avx512gfni_aes(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 5);
	if (ip[0] == 0)
		raid_recX_avx512gfni_aes(5, 1, id, ip, nd, size, vv);
	else
		raid_recX_avx512gfni_aes(5, 0, id, ip, nd, size, vv);
}

void raid_rec6_avx512gfni_aes(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 6);
	if (ip[0] == 0)
		raid_recX_avx512gfni_aes(6, 1, id, ip, nd, size, vv);
	else
		raid_recX_avx512gfni_aes(6, 0, id, ip, nd, size, vv);
}

void raid_register_avx2gfni(void)
{
	if (raid_cpu_has_avx2gfni()) {
		raid_gen_register(RAID_ALGO_CAUCHY_PAR2, "gfni", raid_gen2_avx2gfni_raid, RAID_POLY_RAID);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR3, "gfni", raid_gen3_avx2gfni_raid, RAID_POLY_RAID);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR4, "gfni", raid_gen4_avx2gfni_raid, RAID_POLY_RAID);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR5, "gfni", raid_gen5_avx2gfni_raid, RAID_POLY_RAID);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR6, "gfni", raid_gen6_avx2gfni_raid, RAID_POLY_RAID);

		raid_gen_register(RAID_ALGO_CAUCHY_PAR2, "gfni", raid_gen2_avx2gfni_aes, RAID_POLY_AES);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR3, "gfni", raid_gen3_avx2gfni_aes, RAID_POLY_AES);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR4, "gfni", raid_gen4_avx2gfni_aes, RAID_POLY_AES);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR5, "gfni", raid_gen5_avx2gfni_aes, RAID_POLY_AES);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR6, "gfni", raid_gen6_avx2gfni_aes, RAID_POLY_AES);

		raid_rec_register(RAID_ALGO_CAUCHY_PAR1, "gfni", raid_rec1_avx2gfni_raid, RAID_POLY_RAID);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR2, "gfni", raid_rec2_avx2gfni_raid, RAID_POLY_RAID);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR3, "gfni", raid_rec3_avx2gfni_raid, RAID_POLY_RAID);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR4, "gfni", raid_rec4_avx2gfni_raid, RAID_POLY_RAID);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR5, "gfni", raid_rec5_avx2gfni_raid, RAID_POLY_RAID);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR6, "gfni", raid_rec6_avx2gfni_raid, RAID_POLY_RAID);

		raid_rec_register(RAID_ALGO_CAUCHY_PAR1, "gfni", raid_rec1_avx2gfni_aes, RAID_POLY_AES);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR2, "gfni", raid_rec2_avx2gfni_aes, RAID_POLY_AES);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR3, "gfni", raid_rec3_avx2gfni_aes, RAID_POLY_AES);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR4, "gfni", raid_rec4_avx2gfni_aes, RAID_POLY_AES);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR5, "gfni", raid_rec5_avx2gfni_aes, RAID_POLY_AES);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR6, "gfni", raid_rec6_avx2gfni_aes, RAID_POLY_AES);
	}
}

void raid_register_avx512gfni(void)
{
	if (raid_cpu_has_avx512gfni()) {
		if (!raid_cpu_has_slow_avx512()) {
			raid_gen_register(RAID_ALGO_CAUCHY_PAR2, "gfni512", raid_gen2_avx512gfni_raid, RAID_POLY_RAID);
			raid_gen_register(RAID_ALGO_CAUCHY_PAR3, "gfni512", raid_gen3_avx512gfni_raid, RAID_POLY_RAID);
			raid_gen_register(RAID_ALGO_CAUCHY_PAR4, "gfni512", raid_gen4_avx512gfni_raid, RAID_POLY_RAID);
			raid_gen_register(RAID_ALGO_CAUCHY_PAR5, "gfni512", raid_gen5_avx512gfni_raid, RAID_POLY_RAID);
			raid_gen_register(RAID_ALGO_CAUCHY_PAR6, "gfni512", raid_gen6_avx512gfni_raid, RAID_POLY_RAID);

			raid_gen_register(RAID_ALGO_CAUCHY_PAR2, "gfni512", raid_gen2_avx512gfni_aes, RAID_POLY_AES);
			raid_gen_register(RAID_ALGO_CAUCHY_PAR3, "gfni512", raid_gen3_avx512gfni_aes, RAID_POLY_AES);
			raid_gen_register(RAID_ALGO_CAUCHY_PAR4, "gfni512", raid_gen4_avx512gfni_aes, RAID_POLY_AES);
			raid_gen_register(RAID_ALGO_CAUCHY_PAR5, "gfni512", raid_gen5_avx512gfni_aes, RAID_POLY_AES);
			raid_gen_register(RAID_ALGO_CAUCHY_PAR6, "gfni512", raid_gen6_avx512gfni_aes, RAID_POLY_AES);

			raid_rec_register(RAID_ALGO_CAUCHY_PAR1, "gfni512", raid_rec1_avx512gfni_raid, RAID_POLY_RAID);
			raid_rec_register(RAID_ALGO_CAUCHY_PAR2, "gfni512", raid_rec2_avx512gfni_raid, RAID_POLY_RAID);
			raid_rec_register(RAID_ALGO_CAUCHY_PAR3, "gfni512", raid_rec3_avx512gfni_raid, RAID_POLY_RAID);
			raid_rec_register(RAID_ALGO_CAUCHY_PAR4, "gfni512", raid_rec4_avx512gfni_raid, RAID_POLY_RAID);
			raid_rec_register(RAID_ALGO_CAUCHY_PAR5, "gfni512", raid_rec5_avx512gfni_raid, RAID_POLY_RAID);
			raid_rec_register(RAID_ALGO_CAUCHY_PAR6, "gfni512", raid_rec6_avx512gfni_raid, RAID_POLY_RAID);

			raid_rec_register(RAID_ALGO_CAUCHY_PAR1, "gfni512", raid_rec1_avx512gfni_aes, RAID_POLY_AES);
			raid_rec_register(RAID_ALGO_CAUCHY_PAR2, "gfni512", raid_rec2_avx512gfni_aes, RAID_POLY_AES);
			raid_rec_register(RAID_ALGO_CAUCHY_PAR3, "gfni512", raid_rec3_avx512gfni_aes, RAID_POLY_AES);
			raid_rec_register(RAID_ALGO_CAUCHY_PAR4, "gfni512", raid_rec4_avx512gfni_aes, RAID_POLY_AES);
			raid_rec_register(RAID_ALGO_CAUCHY_PAR5, "gfni512", raid_rec5_avx512gfni_aes, RAID_POLY_AES);
			raid_rec_register(RAID_ALGO_CAUCHY_PAR6, "gfni512", raid_rec6_avx512gfni_aes, RAID_POLY_AES);
		}
	}
}

#endif
