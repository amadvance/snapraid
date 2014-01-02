/*
 * Copyright (C) 2013 Andrea Mazzoleni
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __RAID_MEMORY_H
#define __RAID_MEMORY_H

/**
 * Memory alignment provided by raid_malloc_align().
 *
 * It should guarantee good cache performance everywhere.
 */
#define RAID_MALLOC_ALIGN 256

/**
 * Memory displacement to avoid cache collisions on contiguous blocks,
 * used by raid_malloc_vector().
 *
 * When allocating a sequence of blocks with a size of power of 2,
 * there is the risk that the start of each block is mapped into the same
 * cache line or prefetching prediction, resulting in collisions if you
 * access all the blocks in parallel, from the start to the end.
 *
 * The selected value was choosen empirically with some speed tests
 * with 8/12/16/20/24 data buffers of 256 KB.
 *
 * These are the results with no displacement:
 *
 *            sse2
 *    par1   15368 [MB/s]
 *    par2    6814 [MB/s]
 *    parz    3033 [MB/s]
 *
 * These are the results with displacement resulting in improvments
 * in the order of 20% or more:
 *
 *            sse2
 *    par1   21936 [MB/s]
 *    par2   11902 [MB/s]
 *    parz    5838 [MB/s]
 *
 */
#define RAID_MALLOC_DISPLACEMENT (7*256)

/**
 * Aligned malloc.
 */
void *raid_malloc_align(size_t size, void **freeptr);

/**
 * Aligned vector allocation.
 * Returns a vector of ::n pointers, each one pointing to a block of
 * the specified ::size.
 * The first ::nd elements are reversed in order.
 */
void **raid_malloc_vector(int nd, int n, size_t size, void **freeptr);

/**
 * Fills the memory vector with random data.
 */
void raid_mrand_vector(int n, size_t size, void **vv);

/**
 * Tests the memory vector for RAM problems.
 * If a problem is found, it crashes.
 */
int raid_mtest_vector(int n, size_t size, void **vv);

#endif

