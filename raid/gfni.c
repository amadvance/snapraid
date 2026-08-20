// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Andrea Mazzoleni

#include "internal.h"
#include "gf.h"
#include "cpu.h"

#ifdef CONFIG_X86_64
/*
 * GENX AVX2 GFNI implementation for the RAID polynomial with Horner evaluation.
 *
 * Q is evaluated with Horner's rule using the fixed multiply-by-2 affine
 * matrix. The remaining Cauchy parities use their per-disk coefficients.
 *
 * Only supported up to GEN5 (np <= 5), where ymm11 remains available for the
 * fixed Q multiply-by-2 matrix.
 */
static __always_inline void raid_genX_avx2gfni_horner_raid(int nd, size_t size, void **vv, int np)
{
	uint8_t **v = (uint8_t **)vv;
	size_t i;
	int d, l;

	if (nd == 1) {
		for (d = 0; d < np; ++d)
			memcpy(v[1 + d], v[0], size);
		return;
	}

	l = nd - 1;

	raid_avx_begin();

	/* Fixed GF(2^8) multiply-by-2 matrix used by the Q Horner recurrence. */
	asm volatile ("vpbroadcastq %0,%%ymm11" : : "m" (raid_gfaffine_raid[2][0]));

	for (i = 0; i < size; i += 64) {
		/* Start P and Q from the last data disk, as in Horner evaluation. */
		asm volatile ("vmovdqa %0,%%ymm12" : : "m" (v[l][i]));
		asm volatile ("vmovdqa %0,%%ymm13" : : "m" (v[l][i + 32]));

		asm volatile ("vmovdqa %ymm12,%ymm0");
		asm volatile ("vmovdqa %ymm13,%ymm1");
		asm volatile ("vmovdqa %ymm12,%ymm2");
		asm volatile ("vmovdqa %ymm13,%ymm3");

		/*
		 * Initialize R/S/T with the contribution of the last disk.
		 * Unlike P and Q, their coefficients are not powers forming a
		 * fixed Horner recurrence.
		 */
		if (np >= 3) {
			asm volatile ("vpbroadcastq %0,%%ymm14" : : "m" (raid_gfaffine_raid[raid_gfcauchy_raid[2][l]][0]));
			asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm12,%ymm4");
			asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm13,%ymm5");
		}
		if (np >= 4) {
			asm volatile ("vpbroadcastq %0,%%ymm14" : : "m" (raid_gfaffine_raid[raid_gfcauchy_raid[3][l]][0]));
			asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm12,%ymm6");
			asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm13,%ymm7");
		}
		if (np >= 5) {
			asm volatile ("vpbroadcastq %0,%%ymm14" : : "m" (raid_gfaffine_raid[raid_gfcauchy_raid[4][l]][0]));
			asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm12,%ymm8");
			asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm13,%ymm9");
		}

		/* Process all remaining disks except D0. */
		for (d = l - 1; d >= 1; --d) {
			/* Q = 2 * Q. */
			asm volatile ("vgf2p8affineqb $0,%ymm11,%ymm2,%ymm2");
			asm volatile ("vgf2p8affineqb $0,%ymm11,%ymm3,%ymm3");

			asm volatile ("vmovdqa %0,%%ymm12" : : "m" (v[d][i]));
			asm volatile ("vmovdqa %0,%%ymm13" : : "m" (v[d][i + 32]));

			/* P ^= D[d], Q ^= D[d]. */
			asm volatile ("vpxor %ymm12,%ymm0,%ymm0");
			asm volatile ("vpxor %ymm13,%ymm1,%ymm1");
			asm volatile ("vpxor %ymm12,%ymm2,%ymm2");
			asm volatile ("vpxor %ymm13,%ymm3,%ymm3");

			if (np >= 3) {
				asm volatile ("vpbroadcastq %0,%%ymm14" : : "m" (raid_gfaffine_raid[raid_gfcauchy_raid[2][d]][0]));
				asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm12,%ymm15");
				asm volatile ("vpxor %ymm15,%ymm4,%ymm4");
				asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm13,%ymm15");
				asm volatile ("vpxor %ymm15,%ymm5,%ymm5");
			}
			if (np >= 4) {
				asm volatile ("vpbroadcastq %0,%%ymm14" : : "m" (raid_gfaffine_raid[raid_gfcauchy_raid[3][d]][0]));
				asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm12,%ymm15");
				asm volatile ("vpxor %ymm15,%ymm6,%ymm6");
				asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm13,%ymm15");
				asm volatile ("vpxor %ymm15,%ymm7,%ymm7");
			}
			if (np >= 5) {
				asm volatile ("vpbroadcastq %0,%%ymm14" : : "m" (raid_gfaffine_raid[raid_gfcauchy_raid[4][d]][0]));
				asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm12,%ymm15");
				asm volatile ("vpxor %ymm15,%ymm8,%ymm8");
				asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm13,%ymm15");
				asm volatile ("vpxor %ymm15,%ymm9,%ymm9");
			}
		}

		/*
		 * Final Horner step with D0.
		 *
		 * All Cauchy rows are normalized to coefficient 1 for D0,
		 * so R/S/T require only XOR and no affine multiplication.
		 */
		asm volatile ("vgf2p8affineqb $0,%ymm11,%ymm2,%ymm2");
		asm volatile ("vgf2p8affineqb $0,%ymm11,%ymm3,%ymm3");

		asm volatile ("vmovdqa %0,%%ymm12" : : "m" (v[0][i]));
		asm volatile ("vmovdqa %0,%%ymm13" : : "m" (v[0][i + 32]));

		asm volatile ("vpxor %ymm12,%ymm0,%ymm0");
		asm volatile ("vpxor %ymm13,%ymm1,%ymm1");
		asm volatile ("vpxor %ymm12,%ymm2,%ymm2");
		asm volatile ("vpxor %ymm13,%ymm3,%ymm3");

		if (np >= 3) {
			asm volatile ("vpxor %ymm12,%ymm4,%ymm4");
			asm volatile ("vpxor %ymm13,%ymm5,%ymm5");
		}
		if (np >= 4) {
			asm volatile ("vpxor %ymm12,%ymm6,%ymm6");
			asm volatile ("vpxor %ymm13,%ymm7,%ymm7");
		}
		if (np >= 5) {
			asm volatile ("vpxor %ymm12,%ymm8,%ymm8");
			asm volatile ("vpxor %ymm13,%ymm9,%ymm9");
		}

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
	}

	raid_avx_end();
}

/*
 * GENX AVX2 GFNI implementation
 */
static __always_inline void raid_genX_avx2gfni_raid(int nd, size_t size, void **vv, int np)
{
	uint8_t **v = (uint8_t **)vv;
	size_t i;
	int d;

	if (nd == 1) {
		for (d = 0; d < np; ++d)
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

		/* all other disks */
		for (d = 1; d < nd; ++d) {
			asm volatile ("vmovdqa %0,%%ymm12" : : "m" (v[d][i]));
			asm volatile ("vmovdqa %0,%%ymm13" : : "m" (v[d][i + 32]));

			asm volatile ("vpxor     %ymm12,%ymm0,%ymm0");
			asm volatile ("vpxor     %ymm13,%ymm1,%ymm1");

			asm volatile ("vpbroadcastq %0,%%ymm14" : : "m" (raid_gfaffine_raid[raid_gfcauchy_raid[1][d]][0]));
			asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm12,%ymm15");
			asm volatile ("vpxor    %ymm15,%ymm2,%ymm2");
			asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm13,%ymm15");
			asm volatile ("vpxor    %ymm15,%ymm3,%ymm3");
			if (np >= 3) {
				asm volatile ("vpbroadcastq %0,%%ymm14" : : "m" (raid_gfaffine_raid[raid_gfcauchy_raid[2][d]][0]));
				asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm12,%ymm15");
				asm volatile ("vpxor    %ymm15,%ymm4,%ymm4");
				asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm13,%ymm15");
				asm volatile ("vpxor    %ymm15,%ymm5,%ymm5");
			}
			if (np >= 4) {
				asm volatile ("vpbroadcastq %0,%%ymm14" : : "m" (raid_gfaffine_raid[raid_gfcauchy_raid[3][d]][0]));
				asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm12,%ymm15");
				asm volatile ("vpxor    %ymm15,%ymm6,%ymm6");
				asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm13,%ymm15");
				asm volatile ("vpxor    %ymm15,%ymm7,%ymm7");
			}
			if (np >= 5) {
				asm volatile ("vpbroadcastq %0,%%ymm14" : : "m" (raid_gfaffine_raid[raid_gfcauchy_raid[4][d]][0]));
				asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm12,%ymm15");
				asm volatile ("vpxor    %ymm15,%ymm8,%ymm8");
				asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm13,%ymm15");
				asm volatile ("vpxor    %ymm15,%ymm9,%ymm9");
			}
			if (np >= 6) {
				asm volatile ("vpbroadcastq %0,%%ymm14" : : "m" (raid_gfaffine_raid[raid_gfcauchy_raid[5][d]][0]));
				asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm12,%ymm15");
				asm volatile ("vpxor    %ymm15,%ymm10,%ymm10");
				asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm13,%ymm15");
				asm volatile ("vpxor    %ymm15,%ymm11,%ymm11");
			}
		}

		asm volatile ("vmovntdq  %%ymm0,%0" : "=m" (v[nd][i]));
		asm volatile ("vmovntdq  %%ymm1,%0" : "=m" (v[nd][i + 32]));
		asm volatile ("vmovntdq  %%ymm2,%0" : "=m" (v[nd + 1][i]));
		asm volatile ("vmovntdq  %%ymm3,%0" : "=m" (v[nd + 1][i + 32]));
		if (np >= 3) {
			asm volatile ("vmovntdq  %%ymm4,%0" : "=m" (v[nd + 2][i]));
			asm volatile ("vmovntdq  %%ymm5,%0" : "=m" (v[nd + 2][i + 32]));
		}
		if (np >= 4) {
			asm volatile ("vmovntdq  %%ymm6,%0" : "=m" (v[nd + 3][i]));
			asm volatile ("vmovntdq  %%ymm7,%0" : "=m" (v[nd + 3][i + 32]));
		}
		if (np >= 5) {
			asm volatile ("vmovntdq  %%ymm8,%0" : "=m" (v[nd + 4][i]));
			asm volatile ("vmovntdq  %%ymm9,%0" : "=m" (v[nd + 4][i + 32]));
		}
		if (np >= 6) {
			asm volatile ("vmovntdq  %%ymm10,%0" : "=m" (v[nd + 5][i]));
			asm volatile ("vmovntdq  %%ymm11,%0" : "=m" (v[nd + 5][i + 32]));
		}
	}

	raid_avx_end();
}

