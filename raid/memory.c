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

#include "internal.h"
#include "memory.h"

void *raid_malloc_align(size_t size, void **freeptr)
{
	unsigned char *ptr;
	uintptr_t offset;

	ptr = malloc(size + RAID_MALLOC_ALIGN);
	if (!ptr) {
		/* LCOV_EXCL_START */
		return 0;
		/* LCOV_EXCL_STOP */
	}

	*freeptr = ptr;

	offset = ((uintptr_t)ptr) % RAID_MALLOC_ALIGN;

	if (offset != 0)
		ptr += RAID_MALLOC_ALIGN - offset;

	return ptr;
}

void **raid_malloc_vector(int nd, int n, size_t size, void **freeptr)
{
	void **v;
	unsigned char *va;
	int i;

	BUG_ON(n <= 0 || nd < 0);

	v = malloc(n * sizeof(void *));
	if (!v) {
		/* LCOV_EXCL_START */
		return 0;
		/* LCOV_EXCL_STOP */
	}

	va = raid_malloc_align(n * (size + RAID_MALLOC_DISPLACEMENT), freeptr);
	if (!va) {
		/* LCOV_EXCL_START */
		free(v);
		return 0;
		/* LCOV_EXCL_STOP */
	}

	for (i = 0; i < n; ++i) {
		v[i] = va;
		va += size + RAID_MALLOC_DISPLACEMENT;
	}

	/* reverse order of the data blocks */
	/* because they are usually accessed from the last one */
	for (i = 0; i < nd / 2; ++i) {
		void *ptr = v[i];

		v[i] = v[nd - 1 - i];
		v[nd - 1 - i] = ptr;
	}

	return v;
}

void raid_mrand_vector(unsigned seed, int n, size_t size, void **vv)
{
	unsigned char **v = (unsigned char **)vv;
	int i;
	size_t j;

	for (i = 0; i < n; ++i)
		for (j = 0; j < size; ++j) {
			/* basic C99/C11 linear congruential generator */
			seed = seed * 1103515245U + 12345U;

			v[i][j] = seed >> 16;
		}
}

int raid_mtest_vector(int n, size_t size, void **vv)
{
	unsigned char **v = (unsigned char **)vv;
	int i;
	size_t j;
	unsigned k;
	unsigned char d;
	unsigned char p;

	/* fill with 0 */
	d = 0;
	for (i = 0; i < n; ++i)
		for (j = 0; j < size; ++j)
			v[i][j] = d;

	/* test with all the byte patterns */
	for (k = 1; k < 256; ++k) {
		p = d;
		d = k;

		/* forward fill */
		for (i = 0; i < n; ++i) {
			for (j = 0; j < size; ++j) {
				if (v[i][j] != p) {
					/* LCOV_EXCL_START */
					return -1;
					/* LCOV_EXCL_STOP */
				}
				v[i][j] = d;
			}
		}

		p = d;
		d = ~p;
		/* backward fill with complement */
		for (i = 0; i < n; ++i) {
			for (j = size; j > 0; --j) {
				if (v[i][j - 1] != p) {
					/* LCOV_EXCL_START */
					return -1;
					/* LCOV_EXCL_STOP */
				}
				v[i][j - 1] = d;
			}
		}
	}

	return 0;
}

