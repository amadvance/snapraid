// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2013 Andrea Mazzoleni

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

/*
 * Galois field reduction polynomials.
 *
 * RAID_POLY_RAID - Standard RAID polynomial 0x1d (x^8 + x^4 + x^3 + x^2 + 1).
 * RAID_POLY_AES  - AES polynomial 0x1b (x^8 + x^4 + x^3 + x + 1) for GFNI acceleration.
 */
#define RAID_POLY_RAID 0x1d
#define RAID_POLY_AES 0x1b

/**
 * Multiplication a*b in GF(2^8).
 */
static uint8_t raid_gfmul(uint8_t poly, uint8_t a, uint8_t b)
{
	uint8_t v;

	v = 0;
	while (b) {
		if ((b & 1) != 0)
			v ^= a;

		if ((a & 0x80) != 0) {
			a <<= 1;
			a ^= poly;
		} else {
			a <<= 1;
		}

		b >>= 1;
	}

	return v;
}

/**
 * Setup the GFNI affine matrix for multiplication by a constant.
 *
 * VGF2P8MULB is fixed to the AES polynomial 0x11b, while RAID parity is
 * defined over GF(2^8) modulo 0x11d. VGF2P8AFFINEQB can implement the
 * multiplication because multiplication by a constant is GF(2)-linear.
 *
 * VGF2P8AFFINEQB stores the row producing output bit j in byte 7-j.
 * Within each row, bit k selects input bit k.
 */
static void set_affine(uint8_t poly, uint8_t c, uint8_t *matrix)
{
	int j, k;

	for (j = 0; j < 8; ++j) {
		uint8_t row = 0;

		for (k = 0; k < 8; ++k) {
			uint8_t v = raid_gfmul(poly, c, (uint8_t)(1U << k));

			if ((v & (1U << j)) != 0)
				row |= (uint8_t)(1U << k);
		}

		matrix[7 - j] = row;
	}
}

/**
 * Inversion (1/a) in GF(2^8).
 */
uint8_t raid_gfinv[256];

/**
 * Number of parities.
 * This is the number of rows of the generator matrix.
 */
#define PARITY 6

/**
 * Number of disks.
 * This is the number of columns of the generator matrix.
 */
#define DISK (257 - PARITY)

/**
 * Setup the RAID/g=2 Cauchy matrix used to generate parity.
 */
