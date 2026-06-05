// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Andrea Mazzoleni

#include "internal.h"
#include "gf.h"
#include "cpu.h"

#ifdef CONFIG_X86_64

/*
 * GEN1 (RAID5 with xor) AVX512BW implementation
 *
 * Note that in true AVX512F would suffice, but we don't want to add
 * specific support for AVX512F because this would be the only function
 * to benefit from that.
 */
void raid_gen1_avx512bw(int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t**)vv;
	uint8_t *p;
	int d, l;
	size_t i;

	l = nd - 1;
	p = v[nd];

	raid_avx_begin();

	for (i = 0; i < size; i += 64) {
		asm volatile ("vmovdqa64 %0,%%zmm0" : : "m" (v[0][i]));

		for (d = 1; d <= l - 1; d += 2) {
			asm volatile (
				"vmovdqa64 %0,%%zmm1\n\t"
				"vpternlogq $0x96,%1,%%zmm1,%%zmm0"
				:
				: "m" (v[d][i]), "m" (v[d+1][i])
			);
		}

		if (d == l) {
			asm volatile ("vpxorq %0,%%zmm0,%%zmm0" : : "m" (v[l][i]));
		}

		asm volatile ("vmovntdq %%zmm0,%0" : "=m" (p[i]));
	}

	raid_avx_end();
}

/*
 * GENX AVX512BW implementation
 */
