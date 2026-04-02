// SPDX-License-Identifier: BSD-2-Clause
// Copyright (C) 2011 Andrea Mazzoleni

/** \file
 * Dynamic array based on segments of exponentially growing size.
 *
 * This array is able to grow dynamically upon request, without any reallocation.
 *
 * The grow operation involves an allocation of a new array segment, without reallocating
 * the already used memory, and thus **not increasing** the heap fragmentation.
 * This also implies that the address of the stored elements never change.
 *
 * Allocated segments grow in size exponentially.
 */

#ifndef __TOMMYARRAY_H
#define __TOMMYARRAY_H

#include "tommytypes.h"

#include <assert.h> /* for assert */

/******************************************************************************/
/* array */

/**
 * Initial and minimal size of the array expressed as a power of 2.
 * The initial size is 2^TOMMY_ARRAY_BIT.
 */
#define TOMMY_ARRAY_BIT 6

/**
 * Array container type.
 * \note Don't use internal fields directly, but access the container only using functions.
 */
typedef struct tommy_array_struct {
	void** bucket[TOMMY_SIZE_BIT]; /**< Dynamic array of buckets. */
	tommy_size_t bucket_max; /**< Number of buckets. */
	tommy_size_t count; /**< Number of initialized elements in the array. */
	tommy_uint_t bucket_bit; /**< Bits used in the bit mask. */
} tommy_array;

/**
 * Initializes the array.
 */
TOMMY_API void tommy_array_init(tommy_array* array);

/**
 * Deinitializes the array.
 */
TOMMY_API void tommy_array_done(tommy_array* array);

/**
 * Grows the size up to the specified value.
 * All the new elements in the array are initialized with the 0 value.
 */
TOMMY_API void tommy_array_grow(tommy_array* array, tommy_size_t size);

/**
 * Gets a reference of the element at the specified position.
 * You must be sure that space for this position is already
 * allocated calling tommy_array_grow().
 */
tommy_inline void** tommy_array_ref(tommy_array* array, tommy_size_t pos)
{
	tommy_uint_t bsr;

	assert(pos < array->count);

	/* get the highest bit set, in case of all 0, return 0 */
	bsr = tommy_ilog2(pos | 1);

	return &array->bucket[bsr][pos];
}

/**
 * Sets the element at the specified position.
 * You must be sure that space for this position is already
 * allocated calling tommy_array_grow().
 */
tommy_inline void tommy_array_set(tommy_array* array, tommy_size_t pos, void* element)
{
	*tommy_array_ref(array, pos) = element;
}

/**
 * Gets the element at the specified position.
 * You must be sure that space for this position is already
 * allocated calling tommy_array_grow().
 */
tommy_inline void* tommy_array_get(tommy_array* array, tommy_size_t pos)
{
	return *tommy_array_ref(array, pos);
}

/**
 * Grows and inserts a new element at the end of the array.
 */
tommy_inline void tommy_array_insert(tommy_array* array, void* element)
{
	tommy_size_t pos = array->count;

	tommy_array_grow(array, pos + 1);

	tommy_array_set(array, pos, element);
}

/**
 * Gets the initialized size of the array.
 */
tommy_inline tommy_size_t tommy_array_size(tommy_array* array)
{
	return array->count;
}

/**
 * Gets the size of allocated memory.
 */
TOMMY_API tommy_size_t tommy_array_memory_usage(tommy_array* array);

#endif
