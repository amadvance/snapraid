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
	uint8_t **v = (uint8_t **)vv;
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
				: "m" (v[d][i]), "m" (v[d + 1][i])
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
 * GEN2 Cauchy AVX512BW implementation
 */
void raid_gen2_avx512bw(int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	size_t i;
	int d;

	if (nd == 1) {
		memcpy(v[1], v[0], size);
		memcpy(v[2], v[0], size);
		return;
	}

	raid_avx_begin();

	asm volatile ("vpbroadcastb %0,%%zmm31" : : "m" (gfconst16.low4[0]));

	/* preload as many Q coefficient tables as possible */
	asm volatile ("vbroadcasti32x4 %0,%%zmm4" : : "m" (raid_gfcauchypshufb[1][0][0][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm5" : : "m" (raid_gfcauchypshufb[1][0][1][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm6" : : "m" (raid_gfcauchypshufb[2][0][0][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm7" : : "m" (raid_gfcauchypshufb[2][0][1][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm8" : : "m" (raid_gfcauchypshufb[3][0][0][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm9" : : "m" (raid_gfcauchypshufb[3][0][1][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm10" : : "m" (raid_gfcauchypshufb[4][0][0][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm11" : : "m" (raid_gfcauchypshufb[4][0][1][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm12" : : "m" (raid_gfcauchypshufb[5][0][0][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm13" : : "m" (raid_gfcauchypshufb[5][0][1][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm14" : : "m" (raid_gfcauchypshufb[6][0][0][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm15" : : "m" (raid_gfcauchypshufb[6][0][1][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm16" : : "m" (raid_gfcauchypshufb[7][0][0][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm17" : : "m" (raid_gfcauchypshufb[7][0][1][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm18" : : "m" (raid_gfcauchypshufb[8][0][0][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm19" : : "m" (raid_gfcauchypshufb[8][0][1][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm20" : : "m" (raid_gfcauchypshufb[9][0][0][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm21" : : "m" (raid_gfcauchypshufb[9][0][1][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm22" : : "m" (raid_gfcauchypshufb[10][0][0][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm23" : : "m" (raid_gfcauchypshufb[10][0][1][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm24" : : "m" (raid_gfcauchypshufb[11][0][0][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm25" : : "m" (raid_gfcauchypshufb[11][0][1][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm26" : : "m" (raid_gfcauchypshufb[12][0][0][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm27" : : "m" (raid_gfcauchypshufb[12][0][1][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm28" : : "m" (raid_gfcauchypshufb[13][0][0][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm29" : : "m" (raid_gfcauchypshufb[13][0][1][0]));

	for (i = 0; i < size; i += 64) {
		asm volatile ("vmovdqa64 %0,%%zmm0" : : "m" (v[0][i]));
		asm volatile ("vmovdqa64 %zmm0,%zmm1");

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[1][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vpsrlw $4,%zmm2,%zmm3");
		asm volatile ("vpandq %zmm31,%zmm2,%zmm2");
		asm volatile ("vpandq %zmm31,%zmm3,%zmm3");
		asm volatile ("vpshufb %zmm2,%zmm4,%zmm2");
		asm volatile ("vpshufb %zmm3,%zmm5,%zmm3");
		asm volatile ("vpternlogq $0x96,%zmm2,%zmm3,%zmm1");
		if (nd == 2)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[2][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vpsrlw $4,%zmm2,%zmm3");
		asm volatile ("vpandq %zmm31,%zmm2,%zmm2");
		asm volatile ("vpandq %zmm31,%zmm3,%zmm3");
		asm volatile ("vpshufb %zmm2,%zmm6,%zmm2");
		asm volatile ("vpshufb %zmm3,%zmm7,%zmm3");
		asm volatile ("vpternlogq $0x96,%zmm2,%zmm3,%zmm1");
		if (nd == 3)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[3][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vpsrlw $4,%zmm2,%zmm3");
		asm volatile ("vpandq %zmm31,%zmm2,%zmm2");
		asm volatile ("vpandq %zmm31,%zmm3,%zmm3");
		asm volatile ("vpshufb %zmm2,%zmm8,%zmm2");
		asm volatile ("vpshufb %zmm3,%zmm9,%zmm3");
		asm volatile ("vpternlogq $0x96,%zmm2,%zmm3,%zmm1");
		if (nd == 4)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[4][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vpsrlw $4,%zmm2,%zmm3");
		asm volatile ("vpandq %zmm31,%zmm2,%zmm2");
		asm volatile ("vpandq %zmm31,%zmm3,%zmm3");
		asm volatile ("vpshufb %zmm2,%zmm10,%zmm2");
		asm volatile ("vpshufb %zmm3,%zmm11,%zmm3");
		asm volatile ("vpternlogq $0x96,%zmm2,%zmm3,%zmm1");
		if (nd == 5)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[5][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vpsrlw $4,%zmm2,%zmm3");
		asm volatile ("vpandq %zmm31,%zmm2,%zmm2");
		asm volatile ("vpandq %zmm31,%zmm3,%zmm3");
		asm volatile ("vpshufb %zmm2,%zmm12,%zmm2");
		asm volatile ("vpshufb %zmm3,%zmm13,%zmm3");
		asm volatile ("vpternlogq $0x96,%zmm2,%zmm3,%zmm1");
		if (nd == 6)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[6][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vpsrlw $4,%zmm2,%zmm3");
		asm volatile ("vpandq %zmm31,%zmm2,%zmm2");
		asm volatile ("vpandq %zmm31,%zmm3,%zmm3");
		asm volatile ("vpshufb %zmm2,%zmm14,%zmm2");
		asm volatile ("vpshufb %zmm3,%zmm15,%zmm3");
		asm volatile ("vpternlogq $0x96,%zmm2,%zmm3,%zmm1");
		if (nd == 7)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[7][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vpsrlw $4,%zmm2,%zmm3");
		asm volatile ("vpandq %zmm31,%zmm2,%zmm2");
		asm volatile ("vpandq %zmm31,%zmm3,%zmm3");
		asm volatile ("vpshufb %zmm2,%zmm16,%zmm2");
		asm volatile ("vpshufb %zmm3,%zmm17,%zmm3");
		asm volatile ("vpternlogq $0x96,%zmm2,%zmm3,%zmm1");
		if (nd == 8)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[8][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vpsrlw $4,%zmm2,%zmm3");
		asm volatile ("vpandq %zmm31,%zmm2,%zmm2");
		asm volatile ("vpandq %zmm31,%zmm3,%zmm3");
		asm volatile ("vpshufb %zmm2,%zmm18,%zmm2");
		asm volatile ("vpshufb %zmm3,%zmm19,%zmm3");
		asm volatile ("vpternlogq $0x96,%zmm2,%zmm3,%zmm1");
		if (nd == 9)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[9][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vpsrlw $4,%zmm2,%zmm3");
		asm volatile ("vpandq %zmm31,%zmm2,%zmm2");
		asm volatile ("vpandq %zmm31,%zmm3,%zmm3");
		asm volatile ("vpshufb %zmm2,%zmm20,%zmm2");
		asm volatile ("vpshufb %zmm3,%zmm21,%zmm3");
		asm volatile ("vpternlogq $0x96,%zmm2,%zmm3,%zmm1");
		if (nd == 10)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[10][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vpsrlw $4,%zmm2,%zmm3");
		asm volatile ("vpandq %zmm31,%zmm2,%zmm2");
		asm volatile ("vpandq %zmm31,%zmm3,%zmm3");
		asm volatile ("vpshufb %zmm2,%zmm22,%zmm2");
		asm volatile ("vpshufb %zmm3,%zmm23,%zmm3");
		asm volatile ("vpternlogq $0x96,%zmm2,%zmm3,%zmm1");
		if (nd == 11)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[11][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vpsrlw $4,%zmm2,%zmm3");
		asm volatile ("vpandq %zmm31,%zmm2,%zmm2");
		asm volatile ("vpandq %zmm31,%zmm3,%zmm3");
		asm volatile ("vpshufb %zmm2,%zmm24,%zmm2");
		asm volatile ("vpshufb %zmm3,%zmm25,%zmm3");
		asm volatile ("vpternlogq $0x96,%zmm2,%zmm3,%zmm1");
		if (nd == 12)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[12][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vpsrlw $4,%zmm2,%zmm3");
		asm volatile ("vpandq %zmm31,%zmm2,%zmm2");
		asm volatile ("vpandq %zmm31,%zmm3,%zmm3");
		asm volatile ("vpshufb %zmm2,%zmm26,%zmm2");
		asm volatile ("vpshufb %zmm3,%zmm27,%zmm3");
		asm volatile ("vpternlogq $0x96,%zmm2,%zmm3,%zmm1");
		if (nd == 13)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[13][i]));
		asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");
		asm volatile ("vpsrlw $4,%zmm2,%zmm3");
		asm volatile ("vpandq %zmm31,%zmm2,%zmm2");
		asm volatile ("vpandq %zmm31,%zmm3,%zmm3");
		asm volatile ("vpshufb %zmm2,%zmm28,%zmm2");
		asm volatile ("vpshufb %zmm3,%zmm29,%zmm3");
		asm volatile ("vpternlogq $0x96,%zmm2,%zmm3,%zmm1");
		if (nd == 14)
			goto store;

		/*
		 * No more registers are available for resident coefficient
		 * tables. Process D14 and later with the normal loop.
		 */
		for (d = 14; d < nd; ++d) {
			asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (v[d][i]));

			asm volatile ("vpxorq %zmm2,%zmm0,%zmm0");

			asm volatile ("vpsrlw $4,%zmm2,%zmm3");
			asm volatile ("vpandq %zmm31,%zmm2,%zmm2");
			asm volatile ("vpandq %zmm31,%zmm3,%zmm3");

			asm volatile ("vbroadcasti32x4 %0,%%zmm30" : : "m" (raid_gfcauchypshufb[d][0][0][0]));
			asm volatile ("vpshufb %zmm2,%zmm30,%zmm2");

			asm volatile ("vbroadcasti32x4 %0,%%zmm30" : : "m" (raid_gfcauchypshufb[d][0][1][0]));
			asm volatile ("vpshufb %zmm3,%zmm30,%zmm3");

			asm volatile ("vpternlogq $0x96,%zmm2,%zmm3,%zmm1");
		}

store:
		asm volatile ("vmovntdq %%zmm0,%0" : "=m" (v[nd][i]));
		asm volatile ("vmovntdq %%zmm1,%0" : "=m" (v[nd + 1][i]));
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

			asm volatile ("vbroadcasti32x4 %0,%%zmm12" : : "m" (raid_gfcauchypshufb[d][0][0][0]));
			asm volatile ("vbroadcasti32x4 %0,%%zmm13" : : "m" (raid_gfcauchypshufb[d][0][1][0]));
			if (np >= 3) {
				asm volatile ("vbroadcasti32x4 %0,%%zmm14" : : "m" (raid_gfcauchypshufb[d][1][0][0]));
				asm volatile ("vbroadcasti32x4 %0,%%zmm15" : : "m" (raid_gfcauchypshufb[d][1][1][0]));
			}
			if (np >= 4) {
				asm volatile ("vbroadcasti32x4 %0,%%zmm16" : : "m" (raid_gfcauchypshufb[d][2][0][0]));
				asm volatile ("vbroadcasti32x4 %0,%%zmm17" : : "m" (raid_gfcauchypshufb[d][2][1][0]));
			}
			if (np >= 5) {
				asm volatile ("vbroadcasti32x4 %0,%%zmm18" : : "m" (raid_gfcauchypshufb[d][3][0][0]));
				asm volatile ("vbroadcasti32x4 %0,%%zmm19" : : "m" (raid_gfcauchypshufb[d][3][1][0]));
			}
			if (np >= 6) {
				asm volatile ("vbroadcasti32x4 %0,%%zmm20" : : "m" (raid_gfcauchypshufb[d][4][0][0]));
				asm volatile ("vbroadcasti32x4 %0,%%zmm21" : : "m" (raid_gfcauchypshufb[d][4][1][0]));
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
	asm volatile ("vbroadcasti32x4 %0,%%zmm4" : : "m" (raid_gfmulpshufb[V][0][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm5" : : "m" (raid_gfmulpshufb[V][1][0]));

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

	raid_avx_begin();

	/*
	 * zmm7 = 0x0f nibble mask
	 *
	 * zmm16 = V[0] low table
	 * zmm17 = V[0] high table
	 * zmm18 = V[1] low table
	 * zmm19 = V[1] high table
	 * zmm20 = V[2] low table
	 * zmm21 = V[2] high table
	 * zmm22 = V[3] low table
	 * zmm23 = V[3] high table
	 */

	asm volatile ("vpbroadcastb %0,%%zmm7" : : "m" (gfconst16.low4[0]));

	/* the inverse matrix V[] is constant for the whole recovery. */
	asm volatile ("vbroadcasti32x4 %0,%%zmm16" : : "m" (raid_gfmulpshufb[V[0]][0][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm17" : : "m" (raid_gfmulpshufb[V[0]][1][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm18" : : "m" (raid_gfmulpshufb[V[1]][0][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm19" : : "m" (raid_gfmulpshufb[V[1]][1][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm20" : : "m" (raid_gfmulpshufb[V[2]][0][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm21" : : "m" (raid_gfmulpshufb[V[2]][1][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm22" : : "m" (raid_gfmulpshufb[V[3]][0][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm23" : : "m" (raid_gfmulpshufb[V[3]][1][0]));

	for (i = 0; i < size; i += 64) {
		/* d0 = p[0] ^ pa[0] */
		asm volatile ("vmovdqa64 %0,%%zmm0" : : "m" (p[0][i]));
		asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (pa[0][i]));
		asm volatile ("vpxord %zmm2,%zmm0,%zmm0");

		/* d1 = p[1] ^ pa[1] */
		asm volatile ("vmovdqa64 %0,%%zmm1" : : "m" (p[1][i]));
		asm volatile ("vmovdqa64 %0,%%zmm3" : : "m" (pa[1][i]));
		asm volatile ("vpxord %zmm3,%zmm1,%zmm1");

		/*
		 * Split both deltas into low/high nibbles once.
		 *
		 * zmm4  = d0 low
		 * zmm5  = d0 high
		 * zmm12 = d1 low
		 * zmm13 = d1 high
		 */
		asm volatile ("vpsrlw $4,%zmm0,%zmm5");
		asm volatile ("vpsrlw $4,%zmm1,%zmm13");
		asm volatile ("vpandd %zmm7,%zmm0,%zmm4");
		asm volatile ("vpandd %zmm7,%zmm5,%zmm5");
		asm volatile ("vpandd %zmm7,%zmm1,%zmm12");
		asm volatile ("vpandd %zmm7,%zmm13,%zmm13");

		/*
		 * pa[0] = V[0] * d0 ^ V[1] * d1
		 */
		asm volatile ("vpshufb %zmm4,%zmm16,%zmm2");
		asm volatile ("vpshufb %zmm5,%zmm17,%zmm3");
		asm volatile ("vpshufb %zmm12,%zmm18,%zmm10");
		asm volatile ("vpshufb %zmm13,%zmm19,%zmm11");

		asm volatile ("vpxord %zmm3,%zmm2,%zmm2");
		asm volatile ("vpxord %zmm10,%zmm2,%zmm2");
		asm volatile ("vpxord %zmm11,%zmm2,%zmm2");

		asm volatile ("vmovdqa64 %%zmm2,%0" : "=m" (pa[0][i]));

		/*
		 * pa[1] = V[2] * d0 ^ V[3] * d1
		 *
		 * Reuse the already computed low/high nibbles.
		 */
		asm volatile ("vpshufb %zmm4,%zmm20,%zmm2");
		asm volatile ("vpshufb %zmm5,%zmm21,%zmm3");
		asm volatile ("vpshufb %zmm12,%zmm22,%zmm10");
		asm volatile ("vpshufb %zmm13,%zmm23,%zmm11");

		asm volatile ("vpxord %zmm3,%zmm2,%zmm2");
		asm volatile ("vpxord %zmm10,%zmm2,%zmm2");
		asm volatile ("vpxord %zmm11,%zmm2,%zmm2");

		asm volatile ("vmovdqa64 %%zmm2,%0" : "=m" (pa[1][i]));
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
	const uint8_t *T[RAID_PARITY_MAX * RAID_PARITY_MAX];
	size_t i;
	int j, k;

	/* setup the coefficients matrix */
	for (j = 0; j < N; ++j)
		for (k = 0; k < N; ++k)
			G[j * N + k] = A(ip[j], id[k]);

	/* invert it to solve the system of linear equations */
	raid_invert(G, V, N);

	/* precompute shuffle table pointers */
	for (j = 0; j < N * N; ++j)
		T[j] = &raid_gfmulpshufb[V[j]][0][0];

	/* compute delta parity */
	raid_delta_gen(N, id, ip, nd, size, vv);

	for (j = 0; j < N; ++j) {
		p[j] = v[nd + ip[j]];
		pa[j] = v[id[j]];
	}

	raid_avx_begin();

	asm volatile ("vpbroadcastb %0,%%zmm31" : : "m" (gfconst16.low4[0]));

	for (i = 0; i < size; i += 64) {
		/* delta */
		asm volatile (
			"movq 0(%2), %%rax\n"
			"movq 0(%3), %%rbx\n"
			"vmovdqa64 (%%rax, %1), %%zmm12\n"
			"vmovdqa64 (%%rbx, %1), %%zmm13\n"
			"vpxorq %%zmm13, %%zmm12, %%zmm12\n"
			"vpsrlw $4, %%zmm12, %%zmm1\n"
			"vpandq %%zmm31, %%zmm12, %%zmm0\n"
			"vpandq %%zmm31, %%zmm1, %%zmm1\n"
			"cmpq $1, %0\n"
			"jbe 1f\n"

			"movq 8(%2), %%rax\n"
			"movq 8(%3), %%rbx\n"
			"vmovdqa64 (%%rax, %1), %%zmm12\n"
			"vmovdqa64 (%%rbx, %1), %%zmm13\n"
			"vpxorq %%zmm13, %%zmm12, %%zmm12\n"
			"vpsrlw $4, %%zmm12, %%zmm3\n"
			"vpandq %%zmm31, %%zmm12, %%zmm2\n"
			"vpandq %%zmm31, %%zmm3, %%zmm3\n"
			"cmpq $2, %0\n"
			"jbe 1f\n"

			"movq 16(%2), %%rax\n"
			"movq 16(%3), %%rbx\n"
			"vmovdqa64 (%%rax, %1), %%zmm12\n"
			"vmovdqa64 (%%rbx, %1), %%zmm13\n"
			"vpxorq %%zmm13, %%zmm12, %%zmm12\n"
			"vpsrlw $4, %%zmm12, %%zmm5\n"
			"vpandq %%zmm31, %%zmm12, %%zmm4\n"
			"vpandq %%zmm31, %%zmm5, %%zmm5\n"
			"cmpq $3, %0\n"
			"jbe 1f\n"

			"movq 24(%2), %%rax\n"
			"movq 24(%3), %%rbx\n"
			"vmovdqa64 (%%rax, %1), %%zmm12\n"
			"vmovdqa64 (%%rbx, %1), %%zmm13\n"
			"vpxorq %%zmm13, %%zmm12, %%zmm12\n"
			"vpsrlw $4, %%zmm12, %%zmm7\n"
			"vpandq %%zmm31, %%zmm12, %%zmm6\n"
			"vpandq %%zmm31, %%zmm7, %%zmm7\n"
			"cmpq $4, %0\n"
			"jbe 1f\n"

			"movq 32(%2), %%rax\n"
			"movq 32(%3), %%rbx\n"
			"vmovdqa64 (%%rax, %1), %%zmm12\n"
			"vmovdqa64 (%%rbx, %1), %%zmm13\n"
			"vpxorq %%zmm13, %%zmm12, %%zmm12\n"
			"vpsrlw $4, %%zmm12, %%zmm9\n"
			"vpandq %%zmm31, %%zmm12, %%zmm8\n"
			"vpandq %%zmm31, %%zmm9, %%zmm9\n"
			"cmpq $5, %0\n"
			"jbe 1f\n"

			"movq 40(%2), %%rax\n"
			"movq 40(%3), %%rbx\n"
			"vmovdqa64 (%%rax, %1), %%zmm12\n"
			"vmovdqa64 (%%rbx, %1), %%zmm13\n"
			"vpxorq %%zmm13, %%zmm12, %%zmm12\n"
			"vpsrlw $4, %%zmm12, %%zmm11\n"
			"vpandq %%zmm31, %%zmm12, %%zmm10\n"
			"vpandq %%zmm31, %%zmm11, %%zmm11\n"

			"1:\n"
			:
			: "r" ((uint64_t)N), "r" (i), "r" (p), "r" (pa)
			: "rax", "rbx", "cc", "memory"
		);

		/* reconstruct */
		for (j = 0; j < N; ++j) {
			asm volatile (
				"movq 0(%2), %%rcx\n"
				"vbroadcasti32x4 0(%%rcx), %%zmm14\n"
				"vbroadcasti32x4 16(%%rcx), %%zmm15\n"
				"vpshufb %%zmm0, %%zmm14, %%zmm14\n"
				"vpshufb %%zmm1, %%zmm15, %%zmm15\n"
				"cmpq $1, %0\n"
				"jbe 1f\n"

				"movq 8(%2), %%rcx\n"
				"vbroadcasti32x4 0(%%rcx), %%zmm12\n"
				"vbroadcasti32x4 16(%%rcx), %%zmm13\n"
				"vpshufb %%zmm2, %%zmm12, %%zmm12\n"
				"vpshufb %%zmm3, %%zmm13, %%zmm13\n"
				"vpxorq %%zmm12, %%zmm14, %%zmm14\n"
				"vpxorq %%zmm13, %%zmm15, %%zmm15\n"
				"cmpq $2, %0\n"
				"jbe 1f\n"

				"movq 16(%2), %%rcx\n"
				"vbroadcasti32x4 0(%%rcx), %%zmm12\n"
				"vbroadcasti32x4 16(%%rcx), %%zmm13\n"
				"vpshufb %%zmm4, %%zmm12, %%zmm12\n"
				"vpshufb %%zmm5, %%zmm13, %%zmm13\n"
				"vpxorq %%zmm12, %%zmm14, %%zmm14\n"
				"vpxorq %%zmm13, %%zmm15, %%zmm15\n"
				"cmpq $3, %0\n"
				"jbe 1f\n"

				"movq 24(%2), %%rcx\n"
				"vbroadcasti32x4 0(%%rcx), %%zmm12\n"
				"vbroadcasti32x4 16(%%rcx), %%zmm13\n"
				"vpshufb %%zmm6, %%zmm12, %%zmm12\n"
				"vpshufb %%zmm7, %%zmm13, %%zmm13\n"
				"vpxorq %%zmm12, %%zmm14, %%zmm14\n"
				"vpxorq %%zmm13, %%zmm15, %%zmm15\n"
				"cmpq $4, %0\n"
				"jbe 1f\n"

				"movq 32(%2), %%rcx\n"
				"vbroadcasti32x4 0(%%rcx), %%zmm12\n"
				"vbroadcasti32x4 16(%%rcx), %%zmm13\n"
				"vpshufb %%zmm8, %%zmm12, %%zmm12\n"
				"vpshufb %%zmm9, %%zmm13, %%zmm13\n"
				"vpxorq %%zmm12, %%zmm14, %%zmm14\n"
				"vpxorq %%zmm13, %%zmm15, %%zmm15\n"
				"cmpq $5, %0\n"
				"jbe 1f\n"

				"movq 40(%2), %%rcx\n"
				"vbroadcasti32x4 0(%%rcx), %%zmm12\n"
				"vbroadcasti32x4 16(%%rcx), %%zmm13\n"
				"vpshufb %%zmm10, %%zmm12, %%zmm12\n"
				"vpshufb %%zmm11, %%zmm13, %%zmm13\n"
				"vpxorq %%zmm12, %%zmm14, %%zmm14\n"
				"vpxorq %%zmm13, %%zmm15, %%zmm15\n"

				"1:\n"
				"vpxorq %%zmm15, %%zmm14, %%zmm14\n"
				"movq %3, %%rax\n"
				"vmovdqa64 %%zmm14, (%%rax, %1)\n"
				:
				: "r" ((uint64_t)N), "r" (i), "r" (&T[j * N]), "r" (pa[j])
				: "rax", "rcx", "cc", "memory"
			);
		}
	}

	raid_avx_end();
}

void raid_register_avx512(void)
{
	if (raid_cpu_has_avx512bw()) {
		if (!raid_cpu_has_slow_avx512()) {
			raid_gen_register(RAID_ALGO_CAUCHY_PAR1, "avx512", raid_gen1_avx512bw, RAID_POLY_ANY);
			raid_gen_register(RAID_ALGO_CAUCHY_PAR2, "avx512", raid_gen2_avx512bw, RAID_POLY_ANY);
			raid_gen_register(RAID_ALGO_CAUCHY_PAR3, "avx512", raid_gen3_avx512bw, RAID_POLY_ANY);
			raid_gen_register(RAID_ALGO_CAUCHY_PAR4, "avx512", raid_gen4_avx512bw, RAID_POLY_ANY);
			raid_gen_register(RAID_ALGO_CAUCHY_PAR5, "avx512", raid_gen5_avx512bw, RAID_POLY_ANY);
			raid_gen_register(RAID_ALGO_CAUCHY_PAR6, "avx512", raid_gen6_avx512bw, RAID_POLY_ANY);

			raid_rec_register(RAID_ALGO_CAUCHY_PAR1, "avx512", raid_rec1_avx512bw, RAID_POLY_ANY);
			raid_rec_register(RAID_ALGO_CAUCHY_PAR2, "avx512", raid_rec2_avx512bw, RAID_POLY_ANY);
			raid_rec_register(RAID_ALGO_CAUCHY_PAR3, "avx512", raid_recX_avx512bw, RAID_POLY_ANY);
			raid_rec_register(RAID_ALGO_CAUCHY_PAR4, "avx512", raid_recX_avx512bw, RAID_POLY_ANY);
			raid_rec_register(RAID_ALGO_CAUCHY_PAR5, "avx512", raid_recX_avx512bw, RAID_POLY_ANY);
			raid_rec_register(RAID_ALGO_CAUCHY_PAR6, "avx512", raid_recX_avx512bw, RAID_POLY_ANY);
		}
	}
}
#endif
