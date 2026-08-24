// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Andrea Mazzoleni

#include "internal.h"
#include "gf.h"
#include "cpu.h"

#ifdef CONFIG_NEON32
/*
 * GEN1 (RAID5 with xor) AArch32 NEON implementation
 */
void raid_gen1_neon32(int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	int d, l;
	size_t i;

	l = nd - 1;
	p = v[nd];

	raid_neon32_begin();

	for (i = 0; i < size; i += 64) {
		asm volatile (
			"vld1.8 {q0}, %0\n"
			"vld1.8 {q1}, %1\n"
			"vld1.8 {q2}, %2\n"
			"vld1.8 {q3}, %3\n"
			:
			: "Q" (v[0][i]), "Q" (v[0][i + 16]),
			"Q" (v[0][i + 32]), "Q" (v[0][i + 48])
		);

		for (d = 1; d <= l; ++d) {
			asm volatile (
				"vld1.8 {q4}, %0\n"
				"vld1.8 {q5}, %1\n"
				"vld1.8 {q6}, %2\n"
				"vld1.8 {q7}, %3\n"
				"veor q0, q0, q4\n"
				"veor q1, q1, q5\n"
				"veor q2, q2, q6\n"
				"veor q3, q3, q7\n"
				:
				: "Q" (v[d][i]), "Q" (v[d][i + 16]),
				"Q" (v[d][i + 32]), "Q" (v[d][i + 48])
			);
		}

		asm volatile (
			"vst1.8 {q0}, %0\n"
			"vst1.8 {q1}, %1\n"
			"vst1.8 {q2}, %2\n"
			"vst1.8 {q3}, %3\n"
			: "=Q" (p[i]), "=Q" (p[i + 16]),
			"=Q" (p[i + 32]), "=Q" (p[i + 48])
		);
	}

	raid_neon32_end();
}

/*
 * GEN2 Cauchy AArch32 NEON implementation using the active generator
 */
static __always_inline void raid_gen2_neon32_gen(int nd, size_t size,
	void **vv, int generator)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *q;
	int d, l;
	size_t i;

	l = nd - 1;
	p = v[nd];
	q = v[nd + 1];

	raid_neon32_begin();

	/* q14 contains the active reduction polynomial */
	asm volatile (
		"vld1.8 {q14}, %0\n"
		:
		: "Q" (gfconst16.poly[0])
	);

	for (i = 0; i < size; i += 32) {
		asm volatile (
			"vld1.8 {q0}, %0\n"
			"vld1.8 {q1}, %1\n"
			"vmov q2, q0\n"
			"vmov q3, q1\n"
			:
			: "Q" (v[l][i]), "Q" (v[l][i + 16])
		);

		for (d = l - 1; d >= 0; --d) {
			if (generator == 3) {
				asm volatile (
					"vmov q10, q2\n"
					"vmov q11, q3\n"
				);
			}
			asm volatile (
				"vshr.s8 q8, q2, #7\n"
				"vshl.i8 q2, q2, #1\n"
				"vand q8, q8, q14\n"
				"veor q2, q2, q8\n"
				"vshr.s8 q8, q3, #7\n"
				"vshl.i8 q3, q3, #1\n"
				"vand q8, q8, q14\n"
				"veor q3, q3, q8\n"
				"vld1.8 {q12}, %0\n"
				"vld1.8 {q13}, %1\n"
				"veor q0, q0, q12\n"
				"veor q1, q1, q13\n"
				"veor q2, q2, q12\n"
				"veor q3, q3, q13\n"
				:
				: "Q" (v[d][i]), "Q" (v[d][i + 16])
			);
			if (generator == 3) {
				asm volatile (
					"veor q2, q2, q10\n"
					"veor q3, q3, q11\n"
				);
			}
		}

		asm volatile (
			"vst1.8 {q0}, %0\n"
			"vst1.8 {q1}, %1\n"
			"vst1.8 {q2}, %2\n"
			"vst1.8 {q3}, %3\n"
			: "=Q" (p[i]), "=Q" (p[i + 16]),
			"=Q" (q[i]), "=Q" (q[i + 16])
		);
	}

	raid_neon32_end();
}

/*
 * GENz (triple parity with powers of 2^-1) AArch32 NEON implementation
 */
void raid_genz_neon32_raid(int nd, size_t size, void **vv)
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

	raid_neon32_begin();

	asm volatile (
		"vld1.8 {q14}, %0\n"
		"vld1.8 {q15}, %1\n"
		:
		: "Q" (gfconst16.poly[0]), "Q" (gfconst16.half[0])
	);

	for (i = 0; i < size; i += 32) {
		asm volatile (
			"vld1.8 {q0}, %0\n"
			"vld1.8 {q1}, %1\n"
			"vmov q2, q0\n"
			"vmov q3, q1\n"
			"vmov q4, q0\n"
			"vmov q5, q1\n"
			:
			: "Q" (v[l][i]), "Q" (v[l][i + 16])
		);

		for (d = l - 1; d >= 0; --d) {
			asm volatile (
				"vshr.s8 q8, q2, #7\n"
				"vshl.i8 q2, q2, #1\n"
				"vand q8, q8, q14\n"
				"veor q2, q2, q8\n"
				"vshr.s8 q8, q3, #7\n"
				"vshl.i8 q3, q3, #1\n"
				"vand q8, q8, q14\n"
				"veor q3, q3, q8\n"
				"vshl.i8 q9, q4, #7\n"
				"vshr.s8 q9, q9, #7\n"
				"vshr.u8 q4, q4, #1\n"
				"vand q9, q9, q15\n"
				"veor q4, q4, q9\n"
				"vshl.i8 q9, q5, #7\n"
				"vshr.s8 q9, q9, #7\n"
				"vshr.u8 q5, q5, #1\n"
				"vand q9, q9, q15\n"
				"veor q5, q5, q9\n"
				"vld1.8 {q12}, %0\n"
				"vld1.8 {q13}, %1\n"
				"veor q0, q0, q12\n"
				"veor q1, q1, q13\n"
				"veor q2, q2, q12\n"
				"veor q3, q3, q13\n"
				"veor q4, q4, q12\n"
				"veor q5, q5, q13\n"
				:
				: "Q" (v[d][i]), "Q" (v[d][i + 16])
			);
		}

		asm volatile (
			"vst1.8 {q0}, %0\n"
			"vst1.8 {q1}, %1\n"
			"vst1.8 {q2}, %2\n"
			"vst1.8 {q3}, %3\n"
			"vst1.8 {q4}, %4\n"
			"vst1.8 {q5}, %5\n"
			: "=Q" (p[i]), "=Q" (p[i + 16]),
			"=Q" (q[i]), "=Q" (q[i + 16]),
			"=Q" (r[i]), "=Q" (r[i + 16])
		);
	}

	raid_neon32_end();
}

