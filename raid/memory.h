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

/*
 * Memory displacement to avoid L1 cache set aliasing and 4K aliasing penalties
 * on contiguous blocks, used by raid_malloc_vector().
 *
 * When allocating a sequence of disk buffers with a size that is a large
 * power of 2 (or multiples of 4KB/2MB), their starting addresses naturally
 * map to the exact same sets in the L1 Data Cache. If these blocks are
 * accessed in parallel during SIMD parity generation, the memory streams
 * will constantly evict each other, resulting in severe cache thrashing.
 * * Furthermore, if buffers are separated by exact multiples of 4096 bytes,
 * modern CPUs suffer a "4K Aliasing" penalty. The memory disambiguation unit
 * misidentifies the lower 12-bits of the addresses as a match, attempting
 * invalid Store-to-Load Forwarding which results in a ~20-cycle pipeline flush.
 *
 * To eliminate both bottlenecks, each disk buffer is allocated with a dynamic
 * displacement of (X * 64 bytes). The optimal 'X' balances two hardware rules:
 * 1. L1 Capacity: The cycle of utilized L1 sets must be >= the number of disks.
 * 2. 4K Aliasing: 'X' should be as close to 32 (2048 bytes) as possible to
 * maximize the distance from 4096-byte boundaries.
 *
 * The multiplier 'X' scales dynamically based on the disk count (nd):
 * - nd <= 8  -> 24 * 64: Cycle of 8. Safest distance from 4K boundaries.
 * - nd <= 16 -> 28 * 64: Cycle of 16. Widely disperses memory streams across
 * the cache, preventing hardware prefetcher clustering.
 * - nd <= 32 -> 30 * 64: Cycle of 32. Tighter L1 packing.
 * - nd <= 64 -> 33 * 64: Cycle of 64. 33 and 64 are coprime, guaranteeing up
 * to 64 streams never collide in L1 or trigger 4K stalls.
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
 * These are the results with a strategic cache line displacement,
 * demonstrating throughput improvements in the order of 20% or more:
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
void **raid_malloc_vector_align(int nd, int n, size_t size, size_t align_size, ssize_t displacement_size, void **freeptr);

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
