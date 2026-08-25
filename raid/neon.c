// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Andrea Mazzoleni

#include "internal.h"
#include "gf.h"
#include "cpu.h"

#ifdef CONFIG_NEON
/*
 * Generate one parity block (RAID5 with XOR) using NEON implementation.
 *
 * Uses 64-byte chunks across four 16-byte vectors.
 */
void raid_gen1_neon(int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	int d, l;
	size_t i;

	l = nd - 1;
	p = v[nd];

	raid_neon_begin();

	for (i = 0; i < size; i += 64) {
		asm volatile (
			"ldr q0, %0\n"
			"ldr q1, %1\n"
			"ldr q2, %2\n"
			"ldr q3, %3\n"
			:
			: "m" (v[0][i]), "m" (v[0][i + 16]), "m" (v[0][i + 32]), "m" (v[0][i + 48])
		);

		for (d = 1; d <= l; ++d) {
			asm volatile (
				"ldr q4, %0\n"
				"ldr q5, %1\n"
				"ldr q6, %2\n"
				"ldr q7, %3\n"
				"eor v0.16b, v0.16b, v4.16b\n"
				"eor v1.16b, v1.16b, v5.16b\n"
				"eor v2.16b, v2.16b, v6.16b\n"
				"eor v3.16b, v3.16b, v7.16b\n"
				:
				: "m" (v[d][i]), "m" (v[d][i + 16]), "m" (v[d][i + 32]), "m" (v[d][i + 48])
			);
		}

		asm volatile (
			"str q0, %0\n"
			"str q1, %1\n"
			"str q2, %2\n"
			"str q3, %3\n"
			: "=m" (p[i]), "=m" (p[i + 16]), "=m" (p[i + 32]), "=m" (p[i + 48])
		);
	}

	raid_neon_end();
}

/*
 * Generate two parity blocks (RAID6 with Cauchy matrix) using NEON implementation.
 */
static __always_inline void raid_gen2_neon_gen(int nd, size_t size, void **vv, int generator)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *q;
	int d, l;
	size_t i;

	l = nd - 1;
	p = v[nd];
	q = v[nd + 1];

	raid_neon_begin();

	asm volatile (
		"ldr q29, %0\n"
		:
		: "m" (gfconst16.poly[0])
	);

	for (i = 0; i < size; i += 32) {
		asm volatile (
			"ldr q0, %0\n"
			"ldr q1, %1\n"
			"mov v2.16b, v0.16b\n"
			"mov v3.16b, v1.16b\n"
			:
			: "m" (v[l][i]), "m" (v[l][i + 16])
		);

		for (d = l - 1; d >= 0; --d) {
			if (generator == 3) {
				/* v14-v15 are otherwise unused and preserve Q across the AES xtime */
				asm volatile (
					"mov v14.16b, v2.16b\n"
					"mov v15.16b, v3.16b\n"
				);
			}
			asm volatile (
				/* double Q0-Q1 */
				"sshr v16.16b, v2.16b, #7\n"
				"shl v2.16b, v2.16b, #1\n"
				"and v16.16b, v16.16b, v29.16b\n"
				"eor v2.16b, v2.16b, v16.16b\n"

				"sshr v16.16b, v3.16b, #7\n"
				"shl v3.16b, v3.16b, #1\n"
				"and v16.16b, v16.16b, v29.16b\n"
				"eor v3.16b, v3.16b, v16.16b\n"

				/* load and XOR */
				"ldr q12, %0\n"
				"ldr q13, %1\n"
				"eor v0.16b, v0.16b, v12.16b\n"
				"eor v1.16b, v1.16b, v13.16b\n"
				"eor v2.16b, v2.16b, v12.16b\n"
				"eor v3.16b, v3.16b, v13.16b\n"
				:
				: "m" (v[d][i]), "m" (v[d][i + 16])
			);
			if (generator == 3) {
				asm volatile (
					"eor v2.16b, v2.16b, v14.16b\n"
					"eor v3.16b, v3.16b, v15.16b\n"
				);
			}
		}

		asm volatile (
			"str q0, %0\n"
			"str q1, %1\n"
			"str q2, %2\n"
			"str q3, %3\n"
			: "=m" (p[i]), "=m" (p[i + 16]),
			"=m" (q[i]), "=m" (q[i + 16])
		);
	}

	raid_neon_end();
}

/*
 * Generate three parity blocks with powers of 2^-1 using NEON implementation.
 */