/*
 * GENX AArch32 NEON implementation
 */
static __always_inline void raid_genX_neon32(int nd, size_t size,
	void **vv, int np, int generator)
{
	uint8_t **v = (uint8_t **)vv;
	size_t i;
	int d, l;

	l = nd - 1;

	/* special case with only one data disk */
	if (l == 0) {
		for (d = 0; d < np; ++d)
			memcpy(v[1 + d], v[0], size);
		return;
	}

	raid_neon32_begin();

	/* generic case with at least two data disks */
	asm volatile (
		"vld1.8 {q14}, %0\n"
		"vld1.8 {q15}, %1\n"
		:
		: "Q" (gfconst16.poly[0]), "Q" (gfconst16.low4[0])
	);

	for (i = 0; i < size; i += 16) {
		/* last disk without the generator multiplication */
		asm volatile (
			"vld1.8 {q0}, %0\n"
			"vld1.8 {q1}, %0\n"
			:
			: "Q" (v[l][i])
		);

		if (np >= 3) {
			asm volatile (
				"vshr.u8 q8, q0, #4\n"
				"vand q7, q0, q15\n"
				"vand q8, q8, q15\n"
			);

			asm volatile (
				"vld1.8 {q9}, %0\n"
				"vld1.8 {q10}, %1\n"

				"vtbl.8 d22, {d18-d19}, d14\n"
				"vtbl.8 d23, {d18-d19}, d15\n"
				"vtbl.8 d24, {d20-d21}, d16\n"
				"vtbl.8 d25, {d20-d21}, d17\n"

				"veor q2, q11, q12\n"
				:
				: "Q" (raid_gfcauchypshufb[l][1][0][0]),
				"Q" (raid_gfcauchypshufb[l][1][1][0])
			);
		}

		if (np >= 4) {
			asm volatile (
				"vld1.8 {q9}, %0\n"
				"vld1.8 {q10}, %1\n"

				"vtbl.8 d22, {d18-d19}, d14\n"
				"vtbl.8 d23, {d18-d19}, d15\n"
				"vtbl.8 d24, {d20-d21}, d16\n"
				"vtbl.8 d25, {d20-d21}, d17\n"

				"veor q3, q11, q12\n"
				:
				: "Q" (raid_gfcauchypshufb[l][2][0][0]),
				"Q" (raid_gfcauchypshufb[l][2][1][0])
			);
		}

		if (np >= 5) {
			asm volatile (
				"vld1.8 {q9}, %0\n"
				"vld1.8 {q10}, %1\n"

				"vtbl.8 d22, {d18-d19}, d14\n"
				"vtbl.8 d23, {d18-d19}, d15\n"
				"vtbl.8 d24, {d20-d21}, d16\n"
				"vtbl.8 d25, {d20-d21}, d17\n"

				"veor q4, q11, q12\n"
				:
				: "Q" (raid_gfcauchypshufb[l][3][0][0]),
				"Q" (raid_gfcauchypshufb[l][3][1][0])
			);
		}

		if (np >= 6) {
			asm volatile (
				"vld1.8 {q9}, %0\n"
				"vld1.8 {q10}, %1\n"

				"vtbl.8 d22, {d18-d19}, d14\n"
				"vtbl.8 d23, {d18-d19}, d15\n"
				"vtbl.8 d24, {d20-d21}, d16\n"
				"vtbl.8 d25, {d20-d21}, d17\n"

				"veor q5, q11, q12\n"
				:
				: "Q" (raid_gfcauchypshufb[l][4][0][0]),
				"Q" (raid_gfcauchypshufb[l][4][1][0])
			);
		}

		/* intermediate disks */
		for (d = l - 1; d > 0; --d) {
			if (generator == 3) {
				asm volatile (
					"vmov q13, q1\n"
				);
			}
			asm volatile (
				"vshr.s8 q11, q1, #7\n"
				"vshl.i8 q1, q1, #1\n"
				"vand q11, q11, q14\n"
				"veor q1, q1, q11\n"
				"vld1.8 {q6}, %0\n"
				"veor q0, q0, q6\n"
				"veor q1, q1, q6\n"
				:
				: "Q" (v[d][i])
			);
			if (generator == 3) {
				asm volatile (
					"veor q1, q1, q13\n"
				);
			}

			if (np >= 3) {
				asm volatile (
					"vshr.u8 q8, q6, #4\n"
					"vand q7, q6, q15\n"
					"vand q8, q8, q15\n"
				);
				asm volatile (
					"vld1.8 {q9}, %0\n"
					"vld1.8 {q10}, %1\n"

					"vtbl.8 d22, {d18-d19}, d14\n"
					"vtbl.8 d23, {d18-d19}, d15\n"
					"vtbl.8 d24, {d20-d21}, d16\n"
					"vtbl.8 d25, {d20-d21}, d17\n"

					"veor q11, q11, q12\n"
					"veor q2, q2, q11\n"
					:
					: "Q" (raid_gfcauchypshufb[d][1][0][0]),
					"Q" (raid_gfcauchypshufb[d][1][1][0])
				);
			}

			if (np >= 4) {
				asm volatile (
					"vld1.8 {q9}, %0\n"
					"vld1.8 {q10}, %1\n"
					"vtbl.8 d22, {d18-d19}, d14\n"
					"vtbl.8 d23, {d18-d19}, d15\n"
					"vtbl.8 d24, {d20-d21}, d16\n"
					"vtbl.8 d25, {d20-d21}, d17\n"
					"veor q11, q11, q12\n"
					"veor q3, q3, q11\n"
					:
					: "Q" (raid_gfcauchypshufb[d][2][0][0]),
					"Q" (raid_gfcauchypshufb[d][2][1][0])
				);
			}

			if (np >= 5) {
				asm volatile (
					"vld1.8 {q9}, %0\n"
					"vld1.8 {q10}, %1\n"
					"vtbl.8 d22, {d18-d19}, d14\n"
					"vtbl.8 d23, {d18-d19}, d15\n"
					"vtbl.8 d24, {d20-d21}, d16\n"
					"vtbl.8 d25, {d20-d21}, d17\n"
					"veor q11, q11, q12\n"
					"veor q4, q4, q11\n"
					:
					: "Q" (raid_gfcauchypshufb[d][3][0][0]),
					"Q" (raid_gfcauchypshufb[d][3][1][0])
				);
			}

			if (np >= 6) {
				asm volatile (
					"vld1.8 {q9}, %0\n"
					"vld1.8 {q10}, %1\n"
					"vtbl.8 d22, {d18-d19}, d14\n"
					"vtbl.8 d23, {d18-d19}, d15\n"
					"vtbl.8 d24, {d20-d21}, d16\n"
					"vtbl.8 d25, {d20-d21}, d17\n"
					"veor q11, q11, q12\n"
					"veor q5, q5, q11\n"
					:
					: "Q" (raid_gfcauchypshufb[d][4][0][0]),
					"Q" (raid_gfcauchypshufb[d][4][1][0])
				);
			}
		}

		/* first disk with all coefficients at 1 */
		if (generator == 3) {
			asm volatile (
				"vmov q13, q1\n"
			);
		}
		asm volatile (
			"vshr.s8 q11, q1, #7\n"
			"vshl.i8 q1, q1, #1\n"
			"vand q11, q11, q14\n"
			"veor q1, q1, q11\n"
			"vld1.8 {q6}, %0\n"
			"veor q0, q0, q6\n"
			"veor q1, q1, q6\n"
			:
			: "Q" (v[0][i])
		);
		if (generator == 3) {
			asm volatile (
				"veor q1, q1, q13\n"
			);
		}

		if (np >= 3) {
			asm volatile (
				"veor q2, q2, q6\n"
			);
		}
		if (np >= 4) {
			asm volatile (
				"veor q3, q3, q6\n"
			);
		}
		if (np >= 5) {
			asm volatile (
				"veor q4, q4, q6\n"
			);
		}
		if (np >= 6) {
			asm volatile (
				"veor q5, q5, q6\n"
			);
		}

		/* write parity in increasing order */
		asm volatile (
			"vst1.8 {q0}, %0\n"
			"vst1.8 {q1}, %1\n"
			: "=Q" (v[nd][i]),
			"=Q" (v[nd + 1][i])
		);

		if (np >= 3) {
			asm volatile (
				"vst1.8 {q2}, %0\n"
				: "=Q" (v[nd + 2][i])
			);
		}
		if (np >= 4) {
			asm volatile (
				"vst1.8 {q3}, %0\n"
				: "=Q" (v[nd + 3][i])
			);
		}
		if (np >= 5) {
			asm volatile (
				"vst1.8 {q4}, %0\n"
				: "=Q" (v[nd + 4][i])
			);
		}
		if (np >= 6) {
			asm volatile (
				"vst1.8 {q5}, %0\n"
				: "=Q" (v[nd + 5][i])
			);
		}
	}

	raid_neon32_end();
}