/*
 * GENX AVX512 GFNI implementation for the RAID polynomial.
 *
 * Q is evaluated with Horner's rule using the fixed multiply-by-2 affine
 * matrix. The remaining Cauchy parities use their per-disk coefficients.
 */
static __always_inline void raid_genX_avx512gfni_horner_raid(int nd, size_t size, void **vv, int np)
{
	uint8_t **v = (uint8_t **)vv;
	size_t i;
	int d, l;

	if (nd == 1) {
		for (d = 0; d < np; ++d)
			memcpy(v[1 + d], v[0], size);
		return;
	}

	l = nd - 1;

	raid_avx_begin();

	/* Fixed GF(2^8) multiply-by-2 matrix used by the Q Horner recurrence. */
	asm volatile ("vpbroadcastq %0,%%zmm7" : : "m" (raid_gfaffine_raid[2][0]));

	for (i = 0; i < size; i += 64) {
		/* Start P and Q from the last data disk. */
		asm volatile ("vmovdqa64 %0,%%zmm6" : : "m" (v[l][i]));
		asm volatile ("vmovdqa64 %zmm6,%zmm0");
		asm volatile ("vmovdqa64 %zmm6,%zmm1");

		/* Initialize R/S/T/U with the contribution of the last disk. */
		if (np >= 3) {
			asm volatile ("vpbroadcastq %0,%%zmm8" : : "m" (raid_gfaffine_raid[raid_gfcauchy_raid[2][l]][0]));
			asm volatile ("vgf2p8affineqb $0,%zmm8,%zmm6,%zmm2");
		}
		if (np >= 4) {
			asm volatile ("vpbroadcastq %0,%%zmm9" : : "m" (raid_gfaffine_raid[raid_gfcauchy_raid[3][l]][0]));
			asm volatile ("vgf2p8affineqb $0,%zmm9,%zmm6,%zmm3");
		}
		if (np >= 5) {
			asm volatile ("vpbroadcastq %0,%%zmm10" : : "m" (raid_gfaffine_raid[raid_gfcauchy_raid[4][l]][0]));
			asm volatile ("vgf2p8affineqb $0,%zmm10,%zmm6,%zmm4");
		}
		if (np >= 6) {
			asm volatile ("vpbroadcastq %0,%%zmm11" : : "m" (raid_gfaffine_raid[raid_gfcauchy_raid[5][l]][0]));
			asm volatile ("vgf2p8affineqb $0,%zmm11,%zmm6,%zmm5");
		}

		/* Process all remaining disks except D0. */
		for (d = l - 1; d >= 1; --d) {
			/* Q = 2 * Q. */
			asm volatile ("vgf2p8affineqb $0,%zmm7,%zmm1,%zmm1");

			asm volatile ("vmovdqa64 %0,%%zmm6" : : "m" (v[d][i]));

			/* P ^= D[d], Q ^= D[d]. */
			asm volatile ("vpxorq %zmm6,%zmm0,%zmm0");
			asm volatile ("vpxorq %zmm6,%zmm1,%zmm1");

			if (np >= 3)
				asm volatile ("vpbroadcastq %0,%%zmm8" : : "m" (raid_gfaffine_raid[raid_gfcauchy_raid[2][d]][0]));
			if (np >= 4)
				asm volatile ("vpbroadcastq %0,%%zmm9" : : "m" (raid_gfaffine_raid[raid_gfcauchy_raid[3][d]][0]));
			if (np >= 5)
				asm volatile ("vpbroadcastq %0,%%zmm10" : : "m" (raid_gfaffine_raid[raid_gfcauchy_raid[4][d]][0]));
			if (np >= 6)
				asm volatile ("vpbroadcastq %0,%%zmm11" : : "m" (raid_gfaffine_raid[raid_gfcauchy_raid[5][d]][0]));

			if (np >= 3) {
				asm volatile ("vgf2p8affineqb $0,%zmm8,%zmm6,%zmm12");
				asm volatile ("vpxorq %zmm12,%zmm2,%zmm2");
			}
			if (np >= 4) {
				asm volatile ("vgf2p8affineqb $0,%zmm9,%zmm6,%zmm13");
				asm volatile ("vpxorq %zmm13,%zmm3,%zmm3");
			}
			if (np >= 5) {
				asm volatile ("vgf2p8affineqb $0,%zmm10,%zmm6,%zmm14");
				asm volatile ("vpxorq %zmm14,%zmm4,%zmm4");
			}
			if (np >= 6) {
				asm volatile ("vgf2p8affineqb $0,%zmm11,%zmm6,%zmm15");
				asm volatile ("vpxorq %zmm15,%zmm5,%zmm5");
			}
		}

		/*
		 * Final Horner step with D0.
		 *
		 * All Cauchy rows have coefficient 1 for D0.
		 */
		asm volatile ("vgf2p8affineqb $0,%zmm7,%zmm1,%zmm1");

		asm volatile ("vmovdqa64 %0,%%zmm6" : : "m" (v[0][i]));

		asm volatile ("vpxorq %zmm6,%zmm0,%zmm0");
		asm volatile ("vpxorq %zmm6,%zmm1,%zmm1");

		if (np >= 3)
			asm volatile ("vpxorq %zmm6,%zmm2,%zmm2");
		if (np >= 4)
			asm volatile ("vpxorq %zmm6,%zmm3,%zmm3");
		if (np >= 5)
			asm volatile ("vpxorq %zmm6,%zmm4,%zmm4");
		if (np >= 6)
			asm volatile ("vpxorq %zmm6,%zmm5,%zmm5");

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
	}

	raid_avx_end();
}

/*
 * GENX AVX2 GFNI implementation
 */
