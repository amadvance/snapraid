// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2011 Andrea Mazzoleni

#ifndef __MEMORY_H
#define __MEMORY_H

/****************************************************************************/
/* memory */

/**
 * Return the size of the cumulative allocated memory.
 */
size_t malloc_counter_get(void);

/**
 * Safe malloc.
 * If no memory is available, it aborts.
 */
void* malloc_nofail(size_t size);

/**
 * Safe malloc with calloc calling arguments.
 * If no memory is available, it aborts.
 */
void* nalloc_nofail(size_t count, size_t size);

/**
 * Safe calloc.
 * If no memory is available, it aborts.
 */
void* calloc_nofail(size_t count, size_t size);

/**
 * Safe strdup.
 * If no memory is available, it aborts.
 */
char* strdup_nofail(const char* str);

/**
 * Helper for printing an error about a failed allocation.
 */
void malloc_fail(size_t size);

#endif

