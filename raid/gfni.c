// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Andrea Mazzoleni

#include "internal.h"
#include "gf.h"
#include "cpu.h"

#ifdef CONFIG_X86_64
/*
 * GENX AVX2 GFNI implementation for the RAID polynomial using direct
 * Cauchy coefficients.
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
	}

	raid_avx_end();
}

/*
 * GENX AVX512 GFNI implementation for the RAID polynomial using direct
 * Cauchy coefficients.
 */
static __always_inline void raid_genX_avx512gfni_raid(int nd, size_t size, void **vv, int np)
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
 * GENX AVX2 GFNI implementation for the AES polynomial using direct
 * Cauchy coefficients.
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
	}

	raid_avx_end();
}

/*
 * GENX AVX512 GFNI implementation for the AES polynomial using direct
 * Cauchy coefficients.
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

static __always_inline void raid_recX_avx2gfni_raid(int nr, int *id, int *ip, int nd, size_t size, void **vv)
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

	raid_avx_end();
}

static __always_inline void raid_recX_avx512gfni_raid(int nr, int *id, int *ip, int nd, size_t size, void **vv)
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

		for (j = 0; j < nr; ++j) {
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

			asm volatile ("vmovdqa64 %%zmm7,%0" : "=m" (pa[j][i]));
		}
	}

	raid_avx_end();
}

static __always_inline void raid_recX_avx2gfni_aes(int nr, int *id, int *ip, int nd, size_t size, void **vv)
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

	raid_avx_end();
}

static __always_inline void raid_recX_avx512gfni_aes(int nr, int *id, int *ip, int nd, size_t size, void **vv)
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

		for (j = 0; j < nr; ++j) {
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

			asm volatile ("vmovdqa64 %%zmm7,%0" : "=m" (pa[j][i]));
		}
	}

	raid_avx_end();
}

/*
 * GEN2 (RAID6 with powers of 2) AVX2 GFNI implementation
 */
void raid_gen2_avx2gfni_raid(int nd, size_t size, void **vv)
{
	raid_genX_avx2gfni_raid(nd, size, vv, 2);
}

/*
 * GEN2 (RAID6 with powers of 2) GFNI implementation
 */
void raid_gen2_avx512gfni_raid(int nd, size_t size, void **vv)
{
	raid_genX_avx512gfni_raid(nd, size, vv, 2);
}

/*
 * GEN3 (triple parity with Cauchy matrix) AVX2 GFNI implementation
 */
void raid_gen3_avx2gfni_raid(int nd, size_t size, void **vv)
{
	raid_genX_avx2gfni_raid(nd, size, vv, 3);
}

/*
 * GEN3 (triple parity with Cauchy matrix) GFNI implementation
 */
void raid_gen3_avx512gfni_raid(int nd, size_t size, void **vv)
{
	raid_genX_avx512gfni_raid(nd, size, vv, 3);
}

/*
 * GEN4 (quad parity with Cauchy matrix) AVX2 GFNI implementation
 */
void raid_gen4_avx2gfni_raid(int nd, size_t size, void **vv)
{
	raid_genX_avx2gfni_raid(nd, size, vv, 4);
}

/*
 * GEN4 (quad parity with Cauchy matrix) GFNI implementation
 */
void raid_gen4_avx512gfni_raid(int nd, size_t size, void **vv)
{
	raid_genX_avx512gfni_raid(nd, size, vv, 4);
}

/*
 * GEN5 (penta parity with Cauchy matrix) AVX2 GFNI implementation
 */
void raid_gen5_avx2gfni_raid(int nd, size_t size, void **vv)
{
	raid_genX_avx2gfni_raid(nd, size, vv, 5);
}

/*
 * GEN5 (penta parity with Cauchy matrix) GFNI implementation
 */
void raid_gen5_avx512gfni_raid(int nd, size_t size, void **vv)
{
	raid_genX_avx512gfni_raid(nd, size, vv, 5);
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
	raid_genX_avx512gfni_raid(nd, size, vv, 6);
}

/*
 * GEN2 (RAID6 with powers of 3) AVX2 GFNI implementation
 */
void raid_gen2_avx2gfni_aes(int nd, size_t size, void **vv)
{
	raid_genX_avx2gfni_aes(nd, size, vv, 2);
}

/*
 * GEN2 (RAID6 with powers of 3) GFNI implementation
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

void raid_rec1_avx2gfni_raid(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 1);

	/* if recovering with P uses the delta function */
	if (ip[0] == 0) {
		raid_rec1of1(id, nd, size, vv);
		return;
	}

	raid_recX_avx2gfni_raid(1, id, ip, nd, size, vv);
}

/*
 * Recover failure of two data blocks using P and Q AVX2 RAID GFNI implementation.
 */