static __always_inline void raid_genX_avx2gfni_aes(int nd, size_t size, void **vv, int np)
{
	uint8_t **v = (uint8_t **)vv;
	size_t i;
	int d;

	if (nd == 1) {
		for (d = 0; d < np; ++d)
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

		/* all other disks */
		for (d = 1; d < nd; ++d) {
			asm volatile ("vmovdqa %0,%%ymm12" : : "m" (v[d][i]));
			asm volatile ("vmovdqa %0,%%ymm13" : : "m" (v[d][i + 32]));

			asm volatile ("vpxor     %ymm12,%ymm0,%ymm0");
			asm volatile ("vpxor     %ymm13,%ymm1,%ymm1");

			asm volatile ("vpbroadcastb %0,%%ymm14" : : "m" (raid_gfcauchy[1][d]));
			asm volatile ("vgf2p8mulb %ymm12,%ymm14,%ymm15");
			asm volatile ("vpxor    %ymm15,%ymm2,%ymm2");
			asm volatile ("vgf2p8mulb %ymm13,%ymm14,%ymm15");
			asm volatile ("vpxor    %ymm15,%ymm3,%ymm3");
			if (np >= 3) {
				asm volatile ("vpbroadcastb %0,%%ymm14" : : "m" (raid_gfcauchy[2][d]));
				asm volatile ("vgf2p8mulb %ymm12,%ymm14,%ymm15");
				asm volatile ("vpxor    %ymm15,%ymm4,%ymm4");
				asm volatile ("vgf2p8mulb %ymm13,%ymm14,%ymm15");
				asm volatile ("vpxor    %ymm15,%ymm5,%ymm5");
			}
			if (np >= 4) {
				asm volatile ("vpbroadcastb %0,%%ymm14" : : "m" (raid_gfcauchy[3][d]));
				asm volatile ("vgf2p8mulb %ymm12,%ymm14,%ymm15");
				asm volatile ("vpxor    %ymm15,%ymm6,%ymm6");
				asm volatile ("vgf2p8mulb %ymm13,%ymm14,%ymm15");
				asm volatile ("vpxor    %ymm15,%ymm7,%ymm7");
			}
			if (np >= 5) {
				asm volatile ("vpbroadcastb %0,%%ymm14" : : "m" (raid_gfcauchy[4][d]));
				asm volatile ("vgf2p8mulb %ymm12,%ymm14,%ymm15");
				asm volatile ("vpxor    %ymm15,%ymm8,%ymm8");
				asm volatile ("vgf2p8mulb %ymm13,%ymm14,%ymm15");
				asm volatile ("vpxor    %ymm15,%ymm9,%ymm9");
			}
			if (np >= 6) {
				asm volatile ("vpbroadcastb %0,%%ymm14" : : "m" (raid_gfcauchy[5][d]));
				asm volatile ("vgf2p8mulb %ymm12,%ymm14,%ymm15");
				asm volatile ("vpxor    %ymm15,%ymm10,%ymm10");
				asm volatile ("vgf2p8mulb %ymm13,%ymm14,%ymm15");
				asm volatile ("vpxor    %ymm15,%ymm11,%ymm11");
			}
		}

		asm volatile ("vmovntdq  %%ymm0,%0" : "=m" (v[nd][i]));
		asm volatile ("vmovntdq  %%ymm1,%0" : "=m" (v[nd][i + 32]));
		asm volatile ("vmovntdq  %%ymm2,%0" : "=m" (v[nd + 1][i]));
		asm volatile ("vmovntdq  %%ymm3,%0" : "=m" (v[nd + 1][i + 32]));
		if (np >= 3) {
			asm volatile ("vmovntdq  %%ymm4,%0" : "=m" (v[nd + 2][i]));
			asm volatile ("vmovntdq  %%ymm5,%0" : "=m" (v[nd + 2][i + 32]));
		}
		if (np >= 4) {
			asm volatile ("vmovntdq  %%ymm6,%0" : "=m" (v[nd + 3][i]));
			asm volatile ("vmovntdq  %%ymm7,%0" : "=m" (v[nd + 3][i + 32]));
		}
		if (np >= 5) {
			asm volatile ("vmovntdq  %%ymm8,%0" : "=m" (v[nd + 4][i]));
			asm volatile ("vmovntdq  %%ymm9,%0" : "=m" (v[nd + 4][i + 32]));
		}
		if (np >= 6) {
			asm volatile ("vmovntdq  %%ymm10,%0" : "=m" (v[nd + 5][i]));
			asm volatile ("vmovntdq  %%ymm11,%0" : "=m" (v[nd + 5][i + 32]));
		}
	}

	raid_avx_end();
}

/*
 * GENX AVX512 GFNI implementation
 */
static __always_inline void raid_genX_avx512gfni_aes(int nd, size_t size, void **vv, int np)
{
	uint8_t **v = (uint8_t **)vv;
	size_t i;
	int d;

	if (nd == 1) {
		for (d = 0; d < np; ++d)
			memcpy(v[1 + d], v[0], size);
		return;
	}

	raid_avx_begin();

	for (i = 0; i < size; i += 64) {
		/* first disk with all coefficients at 1 */
		asm volatile ("vmovdqa64 %0,%%zmm0" : : "m" (v[0][i]));

		asm volatile ("vmovdqa64  %zmm0,%zmm1");
		if (np >= 3)
			asm volatile ("vmovdqa64  %zmm0,%zmm2");
		if (np >= 4)
			asm volatile ("vmovdqa64  %zmm0,%zmm3");
		if (np >= 5)
			asm volatile ("vmovdqa64  %zmm0,%zmm4");
		if (np >= 6)
			asm volatile ("vmovdqa64  %zmm0,%zmm5");

		/* all other disks */
		for (d = 1; d < nd; ++d) {
			asm volatile ("vmovdqa64 %0,%%zmm6" : : "m" (v[d][i]));

			asm volatile ("vpbroadcastb %0,%%zmm7" : : "m" (raid_gfcauchy[1][d]));
			if (np >= 3)
				asm volatile ("vpbroadcastb %0,%%zmm8" : : "m" (raid_gfcauchy[2][d]));
			if (np >= 4)
				asm volatile ("vpbroadcastb %0,%%zmm9" : : "m" (raid_gfcauchy[3][d]));
			if (np >= 5)
				asm volatile ("vpbroadcastb %0,%%zmm10" : : "m" (raid_gfcauchy[4][d]));
			if (np >= 6)
				asm volatile ("vpbroadcastb %0,%%zmm11" : : "m" (raid_gfcauchy[5][d]));

			asm volatile ("vgf2p8mulb %zmm6,%zmm7,%zmm12");
			if (np >= 3)
				asm volatile ("vgf2p8mulb %zmm6,%zmm8,%zmm13");
			if (np >= 4)
				asm volatile ("vgf2p8mulb %zmm6,%zmm9,%zmm14");
			if (np >= 5)
				asm volatile ("vgf2p8mulb %zmm6,%zmm10,%zmm15");
			if (np >= 6)
				asm volatile ("vgf2p8mulb %zmm6,%zmm11,%zmm16");

			asm volatile ("vpxorq     %zmm6,%zmm0,%zmm0");
			asm volatile ("vpxorq    %zmm12,%zmm1,%zmm1");
			if (np >= 3)
				asm volatile ("vpxorq    %zmm13,%zmm2,%zmm2");
			if (np >= 4)
				asm volatile ("vpxorq    %zmm14,%zmm3,%zmm3");
			if (np >= 5)
				asm volatile ("vpxorq    %zmm15,%zmm4,%zmm4");
			if (np >= 6)
				asm volatile ("vpxorq    %zmm16,%zmm5,%zmm5");
		}

		asm volatile ("vmovntdq  %%zmm0,%0" : "=m" (v[nd][i]));
		asm volatile ("vmovntdq  %%zmm1,%0" : "=m" (v[nd + 1][i]));
		if (np >= 3)
			asm volatile ("vmovntdq  %%zmm2,%0" : "=m" (v[nd + 2][i]));
		if (np >= 4)
			asm volatile ("vmovntdq  %%zmm3,%0" : "=m" (v[nd + 3][i]));
		if (np >= 5)
			asm volatile ("vmovntdq  %%zmm4,%0" : "=m" (v[nd + 4][i]));
		if (np >= 6)
			asm volatile ("vmovntdq  %%zmm5,%0" : "=m" (v[nd + 5][i]));
	}

	raid_avx_end();
}

/*
 * GEN2 (RAID6 with powers of 2) AVX2 GFNI implementation
 */
void raid_gen2_avx2gfni_raid(int nd, size_t size, void **vv)
{
	raid_genX_avx2gfni_horner_raid(nd, size, vv, 2);
}

/*
 * GEN2 (RAID6 with powers of 2) GFNI implementation
 */
void raid_gen2_avx512gfni_raid(int nd, size_t size, void **vv)
{
	raid_genX_avx512gfni_horner_raid(nd, size, vv, 2);
}

/*
 * GEN3 (triple parity with Cauchy matrix) AVX2 GFNI implementation
 */
void raid_gen3_avx2gfni_raid(int nd, size_t size, void **vv)
{
	raid_genX_avx2gfni_horner_raid(nd, size, vv, 3);
}

/*
 * GEN3 (triple parity with Cauchy matrix) GFNI implementation
 */
void raid_gen3_avx512gfni_raid(int nd, size_t size, void **vv)
{
	raid_genX_avx512gfni_horner_raid(nd, size, vv, 3);
}

/*
 * GEN4 (quad parity with Cauchy matrix) AVX2 GFNI implementation
 */
void raid_gen4_avx2gfni_raid(int nd, size_t size, void **vv)
{
	raid_genX_avx2gfni_horner_raid(nd, size, vv, 4);
}

/*
 * GEN4 (quad parity with Cauchy matrix) GFNI implementation
 */
void raid_gen4_avx512gfni_raid(int nd, size_t size, void **vv)
{
	raid_genX_avx512gfni_horner_raid(nd, size, vv, 4);
}

/*
 * GEN5 (penta parity with Cauchy matrix) AVX2 GFNI implementation
 */
void raid_gen5_avx2gfni_raid(int nd, size_t size, void **vv)
{
	raid_genX_avx2gfni_horner_raid(nd, size, vv, 5);
}

/*
 * GEN5 (penta parity with Cauchy matrix) GFNI implementation
 */
void raid_gen5_avx512gfni_raid(int nd, size_t size, void **vv)
{
	raid_genX_avx512gfni_horner_raid(nd, size, vv, 5);
}

/*
 * GEN6 (hexa parity with Cauchy matrix) AVX2 GFNI implementation
 */
void raid_gen6_avx2gfni_raid(int nd, size_t size, void **vv)
{
	raid_genX_avx2gfni_raid(nd, size, vv, 6);
}

/*
 * GEN6 (hexa parity with Cauchy matrix) GFNI implementation
 */
