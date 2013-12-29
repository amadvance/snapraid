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

#include "internal.h"
#include "memory.h"

void *raid_malloc_align(size_t size, void **freeptr)
{
	unsigned char *ptr;
	uintptr_t offset;

	ptr = malloc(size + RAID_MALLOC_ALIGN);
	if (!ptr)
		return 0;

	*freeptr = ptr;

	offset = ((uintptr_t)ptr) % RAID_MALLOC_ALIGN;

	if (offset != 0)
		ptr += RAID_MALLOC_ALIGN - offset;

	return ptr;
}

void **raid_malloc_vector(size_t reverse, size_t count, size_t size, void **freeptr)
{
	void **v;
	unsigned char *va;
	size_t i;

	assert(reverse <= count);

	v = malloc(count * sizeof(void *));
	if (!v)
		return 0;

	va = raid_malloc_align(count * (size + RAID_MALLOC_DISPLACEMENT), freeptr);
	if (!va) {
		free(v);
		return 0;
	}

	for (i = 0; i < count; ++i)
		v[i] = va + i * (size + RAID_MALLOC_DISPLACEMENT);

	/* reverse order for the initial ones */
	for (i = 0; i < reverse/2; ++i) {
		void *ptr = v[i];
		v[i] = v[reverse - 1 - i];
		v[reverse - 1 - i] = ptr;
	}

	return v;
}

int raid_mtest_vector(void **vv, size_t count, size_t size)
{
	unsigned char **v = (unsigned char **)vv;
	size_t i;
	size_t j;
	unsigned k;
	unsigned char d;
	unsigned char p;

	/* fill with 0 */
	d = 0;
	for (i = 0; i < count; ++i)
		for (j = 0; j < size; ++j)
			v[i][j] = d;

	/* test with all the byte patterns */
	for (k = 1; k < 256; ++k) {
		p = d;
		d = k;

		/* forward fill */
		for (i = 0; i < count; ++i) {
			for (j = 0; j < size; ++j) {
				if (v[i][j] != p)
					return -1;
				v[i][j] = d;
			}
		}

		p = d;
		d = ~p;
		/* backward fill with complement */
		for (i = 0; i < count; ++i) {
			for (j = size; j > 0; --j) {
				if (v[i][j-1] != p)
					return -1;
				v[i][j-1] = d;
			}
		}
	}

	return 0;
}

void raid_mrand_vector(void **vv, size_t count, size_t size)
{
	unsigned char **v = (unsigned char **)vv;
	size_t i;
	size_t j;

	for (i = 0; i < count; ++i)
		for (j = 0; j < size; ++j)
			v[i][j] = rand();
}