static __always_inline void raid_rec2of2_avx2gfni_raid(int *id, int *ip, int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *pa;
	uint8_t *q;
	uint8_t *qa;
	const uint8_t *T[2];
	uint8_t C[2];
	size_t i;

	C[0] = inv(powgen(id[1] - id[0]) ^ 1);
	C[1] = inv(powgen(id[0]) ^ powgen(id[1]));

	T[0] = raid_gfaffine_raid[C[0]];
	T[1] = raid_gfaffine_raid[C[1]];

	/* compute delta parity */
	raid_delta_gen(2, id, ip, nd, size, vv);

	p = v[nd];
	q = v[nd + 1];
	pa = v[id[0]];
	qa = v[id[1]];

	raid_avx_begin();

	asm volatile ("vpbroadcastq %0,%%ymm14" : : "m" (T[0][0]));
	asm volatile ("vpbroadcastq %0,%%ymm15" : : "m" (T[1][0]));

	for (i = 0; i < size; i += 64) {
		/* Pd, two 32-byte lanes */
		asm volatile ("vmovdqa %0,%%ymm0" : : "m" (p[i]));
		asm volatile ("vmovdqa %0,%%ymm1" : : "m" (p[i + 32]));
		asm volatile ("vpxor %0,%%ymm0,%%ymm0" : : "m" (pa[i]));
		asm volatile ("vpxor %0,%%ymm1,%%ymm1" : : "m" (pa[i + 32]));

		/* Qd, two 32-byte lanes */
		asm volatile ("vmovdqa %0,%%ymm2" : : "m" (q[i]));
		asm volatile ("vmovdqa %0,%%ymm3" : : "m" (q[i + 32]));
		asm volatile ("vpxor %0,%%ymm2,%%ymm2" : : "m" (qa[i]));
		asm volatile ("vpxor %0,%%ymm3,%%ymm3" : : "m" (qa[i + 32]));

		/* Dy = C0 * Pd ^ C1 * Qd */
		asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm0,%ymm4");
		asm volatile ("vgf2p8affineqb $0,%ymm14,%ymm1,%ymm5");
		asm volatile ("vgf2p8affineqb $0,%ymm15,%ymm2,%ymm6");
		asm volatile ("vgf2p8affineqb $0,%ymm15,%ymm3,%ymm7");
		asm volatile ("vpxor %ymm6,%ymm4,%ymm4");
		asm volatile ("vpxor %ymm7,%ymm5,%ymm5");

		/* Dx = Pd ^ Dy */
		asm volatile ("vpxor %ymm4,%ymm0,%ymm0");
		asm volatile ("vpxor %ymm5,%ymm1,%ymm1");

		asm volatile ("vmovdqa %%ymm0,%0" : "=m" (pa[i]));
		asm volatile ("vmovdqa %%ymm1,%0" : "=m" (pa[i + 32]));
		asm volatile ("vmovdqa %%ymm4,%0" : "=m" (qa[i]));
		asm volatile ("vmovdqa %%ymm5,%0" : "=m" (qa[i + 32]));
	}

	raid_avx_end();
}

void raid_rec2_avx2gfni_raid(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 2);

	if (ip[0] == 0 && ip[1] == 1) {
		raid_rec2of2_avx2gfni_raid(id, ip, nd, size, vv);
		return;
	}

	raid_recX_avx2gfni_raid(2, id, ip, nd, size, vv);
}

void raid_rec3_avx2gfni_raid(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 3);
	raid_recX_avx2gfni_raid(3, id, ip, nd, size, vv);
}

void raid_rec4_avx2gfni_raid(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 4);
	raid_recX_avx2gfni_raid(4, id, ip, nd, size, vv);
}

void raid_rec5_avx2gfni_raid(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 5);
	raid_recX_avx2gfni_raid(5, id, ip, nd, size, vv);
}

void raid_rec6_avx2gfni_raid(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 6);
	raid_recX_avx2gfni_raid(6, id, ip, nd, size, vv);
}

void raid_rec1_avx512gfni_raid(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 1);

	/* if recovering with P uses the delta function */
	if (ip[0] == 0) {
		raid_rec1of1(id, nd, size, vv);
		return;
	}

	raid_recX_avx512gfni_raid(1, id, ip, nd, size, vv);
}

/*
 * Recover failure of two data blocks using P and Q AVX512 RAID GFNI implementation.
 */
