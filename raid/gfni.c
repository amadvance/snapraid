// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Andrea Mazzoleni

#include "internal.h"
#include "gf.h"
#include "cpu.h"

#ifdef CONFIG_X86_64

/*
 * GENX AVX2 GFNI implementation
 */
static __always_inline void raid_genX_avx2gfni(int nd, size_t size, void **vv, int np)
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

			asm volatile ("vpbroadcastb %0,%%ymm14" : : "m" (gfcauchy[1][d]));
			asm volatile ("vgf2p8mulb %ymm12,%ymm14,%ymm15");
			asm volatile ("vpxor    %ymm15,%ymm2,%ymm2");
			asm volatile ("vgf2p8mulb %ymm13,%ymm14,%ymm15");
			asm volatile ("vpxor    %ymm15,%ymm3,%ymm3");
			if (np >= 3) {
				asm volatile ("vpbroadcastb %0,%%ymm14" : : "m" (gfcauchy[2][d]));
				asm volatile ("vgf2p8mulb %ymm12,%ymm14,%ymm15");
				asm volatile ("vpxor    %ymm15,%ymm4,%ymm4");
				asm volatile ("vgf2p8mulb %ymm13,%ymm14,%ymm15");
				asm volatile ("vpxor    %ymm15,%ymm5,%ymm5");
			}
			if (np >= 4) {
				asm volatile ("vpbroadcastb %0,%%ymm14" : : "m" (gfcauchy[3][d]));
				asm volatile ("vgf2p8mulb %ymm12,%ymm14,%ymm15");
				asm volatile ("vpxor    %ymm15,%ymm6,%ymm6");
				asm volatile ("vgf2p8mulb %ymm13,%ymm14,%ymm15");
				asm volatile ("vpxor    %ymm15,%ymm7,%ymm7");
			}
			if (np >= 5) {
				asm volatile ("vpbroadcastb %0,%%ymm14" : : "m" (gfcauchy[4][d]));
				asm volatile ("vgf2p8mulb %ymm12,%ymm14,%ymm15");
				asm volatile ("vpxor    %ymm15,%ymm8,%ymm8");
				asm volatile ("vgf2p8mulb %ymm13,%ymm14,%ymm15");
				asm volatile ("vpxor    %ymm15,%ymm9,%ymm9");
			}
			if (np >= 6) {
				asm volatile ("vpbroadcastb %0,%%ymm14" : : "m" (gfcauchy[5][d]));
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
static __always_inline void raid_genX_avx512gfni(int nd, size_t size, void **vv, int np)
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

			asm volatile ("vpbroadcastb %0,%%zmm7" : : "m" (gfcauchy[1][d]));
			if (np >= 3)
				asm volatile ("vpbroadcastb %0,%%zmm8" : : "m" (gfcauchy[2][d]));
			if (np >= 4)
				asm volatile ("vpbroadcastb %0,%%zmm9" : : "m" (gfcauchy[3][d]));
			if (np >= 5)
				asm volatile ("vpbroadcastb %0,%%zmm10" : : "m" (gfcauchy[4][d]));
			if (np >= 6)
				asm volatile ("vpbroadcastb %0,%%zmm11" : : "m" (gfcauchy[5][d]));

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
void raid_gen2_avx2gfni(int nd, size_t size, void **vv)
{
	raid_genX_avx2gfni(nd, size, vv, 2);
}

/*
 * GEN2 (RAID6 with powers of 2) GFNI implementation
 */
void raid_gen2_avx512gfni(int nd, size_t size, void **vv)
{
	raid_genX_avx512gfni(nd, size, vv, 2);
}

/*
 * GEN3 (triple parity with Cauchy matrix) AVX2 GFNI implementation
 */
void raid_gen3_avx2gfni(int nd, size_t size, void **vv)
{
	raid_genX_avx2gfni(nd, size, vv, 3);
}

/*
 * GEN3 (triple parity with Cauchy matrix) GFNI implementation
 */
void raid_gen3_avx512gfni(int nd, size_t size, void **vv)
{
	raid_genX_avx512gfni(nd, size, vv, 3);
}

/*
 * GEN4 (quad parity with Cauchy matrix) AVX2 GFNI implementation
 */
void raid_gen4_avx2gfni(int nd, size_t size, void **vv)
{
	raid_genX_avx2gfni(nd, size, vv, 4);
}

/*
 * GEN4 (quad parity with Cauchy matrix) GFNI implementation
 */
void raid_gen4_avx512gfni(int nd, size_t size, void **vv)
{
	raid_genX_avx512gfni(nd, size, vv, 4);
}

/*
 * GEN5 (penta parity with Cauchy matrix) AVX2 GFNI implementation
 */
void raid_gen5_avx2gfni(int nd, size_t size, void **vv)
{
	raid_genX_avx2gfni(nd, size, vv, 5);
}

/*
 * GEN5 (penta parity with Cauchy matrix) GFNI implementation
 */
void raid_gen5_avx512gfni(int nd, size_t size, void **vv)
{
	raid_genX_avx512gfni(nd, size, vv, 5);
}

/*
 * GEN6 (hexa parity with Cauchy matrix) AVX2 GFNI implementation
 */
void raid_gen6_avx2gfni(int nd, size_t size, void **vv)
{
	raid_genX_avx2gfni(nd, size, vv, 6);
}

/*
 * GEN6 (hexa parity with Cauchy matrix) GFNI implementation
 */
void raid_gen6_avx512gfni(int nd, size_t size, void **vv)
{
	raid_genX_avx512gfni(nd, size, vv, 6);
}

/*
 * RAID recovering for one disk AVX2 GFNI implementation
 */
void raid_rec1_avx2gfni(int nr, int *id, int *ip, int nd, size_t size, void **vv)
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
 * RAID recovering for one disk GFNI implementation
 */
void raid_rec1_avx512gfni(int nr, int *id, int *ip, int nd, size_t size, void **vv)
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
 * RAID recovering for two disks AVX2 GFNI implementation
 */
void raid_rec2_avx2gfni(int nr, int *id, int *ip, int nd, size_t size, void **vv)
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

		asm volatile ("vpbroadcastb %0,%%ymm8" : : "m" (V[0]));
		asm volatile ("vgf2p8mulb %ymm0,%ymm8,%ymm4");
		asm volatile ("vgf2p8mulb %ymm1,%ymm8,%ymm5");

		asm volatile ("vpbroadcastb %0,%%ymm8" : : "m" (V[1]));
		asm volatile ("vgf2p8mulb %ymm2,%ymm8,%ymm9");
		asm volatile ("vpxor    %ymm9,%ymm4,%ymm4");
		asm volatile ("vgf2p8mulb %ymm3,%ymm8,%ymm9");
		asm volatile ("vpxor    %ymm9,%ymm5,%ymm5");

		asm volatile ("vmovdqa %%ymm4,%0" : "=m" (pa[0][i]));
		asm volatile ("vmovdqa %%ymm5,%0" : "=m" (pa[0][i + 32]));

		asm volatile ("vpbroadcastb %0,%%ymm8" : : "m" (V[2]));
		asm volatile ("vgf2p8mulb %ymm0,%ymm8,%ymm6");
		asm volatile ("vgf2p8mulb %ymm1,%ymm8,%ymm7");

		asm volatile ("vpbroadcastb %0,%%ymm8" : : "m" (V[3]));
		asm volatile ("vgf2p8mulb %ymm2,%ymm8,%ymm9");
		asm volatile ("vpxor    %ymm9,%ymm6,%ymm6");
		asm volatile ("vgf2p8mulb %ymm3,%ymm8,%ymm9");
		asm volatile ("vpxor    %ymm9,%ymm7,%ymm7");

		asm volatile ("vmovdqa %%ymm6,%0" : "=m" (pa[1][i]));
		asm volatile ("vmovdqa %%ymm7,%0" : "=m" (pa[1][i + 32]));
	}

	raid_avx_end();
}

