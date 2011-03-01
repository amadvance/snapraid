/*
 * Copyright (C) 2011 Andrea Mazzoleni
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
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

#include "portable.h"

void gen(int disk, int size)
{
	char path[PATH_MAX];
	char name[PATH_MAX];
	FILE* f;
	int i;
	int l;

	l = 1 + rand() % 32;

	for(i=0;i<l;++i) {
		name[i] = 'a' + (rand() % 22);
	}
	name[i] = 0;

	snprintf(path, sizeof(path), "test/disk%d/%s", disk, name);

	f = fopen(path, "w");
	if (!f) {
		fprintf(stderr, "Error writing %s\n", path);
		exit(EXIT_FAILURE);
	}

	while (size) {
		fputc(size, f);
		--size;
	}

	fclose(f);
}

int main(int argc, char* argv[])
{
	int i, j;
	int s = 1;
	int d = 3;
	int n = 100;

	if (argc != 4) {
		fprintf(stderr, "Syntax: testgen SEED DISK FILE\n");
		exit(EXIT_FAILURE);
	}

	s = atoi(argv[1]);
	d = atoi(argv[2]);
	n = atoi(argv[3]);

	srand(s);

	for(i=0;i<d;++i) {
		for(j=0;j<n;++j) {
			gen(i+1, j);
		}
	}

	return 0;
}
