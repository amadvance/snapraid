// SPDX-License-Identifier: BSD-2-Clause
// Copyright (C) 2013 Andrea Mazzoleni

/** \file
 * Dynamic array based on blocks of fixed size.
 *
 * This array is able to grow dynamically without reallocating stored elements.
 * The directory of block pointers may be reallocated, but element addresses
 * never change.
 *
 * This is very similar to ::tommy_arrayblk, but it allows to store elements of any
 * size and not just pointers.
 *
 * The container allocates space for elements but never copies user data into
 * or out of it. tommy_arrayblkof_ref(), tommy_arrayblkof_tail(), and
 * tommy_arrayblkof_insert_tail() return an element's address so the caller can
 * read or write it directly.
 */

#ifndef __TOMMYARRAYBLKOF_H
#define __TOMMYARRAYBLKOF_H

#include "tommytypes.h"

#include <assert.h> /* for assert */

/******************************************************************************/
/* array */

/**
 * Initial and minimal capacity of the block directory expressed as a power of 2.
 */
#define TOMMY_ARRAYBLKOF_BIT 6

/**
 * Elements for each block.
 */
#define TOMMY_ARRAYBLKOF_SIZE (4 * 1024)

/**
 * Array container type.
 * \note Don't use internal fields directly, but access the container only using functions.
 */
typedef struct tommy_arrayblkof_struct {
	unsigned char** block; /**< Directory of blocks. */
	tommy_size_t block_count; /**< Number of allocated blocks. */
	tommy_size_t block_capacity; /**< Number of directory entries allocated. */
	tommy_size_t element_size; /**< Size of the stored element in bytes. */
	tommy_size_t count; /**< Number of initialized elements in the array. */
} tommy_arrayblkof;

/**
 * Initializes the array.
 * \param element_size Size in byte of the element to store in the array.
 */
TOMMY_API void tommy_arrayblkof_init(tommy_arrayblkof* array, tommy_size_t element_size);

/**
 * Deinitializes the array.
 */
TOMMY_API void tommy_arrayblkof_done(tommy_arrayblkof* array);

/**
 * Allocates space for at least the specified number of elements.
 * The initialized size and existing elements are unchanged.
 * Existing element references remain valid.
 */
TOMMY_API void tommy_arrayblkof_reserve(tommy_arrayblkof* array, tommy_size_t size);

/**
 * Grows the size up to the specified value.
 * All the new elements in the array are initialized with the 0 value.
 */
tommy_inline void tommy_arrayblkof_grow(tommy_arrayblkof* array, tommy_size_t size)
{
	if (size > array->count) {
		array->count = size;

		if (size > array->block_count * TOMMY_ARRAYBLKOF_SIZE)
			tommy_arrayblkof_reserve(array, size);
	}
}

/**
 * Changes the initialized size, preserving the common prefix.
 * New elements are initialized to zero.
 * Reducing the size does not release allocated capacity.
 */
TOMMY_API void tommy_arrayblkof_resize(tommy_arrayblkof* array, tommy_size_t size);

/**
 * Removes all elements, preserving the allocated capacity.
 * The array remains initialized and can be reused immediately.
 */
tommy_inline void tommy_arrayblkof_clear(tommy_arrayblkof* array)
{
	tommy_arrayblkof_resize(array, 0);
}

/**
 * Releases unused allocated memory.
 * Preserves the size, values, and addresses of existing elements.
 */
TOMMY_API void tommy_arrayblkof_shrink(tommy_arrayblkof* array);

/**
 * Gets a reference of the element at the specified position.
 * You must be sure that space for this position is already
 * allocated calling tommy_arrayblkof_grow().
 */
tommy_inline void* tommy_arrayblkof_ref(tommy_arrayblkof* array, tommy_size_t pos)
{
	assert(pos < array->count);

	return array->block[pos / TOMMY_ARRAYBLKOF_SIZE] + (pos % TOMMY_ARRAYBLKOF_SIZE) * array->element_size;
}

/**
 * Gets a reference to the last element.
 * The array must not be empty.
 */
tommy_inline void* tommy_arrayblkof_tail(tommy_arrayblkof* array)
{
	assert(array->count != 0);
	return tommy_arrayblkof_ref(array, array->count - 1);
}

/**
 * Adds a zero-initialized element and returns its reference.
 */
tommy_inline void* tommy_arrayblkof_insert_tail(tommy_arrayblkof* array)
{
	tommy_size_t pos = array->count;
	tommy_arrayblkof_grow(array, pos + 1);
	return tommy_arrayblkof_ref(array, pos);
}

/**
 * Removes the last element without copying it.
 * The array must not be empty. The removed slot is cleared.
 */
tommy_inline void tommy_arrayblkof_remove_tail(tommy_arrayblkof* array)
{
	assert(array->count != 0);
	tommy_arrayblkof_resize(array, array->count - 1);
}

/**
 * Checks whether the array is empty.
 */
tommy_inline tommy_bool_t tommy_arrayblkof_empty(tommy_arrayblkof* array)
{
	return array->count == 0;
}

/**
 * Gets the initialized size of the array.
 */
tommy_inline tommy_size_t tommy_arrayblkof_size(tommy_arrayblkof* array)
{
	return array->count;
}

/**
 * Gets the number of elements that fit without further allocation.
 */
tommy_inline tommy_size_t tommy_arrayblkof_capacity(tommy_arrayblkof* array)
{
	return array->block_count * TOMMY_ARRAYBLKOF_SIZE;
}

/**
 * Exchanges two initialized arrays without copying their elements.
 * Element references remain valid and belong to the other array.
 * Passing the same array twice has no effect.
 */
tommy_inline void tommy_arrayblkof_swap(tommy_arrayblkof* first, tommy_arrayblkof* second)
{
	/* keep record size with the storage it describes */
	tommy_arrayblkof tmp = *first;
	*first = *second;
	*second = tmp;
}

/**
 * Calls the specified function for each element in the array.
 *
 * You cannot add or remove elements, nor change the size of the array,
 * from inside the callback.
 *
 * \param array Array to iterate.
 * \param func Function to call with each element.
 */
TOMMY_API void tommy_arrayblkof_foreach(tommy_arrayblkof* array, tommy_foreach_func* func);

/**
 * Calls the specified function with an argument for each element in the array.
 *
 * \param array Array to iterate.
 * \param func Function to call with each element.
 * \param arg Argument to pass to the function.
 */
TOMMY_API void tommy_arrayblkof_foreach_arg(tommy_arrayblkof* array, tommy_foreach_arg_func* func, void* arg);

/**
 * Gets the size of allocated memory.
 */
TOMMY_API tommy_size_t tommy_arrayblkof_memory_usage(tommy_arrayblkof* array);

#endif