void raid_genz_neon_raid(int nd, size_t size, void **vv)
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

	raid_neon_begin();

	asm volatile (
		"ldr q29, %0\n"
		"ldr q30, %1\n"
		:
		: "m" (gfconst16.poly[0]), "m" (gfconst16.half[0])
	);

	for (i = 0; i < size; i += 32) {
		asm volatile (
			"ldr q0, %0\n"
			"ldr q1, %1\n"
			"mov v2.16b, v0.16b\n"
			"mov v3.16b, v1.16b\n"
			"mov v4.16b, v0.16b\n"
			"mov v5.16b, v1.16b\n"
			:
			: "m" (v[l][i]), "m" (v[l][i + 16])
		);

		for (d = l - 1; d >= 0; --d) {
			asm volatile (
				/* double Q0-Q1 */
				"sshr v16.16b, v2.16b, #7\n"
				"shl v2.16b, v2.16b, #1\n"
				"and v16.16b, v16.16b, v29.16b\n"
				"eor v2.16b, v2.16b, v16.16b\n"

				"sshr v16.16b, v3.16b, #7\n"
				"shl v3.16b, v3.16b, #1\n"
				"and v16.16b, v16.16b, v29.16b\n"
				"eor v3.16b, v3.16b, v16.16b\n"

				/* halve R0-R1 */
				"shl v17.16b, v4.16b, #7\n"
				"sshr v17.16b, v17.16b, #7\n"
				"ushr v4.16b, v4.16b, #1\n"
				"and v17.16b, v17.16b, v30.16b\n"
				"eor v4.16b, v4.16b, v17.16b\n"

				"shl v17.16b, v5.16b, #7\n"
				"sshr v17.16b, v17.16b, #7\n"
				"ushr v5.16b, v5.16b, #1\n"
				"and v17.16b, v17.16b, v30.16b\n"
				"eor v5.16b, v5.16b, v17.16b\n"

				/* load and XOR */
				"ldr q12, %0\n"
				"ldr q13, %1\n"
				"eor v0.16b, v0.16b, v12.16b\n"
				"eor v1.16b, v1.16b, v13.16b\n"
				"eor v2.16b, v2.16b, v12.16b\n"
				"eor v3.16b, v3.16b, v13.16b\n"
				"eor v4.16b, v4.16b, v12.16b\n"
				"eor v5.16b, v5.16b, v13.16b\n"
				:
				: "m" (v[d][i]), "m" (v[d][i + 16])
			);
		}

		asm volatile (
			"str q0, %0\n"
			"str q1, %1\n"
			"str q2, %2\n"
			"str q3, %3\n"
			"str q4, %4\n"
			"str q5, %5\n"
			: "=m" (p[i]), "=m" (p[i + 16]),
			"=m" (q[i]), "=m" (q[i + 16]),
			"=m" (r[i]), "=m" (r[i + 16])
		);
	}

	raid_neon_end();
}

/*
 * Generate N parity blocks with Cauchy matrix using NEON implementation.
 */
