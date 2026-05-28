// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2013 Andrea Mazzoleni

#ifndef __RAID_MEMORY_H
#define __RAID_MEMORY_H

/**
 * Memory alignment provided by raid_malloc().
 *
 * It should guarantee good cache performance everywhere.
 */
#define RAID_MALLOC_ALIGN 256

/**
 * Memory displacement to avoid cache address sharing on contiguous blocks,
 * used by raid_malloc_vector().
 *
 * When allocating a sequence of blocks with a size of power of 2,
 * there is the risk that the addresses of each block are mapped into the
 * same cache line and prefetching predictor, resulting in a lot of cache
 * sharing if you access all the blocks in parallel, from the start to the
 * end.
 *
 * To avoid this effect, it's better if all the blocks are allocated
 * with a fixed displacement trying to reduce the cache address sharing.
 *
 * The selected displacement was chosen empirically with some speed tests
 * with 8/12/16/20/24 data buffers of 256 KB.
 *
 * These are the results in MB/s with no displacement:
 *
 * RAID functions used for computing the parity with 'sync':
 *             best    int8   int32   int64    sse2   sse2e   ssse3  ssse3e    avx2   avx2e
 *     gen1    avx2           14389   26472   42343                           64085        
 *     gen2    avx2            3788    7309   20930   22042                   36916        
 *     genz   avx2e            2368    4262   11791   11786                           21770
 *     gen3   avx2e     809                                   11249   11913           21937
 *     gen4   avx2e     609                                    8857    9469           17401
 *     gen5   avx2e     488                                    7147    7465           14231
 *     gen6   avx2e     398                                    5828    6381           12196
 *
 * These are the results with displacement of 9*256, resulting in improvements
 * in the order of 20% or more:
 *
 *  RAID functions used for computing the parity with 'sync':
 *             best    int8   int32   int64    sse2   sse2e   ssse3  ssse3e    avx2   avx2e
 *     gen1    avx2           18435   34980   62786                           78159        
 *     gen2    avx2            5326   10026   26334   27349                   45012        
 *     genz   avx2e            3141    5037   14607   14099                           25757
 *     gen3   avx2e    1298                                   13289   14381           27563
 *     gen4   avx2e     974                                   10076   11213           21637
 *     gen5   avx2e     796                                    8091    8863           17458
 *     gen6   avx2e     663                                    6761    7634           14912
 */
#define RAID_MALLOC_DISPLACEMENT (7*256)

/**
 * Aligned malloc.
 * Use an alignment suitable for the raid functions.
 */
void *raid_malloc(size_t size, void **freeptr);

/**
 * Arbitrary aligned malloc.
 */
void *raid_malloc_align(size_t size, size_t align_size, void **freeptr);

/**
 * Aligned vector allocation.
 * Use an alignment suitable for the raid functions.
 * Returns a vector of @n pointers, each one pointing to a block of
 * the specified @size.
 * The first @nd elements are reversed in order.
 */
void **raid_malloc_vector(int nd, int n, size_t size, void **freeptr);

/**
 * Arbitrary aligned vector allocation.
 */
void **raid_malloc_vector_align(int nd, int n, size_t size, size_t align_size, size_t displacement_size, void **freeptr);

/**
 * Fills the memory vector with pseudo-random data based on the specified seed.
 */
void raid_mrand_vector(unsigned seed, int n, size_t size, void **vv);

/**
 * Tests the memory vector for RAM problems.
 * If a problem is found, it crashes.
 */
int raid_mtest_vector(int n, size_t size, void **vv);

#endif

