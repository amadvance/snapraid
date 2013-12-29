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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
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
 * ued by raid_malloc_vector().
 *
 * When allocating a sequence of blocks with a size of power of 2,
 * there is the risk that the start of each block is mapped into the same
 * cache line, resulting in cache collisions if you access all the blocks
 * in parallel, from the start to the end.
 *
 * The selected value was choosen empirically with some speed tests
 * with 8/12/16/20/24 data buffers and 3 parity buffers
 * for RAID-5/6/TP computation.
 *
 * With displacement (8 buffers, icore5, 32 bit):
 *
 * PAR1 sse2x4 21936 [MB/s]
 * PAR2 sse2x2 11902 [MB/s]
 * PARz sse2x1 5838 [MB/s]
 *
 * Without displacement:
 *
 * PAR1 sse2x4 15368 [MB/s]
 * PAR2 sse2x2 6814 [MB/s]
 * PARz sse2x1 3033 [MB/s]
 */
#define RAID_MALLOC_DISPLACEMENT (7*256)

/**
 * Aligned malloc.
 */
void *raid_malloc_align(size_t size, void **freeptr);

/**
 * Aligned vector allocation.
 * Returns a vector of "count" pointers, each one pointing to a block of the
 * specified "size".
 * The first "reverse" pointers are put in reverse memory offset order.
 */
void **raid_malloc_vector(size_t reverse, size_t count, size_t size, void **freeptr);

/**
 * Tests the memory vector for RAM problems.
 * If a problem is found, it crashes.
 */
int raid_mtest_vector(void **vv, size_t count, size_t size);

/**
 * Fills the memory vector with random data.
 */
void raid_mrand_vector(void **vv, size_t count, size_t size);

#endif

