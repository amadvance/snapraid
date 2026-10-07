// SPDX-License-Identifier: BSD-2-Clause
// Copyright (C) 2011 Andrea Mazzoleni

#include "tommyarray.h"

#include <string.h> /* for memset */

/******************************************************************************/
/* array */

TOMMY_API void tommy_array_init(tommy_array* array)
{
	/* fixed initial size */
	array->bucket_bit = TOMMY_ARRAY_BIT;
	array->bucket_max = (tommy_size_t)1 << array->bucket_bit;
	array->bucket[0] = tommy_cast(void**, tommy_calloc(array->bucket_max, sizeof(void*)));
	for (tommy_uint_t i = 1; i < TOMMY_ARRAY_BIT; ++i)
		array->bucket[i] = array->bucket[0];

	array->count = 0;
}

TOMMY_API void tommy_array_done(tommy_array* array)
{
	tommy_free(array->bucket[0]);
	for (tommy_uint_t i = TOMMY_ARRAY_BIT; i < array->bucket_bit; ++i) {
		void** segment = array->bucket[i];
		tommy_free(&segment[(tommy_ptrdiff_t)1 << i]);
	}
}

TOMMY_API void tommy_array_reserve(tommy_array* array, tommy_size_t size)
{
	while (size > array->bucket_max) {
		/* allocate one more segment */
		void** segment = tommy_cast(void**, tommy_calloc(array->bucket_max, sizeof(void*)));

		/* store it adjusting the offset */
		/* cast to ptrdiff_t to ensure to get a negative value */
		array->bucket[array->bucket_bit] = &segment[-(tommy_ptrdiff_t)array->bucket_max];

		++array->bucket_bit;
		array->bucket_max = (tommy_size_t)1 << array->bucket_bit;
	}
}

TOMMY_API void tommy_array_resize(tommy_array* array, tommy_size_t size)
{
	if (size >= array->count) {
		tommy_array_grow(array, size);
		return;
	}

	/* clear the unused elements to maintain the invariant that unused slots are zero */
	tommy_size_t pos = size;
	while (pos < array->count) {
		tommy_uint_t bsr = tommy_ilog2(pos | 1);
		tommy_size_t seg_end = (bsr < TOMMY_ARRAY_BIT) ? ((tommy_size_t)1 << TOMMY_ARRAY_BIT) : ((tommy_size_t)1 << (bsr + 1));
		tommy_size_t chunk_end = array->count < seg_end ? array->count : seg_end;

		memset(&array->bucket[bsr][pos], 0, (chunk_end - pos) * sizeof(void*));
		pos = chunk_end;
	}

	array->count = size;
}

TOMMY_API void tommy_array_shrink(tommy_array* array)
{
	tommy_uint_t target_bucket_bit = TOMMY_ARRAY_BIT;

	if (array->count > (tommy_size_t)1 << TOMMY_ARRAY_BIT)
		target_bucket_bit = tommy_ilog2(array->count - 1) + 1;

	while (array->bucket_bit > target_bucket_bit) {
		--array->bucket_bit;
		void** segment = array->bucket[array->bucket_bit];
		tommy_free(&segment[(tommy_ptrdiff_t)1 << array->bucket_bit]);
	}

	array->bucket_max = (tommy_size_t)1 << array->bucket_bit;
}

TOMMY_API void tommy_array_foreach(tommy_array* array, tommy_foreach_func* func)
{
	tommy_size_t pos = 0;

	while (pos < array->count) {
		tommy_uint_t bsr = tommy_ilog2(pos | 1);
		tommy_size_t seg_end = (bsr < TOMMY_ARRAY_BIT) ? ((tommy_size_t)1 << TOMMY_ARRAY_BIT) : ((tommy_size_t)1 << (bsr + 1));
		tommy_size_t chunk_end = array->count < seg_end ? array->count : seg_end;
		void** ptr = array->bucket[bsr];

		while (pos < chunk_end) {
			func(ptr[pos]);
			++pos;
		}
	}
}

TOMMY_API void tommy_array_foreach_arg(tommy_array* array, tommy_foreach_arg_func* func, void* arg)
{
	tommy_size_t pos = 0;

	while (pos < array->count) {
		tommy_uint_t bsr = tommy_ilog2(pos | 1);
		tommy_size_t seg_end = (bsr < TOMMY_ARRAY_BIT) ? ((tommy_size_t)1 << TOMMY_ARRAY_BIT) : ((tommy_size_t)1 << (bsr + 1));
		tommy_size_t chunk_end = array->count < seg_end ? array->count : seg_end;
		void** ptr = array->bucket[bsr];

		while (pos < chunk_end) {
			func(arg, ptr[pos]);
			++pos;
		}
	}
}

TOMMY_API tommy_size_t tommy_array_memory_usage(const tommy_array* array)
{
	return array->bucket_max * (tommy_size_t)sizeof(void*);
}