/*
 * RAID recovering for two disks GFNI implementation
 */
void raid_rec2_avx512gfni(int nr, int *id, int *ip, int nd, size_t size, void **vv)
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

	for (i = 0; i < size; i += 64) {
		asm volatile ("vmovdqa64 %0,%%zmm0" : : "m" (p[0][i]));
		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (pa[0][i]));
		asm volatile ("vmovdqa64 %0,%%zmm1" : : "m" (p[1][i]));
		asm volatile ("vmovdqa64 %0,%%zmm3" : : "m" (pa[1][i]));
		asm volatile ("vpxorq    %zmm2,%zmm0,%zmm0");
		asm volatile ("vpxorq    %zmm3,%zmm1,%zmm1");

		asm volatile ("vpxorq    %zmm6,%zmm6,%zmm6");

		asm volatile ("vpbroadcastb %0,%%zmm2" : : "m" (V[0]));
		asm volatile ("vgf2p8mulb %zmm0,%zmm2,%zmm2");
		asm volatile ("vpxorq    %zmm2,%zmm6,%zmm6");

		asm volatile ("vpbroadcastb %0,%%zmm3" : : "m" (V[1]));
		asm volatile ("vgf2p8mulb %zmm1,%zmm3,%zmm3");
		asm volatile ("vpxorq    %zmm3,%zmm6,%zmm6");

		asm volatile ("vmovdqa64 %%zmm6,%0" : "=m" (pa[0][i]));

		asm volatile ("vpxorq    %zmm6,%zmm6,%zmm6");

		asm volatile ("vpbroadcastb %0,%%zmm2" : : "m" (V[2]));
		asm volatile ("vgf2p8mulb %zmm0,%zmm2,%zmm2");
		asm volatile ("vpxorq    %zmm2,%zmm6,%zmm6");

		asm volatile ("vpbroadcastb %0,%%zmm3" : : "m" (V[3]));
		asm volatile ("vgf2p8mulb %zmm1,%zmm3,%zmm3");
		asm volatile ("vpxorq    %zmm3,%zmm6,%zmm6");

		asm volatile ("vmovdqa64 %%zmm6,%0" : "=m" (pa[1][i]));
	}

	raid_avx_end();
}

