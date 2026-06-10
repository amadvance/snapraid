// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Andrea Mazzoleni

#include "internal.h"
#include "gf.h"
#include "cpu.h"

#ifdef CONFIG_NEON

/*
 * GEN1 (RAID5 with xor) NEON implementation
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
 * GEN2 (RAID6 with powers of 2) NEON implementation
 */
void raid_gen2_neon(int nd, size_t size, void **vv)
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
 * GENz (triple parity with powers of 2^-1) NEON implementation
 */
void raid_genz_neon(int nd, size_t size, void **vv)
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
 * GENX NEON implementation
 */
static __always_inline void raid_genX_neon(int nd, size_t size, void **vv, int np)
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

	raid_neon_begin();

	/* generic case with at least two data disks */
	asm volatile (
		"ldr q29, %0\n"
		"ldr q28, %1\n"
		:
		: "m" (gfconst16.poly[0]), "m" (gfconst16.low4[0])
	);

	for (i = 0; i < size; i += 32) {
		/* last disk without the multiplication by two */
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
				: "m" (gfgenpshufb[l][1][0][0]), "m" (gfgenpshufb[l][1][1][0])
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
				: "m" (gfgenpshufb[l][2][0][0]), "m" (gfgenpshufb[l][2][1][0])
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
				: "m" (gfgenpshufb[l][3][0][0]), "m" (gfgenpshufb[l][3][1][0])
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
				: "m" (gfgenpshufb[l][4][0][0]), "m" (gfgenpshufb[l][4][1][0])
			);
		}

		/* intermediate disks */
		for (d = l - 1; d > 0; --d) {
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
					: "m" (gfgenpshufb[d][1][0][0]), "m" (gfgenpshufb[d][1][1][0])
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
					: "m" (gfgenpshufb[d][2][0][0]), "m" (gfgenpshufb[d][2][1][0])
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
					: "m" (gfgenpshufb[d][3][0][0]), "m" (gfgenpshufb[d][3][1][0])
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
					: "m" (gfgenpshufb[d][4][0][0]), "m" (gfgenpshufb[d][4][1][0])
				);
			}
		}

		/* first disk with all coefficients at 1 */
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
 * GEN3 (triple parity with Cauchy matrix) NEON implementation
 */
void raid_gen3_neon(int nd, size_t size, void **vv)
{
	raid_genX_neon(nd, size, vv, 3);
}

/*
 * GEN4 (quad parity with Cauchy matrix) NEON implementation
 */
void raid_gen4_neon(int nd, size_t size, void **vv)
{
	raid_genX_neon(nd, size, vv, 4);
}

/*
 * GEN5 (penta parity with Cauchy matrix) NEON implementation
 */
void raid_gen5_neon(int nd, size_t size, void **vv)
{
	raid_genX_neon(nd, size, vv, 5);
}

/*
 * GEN6 (hexa parity with Cauchy matrix) NEON implementation
 */
void raid_gen6_neon(int nd, size_t size, void **vv)
{
	raid_genX_neon(nd, size, vv, 6);
}

/*
 * RAID recovering for one disk NEON implementation
 */
void raid_rec1_neon(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *pa;
	uint8_t G;
	uint8_t V;
	size_t i;

	(void)nr; /* unused, it's always 1 */

	/* if it's RAID5 uses the faster function */
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

	raid_neon_begin();

	for (i = 0; i < size; i += 16) {
		asm volatile (
			"ldr q28, %3\n" /* low4 */
			"ldr q24, %4\n" /* v low table */
			"ldr q25, %5\n" /* v high table */
			"ldr q0, %1\n"
			"ldr q1, %2\n"
			"eor v0.16b, v0.16b, v1.16b\n"
			"ushr v17.16b, v0.16b, #4\n"
			"and v16.16b, v0.16b, v28.16b\n"
			"and v17.16b, v17.16b, v28.16b\n"
			"tbl v2.16b, {v24.16b}, v16.16b\n"
			"tbl v3.16b, {v25.16b}, v17.16b\n"
			"eor v2.16b, v2.16b, v3.16b\n"
			"str q2, %0\n"
			: "=m" (pa[i])
			: "m" (p[i]), "m" (pa[i]), "m" (gfconst16.low4[0]),
			  "m" (gfmulpshufb[V][0][0]), "m" (gfmulpshufb[V][1][0])
		);
	}

	raid_neon_end();
}