static __always_inline void raid_rec2of2_avx512gfni_raid(int *id, int *ip, int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *pa;
	uint8_t *q;
	uint8_t *qa;
	const uint8_t *T[2];
	uint8_t C[2];
	size_t i;

	C[0] = inv(powgen(id[1] - id[0]) ^ 1);
	C[1] = inv(powgen(id[0]) ^ powgen(id[1]));

	T[0] = raid_gfaffine_raid[C[0]];
	T[1] = raid_gfaffine_raid[C[1]];

	raid_delta_gen(2, id, ip, nd, size, vv);

	p = v[nd];
	q = v[nd + 1];
	pa = v[id[0]];
	qa = v[id[1]];

	raid_avx_begin();

	asm volatile ("vpbroadcastq %0,%%zmm14" : : "m" (T[0][0]));
	asm volatile ("vpbroadcastq %0,%%zmm15" : : "m" (T[1][0]));

	for (i = 0; i < size; i += 64) {
		asm volatile ("vmovdqa64 %0,%%zmm0" : : "m" (p[i]));
		asm volatile ("vpxorq %0,%%zmm0,%%zmm0" : : "m" (pa[i]));

		asm volatile ("vmovdqa64 %0,%%zmm1" : : "m" (q[i]));
		asm volatile ("vpxorq %0,%%zmm1,%%zmm1" : : "m" (qa[i]));

		asm volatile ("vgf2p8affineqb $0,%zmm14,%zmm0,%zmm2");
		asm volatile ("vgf2p8affineqb $0,%zmm15,%zmm1,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm2,%zmm2");

		/* Dx = Pd ^ Dy */
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");

		asm volatile ("vmovdqa64 %%zmm0,%0" : "=m" (pa[i]));
		asm volatile ("vmovdqa64 %%zmm2,%0" : "=m" (qa[i]));
	}

	raid_avx_end();
}

void raid_rec2_avx512gfni_raid(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 2);

	if (ip[0] == 0 && ip[1] == 1) {
		raid_rec2of2_avx512gfni_raid(id, ip, nd, size, vv);
		return;
	}

	raid_recX_avx512gfni_raid(2, id, ip, nd, size, vv);
}

void raid_rec3_avx512gfni_raid(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 3);
	raid_recX_avx512gfni_raid(3, id, ip, nd, size, vv);
}

void raid_rec4_avx512gfni_raid(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 4);
	raid_recX_avx512gfni_raid(4, id, ip, nd, size, vv);
}

void raid_rec5_avx512gfni_raid(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 5);
	raid_recX_avx512gfni_raid(5, id, ip, nd, size, vv);
}

void raid_rec6_avx512gfni_raid(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 6);
	raid_recX_avx512gfni_raid(6, id, ip, nd, size, vv);
}

void raid_rec1_avx2gfni_aes(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 1);

	/* if recovering with P uses the delta function */
	if (ip[0] == 0) {
		raid_rec1of1(id, nd, size, vv);
		return;
	}

	raid_recX_avx2gfni_aes(1, id, ip, nd, size, vv);
}

/*
 * Recover failure of two data blocks using P and Q AVX2 AES GFNI implementation.
 */
static __always_inline void raid_rec2of2_avx2gfni_aes(int *id, int *ip, int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *pa;
	uint8_t *q;
	uint8_t *qa;
	uint8_t C[2];
	size_t i;

	C[0] = inv(powgen(id[1] - id[0]) ^ 1);
	C[1] = inv(powgen(id[0]) ^ powgen(id[1]));

	raid_delta_gen(2, id, ip, nd, size, vv);

	p = v[nd];
	q = v[nd + 1];
	pa = v[id[0]];
	qa = v[id[1]];

	raid_avx_begin();

	asm volatile ("vpbroadcastb %0,%%ymm14" : : "m" (C[0]));
	asm volatile ("vpbroadcastb %0,%%ymm15" : : "m" (C[1]));

	for (i = 0; i < size; i += 64) {
		asm volatile ("vmovdqa %0,%%ymm0" : : "m" (p[i]));
		asm volatile ("vmovdqa %0,%%ymm1" : : "m" (p[i + 32]));
		asm volatile ("vpxor %0,%%ymm0,%%ymm0" : : "m" (pa[i]));
		asm volatile ("vpxor %0,%%ymm1,%%ymm1" : : "m" (pa[i + 32]));

		asm volatile ("vmovdqa %0,%%ymm2" : : "m" (q[i]));
		asm volatile ("vmovdqa %0,%%ymm3" : : "m" (q[i + 32]));
		asm volatile ("vpxor %0,%%ymm2,%%ymm2" : : "m" (qa[i]));
		asm volatile ("vpxor %0,%%ymm3,%%ymm3" : : "m" (qa[i + 32]));

		asm volatile ("vgf2p8mulb %ymm14,%ymm0,%ymm4");
		asm volatile ("vgf2p8mulb %ymm14,%ymm1,%ymm5");
		asm volatile ("vgf2p8mulb %ymm15,%ymm2,%ymm6");
		asm volatile ("vgf2p8mulb %ymm15,%ymm3,%ymm7");
		asm volatile ("vpxor %ymm6,%ymm4,%ymm4");
		asm volatile ("vpxor %ymm7,%ymm5,%ymm5");

		asm volatile ("vpxor %ymm4,%ymm0,%ymm0");
		asm volatile ("vpxor %ymm5,%ymm1,%ymm1");

		asm volatile ("vmovdqa %%ymm0,%0" : "=m" (pa[i]));
		asm volatile ("vmovdqa %%ymm1,%0" : "=m" (pa[i + 32]));
		asm volatile ("vmovdqa %%ymm4,%0" : "=m" (qa[i]));
		asm volatile ("vmovdqa %%ymm5,%0" : "=m" (qa[i + 32]));
	}

	raid_avx_end();
}

