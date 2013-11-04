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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>

/**
 * Multiplication in GF(2^8).
 */
unsigned char gfmul(unsigned char a, unsigned char b)
{
	unsigned char v;

	v = 0;
	while (b)  {
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
 * Inversion table in GF(2^8).
 */
unsigned char gfinv[256];

/**
 * Number of parity.
 * This is the number of rows of the generation matrix.
 */
#define PARITY 6

/**
 * Number of disks.
 * This is the number of columns of the generation matrix.
 */
#define DISK (257-PARITY)

/**
 * Setup the Cauchy matrix used to generate the parity.
 */
void set_cauchy(unsigned char* matrix)
{
	int i, j;
	unsigned char inv_x, y;

	/*
	 * First row is formed by all 1.
	 *
	 * This is an Extended Cauchy matrix built from a Cauchy matrix
	 * adding the first row of all 1.
	 */
	for(i=0;i<DISK;++i) {
		matrix[0*DISK+i] = 1;
	}

	/*
	 * Second row is formed by power of 2^i.
	 *
	 * This is the first row of the Cauchy matrix.
	 *
	 * Each element of the Cauchy matrix is in the form 1/(xi+yj)
	 * where all xi, and yj must be different.
	 *
	 * Choosing xi = 2^-i and y0 = 0, we obtain for the first row:
	 *
	 * 1/(xi+y0) = 1/(2^-i + 0) = 2^i
	 *
	 * with 2^-i != 0 for any i
	 */
	inv_x = 1;
	for(i=0;i<DISK;++i) {
		matrix[1*DISK+i] = inv_x;
		inv_x = gfmul(2, inv_x);
	}

	/*
	 * Next rows of the Cauchy matrix.
	 *
	 * Continue forming the Cauchy matrix with yj = 2^j obtaining :
	 *
	 * 1/(xi+yj) = 1/(2^-i + 2^j)
	 *
	 * with xi != yj for any i,j with i>=0,j>=1,i+j<256
	 */
	y = 2;
	for(j=0;j<PARITY-2;++j) {
		inv_x = 1;
		for(i=0;i<DISK;++i) {
			unsigned char x = gfinv[inv_x];
			matrix[(j+2)*DISK+i] = gfinv[y ^ x];
			inv_x = gfmul(2, inv_x);
		}

		y = gfmul(2, y);
	}

	/*
	 * Adjusts the matrix multipling each row for
	 * the inverse of the first element in the row.
	 *
	 * This operation doesn't invalidate the property that all the square
	 * submatrices are not singular.
	 */
	for(j=0;j<PARITY-2;++j) {
		unsigned char f = gfinv[matrix[(j+2)*DISK]];

		for(i=0;i<DISK;++i) {
			matrix[(j+2)*DISK+i] = gfmul(matrix[(j+2)*DISK+i], f);
		}
	}
}

/**
 * Setup the Power matrix used to generate the parity.
 */
void set_power(unsigned char* matrix)
{
	unsigned i;
	unsigned char v;

	v = 1;
	for(i=0;i<DISK;++i) {
		matrix[0*DISK+i] = v;
	}

	v = 1;
	for(i=0;i<DISK;++i) {
		matrix[1*DISK+i] = v;
		v = gfmul(2, v);
	}

	v = 1;
	for(i=0;i<DISK;++i) {
		matrix[2*DISK+i] = v;
		v = gfmul(4, v);
	}
}

/**
 * Next power of 2.
 */
unsigned np(unsigned v)
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
	unsigned char v;
	int i, j, k, p;
	unsigned char matrix[PARITY * 256];

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
	printf(" *\n");
	printf(" * You should have received a copy of the GNU General Public License\n");
	printf(" * along with this program.  If not, see <http://www.gnu.org/licenses/>.\n");
	printf(" */\n");
	printf("\n");

	/* a*b */
	printf("const unsigned char  __attribute__((aligned(256))) gfmul[256][256] =\n");
	printf("{\n");
	for(i=0;i<256;++i) {
		printf("\t{\n");
		for(j=0;j<256;++j) {
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
	printf("const unsigned char __attribute__((aligned(256))) gfexp2[256] =\n");
	printf("{\n");
	v = 1;
	for(i=0;i<256;++i) {
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
	printf("const unsigned char __attribute__((aligned(256))) gfinv[256] =\n");
	printf("{\n");
	printf("\t/* note that the first element is not significative */\n");
	for(i=0;i<256;++i) {
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
	for(p=0;p<3;++p) {
		printf(" * ");
		for(i=0;i<DISK;++i) {
			printf("%02x ", matrix[p*DISK+i]);
		}
		printf("\n");
	}
	printf(" */\n");
	printf("const unsigned char  __attribute__((aligned(256))) gfpower[%u][256] =\n", 3);
	printf("{\n");
	for(p=0;p<3;++p) {
		printf("\t{\n");
		for(i=0;i<DISK;++i) {
			if (i % 8 == 0)
				printf("\t\t");
			printf("0x%02x,", matrix[p*DISK+i]);
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
	for(p=0;p<PARITY;++p) {
		printf(" * ");
		for(i=0;i<DISK;++i) {
			printf("%02x ", matrix[p*DISK+i]);
		}
		printf("\n");
	}
	printf(" */\n");
	printf("const unsigned char  __attribute__((aligned(256))) gfgen[%u][256] =\n", PARITY);
	printf("{\n");
	for(p=0;p<PARITY;++p) {
		printf("\t{\n");
		for(i=0;i<DISK;++i) {
			if (i % 8 == 0)
				printf("\t\t");
			printf("0x%02x,", matrix[p*DISK+i]);
			if (i % 8 == 7)
				printf("\n");
			else
				printf(" ");
		}
		printf("\n\t},\n");
	}
	printf("};\n\n");

	printf("#if defined(__i386__) || defined(__x86_64__)\n");
	printf("/**\n");
	printf(" * PSHUFB tables for the Cauchy matrix.\n");
	printf(" *\n");
	printf(" * Indexes are [DISK][PARITY - 2][LH].\n");
	printf(" * Where DISK is from 0 to %u, PARITY from 2 to %u, LH from 0 to 1.\n", DISK - 1, PARITY - 1);
	printf(" */\n");
	printf("const unsigned char  __attribute__((aligned(256))) gfgenpshufb[%u][%u][2][16] =\n", DISK, np(PARITY - 2));
	printf("{\n");
	for(i=0;i<DISK;++i) {
		printf("\t{\n");
		for(p=2;p<PARITY;++p) {
			printf("\t\t{\n");
			for(j=0;j<2;++j) {
				printf("\t\t\t{ ");
				for(k=0;k<16;++k) {
					v = gfmul(matrix[p*DISK+i], k);
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

	printf("#if defined(__i386__) || defined(__x86_64__)\n");
	printf("/**\n");
	printf(" * PSHUFB tables for generic multiplication.\n");
	printf(" *\n");
	printf(" * Indexes are [MULTIPLER][LH].\n");
	printf(" * Where MULTIPLER is from 0 to 255, LH from 0 to 1.\n");
	printf(" */\n");
	printf("const unsigned char  __attribute__((aligned(256))) gfmulpshufb[256][2][16] =\n");
	printf("{\n");
	for(i=0;i<256;++i) {
		printf("\t{\n");
		for(j=0;j<2;++j) {
			printf("\t\t{ ");
			for(k=0;k<16;++k) {
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