static __always_inline void raid_recX_neon32_123(int nr, int *id, int *ip,
	int nd, size_t size, void **vv)
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

	raid_neon32_begin();

	for (i = 0; i < size; i += 32) {
		/*
		 * Q15 is the low-nibble mask during generation/splitting.
		 * Reconstruction later reuses q15 as the second accumulator,
		 * therefore reload it on every iteration.
		 */
		asm volatile ("vld1.8 {q15}, %0" : : "Q" (gfconst16.low4[0]));

		/* selected stored parity */
		asm volatile ("vld1.8 {q0}, %0" : : "Q" (p[0][i]));
		asm volatile ("vld1.8 {q1}, %0" : : "Q" (p[0][i + 16]));

		if (nr >= 2) {
			asm volatile ("vld1.8 {q2}, %0" : : "Q" (p[1][i]));
			asm volatile ("vld1.8 {q3}, %0" : : "Q" (p[1][i + 16]));
		}

		if (nr >= 3) {
			asm volatile ("vld1.8 {q4}, %0" : : "Q" (p[2][i]));
			asm volatile ("vld1.8 {q5}, %0" : : "Q" (p[2][i + 16]));
		}

		for (s = 0; s < ns; ++s) {
			const uint8_t **t = S[s];

			asm volatile ("vld1.8 {q6}, %0" : : "Q" (src[s][i]));
			asm volatile ("vld1.8 {q7}, %0" : : "Q" (src[s][i + 16]));

			if (has_p) {
				asm volatile ("veor q0, q0, q6");
				asm volatile ("veor q1, q1, q7");

				asm volatile ("vshr.u8 q8, q6, #4");
				asm volatile ("vshr.u8 q9, q7, #4");
				asm volatile ("vand q6, q6, q15");
				asm volatile ("vand q7, q7, q15");
				asm volatile ("vand q8, q8, q15");
				asm volatile ("vand q9, q9, q15");
			} else {
				asm volatile ("vshr.u8 q8, q6, #4");
				asm volatile ("vshr.u8 q9, q7, #4");
				asm volatile ("vand q6, q6, q15");
				asm volatile ("vand q7, q7, q15");
				asm volatile ("vand q8, q8, q15");
				asm volatile ("vand q9, q9, q15");

				/*
				 * Q10 = low table
				 * q11 = high table
				 * q12/q13 = multiplication temporaries
				 */
				asm volatile ("vld1.8 {q10}, %0" : : "Q" (t[0][0]));
				asm volatile ("vld1.8 {q11}, %0" : : "Q" (t[0][16]));

				asm volatile ("vtbl.8 d24, {d20-d21}, d12");
				asm volatile ("vtbl.8 d25, {d20-d21}, d13");
				asm volatile ("vtbl.8 d26, {d22-d23}, d16");
				asm volatile ("vtbl.8 d27, {d22-d23}, d17");
				asm volatile ("veor q12, q12, q13");
				asm volatile ("veor q0, q0, q12");

				asm volatile ("vtbl.8 d24, {d20-d21}, d14");
				asm volatile ("vtbl.8 d25, {d20-d21}, d15");
				asm volatile ("vtbl.8 d26, {d22-d23}, d18");
				asm volatile ("vtbl.8 d27, {d22-d23}, d19");
				asm volatile ("veor q12, q12, q13");
				asm volatile ("veor q1, q1, q12");
			}

			if (nr >= 2) {
				asm volatile ("vld1.8 {q10}, %0" : : "Q" (t[1][0]));
				asm volatile ("vld1.8 {q11}, %0" : : "Q" (t[1][16]));

				asm volatile ("vtbl.8 d24, {d20-d21}, d12");
				asm volatile ("vtbl.8 d25, {d20-d21}, d13");
				asm volatile ("vtbl.8 d26, {d22-d23}, d16");
				asm volatile ("vtbl.8 d27, {d22-d23}, d17");
				asm volatile ("veor q12, q12, q13");
				asm volatile ("veor q2, q2, q12");

				asm volatile ("vtbl.8 d24, {d20-d21}, d14");
				asm volatile ("vtbl.8 d25, {d20-d21}, d15");
				asm volatile ("vtbl.8 d26, {d22-d23}, d18");
				asm volatile ("vtbl.8 d27, {d22-d23}, d19");
				asm volatile ("veor q12, q12, q13");
				asm volatile ("veor q3, q3, q12");
			}

			if (nr >= 3) {
				asm volatile ("vld1.8 {q10}, %0" : : "Q" (t[2][0]));
				asm volatile ("vld1.8 {q11}, %0" : : "Q" (t[2][16]));

				asm volatile ("vtbl.8 d24, {d20-d21}, d12");
				asm volatile ("vtbl.8 d25, {d20-d21}, d13");
				asm volatile ("vtbl.8 d26, {d22-d23}, d16");
				asm volatile ("vtbl.8 d27, {d22-d23}, d17");
				asm volatile ("veor q12, q12, q13");
				asm volatile ("veor q4, q4, q12");

				asm volatile ("vtbl.8 d24, {d20-d21}, d14");
				asm volatile ("vtbl.8 d25, {d20-d21}, d15");
				asm volatile ("vtbl.8 d26, {d22-d23}, d18");
				asm volatile ("vtbl.8 d27, {d22-d23}, d19");
				asm volatile ("veor q12, q12, q13");
				asm volatile ("veor q5, q5, q12");
			}
		}

		/*
		 * Expand raw syndromes backwards.
		 *
		 * Final layout:
		 *
		 * S0:
		 *   q0 = lane0 low
		 *   q1 = lane0 high
		 *   q2 = lane1 low
		 *   q3 = lane1 high
		 *
		 * S1:
		 *   q4/q5 = lane0 low/high
		 *   q6/q7 = lane1 low/high
		 *
		 * S2:
		 *   q8/q9   = lane0 low/high
		 *   q10/q11 = lane1 low/high
		 */

		if (nr >= 3) {
			asm volatile ("vshr.u8 q9, q4, #4");
			asm volatile ("vand q8, q4, q15");
			asm volatile ("vand q9, q9, q15");

			asm volatile ("vshr.u8 q11, q5, #4");
			asm volatile ("vand q10, q5, q15");
			asm volatile ("vand q11, q11, q15");
		}

		if (nr >= 2) {
			asm volatile ("vshr.u8 q5, q2, #4");
			asm volatile ("vand q4, q2, q15");
			asm volatile ("vand q5, q5, q15");

			asm volatile ("vshr.u8 q7, q3, #4");
			asm volatile ("vand q6, q3, q15");
			asm volatile ("vand q7, q7, q15");
		}

		/*
		 * Raw S0 lane1 is q1, so process it before q1 is overwritten
		 * by the high nibble of lane0.
		 */
		asm volatile ("vshr.u8 q3, q1, #4");
		asm volatile ("vand q2, q1, q15");
		asm volatile ("vand q3, q3, q15");

		asm volatile ("vshr.u8 q1, q0, #4");
		asm volatile ("vand q0, q0, q15");
		asm volatile ("vand q1, q1, q15");

		/*
		 * Reconstruction.
		 *
		 * q12 = multiplication table
		 * q13 = temporary result
		 * q14 = lane0 accumulator
		 * q15 = lane1 accumulator
		 */
		for (j = 0; j < nr; ++j) {
			const uint8_t **t = R[j];

			/* coefficient 0 initializes both accumulators */
			asm volatile ("vld1.8 {q12}, %0" : : "Q" (t[0][0]));

			asm volatile ("vtbl.8 d28, {d24-d25}, d0");
			asm volatile ("vtbl.8 d29, {d24-d25}, d1");
			asm volatile ("vtbl.8 d30, {d24-d25}, d4");
			asm volatile ("vtbl.8 d31, {d24-d25}, d5");

			asm volatile ("vld1.8 {q12}, %0" : : "Q" (t[0][16]));

			asm volatile ("vtbl.8 d26, {d24-d25}, d2");
			asm volatile ("vtbl.8 d27, {d24-d25}, d3");
			asm volatile ("veor q14, q14, q13");

			asm volatile ("vtbl.8 d26, {d24-d25}, d6");
			asm volatile ("vtbl.8 d27, {d24-d25}, d7");
			asm volatile ("veor q15, q15, q13");

			if (nr >= 2) {
				asm volatile ("vld1.8 {q12}, %0" : : "Q" (t[1][0]));

				asm volatile ("vtbl.8 d26, {d24-d25}, d8");
				asm volatile ("vtbl.8 d27, {d24-d25}, d9");
				asm volatile ("veor q14, q14, q13");

				asm volatile ("vtbl.8 d26, {d24-d25}, d12");
				asm volatile ("vtbl.8 d27, {d24-d25}, d13");
				asm volatile ("veor q15, q15, q13");

				asm volatile ("vld1.8 {q12}, %0" : : "Q" (t[1][16]));

				asm volatile ("vtbl.8 d26, {d24-d25}, d10");
				asm volatile ("vtbl.8 d27, {d24-d25}, d11");
				asm volatile ("veor q14, q14, q13");

				asm volatile ("vtbl.8 d26, {d24-d25}, d14");
				asm volatile ("vtbl.8 d27, {d24-d25}, d15");
				asm volatile ("veor q15, q15, q13");
			}

			if (nr >= 3) {
				asm volatile ("vld1.8 {q12}, %0" : : "Q" (t[2][0]));

				asm volatile ("vtbl.8 d26, {d24-d25}, d16");
				asm volatile ("vtbl.8 d27, {d24-d25}, d17");
				asm volatile ("veor q14, q14, q13");

				asm volatile ("vtbl.8 d26, {d24-d25}, d20");
				asm volatile ("vtbl.8 d27, {d24-d25}, d21");
				asm volatile ("veor q15, q15, q13");

				asm volatile ("vld1.8 {q12}, %0" : : "Q" (t[2][16]));

				asm volatile ("vtbl.8 d26, {d24-d25}, d18");
				asm volatile ("vtbl.8 d27, {d24-d25}, d19");
				asm volatile ("veor q14, q14, q13");

				asm volatile ("vtbl.8 d26, {d24-d25}, d22");
				asm volatile ("vtbl.8 d27, {d24-d25}, d23");
				asm volatile ("veor q15, q15, q13");
			}

			asm volatile ("vst1.8 {q14}, %0" : "=Q" (pa[j][i]));
			asm volatile ("vst1.8 {q15}, %0" : "=Q" (pa[j][i + 16]));
		}
	}

	raid_neon32_end();
}

