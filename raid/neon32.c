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
 * GEN2 Cauchy AArch32 NEON implementation using only Q *= 2.
 */
static __always_inline void raid_gen2_neon32_x2(int nd, size_t size, void **vv)
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

	/* q14 contains the active reduction polynomial. */
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
			asm volatile (
				/* double Q0-Q1 */
				"vshr.s8 q8, q2, #7\n"
				"vshl.i8 q2, q2, #1\n"
				"vand q8, q8, q14\n"
				"veor q2, q2, q8\n"

				"vshr.s8 q8, q3, #7\n"
				"vshl.i8 q3, q3, #1\n"
				"vand q8, q8, q14\n"
				"veor q3, q3, q8\n"

				/* load and XOR */
				"vld1.8 {q12}, %0\n"
				"vld1.8 {q13}, %1\n"
				"veor q0, q0, q12\n"
				"veor q1, q1, q13\n"
				"veor q2, q2, q12\n"
				"veor q3, q3, q13\n"
				:
				: "Q" (v[d][i]), "Q" (v[d][i + 16])
			);
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
 * GEN2 Cauchy AArch32 NEON implementation using the AES G23 Q recurrence.
 */
static __always_inline void raid_gen2_neon32_g23(int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *q;
	int d, l;
	size_t i;

	l = nd - 1;
	p = v[nd];
	q = v[nd + 1];

	int g23_boundary = raid_g23_boundary(l - 1);

	raid_neon32_begin();

	/* q14 contains the active reduction polynomial. */
	asm volatile (
		"vld1.8 {q14}, %0\n"
		:
		: "Q" (gfconst16.poly[0])
	);

	for (i = 0; i < size; i += 32) {
		int boundary = g23_boundary;

		asm volatile (
			"vld1.8 {q0}, %0\n"
			"vld1.8 {q1}, %1\n"
			"vmov q2, q0\n"
			"vmov q3, q1\n"
			:
			: "Q" (v[l][i]), "Q" (v[l][i + 16])
		);

		d = l - 1;

		for (;;) {
			/*
			 * All transitions above 'boundary' are x2.
			 */
			for (; d > boundary; --d) {
				asm volatile (
					/* double Q0-Q1 */
					"vshr.s8 q8, q2, #7\n"
					"vshl.i8 q2, q2, #1\n"
					"vand q8, q8, q14\n"
					"veor q2, q2, q8\n"

					"vshr.s8 q8, q3, #7\n"
					"vshl.i8 q3, q3, #1\n"
					"vand q8, q8, q14\n"
					"veor q3, q3, q8\n"

					/* load and XOR */
					"vld1.8 {q12}, %0\n"
					"vld1.8 {q13}, %1\n"
					"veor q0, q0, q12\n"
					"veor q1, q1, q13\n"
					"veor q2, q2, q12\n"
					"veor q3, q3, q13\n"
					:
					: "Q" (v[d][i]), "Q" (v[d][i + 16])
				);
			}

			/*
			 * boundary == -1 identifies the final x2-only segment.
			 * At this point all remaining disks have been consumed.
			 */
			if (boundary < 0)
				break;

			/*
			 * d == boundary.
			 *
			 * Perform the fixed G23 x3 transition and then add
			 * the current disk to P and Q.
			 */
			asm volatile (
				/* preserve Q for Q *= 3 */
				"vmov q10, q2\n"
				"vmov q11, q3\n"

				/* double Q0-Q1 */
				"vshr.s8 q8, q2, #7\n"
				"vshl.i8 q2, q2, #1\n"
				"vand q8, q8, q14\n"
				"veor q2, q2, q8\n"

				"vshr.s8 q8, q3, #7\n"
				"vshl.i8 q3, q3, #1\n"
				"vand q8, q8, q14\n"
				"veor q3, q3, q8\n"

				/* load and XOR */
				"vld1.8 {q12}, %0\n"
				"vld1.8 {q13}, %1\n"
				"veor q0, q0, q12\n"
				"veor q1, q1, q13\n"
				"veor q2, q2, q12\n"
				"veor q3, q3, q13\n"

				/* complete Q *= 3 */
				"veor q2, q2, q10\n"
				"veor q3, q3, q11\n"
				:
				: "Q" (v[d][i]), "Q" (v[d][i + 16])
			);

			--d;

			/*
			 * G23 boundaries are exactly 51 transitions apart.
			 * After d = 50 there are no more x3 transitions.
			 */
			boundary -= 51;
			if (boundary < 50)
				boundary = -1;
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

void raid_gen2_neon32_raid(int nd, size_t size, void **vv)
{
	raid_gen2_neon32_x2(nd, size, vv);
}

void raid_gen2_neon32_aes(int nd, size_t size, void **vv)
{
	if (raid_g23_x2_only(nd))
		raid_gen2_neon32_x2(nd, size, vv);
	else
		raid_gen2_neon32_g23(nd, size, vv);
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
 * GENX AArch32 NEON implementation using only Q *= 2.
 */
static __always_inline void raid_genX_neon32_x2(int nd, size_t size,
	void **vv, int np)
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

	asm volatile (
		"vld1.8 {q14}, %0\n"
		"vld1.8 {q15}, %1\n"
		:
		: "Q" (gfconst16.poly[0]), "Q" (gfconst16.low4[0])
	);

	for (i = 0; i < size; i += 16) {
		/* last disk initializes P/Q without a preceding Q transition */
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
			asm volatile (
				/* Q *= 2 */
				"vshr.s8 q11, q1, #7\n"
				"vshl.i8 q1, q1, #1\n"
				"vand q11, q11, q14\n"
				"veor q1, q1, q11\n"

				/* load disk and update P/Q */
				"vld1.8 {q6}, %0\n"
				"veor q0, q0, q6\n"
				"veor q1, q1, q6\n"
				:
				: "Q" (v[d][i])
			);

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

		if (np >= 3)
			asm volatile ("veor q2, q2, q6\n");
		if (np >= 4)
			asm volatile ("veor q3, q3, q6\n");
		if (np >= 5)
			asm volatile ("veor q4, q4, q6\n");
		if (np >= 6)
			asm volatile ("veor q5, q5, q6\n");

		/* write parity in increasing order */
		asm volatile (
			"vst1.8 {q0}, %0\n"
			"vst1.8 {q1}, %1\n"
			: "=Q" (v[nd][i]),
			  "=Q" (v[nd + 1][i])
		);

		if (np >= 3)
			asm volatile ("vst1.8 {q2}, %0\n" : "=Q" (v[nd + 2][i]));
		if (np >= 4)
			asm volatile ("vst1.8 {q3}, %0\n" : "=Q" (v[nd + 3][i]));
		if (np >= 5)
			asm volatile ("vst1.8 {q4}, %0\n" : "=Q" (v[nd + 4][i]));
		if (np >= 6)
			asm volatile ("vst1.8 {q5}, %0\n" : "=Q" (v[nd + 5][i]));
	}

	raid_neon32_end();
}

/*
 * GENX AArch32 NEON implementation using the AES G23 Q recurrence.
 */
static __always_inline void raid_genX_neon32_g23(int nd, size_t size,
	void **vv, int np)
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

	int g23_boundary = raid_g23_boundary(l - 1);

	raid_neon32_begin();

	asm volatile (
		"vld1.8 {q14}, %0\n"
		"vld1.8 {q15}, %1\n"
		:
		: "Q" (gfconst16.poly[0]), "Q" (gfconst16.low4[0])
	);

	for (i = 0; i < size; i += 16) {
		int boundary = g23_boundary;

		/* last disk initializes P/Q without a preceding Q transition */
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

		d = l - 1;

		while (boundary >= 0) {
			/*
			 * Everything above the next G23 boundary is Q *= 2.
			 */
			for (; d > boundary; --d) {
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

			/*
			 * d == boundary: Q *= 3.
			 *
			 * q13 is otherwise unused here, so preserve the old Q
			 * there and XOR it back after the normal x2 transition.
			 */
			asm volatile (
				"vmov q13, q1\n"

				"vshr.s8 q11, q1, #7\n"
				"vshl.i8 q1, q1, #1\n"
				"vand q11, q11, q14\n"
				"veor q1, q1, q11\n"

				"vld1.8 {q6}, %0\n"
				"veor q0, q0, q6\n"
				"veor q1, q1, q6\n"

				/* complete Q *= 3 */
				"veor q1, q1, q13\n"
				:
				: "Q" (v[d][i])
			);

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

			--d;

			boundary -= 51;
			if (boundary < 50)
				boundary = -1;
		}

		/*
		 * Remaining intermediate disks contain no more G23
		 * boundaries, so this loop is pure x2.
		 */
		for (; d > 0; --d) {
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

		/*
		 * D0 always follows Q *= 2. The lowest G23 boundary is 50.
		 */
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

		if (np >= 3)
			asm volatile ("veor q2, q2, q6\n");
		if (np >= 4)
			asm volatile ("veor q3, q3, q6\n");
		if (np >= 5)
			asm volatile ("veor q4, q4, q6\n");
		if (np >= 6)
			asm volatile ("veor q5, q5, q6\n");

		/* write parity in increasing order */
		asm volatile (
			"vst1.8 {q0}, %0\n"
			"vst1.8 {q1}, %1\n"
			: "=Q" (v[nd][i]),
			  "=Q" (v[nd + 1][i])
		);

		if (np >= 3)
			asm volatile ("vst1.8 {q2}, %0\n" : "=Q" (v[nd + 2][i]));
		if (np >= 4)
			asm volatile ("vst1.8 {q3}, %0\n" : "=Q" (v[nd + 3][i]));
		if (np >= 5)
			asm volatile ("vst1.8 {q4}, %0\n" : "=Q" (v[nd + 4][i]));
		if (np >= 6)
			asm volatile ("vst1.8 {q5}, %0\n" : "=Q" (v[nd + 5][i]));
	}

	raid_neon32_end();
}

/*
 * GEN3 (triple parity with Cauchy matrix) AArch32 NEON implementation
 */
void raid_gen3_neon32_raid(int nd, size_t size, void **vv)
{
	raid_genX_neon32_x2(nd, size, vv, 3);
}

void raid_gen3_neon32_aes(int nd, size_t size, void **vv)
{
	if (raid_g23_x2_only(nd))
		raid_genX_neon32_x2(nd, size, vv, 3);
	else
		raid_genX_neon32_g23(nd, size, vv, 3);
}

/*
 * GEN4 (quad parity with Cauchy matrix) AArch32 NEON implementation
 */
void raid_gen4_neon32_raid(int nd, size_t size, void **vv)
{
	raid_genX_neon32_x2(nd, size, vv, 4);
}

void raid_gen4_neon32_aes(int nd, size_t size, void **vv)
{
	if (raid_g23_x2_only(nd))
		raid_genX_neon32_x2(nd, size, vv, 4);
	else
		raid_genX_neon32_g23(nd, size, vv, 4);
}

/*
 * GEN5 (penta parity with Cauchy matrix) AArch32 NEON implementation
 */
void raid_gen5_neon32_raid(int nd, size_t size, void **vv)
{
	raid_genX_neon32_x2(nd, size, vv, 5);
}

void raid_gen5_neon32_aes(int nd, size_t size, void **vv)
{
	if (raid_g23_x2_only(nd))
		raid_genX_neon32_x2(nd, size, vv, 5);
	else
		raid_genX_neon32_g23(nd, size, vv, 5);
}

/*
 * GEN6 (hexa parity with Cauchy matrix) AArch32 NEON implementation
 */
void raid_gen6_neon32_raid(int nd, size_t size, void **vv)
{
	raid_genX_neon32_x2(nd, size, vv, 6);
}

void raid_gen6_neon32_aes(int nd, size_t size, void **vv)
{
	if (raid_g23_x2_only(nd))
		raid_genX_neon32_x2(nd, size, vv, 6);
	else
		raid_genX_neon32_g23(nd, size, vv, 6);
}

/*
 * RAID recovering for one disk AArch32 NEON implementation
 */
void raid_rec1_neon32(int nr, int *id, int *ip, int nd,
	size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *pa;
	uint8_t G;
	uint8_t V;
	size_t i;

	(void)nr; /* unused, it's always 1 */

	/* if it's RAID5 use the faster function */
	if (ip[0] == 0) {
		raid_rec1of1(id, nd, size, vv);
		return;
	}

	/* setup the coefficients matrix */
	G = A(ip[0], id[0]);

	/* invert it to solve the system of linear equations */
	V = inv(G);

	/* compute delta parity */
	raid_delta_gen(1, id, ip, nd, size, vv);

	p = v[nd + ip[0]];
	pa = v[id[0]];

	raid_neon32_begin();

	/*
	 * Preload the mask and multiplication tables, just as neon.c does.
	 *
	 * q12 = low table
	 * q13 = high table
	 * q14 = low4 mask
	 */
	asm volatile (
		"vld1.8 {q14}, %0\n"
		"vld1.8 {q12}, %1\n"
		"vld1.8 {q13}, %2\n"
		:
		: "Q" (gfconst16.low4[0]),
		"Q" (raid_gfmulpshufb[V][0][0]),
		"Q" (raid_gfmulpshufb[V][1][0])
	);

	for (i = 0; i < size; i += 32) {
		asm volatile (
			"vld1.8 {q0}, %2\n"
			"vld1.8 {q1}, %3\n"
			"vld1.8 {q2}, %4\n"
			"vld1.8 {q3}, %5\n"

			"veor q0, q0, q2\n"
			"veor q1, q1, q3\n"

			"vshr.u8 q4, q0, #4\n"
			"vshr.u8 q5, q1, #4\n"

			"vand q0, q0, q14\n"
			"vand q1, q1, q14\n"
			"vand q4, q4, q14\n"
			"vand q5, q5, q14\n"

			/* low nibbles */
			"vtbl.8 d12, {d24-d25}, d0\n"
			"vtbl.8 d13, {d24-d25}, d1\n"
			"vtbl.8 d14, {d24-d25}, d2\n"
			"vtbl.8 d15, {d24-d25}, d3\n"

			/* high nibbles */
			"vtbl.8 d16, {d26-d27}, d8\n"
			"vtbl.8 d17, {d26-d27}, d9\n"
			"vtbl.8 d18, {d26-d27}, d10\n"
			"vtbl.8 d19, {d26-d27}, d11\n"

			"veor q6, q6, q8\n"
			"veor q7, q7, q9\n"

			"vst1.8 {q6}, %0\n"
			"vst1.8 {q7}, %1\n"
			: "=Q" (pa[i]), "=Q" (pa[i + 16])
			: "Q" (p[i]), "Q" (p[i + 16]),
			"Q" (pa[i]), "Q" (pa[i + 16])
		);
	}

	raid_neon32_end();
}

/*
 * RAID recovering for two disks AArch32 NEON implementation
 */
void raid_rec2_neon32(int nr, int *id, int *ip, int nd,
	size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	const int N = 2;
	uint8_t *p[2];
	uint8_t *pa[2];
	uint8_t G[2 * 2];
	uint8_t V[2 * 2];
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

	raid_neon32_begin();

	/*
	 * q4-q11 contain the four low/high multiplication table pairs.
	 * q15 contains low4.
	 */
	asm volatile (
		"vld1.8 {q15}, %0\n"

		"vld1.8 {q4}, %1\n"
		"vld1.8 {q5}, %2\n"

		"vld1.8 {q6}, %3\n"
		"vld1.8 {q7}, %4\n"

		"vld1.8 {q8}, %5\n"
		"vld1.8 {q9}, %6\n"

		"vld1.8 {q10}, %7\n"
		"vld1.8 {q11}, %8\n"
		:
		: "Q" (gfconst16.low4[0]),
		"Q" (raid_gfmulpshufb[V[0]][0][0]),
		"Q" (raid_gfmulpshufb[V[0]][1][0]),
		"Q" (raid_gfmulpshufb[V[1]][0][0]),
		"Q" (raid_gfmulpshufb[V[1]][1][0]),
		"Q" (raid_gfmulpshufb[V[2]][0][0]),
		"Q" (raid_gfmulpshufb[V[2]][1][0]),
		"Q" (raid_gfmulpshufb[V[3]][0][0]),
		"Q" (raid_gfmulpshufb[V[3]][1][0])
	);

	for (i = 0; i < size; i += 16) {
		asm volatile (
			/* delta 0 */
			"vld1.8 {q0}, %2\n"
			"vld1.8 {q12}, %4\n"
			"veor q0, q0, q12\n"
			"vshr.u8 q1, q0, #4\n"
			"vand q0, q0, q15\n"
			"vand q1, q1, q15\n"

			/* delta 1 */
			"vld1.8 {q2}, %3\n"
			"vld1.8 {q12}, %5\n"
			"veor q2, q2, q12\n"
			"vshr.u8 q3, q2, #4\n"
			"vand q2, q2, q15\n"
			"vand q3, q3, q15\n"

			/* pa[0] = V[0] * delta0 */

			"vtbl.8 d24, {d8-d9}, d0\n"
			"vtbl.8 d25, {d8-d9}, d1\n"
			"vtbl.8 d26, {d10-d11}, d2\n"
			"vtbl.8 d27, {d10-d11}, d3\n"
			"veor q14, q12, q13\n"

			/* ^ V[1] * delta1 */

			"vtbl.8 d24, {d12-d13}, d4\n"
			"vtbl.8 d25, {d12-d13}, d5\n"
			"vtbl.8 d26, {d14-d15}, d6\n"
			"vtbl.8 d27, {d14-d15}, d7\n"
			"veor q12, q12, q13\n"
			"veor q14, q14, q12\n"

			"vst1.8 {q14}, %0\n"

			/* pa[1] = V[2] * delta0 */

			"vtbl.8 d24, {d16-d17}, d0\n"
			"vtbl.8 d25, {d16-d17}, d1\n"
			"vtbl.8 d26, {d18-d19}, d2\n"
			"vtbl.8 d27, {d18-d19}, d3\n"
			"veor q14, q12, q13\n"

			/* ^ V[3] * delta1 */

			"vtbl.8 d24, {d20-d21}, d4\n"
			"vtbl.8 d25, {d20-d21}, d5\n"
			"vtbl.8 d26, {d22-d23}, d6\n"
			"vtbl.8 d27, {d22-d23}, d7\n"
			"veor q12, q12, q13\n"
			"veor q14, q14, q12\n"

			"vst1.8 {q14}, %1\n"
			: "=Q" (pa[0][i]), "=Q" (pa[1][i])
			: "Q" (p[0][i]), "Q" (p[1][i]),
			"Q" (pa[0][i]), "Q" (pa[1][i])
		);
	}

	raid_neon32_end();
}

/*
 * RAID recovering AArch32 NEON implementation
 */
void raid_recX_neon32(int nr, int *id, int *ip, int nd,
	size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	int N = nr;
	uint8_t *p[RAID_PARITY_MAX];
	uint8_t *pa[RAID_PARITY_MAX];
	uint8_t G[RAID_PARITY_MAX * RAID_PARITY_MAX];
	uint8_t V[RAID_PARITY_MAX * RAID_PARITY_MAX];
	const uint8_t *T[RAID_PARITY_MAX * RAID_PARITY_MAX];
	uint8_t D[RAID_PARITY_MAX][2][16] __aligned(16);
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

	raid_neon32_begin();

	/* preload mask */
	asm volatile (
		"vld1.8 {q15}, %0\n"
		:
		: "Q" (gfconst16.low4[0])
	);

	for (i = 0; i < size; i += 16) {
		for (k = 0; k < N; ++k) {
			asm volatile (
				"vld1.8 {q0}, %2\n"
				"vld1.8 {q1}, %3\n"
				"veor q0, q0, q1\n"

				"vshr.u8 q1, q0, #4\n"
				"vand q0, q0, q15\n"
				"vand q1, q1, q15\n"

				"vst1.8 {q0}, %0\n"
				"vst1.8 {q1}, %1\n"
				: "=Q" (D[k][0][0]), "=Q" (D[k][1][0])
				: "Q" (p[k][i]), "Q" (pa[k][i])
			);
		}

		/* reconstruct */
		for (j = 0; j < N; ++j) {
			asm volatile (
				"veor q0, q0, q0\n"
			);

			for (k = 0; k < N; ++k) {
				asm volatile (
					"vld1.8 {q2}, %0\n"
					"vld1.8 {q3}, %1\n"

					"vld1.8 {q4}, %2\n"
					"vld1.8 {q5}, %3\n"

					"vtbl.8 d12, {d8-d9}, d4\n"
					"vtbl.8 d13, {d8-d9}, d5\n"

					"vtbl.8 d14, {d10-d11}, d6\n"
					"vtbl.8 d15, {d10-d11}, d7\n"

					"veor q6, q6, q7\n"
					"veor q0, q0, q6\n"
					:
					: "Q" (D[k][0][0]),
					"Q" (D[k][1][0]),
					"Q" (T[j * N + k][0]),
					"Q" (T[j * N + k][16])
				);
			}

			asm volatile (
				"vst1.8 {q0}, %0\n"
				: "=Q" (pa[j][i])
			);
		}
	}

	raid_neon32_end();
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
	raid_rec_register(RAID_ALGO_CAUCHY_PAR3, "neon32", raid_recX_neon32, RAID_POLY_ANY);
	raid_rec_register(RAID_ALGO_CAUCHY_PAR4, "neon32", raid_recX_neon32, RAID_POLY_ANY);
	raid_rec_register(RAID_ALGO_CAUCHY_PAR5, "neon32", raid_recX_neon32, RAID_POLY_ANY);
	raid_rec_register(RAID_ALGO_CAUCHY_PAR6, "neon32", raid_recX_neon32, RAID_POLY_ANY);
}

#endif