void raid_gen6_avx512gfni_raid(int nd, size_t size, void **vv)
{
	raid_genX_avx512gfni_horner_raid(nd, size, vv, 6);
}

/*
 * GEN2 Cauchy AVX2 GFNI implementation
 */
void raid_gen2_avx2gfni_aes(int nd, size_t size, void **vv)
{
	raid_genX_avx2gfni_aes(nd, size, vv, 2);
}

/*
 * GEN2 Cauchy GFNI implementation
 */
void raid_gen2_avx512gfni_aes(int nd, size_t size, void **vv)
{
	raid_genX_avx512gfni_aes(nd, size, vv, 2);
}

/*
 * GEN3 (triple parity with Cauchy matrix) AVX2 GFNI implementation
 */
void raid_gen3_avx2gfni_aes(int nd, size_t size, void **vv)
{
	raid_genX_avx2gfni_aes(nd, size, vv, 3);
}

/*
 * GEN3 (triple parity with Cauchy matrix) GFNI implementation
 */
void raid_gen3_avx512gfni_aes(int nd, size_t size, void **vv)
{
	raid_genX_avx512gfni_aes(nd, size, vv, 3);
}

/*
 * GEN4 (quad parity with Cauchy matrix) AVX2 GFNI implementation
 */
void raid_gen4_avx2gfni_aes(int nd, size_t size, void **vv)
{
	raid_genX_avx2gfni_aes(nd, size, vv, 4);
}

/*
 * GEN4 (quad parity with Cauchy matrix) GFNI implementation
 */
void raid_gen4_avx512gfni_aes(int nd, size_t size, void **vv)
{
	raid_genX_avx512gfni_aes(nd, size, vv, 4);
}

/*
 * GEN5 (penta parity with Cauchy matrix) AVX2 GFNI implementation
 */
void raid_gen5_avx2gfni_aes(int nd, size_t size, void **vv)
{
	raid_genX_avx2gfni_aes(nd, size, vv, 5);
}

/*
 * GEN5 (penta parity with Cauchy matrix) GFNI implementation
 */
void raid_gen5_avx512gfni_aes(int nd, size_t size, void **vv)
{
	raid_genX_avx512gfni_aes(nd, size, vv, 5);
}

/*
 * GEN6 (hexa parity with Cauchy matrix) AVX2 GFNI implementation
 */
void raid_gen6_avx2gfni_aes(int nd, size_t size, void **vv)
{
	raid_genX_avx2gfni_aes(nd, size, vv, 6);
}

/*
 * GEN6 (hexa parity with Cauchy matrix) GFNI implementation
 */
void raid_gen6_avx512gfni_aes(int nd, size_t size, void **vv)
{
	raid_genX_avx512gfni_aes(nd, size, vv, 6);
}

/*
 * RAID recovering for one disk AVX2 GFNI implementation
 */
void raid_rec1_avx2gfni_raid(int nr, int *id, int *ip, int nd, size_t size, void **vv)
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

	asm volatile ("vpbroadcastq %0,%%ymm4" : : "m" (raid_gfaffine_raid[V][0]));

	for (i = 0; i < size; i += 64) {
		asm volatile ("vmovdqa %0,%%ymm0" : : "m" (p[i]));
		asm volatile ("vmovdqa %0,%%ymm1" : : "m" (p[i + 32]));

		asm volatile ("vmovdqa %0,%%ymm2" : : "m" (pa[i]));
		asm volatile ("vmovdqa %0,%%ymm3" : : "m" (pa[i + 32]));

		asm volatile ("vpxor    %ymm2,%ymm0,%ymm0");
		asm volatile ("vpxor    %ymm3,%ymm1,%ymm1");

		asm volatile ("vgf2p8affineqb $0,%ymm4,%ymm0,%ymm0");
		asm volatile ("vgf2p8affineqb $0,%ymm4,%ymm1,%ymm1");

		asm volatile ("vmovdqa %%ymm0,%0" : "=m" (pa[i]));
		asm volatile ("vmovdqa %%ymm1,%0" : "=m" (pa[i + 32]));
	}

	raid_avx_end();
}

/*
 * RAID recovering for one disk GFNI implementation
 */
void raid_rec1_avx512gfni_raid(int nr, int *id, int *ip, int nd, size_t size, void **vv)
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

	asm volatile ("vpbroadcastq %0,%%zmm4" : : "m" (raid_gfaffine_raid[V][0]));

	for (i = 0; i < size; i += 64) {
		asm volatile ("vmovdqa64 %0,%%zmm0" : : "m" (p[i]));
		asm volatile ("vmovdqa64 %0,%%zmm1" : : "m" (pa[i]));
		asm volatile ("vpxorq    %zmm1,%zmm0,%zmm0");
		asm volatile ("vgf2p8affineqb $0,%zmm4,%zmm0,%zmm0");
		asm volatile ("vmovdqa64 %%zmm0,%0" : "=m" (pa[i]));
	}

	raid_avx_end();
}

/*
 * RAID recovering for two disks AVX2 GFNI implementation
 */
void raid_rec2_avx2gfni_raid(int nr, int *id, int *ip, int nd, size_t size, void **vv)
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
	asm volatile ("vpbroadcastq %0,%%ymm10" : : "m" (raid_gfaffine_raid[V[0]][0]));
	asm volatile ("vpbroadcastq %0,%%ymm11" : : "m" (raid_gfaffine_raid[V[1]][0]));
	asm volatile ("vpbroadcastq %0,%%ymm12" : : "m" (raid_gfaffine_raid[V[2]][0]));
	asm volatile ("vpbroadcastq %0,%%ymm13" : : "m" (raid_gfaffine_raid[V[3]][0]));

	for (i = 0; i < size; i += 64) {
		asm volatile ("vmovdqa %0,%%ymm0" : : "m" (p[0][i]));
		asm volatile ("vmovdqa %0,%%ymm1" : : "m" (p[0][i + 32]));

		asm volatile ("vmovdqa %0,%%ymm4" : : "m" (pa[0][i]));
		asm volatile ("vmovdqa %0,%%ymm5" : : "m" (pa[0][i + 32]));

		asm volatile ("vpxor    %ymm4,%ymm0,%ymm0");
		asm volatile ("vpxor    %ymm5,%ymm1,%ymm1");

		asm volatile ("vmovdqa %0,%%ymm2" : : "m" (p[1][i]));
		asm volatile ("vmovdqa %0,%%ymm3" : : "m" (p[1][i + 32]));

		asm volatile ("vmovdqa %0,%%ymm6" : : "m" (pa[1][i]));
		asm volatile ("vmovdqa %0,%%ymm7" : : "m" (pa[1][i + 32]));

		asm volatile ("vpxor    %ymm6,%ymm2,%ymm2");
		asm volatile ("vpxor    %ymm7,%ymm3,%ymm3");

		asm volatile ("vgf2p8affineqb $0,%ymm10,%ymm0,%ymm4");
		asm volatile ("vgf2p8affineqb $0,%ymm10,%ymm1,%ymm5");

		asm volatile ("vgf2p8affineqb $0,%ymm11,%ymm2,%ymm9");
		asm volatile ("vpxor    %ymm9,%ymm4,%ymm4");
		asm volatile ("vgf2p8affineqb $0,%ymm11,%ymm3,%ymm9");
		asm volatile ("vpxor    %ymm9,%ymm5,%ymm5");

		asm volatile ("vmovdqa %%ymm4,%0" : "=m" (pa[0][i]));
		asm volatile ("vmovdqa %%ymm5,%0" : "=m" (pa[0][i + 32]));

		asm volatile ("vgf2p8affineqb $0,%ymm12,%ymm0,%ymm6");
		asm volatile ("vgf2p8affineqb $0,%ymm12,%ymm1,%ymm7");

		asm volatile ("vgf2p8affineqb $0,%ymm13,%ymm2,%ymm9");
		asm volatile ("vpxor    %ymm9,%ymm6,%ymm6");
		asm volatile ("vgf2p8affineqb $0,%ymm13,%ymm3,%ymm9");
		asm volatile ("vpxor    %ymm9,%ymm7,%ymm7");

		asm volatile ("vmovdqa %%ymm6,%0" : "=m" (pa[1][i]));
		asm volatile ("vmovdqa %%ymm7,%0" : "=m" (pa[1][i + 32]));
	}

	raid_avx_end();
}

/*
 * RAID recovering for two disks GFNI implementation
 */
