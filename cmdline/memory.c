// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2011 Andrea Mazzoleni

#include "os/portable.h"

#include "support.h"
#include "memory.h"

/****************************************************************************/
/* memory */

/**
 * Cumulative amount of memory allocated.
 */
static size_t mcounter;

size_t malloc_counter_get(void)
{
	size_t ret;

	lock_memory();

	ret = mcounter;

	unlock_memory();

	return ret;
}

static void malloc_counter_inc(size_t inc)
{
	lock_memory();

	mcounter += inc;

	unlock_memory();
}

/* LCOV_EXCL_START */
static ssize_t malloc_print(int f, const char* str)
{
	ssize_t len = 0;

	while (str[len])
		++len;
	return write(f, str, len);
}
/* LCOV_EXCL_STOP */

/* LCOV_EXCL_START */
static ssize_t malloc_printn(int f, size_t value)
{
	char buf[32];
	int i;

	if (!value)
		return write(f, "0", 1);

	i = sizeof(buf);
	while (value) {
		buf[--i] = (value % 10) + '0';
		value /= 10;
	}

	return write(f, buf + i, sizeof(buf) - i);
}
/* LCOV_EXCL_STOP */

/* LCOV_EXCL_START */
void malloc_fail(size_t size)
{
	/* don't use printf to avoid any possible extra allocation */
	int f = 2; /* stderr */

	malloc_print(f, "Failed for Low Memory!\n");
	malloc_print(f, "Allocating ");
	malloc_printn(f, size);
	malloc_print(f, " bytes.\n");
	malloc_print(f, "Already allocated ");
	malloc_printn(f, malloc_counter_get());
	malloc_print(f, " bytes.\n");
	if (sizeof(void*) == 4) {
		malloc_print(f, "You are currently using a 32 bits executable.\n");
		malloc_print(f, "If you have more than 4GB of memory, please upgrade to a 64 bits one.\n");
	}
}
/* LCOV_EXCL_STOP */

void* malloc_nofail(size_t size)
{
	void* ptr = malloc(size ? size : 1);

	if (!ptr) {
		/* LCOV_EXCL_START */
		malloc_fail(size);
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

#ifndef CHECKER /* Don't preinitialize when running for valgrind */
	/*
	 * Here we preinitialize the memory to ensure that the OS is really allocating it
	 * and not only reserving the addressable space.
	 * Otherwise we are risking that the OOM (Out Of Memory) killer in Linux will kill the process.
	 * Filling the memory doesn't ensure to disable OOM, but it increase a lot the chances to
	 * get a real error from malloc() instead than a process killed.
	 * Note that calloc() doesn't have the same effect.
	 */
	memset(ptr, 0xA5, size);
#endif

	malloc_counter_inc(size);

	return ptr;
}

void* nalloc_nofail(size_t count, size_t size)
{
	if (count > SIZE_MAX / size) {
		log_fatal(EINTERNAL, "Allocation size overflow\n");
		os_abort();
	}

	size *= count;

	/* see the note in malloc_nofail() of why we don't use calloc() */
	void* ptr = malloc(size ? size : 1);

	if (!ptr) {
		/* LCOV_EXCL_START */
		malloc_fail(size);
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	malloc_counter_inc(size);

	return ptr;
}

void* calloc_nofail(size_t count, size_t size)
{
	void* ptr = nalloc_nofail(count, size);

	memset(ptr, 0, count * size);

	return ptr;
}

char* strdup_nofail(const char* str)
{
	size_t size = strlen(str) + 1;

	char* ptr = malloc_nofail(size);

	memcpy(ptr, str, size);

	return ptr;
}