static __always_inline void raid_genX_avx512bw(int nd, size_t size, void **vv, int np)
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

	asm volatile ("vpbroadcastb %0,%%zmm31" : : "m" (gfconst16.low4[0]));

	for (i = 0; i < size; i += 64) {
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

		for (d = 1; d < nd; ++d) {
			asm volatile ("vmovdqa64 %0,%%zmm10" : : "m" (v[d][i]));

			asm volatile ("vpxorq    %zmm10,%zmm0,%zmm0");

			asm volatile ("vpsrlw    $4,%zmm10,%zmm11");
			asm volatile ("vpandq    %zmm31,%zmm10,%zmm10");
			asm volatile ("vpandq    %zmm31,%zmm11,%zmm11");

			asm volatile ("vbroadcasti32x4 %0,%%zmm12" : : "m" (gfgenpshufb[d][0][0][0]));
			asm volatile ("vbroadcasti32x4 %0,%%zmm13" : : "m" (gfgenpshufb[d][0][1][0]));
			if (np >= 3) {
				asm volatile ("vbroadcasti32x4 %0,%%zmm14" : : "m" (gfgenpshufb[d][1][0][0]));
				asm volatile ("vbroadcasti32x4 %0,%%zmm15" : : "m" (gfgenpshufb[d][1][1][0]));
			}
			if (np >= 4) {
				asm volatile ("vbroadcasti32x4 %0,%%zmm16" : : "m" (gfgenpshufb[d][2][0][0]));
				asm volatile ("vbroadcasti32x4 %0,%%zmm17" : : "m" (gfgenpshufb[d][2][1][0]));
			}
			if (np >= 5) {
				asm volatile ("vbroadcasti32x4 %0,%%zmm18" : : "m" (gfgenpshufb[d][3][0][0]));
				asm volatile ("vbroadcasti32x4 %0,%%zmm19" : : "m" (gfgenpshufb[d][3][1][0]));
			}
			if (np >= 6) {
				asm volatile ("vbroadcasti32x4 %0,%%zmm20" : : "m" (gfgenpshufb[d][4][0][0]));
				asm volatile ("vbroadcasti32x4 %0,%%zmm21" : : "m" (gfgenpshufb[d][4][1][0]));
			}

			asm volatile ("vpshufb   %zmm10,%zmm12,%zmm12");
			asm volatile ("vpshufb   %zmm11,%zmm13,%zmm13");
			if (np >= 3) {
				asm volatile ("vpshufb   %zmm10,%zmm14,%zmm14");
				asm volatile ("vpshufb   %zmm11,%zmm15,%zmm15");
			}
			if (np >= 4) {
				asm volatile ("vpshufb   %zmm10,%zmm16,%zmm16");
				asm volatile ("vpshufb   %zmm11,%zmm17,%zmm17");
			}
			if (np >= 5) {
				asm volatile ("vpshufb   %zmm10,%zmm18,%zmm18");
				asm volatile ("vpshufb   %zmm11,%zmm19,%zmm19");
			}
			if (np >= 6) {
				asm volatile ("vpshufb   %zmm10,%zmm20,%zmm20");
				asm volatile ("vpshufb   %zmm11,%zmm21,%zmm21");
			}

			asm volatile ("vpternlogq $0x96,%zmm12,%zmm13,%zmm1");
			if (np >= 3)
				asm volatile ("vpternlogq $0x96,%zmm14,%zmm15,%zmm2");
			if (np >= 4)
				asm volatile ("vpternlogq $0x96,%zmm16,%zmm17,%zmm3");
			if (np >= 5)
				asm volatile ("vpternlogq $0x96,%zmm18,%zmm19,%zmm4");
			if (np >= 6)
				asm volatile ("vpternlogq $0x96,%zmm20,%zmm21,%zmm5");
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
 * GEN2 (RAID6 with powers of 2) AVX512BW implementation
 */
void raid_gen2_avx512bw(int nd, size_t size, void **vv)
{
	raid_genX_avx512bw(nd, size, vv, 2);
}

/*
 * GEN3 (triple parity with Cauchy matrix) AVX512BW implementation
 */
void raid_gen3_avx512bw(int nd, size_t size, void **vv)
{
	raid_genX_avx512bw(nd, size, vv, 3);
}

/*
 * GEN4 (quad parity with Cauchy matrix) AVX512BW implementation
 */
void raid_gen4_avx512bw(int nd, size_t size, void **vv)
{
	raid_genX_avx512bw(nd, size, vv, 4);
}

/*
 * GEN5 (penta parity with Cauchy matrix) AVX512BW implementation
 */
void raid_gen5_avx512bw(int nd, size_t size, void **vv)
{
	raid_genX_avx512bw(nd, size, vv, 5);
}

/*
 * GEN6 (hexa parity with Cauchy matrix) AVX512BW implementation
 */
void raid_gen6_avx512bw(int nd, size_t size, void **vv)
{
	raid_genX_avx512bw(nd, size, vv, 6);
}

/*
 * RAID recovering for one disk AVX512BW implementation
 */
void raid_rec1_avx512bw(int nr, int *id, int *ip, int nd, size_t size, void **vv)
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

	asm volatile ("vpbroadcastb %0,%%zmm7" : : "m" (gfconst16.low4[0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm4" : : "m" (gfmulpshufb[V][0][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm5" : : "m" (gfmulpshufb[V][1][0]));

	for (i = 0; i < size; i += 64) {
		asm volatile ("vmovdqa64 %0,%%zmm0" : : "m" (p[i]));
		asm volatile ("vmovdqa64 %0,%%zmm1" : : "m" (pa[i]));
		asm volatile ("vpxord   %zmm1,%zmm0,%zmm0");
		asm volatile ("vpsrlw   $4,%zmm0,%zmm1");
		asm volatile ("vpandd   %zmm7,%zmm0,%zmm0");
		asm volatile ("vpandd   %zmm7,%zmm1,%zmm1");
		asm volatile ("vpshufb  %zmm0,%zmm4,%zmm2");
		asm volatile ("vpshufb  %zmm1,%zmm5,%zmm3");
		asm volatile ("vpxord   %zmm3,%zmm2,%zmm2");
		asm volatile ("vmovdqa64 %%zmm2, %0" : "=m" (pa[i]));
	}

	raid_avx_end();
}

/*
 * RAID recovering for two disks AVX512BW implementation
 */
void raid_rec2_avx512bw(int nr, int *id, int *ip, int nd, size_t size, void **vv)
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

	asm volatile ("vpbroadcastb %0,%%zmm7" : : "m" (gfconst16.low4[0]));

	for (i = 0; i < size; i += 64) {
		asm volatile ("vmovdqa64 %0,%%zmm0" : : "m" (p[0][i]));
		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (pa[0][i]));
		asm volatile ("vmovdqa64 %0,%%zmm1" : : "m" (p[1][i]));
		asm volatile ("vmovdqa64 %0,%%zmm3" : : "m" (pa[1][i]));
		asm volatile ("vpxord   %zmm2,%zmm0,%zmm0");
		asm volatile ("vpxord   %zmm3,%zmm1,%zmm1");

		asm volatile ("vpxord %zmm6,%zmm6,%zmm6");

		asm volatile ("vbroadcasti32x4 %0,%%zmm2" : : "m" (gfmulpshufb[V[0]][0][0]));
		asm volatile ("vbroadcasti32x4 %0,%%zmm3" : : "m" (gfmulpshufb[V[0]][1][0]));
		asm volatile ("vbroadcasti32x4 %0,%%zmm10" : : "m" (gfmulpshufb[V[1]][0][0]));
		asm volatile ("vbroadcasti32x4 %0,%%zmm11" : : "m" (gfmulpshufb[V[1]][1][0]));
		asm volatile ("vpsrlw  $4,%zmm0,%zmm5");
		asm volatile ("vpsrlw  $4,%zmm1,%zmm13");
		asm volatile ("vpandd  %zmm7,%zmm0,%zmm4");
		asm volatile ("vpandd  %zmm7,%zmm5,%zmm5");
		asm volatile ("vpandd  %zmm7,%zmm1,%zmm12");
		asm volatile ("vpandd  %zmm7,%zmm13,%zmm13");
		asm volatile ("vpshufb %zmm4,%zmm2,%zmm2");
		asm volatile ("vpshufb %zmm5,%zmm3,%zmm3");
		asm volatile ("vpshufb %zmm12,%zmm10,%zmm10");
		asm volatile ("vpshufb %zmm13,%zmm11,%zmm11");
		asm volatile ("vpxord  %zmm2,%zmm6,%zmm6");
		asm volatile ("vpxord  %zmm3,%zmm6,%zmm6");
		asm volatile ("vpxord  %zmm10,%zmm6,%zmm6");
		asm volatile ("vpxord  %zmm11,%zmm6,%zmm6");

		asm volatile ("vmovdqa64 %%zmm6,%0" : "=m" (pa[0][i]));

		asm volatile ("vpxord %zmm6,%zmm6,%zmm6");

		asm volatile ("vbroadcasti32x4 %0,%%zmm2" : : "m" (gfmulpshufb[V[2]][0][0]));
		asm volatile ("vbroadcasti32x4 %0,%%zmm3" : : "m" (gfmulpshufb[V[2]][1][0]));
		asm volatile ("vbroadcasti32x4 %0,%%zmm10" : : "m" (gfmulpshufb[V[3]][0][0]));
		asm volatile ("vbroadcasti32x4 %0,%%zmm11" : : "m" (gfmulpshufb[V[3]][1][0]));
		asm volatile ("vpsrlw  $4,%zmm0,%zmm5");
		asm volatile ("vpsrlw  $4,%zmm1,%zmm13");
		asm volatile ("vpandd  %zmm7,%zmm0,%zmm4");
		asm volatile ("vpandd  %zmm7,%zmm5,%zmm5");
		asm volatile ("vpandd  %zmm7,%zmm1,%zmm12");
		asm volatile ("vpandd  %zmm7,%zmm13,%zmm13");
		asm volatile ("vpshufb %zmm4,%zmm2,%zmm2");
		asm volatile ("vpshufb %zmm5,%zmm3,%zmm3");
		asm volatile ("vpshufb %zmm12,%zmm10,%zmm10");
		asm volatile ("vpshufb %zmm13,%zmm11,%zmm11");
		asm volatile ("vpxord  %zmm2,%zmm6,%zmm6");
		asm volatile ("vpxord  %zmm3,%zmm6,%zmm6");
		asm volatile ("vpxord  %zmm10,%zmm6,%zmm6");
		asm volatile ("vpxord  %zmm11,%zmm6,%zmm6");

		asm volatile ("vmovdqa64 %%zmm6,%0" : "=m" (pa[1][i]));
	}

	raid_avx_end();
}

/*
 * RAID recovering AVX512BW implementation
 */
void raid_recX_avx512bw(int nr, int *id, int *ip, int nd, size_t size, void **vv)
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

	asm volatile ("vpbroadcastb %0,%%zmm7" : : "m" (gfconst16.low4[0]));

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
			asm volatile ("vpxorq %zmm1,%zmm1,%zmm1");
			for (k = 0; k < N; ++k) {
				uint8_t m = V[j * N + k];
				asm volatile ("vbroadcasti32x4 %0,%%zmm2" : : "m" (gfmulpshufb[m][0][0]));
				asm volatile ("vbroadcasti32x4 %0,%%zmm3" : : "m" (gfmulpshufb[m][1][0]));
				asm volatile ("vmovdqa64 %0,%%zmm4" : : "m" (pd[k*64]));
				asm volatile ("vpsrlw    $4,%zmm4,%zmm5");
				asm volatile ("vpandq    %zmm7,%zmm4,%zmm4");
				asm volatile ("vpandq    %zmm7,%zmm5,%zmm5");
				asm volatile ("vpshufb   %zmm4,%zmm2,%zmm2");
				asm volatile ("vpshufb   %zmm5,%zmm3,%zmm3");
				asm volatile ("vpxorq    %zmm2,%zmm0,%zmm0");
				asm volatile ("vpxorq    %zmm3,%zmm1,%zmm1");
			}
			asm volatile ("vpxorq    %zmm1,%zmm0,%zmm0");
			asm volatile ("vmovdqa64 %%zmm0,%0" : "=m" (pa[j][i]));
		}
	}

	raid_avx_end();
}