void raid_rec2_avx512gfni_raid(int nr, int *id, int *ip, int nd, size_t size, void **vv)
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
	asm volatile ("vpbroadcastq %0,%%zmm8" : : "m" (raid_gfaffine_raid[V[0]][0]));
	asm volatile ("vpbroadcastq %0,%%zmm9" : : "m" (raid_gfaffine_raid[V[1]][0]));
	asm volatile ("vpbroadcastq %0,%%zmm10" : : "m" (raid_gfaffine_raid[V[2]][0]));
	asm volatile ("vpbroadcastq %0,%%zmm11" : : "m" (raid_gfaffine_raid[V[3]][0]));

	for (i = 0; i < size; i += 64) {
		asm volatile ("vmovdqa64 %0,%%zmm0" : : "m" (p[0][i]));
		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (pa[0][i]));
		asm volatile ("vmovdqa64 %0,%%zmm1" : : "m" (p[1][i]));
		asm volatile ("vmovdqa64 %0,%%zmm3" : : "m" (pa[1][i]));
		asm volatile ("vpxorq    %zmm2,%zmm0,%zmm0");
		asm volatile ("vpxorq    %zmm3,%zmm1,%zmm1");

		asm volatile ("vpxorq    %zmm6,%zmm6,%zmm6");

		asm volatile ("vgf2p8affineqb $0,%zmm8,%zmm0,%zmm2");
		asm volatile ("vpxorq    %zmm2,%zmm6,%zmm6");

		asm volatile ("vgf2p8affineqb $0,%zmm9,%zmm1,%zmm3");
		asm volatile ("vpxorq    %zmm3,%zmm6,%zmm6");

		asm volatile ("vmovdqa64 %%zmm6,%0" : "=m" (pa[0][i]));

		asm volatile ("vpxorq    %zmm6,%zmm6,%zmm6");

		asm volatile ("vgf2p8affineqb $0,%zmm10,%zmm0,%zmm2");
		asm volatile ("vpxorq    %zmm2,%zmm6,%zmm6");

		asm volatile ("vgf2p8affineqb $0,%zmm11,%zmm1,%zmm3");
		asm volatile ("vpxorq    %zmm3,%zmm6,%zmm6");

		asm volatile ("vmovdqa64 %%zmm6,%0" : "=m" (pa[1][i]));
	}

	raid_avx_end();
}

/*
 * RAID recovering AVX2 GFNI implementation
 */
void raid_recX_avx2gfni_raid(int nr, int *id, int *ip, int nd, size_t size, void **vv)
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

	raid_avx_begin();

	for (i = 0; i < size; i += 64) {
		/* delta */
		asm volatile (
			"movq 0(%2), %%rax\n"
			"movq 0(%3), %%rbx\n"
			"vmovdqa 0(%%rax, %1), %%ymm0\n"
			"vmovdqa 32(%%rax, %1), %%ymm1\n"
			"vmovdqa 0(%%rbx, %1), %%ymm14\n"
			"vmovdqa 32(%%rbx, %1), %%ymm15\n"
			"vpxor %%ymm14, %%ymm0, %%ymm0\n"
			"vpxor %%ymm15, %%ymm1, %%ymm1\n"
			"cmpq $1, %0\n"
			"jbe 1f\n"

			"movq 8(%2), %%rax\n"
			"movq 8(%3), %%rbx\n"
			"vmovdqa 0(%%rax, %1), %%ymm2\n"
			"vmovdqa 32(%%rax, %1), %%ymm3\n"
			"vmovdqa 0(%%rbx, %1), %%ymm14\n"
			"vmovdqa 32(%%rbx, %1), %%ymm15\n"
			"vpxor %%ymm14, %%ymm2, %%ymm2\n"
			"vpxor %%ymm15, %%ymm3, %%ymm3\n"
			"cmpq $2, %0\n"
			"jbe 1f\n"

			"movq 16(%2), %%rax\n"
			"movq 16(%3), %%rbx\n"
			"vmovdqa 0(%%rax, %1), %%ymm4\n"
			"vmovdqa 32(%%rax, %1), %%ymm5\n"
			"vmovdqa 0(%%rbx, %1), %%ymm14\n"
			"vmovdqa 32(%%rbx, %1), %%ymm15\n"
			"vpxor %%ymm14, %%ymm4, %%ymm4\n"
			"vpxor %%ymm15, %%ymm5, %%ymm5\n"
			"cmpq $3, %0\n"
			"jbe 1f\n"

			"movq 24(%2), %%rax\n"
			"movq 24(%3), %%rbx\n"
			"vmovdqa 0(%%rax, %1), %%ymm6\n"
			"vmovdqa 32(%%rax, %1), %%ymm7\n"
			"vmovdqa 0(%%rbx, %1), %%ymm14\n"
			"vmovdqa 32(%%rbx, %1), %%ymm15\n"
			"vpxor %%ymm14, %%ymm6, %%ymm6\n"
			"vpxor %%ymm15, %%ymm7, %%ymm7\n"
			"cmpq $4, %0\n"
			"jbe 1f\n"

			"movq 32(%2), %%rax\n"
			"movq 32(%3), %%rbx\n"
			"vmovdqa 0(%%rax, %1), %%ymm8\n"
			"vmovdqa 32(%%rax, %1), %%ymm9\n"
			"vmovdqa 0(%%rbx, %1), %%ymm14\n"
			"vmovdqa 32(%%rbx, %1), %%ymm15\n"
			"vpxor %%ymm14, %%ymm8, %%ymm8\n"
			"vpxor %%ymm15, %%ymm9, %%ymm9\n"
			"cmpq $5, %0\n"
			"jbe 1f\n"

			"movq 40(%2), %%rax\n"
			"movq 40(%3), %%rbx\n"
			"vmovdqa 0(%%rax, %1), %%ymm10\n"
			"vmovdqa 32(%%rax, %1), %%ymm11\n"
			"vmovdqa 0(%%rbx, %1), %%ymm14\n"
			"vmovdqa 32(%%rbx, %1), %%ymm15\n"
			"vpxor %%ymm14, %%ymm10, %%ymm10\n"
			"vpxor %%ymm15, %%ymm11, %%ymm11\n"

			"1:\n"
			:
			: "r" ((uint64_t)N), "r" (i), "r" (p), "r" (pa)
			: "rax", "rbx", "cc", "memory"
		);

		/* reconstruct */
		for (j = 0; j < N; ++j) {
			asm volatile (
				"movzbq 0(%2), %%rcx\n"
				"vpbroadcastq (%4, %%rcx, 8), %%ymm14\n"
				"vgf2p8affineqb $0, %%ymm14, %%ymm0, %%ymm12\n"
				"vgf2p8affineqb $0, %%ymm14, %%ymm1, %%ymm13\n"
				"cmpq $1, %0\n"
				"jbe 1f\n"

				"movzbq 1(%2), %%rcx\n"
				"vpbroadcastq (%4, %%rcx, 8), %%ymm14\n"
				"vgf2p8affineqb $0, %%ymm14, %%ymm2, %%ymm15\n"
				"vpxor %%ymm15, %%ymm12, %%ymm12\n"
				"vgf2p8affineqb $0, %%ymm14, %%ymm3, %%ymm15\n"
				"vpxor %%ymm15, %%ymm13, %%ymm13\n"
				"cmpq $2, %0\n"
				"jbe 1f\n"

				"movzbq 2(%2), %%rcx\n"
				"vpbroadcastq (%4, %%rcx, 8), %%ymm14\n"
				"vgf2p8affineqb $0, %%ymm14, %%ymm4, %%ymm15\n"
				"vpxor %%ymm15, %%ymm12, %%ymm12\n"
				"vgf2p8affineqb $0, %%ymm14, %%ymm5, %%ymm15\n"
				"vpxor %%ymm15, %%ymm13, %%ymm13\n"
				"cmpq $3, %0\n"
				"jbe 1f\n"

				"movzbq 3(%2), %%rcx\n"
				"vpbroadcastq (%4, %%rcx, 8), %%ymm14\n"
				"vgf2p8affineqb $0, %%ymm14, %%ymm6, %%ymm15\n"
				"vpxor %%ymm15, %%ymm12, %%ymm12\n"
				"vgf2p8affineqb $0, %%ymm14, %%ymm7, %%ymm15\n"
				"vpxor %%ymm15, %%ymm13, %%ymm13\n"
				"cmpq $4, %0\n"
				"jbe 1f\n"

				"movzbq 4(%2), %%rcx\n"
				"vpbroadcastq (%4, %%rcx, 8), %%ymm14\n"
				"vgf2p8affineqb $0, %%ymm14, %%ymm8, %%ymm15\n"
				"vpxor %%ymm15, %%ymm12, %%ymm12\n"
				"vgf2p8affineqb $0, %%ymm14, %%ymm9, %%ymm15\n"
				"vpxor %%ymm15, %%ymm13, %%ymm13\n"
				"cmpq $5, %0\n"
				"jbe 1f\n"

				"movzbq 5(%2), %%rcx\n"
				"vpbroadcastq (%4, %%rcx, 8), %%ymm14\n"
				"vgf2p8affineqb $0, %%ymm14, %%ymm10, %%ymm15\n"
				"vpxor %%ymm15, %%ymm12, %%ymm12\n"
				"vgf2p8affineqb $0, %%ymm14, %%ymm11, %%ymm15\n"
				"vpxor %%ymm15, %%ymm13, %%ymm13\n"

				"1:\n"
				"movq %3, %%rax\n"
				"vmovdqa %%ymm12, 0(%%rax, %1)\n"
				"vmovdqa %%ymm13, 32(%%rax, %1)\n"
				:
				: "r" ((uint64_t)N), "r" (i), "r" (&V[j * N]), "r" (pa[j]), "r" (raid_gfaffine_raid)
				: "rax", "rcx", "cc", "memory"
			);
		}
	}

	raid_avx_end();
}

