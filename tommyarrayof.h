/*
 * Copyright 2013 Andrea Mazzoleni. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY ANDREA MAZZOLENI AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL ANDREA MAZZOLENI OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/** \file
 * Dynamic array.
 *
 * This array is able to grow dynamically upon request.
 *
 * This is very similar at ::tommy_array, but it allows to store elements of any
 * size and not just pointers.
 *
 * The resize involve an allocation of a new array segment, without reallocating
 * the already allocated memory, and then not increasing the heap fragmentation.
 *
 * This means that the address of the allocated segments never change.
 */

#ifndef __TOMMYARRAYOF_H
#define __TOMMYARRAYOF_H

#include "tommytypes.h"

#include <assert.h> /* for assert */

/******************************************************************************/
/* array */

/**
 * Initial and minimal size of the array expressed as a power of 2.
 * The initial size is 2^TOMMY_ARRAYOF_BIT.
 */
#define TOMMY_ARRAYOF_BIT 6

/** \internal
 * Max number of elements as a power of 2.
 */
#define TOMMY_ARRAYOF_BIT_MAX 32

/**
 * Array.
 */
typedef struct tommy_arrayof_struct {
	void* bucket[TOMMY_ARRAYOF_BIT_MAX]; /**< Dynamic array of buckets. */
	unsigned element_size; /**< Size of the stored element in bytes. */
	unsigned bucket_bit; /**< Bits used in the bit mask. */
	unsigned bucket_max; /**< Number of buckets. */
	unsigned bucket_mac; /**< Number of vectors allocated. */
	unsigned size; /**< Currently allocated and initialized size. */
} tommy_arrayof;

/**
 * Initializes the array.
 */
void tommy_arrayof_init(tommy_arrayof* array, unsigned element_size);

/**
 * Deinitializes the array.
 */
void tommy_arrayof_done(tommy_arrayof* array);

/**
 * Grow the size up to the specified value.
 * All the new elements in the array are initialized with the 0 value.
 */
void tommy_arrayof_grow(tommy_arrayof* array, unsigned size);

/**
 * Gets a reference of the element at the specified position.
 * You must be sure that space for this position is already
 * allocated calling tommy_arrayof_grow().
 */
tommy_inline void* tommy_arrayof_ref(tommy_arrayof* array, unsigned pos)
{
	unsigned char* base;

	assert(pos < array->size);

	/* special case for the first bucket */
	if (pos < (1 << TOMMY_ARRAYOF_BIT)) {
		base = array->bucket[0];
	} else {
		/* get the highest bit set */
		unsigned bsr = tommy_ilog2_u32(pos);

		/* clear the highest bit */
		pos -= 1 << bsr;

		base = array->bucket[bsr - TOMMY_ARRAYOF_BIT + 1];
	}

	return base + pos * array->element_size;
}

/**
 * Gets the initialized size of the array.
 */
tommy_inline unsigned tommy_arrayof_size(tommy_arrayof* array)
{
	return array->size;
}

/**
 * Gets the size of allocated memory.
 */
tommy_size_t tommy_arrayof_memory_usage(tommy_arrayof* array);

#endif

