/*
 * Copyright (C) 2011 Andrea Mazzoleni
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

unsigned char gfinv[256];

#define POWER_PARITY 4
#define POWER_DISK 255

/**
 * Setup the power matrix used to generate parity.
 * The first row is formed by all 1.
 * The second row contains power of 2, the third power of 4, and so on...
 * Note that this matrix is valid only for up to Quad Parity and for up to 21 data disks.
 */
void set_power(unsigned char* matrix)
{
	int i, j;
	unsigned char g;

	g = 1;
	for(i=0;i<POWER_PARITY;++i) {
		unsigned char v = 1;
		for(j=0;j<POWER_DISK;++j) {
			matrix[i*POWER_DISK+j] = v;
			v = gfmul(g, v);
		}
		g = gfmul(2, g);
	}
}

#define CAUCHY_PARITY 6
#define CAUCHY_DISK (256-(CAUCHY_PARITY-1))

/**
 * Setup the Extended Cauchy matrix used to generate parity.
 * The first row is formed by all 1.
 * The second row are power of 2, 2^0, 2^1, 2^2, ...
 * The others row are computed.
 */
void set_cauchy(unsigned char* matrix)
{
	int i, j;
	unsigned char cv, rv;

	/* first row all 1, this is an Extended Cauchy matrix */
	for(i=0;i<CAUCHY_DISK;++i) {
		matrix[0*CAUCHY_DISK+i] = 1;
	}

	/* second row power of 2, this is the first row of the Cauchy matrix */
	cv = 1;
	for(i=0;i<CAUCHY_DISK;++i) {
		matrix[1*CAUCHY_DISK+i] = cv;
		cv = gfmul(2, cv);
	}

	/* other rows using a Cauchy matrix */
	rv = 2;
	for(j=0;j<CAUCHY_PARITY-2;++j) {
		unsigned char ce;

		ce = 1;
		for(i=0;i<CAUCHY_DISK;++i) {
			cv = gfinv[ce];
			matrix[(j+2)*CAUCHY_DISK+i] = gfinv[rv ^ cv];
			ce = gfmul(2, ce);
		}

		rv = gfmul(2, rv);
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
	unsigned char matrix[8 * 256];

	printf("/*\n");
	printf(" * Copyright (C) 2011 Andrea Mazzoleni\n");
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

	/* 4^a */
	printf("const unsigned char __attribute__((aligned(256))) gfexp4[256] =\n");
	printf("{\n");
	v = 1;
	for(i=0;i<256;++i) {
		if (i % 8 == 0)
			printf("\t");
		printf("0x%02x,", v);
		v = gfmul(v, 4);
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

	/* x4l */
	printf("#define GFX4L { ");
	for(i=0;i<16;++i) {
		v = gfmul(0x4, i);
		printf("0x%02x", (unsigned)v);
		if (i != 15)
			printf(", ");
	}
	printf(" }\n");

	/* x4h */
	printf("#define GFX4H { ");
	for(i=0;i<16;++i) {
		v = gfmul(0x40, i);
		printf("0x%02x", (unsigned)v);
		if (i != 15)
			printf(", ");
	}
	printf(" }\n");

	/* x8l */
	printf("#define GFX8L { ");
	for(i=0;i<16;++i) {
		v = gfmul(0x8, i);
		printf("0x%02x", (unsigned)v);
		if (i != 15)
			printf(", ");
	}
	printf(" }\n");

	/* x8h */
	printf("#define GFX8H { ");
	for(i=0;i<16;++i) {
		v = gfmul(0x80, i);
		printf("0x%02x", (unsigned)v);
		if (i != 15)
			printf(", ");
	}
	printf(" }\n");

	/* poly */
	printf("#define GFPOLY8 { 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d } \n");
	printf("#define GFPOLY16 { 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d } \n");
	printf("#define GFMASK16 { 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f } \n");
	printf("\n");

	/* power matrix */
	set_power(matrix);

	printf("/**\n");
	printf(" * Power matrix used to generate parity.\n");
	printf(" * This matrix is valid for up to 4 parity with 21 data disks.\n");
	printf(" *\n");
	for(p=0;p<POWER_PARITY;++p) {
		printf(" * ");
		for(i=0;i<POWER_DISK;++i) {
			printf("%02x ", matrix[p*POWER_DISK+i]);
			if (p+1 == 4 && i+1 == 21) {
				/* stop at 4 parity and 21 data disks */
				break;
			}
		}
		printf("\n");
	}
	printf(" */\n");
	printf("const unsigned char  __attribute__((aligned(256))) gfmatrix_power[%u][256] =\n", POWER_PARITY);
	printf("{\n");
	for(p=0;p<POWER_PARITY;++p) {
		printf("\t{\n");
		for(i=0;i<POWER_DISK;++i) {
			if (i % 8 == 0)
				printf("\t\t");
			printf("0x%02x,", matrix[p*POWER_DISK+i]);
			if (p+1 == 4 && i+1 == 21) {
				/* stop at 4 parity and 21 data disks */
				break;
			}
			if (i % 8 == 7)
				printf("\n");
			else
				printf(" ");
		}
		printf("\n\t},\n");
	}
	printf("};\n\n");

	/* matrix matrix */
	set_cauchy(matrix);

	printf("/**\n");
	printf(" * Extended Cauchy matrix used to generate parity.\n");
	printf(" * This matrix is valid for up to %u parity with %u data disks.\n", CAUCHY_PARITY, CAUCHY_DISK);
	printf(" *\n");
	for(p=0;p<CAUCHY_PARITY;++p) {
		printf(" * ");
		for(i=0;i<CAUCHY_DISK;++i) {
			printf("%02x ", matrix[p*CAUCHY_DISK+i]);
		}
		printf("\n");
	}
	printf(" */\n");
	printf("const unsigned char  __attribute__((aligned(256))) gfmatrix_cauchy[%u][256] =\n", CAUCHY_PARITY);
	printf("{\n");
	for(p=0;p<CAUCHY_PARITY;++p) {
		printf("\t{\n");
		for(i=0;i<CAUCHY_DISK;++i) {
			if (i % 8 == 0)
				printf("\t\t");
			printf("0x%02x,", matrix[p*CAUCHY_DISK+i]);
			if (i % 8 == 7)
				printf("\n");
			else
				printf(" ");
		}
		printf("\n\t},\n");
	}
	printf("};\n\n");

	printf("/**\n");
	printf(" * Multiplication tables for the Extended Cauchy matrix.\n");
	printf(" *\n");
	printf(" * Indexes are [DISK][DATA][PARITY - 1].\n");
	printf(" * Where DISK is from 0 to %u, DATA from 0 to 255, PARITY from 1 to %u.\n", CAUCHY_DISK - 1, CAUCHY_PARITY - 1);
	printf(" */\n");
	printf("const unsigned char  __attribute__((aligned(256))) gfcauchy_mul[%u][256][%u] =\n", CAUCHY_DISK, np(CAUCHY_PARITY - 1));
	printf("{\n");
	for(i=0;i<CAUCHY_DISK;++i) {
		printf("\t{\n");
		for(j=0;j<256;++j) {
			printf("\t\t{ ");
			for(p=1;p<CAUCHY_PARITY;++p) {
				v = gfmul(matrix[p*CAUCHY_DISK+i], j);
				printf("0x%02x", (unsigned)v);
				if (p != CAUCHY_PARITY - 1)
					printf(", ");
			}
			printf(" },\n");
		}
		printf("\t},\n");
	}
	printf("};\n\n");

	printf("#if defined(__i386__) || defined(__x86_64__)\n");
	printf("/**\n");
	printf(" * PSHUFB tables for the Extended Cauchy matrix.\n");
	printf(" *\n");
	printf(" * Indexes are [DISK][DATA][PARITY - 2].\n");
	printf(" * Where DISK is from 0 to %u, DATA from 0 to 255, PARITY from 2 to %u.\n", CAUCHY_DISK - 1, CAUCHY_PARITY - 1);
	printf(" */\n");
	printf("const unsigned char  __attribute__((aligned(256))) gfcauchy_pshufb[%u][%u][2][16] =\n", CAUCHY_DISK, np(CAUCHY_PARITY - 2));
	printf("{\n");
	for(i=0;i<CAUCHY_DISK;++i) {
		printf("\t{\n");
		for(p=2;p<CAUCHY_PARITY;++p) {
			printf("\t\t{\n");
			for(j=0;j<2;++j) {
				printf("\t\t\t{ ");
				for(k=0;k<16;++k) {
					v = gfmul(matrix[p*CAUCHY_DISK+i], k);
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

	return 0;
}