/*
 * RAID recovering GFNI implementation
 */
void raid_recX_avx512gfni_raid(int nr, int *id, int *ip, int nd, size_t size, void **vv)
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

	raid_avx_begin();

	for (i = 0; i < size; i += 64) {
		/* delta */
		asm volatile (
			"movq 0(%2), %%rax\n"
			"movq 0(%3), %%rbx\n"
			"vmovdqa64 (%%rax, %1), %%zmm0\n"
			"vmovdqa64 (%%rbx, %1), %%zmm6\n"
			"vpxorq %%zmm6, %%zmm0, %%zmm0\n"
			"cmpq $1, %0\n"
			"jbe 1f\n"

			"movq 8(%2), %%rax\n"
			"movq 8(%3), %%rbx\n"
			"vmovdqa64 (%%rax, %1), %%zmm1\n"
			"vmovdqa64 (%%rbx, %1), %%zmm6\n"
			"vpxorq %%zmm6, %%zmm1, %%zmm1\n"
			"cmpq $2, %0\n"
			"jbe 1f\n"

			"movq 16(%2), %%rax\n"
			"movq 16(%3), %%rbx\n"
			"vmovdqa64 (%%rax, %1), %%zmm2\n"
			"vmovdqa64 (%%rbx, %1), %%zmm6\n"
			"vpxorq %%zmm6, %%zmm2, %%zmm2\n"
			"cmpq $3, %0\n"
			"jbe 1f\n"

			"movq 24(%2), %%rax\n"
			"movq 24(%3), %%rbx\n"
			"vmovdqa64 (%%rax, %1), %%zmm3\n"
			"vmovdqa64 (%%rbx, %1), %%zmm6\n"
			"vpxorq %%zmm6, %%zmm3, %%zmm3\n"
			"cmpq $4, %0\n"
			"jbe 1f\n"

			"movq 32(%2), %%rax\n"
			"movq 32(%3), %%rbx\n"
			"vmovdqa64 (%%rax, %1), %%zmm4\n"
			"vmovdqa64 (%%rbx, %1), %%zmm6\n"
			"vpxorq %%zmm6, %%zmm4, %%zmm4\n"
			"cmpq $5, %0\n"
			"jbe 1f\n"

			"movq 40(%2), %%rax\n"
			"movq 40(%3), %%rbx\n"
			"vmovdqa64 (%%rax, %1), %%zmm5\n"
			"vmovdqa64 (%%rbx, %1), %%zmm6\n"
			"vpxorq %%zmm6, %%zmm5, %%zmm5\n"

			"1:\n"
			:
			: "r" ((uint64_t)N), "r" (i), "r" (p), "r" (pa)
			: "rax", "rbx", "cc", "memory"
		);

		/* reconstruct */
		for (j = 0; j < N; ++j) {
			asm volatile (
				"movzbq 0(%2), %%rcx\n"
				"vpbroadcastq (%4, %%rcx, 8), %%zmm6\n"
				"vgf2p8affineqb $0, %%zmm6, %%zmm0, %%zmm7\n"
				"cmpq $1, %0\n"
				"jbe 1f\n"

				"movzbq 1(%2), %%rcx\n"
				"vpbroadcastq (%4, %%rcx, 8), %%zmm6\n"
				"vgf2p8affineqb $0, %%zmm6, %%zmm1, %%zmm6\n"
				"vpxorq %%zmm6, %%zmm7, %%zmm7\n"
				"cmpq $2, %0\n"
				"jbe 1f\n"

				"movzbq 2(%2), %%rcx\n"
				"vpbroadcastq (%4, %%rcx, 8), %%zmm6\n"
				"vgf2p8affineqb $0, %%zmm6, %%zmm2, %%zmm6\n"
				"vpxorq %%zmm6, %%zmm7, %%zmm7\n"
				"cmpq $3, %0\n"
				"jbe 1f\n"

				"movzbq 3(%2), %%rcx\n"
				"vpbroadcastq (%4, %%rcx, 8), %%zmm6\n"
				"vgf2p8affineqb $0, %%zmm6, %%zmm3, %%zmm6\n"
				"vpxorq %%zmm6, %%zmm7, %%zmm7\n"
				"cmpq $4, %0\n"
				"jbe 1f\n"

				"movzbq 4(%2), %%rcx\n"
				"vpbroadcastq (%4, %%rcx, 8), %%zmm6\n"
				"vgf2p8affineqb $0, %%zmm6, %%zmm4, %%zmm6\n"
				"vpxorq %%zmm6, %%zmm7, %%zmm7\n"
				"cmpq $5, %0\n"
				"jbe 1f\n"

				"movzbq 5(%2), %%rcx\n"
				"vpbroadcastq (%4, %%rcx, 8), %%zmm6\n"
				"vgf2p8affineqb $0, %%zmm6, %%zmm5, %%zmm6\n"
				"vpxorq %%zmm6, %%zmm7, %%zmm7\n"

				"1:\n"
				"movq %3, %%rax\n"
				"vmovdqa64 %%zmm7, (%%rax, %1)\n"
				:
				: "r" ((uint64_t)N), "r" (i), "r" (&V[j * N]), "r" (pa[j]), "r" (raid_gfaffine_raid)
				: "rax", "rcx", "cc", "memory"
			);
		}
	}

	raid_avx_end();
}

/*
 * AES recovering for one disk AVX2 GFNI implementation
 */
void raid_rec1_avx2gfni_aes(int nr, int *id, int *ip, int nd, size_t size, void **vv)
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

	asm volatile ("vpbroadcastb %0,%%ymm4" : : "m" (V));

	for (i = 0; i < size; i += 64) {
		asm volatile ("vmovdqa %0,%%ymm0" : : "m" (p[i]));
		asm volatile ("vmovdqa %0,%%ymm1" : : "m" (p[i + 32]));

		asm volatile ("vmovdqa %0,%%ymm2" : : "m" (pa[i]));
		asm volatile ("vmovdqa %0,%%ymm3" : : "m" (pa[i + 32]));

		asm volatile ("vpxor    %ymm2,%ymm0,%ymm0");
		asm volatile ("vpxor    %ymm3,%ymm1,%ymm1");

		asm volatile ("vgf2p8mulb %ymm4,%ymm0,%ymm0");
		asm volatile ("vgf2p8mulb %ymm4,%ymm1,%ymm1");

		asm volatile ("vmovdqa %%ymm0,%0" : "=m" (pa[i]));
		asm volatile ("vmovdqa %%ymm1,%0" : "=m" (pa[i + 32]));
	}

	raid_avx_end();
}

/*
 * AES recovering for one disk GFNI implementation
 */
void raid_rec1_avx512gfni_aes(int nr, int *id, int *ip, int nd, size_t size, void **vv)
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

	asm volatile ("vpbroadcastb %0,%%zmm4" : : "m" (V));

	for (i = 0; i < size; i += 64) {
		asm volatile ("vmovdqa64 %0,%%zmm0" : : "m" (p[i]));
		asm volatile ("vmovdqa64 %0,%%zmm1" : : "m" (pa[i]));
		asm volatile ("vpxorq    %zmm1,%zmm0,%zmm0");
		asm volatile ("vgf2p8mulb %zmm4,%zmm0,%zmm0");
		asm volatile ("vmovdqa64 %%zmm0,%0" : "=m" (pa[i]));
	}

	raid_avx_end();
}

/*
 * AES recovering for two disks AVX2 GFNI implementation
 */
