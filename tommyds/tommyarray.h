/*
 * Copyright (c) 2011, Andrea Mazzoleni. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/** \file
 * Dynamic array based on segments of exponential growing size.
 *
 * This array is able to grow dynamically upon request.
 *
 * The resize involve an allocation of a new array segment, without reallocating
 * the already allocated memory, and then not increasing the heap fragmentation.
 * This means that the address of the allocated segments never change.
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

/** \internal
 * Max number of elements as a power of 2.
 */
#define TOMMY_ARRAY_BIT_MAX 32

/**
 * Array.
 */
typedef struct tommy_array_struct {
	void** bucket[TOMMY_ARRAY_BIT_MAX]; /**< Dynamic array of buckets. */
	unsigned bucket_bit; /**< Bits used in the bit mask. */
	unsigned bucket_max; /**< Number of buckets. */
	unsigned bucket_mac; /**< Number of vectors allocated. */
	unsigned size; /**< Currently allocated and initialized size. */
} tommy_array;

/**
 * Initializes the array.
 */
void tommy_array_init(tommy_array* array);

/**
 * Deinitializes the array.
 */
void tommy_array_done(tommy_array* array);

/**
 * Grow the size up to the specified value.
 * All the new elements in the array are initialized with the 0 value.
 */
void tommy_array_grow(tommy_array* array, unsigned size);

/**
 * Gets a reference of the element at the specified position.
 * You must be sure that space for this position is already
 * allocated calling tommy_array_grow().
 */
tommy_inline void** tommy_array_ref(tommy_array* array, unsigned pos)
{
	unsigned bsr;  

	assert(pos < array->size);

	/* special case for the first bucket */
	if (pos < (1 << TOMMY_ARRAY_BIT)) {
		return &array->bucket[0][pos];
	}

	/* get the highest bit set */
	bsr = tommy_ilog2_u32(pos);

	/* clear the highest bit */
	pos -= 1 << bsr;

	return &array->bucket[bsr - TOMMY_ARRAY_BIT + 1][pos];
}

/**
 * Sets the element at the specified position.
 * You must be sure that space for this position is already
 * allocated calling tommy_array_grow().
 */
tommy_inline void tommy_array_set(tommy_array* array, unsigned pos, void* element)
{
	*tommy_array_ref(array, pos) = element;
}

/**
 * Gets the element at the specified position.
 * You must be sure that space for this position is already
 * allocated calling tommy_array_grow().
 */
tommy_inline void* tommy_array_get(tommy_array* array, unsigned pos)
{
	return *tommy_array_ref(array, pos);
}

/**
 * Grows and inserts a new element at the end of the array.
 */
tommy_inline void tommy_array_insert(tommy_array* array, void* element)
{
	unsigned pos = array->size;

	tommy_array_grow(array, pos + 1);

	tommy_array_set(array, pos, element);
}

/**
 * Gets the initialized size of the array.
 */
tommy_inline unsigned tommy_array_size(tommy_array* array)
{
	return array->size;
}

/**
 * Gets the size of allocated memory.
 */
tommy_size_t tommy_array_memory_usage(tommy_array* array);

#endif

