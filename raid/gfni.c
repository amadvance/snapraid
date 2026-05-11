// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Andrea Mazzoleni

#include "internal.h"
#include "gf.h"
#include "cpu.h"

#if defined(CONFIG_X86_64) && defined(CONFIG_AVX512GFNI)
/*
 * GEN2 (RAID6 with powers of 2) GFNI implementation
 */
void raid_gen2_avx512gfni(int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p = v[nd];
	uint8_t *q = v[nd + 1];
	int d, l = nd - 1;
	size_t i;
	uint8_t gf2 = 0x02;

	raid_avx_begin();

	asm volatile ("vpbroadcastb %0,%%zmm31" : : "m" (gf2));

	for (i = 0; i < size; i += 64) {
		asm volatile ("vmovdqa64 %0, %%zmm0" : : "m" (v[l][i]));
		asm volatile ("vmovdqa64 %zmm0, %zmm1");

		for (d = l - 1; d >= 0; --d) {
			asm volatile ("vgf2p8mulb %zmm31, %zmm1, %zmm1");
			asm volatile ("vmovdqa64 %0, %%zmm2"  : : "m" (v[d][i]));
			asm volatile ("vpxorq %zmm2,  %zmm0, %zmm0");
			asm volatile ("vpxorq %zmm2,  %zmm1, %zmm1");
		}

		asm volatile ("vmovntdq %%zmm0, %0" : "=m" (p[i]));
		asm volatile ("vmovntdq %%zmm1, %0" : "=m" (q[i]));
	}

	raid_avx_end();
}
#endif

#if defined(CONFIG_X86_64) && defined(CONFIG_AVX512GFNI)
/*
 * GEN3 (triple parity with Cauchy matrix) GFNI implementation
 */
void raid_gen3_avx512gfni(int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p = v[nd];
	uint8_t *q = v[nd + 1];
	uint8_t *r = v[nd + 2];
	int d, l = nd - 1;
	size_t i;

	if (l == 0) {
		for (i = 0; i < 3; ++i)
			memcpy(v[1 + i], v[0], size);
		return;
	}

	raid_avx_begin();

	for (i = 0; i < size; i += 64) {
		/* last disk without the by two multiplication */
		asm volatile ("vmovdqa64 %0,%%zmm6" : : "m" (v[l][i]));

		asm volatile ("vpbroadcastb %0,%%zmm7" : : "m" (gfcauchy[1][l]));
		asm volatile ("vpbroadcastb %0,%%zmm8" : : "m" (gfcauchy[2][l]));

		asm volatile ("vmovdqa64  %zmm6,%zmm0");
		asm volatile ("vgf2p8mulb %zmm6,%zmm7,%zmm1");
		asm volatile ("vgf2p8mulb %zmm6,%zmm8,%zmm2");

		/* intermediate disks */
		for (d = l - 1; d > 0; --d) {
			asm volatile ("vmovdqa64 %0,%%zmm6" : : "m" (v[d][i]));

			asm volatile ("vpbroadcastb %0,%%zmm7" : : "m" (gfcauchy[1][d]));
			asm volatile ("vpbroadcastb %0,%%zmm8" : : "m" (gfcauchy[2][d]));

			asm volatile ("vpxorq     %zmm6,%zmm0,%zmm0");
			asm volatile ("vgf2p8mulb %zmm6,%zmm7,%zmm12");
			asm volatile ("vgf2p8mulb %zmm6,%zmm8,%zmm13");

			asm volatile ("vpxorq    %zmm12,%zmm1,%zmm1");
			asm volatile ("vpxorq    %zmm13,%zmm2,%zmm2");
		}

		/* first disk with all coefficients at 1 */
		asm volatile ("vmovdqa64 %0,%%zmm6" : : "m" (v[0][i]));

		asm volatile ("vpxorq    %zmm6,%zmm0,%zmm0");
		asm volatile ("vpxorq    %zmm6,%zmm1,%zmm1");
		asm volatile ("vpxorq    %zmm6,%zmm2,%zmm2");

		asm volatile ("vmovntdq  %%zmm0,%0" : "=m" (p[i]));
		asm volatile ("vmovntdq  %%zmm1,%0" : "=m" (q[i]));
		asm volatile ("vmovntdq  %%zmm2,%0" : "=m" (r[i]));
	}

	raid_avx_end();
}
#endif