static __always_inline void raid_genX_neon(int nd, size_t size, void **vv, int np, int generator)
{
	uint8_t **v = (uint8_t **)vv;
	size_t i;
	int d, l;

	l = nd - 1;

	/* special case with only one data disk */
	if (l == 0) {
		for (d = 0; d < np; ++d)
			if (v[1 + d] != v[0])
				memcpy(v[1 + d], v[0], size);
		return;
	}

	raid_neon_begin();

	/* generic case with at least two data disks */
	asm volatile (
		"ldr q29, %0\n"
		"ldr q28, %1\n"
		:
		: "m" (gfconst16.poly[0]), "m" (gfconst16.low4[0])
	);

	for (i = 0; i < size; i += 32) {
		/* last disk without the generator multiplication */
		asm volatile (
			"ldr q0, %0\n"
			"ldr q1, %1\n"
			"mov v2.16b, v0.16b\n"
			"mov v3.16b, v1.16b\n"
			:
			: "m" (v[l][i]), "m" (v[l][i + 16])
		);

		if (np >= 3) {
			asm volatile (
				"ushr v17.16b, v0.16b, #4\n"
				"and v16.16b, v0.16b, v28.16b\n"
				"and v17.16b, v17.16b, v28.16b\n"

				"ushr v19.16b, v1.16b, #4\n"
				"and v18.16b, v1.16b, v28.16b\n"
				"and v19.16b, v19.16b, v28.16b\n"
			);

			asm volatile (
				"ldr q22, %0\n"
				"ldr q23, %1\n"
				"tbl v4.16b, {v22.16b}, v16.16b\n"
				"tbl v20.16b, {v23.16b}, v17.16b\n"
				"eor v4.16b, v4.16b, v20.16b\n"

				"tbl v5.16b, {v22.16b}, v18.16b\n"
				"tbl v20.16b, {v23.16b}, v19.16b\n"
				"eor v5.16b, v5.16b, v20.16b\n"
				:
				: "m" (raid_gfcauchypshufb[l][1][0][0]), "m" (raid_gfcauchypshufb[l][1][1][0])
			);
		}
		if (np >= 4) {
			asm volatile (
				"ldr q22, %0\n"
				"ldr q23, %1\n"
				"tbl v6.16b, {v22.16b}, v16.16b\n"
				"tbl v20.16b, {v23.16b}, v17.16b\n"
				"eor v6.16b, v6.16b, v20.16b\n"

				"tbl v7.16b, {v22.16b}, v18.16b\n"
				"tbl v20.16b, {v23.16b}, v19.16b\n"
				"eor v7.16b, v7.16b, v20.16b\n"
				:
				: "m" (raid_gfcauchypshufb[l][2][0][0]), "m" (raid_gfcauchypshufb[l][2][1][0])
			);
		}
		if (np >= 5) {
			asm volatile (
				"ldr q22, %0\n"
				"ldr q23, %1\n"
				"tbl v8.16b, {v22.16b}, v16.16b\n"
				"tbl v20.16b, {v23.16b}, v17.16b\n"
				"eor v8.16b, v8.16b, v20.16b\n"

				"tbl v9.16b, {v22.16b}, v18.16b\n"
				"tbl v20.16b, {v23.16b}, v19.16b\n"
				"eor v9.16b, v9.16b, v20.16b\n"
				:
				: "m" (raid_gfcauchypshufb[l][3][0][0]), "m" (raid_gfcauchypshufb[l][3][1][0])
			);
		}
		if (np >= 6) {
			asm volatile (
				"ldr q22, %0\n"
				"ldr q23, %1\n"
				"tbl v10.16b, {v22.16b}, v16.16b\n"
				"tbl v20.16b, {v23.16b}, v17.16b\n"
				"eor v10.16b, v10.16b, v20.16b\n"

				"tbl v11.16b, {v22.16b}, v18.16b\n"
				"tbl v20.16b, {v23.16b}, v19.16b\n"
				"eor v11.16b, v11.16b, v20.16b\n"
				:
				: "m" (raid_gfcauchypshufb[l][4][0][0]), "m" (raid_gfcauchypshufb[l][4][1][0])
			);
		}

		/* intermediate disks */
		for (d = l - 1; d > 0; --d) {
			if (generator == 3) {
				asm volatile (
					"mov v14.16b, v2.16b\n"
					"mov v15.16b, v3.16b\n"
				);
			}
			asm volatile (
				/* double Q0-Q1 */
				"sshr v20.16b, v2.16b, #7\n"
				"shl v2.16b, v2.16b, #1\n"
				"and v20.16b, v20.16b, v29.16b\n"
				"eor v2.16b, v2.16b, v20.16b\n"

				"sshr v20.16b, v3.16b, #7\n"
				"shl v3.16b, v3.16b, #1\n"
				"and v20.16b, v20.16b, v29.16b\n"
				"eor v3.16b, v3.16b, v20.16b\n"

				/* load data */
				"ldr q12, %0\n"
				"ldr q13, %1\n"
				"eor v0.16b, v0.16b, v12.16b\n"
				"eor v1.16b, v1.16b, v13.16b\n"
				"eor v2.16b, v2.16b, v12.16b\n"
				"eor v3.16b, v3.16b, v13.16b\n"
				:
				: "m" (v[d][i]), "m" (v[d][i + 16])
			);
			if (generator == 3) {
				asm volatile (
					"eor v2.16b, v2.16b, v14.16b\n"
					"eor v3.16b, v3.16b, v15.16b\n"
				);
			}

			if (np >= 3) {
				asm volatile (
					"ushr v17.16b, v12.16b, #4\n"
					"and v16.16b, v12.16b, v28.16b\n"
					"and v17.16b, v17.16b, v28.16b\n"

					"ushr v19.16b, v13.16b, #4\n"
					"and v18.16b, v13.16b, v28.16b\n"
					"and v19.16b, v19.16b, v28.16b\n"
				);

				asm volatile (
					"ldr q22, %0\n"
					"ldr q23, %1\n"
					"tbl v20.16b, {v22.16b}, v16.16b\n"
					"tbl v21.16b, {v23.16b}, v17.16b\n"
					"eor v20.16b, v20.16b, v21.16b\n"
					"eor v4.16b, v4.16b, v20.16b\n"

					"tbl v20.16b, {v22.16b}, v18.16b\n"
					"tbl v21.16b, {v23.16b}, v19.16b\n"
					"eor v20.16b, v20.16b, v21.16b\n"
					"eor v5.16b, v5.16b, v20.16b\n"
					:
					: "m" (raid_gfcauchypshufb[d][1][0][0]), "m" (raid_gfcauchypshufb[d][1][1][0])
				);
			}
			if (np >= 4) {
				asm volatile (
					"ldr q22, %0\n"
					"ldr q23, %1\n"
					"tbl v20.16b, {v22.16b}, v16.16b\n"
					"tbl v21.16b, {v23.16b}, v17.16b\n"
					"eor v20.16b, v20.16b, v21.16b\n"
					"eor v6.16b, v6.16b, v20.16b\n"

					"tbl v20.16b, {v22.16b}, v18.16b\n"
					"tbl v21.16b, {v23.16b}, v19.16b\n"
					"eor v20.16b, v20.16b, v21.16b\n"
					"eor v7.16b, v7.16b, v20.16b\n"
					:
					: "m" (raid_gfcauchypshufb[d][2][0][0]), "m" (raid_gfcauchypshufb[d][2][1][0])
				);
			}
			if (np >= 5) {
				asm volatile (
					"ldr q22, %0\n"
					"ldr q23, %1\n"
					"tbl v20.16b, {v22.16b}, v16.16b\n"
					"tbl v21.16b, {v23.16b}, v17.16b\n"
					"eor v20.16b, v20.16b, v21.16b\n"
					"eor v8.16b, v8.16b, v20.16b\n"

					"tbl v20.16b, {v22.16b}, v18.16b\n"
					"tbl v21.16b, {v23.16b}, v19.16b\n"
					"eor v20.16b, v20.16b, v21.16b\n"
					"eor v9.16b, v9.16b, v20.16b\n"
					:
					: "m" (raid_gfcauchypshufb[d][3][0][0]), "m" (raid_gfcauchypshufb[d][3][1][0])
				);
			}
			if (np >= 6) {
				asm volatile (
					"ldr q22, %0\n"
					"ldr q23, %1\n"
					"tbl v20.16b, {v22.16b}, v16.16b\n"
					"tbl v21.16b, {v23.16b}, v17.16b\n"
					"eor v20.16b, v20.16b, v21.16b\n"
					"eor v10.16b, v10.16b, v20.16b\n"

					"tbl v20.16b, {v22.16b}, v18.16b\n"
					"tbl v21.16b, {v23.16b}, v19.16b\n"
					"eor v20.16b, v20.16b, v21.16b\n"
					"eor v11.16b, v11.16b, v20.16b\n"
					:
					: "m" (raid_gfcauchypshufb[d][4][0][0]), "m" (raid_gfcauchypshufb[d][4][1][0])
				);
			}
		}

		/* first disk with all coefficients at 1 */
		if (generator == 3) {
			asm volatile (
				"mov v14.16b, v2.16b\n"
				"mov v15.16b, v3.16b\n"
			);
		}
		asm volatile (
			/* double Q0-Q1 */
			"sshr v20.16b, v2.16b, #7\n"
			"shl v2.16b, v2.16b, #1\n"
			"and v20.16b, v20.16b, v29.16b\n"
			"eor v2.16b, v2.16b, v20.16b\n"

			"sshr v20.16b, v3.16b, #7\n"
			"shl v3.16b, v3.16b, #1\n"
			"and v20.16b, v20.16b, v29.16b\n"
			"eor v3.16b, v3.16b, v20.16b\n"

			/* load disk 0 data and XOR */
			"ldr q12, %0\n"
			"ldr q13, %1\n"
			"eor v0.16b, v0.16b, v12.16b\n"
			"eor v1.16b, v1.16b, v13.16b\n"
			"eor v2.16b, v2.16b, v12.16b\n"
			"eor v3.16b, v3.16b, v13.16b\n"
			:
			: "m" (v[0][i]), "m" (v[0][i + 16])
		);
		if (generator == 3) {
			asm volatile (
				"eor v2.16b, v2.16b, v14.16b\n"
				"eor v3.16b, v3.16b, v15.16b\n"
			);
		}

		if (np >= 3) {
			asm volatile (
				"eor v4.16b, v4.16b, v12.16b\n"
				"eor v5.16b, v5.16b, v13.16b\n"
			);
		}
		if (np >= 4) {
			asm volatile (
				"eor v6.16b, v6.16b, v12.16b\n"
				"eor v7.16b, v7.16b, v13.16b\n"
			);
		}
		if (np >= 5) {
			asm volatile (
				"eor v8.16b, v8.16b, v12.16b\n"
				"eor v9.16b, v9.16b, v13.16b\n"
			);
		}
		if (np >= 6) {
			asm volatile (
				"eor v10.16b, v10.16b, v12.16b\n"
				"eor v11.16b, v11.16b, v13.16b\n"
			);
		}

		asm volatile (
			"str q0, %0\n"
			"str q1, %1\n"
			"str q2, %2\n"
			"str q3, %3\n"
			: "=m" (v[nd][i]), "=m" (v[nd][i + 16]),
			"=m" (v[nd + 1][i]), "=m" (v[nd + 1][i + 16])
		);

		if (np >= 3) {
			asm volatile (
				"str q4, %0\n"
				"str q5, %1\n"
				: "=m" (v[nd + 2][i]), "=m" (v[nd + 2][i + 16])
			);
		}
		if (np >= 4) {
			asm volatile (
				"str q6, %0\n"
				"str q7, %1\n"
				: "=m" (v[nd + 3][i]), "=m" (v[nd + 3][i + 16])
			);
		}
		if (np >= 5) {
			asm volatile (
				"str q8, %0\n"
				"str q9, %1\n"
				: "=m" (v[nd + 4][i]), "=m" (v[nd + 4][i + 16])
			);
		}
		if (np >= 6) {
			asm volatile (
				"str q10, %0\n"
				"str q11, %1\n"
				: "=m" (v[nd + 5][i]), "=m" (v[nd + 5][i + 16])
			);
		}
	}

	raid_neon_end();
}