/*
 * RAID recovering AVX2 GFNI implementation
 */
void raid_recX_avx2gfni(int nr, int *id, int *ip, int nd, size_t size, void **vv)
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

	for (i = 0; i < size; i += 64) {
		/* delta */
		for (j = 0; j < N; ++j) {
			asm volatile ("vmovdqa %0,%%ymm0" : : "m" (p[j][i]));
			asm volatile ("vmovdqa %0,%%ymm1" : : "m" (p[j][i + 32]));

			asm volatile ("vmovdqa %0,%%ymm2" : : "m" (pa[j][i]));
			asm volatile ("vmovdqa %0,%%ymm3" : : "m" (pa[j][i + 32]));

			asm volatile ("vpxor    %ymm2,%ymm0,%ymm0");
			asm volatile ("vpxor    %ymm3,%ymm1,%ymm1");

			asm volatile ("vmovdqa %%ymm0,%0" : "=m" (pd[j * 64]));
			asm volatile ("vmovdqa %%ymm1,%0" : "=m" (pd[j * 64 + 32]));
		}

		/* reconstruct */
		for (j = 0; j < N; ++j) {
			asm volatile ("vpxor %ymm0,%ymm0,%ymm0");
			asm volatile ("vpxor %ymm1,%ymm1,%ymm1");
			for (k = 0; k < N; ++k) {
				asm volatile ("vmovdqa %0,%%ymm4" : : "m" (pd[k * 64]));
				asm volatile ("vmovdqa %0,%%ymm5" : : "m" (pd[k * 64 + 32]));

				asm volatile ("vpbroadcastb %0,%%ymm2" : : "m" (V[j * N + k]));

				asm volatile ("vgf2p8mulb %ymm4,%ymm2,%ymm3");
				asm volatile ("vpxor    %ymm3,%ymm0,%ymm0");

				asm volatile ("vgf2p8mulb %ymm5,%ymm2,%ymm3");
				asm volatile ("vpxor    %ymm3,%ymm1,%ymm1");
			}
			asm volatile ("vmovdqa %%ymm0,%0" : "=m" (pa[j][i]));
			asm volatile ("vmovdqa %%ymm1,%0" : "=m" (pa[j][i + 32]));
		}
	}

	raid_avx_end();
}

