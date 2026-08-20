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
 * GEN2 Cauchy NEON implementation using the active generator
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
				/* v14-v15 are otherwise unused and preserve Q across the AES xtime. */
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

void raid_gen2_neon_raid(int nd, size_t size, void **vv)
{
	raid_gen2_neon_gen(nd, size, vv, 2);
}

void raid_gen2_neon_aes(int nd, size_t size, void **vv)
{
	raid_gen2_neon_gen(nd, size, vv, 3);
}

/*
 * GENz (triple parity with powers of 2^-1) NEON implementation
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
 * GENX NEON implementation
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
 * GEN3 (triple parity with Cauchy matrix) NEON implementation
 */
void raid_gen3_neon_raid(int nd, size_t size, void **vv)
{
	raid_genX_neon(nd, size, vv, 3, 2);
}

void raid_gen3_neon_aes(int nd, size_t size, void **vv)
{
	raid_genX_neon(nd, size, vv, 3, 3);
}

/*
 * GEN4 (quad parity with Cauchy matrix) NEON implementation
 */
void raid_gen4_neon_raid(int nd, size_t size, void **vv)
{
	raid_genX_neon(nd, size, vv, 4, 2);
}

void raid_gen4_neon_aes(int nd, size_t size, void **vv)
{
	raid_genX_neon(nd, size, vv, 4, 3);
}

/*
 * GEN5 (penta parity with Cauchy matrix) NEON implementation
 */
void raid_gen5_neon_raid(int nd, size_t size, void **vv)
{
	raid_genX_neon(nd, size, vv, 5, 2);
}

void raid_gen5_neon_aes(int nd, size_t size, void **vv)
{
	raid_genX_neon(nd, size, vv, 5, 3);
}

/*
 * GEN6 (hexa parity with Cauchy matrix) NEON implementation
 */
void raid_gen6_neon_raid(int nd, size_t size, void **vv)
{
	raid_genX_neon(nd, size, vv, 6, 2);
}

