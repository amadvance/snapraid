// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Andrea Mazzoleni

#include "internal.h"
#include "gf.h"
#include "cpu.h"

#ifdef CONFIG_X86_64
/*
 * Generate one parity block (RAID5 with XOR) using AVX512BW implementation.
 *
 * Processes two disks per iteration using vpternlogq over 64-byte blocks.
 *
 * Note that in true AVX512F would suffice, but we don't want to add
 * specific support for AVX512F because this would be the only function
 * to benefit from that.
 */
static __always_inline void raid_gen1_avx512bw_gen(int nd, size_t size, void **vv, int streaming)
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

		if (streaming)
			asm volatile ("vmovntdq %%zmm0,%0" : "=m" (p[i]));
		else
			asm volatile ("vmovdqa64 %%zmm0,%0" : "=m" (p[i]));
	}

	raid_avx_end(streaming);
}

void raid_gen1_avx512bw(int nd, size_t size, void **vv, int streaming)
{
	if (streaming)
		raid_gen1_avx512bw_gen(nd, size, vv, 1);
	else
		raid_gen1_avx512bw_gen(nd, size, vv, 0);
}

/*
 * Generate two parity blocks (RAID6 with Cauchy matrix) using AVX512BW implementation.
 *
 * Preloads 13 pairs of Q coefficient tables in ZMM registers (zmm4..zmm29)
 * with an unrolled disk loop and early exits.
 */
static __always_inline void raid_gen2_avx512bw_gen(int nd, size_t size, void **vv, int streaming)
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

void raid_gen2_avx512bw(int nd, size_t size, void **vv, int streaming)
{
	if (streaming)
		raid_gen2_avx512bw_gen(nd, size, vv, 1);
	else
		raid_gen2_avx512bw_gen(nd, size, vv, 0);
}

/*
 * Generate three parity blocks with Cauchy matrix using AVX512BW implementation.
 *
 * Preloads 6 sets of Q and R coefficient tables in ZMM registers (zmm7..zmm30)
 * with an unrolled disk loop and early exits.
 */
