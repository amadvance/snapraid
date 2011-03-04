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

#include "util.h"

void* malloc_nofail(size_t size)
{
	void* ptr = malloc(size);

	if (!ptr) {
		fprintf(stderr, "Low memory\n");
		exit(EXIT_FAILURE);
	}

	return ptr;
}

void memxor(unsigned char* xor, const unsigned char* block, unsigned size)
{
	while (size >= 16) {
		uint32_t* xor32 = (uint32_t*)xor;
		const uint32_t* block32 = (const uint32_t*)block;
		xor32[0] ^= block32[0];
		xor32[1] ^= block32[1];
		xor32[2] ^= block32[2];
		xor32[3] ^= block32[3];

		xor += 16;
		block += 16;
		size -= 16;
	}

	while (size >= 4) {
		uint32_t* xor32 = (uint32_t*)xor;
		const uint32_t* block32 = (const uint32_t*)block;
		xor32[0] ^= block32[0];

		xor += 4;
		block += 4;
		size -= 4;
	}

	while (size != 0) {
		xor[0] ^= block[0];

		xor += 1;
		block += 1;
		size -= 1;
	}
}