/*
 * Recover multiple data failures using selected parity blocks with NEON.
 *
 * This avoids raid_delta_gen(), temporary syndrome buffers, and the
 * generation of unused parity rows.
 *
 * If P is available, preserve the complete P delta syndrome and
 * reconstruct only nr - 1 missing blocks through the inverse matrix.
 * The last missing block is obtained by XORing the reconstructed blocks
 * out of Pdelta.
 */
static __always_inline void raid_recX_neon(int nr, int *id, int *ip, int nd, size_t size, void **vv)
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

	/* ip[] is ordered. If P is available, it is always ip[0] */
	has_p = ip[0] == 0;

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

	/* precompute inverse-matrix multiplication table pointers */
	for (j = 0; j < nr; ++j)
		for (k = 0; k < nr; ++k)
			R[j][k] = &raid_gfmulpshufb[V[j * nr + k]][0][0];

	raid_neon_begin();

	for (i = 0; i < size; i += 32) {
		/*
		 * V31 is reused during reconstruction, so reload the nibble mask
		 * at the beginning of every iteration.
		 */
		asm volatile ("ldr q31, %0" : : "m" (gfconst16.low4[0]));

		/* start all selected syndromes from the stored parity */
		asm volatile ("ldr q0, %0" : : "m" (p[0][i]));
		asm volatile ("ldr q1, %0" : : "m" (p[0][i + 16]));

		if (nr >= 2) {
			asm volatile ("ldr q2, %0" : : "m" (p[1][i]));
			asm volatile ("ldr q3, %0" : : "m" (p[1][i + 16]));
		}

		if (nr >= 3) {
			asm volatile ("ldr q4, %0" : : "m" (p[2][i]));
			asm volatile ("ldr q5, %0" : : "m" (p[2][i + 16]));
		}

		if (nr >= 4) {
			asm volatile ("ldr q6, %0" : : "m" (p[3][i]));
			asm volatile ("ldr q7, %0" : : "m" (p[3][i + 16]));
		}

		if (nr >= 5) {
			asm volatile ("ldr q8, %0" : : "m" (p[4][i]));
			asm volatile ("ldr q9, %0" : : "m" (p[4][i + 16]));
		}

		if (nr >= 6) {
			asm volatile ("ldr q10, %0" : : "m" (p[5][i]));
			asm volatile ("ldr q11, %0" : : "m" (p[5][i + 16]));
		}

		/*
		 * Add all surviving data contributions.
		 * All source reads for this chunk occur before any destination
		 * write.
		 */
		for (s = 0; s < ns; ++s) {
			const uint8_t **t = S[s];

			asm volatile ("ldr q12, %0" : : "m" (src[s][i]));
			asm volatile ("ldr q13, %0" : : "m" (src[s][i + 16]));

			/*
			 * P has coefficient 1. Use the original source before
			 * destructively splitting it into nibbles.
			 */
			if (has_p) {
				asm volatile ("eor v0.16b, v0.16b, v12.16b");
				asm volatile ("eor v1.16b, v1.16b, v13.16b");

				/* split the source lanes */
				asm volatile ("ushr v14.16b, v12.16b, #4");
				asm volatile ("ushr v15.16b, v13.16b, #4");
				asm volatile ("and v12.16b, v12.16b, v31.16b");
				asm volatile ("and v13.16b, v13.16b, v31.16b");
				asm volatile ("and v14.16b, v14.16b, v31.16b");
				asm volatile ("and v15.16b, v15.16b, v31.16b");
			} else {
				/* split the source lanes */
				asm volatile ("ushr v14.16b, v12.16b, #4");
				asm volatile ("ushr v15.16b, v13.16b, #4");
				asm volatile ("and v12.16b, v12.16b, v31.16b");
				asm volatile ("and v13.16b, v13.16b, v31.16b");
				asm volatile ("and v14.16b, v14.16b, v31.16b");
				asm volatile ("and v15.16b, v15.16b, v31.16b");

				/* syndrome 0 */
				asm volatile ("ldr q24, %0" : : "m" (t[0][0]));
				asm volatile ("ldr q25, %0" : : "m" (t[0][16]));

				asm volatile ("tbl v26.16b, {v24.16b}, v12.16b");
				asm volatile ("tbl v27.16b, {v25.16b}, v14.16b");
				asm volatile ("eor v26.16b, v26.16b, v27.16b");
				asm volatile ("eor v0.16b, v0.16b, v26.16b");

				asm volatile ("tbl v26.16b, {v24.16b}, v13.16b");
				asm volatile ("tbl v27.16b, {v25.16b}, v15.16b");
				asm volatile ("eor v26.16b, v26.16b, v27.16b");
				asm volatile ("eor v1.16b, v1.16b, v26.16b");
			}

			/* syndrome 1 */
			if (nr >= 2) {
				asm volatile ("ldr q24, %0" : : "m" (t[1][0]));
				asm volatile ("ldr q25, %0" : : "m" (t[1][16]));

				asm volatile ("tbl v26.16b, {v24.16b}, v12.16b");
				asm volatile ("tbl v27.16b, {v25.16b}, v14.16b");
				asm volatile ("eor v26.16b, v26.16b, v27.16b");
				asm volatile ("eor v2.16b, v2.16b, v26.16b");

				asm volatile ("tbl v26.16b, {v24.16b}, v13.16b");
				asm volatile ("tbl v27.16b, {v25.16b}, v15.16b");
				asm volatile ("eor v26.16b, v26.16b, v27.16b");
				asm volatile ("eor v3.16b, v3.16b, v26.16b");
			}

			/* syndrome 2 */
			if (nr >= 3) {
				asm volatile ("ldr q24, %0" : : "m" (t[2][0]));
				asm volatile ("ldr q25, %0" : : "m" (t[2][16]));

				asm volatile ("tbl v26.16b, {v24.16b}, v12.16b");
				asm volatile ("tbl v27.16b, {v25.16b}, v14.16b");
				asm volatile ("eor v26.16b, v26.16b, v27.16b");
				asm volatile ("eor v4.16b, v4.16b, v26.16b");

				asm volatile ("tbl v26.16b, {v24.16b}, v13.16b");
				asm volatile ("tbl v27.16b, {v25.16b}, v15.16b");
				asm volatile ("eor v26.16b, v26.16b, v27.16b");
				asm volatile ("eor v5.16b, v5.16b, v26.16b");
			}

			/* syndrome 3 */
			if (nr >= 4) {
				asm volatile ("ldr q24, %0" : : "m" (t[3][0]));
				asm volatile ("ldr q25, %0" : : "m" (t[3][16]));

				asm volatile ("tbl v26.16b, {v24.16b}, v12.16b");
				asm volatile ("tbl v27.16b, {v25.16b}, v14.16b");
				asm volatile ("eor v26.16b, v26.16b, v27.16b");
				asm volatile ("eor v6.16b, v6.16b, v26.16b");

				asm volatile ("tbl v26.16b, {v24.16b}, v13.16b");
				asm volatile ("tbl v27.16b, {v25.16b}, v15.16b");
				asm volatile ("eor v26.16b, v26.16b, v27.16b");
				asm volatile ("eor v7.16b, v7.16b, v26.16b");
			}

			/* syndrome 4 */
			if (nr >= 5) {
				asm volatile ("ldr q24, %0" : : "m" (t[4][0]));
				asm volatile ("ldr q25, %0" : : "m" (t[4][16]));

				asm volatile ("tbl v26.16b, {v24.16b}, v12.16b");
				asm volatile ("tbl v27.16b, {v25.16b}, v14.16b");
				asm volatile ("eor v26.16b, v26.16b, v27.16b");
				asm volatile ("eor v8.16b, v8.16b, v26.16b");

				asm volatile ("tbl v26.16b, {v24.16b}, v13.16b");
				asm volatile ("tbl v27.16b, {v25.16b}, v15.16b");
				asm volatile ("eor v26.16b, v26.16b, v27.16b");
				asm volatile ("eor v9.16b, v9.16b, v26.16b");
			}

			/* syndrome 5 */
			if (nr >= 6) {
				asm volatile ("ldr q24, %0" : : "m" (t[5][0]));
				asm volatile ("ldr q25, %0" : : "m" (t[5][16]));

				asm volatile ("tbl v26.16b, {v24.16b}, v12.16b");
				asm volatile ("tbl v27.16b, {v25.16b}, v14.16b");
				asm volatile ("eor v26.16b, v26.16b, v27.16b");
				asm volatile ("eor v10.16b, v10.16b, v26.16b");

				asm volatile ("tbl v26.16b, {v24.16b}, v13.16b");
				asm volatile ("tbl v27.16b, {v25.16b}, v15.16b");
				asm volatile ("eor v26.16b, v26.16b, v27.16b");
				asm volatile ("eor v11.16b, v11.16b, v26.16b");
			}
		}

		/*
		 * Expand raw syndromes backwards into:
		 *
		 *   S0 = v0..v3
		 *   S1 = v4..v7
		 *   S2 = v8..v11
		 *   S3 = v12..v15
		 *   S4 = v16..v19
		 *   S5 = v20..v23
		 */

		if (nr >= 6) {
			asm volatile ("ushr v21.16b, v10.16b, #4");
			asm volatile ("and v20.16b, v10.16b, v31.16b");
			asm volatile ("and v21.16b, v21.16b, v31.16b");
			asm volatile ("ushr v23.16b, v11.16b, #4");
			asm volatile ("and v22.16b, v11.16b, v31.16b");
			asm volatile ("and v23.16b, v23.16b, v31.16b");
		}

		if (nr >= 5) {
			asm volatile ("ushr v17.16b, v8.16b, #4");
			asm volatile ("and v16.16b, v8.16b, v31.16b");
			asm volatile ("and v17.16b, v17.16b, v31.16b");
			asm volatile ("ushr v19.16b, v9.16b, #4");
			asm volatile ("and v18.16b, v9.16b, v31.16b");
			asm volatile ("and v19.16b, v19.16b, v31.16b");
		}

		if (nr >= 4) {
			asm volatile ("ushr v13.16b, v6.16b, #4");
			asm volatile ("and v12.16b, v6.16b, v31.16b");
			asm volatile ("and v13.16b, v13.16b, v31.16b");
			asm volatile ("ushr v15.16b, v7.16b, #4");
			asm volatile ("and v14.16b, v7.16b, v31.16b");
			asm volatile ("and v15.16b, v15.16b, v31.16b");
		}

		if (nr >= 3) {
			asm volatile ("ushr v9.16b, v4.16b, #4");
			asm volatile ("and v8.16b, v4.16b, v31.16b");
			asm volatile ("and v9.16b, v9.16b, v31.16b");
			asm volatile ("ushr v11.16b, v5.16b, #4");
			asm volatile ("and v10.16b, v5.16b, v31.16b");
			asm volatile ("and v11.16b, v11.16b, v31.16b");
		}

		if (nr >= 2) {
			asm volatile ("ushr v5.16b, v2.16b, #4");
			asm volatile ("and v4.16b, v2.16b, v31.16b");
			asm volatile ("and v5.16b, v5.16b, v31.16b");
			asm volatile ("ushr v7.16b, v3.16b, #4");
			asm volatile ("and v6.16b, v3.16b, v31.16b");
			asm volatile ("and v7.16b, v7.16b, v31.16b");
		}

		/*
		 * S0 overlaps its original raw lane registers.
		 * Process raw lane 1 before overwriting v1.
		 */
		asm volatile ("ushr v3.16b, v1.16b, #4");
		asm volatile ("and v2.16b, v1.16b, v31.16b");
		asm volatile ("and v3.16b, v3.16b, v31.16b");
		asm volatile ("ushr v1.16b, v0.16b, #4");
		asm volatile ("and v0.16b, v0.16b, v31.16b");
		asm volatile ("and v1.16b, v1.16b, v31.16b");

		/*
		 * Reconstruct all missing blocks.
		 *
		 * v24/v25 = low/high tables
		 * v26/v27 = lane-0 low/high accumulators
		 * v28/v29 = lane-1 low/high accumulators
		 * v30/v31 = low/high multiplication temporaries
		 */
		for (j = 0; j < nr; ++j) {
			const uint8_t **t = R[j];

			/* coefficient 0 initializes both lane accumulators */
			asm volatile ("ldr q24, %0" : : "m" (t[0][0]));
			asm volatile ("ldr q25, %0" : : "m" (t[0][16]));
			asm volatile ("tbl v26.16b, {v24.16b}, v0.16b");
			asm volatile ("tbl v27.16b, {v25.16b}, v1.16b");
			asm volatile ("tbl v28.16b, {v24.16b}, v2.16b");
			asm volatile ("tbl v29.16b, {v25.16b}, v3.16b");

			if (nr >= 2) {
				asm volatile ("ldr q24, %0" : : "m" (t[1][0]));
				asm volatile ("ldr q25, %0" : : "m" (t[1][16]));

				asm volatile ("tbl v30.16b, {v24.16b}, v4.16b");
				asm volatile ("tbl v31.16b, {v25.16b}, v5.16b");
				asm volatile ("eor v26.16b, v26.16b, v30.16b");
				asm volatile ("eor v27.16b, v27.16b, v31.16b");

				asm volatile ("tbl v30.16b, {v24.16b}, v6.16b");
				asm volatile ("tbl v31.16b, {v25.16b}, v7.16b");
				asm volatile ("eor v28.16b, v28.16b, v30.16b");
				asm volatile ("eor v29.16b, v29.16b, v31.16b");
			}

			if (nr >= 3) {
				asm volatile ("ldr q24, %0" : : "m" (t[2][0]));
				asm volatile ("ldr q25, %0" : : "m" (t[2][16]));

				asm volatile ("tbl v30.16b, {v24.16b}, v8.16b");
				asm volatile ("tbl v31.16b, {v25.16b}, v9.16b");
				asm volatile ("eor v26.16b, v26.16b, v30.16b");
				asm volatile ("eor v27.16b, v27.16b, v31.16b");

				asm volatile ("tbl v30.16b, {v24.16b}, v10.16b");
				asm volatile ("tbl v31.16b, {v25.16b}, v11.16b");
				asm volatile ("eor v28.16b, v28.16b, v30.16b");
				asm volatile ("eor v29.16b, v29.16b, v31.16b");
			}

			if (nr >= 4) {
				asm volatile ("ldr q24, %0" : : "m" (t[3][0]));
				asm volatile ("ldr q25, %0" : : "m" (t[3][16]));

				asm volatile ("tbl v30.16b, {v24.16b}, v12.16b");
				asm volatile ("tbl v31.16b, {v25.16b}, v13.16b");
				asm volatile ("eor v26.16b, v26.16b, v30.16b");
				asm volatile ("eor v27.16b, v27.16b, v31.16b");

				asm volatile ("tbl v30.16b, {v24.16b}, v14.16b");
				asm volatile ("tbl v31.16b, {v25.16b}, v15.16b");
				asm volatile ("eor v28.16b, v28.16b, v30.16b");
				asm volatile ("eor v29.16b, v29.16b, v31.16b");
			}

			if (nr >= 5) {
				asm volatile ("ldr q24, %0" : : "m" (t[4][0]));
				asm volatile ("ldr q25, %0" : : "m" (t[4][16]));

				asm volatile ("tbl v30.16b, {v24.16b}, v16.16b");
				asm volatile ("tbl v31.16b, {v25.16b}, v17.16b");
				asm volatile ("eor v26.16b, v26.16b, v30.16b");
				asm volatile ("eor v27.16b, v27.16b, v31.16b");

				asm volatile ("tbl v30.16b, {v24.16b}, v18.16b");
				asm volatile ("tbl v31.16b, {v25.16b}, v19.16b");
				asm volatile ("eor v28.16b, v28.16b, v30.16b");
				asm volatile ("eor v29.16b, v29.16b, v31.16b");
			}

			if (nr >= 6) {
				asm volatile ("ldr q24, %0" : : "m" (t[5][0]));
				asm volatile ("ldr q25, %0" : : "m" (t[5][16]));

				asm volatile ("tbl v30.16b, {v24.16b}, v20.16b");
				asm volatile ("tbl v31.16b, {v25.16b}, v21.16b");
				asm volatile ("eor v26.16b, v26.16b, v30.16b");
				asm volatile ("eor v27.16b, v27.16b, v31.16b");

				asm volatile ("tbl v30.16b, {v24.16b}, v22.16b");
				asm volatile ("tbl v31.16b, {v25.16b}, v23.16b");
				asm volatile ("eor v28.16b, v28.16b, v30.16b");
				asm volatile ("eor v29.16b, v29.16b, v31.16b");
			}

			asm volatile ("eor v26.16b, v26.16b, v27.16b");
			asm volatile ("eor v28.16b, v28.16b, v29.16b");
			asm volatile ("str q26, %0" : "=m" (pa[j][i]));
			asm volatile ("str q28, %0" : "=m" (pa[j][i + 16]));
		}
	}

	raid_neon_end();
}