static void set_cauchy_raid(uint8_t poly, uint8_t generator, uint8_t *matrix)
{
	int i, j;
	uint8_t inv_x, y;

	/*
	 * The first row of the generator matrix is formed by all 1s.
	 *
	 * The generator matrix is an Extended Cauchy matrix built from
	 * a Cauchy matrix by adding a row at the top of all 1s.
	 *
	 * Extending a Cauchy matrix in this way maintains the MDS property
	 * of the matrix.
	 *
	 * For example, considering a generator matrix of 4x6 we have now:
	 *
	 *   1   1   1   1   1   1
	 *   -   -   -   -   -   -
	 *   -   -   -   -   -   -
	 *   -   -   -   -   -   -
	 */
	for (i = 0; i < DISK; ++i)
		matrix[0 * DISK + i] = 1;

	/*
	 * The second row is formed with powers g^i, and it's the first
	 * row of the Cauchy matrix.
	 *
	 * Each element of the Cauchy matrix is in the form 1/(x_i + y_j)
	 * where all x_i and y_j must be different for any i and j.
	 *
	 * For the first row with j=0, we choose x_i = g^-i and y_0 = 0
	 * and we obtain a first row formed as:
	 *
	 * 1/(x_i + y_0) = 1/(g^-i + 0) = g^i
	 *
	 * with g^-i != 0 for any i
	 *
	 * The numerical examples below use RAID_MODE_CAUCHY_RAID,
	 * with polynomial 0x11d and generator g=2.
	 *
	 * In the example we get:
	 *
	 * x_0 = 1
	 * x_1 = 142
	 * x_2 = 71
	 * x_3 = 173
	 * x_4 = 216
	 * x_5 = 108
	 * y_0 = 0
	 *
	 * with the matrix:
	 *
	 *   1   1   1   1   1   1
	 *   1   2   4   8  16  32
	 *   -   -   -   -   -   -
	 *   -   -   -   -   -   -
	 */
	inv_x = 1;
	for (i = 0; i < DISK; ++i) {
		matrix[1 * DISK + i] = inv_x;
		inv_x = raid_gfmul(poly, generator, inv_x);
	}

	/*
	 * The rest of the Cauchy matrix is formed by choosing for each row j
	 * a new y_j = g^j and reusing the x_i already assigned in the first
	 * row obtaining :
	 *
	 * 1/(x_i + y_j) = 1/(g^-i + g^j)
	 *
	 * with g^-i + g^j != 0, because g is primitive and has
	 * multiplicative order 255, with i + j < 255 for all supported entries.
	 *
	 * In the example we get:
	 *
	 * y_1 = 2
	 * y_2 = 4
	 *
	 * with the matrix:
	 *
	 *   1   1   1   1   1   1
	 *   1   2   4   8  16  32
	 * 244  83  78 183 118  47
	 * 167  39 213  59 153  82
	 */
	y = generator;
	for (j = 0; j < PARITY - 2; ++j) {
		inv_x = 1;
		for (i = 0; i < DISK; ++i) {
			uint8_t x = raid_gfinv[inv_x];

			matrix[(j + 2) * DISK + i] = raid_gfinv[y ^ x];
			inv_x = raid_gfmul(poly, generator, inv_x);
		}

		y = raid_gfmul(poly, generator, y);
	}

	/*
	 * Finally we adjust the matrix multiplying each row by
	 * the inverse of the first element in the row.
	 *
	 * Also this operation maintains the MDS property of the matrix.
	 *
	 * Resulting in:
	 *
	 *   1   1   1   1   1   1
	 *   1   2   4   8  16  32
	 *   1 245 210 196 154 113
	 *   1 187 166 215 199   7
	 */
	for (j = 0; j < PARITY - 2; ++j) {
		uint8_t f = raid_gfinv[matrix[(j + 2) * DISK]];

		for (i = 0; i < DISK; ++i)
			matrix[(j + 2) * DISK + i] = raid_gfmul(poly, matrix[(j + 2) * DISK + i], f);
	}
}

/**
 * Setup the AES Extended Cauchy matrix using the G23 sequence.
 *
 * The data X set consumes the canonical sequence from the beginning,
 * x[i]=inv(q[i]), while the Cauchy Y set starts with zero and consumes the
 * sequence backward from the end, y[j]=inv(q[255-j]) for j>0. For PARITY=n,
 * DISK=257-n; increasing parity removes one X element from the tail and
 * appends that same element to Y, leaving all existing rows unchanged on the
 * remaining data columns. This defines a nested matrix without implementing
 * parity levels above the current limit.
 */
static void set_cauchy_aes(uint8_t *matrix)
{
	uint8_t q[255];
	uint8_t y[PARITY - 1];
	int i, j, k;

	q[0] = 1;
	for (i = 0; i < 254; ++i) {
		uint8_t generator = i == 50 || i == 101 || i == 152 || i == 203 ? 3 : 2;

		q[i + 1] = raid_gfmul(RAID_POLY_AES, generator, q[i]);
	}

	/* The five cosets must enumerate GF(256)* exactly once. */
	for (i = 0; i < 255; ++i) {
		if (q[i] == 0) {
			fprintf(stderr, "Invalid zero in AES G23 sequence at %d\n", i);
			exit(EXIT_FAILURE);
		}
		for (k = i + 1; k < 255; ++k) {
			if (q[i] == q[k]) {
				fprintf(stderr, "Duplicate in AES G23 sequence at %d and %d\n", i, k);
				exit(EXIT_FAILURE);
			}
		}
	}

	for (i = 0; i < DISK; ++i)
		matrix[i] = 1;

	y[0] = 0;
	for (j = 1; j < PARITY - 1; ++j)
		y[j] = raid_gfinv[q[255 - j]];

	for (j = 0; j < PARITY - 1; ++j) {
		for (i = 0; i < DISK; ++i) {
			uint8_t x = raid_gfinv[q[i]];

			matrix[(j + 1) * DISK + i] = raid_gfinv[x ^ y[j]];
		}
	}

	/* Normalize R..U so every coefficient in the first column is 1. */
	for (j = 1; j < PARITY - 1; ++j) {
		uint8_t f = raid_gfinv[matrix[(j + 1) * DISK]];

		for (i = 0; i < DISK; ++i)
			matrix[(j + 1) * DISK + i] = raid_gfmul(RAID_POLY_AES, matrix[(j + 1) * DISK + i], f);
	}
}