void raid_register_avx512(void)
{
	if (raid_cpu_has_avx512bw()) {
		if (!raid_cpu_has_slow_avx512()) {
			raid_gen_register(RAID_ALGO_CAUCHY_PAR1, "avx512", raid_gen1_avx512bw);
			raid_gen_register(RAID_ALGO_CAUCHY_PAR2, "avx512", raid_gen2_avx512bw);
			raid_gen_register(RAID_ALGO_CAUCHY_PAR3, "avx512", raid_gen3_avx512bw);
			raid_gen_register(RAID_ALGO_CAUCHY_PAR4, "avx512", raid_gen4_avx512bw);
			raid_gen_register(RAID_ALGO_CAUCHY_PAR5, "avx512", raid_gen5_avx512bw);
			raid_gen_register(RAID_ALGO_CAUCHY_PAR6, "avx512", raid_gen6_avx512bw);

			raid_rec_register(RAID_ALGO_CAUCHY_PAR1, "avx512", raid_rec1_avx512bw);
			raid_rec_register(RAID_ALGO_CAUCHY_PAR2, "avx512", raid_rec2_avx512bw);
			raid_rec_register(RAID_ALGO_CAUCHY_PAR3, "avx512", raid_recX_avx512bw);
			raid_rec_register(RAID_ALGO_CAUCHY_PAR4, "avx512", raid_recX_avx512bw);
			raid_rec_register(RAID_ALGO_CAUCHY_PAR5, "avx512", raid_recX_avx512bw);
			raid_rec_register(RAID_ALGO_CAUCHY_PAR6, "avx512", raid_recX_avx512bw);
		}
	}
}
#endif