void raid_gen2_neon_raid(int nd, size_t size, void **vv)
{
	raid_gen2_neon_gen(nd, size, vv, 2);
}

void raid_gen2_neon_aes(int nd, size_t size, void **vv)
{
	raid_gen2_neon_gen(nd, size, vv, 3);
}

void raid_gen3_neon_raid(int nd, size_t size, void **vv)
{
	raid_genX_neon(nd, size, vv, 3, 2);
}

void raid_gen3_neon_aes(int nd, size_t size, void **vv)
{
	raid_genX_neon(nd, size, vv, 3, 3);
}

void raid_gen4_neon_raid(int nd, size_t size, void **vv)
{
	raid_genX_neon(nd, size, vv, 4, 2);
}

void raid_gen4_neon_aes(int nd, size_t size, void **vv)
{
	raid_genX_neon(nd, size, vv, 4, 3);
}

void raid_gen5_neon_raid(int nd, size_t size, void **vv)
{
	raid_genX_neon(nd, size, vv, 5, 2);
}

void raid_gen5_neon_aes(int nd, size_t size, void **vv)
{
	raid_genX_neon(nd, size, vv, 5, 3);
}

void raid_gen6_neon_raid(int nd, size_t size, void **vv)
{
	raid_genX_neon(nd, size, vv, 6, 2);
}