static __always_inline void raid_recX_neon32(int nr, int *id, int *ip,
	int nd, size_t size, void **vv)
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

	raid_neon32_begin();

	for (i = 0; i < size; i += 16) {
		/*
		 * Q15 is the low-nibble mask during generation/splitting.
		 * Reconstruction later reuses q15 as a multiplication temporary.
		 */
		asm volatile ("vld1.8 {q15}, %0" : : "Q" (gfconst16.low4[0]));

		/*
		 * Raw syndrome registers:
		 *
		 * q0 = syndrome 0
		 * q1 = syndrome 1
		 * q2 = syndrome 2
		 * q3 = syndrome 3
		 * q4 = syndrome 4
		 * q5 = syndrome 5
		 */

		asm volatile ("vld1.8 {q0}, %0" : : "Q" (p[0][i]));

		if (nr >= 2)
			asm volatile ("vld1.8 {q1}, %0" : : "Q" (p[1][i]));

		if (nr >= 3)
			asm volatile ("vld1.8 {q2}, %0" : : "Q" (p[2][i]));

		if (nr >= 4)
			asm volatile ("vld1.8 {q3}, %0" : : "Q" (p[3][i]));

		if (nr >= 5)
			asm volatile ("vld1.8 {q4}, %0" : : "Q" (p[4][i]));

		if (nr >= 6)
			asm volatile ("vld1.8 {q5}, %0" : : "Q" (p[5][i]));

		/*
		 * During survivor generation:
		 *
		 * q6  = source low
		 * q7  = source high
		 * q8  = low table
		 * q9  = high table
		 * q10 = low result
		 * q11 = high result
		 */

		for (s = 0; s < ns; ++s) {
			const uint8_t **t = S[s];

			asm volatile ("vld1.8 {q6}, %0" : : "Q" (src[s][i]));

			if (has_p) {
				asm volatile ("veor q0, q0, q6");

				asm volatile ("vshr.u8 q7, q6, #4");
				asm volatile ("vand q6, q6, q15");
				asm volatile ("vand q7, q7, q15");
			} else {
				asm volatile ("vshr.u8 q7, q6, #4");
				asm volatile ("vand q6, q6, q15");
				asm volatile ("vand q7, q7, q15");

				asm volatile ("vld1.8 {q8}, %0" : : "Q" (t[0][0]));
				asm volatile ("vld1.8 {q9}, %0" : : "Q" (t[0][16]));

				asm volatile ("vtbl.8 d20, {d16-d17}, d12");
				asm volatile ("vtbl.8 d21, {d16-d17}, d13");
				asm volatile ("vtbl.8 d22, {d18-d19}, d14");
				asm volatile ("vtbl.8 d23, {d18-d19}, d15");
				asm volatile ("veor q10, q10, q11");
				asm volatile ("veor q0, q0, q10");
			}

			if (nr >= 2) {
				asm volatile ("vld1.8 {q8}, %0" : : "Q" (t[1][0]));
				asm volatile ("vld1.8 {q9}, %0" : : "Q" (t[1][16]));

				asm volatile ("vtbl.8 d20, {d16-d17}, d12");
				asm volatile ("vtbl.8 d21, {d16-d17}, d13");
				asm volatile ("vtbl.8 d22, {d18-d19}, d14");
				asm volatile ("vtbl.8 d23, {d18-d19}, d15");
				asm volatile ("veor q10, q10, q11");
				asm volatile ("veor q1, q1, q10");
			}

			if (nr >= 3) {
				asm volatile ("vld1.8 {q8}, %0" : : "Q" (t[2][0]));
				asm volatile ("vld1.8 {q9}, %0" : : "Q" (t[2][16]));

				asm volatile ("vtbl.8 d20, {d16-d17}, d12");
				asm volatile ("vtbl.8 d21, {d16-d17}, d13");
				asm volatile ("vtbl.8 d22, {d18-d19}, d14");
				asm volatile ("vtbl.8 d23, {d18-d19}, d15");
				asm volatile ("veor q10, q10, q11");
				asm volatile ("veor q2, q2, q10");
			}

			if (nr >= 4) {
				asm volatile ("vld1.8 {q8}, %0" : : "Q" (t[3][0]));
				asm volatile ("vld1.8 {q9}, %0" : : "Q" (t[3][16]));

				asm volatile ("vtbl.8 d20, {d16-d17}, d12");
				asm volatile ("vtbl.8 d21, {d16-d17}, d13");
				asm volatile ("vtbl.8 d22, {d18-d19}, d14");
				asm volatile ("vtbl.8 d23, {d18-d19}, d15");
				asm volatile ("veor q10, q10, q11");
				asm volatile ("veor q3, q3, q10");
			}

			if (nr >= 5) {
				asm volatile ("vld1.8 {q8}, %0" : : "Q" (t[4][0]));
				asm volatile ("vld1.8 {q9}, %0" : : "Q" (t[4][16]));

				asm volatile ("vtbl.8 d20, {d16-d17}, d12");
				asm volatile ("vtbl.8 d21, {d16-d17}, d13");
				asm volatile ("vtbl.8 d22, {d18-d19}, d14");
				asm volatile ("vtbl.8 d23, {d18-d19}, d15");
				asm volatile ("veor q10, q10, q11");
				asm volatile ("veor q4, q4, q10");
			}

			if (nr >= 6) {
				asm volatile ("vld1.8 {q8}, %0" : : "Q" (t[5][0]));
				asm volatile ("vld1.8 {q9}, %0" : : "Q" (t[5][16]));

				asm volatile ("vtbl.8 d20, {d16-d17}, d12");
				asm volatile ("vtbl.8 d21, {d16-d17}, d13");
				asm volatile ("vtbl.8 d22, {d18-d19}, d14");
				asm volatile ("vtbl.8 d23, {d18-d19}, d15");
				asm volatile ("veor q10, q10, q11");
				asm volatile ("veor q5, q5, q10");
			}
		}

		/*
		 * Expand raw syndromes backwards.
		 *
		 * Final layout:
		 *
		 * q0/q1   syndrome 0 low/high
		 * q2/q3   syndrome 1 low/high
		 * q4/q5   syndrome 2 low/high
		 * q6/q7   syndrome 3 low/high
		 * q8/q9   syndrome 4 low/high
		 * q10/q11 syndrome 5 low/high
		 */

		if (nr >= 6) {
			asm volatile ("vshr.u8 q11, q5, #4");
			asm volatile ("vand q10, q5, q15");
			asm volatile ("vand q11, q11, q15");
		}

		if (nr >= 5) {
			asm volatile ("vshr.u8 q9, q4, #4");
			asm volatile ("vand q8, q4, q15");
			asm volatile ("vand q9, q9, q15");
		}

		if (nr >= 4) {
			asm volatile ("vshr.u8 q7, q3, #4");
			asm volatile ("vand q6, q3, q15");
			asm volatile ("vand q7, q7, q15");
		}

		if (nr >= 3) {
			asm volatile ("vshr.u8 q5, q2, #4");
			asm volatile ("vand q4, q2, q15");
			asm volatile ("vand q5, q5, q15");
		}

		if (nr >= 2) {
			asm volatile ("vshr.u8 q3, q1, #4");
			asm volatile ("vand q2, q1, q15");
			asm volatile ("vand q3, q3, q15");
		}

		asm volatile ("vshr.u8 q1, q0, #4");
		asm volatile ("vand q0, q0, q15");
		asm volatile ("vand q1, q1, q15");

		/*
		 * Reconstruction:
		 *
		 * q12 = low table
		 * q13 = high table
		 * q14 = output accumulator
		 * q15 = multiplication temporary
		 */

		for (j = 0; j < nr; ++j) {
			const uint8_t **t = R[j];

			/* coefficient 0 initializes q14 */
			asm volatile ("vld1.8 {q12}, %0" : : "Q" (t[0][0]));
			asm volatile ("vld1.8 {q13}, %0" : : "Q" (t[0][16]));

			asm volatile ("vtbl.8 d28, {d24-d25}, d0");
			asm volatile ("vtbl.8 d29, {d24-d25}, d1");

			asm volatile ("vtbl.8 d30, {d26-d27}, d2");
			asm volatile ("vtbl.8 d31, {d26-d27}, d3");

			asm volatile ("veor q14, q14, q15");

			if (nr >= 2) {
				asm volatile ("vld1.8 {q12}, %0" : : "Q" (t[1][0]));
				asm volatile ("vld1.8 {q13}, %0" : : "Q" (t[1][16]));

				asm volatile ("vtbl.8 d30, {d24-d25}, d4");
				asm volatile ("vtbl.8 d31, {d24-d25}, d5");
				asm volatile ("veor q14, q14, q15");

				asm volatile ("vtbl.8 d30, {d26-d27}, d6");
				asm volatile ("vtbl.8 d31, {d26-d27}, d7");
				asm volatile ("veor q14, q14, q15");
			}

			if (nr >= 3) {
				asm volatile ("vld1.8 {q12}, %0" : : "Q" (t[2][0]));
				asm volatile ("vld1.8 {q13}, %0" : : "Q" (t[2][16]));

				asm volatile ("vtbl.8 d30, {d24-d25}, d8");
				asm volatile ("vtbl.8 d31, {d24-d25}, d9");
				asm volatile ("veor q14, q14, q15");

				asm volatile ("vtbl.8 d30, {d26-d27}, d10");
				asm volatile ("vtbl.8 d31, {d26-d27}, d11");
				asm volatile ("veor q14, q14, q15");
			}

			if (nr >= 4) {
				asm volatile ("vld1.8 {q12}, %0" : : "Q" (t[3][0]));
				asm volatile ("vld1.8 {q13}, %0" : : "Q" (t[3][16]));

				asm volatile ("vtbl.8 d30, {d24-d25}, d12");
				asm volatile ("vtbl.8 d31, {d24-d25}, d13");
				asm volatile ("veor q14, q14, q15");

				asm volatile ("vtbl.8 d30, {d26-d27}, d14");
				asm volatile ("vtbl.8 d31, {d26-d27}, d15");
				asm volatile ("veor q14, q14, q15");
			}

			if (nr >= 5) {
				asm volatile ("vld1.8 {q12}, %0" : : "Q" (t[4][0]));
				asm volatile ("vld1.8 {q13}, %0" : : "Q" (t[4][16]));

				asm volatile ("vtbl.8 d30, {d24-d25}, d16");
				asm volatile ("vtbl.8 d31, {d24-d25}, d17");
				asm volatile ("veor q14, q14, q15");

				asm volatile ("vtbl.8 d30, {d26-d27}, d18");
				asm volatile ("vtbl.8 d31, {d26-d27}, d19");
				asm volatile ("veor q14, q14, q15");
			}

			if (nr >= 6) {
				asm volatile ("vld1.8 {q12}, %0" : : "Q" (t[5][0]));
				asm volatile ("vld1.8 {q13}, %0" : : "Q" (t[5][16]));

				asm volatile ("vtbl.8 d30, {d24-d25}, d20");
				asm volatile ("vtbl.8 d31, {d24-d25}, d21");
				asm volatile ("veor q14, q14, q15");

				asm volatile ("vtbl.8 d30, {d26-d27}, d22");
				asm volatile ("vtbl.8 d31, {d26-d27}, d23");
				asm volatile ("veor q14, q14, q15");
			}

			asm volatile ("vst1.8 {q14}, %0" : "=Q" (pa[j][i]));
		}
	}

	raid_neon32_end();
}