void raid_rec2_avx2gfni_aes(int nr, int *id, int *ip, int nd, size_t size, void **vv)
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
	asm volatile ("vpbroadcastb %0,%%ymm10" : : "m" (V[0]));
	asm volatile ("vpbroadcastb %0,%%ymm11" : : "m" (V[1]));
	asm volatile ("vpbroadcastb %0,%%ymm12" : : "m" (V[2]));
	asm volatile ("vpbroadcastb %0,%%ymm13" : : "m" (V[3]));

	for (i = 0; i < size; i += 64) {
		asm volatile ("vmovdqa %0,%%ymm0" : : "m" (p[0][i]));
		asm volatile ("vmovdqa %0,%%ymm1" : : "m" (p[0][i + 32]));

		asm volatile ("vmovdqa %0,%%ymm4" : : "m" (pa[0][i]));
		asm volatile ("vmovdqa %0,%%ymm5" : : "m" (pa[0][i + 32]));

		asm volatile ("vpxor    %ymm4,%ymm0,%ymm0");
		asm volatile ("vpxor    %ymm5,%ymm1,%ymm1");

		asm volatile ("vmovdqa %0,%%ymm2" : : "m" (p[1][i]));
		asm volatile ("vmovdqa %0,%%ymm3" : : "m" (p[1][i + 32]));

		asm volatile ("vmovdqa %0,%%ymm6" : : "m" (pa[1][i]));
		asm volatile ("vmovdqa %0,%%ymm7" : : "m" (pa[1][i + 32]));

		asm volatile ("vpxor    %ymm6,%ymm2,%ymm2");
		asm volatile ("vpxor    %ymm7,%ymm3,%ymm3");

		asm volatile ("vgf2p8mulb %ymm0,%ymm10,%ymm4");
		asm volatile ("vgf2p8mulb %ymm1,%ymm10,%ymm5");

		asm volatile ("vgf2p8mulb %ymm2,%ymm11,%ymm9");
		asm volatile ("vpxor    %ymm9,%ymm4,%ymm4");
		asm volatile ("vgf2p8mulb %ymm3,%ymm11,%ymm9");
		asm volatile ("vpxor    %ymm9,%ymm5,%ymm5");

		asm volatile ("vmovdqa %%ymm4,%0" : "=m" (pa[0][i]));
		asm volatile ("vmovdqa %%ymm5,%0" : "=m" (pa[0][i + 32]));

		asm volatile ("vgf2p8mulb %ymm0,%ymm12,%ymm6");
		asm volatile ("vgf2p8mulb %ymm1,%ymm12,%ymm7");

		asm volatile ("vgf2p8mulb %ymm2,%ymm13,%ymm9");
		asm volatile ("vpxor    %ymm9,%ymm6,%ymm6");
		asm volatile ("vgf2p8mulb %ymm3,%ymm13,%ymm9");
		asm volatile ("vpxor    %ymm9,%ymm7,%ymm7");

		asm volatile ("vmovdqa %%ymm6,%0" : "=m" (pa[1][i]));
		asm volatile ("vmovdqa %%ymm7,%0" : "=m" (pa[1][i + 32]));
	}

	raid_avx_end();
}

/*
 * AES recovering for two disks GFNI implementation
 */
void raid_rec2_avx512gfni_aes(int nr, int *id, int *ip, int nd, size_t size, void **vv)
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
	asm volatile ("vpbroadcastb %0,%%zmm8" : : "m" (V[0]));
	asm volatile ("vpbroadcastb %0,%%zmm9" : : "m" (V[1]));
	asm volatile ("vpbroadcastb %0,%%zmm10" : : "m" (V[2]));
	asm volatile ("vpbroadcastb %0,%%zmm11" : : "m" (V[3]));

	for (i = 0; i < size; i += 64) {
		asm volatile ("vmovdqa64 %0,%%zmm0" : : "m" (p[0][i]));
		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (pa[0][i]));
		asm volatile ("vmovdqa64 %0,%%zmm1" : : "m" (p[1][i]));
		asm volatile ("vmovdqa64 %0,%%zmm3" : : "m" (pa[1][i]));
		asm volatile ("vpxorq    %zmm2,%zmm0,%zmm0");
		asm volatile ("vpxorq    %zmm3,%zmm1,%zmm1");

		asm volatile ("vpxorq    %zmm6,%zmm6,%zmm6");

		asm volatile ("vgf2p8mulb %zmm0,%zmm8,%zmm2");
		asm volatile ("vpxorq    %zmm2,%zmm6,%zmm6");

		asm volatile ("vgf2p8mulb %zmm1,%zmm9,%zmm3");
		asm volatile ("vpxorq    %zmm3,%zmm6,%zmm6");

		asm volatile ("vmovdqa64 %%zmm6,%0" : "=m" (pa[0][i]));

		asm volatile ("vpxorq    %zmm6,%zmm6,%zmm6");

		asm volatile ("vgf2p8mulb %zmm0,%zmm10,%zmm2");
		asm volatile ("vpxorq    %zmm2,%zmm6,%zmm6");

		asm volatile ("vgf2p8mulb %zmm1,%zmm11,%zmm3");
		asm volatile ("vpxorq    %zmm3,%zmm6,%zmm6");

		asm volatile ("vmovdqa64 %%zmm6,%0" : "=m" (pa[1][i]));
	}

	raid_avx_end();
}

/*
 * AES recovering AVX2 GFNI implementation
 */
void raid_recX_avx2gfni_aes(int nr, int *id, int *ip, int nd, size_t size, void **vv)
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

	raid_avx_begin();

	for (i = 0; i < size; i += 64) {
		/* delta */
		asm volatile (
			"movq 0(%2), %%rax\n"
			"movq 0(%3), %%rbx\n"
			"vmovdqa 0(%%rax, %1), %%ymm0\n"
			"vmovdqa 32(%%rax, %1), %%ymm1\n"
			"vmovdqa 0(%%rbx, %1), %%ymm14\n"
			"vmovdqa 32(%%rbx, %1), %%ymm15\n"
			"vpxor %%ymm14, %%ymm0, %%ymm0\n"
			"vpxor %%ymm15, %%ymm1, %%ymm1\n"
			"cmpq $1, %0\n"
			"jbe 1f\n"

			"movq 8(%2), %%rax\n"
			"movq 8(%3), %%rbx\n"
			"vmovdqa 0(%%rax, %1), %%ymm2\n"
			"vmovdqa 32(%%rax, %1), %%ymm3\n"
			"vmovdqa 0(%%rbx, %1), %%ymm14\n"
			"vmovdqa 32(%%rbx, %1), %%ymm15\n"
			"vpxor %%ymm14, %%ymm2, %%ymm2\n"
			"vpxor %%ymm15, %%ymm3, %%ymm3\n"
			"cmpq $2, %0\n"
			"jbe 1f\n"

			"movq 16(%2), %%rax\n"
			"movq 16(%3), %%rbx\n"
			"vmovdqa 0(%%rax, %1), %%ymm4\n"
			"vmovdqa 32(%%rax, %1), %%ymm5\n"
			"vmovdqa 0(%%rbx, %1), %%ymm14\n"
			"vmovdqa 32(%%rbx, %1), %%ymm15\n"
			"vpxor %%ymm14, %%ymm4, %%ymm4\n"
			"vpxor %%ymm15, %%ymm5, %%ymm5\n"
			"cmpq $3, %0\n"
			"jbe 1f\n"

			"movq 24(%2), %%rax\n"
			"movq 24(%3), %%rbx\n"
			"vmovdqa 0(%%rax, %1), %%ymm6\n"
			"vmovdqa 32(%%rax, %1), %%ymm7\n"
			"vmovdqa 0(%%rbx, %1), %%ymm14\n"
			"vmovdqa 32(%%rbx, %1), %%ymm15\n"
			"vpxor %%ymm14, %%ymm6, %%ymm6\n"
			"vpxor %%ymm15, %%ymm7, %%ymm7\n"
			"cmpq $4, %0\n"
			"jbe 1f\n"

			"movq 32(%2), %%rax\n"
			"movq 32(%3), %%rbx\n"
			"vmovdqa 0(%%rax, %1), %%ymm8\n"
			"vmovdqa 32(%%rax, %1), %%ymm9\n"
			"vmovdqa 0(%%rbx, %1), %%ymm14\n"
			"vmovdqa 32(%%rbx, %1), %%ymm15\n"
			"vpxor %%ymm14, %%ymm8, %%ymm8\n"
			"vpxor %%ymm15, %%ymm9, %%ymm9\n"
			"cmpq $5, %0\n"
			"jbe 1f\n"

			"movq 40(%2), %%rax\n"
			"movq 40(%3), %%rbx\n"
			"vmovdqa 0(%%rax, %1), %%ymm10\n"
			"vmovdqa 32(%%rax, %1), %%ymm11\n"
			"vmovdqa 0(%%rbx, %1), %%ymm14\n"
			"vmovdqa 32(%%rbx, %1), %%ymm15\n"
			"vpxor %%ymm14, %%ymm10, %%ymm10\n"
			"vpxor %%ymm15, %%ymm11, %%ymm11\n"

			"1:\n"
			:
			: "r" ((uint64_t)N), "r" (i), "r" (p), "r" (pa)
			: "rax", "rbx", "cc", "memory"
		);

		/* reconstruct */
		for (j = 0; j < N; ++j) {
			asm volatile (
				"vpbroadcastb 0(%2), %%ymm14\n"
				"vgf2p8mulb %%ymm0, %%ymm14, %%ymm12\n"
				"vgf2p8mulb %%ymm1, %%ymm14, %%ymm13\n"
				"cmpq $1, %0\n"
				"jbe 1f\n"

				"vpbroadcastb 1(%2), %%ymm14\n"
				"vgf2p8mulb %%ymm2, %%ymm14, %%ymm15\n"
				"vpxor %%ymm15, %%ymm12, %%ymm12\n"
				"vgf2p8mulb %%ymm3, %%ymm14, %%ymm15\n"
				"vpxor %%ymm15, %%ymm13, %%ymm13\n"
				"cmpq $2, %0\n"
				"jbe 1f\n"

				"vpbroadcastb 2(%2), %%ymm14\n"
				"vgf2p8mulb %%ymm4, %%ymm14, %%ymm15\n"
				"vpxor %%ymm15, %%ymm12, %%ymm12\n"
				"vgf2p8mulb %%ymm5, %%ymm14, %%ymm15\n"
				"vpxor %%ymm15, %%ymm13, %%ymm13\n"
				"cmpq $3, %0\n"
				"jbe 1f\n"

				"vpbroadcastb 3(%2), %%ymm14\n"
				"vgf2p8mulb %%ymm6, %%ymm14, %%ymm15\n"
				"vpxor %%ymm15, %%ymm12, %%ymm12\n"
				"vgf2p8mulb %%ymm7, %%ymm14, %%ymm15\n"
				"vpxor %%ymm15, %%ymm13, %%ymm13\n"
				"cmpq $4, %0\n"
				"jbe 1f\n"

				"vpbroadcastb 4(%2), %%ymm14\n"
				"vgf2p8mulb %%ymm8, %%ymm14, %%ymm15\n"
				"vpxor %%ymm15, %%ymm12, %%ymm12\n"
				"vgf2p8mulb %%ymm9, %%ymm14, %%ymm15\n"
				"vpxor %%ymm15, %%ymm13, %%ymm13\n"
				"cmpq $5, %0\n"
				"jbe 1f\n"

				"vpbroadcastb 5(%2), %%ymm14\n"
				"vgf2p8mulb %%ymm10, %%ymm14, %%ymm15\n"
				"vpxor %%ymm15, %%ymm12, %%ymm12\n"
				"vgf2p8mulb %%ymm11, %%ymm14, %%ymm15\n"
				"vpxor %%ymm15, %%ymm13, %%ymm13\n"

				"1:\n"
				"movq %3, %%rax\n"
				"vmovdqa %%ymm12, 0(%%rax, %1)\n"
				"vmovdqa %%ymm13, 32(%%rax, %1)\n"
				:
				: "r" ((uint64_t)N), "r" (i), "r" (&V[j * N]), "r" (pa[j])
				: "rax", "cc", "memory"
			);
		}
	}

	raid_avx_end();
}

