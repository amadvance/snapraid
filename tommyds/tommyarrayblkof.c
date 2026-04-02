// SPDX-License-Identifier: BSD-2-Clause
// Copyright (C) 2013 Andrea Mazzoleni

#include "tommyarrayblkof.h"

/******************************************************************************/
/* array */

TOMMY_API void tommy_arrayblkof_init(tommy_arrayblkof* array, tommy_size_t element_size)
{
	tommy_array_init(&array->block);

	array->element_size = element_size;
	array->count = 0;
}

TOMMY_API void tommy_arrayblkof_done(tommy_arrayblkof* array)
{
	tommy_size_t i;

	for (i = 0; i < tommy_array_size(&array->block); ++i)
		tommy_free(tommy_array_get(&array->block, i));

	tommy_array_done(&array->block);
}

TOMMY_API void tommy_arrayblkof_grow(tommy_arrayblkof* array, tommy_size_t count)
{
	tommy_size_t block_max;
	tommy_size_t block_mac;

	if (array->count >= count)
		return;
	array->count = count;

	block_max = (count + TOMMY_ARRAYBLKOF_SIZE - 1) / TOMMY_ARRAYBLKOF_SIZE;
	block_mac = tommy_array_size(&array->block);

	if (block_mac < block_max) {
		/* grow the block array */
		tommy_array_grow(&array->block, block_max);

		/* allocate new blocks */
		while (block_mac < block_max) {
			void** ptr = tommy_cast(void**, tommy_calloc(TOMMY_ARRAYBLKOF_SIZE, array->element_size));

			/* set the new block */
			tommy_array_set(&array->block, block_mac, ptr);

			++block_mac;
		}
	}
}

TOMMY_API tommy_size_t tommy_arrayblkof_memory_usage(tommy_arrayblkof* array)
{
	return tommy_array_memory_usage(&array->block) + tommy_array_size(&array->block) * TOMMY_ARRAYBLKOF_SIZE * array->element_size;
}