void raid_rec2_avx2gfni_aes(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 2);

	if (ip[0] == 0 && ip[1] == 1) {
		raid_rec2of2_avx2gfni_aes(id, ip, nd, size, vv);
		return;
	}

	raid_recX_avx2gfni_aes(2, id, ip, nd, size, vv);
}

void raid_rec3_avx2gfni_aes(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 3);
	raid_recX_avx2gfni_aes(3, id, ip, nd, size, vv);
}

void raid_rec4_avx2gfni_aes(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 4);
	raid_recX_avx2gfni_aes(4, id, ip, nd, size, vv);
}

void raid_rec5_avx2gfni_aes(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 5);
	raid_recX_avx2gfni_aes(5, id, ip, nd, size, vv);
}

void raid_rec6_avx2gfni_aes(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 6);
	raid_recX_avx2gfni_aes(6, id, ip, nd, size, vv);
}

void raid_rec1_avx512gfni_aes(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 1);

	/* if recovering with P uses the delta function */
	if (ip[0] == 0) {
		raid_rec1of1(id, nd, size, vv);
		return;
	}

	raid_recX_avx512gfni_aes(1, id, ip, nd, size, vv);
}

/*
 * Recover failure of two data blocks using P and Q AVX512 AES GFNI implementation.
 */
static __always_inline void raid_rec2of2_avx512gfni_aes(int *id, int *ip, int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *pa;
	uint8_t *q;
	uint8_t *qa;
	uint8_t C[2];
	size_t i;

	C[0] = inv(powgen(id[1] - id[0]) ^ 1);
	C[1] = inv(powgen(id[0]) ^ powgen(id[1]));

	raid_delta_gen(2, id, ip, nd, size, vv);

	p = v[nd];
	q = v[nd + 1];
	pa = v[id[0]];
	qa = v[id[1]];

	raid_avx_begin();

	asm volatile ("vpbroadcastb %0,%%zmm14" : : "m" (C[0]));
	asm volatile ("vpbroadcastb %0,%%zmm15" : : "m" (C[1]));

	for (i = 0; i < size; i += 64) {
		asm volatile ("vmovdqa64 %0,%%zmm0" : : "m" (p[i]));
		asm volatile ("vpxorq %0,%%zmm0,%%zmm0" : : "m" (pa[i]));

		asm volatile ("vmovdqa64 %0,%%zmm1" : : "m" (q[i]));
		asm volatile ("vpxorq %0,%%zmm1,%%zmm1" : : "m" (qa[i]));

		asm volatile ("vgf2p8mulb %zmm14,%zmm0,%zmm2");
		asm volatile ("vgf2p8mulb %zmm15,%zmm1,%zmm3");
		asm volatile ("vpxorq %zmm3,%zmm2,%zmm2");

		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");

		asm volatile ("vmovdqa64 %%zmm0,%0" : "=m" (pa[i]));
		asm volatile ("vmovdqa64 %%zmm2,%0" : "=m" (qa[i]));
	}

	raid_avx_end();
}

void raid_rec2_avx512gfni_aes(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 2);

	if (ip[0] == 0 && ip[1] == 1) {
		raid_rec2of2_avx512gfni_aes(id, ip, nd, size, vv);
		return;
	}

	raid_recX_avx512gfni_aes(2, id, ip, nd, size, vv);
}

void raid_rec3_avx512gfni_aes(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 3);
	raid_recX_avx512gfni_aes(3, id, ip, nd, size, vv);
}

void raid_rec4_avx512gfni_aes(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 4);
	raid_recX_avx512gfni_aes(4, id, ip, nd, size, vv);
}

void raid_rec5_avx512gfni_aes(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 5);
	raid_recX_avx512gfni_aes(5, id, ip, nd, size, vv);
}

void raid_rec6_avx512gfni_aes(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 6);
	raid_recX_avx512gfni_aes(6, id, ip, nd, size, vv);
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
