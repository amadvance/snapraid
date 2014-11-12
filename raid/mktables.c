/*
 * Copyright (C) 2013 Andrea Mazzoleni
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <stdio.h>
#include <stdint.h>

/**
 * Multiplication a*b in GF(2^8).
 */
static uint8_t gfmul(uint8_t a, uint8_t b)
{
	uint8_t v;

	v = 0;
	while (b) {
		if ((b & 1) != 0)
			v ^= a;

		if ((a & 0x80) != 0) {
			a <<= 1;
			a ^= 0x1d;
		} else {
			a <<= 1;
		}

		b >>= 1;
	}

	return v;
}

/**
 * Inversion (1/a) in GF(2^8).
 */
uint8_t gfinv[256];

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
 * Setup the Cauchy matrix used to generate the parity.
 */
static void set_cauchy(uint8_t *matrix)
{
	int i, j;
	uint8_t inv_x, y;

	/*
	 * The first row of the generator matrix is formed by all 1.
	 *
	 * The generator matrix is an Extended Cauchy matrix built from
	 * a Cauchy matrix adding at the top a row of all 1.
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
	 * Second row is formed with powers 2^i, and it's the first
	 * row of the Cauchy matrix.
	 *
	 * Each element of the Cauchy matrix is in the form 1/(x_i + y_j)
	 * where all x_i and y_j must be different for any i and j.
	 *
	 * For the first row with j=0, we choose x_i = 2^-i and y_0 = 0
	 * and we obtain a first row formed as:
	 *
	 * 1/(x_i + y_0) = 1/(2^-i + 0) = 2^i
	 *
	 * with 2^-i != 0 for any i
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
		inv_x = gfmul(2, inv_x);
	}

	/*
	 * The rest of the Cauchy matrix is formed choosing for each row j
	 * a new y_j = 2^j and reusing the x_i already assigned in the first
	 * row obtaining :
	 *
	 * 1/(x_i + y_j) = 1/(2^-i + 2^j)
	 *
	 * with 2^-i + 2^j != 0 for any i,j with i>=0,j>=1,i+j<255
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
	y = 2;
	for (j = 0; j < PARITY - 2; ++j) {
		inv_x = 1;
		for (i = 0; i < DISK; ++i) {
			uint8_t x = gfinv[inv_x];
			matrix[(j + 2) * DISK + i] = gfinv[y ^ x];
			inv_x = gfmul(2, inv_x);
		}

		y = gfmul(2, y);
	}

	/*
	 * Finally we adjust the matrix multipling each row for
	 * the inverse of the first element in the row.
	 *
	 * Also this operation maintains the MDS property of the matrix.
	 *
	 * Resulting in:
	 *
	 *   1   1   1   1   1   1
	 *   1   2   4   8  16  32
	 *   1 245 210 196 154 113
	 *   1 187 166 215   7 106
	 */
	for (j = 0; j < PARITY - 2; ++j) {
		uint8_t f = gfinv[matrix[(j + 2) * DISK]];

		for (i = 0; i < DISK; ++i)
			matrix[(j + 2) * DISK + i] = gfmul(matrix[(j + 2) * DISK + i], f);
	}
}

/**
 * Setup the Power matrix used to generate the parity.
 */
static void set_power(uint8_t *matrix)
{
	unsigned i;
	uint8_t v;

	v = 1;
	for (i = 0; i < DISK; ++i)
		matrix[0 * DISK + i] = v;

	v = 1;
	for (i = 0; i < DISK; ++i) {
		matrix[1 * DISK + i] = v;
		v = gfmul(2, v);
	}

	v = 1;
	for (i = 0; i < DISK; ++i) {
		matrix[2 * DISK + i] = v;
		v = gfmul(0x8e, v);
	}
}

/**
 * Next power of 2.
 */
static unsigned np(unsigned v)
{
	--v;
	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v |= v >> 16;
	++v;

	return v;
}

