// SPDX-License-Identifier: BSD-2-Clause
// Copyright (C) 2013 Andrea Mazzoleni

#include "tommyarrayblkof.h"

#include <string.h> /* for memset */

/******************************************************************************/
/* array */

TOMMY_API void tommy_arrayblkof_init(tommy_arrayblkof* array, tommy_size_t element_size)
{
	array->block_capacity = (tommy_size_t)1 << TOMMY_ARRAYBLKOF_BIT;
	array->block = tommy_cast(unsigned char**, tommy_malloc(array->block_capacity * sizeof(array->block[0])));
	array->block_count = 0;

	array->element_size = element_size;
	array->count = 0;
}

TOMMY_API void tommy_arrayblkof_done(tommy_arrayblkof* array)
{
	for (tommy_size_t i = 0; i < array->block_count; ++i)
		tommy_free(array->block[i]);

	tommy_free(array->block);
}

TOMMY_API void tommy_arrayblkof_reserve(tommy_arrayblkof* array, tommy_size_t size)
{
	if (size <= array->block_count * TOMMY_ARRAYBLKOF_SIZE)
		return;

	tommy_size_t block_max = (size - 1) / TOMMY_ARRAYBLKOF_SIZE + 1;

	if (array->block_capacity < block_max) {
		/* only the directory moves; addresses inside existing blocks stay valid */
		tommy_size_t capacity = array->block_capacity;
		while (capacity < block_max)
			capacity *= 2;
		array->block = tommy_cast(unsigned char**, tommy_realloc(array->block, capacity * sizeof(array->block[0])));
		array->block_capacity = capacity;
	}

	/* allocate new blocks */
	while (array->block_count < block_max) {
		unsigned char* ptr = tommy_cast(unsigned char*, tommy_calloc(TOMMY_ARRAYBLKOF_SIZE, array->element_size));

		array->block[array->block_count] = ptr;
		++array->block_count;
	}
}

TOMMY_API void tommy_arrayblkof_resize(tommy_arrayblkof* array, tommy_size_t size)
{
	if (size >= array->count) {
		tommy_arrayblkof_grow(array, size);
		return;
	}

	/* clear the unused elements to maintain the invariant that unused slots are zero */
	tommy_size_t pos = size;
	while (pos < array->count) {
		tommy_size_t blk_idx = pos / TOMMY_ARRAYBLKOF_SIZE;
		tommy_size_t blk_offset = pos % TOMMY_ARRAYBLKOF_SIZE;
		tommy_size_t blk_rem = TOMMY_ARRAYBLKOF_SIZE - blk_offset;
		tommy_size_t count_rem = array->count - pos;
		tommy_size_t chunk = blk_rem < count_rem ? blk_rem : count_rem;

		memset(array->block[blk_idx] + blk_offset * array->element_size, 0, chunk * array->element_size);
		pos += chunk;
	}

	array->count = size;
}

TOMMY_API void tommy_arrayblkof_shrink(tommy_arrayblkof* array)
{
	tommy_size_t block_max = (array->count == 0) ? 0 : ((array->count - 1) / TOMMY_ARRAYBLKOF_SIZE + 1);

	while (array->block_count > block_max) {
		--array->block_count;
		tommy_free(array->block[array->block_count]);
	}

	tommy_size_t min_capacity = (tommy_size_t)1 << TOMMY_ARRAYBLKOF_BIT;
	tommy_size_t capacity = min_capacity;
	while (capacity < block_max)
		capacity *= 2;
	if (capacity < array->block_capacity) {
		array->block = tommy_cast(unsigned char**, tommy_realloc(array->block, capacity * sizeof(array->block[0])));
		array->block_capacity = capacity;
	}
}

TOMMY_API void tommy_arrayblkof_foreach(tommy_arrayblkof* array, tommy_foreach_func* func)
{
	/* cache the element size to avoid reloading it after each callback */
	tommy_size_t element_size = array->element_size;
	tommy_size_t pos = 0;

	while (pos < array->count) {
		tommy_size_t blk_idx = pos / TOMMY_ARRAYBLKOF_SIZE;
		tommy_size_t chunk = array->count - pos;
		unsigned char* ptr = array->block[blk_idx];

		if (chunk > TOMMY_ARRAYBLKOF_SIZE)
			chunk = TOMMY_ARRAYBLKOF_SIZE;

		for (tommy_size_t i = 0; i < chunk; ++i) {
			func(ptr);
			ptr += element_size;
		}

		pos += chunk;
	}
}

TOMMY_API void tommy_arrayblkof_foreach_arg(tommy_arrayblkof* array, tommy_foreach_arg_func* func, void* arg)
{
	/* cache the element size to avoid reloading it after each callback */
	tommy_size_t element_size = array->element_size;
	tommy_size_t pos = 0;

	while (pos < array->count) {
		tommy_size_t blk_idx = pos / TOMMY_ARRAYBLKOF_SIZE;
		tommy_size_t chunk = array->count - pos;
		unsigned char* ptr = array->block[blk_idx];

		if (chunk > TOMMY_ARRAYBLKOF_SIZE)
			chunk = TOMMY_ARRAYBLKOF_SIZE;

		for (tommy_size_t i = 0; i < chunk; ++i) {
			func(arg, ptr);
			ptr += element_size;
		}

		pos += chunk;
	}
}

TOMMY_API tommy_size_t tommy_arrayblkof_memory_usage(const tommy_arrayblkof* array)
{
	return array->block_capacity * sizeof(array->block[0]) + array->block_count * TOMMY_ARRAYBLKOF_SIZE * array->element_size;
}