#if defined(CONFIG_X86_64) && defined(CONFIG_AVX512GFNI)
/*
 * GEN4 (quad parity with Cauchy matrix) GFNI implementation
 */
void raid_gen4_avx512gfni(int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p = v[nd];
	uint8_t *q = v[nd + 1];
	uint8_t *r = v[nd + 2];
	uint8_t *s = v[nd + 3];
	int d, l = nd - 1;
	size_t i;

	if (l == 0) {
		for (i = 0; i < 4; ++i)
			memcpy(v[1 + i], v[0], size);
		return;
	}

	raid_avx_begin();

	for (i = 0; i < size; i += 64) {
		/* last disk without the by two multiplication */
		asm volatile ("vmovdqa64 %0,%%zmm6" : : "m" (v[l][i]));

		asm volatile ("vpbroadcastb %0,%%zmm7" : : "m" (gfcauchy[1][l]));
		asm volatile ("vpbroadcastb %0,%%zmm8" : : "m" (gfcauchy[2][l]));
		asm volatile ("vpbroadcastb %0,%%zmm9" : : "m" (gfcauchy[3][l]));

		asm volatile ("vmovdqa64  %zmm6,%zmm0");
		asm volatile ("vgf2p8mulb %zmm6,%zmm7,%zmm1");
		asm volatile ("vgf2p8mulb %zmm6,%zmm8,%zmm2");
		asm volatile ("vgf2p8mulb %zmm6,%zmm9,%zmm3");

		/* intermediate disks */
		for (d = l - 1; d > 0; --d) {
			asm volatile ("vmovdqa64 %0,%%zmm6" : : "m" (v[d][i]));

			asm volatile ("vpbroadcastb %0,%%zmm7" : : "m" (gfcauchy[1][d]));
			asm volatile ("vpbroadcastb %0,%%zmm8" : : "m" (gfcauchy[2][d]));
			asm volatile ("vpbroadcastb %0,%%zmm9" : : "m" (gfcauchy[3][d]));

			asm volatile ("vpxorq     %zmm6,%zmm0,%zmm0");
			asm volatile ("vgf2p8mulb %zmm6,%zmm7,%zmm12");
			asm volatile ("vgf2p8mulb %zmm6,%zmm8,%zmm13");
			asm volatile ("vgf2p8mulb %zmm6,%zmm9,%zmm14");

			asm volatile ("vpxorq    %zmm12,%zmm1,%zmm1");
			asm volatile ("vpxorq    %zmm13,%zmm2,%zmm2");
			asm volatile ("vpxorq    %zmm14,%zmm3,%zmm3");
		}

		/* first disk with all coefficients at 1 */
		asm volatile ("vmovdqa64 %0,%%zmm6" : : "m" (v[0][i]));

		asm volatile ("vpxorq    %zmm6,%zmm0,%zmm0");
		asm volatile ("vpxorq    %zmm6,%zmm1,%zmm1");
		asm volatile ("vpxorq    %zmm6,%zmm2,%zmm2");
		asm volatile ("vpxorq    %zmm6,%zmm3,%zmm3");

		asm volatile ("vmovntdq  %%zmm0,%0" : "=m" (p[i]));
		asm volatile ("vmovntdq  %%zmm1,%0" : "=m" (q[i]));
		asm volatile ("vmovntdq  %%zmm2,%0" : "=m" (r[i]));
		asm volatile ("vmovntdq  %%zmm3,%0" : "=m" (s[i]));
	}

	raid_avx_end();
}
#endif