/*
 * AES recovering GFNI implementation
 */
void raid_recX_avx512gfni_aes(int nr, int *id, int *ip, int nd, size_t size, void **vv)
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

	raid_avx_begin();

	for (i = 0; i < size; i += 64) {
		/* delta */
		asm volatile (
			"movq 0(%2), %%rax\n"
			"movq 0(%3), %%rbx\n"
			"vmovdqa64 (%%rax, %1), %%zmm0\n"
			"vmovdqa64 (%%rbx, %1), %%zmm6\n"
			"vpxorq %%zmm6, %%zmm0, %%zmm0\n"
			"cmpq $1, %0\n"
			"jbe 1f\n"

			"movq 8(%2), %%rax\n"
			"movq 8(%3), %%rbx\n"
			"vmovdqa64 (%%rax, %1), %%zmm1\n"
			"vmovdqa64 (%%rbx, %1), %%zmm6\n"
			"vpxorq %%zmm6, %%zmm1, %%zmm1\n"
			"cmpq $2, %0\n"
			"jbe 1f\n"

			"movq 16(%2), %%rax\n"
			"movq 16(%3), %%rbx\n"
			"vmovdqa64 (%%rax, %1), %%zmm2\n"
			"vmovdqa64 (%%rbx, %1), %%zmm6\n"
			"vpxorq %%zmm6, %%zmm2, %%zmm2\n"
			"cmpq $3, %0\n"
			"jbe 1f\n"

			"movq 24(%2), %%rax\n"
			"movq 24(%3), %%rbx\n"
			"vmovdqa64 (%%rax, %1), %%zmm3\n"
			"vmovdqa64 (%%rbx, %1), %%zmm6\n"
			"vpxorq %%zmm6, %%zmm3, %%zmm3\n"
			"cmpq $4, %0\n"
			"jbe 1f\n"

			"movq 32(%2), %%rax\n"
			"movq 32(%3), %%rbx\n"
			"vmovdqa64 (%%rax, %1), %%zmm4\n"
			"vmovdqa64 (%%rbx, %1), %%zmm6\n"
			"vpxorq %%zmm6, %%zmm4, %%zmm4\n"
			"cmpq $5, %0\n"
			"jbe 1f\n"

			"movq 40(%2), %%rax\n"
			"movq 40(%3), %%rbx\n"
			"vmovdqa64 (%%rax, %1), %%zmm5\n"
			"vmovdqa64 (%%rbx, %1), %%zmm6\n"
			"vpxorq %%zmm6, %%zmm5, %%zmm5\n"

			"1:\n"
			:
			: "r" ((uint64_t)N), "r" (i), "r" (p), "r" (pa)
			: "rax", "rbx", "cc", "memory"
		);

		/* reconstruct */
		for (j = 0; j < N; ++j) {
			asm volatile (
				"vpbroadcastb 0(%2), %%zmm6\n"
				"vgf2p8mulb %%zmm0, %%zmm6, %%zmm7\n"
				"cmpq $1, %0\n"
				"jbe 1f\n"

				"vpbroadcastb 1(%2), %%zmm6\n"
				"vgf2p8mulb %%zmm1, %%zmm6, %%zmm6\n"
				"vpxorq %%zmm6, %%zmm7, %%zmm7\n"
				"cmpq $2, %0\n"
				"jbe 1f\n"

				"vpbroadcastb 2(%2), %%zmm6\n"
				"vgf2p8mulb %%zmm2, %%zmm6, %%zmm6\n"
				"vpxorq %%zmm6, %%zmm7, %%zmm7\n"
				"cmpq $3, %0\n"
				"jbe 1f\n"

				"vpbroadcastb 3(%2), %%zmm6\n"
				"vgf2p8mulb %%zmm3, %%zmm6, %%zmm6\n"
				"vpxorq %%zmm6, %%zmm7, %%zmm7\n"
				"cmpq $4, %0\n"
				"jbe 1f\n"

				"vpbroadcastb 4(%2), %%zmm6\n"
				"vgf2p8mulb %%zmm4, %%zmm6, %%zmm6\n"
				"vpxorq %%zmm6, %%zmm7, %%zmm7\n"
				"cmpq $5, %0\n"
				"jbe 1f\n"

				"vpbroadcastb 5(%2), %%zmm6\n"
				"vgf2p8mulb %%zmm5, %%zmm6, %%zmm6\n"
				"vpxorq %%zmm6, %%zmm7, %%zmm7\n"

				"1:\n"
				"movq %3, %%rax\n"
				"vmovdqa64 %%zmm7, (%%rax, %1)\n"
				:
				: "r" ((uint64_t)N), "r" (i), "r" (&V[j * N]), "r" (pa[j])
				: "rax", "cc", "memory"
			);
		}
	}

	raid_avx_end();
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
		raid_rec_register(RAID_ALGO_CAUCHY_PAR3, "gfni", raid_recX_avx2gfni_raid, RAID_POLY_RAID);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR4, "gfni", raid_recX_avx2gfni_raid, RAID_POLY_RAID);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR5, "gfni", raid_recX_avx2gfni_raid, RAID_POLY_RAID);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR6, "gfni", raid_recX_avx2gfni_raid, RAID_POLY_RAID);

		raid_rec_register(RAID_ALGO_CAUCHY_PAR1, "gfni", raid_rec1_avx2gfni_aes, RAID_POLY_AES);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR2, "gfni", raid_rec2_avx2gfni_aes, RAID_POLY_AES);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR3, "gfni", raid_recX_avx2gfni_aes, RAID_POLY_AES);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR4, "gfni", raid_recX_avx2gfni_aes, RAID_POLY_AES);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR5, "gfni", raid_recX_avx2gfni_aes, RAID_POLY_AES);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR6, "gfni", raid_recX_avx2gfni_aes, RAID_POLY_AES);
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
			raid_rec_register(RAID_ALGO_CAUCHY_PAR3, "gfni512", raid_recX_avx512gfni_raid, RAID_POLY_RAID);
			raid_rec_register(RAID_ALGO_CAUCHY_PAR4, "gfni512", raid_recX_avx512gfni_raid, RAID_POLY_RAID);
			raid_rec_register(RAID_ALGO_CAUCHY_PAR5, "gfni512", raid_recX_avx512gfni_raid, RAID_POLY_RAID);
			raid_rec_register(RAID_ALGO_CAUCHY_PAR6, "gfni512", raid_recX_avx512gfni_raid, RAID_POLY_RAID);

			raid_rec_register(RAID_ALGO_CAUCHY_PAR1, "gfni512", raid_rec1_avx512gfni_aes, RAID_POLY_AES);
			raid_rec_register(RAID_ALGO_CAUCHY_PAR2, "gfni512", raid_rec2_avx512gfni_aes, RAID_POLY_AES);
			raid_rec_register(RAID_ALGO_CAUCHY_PAR3, "gfni512", raid_recX_avx512gfni_aes, RAID_POLY_AES);
			raid_rec_register(RAID_ALGO_CAUCHY_PAR4, "gfni512", raid_recX_avx512gfni_aes, RAID_POLY_AES);
			raid_rec_register(RAID_ALGO_CAUCHY_PAR5, "gfni512", raid_recX_avx512gfni_aes, RAID_POLY_AES);
			raid_rec_register(RAID_ALGO_CAUCHY_PAR6, "gfni512", raid_recX_avx512gfni_aes, RAID_POLY_AES);
		}
	}
}
#endif