/*
 * RAID recovering for two disks NEON implementation
 */
void raid_rec2_neon(int nr, int *id, int *ip, int nd, size_t size, void **vv)
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

	raid_neon_begin();

	for (i = 0; i < size; i += 16) {
		asm volatile (
			"ldr q28, %2\n" /* low4 */

			/* load delta 0 */
			"ldr q0, %3\n"
			"ldr q2, %4\n"
			"eor v0.16b, v0.16b, v2.16b\n"

			/* load delta 1 */
			"ldr q1, %5\n"
			"ldr q3, %6\n"
			"eor v1.16b, v1.16b, v3.16b\n"

			/* reconstruct pa[0] */
			"ldr q24, %7\n" /* v[0] low table */
			"ldr q25, %8\n" /* v[0] high table */
			"ushr v17.16b, v0.16b, #4\n"
			"and v16.16b, v0.16b, v28.16b\n"
			"and v17.16b, v17.16b, v28.16b\n"
			"tbl v4.16b, {v24.16b}, v16.16b\n"
			"tbl v5.16b, {v25.16b}, v17.16b\n"
			"eor v4.16b, v4.16b, v5.16b\n" /* v4 has delta0 * V[0] */

			"ldr q24, %9\n" /* v[1] low table */
			"ldr q25, %10\n" /* v[1] high table */
			"ushr v17.16b, v1.16b, #4\n"
			"and v16.16b, v1.16b, v28.16b\n"
			"and v17.16b, v17.16b, v28.16b\n"
			"tbl v6.16b, {v24.16b}, v16.16b\n"
			"tbl v7.16b, {v25.16b}, v17.16b\n"
			"eor v6.16b, v6.16b, v7.16b\n" /* v6 has delta1 * V[1] */

			"eor v4.16b, v4.16b, v6.16b\n"
			"str q4, %0\n"

			/* reconstruct pa[1] */
			"ldr q24, %11\n" /* v[2] low table */
			"ldr q25, %12\n" /* v[2] high table */
			"ushr v17.16b, v0.16b, #4\n"
			"and v16.16b, v0.16b, v28.16b\n"
			"and v17.16b, v17.16b, v28.16b\n"
			"tbl v4.16b, {v24.16b}, v16.16b\n"
			"tbl v5.16b, {v25.16b}, v17.16b\n"
			"eor v4.16b, v4.16b, v5.16b\n" /* v4 has delta0 * V[2] */

			"ldr q24, %13\n" /* v[3] low table */
			"ldr q25, %14\n" /* v[3] high table */
			"ushr v17.16b, v1.16b, #4\n"
			"and v16.16b, v1.16b, v28.16b\n"
			"and v17.16b, v17.16b, v28.16b\n"
			"tbl v6.16b, {v24.16b}, v16.16b\n"
			"tbl v7.16b, {v25.16b}, v17.16b\n"
			"eor v6.16b, v6.16b, v7.16b\n" /* v6 has delta1 * V[3] */

			"eor v4.16b, v4.16b, v6.16b\n"
			"str q4, %1\n"
			: "=m" (pa[0][i]), "=m" (pa[1][i])
			: "m" (gfconst16.low4[0]),
			  "m" (p[0][i]), "m" (pa[0][i]),
			  "m" (p[1][i]), "m" (pa[1][i]),
			  "m" (gfmulpshufb[V[0]][0][0]), "m" (gfmulpshufb[V[0]][1][0]),
			  "m" (gfmulpshufb[V[1]][0][0]), "m" (gfmulpshufb[V[1]][1][0]),
			  "m" (gfmulpshufb[V[2]][0][0]), "m" (gfmulpshufb[V[2]][1][0]),
			  "m" (gfmulpshufb[V[3]][0][0]), "m" (gfmulpshufb[V[3]][1][0])
		);
	}

	raid_neon_end();
}