#if defined(CONFIG_X86_64) && defined(CONFIG_AVX512GFNI)
/*
 * GEN5 (penta parity with Cauchy matrix) GFNI implementation
 */
void raid_gen5_avx512gfni(int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p = v[nd];
	uint8_t *q = v[nd + 1];
	uint8_t *r = v[nd + 2];
	uint8_t *s = v[nd + 3];
	uint8_t *t = v[nd + 4];
	int d, l = nd - 1;
	size_t i;

	if (l == 0) {
		for (i = 0; i < 5; ++i)
			memcpy(v[1 + i], v[0], size);
		return;
	}

	raid_avx_begin();

	for (i = 0; i < size; i += 64) {
		/* last disk without the by two multiplication */
		asm volatile ("vmovdqa64 %0,%%zmm6" : : "m" (v[l][i]));

		asm volatile ("vpbroadcastb %0,%%zmm7" : : "m" (gfcauchy[1][l]));
		asm volatile ("vpbroadcastb %0,%%zmm8" : : "m" (gfcauchy[2][l]));
		asm volatile ("vpbroadcastb %0,%%zmm9" : : "m" (gfcauchy[3][l]));
		asm volatile ("vpbroadcastb %0,%%zmm10" : : "m" (gfcauchy[4][l]));

		asm volatile ("vmovdqa64  %zmm6,%zmm0");
		asm volatile ("vgf2p8mulb %zmm6,%zmm7,%zmm1");
		asm volatile ("vgf2p8mulb %zmm6,%zmm8,%zmm2");
		asm volatile ("vgf2p8mulb %zmm6,%zmm9,%zmm3");
		asm volatile ("vgf2p8mulb %zmm6,%zmm10,%zmm4");

		/* intermediate disks */
		for (d = l - 1; d > 0; --d) {
			asm volatile ("vmovdqa64 %0,%%zmm6" : : "m" (v[d][i]));

			asm volatile ("vpbroadcastb %0,%%zmm7" : : "m" (gfcauchy[1][d]));
			asm volatile ("vpbroadcastb %0,%%zmm8" : : "m" (gfcauchy[2][d]));
			asm volatile ("vpbroadcastb %0,%%zmm9" : : "m" (gfcauchy[3][d]));
			asm volatile ("vpbroadcastb %0,%%zmm10" : : "m" (gfcauchy[4][d]));

			asm volatile ("vpxorq     %zmm6,%zmm0,%zmm0");
			asm volatile ("vgf2p8mulb %zmm6,%zmm7,%zmm12");
			asm volatile ("vgf2p8mulb %zmm6,%zmm8,%zmm13");
			asm volatile ("vgf2p8mulb %zmm6,%zmm9,%zmm14");
			asm volatile ("vgf2p8mulb %zmm6,%zmm10,%zmm15");

			asm volatile ("vpxorq    %zmm12,%zmm1,%zmm1");
			asm volatile ("vpxorq    %zmm13,%zmm2,%zmm2");
			asm volatile ("vpxorq    %zmm14,%zmm3,%zmm3");
			asm volatile ("vpxorq    %zmm15,%zmm4,%zmm4");
		}

		/* first disk with all coefficients at 1 */
		asm volatile ("vmovdqa64 %0,%%zmm6" : : "m" (v[0][i]));

		asm volatile ("vpxorq    %zmm6,%zmm0,%zmm0");
		asm volatile ("vpxorq    %zmm6,%zmm1,%zmm1");
		asm volatile ("vpxorq    %zmm6,%zmm2,%zmm2");
		asm volatile ("vpxorq    %zmm6,%zmm3,%zmm3");
		asm volatile ("vpxorq    %zmm6,%zmm4,%zmm4");

		asm volatile ("vmovntdq  %%zmm0,%0" : "=m" (p[i]));
		asm volatile ("vmovntdq  %%zmm1,%0" : "=m" (q[i]));
		asm volatile ("vmovntdq  %%zmm2,%0" : "=m" (r[i]));
		asm volatile ("vmovntdq  %%zmm3,%0" : "=m" (s[i]));
		asm volatile ("vmovntdq  %%zmm4,%0" : "=m" (t[i]));
	}

	raid_avx_end();
}
#endif