int main(void)
{
	uint8_t v;
	int i, j, k, p;
	uint8_t matrix[PARITY * 256];

	printf("/*\n");
	printf(" * Copyright (C) 2013 Andrea Mazzoleni\n");
	printf(" *\n");
	printf(" * This program is free software: you can redistribute it and/or modify\n");
	printf(" * it under the terms of the GNU General Public License as published by\n");
	printf(" * the Free Software Foundation, either version 2 of the License, or\n");
	printf(" * (at your option) any later version.\n");
	printf(" *\n");
	printf(" * This program is distributed in the hope that it will be useful,\n");
	printf(" * but WITHOUT ANY WARRANTY; without even the implied warranty of\n");
	printf(" * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n");
	printf(" * GNU General Public License for more details.\n");
	printf(" */\n");
	printf("\n");

	printf("#include \"internal.h\"\n");
	printf("\n");

	/* a*b */
	printf("const uint8_t __aligned(256) raid_gfmul[256][256] =\n");
	printf("{\n");
	for (i = 0; i < 256; ++i) {
		printf("\t{\n");
		for (j = 0; j < 256; ++j) {
			if (j % 8 == 0)
				printf("\t\t");
			v = gfmul(i, j);
			if (v == 1)
				gfinv[i] = j;
			printf("0x%02x,", (unsigned)v);
			if (j % 8 == 7)
				printf("\n");
			else
				printf(" ");
		}
		printf("\t},\n");
	}
	printf("};\n\n");

	/* 2^a */
	printf("const uint8_t __aligned(256) raid_gfexp[256] =\n");
	printf("{\n");
	v = 1;
	for (i = 0; i < 256; ++i) {
		if (i % 8 == 0)
			printf("\t");
		printf("0x%02x,", v);
		v = gfmul(v, 2);
		if (i % 8 == 7)
			printf("\n");
		else
			printf(" ");
	}
	printf("};\n\n");

	/* 1/a */
	printf("const uint8_t __aligned(256) raid_gfinv[256] =\n");
	printf("{\n");
	printf("\t/* note that the first element is not significative */\n");
	for (i = 0; i < 256; ++i) {
		if (i % 8 == 0)
			printf("\t");
		if (i == 0)
			v = 0;
		else
			v = gfinv[i];
		printf("0x%02x,", v);
		if (i % 8 == 7)
			printf("\n");
		else
			printf(" ");
	}
	printf("};\n\n");

	/* power matrix */
	set_power(matrix);

	printf("/**\n");
	printf(" * Power matrix used to generate parity.\n");
	printf(" * This matrix is valid for up to %u parity with %u data disks.\n", 3, DISK);
	printf(" *\n");
	for (p = 0; p < 3; ++p) {
		printf(" * ");
		for (i = 0; i < DISK; ++i)
			printf("%02x ", matrix[p * DISK + i]);
		printf("\n");
	}
	printf(" */\n");
	printf("const uint8_t __aligned(256) raid_gfvandermonde[%u][256] =\n", 3);
	printf("{\n");
	for (p = 0; p < 3; ++p) {
		printf("\t{\n");
		for (i = 0; i < DISK; ++i) {
			if (i % 8 == 0)
				printf("\t\t");
			printf("0x%02x,", matrix[p * DISK + i]);
			if (i % 8 == 7)
				printf("\n");
			else
				printf(" ");
		}
		printf("\n\t},\n");
	}
	printf("};\n\n");

	/* cauchy matrix */
	set_cauchy(matrix);

	printf("/**\n");
	printf(" * Cauchy matrix used to generate parity.\n");
	printf(" * This matrix is valid for up to %u parity with %u data disks.\n", PARITY, DISK);
	printf(" *\n");
	for (p = 0; p < PARITY; ++p) {
		printf(" * ");
		for (i = 0; i < DISK; ++i)
			printf("%02x ", matrix[p * DISK + i]);
		printf("\n");
	}
	printf(" */\n");
	printf("const uint8_t __aligned(256) raid_gfcauchy[%u][256] =\n", PARITY);
	printf("{\n");
	for (p = 0; p < PARITY; ++p) {
		printf("\t{\n");
		for (i = 0; i < DISK; ++i) {
			if (i % 8 == 0)
				printf("\t\t");
			printf("0x%02x,", matrix[p * DISK + i]);
			if (i % 8 == 7)
				printf("\n");
			else
				printf(" ");
		}
		printf("\n\t},\n");
	}
	printf("};\n\n");

	printf("#ifdef CONFIG_X86\n");
	printf("/**\n");
	printf(" * PSHUFB tables for the Cauchy matrix.\n");
	printf(" *\n");
	printf(" * Indexes are [DISK][PARITY - 2][LH].\n");
	printf(" * Where DISK is from 0 to %u, PARITY from 2 to %u, LH from 0 to 1.\n", DISK - 1, PARITY - 1);
	printf(" */\n");
	printf("const uint8_t __aligned(256) raid_gfcauchypshufb[%u][%u][2][16] =\n", DISK, np(PARITY - 2));
	printf("{\n");
	for (i = 0; i < DISK; ++i) {
		printf("\t{\n");
		for (p = 2; p < PARITY; ++p) {
			printf("\t\t{\n");
			for (j = 0; j < 2; ++j) {
				printf("\t\t\t{ ");
				for (k = 0; k < 16; ++k) {
					v = gfmul(matrix[p * DISK + i], k);
					if (j == 1)
						v = gfmul(v, 16);
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

	printf("#ifdef CONFIG_X86\n");
	printf("/**\n");
	printf(" * PSHUFB tables for generic multiplication.\n");
	printf(" *\n");
	printf(" * Indexes are [MULTIPLER][LH].\n");
	printf(" * Where MULTIPLER is from 0 to 255, LH from 0 to 1.\n");
	printf(" */\n");
	printf("const uint8_t __aligned(256) raid_gfmulpshufb[256][2][16] =\n");
	printf("{\n");
	for (i = 0; i < 256; ++i) {
		printf("\t{\n");
		for (j = 0; j < 2; ++j) {
			printf("\t\t{ ");
			for (k = 0; k < 16; ++k) {
				v = gfmul(i, k);
				if (j == 1)
					v = gfmul(v, 16);
				printf("0x%02x", (unsigned)v);
				if (k != 15)
					printf(", ");
			}
			printf(" },\n");
		}
		printf("\t},\n");
	}
	printf("};\n");
	printf("#endif\n\n");

	return 0;
}