static __always_inline void raid_gen3_avx512bw_gen(int nd, size_t size, void **vv, int streaming)
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

	asm volatile ("vpbroadcastb %0,%%zmm31" : : "m" (gfconst16.low4[0]));

	/* preload as many Q and R coefficient tables as possible */
	asm volatile ("vbroadcasti32x4 %0,%%zmm7" : : "m" (raid_gfcauchypshufb[1][0][0][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm8" : : "m" (raid_gfcauchypshufb[1][0][1][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm9" : : "m" (raid_gfcauchypshufb[1][1][0][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm10" : : "m" (raid_gfcauchypshufb[1][1][1][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm11" : : "m" (raid_gfcauchypshufb[2][0][0][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm12" : : "m" (raid_gfcauchypshufb[2][0][1][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm13" : : "m" (raid_gfcauchypshufb[2][1][0][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm14" : : "m" (raid_gfcauchypshufb[2][1][1][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm15" : : "m" (raid_gfcauchypshufb[3][0][0][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm16" : : "m" (raid_gfcauchypshufb[3][0][1][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm17" : : "m" (raid_gfcauchypshufb[3][1][0][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm18" : : "m" (raid_gfcauchypshufb[3][1][1][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm19" : : "m" (raid_gfcauchypshufb[4][0][0][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm20" : : "m" (raid_gfcauchypshufb[4][0][1][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm21" : : "m" (raid_gfcauchypshufb[4][1][0][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm22" : : "m" (raid_gfcauchypshufb[4][1][1][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm23" : : "m" (raid_gfcauchypshufb[5][0][0][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm24" : : "m" (raid_gfcauchypshufb[5][0][1][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm25" : : "m" (raid_gfcauchypshufb[5][1][0][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm26" : : "m" (raid_gfcauchypshufb[5][1][1][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm27" : : "m" (raid_gfcauchypshufb[6][0][0][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm28" : : "m" (raid_gfcauchypshufb[6][0][1][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm29" : : "m" (raid_gfcauchypshufb[6][1][0][0]));
	asm volatile ("vbroadcasti32x4 %0,%%zmm30" : : "m" (raid_gfcauchypshufb[6][1][1][0]));

	for (i = 0; i < size; i += 64) {
		asm volatile ("vmovdqa64 %0,%%zmm0" : : "m" (v[0][i]));
		asm volatile ("vmovdqa64 %zmm0,%zmm1");
		asm volatile ("vmovdqa64 %zmm0,%zmm2");

		asm volatile ("vmovdqa64 %0,%%zmm3" : : "m" (v[1][i]));
		asm volatile ("vpxorq %zmm3,%zmm0,%zmm0");
		asm volatile ("vpsrlw $4,%zmm3,%zmm5");
		asm volatile ("vpandq %zmm31,%zmm3,%zmm4");
		asm volatile ("vpandq %zmm31,%zmm5,%zmm5");
		asm volatile ("vpshufb %zmm4,%zmm7,%zmm3");
		asm volatile ("vpshufb %zmm5,%zmm8,%zmm6");
		asm volatile ("vpternlogq $0x96,%zmm3,%zmm6,%zmm1");
		asm volatile ("vpshufb %zmm4,%zmm9,%zmm3");
		asm volatile ("vpshufb %zmm5,%zmm10,%zmm6");
		asm volatile ("vpternlogq $0x96,%zmm3,%zmm6,%zmm2");
		if (nd == 2)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm3" : : "m" (v[2][i]));
		asm volatile ("vpxorq %zmm3,%zmm0,%zmm0");
		asm volatile ("vpsrlw $4,%zmm3,%zmm5");
		asm volatile ("vpandq %zmm31,%zmm3,%zmm4");
		asm volatile ("vpandq %zmm31,%zmm5,%zmm5");
		asm volatile ("vpshufb %zmm4,%zmm11,%zmm3");
		asm volatile ("vpshufb %zmm5,%zmm12,%zmm6");
		asm volatile ("vpternlogq $0x96,%zmm3,%zmm6,%zmm1");
		asm volatile ("vpshufb %zmm4,%zmm13,%zmm3");
		asm volatile ("vpshufb %zmm5,%zmm14,%zmm6");
		asm volatile ("vpternlogq $0x96,%zmm3,%zmm6,%zmm2");
		if (nd == 3)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm3" : : "m" (v[3][i]));
		asm volatile ("vpxorq %zmm3,%zmm0,%zmm0");
		asm volatile ("vpsrlw $4,%zmm3,%zmm5");
		asm volatile ("vpandq %zmm31,%zmm3,%zmm4");
		asm volatile ("vpandq %zmm31,%zmm5,%zmm5");
		asm volatile ("vpshufb %zmm4,%zmm15,%zmm3");
		asm volatile ("vpshufb %zmm5,%zmm16,%zmm6");
		asm volatile ("vpternlogq $0x96,%zmm3,%zmm6,%zmm1");
		asm volatile ("vpshufb %zmm4,%zmm17,%zmm3");
		asm volatile ("vpshufb %zmm5,%zmm18,%zmm6");
		asm volatile ("vpternlogq $0x96,%zmm3,%zmm6,%zmm2");
		if (nd == 4)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm3" : : "m" (v[4][i]));
		asm volatile ("vpxorq %zmm3,%zmm0,%zmm0");
		asm volatile ("vpsrlw $4,%zmm3,%zmm5");
		asm volatile ("vpandq %zmm31,%zmm3,%zmm4");
		asm volatile ("vpandq %zmm31,%zmm5,%zmm5");
		asm volatile ("vpshufb %zmm4,%zmm19,%zmm3");
		asm volatile ("vpshufb %zmm5,%zmm20,%zmm6");
		asm volatile ("vpternlogq $0x96,%zmm3,%zmm6,%zmm1");
		asm volatile ("vpshufb %zmm4,%zmm21,%zmm3");
		asm volatile ("vpshufb %zmm5,%zmm22,%zmm6");
		asm volatile ("vpternlogq $0x96,%zmm3,%zmm6,%zmm2");
		if (nd == 5)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm3" : : "m" (v[5][i]));
		asm volatile ("vpxorq %zmm3,%zmm0,%zmm0");
		asm volatile ("vpsrlw $4,%zmm3,%zmm5");
		asm volatile ("vpandq %zmm31,%zmm3,%zmm4");
		asm volatile ("vpandq %zmm31,%zmm5,%zmm5");
		asm volatile ("vpshufb %zmm4,%zmm23,%zmm3");
		asm volatile ("vpshufb %zmm5,%zmm24,%zmm6");
		asm volatile ("vpternlogq $0x96,%zmm3,%zmm6,%zmm1");
		asm volatile ("vpshufb %zmm4,%zmm25,%zmm3");
		asm volatile ("vpshufb %zmm5,%zmm26,%zmm6");
		asm volatile ("vpternlogq $0x96,%zmm3,%zmm6,%zmm2");
		if (nd == 6)
			goto store;

		asm volatile ("vmovdqa64 %0,%%zmm3" : : "m" (v[6][i]));
		asm volatile ("vpxorq %zmm3,%zmm0,%zmm0");
		asm volatile ("vpsrlw $4,%zmm3,%zmm5");
		asm volatile ("vpandq %zmm31,%zmm3,%zmm4");
		asm volatile ("vpandq %zmm31,%zmm5,%zmm5");
		asm volatile ("vpshufb %zmm4,%zmm27,%zmm3");
		asm volatile ("vpshufb %zmm5,%zmm28,%zmm6");
		asm volatile ("vpternlogq $0x96,%zmm3,%zmm6,%zmm1");
		asm volatile ("vpshufb %zmm4,%zmm29,%zmm3");
		asm volatile ("vpshufb %zmm5,%zmm30,%zmm6");
		asm volatile ("vpternlogq $0x96,%zmm3,%zmm6,%zmm2");
		if (nd == 7)
			goto store;

		/*
		 * No more registers are available for resident coefficient
		 * tables. Process D7 and later with the normal loop.
		 */
		for (d = 7; d < nd; ++d) {
			asm volatile ("vmovdqa64 %0,%%zmm3" : : "m" (v[d][i]));
			asm volatile ("vpxorq %zmm3,%zmm0,%zmm0");
			asm volatile ("vpsrlw $4,%zmm3,%zmm5");
			asm volatile ("vpandq %zmm31,%zmm3,%zmm4");
			asm volatile ("vpandq %zmm31,%zmm5,%zmm5");

			asm volatile ("vbroadcasti32x4 %0,%%zmm6" : : "m" (raid_gfcauchypshufb[d][0][0][0]));
			asm volatile ("vpshufb %zmm4,%zmm6,%zmm6");
			asm volatile ("vbroadcasti32x4 %0,%%zmm3" : : "m" (raid_gfcauchypshufb[d][0][1][0]));
			asm volatile ("vpshufb %zmm5,%zmm3,%zmm3");
			asm volatile ("vpternlogq $0x96,%zmm3,%zmm6,%zmm1");

			asm volatile ("vbroadcasti32x4 %0,%%zmm6" : : "m" (raid_gfcauchypshufb[d][1][0][0]));
			asm volatile ("vpshufb %zmm4,%zmm6,%zmm6");
			asm volatile ("vbroadcasti32x4 %0,%%zmm3" : : "m" (raid_gfcauchypshufb[d][1][1][0]));
			asm volatile ("vpshufb %zmm5,%zmm3,%zmm3");
			asm volatile ("vpternlogq $0x96,%zmm3,%zmm6,%zmm2");
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

void raid_gen3_avx512bw(int nd, size_t size, void **vv, int streaming)
{
	if (streaming)
		raid_gen3_avx512bw_gen(nd, size, vv, 1);
	else
		raid_gen3_avx512bw_gen(nd, size, vv, 0);
}

/*
 * Generate N parity blocks with Cauchy matrix using AVX512BW implementation.
 */
static __always_inline void raid_genX_avx512bw(int nd, size_t size, void **vv, int np, int streaming)
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

		if (streaming) {
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
 * Recover multiple data failures using selected parity blocks with AVX512BW.
 *
 * Compute only the selected syndromes, keeping them in registers.
 * This avoids raid_delta_gen(), temporary syndrome buffers, recomputation of
 * parity blocks, and generation of unused parity rows.
 *
 * AVX512 provides enough registers to keep all six supported syndromes
 * in registers in a single surviving-data scan.
 */
static __always_inline void raid_recX_avx512bw(int nr, int has_p, int *id, int *ip, int nd, size_t size, void **vv)
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

	/*
	 * Build the compact list of surviving data blocks and precompute
	 * the multiplication-table pointers for each selected syndrome.
	 */
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
	 * If P is available, the last missing block is reconstructed
	 * directly from Pdelta, so the last inverse row isn't needed.
	 */
	for (j = 0; j < nr - has_p; ++j)
		for (k = 0; k < nr; ++k)
			R[j][k] = &raid_gfmulpshufb[V[j * nr + k]][0][0];

	raid_avx_begin();

	/*
	 * Register allocation during syndrome generation:
	 *
	 *   zmm0   syndrome 0
	 *   zmm2   syndrome 1
	 *   zmm4   syndrome 2
	 *   zmm6   syndrome 3
	 *   zmm8   syndrome 4
	 *   zmm10  syndrome 5
	 *
	 *   zmm12  source / source low nibble
	 *   zmm13  source high nibble
	 *   zmm14  low multiplication table / result
	 *   zmm15  high multiplication table / result
	 *   zmm30  remaining P delta, if has_p
	 *   zmm31  low-nibble mask
	 *
	 * After syndrome splitting:
	 *
	 *   zmm0 /zmm1   syndrome 0 low/high
	 *   zmm2 /zmm3   syndrome 1 low/high
	 *   zmm4 /zmm5   syndrome 2 low/high
	 *   zmm6 /zmm7   syndrome 3 low/high
	 *   zmm8 /zmm9   syndrome 4 low/high
	 *   zmm10/zmm11  syndrome 5 low/high
	 */

	asm volatile ("vpbroadcastb %0,%%zmm31" : : "m" (gfconst16.low4[0]));

	for (i = 0; i < size; i += 64) {
		/* start every selected syndrome from its stored parity block */
		asm volatile ("vmovdqa64 %0,%%zmm0" : : "m" (p[0][i]));

		if (nr >= 2)
			asm volatile ("vmovdqa64 %0,%%zmm2" : : "m" (p[1][i]));

		if (nr >= 3)
			asm volatile ("vmovdqa64 %0,%%zmm4" : : "m" (p[2][i]));

		if (nr >= 4)
			asm volatile ("vmovdqa64 %0,%%zmm6" : : "m" (p[3][i]));

		if (nr >= 5)
			asm volatile ("vmovdqa64 %0,%%zmm8" : : "m" (p[4][i]));

		if (nr >= 6)
			asm volatile ("vmovdqa64 %0,%%zmm10" : : "m" (p[5][i]));

		/* add all surviving data contributions in one source scan */
		for (s = 0; s < ns; ++s) {
			const uint8_t **t = S[s];

			asm volatile ("vmovdqa64 %0,%%zmm12" : : "m" (src[s][i]));

			/*
			 * P has coefficient 1 for every data disk.
			 * XOR the original source before splitting it.
			 */
			if (has_p) {
				asm volatile ("vpxorq %zmm12,%zmm0,%zmm0");

				asm volatile ("vpsrlw $4,%zmm12,%zmm13");
				asm volatile ("vpandq %zmm31,%zmm12,%zmm12");
				asm volatile ("vpandq %zmm31,%zmm13,%zmm13");
			} else {
				asm volatile ("vpsrlw $4,%zmm12,%zmm13");
				asm volatile ("vpandq %zmm31,%zmm12,%zmm12");
				asm volatile ("vpandq %zmm31,%zmm13,%zmm13");

				/* syndrome 0 */
				asm volatile ("vbroadcasti32x4 %0,%%zmm14" : : "m" (t[0][0]));
				asm volatile ("vbroadcasti32x4 %0,%%zmm15" : : "m" (t[0][16]));
				asm volatile ("vpshufb %zmm12,%zmm14,%zmm14");
				asm volatile ("vpshufb %zmm13,%zmm15,%zmm15");
				asm volatile ("vpternlogq $0x96,%zmm14,%zmm15,%zmm0");
			}

			/* syndrome 1 */
			if (nr >= 2) {
				asm volatile ("vbroadcasti32x4 %0,%%zmm14" : : "m" (t[1][0]));
				asm volatile ("vbroadcasti32x4 %0,%%zmm15" : : "m" (t[1][16]));
				asm volatile ("vpshufb %zmm12,%zmm14,%zmm14");
				asm volatile ("vpshufb %zmm13,%zmm15,%zmm15");
				asm volatile ("vpternlogq $0x96,%zmm14,%zmm15,%zmm2");
			}

			/* syndrome 2 */
			if (nr >= 3) {
				asm volatile ("vbroadcasti32x4 %0,%%zmm14" : : "m" (t[2][0]));
				asm volatile ("vbroadcasti32x4 %0,%%zmm15" : : "m" (t[2][16]));
				asm volatile ("vpshufb %zmm12,%zmm14,%zmm14");
				asm volatile ("vpshufb %zmm13,%zmm15,%zmm15");
				asm volatile ("vpternlogq $0x96,%zmm14,%zmm15,%zmm4");
			}

			/* syndrome 3 */
			if (nr >= 4) {
				asm volatile ("vbroadcasti32x4 %0,%%zmm14" : : "m" (t[3][0]));
				asm volatile ("vbroadcasti32x4 %0,%%zmm15" : : "m" (t[3][16]));
				asm volatile ("vpshufb %zmm12,%zmm14,%zmm14");
				asm volatile ("vpshufb %zmm13,%zmm15,%zmm15");
				asm volatile ("vpternlogq $0x96,%zmm14,%zmm15,%zmm6");
			}

			/* syndrome 4 */
			if (nr >= 5) {
				asm volatile ("vbroadcasti32x4 %0,%%zmm14" : : "m" (t[4][0]));
				asm volatile ("vbroadcasti32x4 %0,%%zmm15" : : "m" (t[4][16]));
				asm volatile ("vpshufb %zmm12,%zmm14,%zmm14");
				asm volatile ("vpshufb %zmm13,%zmm15,%zmm15");
				asm volatile ("vpternlogq $0x96,%zmm14,%zmm15,%zmm8");
			}

			/* syndrome 5 */
			if (nr >= 6) {
				asm volatile ("vbroadcasti32x4 %0,%%zmm14" : : "m" (t[5][0]));
				asm volatile ("vbroadcasti32x4 %0,%%zmm15" : : "m" (t[5][16]));
				asm volatile ("vpshufb %zmm12,%zmm14,%zmm14");
				asm volatile ("vpshufb %zmm13,%zmm15,%zmm15");
				asm volatile ("vpternlogq $0x96,%zmm14,%zmm15,%zmm10");
			}
		}

		/*
		 * Preserve the complete P delta before syndrome 0 is
		 * destructively split into low/high nibbles.
		 */
		if (has_p)
			asm volatile ("vmovdqa64 %zmm0,%zmm30");

		/* split every completed syndrome once into low/high nibbles */
		asm volatile ("vpsrlw $4,%zmm0,%zmm1");
		asm volatile ("vpandq %zmm31,%zmm0,%zmm0");
		asm volatile ("vpandq %zmm31,%zmm1,%zmm1");

		if (nr >= 2) {
			asm volatile ("vpsrlw $4,%zmm2,%zmm3");
			asm volatile ("vpandq %zmm31,%zmm2,%zmm2");
			asm volatile ("vpandq %zmm31,%zmm3,%zmm3");
		}

		if (nr >= 3) {
			asm volatile ("vpsrlw $4,%zmm4,%zmm5");
			asm volatile ("vpandq %zmm31,%zmm4,%zmm4");
			asm volatile ("vpandq %zmm31,%zmm5,%zmm5");
		}

		if (nr >= 4) {
			asm volatile ("vpsrlw $4,%zmm6,%zmm7");
			asm volatile ("vpandq %zmm31,%zmm6,%zmm6");
			asm volatile ("vpandq %zmm31,%zmm7,%zmm7");
		}

		if (nr >= 5) {
			asm volatile ("vpsrlw $4,%zmm8,%zmm9");
			asm volatile ("vpandq %zmm31,%zmm8,%zmm8");
			asm volatile ("vpandq %zmm31,%zmm9,%zmm9");
		}

		if (nr >= 6) {
			asm volatile ("vpsrlw $4,%zmm10,%zmm11");
			asm volatile ("vpandq %zmm31,%zmm10,%zmm10");
			asm volatile ("vpandq %zmm31,%zmm11,%zmm11");
		}

		/*
		 * Reconstruct the missing data blocks.
		 *
		 * If P is available, reconstruct only nr - 1 blocks through
		 * the inverse matrix. XOR each result out of zmm30, leaving
		 * the final missing block in zmm30.
		 *
		 * Keep independent low/high dependency chains:
		 *
		 *   zmm12 low accumulator
		 *   zmm13 high accumulator
		 *   zmm14 low multiplication table/result
		 *   zmm15 high multiplication table/result
		 *   zmm30 remaining P delta, if has_p
		 *
		 * This is intentional. Do not serialize low/high products
		 * through a single temporary register.
		 */
		for (j = 0; j < nr - has_p; ++j) {
			const uint8_t **t = R[j];

			/* coefficient 0 initializes both accumulators */
			asm volatile ("vbroadcasti32x4 %0,%%zmm12" : : "m" (t[0][0]));
			asm volatile ("vbroadcasti32x4 %0,%%zmm13" : : "m" (t[0][16]));
			asm volatile ("vpshufb %zmm0,%zmm12,%zmm12");
			asm volatile ("vpshufb %zmm1,%zmm13,%zmm13");

			if (nr >= 2) {
				asm volatile ("vbroadcasti32x4 %0,%%zmm14" : : "m" (t[1][0]));
				asm volatile ("vbroadcasti32x4 %0,%%zmm15" : : "m" (t[1][16]));
				asm volatile ("vpshufb %zmm2,%zmm14,%zmm14");
				asm volatile ("vpshufb %zmm3,%zmm15,%zmm15");
				asm volatile ("vpxorq %zmm14,%zmm12,%zmm12");
				asm volatile ("vpxorq %zmm15,%zmm13,%zmm13");
			}

			if (nr >= 3) {
				asm volatile ("vbroadcasti32x4 %0,%%zmm14" : : "m" (t[2][0]));
				asm volatile ("vbroadcasti32x4 %0,%%zmm15" : : "m" (t[2][16]));
				asm volatile ("vpshufb %zmm4,%zmm14,%zmm14");
				asm volatile ("vpshufb %zmm5,%zmm15,%zmm15");
				asm volatile ("vpxorq %zmm14,%zmm12,%zmm12");
				asm volatile ("vpxorq %zmm15,%zmm13,%zmm13");
			}

			if (nr >= 4) {
				asm volatile ("vbroadcasti32x4 %0,%%zmm14" : : "m" (t[3][0]));
				asm volatile ("vbroadcasti32x4 %0,%%zmm15" : : "m" (t[3][16]));
				asm volatile ("vpshufb %zmm6,%zmm14,%zmm14");
				asm volatile ("vpshufb %zmm7,%zmm15,%zmm15");
				asm volatile ("vpxorq %zmm14,%zmm12,%zmm12");
				asm volatile ("vpxorq %zmm15,%zmm13,%zmm13");
			}

			if (nr >= 5) {
				asm volatile ("vbroadcasti32x4 %0,%%zmm14" : : "m" (t[4][0]));
				asm volatile ("vbroadcasti32x4 %0,%%zmm15" : : "m" (t[4][16]));
				asm volatile ("vpshufb %zmm8,%zmm14,%zmm14");
				asm volatile ("vpshufb %zmm9,%zmm15,%zmm15");
				asm volatile ("vpxorq %zmm14,%zmm12,%zmm12");
				asm volatile ("vpxorq %zmm15,%zmm13,%zmm13");
			}

			if (nr >= 6) {
				asm volatile ("vbroadcasti32x4 %0,%%zmm14" : : "m" (t[5][0]));
				asm volatile ("vbroadcasti32x4 %0,%%zmm15" : : "m" (t[5][16]));
				asm volatile ("vpshufb %zmm10,%zmm14,%zmm14");
				asm volatile ("vpshufb %zmm11,%zmm15,%zmm15");
				asm volatile ("vpxorq %zmm14,%zmm12,%zmm12");
				asm volatile ("vpxorq %zmm15,%zmm13,%zmm13");
			}

			/* combine low/high products into the reconstructed block */
			asm volatile ("vpxorq %zmm13,%zmm12,%zmm12");

			/*
			 * Remove the reconstructed block from Pdelta.
			 * After nr - 1 iterations zmm30 contains the final
			 * missing data block.
			 */
			if (has_p)
				asm volatile ("vpxorq %zmm12,%zmm30,%zmm30");

			asm volatile ("vmovdqa64 %%zmm12,%0" : "=m" (pa[j][i]));
		}

		if (has_p)
			asm volatile ("vmovdqa64 %%zmm30,%0" : "=m" (pa[nr - 1][i]));
	}

	raid_avx_end(0);
}

void raid_gen4_avx512bw(int nd, size_t size, void **vv, int streaming)
{
	if (streaming)
		raid_genX_avx512bw(nd, size, vv, 4, 1);
	else
		raid_genX_avx512bw(nd, size, vv, 4, 0);
}

void raid_gen5_avx512bw(int nd, size_t size, void **vv, int streaming)
{
	if (streaming)
		raid_genX_avx512bw(nd, size, vv, 5, 1);
	else
		raid_genX_avx512bw(nd, size, vv, 5, 0);
}

void raid_gen6_avx512bw(int nd, size_t size, void **vv, int streaming)
{
	if (streaming)
		raid_genX_avx512bw(nd, size, vv, 6, 1);
	else
		raid_genX_avx512bw(nd, size, vv, 6, 0);
}

void raid_rec1_avx512bw(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 1);

	/* if recovering with P, use a custom XOR-only path with temporal stores */
	if (ip[0] == 0) {
		raid_rec1of1(id, nd, size, vv);
		return;
	}

	raid_recX_avx512bw(1, 0, id, ip, nd, size, vv);
}

void raid_rec2_avx512bw(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 2);
	if (ip[0] == 0)
		raid_recX_avx512bw(2, 1, id, ip, nd, size, vv);
	else
		raid_recX_avx512bw(2, 0, id, ip, nd, size, vv);
}

void raid_rec3_avx512bw(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 3);
	if (ip[0] == 0)
		raid_recX_avx512bw(3, 1, id, ip, nd, size, vv);
	else
		raid_recX_avx512bw(3, 0, id, ip, nd, size, vv);
}

void raid_rec4_avx512bw(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 4);
	if (ip[0] == 0)
		raid_recX_avx512bw(4, 1, id, ip, nd, size, vv);
	else
		raid_recX_avx512bw(4, 0, id, ip, nd, size, vv);
}

void raid_rec5_avx512bw(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 5);
	if (ip[0] == 0)
		raid_recX_avx512bw(5, 1, id, ip, nd, size, vv);
	else
		raid_recX_avx512bw(5, 0, id, ip, nd, size, vv);
}

void raid_rec6_avx512bw(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 6);
	if (ip[0] == 0)
		raid_recX_avx512bw(6, 1, id, ip, nd, size, vv);
	else
		raid_recX_avx512bw(6, 0, id, ip, nd, size, vv);
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
			raid_rec_register(RAID_ALGO_CAUCHY_PAR3, "avx512", raid_rec3_avx512bw, RAID_POLY_ANY);
			raid_rec_register(RAID_ALGO_CAUCHY_PAR4, "avx512", raid_rec4_avx512bw, RAID_POLY_ANY);
			raid_rec_register(RAID_ALGO_CAUCHY_PAR5, "avx512", raid_rec5_avx512bw, RAID_POLY_ANY);
			raid_rec_register(RAID_ALGO_CAUCHY_PAR6, "avx512", raid_rec6_avx512bw, RAID_POLY_ANY);
		}
	}
}

#endif