#if defined(CONFIG_X86_64) && defined(CONFIG_AVX512GFNI)
/*
 * GEN6 (hexa parity with Cauchy matrix) GFNI implementation
 */
void raid_gen6_avx512gfni(int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p = v[nd];
	uint8_t *q = v[nd + 1];
	uint8_t *r = v[nd + 2];
	uint8_t *s = v[nd + 3];
	uint8_t *t = v[nd + 4];
	uint8_t *u = v[nd + 5];
	int d, l = nd - 1;
	size_t i;

	if (l == 0) {
		for (i = 0; i < 6; ++i)
			memcpy(v[1 + i], v[0], size);
		return;
	}

	raid_avx_begin();

	for (i = 0; i < size; i += 64) {
		/* last disk without the by two multiplication */
		asm volatile ("vmovdqa64 %0,%%zmm6" : : "m" (v[l][i]));

		asm volatile ("vpbroadcastb %0,%%zmm7" : : "m" (gfcauchy[1][l]));
		asm volatile ("vpbroadcastb %0,%%zmm8" : : "m" (gfcauchy[2][l]));
		asm volatile ("vpbroadcastb %0,%%zmm9" : : "m" (gfcauchy[3][l]));
		asm volatile ("vpbroadcastb %0,%%zmm10" : : "m" (gfcauchy[4][l]));
		asm volatile ("vpbroadcastb %0,%%zmm11" : : "m" (gfcauchy[5][l]));

		asm volatile ("vmovdqa64  %zmm6,%zmm0");
		asm volatile ("vgf2p8mulb %zmm6,%zmm7,%zmm1");
		asm volatile ("vgf2p8mulb %zmm6,%zmm8,%zmm2");
		asm volatile ("vgf2p8mulb %zmm6,%zmm9,%zmm3");
		asm volatile ("vgf2p8mulb %zmm6,%zmm10,%zmm4");
		asm volatile ("vgf2p8mulb %zmm6,%zmm11,%zmm5");

		/* intermediate disks */
		for (d = l - 1; d > 0; --d) {
			asm volatile ("vmovdqa64 %0,%%zmm6" : : "m" (v[d][i]));

			asm volatile ("vpbroadcastb %0,%%zmm7" : : "m" (gfcauchy[1][d]));
			asm volatile ("vpbroadcastb %0,%%zmm8" : : "m" (gfcauchy[2][d]));
			asm volatile ("vpbroadcastb %0,%%zmm9" : : "m" (gfcauchy[3][d]));
			asm volatile ("vpbroadcastb %0,%%zmm10" : : "m" (gfcauchy[4][d]));
			asm volatile ("vpbroadcastb %0,%%zmm11" : : "m" (gfcauchy[5][d]));

			asm volatile ("vpxorq     %zmm6,%zmm0,%zmm0");
			asm volatile ("vgf2p8mulb %zmm6,%zmm7,%zmm12");
			asm volatile ("vgf2p8mulb %zmm6,%zmm8,%zmm13");
			asm volatile ("vgf2p8mulb %zmm6,%zmm9,%zmm14");
			asm volatile ("vgf2p8mulb %zmm6,%zmm10,%zmm15");
			asm volatile ("vgf2p8mulb %zmm6,%zmm11,%zmm16");

			asm volatile ("vpxorq    %zmm12,%zmm1,%zmm1");
			asm volatile ("vpxorq    %zmm13,%zmm2,%zmm2");
			asm volatile ("vpxorq    %zmm14,%zmm3,%zmm3");
			asm volatile ("vpxorq    %zmm15,%zmm4,%zmm4");
			asm volatile ("vpxorq    %zmm16,%zmm5,%zmm5");
		}

		/* first disk with all coefficients at 1 */
		asm volatile ("vmovdqa64 %0,%%zmm6" : : "m" (v[0][i]));

		asm volatile ("vpxorq    %zmm6,%zmm0,%zmm0");
		asm volatile ("vpxorq    %zmm6,%zmm1,%zmm1");
		asm volatile ("vpxorq    %zmm6,%zmm2,%zmm2");
		asm volatile ("vpxorq    %zmm6,%zmm3,%zmm3");
		asm volatile ("vpxorq    %zmm6,%zmm4,%zmm4");
		asm volatile ("vpxorq    %zmm6,%zmm5,%zmm5");

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

#if defined(CONFIG_X86_64) && defined(CONFIG_AVX512GFNI)
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
#endif

#if defined(CONFIG_X86_64) && defined(CONFIG_AVX512GFNI)
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

		/*
		 * pa[0] = V[0]*delta[0] ^ V[1]*delta[1]
		 */
		asm volatile ("vpbroadcastb %0,%%zmm2" : : "m" (V[0]));
		asm volatile ("vgf2p8mulb %zmm0,%zmm2,%zmm2");
		asm volatile ("vpxorq    %zmm2,%zmm6,%zmm6");

		asm volatile ("vpbroadcastb %0,%%zmm3" : : "m" (V[1]));
		asm volatile ("vgf2p8mulb %zmm1,%zmm3,%zmm3");
		asm volatile ("vpxorq    %zmm3,%zmm6,%zmm6");

		asm volatile ("vmovdqa64 %%zmm6,%0" : "=m" (pa[0][i]));

		asm volatile ("vpxorq    %zmm6,%zmm6,%zmm6");

		/* pa[1] = V[2]*delta[0] ^ V[3]*delta[1] */
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
#endif

#if defined(CONFIG_X86_64) && defined(CONFIG_AVX512GFNI)
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
			asm volatile ("vmovdqa64 %%zmm0,%0" : "=m" (pd[j*64]));
		}
		/* reconstruct */
		for (j = 0; j < N; ++j) {
			asm volatile ("vpxorq %zmm0,%zmm0,%zmm0");
			for (k = 0; k < N; ++k) {
				asm volatile ("vmovdqa64 %0,%%zmm4" : : "m" (pd[k*64]));
				asm volatile ("vpbroadcastb %0,%%zmm2" : : "m" (V[j * N + k]));
				asm volatile ("vgf2p8mulb %zmm4,%zmm2,%zmm2");
				asm volatile ("vpxorq    %zmm2,%zmm0,%zmm0");
			}
			asm volatile ("vmovdqa64 %%zmm0,%0" : "=m" (pa[j][i]));
		}
	}

	raid_avx_end();
}
#endif

#if defined(CONFIG_X86_64) && defined(CONFIG_AVX512GFNI)
void raid_register_avx512gfni(void)
{
#if defined(USE_RAID_AES)
	if (raid_cpu_has_avx512gfni()) {
		raid_gen_register(RAID_ALGO_CAUCHY_PAR2, "gfni", raid_gen2_avx512gfni);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR3, "gfni", raid_gen3_avx512gfni);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR4, "gfni", raid_gen4_avx512gfni);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR5, "gfni", raid_gen5_avx512gfni);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR6, "gfni", raid_gen6_avx512gfni);

		raid_rec_register(RAID_ALGO_CAUCHY_PAR1, "gfni", raid_rec1_avx512gfni);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR2, "gfni", raid_rec2_avx512gfni);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR3, "gfni", raid_recX_avx512gfni);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR4, "gfni", raid_recX_avx512gfni);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR5, "gfni", raid_recX_avx512gfni);
		raid_rec_register(RAID_ALGO_CAUCHY_PAR6, "gfni", raid_recX_avx512gfni);
	}
#endif
}
#endif
