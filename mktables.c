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

int main(void)
{
	unsigned char pow2[256];
	unsigned char log2[256];
	unsigned char v;
	int i, j;

	/* a*b */
	printf("const unsigned char  __attribute__((aligned(256))) gfmul[256][256] =\n");
	printf("{\n");
	for(i=0;i<256;++i) {
		printf("\t{\n");
		for(j=0;j<256;++j) {
			if (j % 8 == 0)
				printf("\t\t");
			printf("0x%02x,", gfmul(i, j));
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
		pow2[i] = v;
		log2[pow2[i]] = i;
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
			v = pow2[255 - log2[i]];
		printf("0x%02x,", v);
		if (i % 8 == 7)
			printf("\n");
		else
			printf(" ");
	}
	printf("};\n\n");

	return 0;
}