void raid_gen2_neon32_raid(int nd, size_t size, void **vv)
{
	raid_gen2_neon32_gen(nd, size, vv, 2);
}

void raid_gen2_neon32_aes(int nd, size_t size, void **vv)
{
	raid_gen2_neon32_gen(nd, size, vv, 3);
}

/*
 * GEN3 (triple parity with Cauchy matrix) AArch32 NEON implementation
 */
void raid_gen3_neon32_raid(int nd, size_t size, void **vv)
{
	raid_genX_neon32(nd, size, vv, 3, 2);
}

void raid_gen3_neon32_aes(int nd, size_t size, void **vv)
{
	raid_genX_neon32(nd, size, vv, 3, 3);
}

/*
 * GEN4 (quad parity with Cauchy matrix) AArch32 NEON implementation
 */
void raid_gen4_neon32_raid(int nd, size_t size, void **vv)
{
	raid_genX_neon32(nd, size, vv, 4, 2);
}

void raid_gen4_neon32_aes(int nd, size_t size, void **vv)
{
	raid_genX_neon32(nd, size, vv, 4, 3);
}

/*
 * GEN5 (penta parity with Cauchy matrix) AArch32 NEON implementation
 */
void raid_gen5_neon32_raid(int nd, size_t size, void **vv)
{
	raid_genX_neon32(nd, size, vv, 5, 2);
}