/*
 * RAID recovering GFNI implementation
 */
void raid_recX_avx512gfni(int nr, int *id, int *ip, int nd, size_t size, void **vv)
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

	for (i = 0; i < size; i += 64) {
		/* delta */
		for (j = 0; j < N; ++j) {
			asm volatile ("vmovdqa64 %0,%%zmm0" : : "m" (p[j][i]));
			asm volatile ("vmovdqa64 %0,%%zmm1" : : "m" (pa[j][i]));
			asm volatile ("vpxorq    %zmm1,%zmm0,%zmm0");
			asm volatile ("vmovdqa64 %%zmm0,%0" : "=m" (pd[j * 64]));
		}

		/* reconstruct */
		for (j = 0; j < N; ++j) {
			asm volatile ("vpxorq %zmm0,%zmm0,%zmm0");
			for (k = 0; k < N; ++k) {
				asm volatile ("vmovdqa64 %0,%%zmm4" : : "m" (pd[k * 64]));
				asm volatile ("vpbroadcastb %0,%%zmm2" : : "m" (V[j * N + k]));
				asm volatile ("vgf2p8mulb %zmm4,%zmm2,%zmm2");
				asm volatile ("vpxorq    %zmm2,%zmm0,%zmm0");
			}
			asm volatile ("vmovdqa64 %%zmm0,%0" : "=m" (pa[j][i]));
		}
	}

	raid_avx_end();
}

void raid_register_avx2gfni(void)
{
#ifdef USE_RAID_AES
	if (raid_cpu_has_avx2gfni()) {
		raid_gen_register(RAID_ALGO_CAUCHY_PAR2, "gfni", raid_gen2_avx2gfni);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR3, "gfni", raid_gen3_avx2gfni);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR4, "gfni", raid_gen4_avx2gfni);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR5, "gfni", raid_gen5_avx2gfni);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR6, "gfni", raid_gen6_avx2gfni);

		raid_rec_register(RAID_ALGO_CAUCHY_PAR1, "gfni", raid_rec1_avx2gfni);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR2, "gfni", raid_rec2_avx2gfni);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR3, "gfni", raid_recX_avx2gfni);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR4, "gfni", raid_recX_avx2gfni);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR5, "gfni", raid_recX_avx2gfni);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR6, "gfni", raid_recX_avx2gfni);
	}
#endif
}

void raid_register_avx512gfni(void)
{
#ifdef USE_RAID_AES
	if (raid_cpu_has_avx512gfni()) {
		if (!raid_cpu_has_slow_avx512()) {
			raid_gen_register(RAID_ALGO_CAUCHY_PAR2, "gfni512", raid_gen2_avx512gfni);
			raid_gen_register(RAID_ALGO_CAUCHY_PAR3, "gfni512", raid_gen3_avx512gfni);
			raid_gen_register(RAID_ALGO_CAUCHY_PAR4, "gfni512", raid_gen4_avx512gfni);
			raid_gen_register(RAID_ALGO_CAUCHY_PAR5, "gfni512", raid_gen5_avx512gfni);
			raid_gen_register(RAID_ALGO_CAUCHY_PAR6, "gfni512", raid_gen6_avx512gfni);

			raid_rec_register(RAID_ALGO_CAUCHY_PAR1, "gfni512", raid_rec1_avx512gfni);
			raid_rec_register(RAID_ALGO_CAUCHY_PAR2, "gfni512", raid_rec2_avx512gfni);
			raid_rec_register(RAID_ALGO_CAUCHY_PAR3, "gfni512", raid_recX_avx512gfni);
			raid_rec_register(RAID_ALGO_CAUCHY_PAR4, "gfni512", raid_recX_avx512gfni);
			raid_rec_register(RAID_ALGO_CAUCHY_PAR5, "gfni512", raid_recX_avx512gfni);
			raid_rec_register(RAID_ALGO_CAUCHY_PAR6, "gfni512", raid_recX_avx512gfni);
		}
	}
#endif
}
#endif
