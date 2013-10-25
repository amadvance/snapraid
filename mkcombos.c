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

unsigned combo(unsigned mask, unsigned pick, unsigned from, unsigned to, unsigned* map)
{
	unsigned i;
	unsigned count;

	if (pick == 0) {
		map[0] = mask;
		return 1;
	}

	count = 0;
	for(i=from;i<=to;++i)
		count += combo(mask | (1 << i), pick - 1, i + 1, to, map + count);

	return count;
}

void print_combo(unsigned n)
{
	unsigned map[10000];
	unsigned count;
	unsigned i, j, k;

	printf("/**\n");
	printf(" * Combinations of %u elements.\n", n);
	printf(" * Ends is marked with first element as -1.\n");
	printf(" */\n");
	printf("static int combo[][32][8] = {\n"); /* fixed value 32/8, change them if changing maximum n */

	for(i=1;i<=n;++i) {
		count = combo(0, i, 0, n - 1, map);

		printf("\t/* combinations of %u of %u elements */\n", i, n);
		printf("\t{\n");
		for(j=0;j<count;++j) {
			unsigned mask = map[j];
			printf("\t\t{ ");
			for(k=0;mask;++k) {
				if (mask & 1) {
					printf("%u", k);
					mask >>= 1;
					if (mask)
						printf(", ");
				} else {
					mask >>= 1;
				}
			}
			printf(" },\n");
		}
		printf("\t\t{ -1 }\n");
		printf("\t},\n");
	}
	printf("};\n\n");
}

int main(void)
{
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

	print_combo(6);
	return 0;
}