/**
 * Setup the Power matrix used to generate the parity.
 */
static void set_power(uint8_t poly, uint8_t *matrix)
{
	unsigned i;
	uint8_t v;
	uint8_t div_by_2 = (((poly) >> 1) | 0x80);

	v = 1;
	for (i = 0; i < DISK; ++i)
		matrix[0 * DISK + i] = v;

	v = 1;
	for (i = 0; i < DISK; ++i) {
		matrix[1 * DISK + i] = v;
		v = raid_gfmul(poly, 2, v);
	}

	v = 1;
	for (i = 0; i < DISK; ++i) {
		matrix[2 * DISK + i] = v;
		v = raid_gfmul(poly, div_by_2, v);
	}
}

void tables(uint8_t poly, uint8_t generator, const char *tag)
{
	uint8_t v;
	int i, j, k, p;
	uint8_t matrix[PARITY * 256];

	/* a*b */
	printf("const uint8_t __aligned(256) raid_gfmul_%s[256][256] =\n", tag);
	printf("{\n");
	for (i = 0; i < 256; ++i) {
		printf("\t{\n");
		for (j = 0; j < 256; ++j) {
			if (j % 8 == 0)
				printf("\t\t");
			v = raid_gfmul(poly, i, j);
			if (v == 1)
				raid_gfinv[i] = j;
			printf("0x%02x,", (unsigned)v);
			if (j % 8 == 7)
				printf("\n");
			else
				printf(" ");
		}
		printf("\t},\n");
	}
	printf("};\n\n");

	if (poly == RAID_POLY_RAID) {
		uint8_t affine[8];

		printf("/**\n");
		printf(" * GFNI affine matrices for multiplication in GF(2^8)/0x11d.\n");
		printf(" *\n");
		printf(" * VGF2P8AFFINEQB consumes one 8x8 matrix per 64-bit lane.\n");
		printf(" * Byte 7-j contains the row producing output bit j, and bit k\n");
		printf(" * of that byte selects input bit k. The instruction uses imm8=0.\n");
		printf(" */\n");
		printf("const uint8_t __aligned(256) raid_gfaffine_raid[256][8] =\n");
		printf("{\n");
		for (i = 0; i < 256; ++i) {
			set_affine(poly, (uint8_t)i, affine);
			printf("\t{ ");
			for (j = 0; j < 8; ++j) {
				printf("0x%02x", (unsigned)affine[j]);
				if (j != 7)
					printf(", ");
			}
			printf(" },\n");
		}
		printf("};\n\n");
	}

	/* 1/a */
	printf("const uint8_t __aligned(256) raid_gfinv_%s[256] =\n", tag);
	printf("{\n");
	printf("\t/* note that the first element is not significant */\n");
	for (i = 0; i < 256; ++i) {
		if (i % 8 == 0)
			printf("\t");
		if (i == 0)
			v = 0;
		else
			v = raid_gfinv[i];
		printf("0x%02x,", v);
		if (i % 8 == 7)
			printf("\n");
		else
			printf(" ");
	}
	printf("};\n\n");

	/* cauchy matrix */
	if (poly == RAID_POLY_AES)
		set_cauchy_aes(matrix);
	else
		set_cauchy_raid(poly, generator, matrix);

	printf("/**\n");
	printf(" * Cauchy matrix used to generate parity.\n");
	printf(" * This matrix is valid for up to %u parity with %u data disks.\n", PARITY, DISK);
	printf(" *\n");
	for (p = 0; p < PARITY; ++p) {
		printf(" *");
		for (i = 0; i < DISK; ++i)
			printf(" %02x", matrix[p * DISK + i]);
		printf("\n");
	}
	printf(" */\n");
	printf("const uint8_t __aligned(256) raid_gfcauchy_%s[%u][256] =\n", tag, PARITY);
	printf("{\n");
	for (p = 0; p < PARITY; ++p) {
		printf("\t{\n");
		for (i = 0; i < DISK; ++i) {
			if (i % 8 == 0)
				printf("\t\t");
			printf("0x%02x,", matrix[p * DISK + i]);
			if (i != DISK - 1) {
				if (i % 8 == 7)
					printf("\n");
				else
					printf(" ");
			}
		}
		printf("\n\t},\n");
	}
	printf("};\n\n");

	printf("#if defined(CONFIG_X86) || defined(CONFIG_NEON) || defined(CONFIG_NEON32)\n");
	printf("/**\n");
	printf(" * PSHUFB tables for the Cauchy matrix.\n");
	printf(" *\n");
	printf(" * Indexes are [DISK][PARITY - 2][LH].\n");
	printf(" * Where DISK is from 0 to %u, PARITY from 1 to %u, LH from 0 to 1.\n", DISK - 1, PARITY);
	printf(" */\n");
	printf("const uint8_t __aligned(256) raid_gfcauchypshufb_%s[%u][%u][2][16] =\n", tag, DISK, PARITY - 1);
	printf("{\n");
	for (i = 0; i < DISK; ++i) {
		printf("\t{\n");
		for (p = 1; p < PARITY; ++p) {
			printf("\t\t{\n");
			for (j = 0; j < 2; ++j) {
				printf("\t\t\t{ ");
				for (k = 0; k < 16; ++k) {
					v = raid_gfmul(poly, matrix[p * DISK + i], k);
					if (j == 1)
						v = raid_gfmul(poly, v, 16);
					printf("0x%02x", (unsigned)v);
					if (k != 15)
						printf(", ");
				}
				printf(" },\n");
			}
			printf("\t\t},\n");
		}
		printf("\t},\n");
	}
	printf("};\n");
	printf("#endif\n\n");

	printf("#if defined(CONFIG_X86) || defined(CONFIG_NEON) || defined(CONFIG_NEON32)\n");
	printf("/**\n");
	printf(" * PSHUFB tables for generic multiplication.\n");
	printf(" *\n");
	printf(" * Indexes are [MULTIPLIER][LH].\n");
	printf(" * Where MULTIPLIER is from 0 to 255, LH from 0 to 1.\n");
	printf(" */\n");
	printf("const uint8_t __aligned(256) raid_gfmulpshufb_%s[256][2][16] =\n", tag);
	printf("{\n");
	for (i = 0; i < 256; ++i) {
		printf("\t{\n");
		for (j = 0; j < 2; ++j) {
			printf("\t\t{ ");
			for (k = 0; k < 16; ++k) {
				v = raid_gfmul(poly, i, k);
				if (j == 1)
					v = raid_gfmul(poly, v, 16);
				printf("0x%02x", (unsigned)v);
				if (k != 15)
					printf(", ");
			}
			printf(" },\n");
		}
		printf("\t},\n");
	}
	printf("};\n");
	printf("#endif\n");

	if (poly == RAID_POLY_AES)
		return;
	printf("\n");

	/* power matrix */
	set_power(poly, matrix);

	printf("/**\n");
	printf(" * Power matrix used to generate parity.\n");
	printf(" * This matrix is valid for up to %u parities with %u data disks.\n", 3, DISK);
	printf(" *\n");
	for (p = 0; p < 3; ++p) {
		printf(" *");
		for (i = 0; i < DISK; ++i)
			printf(" %02x", matrix[p * DISK + i]);
		printf("\n");
	}
	printf(" */\n");
	printf("const uint8_t __aligned(256) raid_gfvandermonde_%s[%u][256] =\n", tag, 3);
	printf("{\n");
	for (p = 0; p < 3; ++p) {
		printf("\t{\n");
		for (i = 0; i < DISK; ++i) {
			if (i % 8 == 0)
				printf("\t\t");
			printf("0x%02x,", matrix[p * DISK + i]);
			if (i != DISK - 1) {
				if (i % 8 == 7)
					printf("\n");
				else
					printf(" ");
			}
		}
		printf("\n\t},\n");
	}
	printf("};\n\n");
}

int main(void)
{
	printf("// SPDX-License-Identifier: GPL-2.0-or-later\n");
	printf("// Copyright (C) 2013 Andrea Mazzoleni\n");
	printf("\n");

	printf("#include \"internal.h\"\n");
	printf("#include \"gf.h\"\n");
	printf("\n");

	printf("/* Tables with the RAID Polynomial 0x1d */\n");
	tables(RAID_POLY_RAID, 2, "raid");

	printf("/* Tables with the AES Polynomial 0x1b */\n");
	tables(RAID_POLY_AES, 3, "aes");

	return 0;
}