void raid_gen6_neon_aes(int nd, size_t size, void **vv)
{
	raid_genX_neon(nd, size, vv, 6, 3);
}

void raid_rec1_neon(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 1);

	/* if recovering with P uses the delta function */
	if (ip[0] == 0) {
		raid_rec1of1(id, nd, size, vv);
		return;
	}

	raid_recX_neon(1, id, ip, nd, size, vv);
}

/*
 * Recover failure of two data blocks using P and Q AArch64 NEON implementation.
 *
 * Uses raid_delta_gen() and keeps all four multiplication tables resident
 * in NEON registers (q20..q23) with a 32-byte two-lane unroll.
 */
static __always_inline void raid_rec2of2_neon(int *id, int *ip, int nd, size_t size, void **vv)
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

	raid_neon_begin();

	asm volatile (
		"ldr q28, %0\n"
		"ldr q20, %1\n"
		"ldr q21, %2\n"
		"ldr q22, %3\n"
		"ldr q23, %4\n"
		:
		: "m" (gfconst16.low4[0]),
		"m" (raid_gfmulpshufb[C[0]][0][0]),
		"m" (raid_gfmulpshufb[C[0]][1][0]),
		"m" (raid_gfmulpshufb[C[1]][0][0]),
		"m" (raid_gfmulpshufb[C[1]][1][0])
	);

	for (i = 0; i < size; i += 32) {
		asm volatile (
			/* Pd, two lanes */
			"ldr q0, %4\n"
			"ldr q1, %5\n"
			"ldr q12, %8\n"
			"ldr q13, %9\n"
			"eor v0.16b, v0.16b, v12.16b\n"
			"eor v1.16b, v1.16b, v13.16b\n"

			/* preserve Pd for Dx */
			"mov v8.16b, v0.16b\n"
			"mov v9.16b, v1.16b\n"

			/* Qd, two lanes */
			"ldr q2, %6\n"
			"ldr q3, %7\n"
			"ldr q12, %10\n"
			"ldr q13, %11\n"
			"eor v2.16b, v2.16b, v12.16b\n"
			"eor v3.16b, v3.16b, v13.16b\n"

			/* split Pd */
			"ushr v4.16b, v0.16b, #4\n"
			"ushr v5.16b, v1.16b, #4\n"
			"and v0.16b, v0.16b, v28.16b\n"
			"and v1.16b, v1.16b, v28.16b\n"
			"and v4.16b, v4.16b, v28.16b\n"
			"and v5.16b, v5.16b, v28.16b\n"

			/* split Qd */
			"ushr v6.16b, v2.16b, #4\n"
			"ushr v7.16b, v3.16b, #4\n"
			"and v2.16b, v2.16b, v28.16b\n"
			"and v3.16b, v3.16b, v28.16b\n"
			"and v6.16b, v6.16b, v28.16b\n"
			"and v7.16b, v7.16b, v28.16b\n"

			/* C0 * Pd */
			"tbl v10.16b, {v20.16b}, v0.16b\n"
			"tbl v11.16b, {v20.16b}, v1.16b\n"
			"tbl v12.16b, {v21.16b}, v4.16b\n"
			"tbl v13.16b, {v21.16b}, v5.16b\n"
			"eor v10.16b, v10.16b, v12.16b\n"
			"eor v11.16b, v11.16b, v13.16b\n"

			/* C1 * Qd */
			"tbl v12.16b, {v22.16b}, v2.16b\n"
			"tbl v13.16b, {v22.16b}, v3.16b\n"
			"tbl v14.16b, {v23.16b}, v6.16b\n"
			"tbl v15.16b, {v23.16b}, v7.16b\n"
			"eor v12.16b, v12.16b, v14.16b\n"
			"eor v13.16b, v13.16b, v15.16b\n"

			/* Dy */
			"eor v10.16b, v10.16b, v12.16b\n"
			"eor v11.16b, v11.16b, v13.16b\n"

			/* Dx = Pd ^ Dy */
			"eor v8.16b, v8.16b, v10.16b\n"
			"eor v9.16b, v9.16b, v11.16b\n"

			"str q8, %0\n"
			"str q9, %1\n"
			"str q10, %2\n"
			"str q11, %3\n"
			: "=m" (pa[i]), "=m" (pa[i + 16]),
			"=m" (qa[i]), "=m" (qa[i + 16])
			: "m" (p[i]), "m" (p[i + 16]),
			"m" (q[i]), "m" (q[i + 16]),
			"m" (pa[i]), "m" (pa[i + 16]),
			"m" (qa[i]), "m" (qa[i + 16])
		);
	}

	raid_neon_end();
}