void raid_gen6_neon_aes(int nd, size_t size, void **vv)
{
	raid_genX_neon(nd, size, vv, 6, 3);
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

	/* preload tables */
	asm volatile (
		"ldr q28, %0\n" /* low4 */
		"ldr q24, %1\n" /* v low table */
		"ldr q25, %2\n" /* v high table */
		:
		: "m" (gfconst16.low4[0]),
		"m" (raid_gfmulpshufb[V][0][0]), "m" (raid_gfmulpshufb[V][1][0])
	);

	for (i = 0; i < size; i += 64) {
		asm volatile (
			"ldr q0, %4\n"
			"ldr q1, %5\n"
			"ldr q2, %6\n"
			"ldr q3, %7\n"

			"ldr q8, %8\n"
			"ldr q9, %9\n"
			"ldr q10, %10\n"
			"ldr q11, %11\n"

			"eor v0.16b, v0.16b, v8.16b\n"
			"eor v1.16b, v1.16b, v9.16b\n"
			"eor v2.16b, v2.16b, v10.16b\n"
			"eor v3.16b, v3.16b, v11.16b\n"

			"ushr v8.16b, v0.16b, #4\n"
			"ushr v9.16b, v1.16b, #4\n"
			"ushr v10.16b, v2.16b, #4\n"
			"ushr v11.16b, v3.16b, #4\n"

			"and v0.16b, v0.16b, v28.16b\n"
			"and v1.16b, v1.16b, v28.16b\n"
			"and v2.16b, v2.16b, v28.16b\n"
			"and v3.16b, v3.16b, v28.16b\n"

			"and v8.16b, v8.16b, v28.16b\n"
			"and v9.16b, v9.16b, v28.16b\n"
			"and v10.16b, v10.16b, v28.16b\n"
			"and v11.16b, v11.16b, v28.16b\n"

			"tbl v12.16b, {v24.16b}, v0.16b\n"
			"tbl v13.16b, {v24.16b}, v1.16b\n"
			"tbl v14.16b, {v24.16b}, v2.16b\n"
			"tbl v15.16b, {v24.16b}, v3.16b\n"

			"tbl v8.16b, {v25.16b}, v8.16b\n"
			"tbl v9.16b, {v25.16b}, v9.16b\n"
			"tbl v10.16b, {v25.16b}, v10.16b\n"
			"tbl v11.16b, {v25.16b}, v11.16b\n"

			"eor v0.16b, v12.16b, v8.16b\n"
			"eor v1.16b, v13.16b, v9.16b\n"
			"eor v2.16b, v14.16b, v10.16b\n"
			"eor v3.16b, v15.16b, v11.16b\n"

			"str q0, %0\n"
			"str q1, %1\n"
			"str q2, %2\n"
			"str q3, %3\n"
			: "=m" (pa[i]), "=m" (pa[i + 16]), "=m" (pa[i + 32]), "=m" (pa[i + 48])
			: "m" (p[i]), "m" (p[i + 16]), "m" (p[i + 32]), "m" (p[i + 48]),
			"m" (pa[i]), "m" (pa[i + 16]), "m" (pa[i + 32]), "m" (pa[i + 48])
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

	raid_neon_begin();

	/* preload tables */
	asm volatile (
		"ldr q28, %0\n" /* low4 */
		"ldr q20, %1\n" /* v[0] low table */
		"ldr q21, %2\n" /* v[0] high table */
		"ldr q22, %3\n" /* v[1] low table */
		"ldr q23, %4\n" /* v[1] high table */
		"ldr q24, %5\n" /* v[2] low table */
		"ldr q25, %6\n" /* v[2] high table */
		"ldr q26, %7\n" /* v[3] low table */
		"ldr q27, %8\n" /* v[3] high table */
		:
		: "m" (gfconst16.low4[0]),
		"m" (raid_gfmulpshufb[V[0]][0][0]), "m" (raid_gfmulpshufb[V[0]][1][0]),
		"m" (raid_gfmulpshufb[V[1]][0][0]), "m" (raid_gfmulpshufb[V[1]][1][0]),
		"m" (raid_gfmulpshufb[V[2]][0][0]), "m" (raid_gfmulpshufb[V[2]][1][0]),
		"m" (raid_gfmulpshufb[V[3]][0][0]), "m" (raid_gfmulpshufb[V[3]][1][0])
	);

	for (i = 0; i < size; i += 16) {
		asm volatile (
			/* load delta 0 */
			"ldr q0, %2\n"
			"ldr q2, %3\n"
			"eor v0.16b, v0.16b, v2.16b\n"

			/* load delta 1 */
			"ldr q1, %4\n"
			"ldr q3, %5\n"
			"eor v1.16b, v1.16b, v3.16b\n"

			/* split delta 0 */
			"ushr v17.16b, v0.16b, #4\n"
			"and v16.16b, v0.16b, v28.16b\n"
			"and v17.16b, v17.16b, v28.16b\n"

			/* split delta 1 */
			"ushr v19.16b, v1.16b, #4\n"
			"and v18.16b, v1.16b, v28.16b\n"
			"and v19.16b, v19.16b, v28.16b\n"

			/* reconstruct pa[0] */
			"tbl v4.16b, {v20.16b}, v16.16b\n"
			"tbl v5.16b, {v21.16b}, v17.16b\n"
			"eor v4.16b, v4.16b, v5.16b\n" /* v4 has delta0 * V[0] */
			"tbl v6.16b, {v22.16b}, v18.16b\n"
			"tbl v7.16b, {v23.16b}, v19.16b\n"
			"eor v6.16b, v6.16b, v7.16b\n" /* v6 has delta1 * V[1] */
			"eor v4.16b, v4.16b, v6.16b\n"
			"str q4, %0\n"

			/* reconstruct pa[1] */
			"tbl v4.16b, {v24.16b}, v16.16b\n"
			"tbl v5.16b, {v25.16b}, v17.16b\n"
			"eor v4.16b, v4.16b, v5.16b\n" /* v4 has delta0 * V[2] */
			"tbl v6.16b, {v26.16b}, v18.16b\n"
			"tbl v7.16b, {v27.16b}, v19.16b\n"
			"eor v6.16b, v6.16b, v7.16b\n" /* v6 has delta1 * V[3] */
			"eor v4.16b, v4.16b, v6.16b\n"
			"str q4, %1\n"
			: "=m" (pa[0][i]), "=m" (pa[1][i])
			: "m" (p[0][i]), "m" (pa[0][i]),
			"m" (p[1][i]), "m" (pa[1][i])
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

	/* preload mask */
	asm volatile (
		"ldr q28, %0\n" /* low4 */
		:
		: "m" (gfconst16.low4[0])
	);

	for (i = 0; i < size; i += 16) {
		/* delta */
		asm volatile (
			"ldr x4, [%2, #0]\n"
			"ldr x5, [%3, #0]\n"
			"ldr q0, [x4, %1]\n"
			"ldr q14, [x5, %1]\n"
			"eor v0.16b, v0.16b, v14.16b\n"
			"ushr v1.16b, v0.16b, #4\n"
			"and v0.16b, v0.16b, v28.16b\n"
			"and v1.16b, v1.16b, v28.16b\n"
			"cmp %0, #1\n"
			"b.ls 1f\n"

			"ldr x4, [%2, #8]\n"
			"ldr x5, [%3, #8]\n"
			"ldr q2, [x4, %1]\n"
			"ldr q14, [x5, %1]\n"
			"eor v2.16b, v2.16b, v14.16b\n"
			"ushr v3.16b, v2.16b, #4\n"
			"and v2.16b, v2.16b, v28.16b\n"
			"and v3.16b, v3.16b, v28.16b\n"
			"cmp %0, #2\n"
			"b.ls 1f\n"

			"ldr x4, [%2, #16]\n"
			"ldr x5, [%3, #16]\n"
			"ldr q4, [x4, %1]\n"
			"ldr q14, [x5, %1]\n"
			"eor v4.16b, v4.16b, v14.16b\n"
			"ushr v5.16b, v4.16b, #4\n"
			"and v4.16b, v4.16b, v28.16b\n"
			"and v5.16b, v5.16b, v28.16b\n"
			"cmp %0, #3\n"
			"b.ls 1f\n"

			"ldr x4, [%2, #24]\n"
			"ldr x5, [%3, #24]\n"
			"ldr q6, [x4, %1]\n"
			"ldr q14, [x5, %1]\n"
			"eor v6.16b, v6.16b, v14.16b\n"
			"ushr v7.16b, v6.16b, #4\n"
			"and v6.16b, v6.16b, v28.16b\n"
			"and v7.16b, v7.16b, v28.16b\n"
			"cmp %0, #4\n"
			"b.ls 1f\n"

			"ldr x4, [%2, #32]\n"
			"ldr x5, [%3, #32]\n"
			"ldr q8, [x4, %1]\n"
			"ldr q14, [x5, %1]\n"
			"eor v8.16b, v8.16b, v14.16b\n"
			"ushr v9.16b, v8.16b, #4\n"
			"and v8.16b, v8.16b, v28.16b\n"
			"and v9.16b, v9.16b, v28.16b\n"
			"cmp %0, #5\n"
			"b.ls 1f\n"

			"ldr x4, [%2, #40]\n"
			"ldr x5, [%3, #40]\n"
			"ldr q10, [x4, %1]\n"
			"ldr q14, [x5, %1]\n"
			"eor v10.16b, v10.16b, v14.16b\n"
			"ushr v11.16b, v10.16b, #4\n"
			"and v10.16b, v10.16b, v28.16b\n"
			"and v11.16b, v11.16b, v28.16b\n"

			"1:\n"
			:
			: "r" ((uint64_t)N), "r" (i), "r" (p), "r" (pa)
			: "x4", "x5", "cc", "memory"
		);

		/* reconstruct */
		for (j = 0; j < N; ++j) {
			asm volatile (
				"ldrb w4, [%2, #0]\n"
				"add x5, %4, x4, lsl #5\n"
				"ldp q24, q25, [x5]\n"
				"tbl v12.16b, {v24.16b}, v0.16b\n"
				"tbl v13.16b, {v25.16b}, v1.16b\n"
				"cmp %0, #1\n"
				"b.ls 1f\n"

				"ldrb w4, [%2, #1]\n"
				"add x5, %4, x4, lsl #5\n"
				"ldp q24, q25, [x5]\n"
				"tbl v14.16b, {v24.16b}, v2.16b\n"
				"tbl v15.16b, {v25.16b}, v3.16b\n"
				"eor v12.16b, v12.16b, v14.16b\n"
				"eor v13.16b, v13.16b, v15.16b\n"
				"cmp %0, #2\n"
				"b.ls 1f\n"

				"ldrb w4, [%2, #2]\n"
				"add x5, %4, x4, lsl #5\n"
				"ldp q24, q25, [x5]\n"
				"tbl v14.16b, {v24.16b}, v4.16b\n"
				"tbl v15.16b, {v25.16b}, v5.16b\n"
				"eor v12.16b, v12.16b, v14.16b\n"
				"eor v13.16b, v13.16b, v15.16b\n"
				"cmp %0, #3\n"
				"b.ls 1f\n"

				"ldrb w4, [%2, #3]\n"
				"add x5, %4, x4, lsl #5\n"
				"ldp q24, q25, [x5]\n"
				"tbl v14.16b, {v24.16b}, v6.16b\n"
				"tbl v15.16b, {v25.16b}, v7.16b\n"
				"eor v12.16b, v12.16b, v14.16b\n"
				"eor v13.16b, v13.16b, v15.16b\n"
				"cmp %0, #4\n"
				"b.ls 1f\n"

				"ldrb w4, [%2, #4]\n"
				"add x5, %4, x4, lsl #5\n"
				"ldp q24, q25, [x5]\n"
				"tbl v14.16b, {v24.16b}, v8.16b\n"
				"tbl v15.16b, {v25.16b}, v9.16b\n"
				"eor v12.16b, v12.16b, v14.16b\n"
				"eor v13.16b, v13.16b, v15.16b\n"
				"cmp %0, #5\n"
				"b.ls 1f\n"

				"ldrb w4, [%2, #5]\n"
				"add x5, %4, x4, lsl #5\n"
				"ldp q24, q25, [x5]\n"
				"tbl v14.16b, {v24.16b}, v10.16b\n"
				"tbl v15.16b, {v25.16b}, v11.16b\n"
				"eor v12.16b, v12.16b, v14.16b\n"
				"eor v13.16b, v13.16b, v15.16b\n"

				"1:\n"
				"eor v12.16b, v12.16b, v13.16b\n"
				"str q12, [%3, %1]\n"
				:
				: "r" ((uint64_t)N), "r" (i), "r" (&V[j * N]), "r" (pa[j]), "r" (raid_gfmulpshufb)
				: "x4", "x5", "cc", "memory"
			);
		}
	}

	raid_neon_end();
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
	raid_rec_register(RAID_ALGO_CAUCHY_PAR3, "neon", raid_recX_neon, RAID_POLY_ANY);
	raid_rec_register(RAID_ALGO_CAUCHY_PAR4, "neon", raid_recX_neon, RAID_POLY_ANY);
	raid_rec_register(RAID_ALGO_CAUCHY_PAR5, "neon", raid_recX_neon, RAID_POLY_ANY);
	raid_rec_register(RAID_ALGO_CAUCHY_PAR6, "neon", raid_recX_neon, RAID_POLY_ANY);
}
#endif