void raid_gen5_neon32_aes(int nd, size_t size, void **vv)
{
	raid_genX_neon32(nd, size, vv, 5, 3);
}

/*
 * GEN6 (hexa parity with Cauchy matrix) AArch32 NEON implementation
 */
void raid_gen6_neon32_raid(int nd, size_t size, void **vv)
{
	raid_genX_neon32(nd, size, vv, 6, 2);
}

void raid_gen6_neon32_aes(int nd, size_t size, void **vv)
{
	raid_genX_neon32(nd, size, vv, 6, 3);
}

void raid_rec1_neon32(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 1);

	/* if recovering with P uses the delta function */
	if (ip[0] == 0) {
		raid_rec1of1(id, nd, size, vv);
		return;
	}

	raid_recX_neon32_123(1, id, ip, nd, size, vv);
}

/*
 * Recover failure of two data blocks using P and Q AArch32 NEON implementation.
 */
static __always_inline void raid_rec2of2_neon32(int *id, int *ip, int nd, size_t size, void **vv)
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

	raid_neon32_begin();

	/* q15 = nibble mask, q9-q12 = C0/C1 low/high tables */
	asm volatile (
		"vld1.8 {q15}, %0\n"
		"vld1.8 {q9}, %1\n"
		"vld1.8 {q10}, %2\n"
		"vld1.8 {q11}, %3\n"
		"vld1.8 {q12}, %4\n"
		:
		: "Q" (gfconst16.low4[0]),
		"Q" (raid_gfmulpshufb[C[0]][0][0]),
		"Q" (raid_gfmulpshufb[C[0]][1][0]),
		"Q" (raid_gfmulpshufb[C[1]][0][0]),
		"Q" (raid_gfmulpshufb[C[1]][1][0])
	);

	for (i = 0; i < size; i += 16) {
		asm volatile (
			/* Pd */
			"vld1.8 {q0}, %2\n"
			"vld1.8 {q13}, %4\n"
			"veor q0, q0, q13\n"

			/* Qd */
			"vld1.8 {q1}, %3\n"
			"vld1.8 {q13}, %5\n"
			"veor q1, q1, q13\n"

			/* split Pd */
			"vmov q2, q0\n"
			"vshr.u8 q3, q0, #4\n"
			"vand q2, q2, q15\n"
			"vand q3, q3, q15\n"

			/* split Qd */
			"vmov q4, q1\n"
			"vshr.u8 q5, q1, #4\n"
			"vand q4, q4, q15\n"
			"vand q5, q5, q15\n"

			/* C0 low * Pd low -> q6 */
			"vtbl.8 d12, {d18-d19}, d4\n"
			"vtbl.8 d13, {d18-d19}, d5\n"

			/* C0 high * Pd high -> q7 */
			"vtbl.8 d14, {d20-d21}, d6\n"
			"vtbl.8 d15, {d20-d21}, d7\n"
			"veor q6, q6, q7\n"

			/* C1 low * Qd low -> q7 */
			"vtbl.8 d14, {d22-d23}, d8\n"
			"vtbl.8 d15, {d22-d23}, d9\n"

			/* C1 high * Qd high -> q8 */
			"vtbl.8 d16, {d24-d25}, d10\n"
			"vtbl.8 d17, {d24-d25}, d11\n"
			"veor q7, q7, q8\n"

			/* Dy */
			"veor q6, q6, q7\n"

			/* Dx = Pd ^ Dy */
			"veor q0, q0, q6\n"

			"vst1.8 {q0}, %0\n"
			"vst1.8 {q6}, %1\n"
			: "=Q" (pa[i]), "=Q" (qa[i])
			: "Q" (p[i]), "Q" (q[i]),
			"Q" (pa[i]), "Q" (qa[i])
		);
	}

	raid_neon32_end();
}

