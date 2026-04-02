// SPDX-License-Identifier: BSD-2-Clause
// Copyright (C) 2011 Andrea Mazzoleni

#include "tommyarray.h"

/******************************************************************************/
/* array */

TOMMY_API void tommy_array_init(tommy_array* array)
{
	tommy_uint_t i;

	/* fixed initial size */
	array->bucket_bit = TOMMY_ARRAY_BIT;
	array->bucket_max = (tommy_size_t)1 << array->bucket_bit;
	array->bucket[0] = tommy_cast(void**, tommy_calloc(array->bucket_max, sizeof(void*)));
	for (i = 1; i < TOMMY_ARRAY_BIT; ++i)
		array->bucket[i] = array->bucket[0];

	array->count = 0;
}

TOMMY_API void tommy_array_done(tommy_array* array)
{
	tommy_uint_t i;

	tommy_free(array->bucket[0]);
	for (i = TOMMY_ARRAY_BIT; i < array->bucket_bit; ++i) {
		void** segment = array->bucket[i];
		tommy_free(&segment[(tommy_ptrdiff_t)1 << i]);
	}
}

TOMMY_API void tommy_array_grow(tommy_array* array, tommy_size_t count)
{
	if (array->count >= count)
		return;
	array->count = count;

	while (count > array->bucket_max) {
		void** segment;

		/* allocate one more segment */
		segment = tommy_cast(void**, tommy_calloc(array->bucket_max, sizeof(void*)));

		/* store it adjusting the offset */
		/* cast to ptrdiff_t to ensure to get a negative value */
		array->bucket[array->bucket_bit] = &segment[-(tommy_ptrdiff_t)array->bucket_max];

		++array->bucket_bit;
		array->bucket_max = (tommy_size_t)1 << array->bucket_bit;
	}
}

TOMMY_API tommy_size_t tommy_array_memory_usage(tommy_array* array)
{
	return array->bucket_max * (tommy_size_t)sizeof(void*);
}