void raid_rec2_neon(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 2);

	if (ip[0] == 0 && ip[1] == 1) {
		raid_rec2of2_neon(id, ip, nd, size, vv);
		return;
	}

	raid_recX_neon(2, id, ip, nd, size, vv);
}

void raid_rec3_neon(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 3);
	raid_recX_neon(3, id, ip, nd, size, vv);
}

void raid_rec4_neon(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 4);
	raid_recX_neon(4, id, ip, nd, size, vv);
}

void raid_rec5_neon(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 5);
	raid_recX_neon(5, id, ip, nd, size, vv);
}

void raid_rec6_neon(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 6);
	raid_recX_neon(6, id, ip, nd, size, vv);
}

void raid_register_neon(void)
{
	raid_gen_register(RAID_ALGO_CAUCHY_PAR1, "neon", raid_gen1_neon, RAID_POLY_ANY);
	raid_gen_register(RAID_ALGO_CAUCHY_PAR2, "neon", raid_gen2_neon_raid, RAID_POLY_RAID);
	raid_gen_register(RAID_ALGO_CAUCHY_PAR2, "neon", raid_gen2_neon_aes, RAID_POLY_AES);
	raid_gen_register(RAID_ALGO_VANDERMONDE_PAR3, "neon", raid_genz_neon_raid, RAID_POLY_RAID);
	raid_gen_register(RAID_ALGO_CAUCHY_PAR3, "neon", raid_gen3_neon_raid, RAID_POLY_RAID);
	raid_gen_register(RAID_ALGO_CAUCHY_PAR3, "neon", raid_gen3_neon_aes, RAID_POLY_AES);
	raid_gen_register(RAID_ALGO_CAUCHY_PAR4, "neon", raid_gen4_neon_raid, RAID_POLY_RAID);
	raid_gen_register(RAID_ALGO_CAUCHY_PAR4, "neon", raid_gen4_neon_aes, RAID_POLY_AES);
	raid_gen_register(RAID_ALGO_CAUCHY_PAR5, "neon", raid_gen5_neon_raid, RAID_POLY_RAID);
	raid_gen_register(RAID_ALGO_CAUCHY_PAR5, "neon", raid_gen5_neon_aes, RAID_POLY_AES);
	raid_gen_register(RAID_ALGO_CAUCHY_PAR6, "neon", raid_gen6_neon_raid, RAID_POLY_RAID);
	raid_gen_register(RAID_ALGO_CAUCHY_PAR6, "neon", raid_gen6_neon_aes, RAID_POLY_AES);

	raid_rec_register(RAID_ALGO_CAUCHY_PAR1, "neon", raid_rec1_neon, RAID_POLY_ANY);
	raid_rec_register(RAID_ALGO_CAUCHY_PAR2, "neon", raid_rec2_neon, RAID_POLY_ANY);
	raid_rec_register(RAID_ALGO_CAUCHY_PAR3, "neon", raid_rec3_neon, RAID_POLY_ANY);
	raid_rec_register(RAID_ALGO_CAUCHY_PAR4, "neon", raid_rec4_neon, RAID_POLY_ANY);
	raid_rec_register(RAID_ALGO_CAUCHY_PAR5, "neon", raid_rec5_neon, RAID_POLY_ANY);
	raid_rec_register(RAID_ALGO_CAUCHY_PAR6, "neon", raid_rec6_neon, RAID_POLY_ANY);
}

#endif