void raid_rec2_neon32(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 2);

	if (ip[0] == 0 && ip[1] == 1) {
		raid_rec2of2_neon32(id, ip, nd, size, vv);
		return;
	}

	raid_recX_neon32_123(2, id, ip, nd, size, vv);
}

void raid_rec3_neon32(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 3);
	raid_recX_neon32_123(3, id, ip, nd, size, vv);
}

void raid_rec4_neon32(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 4);
	raid_recX_neon32(4, id, ip, nd, size, vv);
}

void raid_rec5_neon32(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 5);
	raid_recX_neon32(5, id, ip, nd, size, vv);
}

void raid_rec6_neon32(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 6);
	raid_recX_neon32(6, id, ip, nd, size, vv);
}

void raid_register_neon32(void)
{
	raid_gen_register(RAID_ALGO_CAUCHY_PAR1, "neon32", raid_gen1_neon32, RAID_POLY_ANY);

	raid_gen_register(RAID_ALGO_CAUCHY_PAR2, "neon32", raid_gen2_neon32_raid, RAID_POLY_RAID);
	raid_gen_register(RAID_ALGO_CAUCHY_PAR2, "neon32", raid_gen2_neon32_aes, RAID_POLY_AES);

	raid_gen_register(RAID_ALGO_VANDERMONDE_PAR3, "neon32", raid_genz_neon32_raid, RAID_POLY_RAID);

	raid_gen_register(RAID_ALGO_CAUCHY_PAR3, "neon32", raid_gen3_neon32_raid, RAID_POLY_RAID);
	raid_gen_register(RAID_ALGO_CAUCHY_PAR3, "neon32", raid_gen3_neon32_aes, RAID_POLY_AES);

	raid_gen_register(RAID_ALGO_CAUCHY_PAR4, "neon32", raid_gen4_neon32_raid, RAID_POLY_RAID);
	raid_gen_register(RAID_ALGO_CAUCHY_PAR4, "neon32", raid_gen4_neon32_aes, RAID_POLY_AES);

	raid_gen_register(RAID_ALGO_CAUCHY_PAR5, "neon32", raid_gen5_neon32_raid, RAID_POLY_RAID);
	raid_gen_register(RAID_ALGO_CAUCHY_PAR5, "neon32", raid_gen5_neon32_aes, RAID_POLY_AES);

	raid_gen_register(RAID_ALGO_CAUCHY_PAR6, "neon32", raid_gen6_neon32_raid, RAID_POLY_RAID);
	raid_gen_register(RAID_ALGO_CAUCHY_PAR6, "neon32", raid_gen6_neon32_aes, RAID_POLY_AES);

	raid_rec_register(RAID_ALGO_CAUCHY_PAR1, "neon32", raid_rec1_neon32, RAID_POLY_ANY);
	raid_rec_register(RAID_ALGO_CAUCHY_PAR2, "neon32", raid_rec2_neon32, RAID_POLY_ANY);
	raid_rec_register(RAID_ALGO_CAUCHY_PAR3, "neon32", raid_rec3_neon32, RAID_POLY_ANY);
	raid_rec_register(RAID_ALGO_CAUCHY_PAR4, "neon32", raid_rec4_neon32, RAID_POLY_ANY);
	raid_rec_register(RAID_ALGO_CAUCHY_PAR5, "neon32", raid_rec5_neon32, RAID_POLY_ANY);
	raid_rec_register(RAID_ALGO_CAUCHY_PAR6, "neon32", raid_rec6_neon32, RAID_POLY_ANY);
}

#endif
