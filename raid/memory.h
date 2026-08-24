// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2013 Andrea Mazzoleni

#ifndef __RAID_MEMORY_H
#define __RAID_MEMORY_H

/**
 * Memory allocation and benchmark layout helpers.
 *
 * This module provides optional utility functions to allocate SIMD-aligned and
 * cache-displaced memory buffers for test suites, benchmarks, and Direct I/O.
 * The core RAID engine (raid.h) remains zero-allocation and operates on
 * preallocated buffers.
 *
 * Like other internal helpers in this library, these functions intentionally do
 * not validate against arithmetic overflow. Callers are responsible for passing
 * reasonable and bounded arguments.
 */

/**
 * Memory alignment provided by raid_malloc().
 *
 * It should guarantee good cache performance everywhere.
 */
#define RAID_MALLOC_ALIGN 256

/*
 * Memory displacement to color buffer starts across L1 cache sets, used by
 * raid_malloc_vector().
 *
 * On the intended x86 L1 data cache layout, cache lines are 64 bytes and there
 * are 64 sets. Contiguous buffers whose size is a multiple of 4096 bytes have
 * the same L1 set index at corresponding offsets. Parallel SIMD streams can
 * then contend for the same set, reducing the useful prefetch distance and
 * causing conflict misses.
 *
 * A displacement of X * 64 bytes changes the starting set index of each
 * buffer. The cycle of distinct starting sets is 64 / gcd(X, 64).
 *
 * The multiplier X scales with the number of allocated buffers (n):
 * - n <= 8  -> 24 * 64: Cycle of 8.
 * - n <= 16 -> 28 * 64: Cycle of 16.
 * - n <= 32 -> 30 * 64: Cycle of 32.
 * - n > 32  -> 33 * 64: Cycle of 64.
 *
 * The 64-set cycle gives distinct starting set indexes for up to 64 buffers;
 * for larger vectors the starting set indexes repeat. It reduces, but does not
 * eliminate, all possible L1 cache conflicts.
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
 * These are the results with the cache-line displacement, demonstrating
 * throughput improvements in the order of 20% or more. They were measured on
 * a machine without a two-dimensional prefetcher, where STRIDE_NOISE had no
 * material effect. They therefore measure the fixed displacement alone:
 *
 * RAID functions used for computing the parity with 'sync':
 *             best    int8   int32   int64    sse2   sse2e   ssse3  ssse3e    avx2   avx2e
 *     gen1    avx2           18550   34501   62950                           78335
 *     gen2    avx2            5410   10080   27334   28502                   46921
 *     genz   avx2e            3161    5083   14741   14704                           26404
 *     gen3   avx2e    1296                                   12752   13902           26446
 *     gen4   avx2e     975                                    9757   10893           20598
 *     gen5   avx2e     799                                    7879    8612           16613
 *     gen6   avx2e     663                                    6386    7283           13987
 */
unsigned raid_optimal_displacement(int n);

/**
 * Aligned malloc with default RAID alignment (256 bytes).
 *
 * Stores the base allocation address in @freeptr to be passed to free().
 */
void *raid_malloc(size_t size, void **freeptr);

/**
 * Arbitrary aligned malloc.
 *
 * Allocates a buffer of @size bytes aligned to @align_size bytes.
 * Stores the base allocation address in @freeptr to be passed to free().
 */
void *raid_malloc_align(size_t size, size_t align_size, void **freeptr);

/**
 * Aligned vector allocation.
 *
 * Allocates an array of @n pointers, each one pointing to a block of
 * the specified @size with optimal L1 displacement and stride perturbation.
 *
 * Freeing requires two calls: free(*freeptr) for the data buffer,
 * and free(v) for the pointer vector.
 */
void **raid_malloc_vector(int n, size_t size, void **freeptr);

/**
 * Arbitrary aligned vector allocation.
 *
 * Freeing requires two calls: free(*freeptr) for the data buffer,
 * and free(v) for the pointer vector.
 */
void **raid_malloc_vector_align(int n, size_t size, size_t align_size, ssize_t displacement_size, ssize_t wrap_size, void **freeptr);

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