/*
 * RAID recovering NEON implementation
 */
void raid_recX_neon(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	int N = nr;
	uint8_t *p[RAID_PARITY_MAX];
	uint8_t *pa[RAID_PARITY_MAX];
	uint8_t G[RAID_PARITY_MAX * RAID_PARITY_MAX];
	uint8_t V[RAID_PARITY_MAX * RAID_PARITY_MAX];
	uint8_t buffer[RAID_PARITY_MAX*16+16];
	uint8_t *pd = __align_ptr(buffer, 16);
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

	raid_neon_begin();

	for (i = 0; i < size; i += 16) {
		/* delta */
		for (j = 0; j < N; ++j) {
			asm volatile (
				"ldr q0, %1\n"
				"ldr q1, %2\n"
				"eor v0.16b, v0.16b, v1.16b\n"
				"str q0, %0\n"
				: "=m" (pd[j*16])
				: "m" (p[j][i]), "m" (pa[j][i])
			);
		}

		/* reconstruct */
		for (j = 0; j < N; ++j) {
			asm volatile (
				"eor v0.16b, v0.16b, v0.16b\n"
			);

			for (k = 0; k < N; ++k) {
				uint8_t m = V[j * N + k];
				asm volatile (
					"ldr q28, %1\n" /* low4 */
					"ldr q24, %2\n"
					"ldr q25, %3\n"
					"ldr q4, %0\n"
					"ushr v17.16b, v4.16b, #4\n"
					"and v16.16b, v4.16b, v28.16b\n"
					"and v17.16b, v17.16b, v28.16b\n"
					"tbl v2.16b, {v24.16b}, v16.16b\n"
					"tbl v3.16b, {v25.16b}, v17.16b\n"
					"eor v2.16b, v2.16b, v3.16b\n"
					"eor v0.16b, v0.16b, v2.16b\n"
					:
					: "m" (pd[k*16]), "m" (gfconst16.low4[0]),
					  "m" (gfmulpshufb[m][0][0]), "m" (gfmulpshufb[m][1][0])
				);
			}

			asm volatile (
				"str q0, %0\n"
				: "=m" (pa[j][i])
			);
		}
	}

	raid_neon_end();
}

void raid_register_neon(void)
{
	raid_gen_register(RAID_ALGO_CAUCHY_PAR1, "neon", raid_gen1_neon);
	raid_gen_register(RAID_ALGO_CAUCHY_PAR2, "neon", raid_gen2_neon);
	raid_gen_register(RAID_ALGO_VANDERMONDE_PAR3, "neon", raid_genz_neon);
	raid_gen_register(RAID_ALGO_CAUCHY_PAR3, "neon", raid_gen3_neon);
	raid_gen_register(RAID_ALGO_CAUCHY_PAR4, "neon", raid_gen4_neon);
	raid_gen_register(RAID_ALGO_CAUCHY_PAR5, "neon", raid_gen5_neon);
	raid_gen_register(RAID_ALGO_CAUCHY_PAR6, "neon", raid_gen6_neon);

	raid_rec_register(RAID_ALGO_CAUCHY_PAR1, "neon", raid_rec1_neon);
	raid_rec_register(RAID_ALGO_CAUCHY_PAR2, "neon", raid_rec2_neon);
	raid_rec_register(RAID_ALGO_CAUCHY_PAR3, "neon", raid_recX_neon);
	raid_rec_register(RAID_ALGO_CAUCHY_PAR4, "neon", raid_recX_neon);
	raid_rec_register(RAID_ALGO_CAUCHY_PAR5, "neon", raid_recX_neon);
	raid_rec_register(RAID_ALGO_CAUCHY_PAR6, "neon", raid_recX_neon);
}
#endif
