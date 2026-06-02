// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2013 Andrea Mazzoleni

#include "internal.h"
#include "memory.h"

void *raid_malloc_align(size_t size, size_t align_size, void **freeptr)
{
	unsigned char *ptr;
	uintptr_t offset;

	ptr = malloc(size + align_size);
	if (!ptr) {
		/* LCOV_EXCL_START */
		return 0;
		/* LCOV_EXCL_STOP */
	}

	*freeptr = ptr;

	offset = ((uintptr_t)ptr) % align_size;

	if (offset != 0)
		ptr += align_size - offset;

	return ptr;
}

void *raid_malloc(size_t size, void **freeptr)
{
	return raid_malloc_align(size, RAID_MALLOC_ALIGN, freeptr);
}

unsigned raid_optimal_displacement(int n)
{
	if (n <= 8)
		return 24 * 64;
	if (n <= 16)
		return 28 * 64;
	if (n <= 32)
		return 30 * 64;
	return 33 * 64;
}

/*
 * The 4096 bytes represents a full 64-set * 64-byte L1 cache cycle.
 */
#define L1_WRAP_SIZE 4096

/*
 * PREFETCHER MITIGATION VIA PAGE-OFFSET RANDOMIZATION (WAY-SHIFTING)
 *
 * STRIDE_NOISE is a sequence of small, non-linear multipliers used to inject
 * variable byte offsets into the physical distance between consecutive disk
 * buffers during allocation. These multipliers are applied in 4096-byte chunks.
 *
 * The parity generation loops contain a massive cross-disk jump in the inner
 * loop. If the distance between every disk is a constant delta (e.g., exactly
 * 256KB + fixed displacement bytes), modern IP-based stride prefetchers
 * (particularly on architectures like AMD Zen 5) will easily recognize the pattern.
 *
 * Because the prefetcher is biased to aggressively accelerate constant positive
 * strides, it incorrectly assumes the program is executing a massive linear
 * sweep of memory. It begins flooding the memory controller with speculative,
 * useless requests for data far beyond the active disks. This "garbage traffic"
 * saturates the memory bus, starving the AVX/SIMD execution engine of
 * the actual data it needs and crippling throughput.
 *
 * We must blind the prefetcher by making the stride unpredictable, but we CANNOT
 * alter the carefully calculated L1 cache set mapping (raid_optimal_displacement),
 * otherwise we will cause catastrophic L1 cache thrashing.
 *
 * - The standard x86 L1 data cache has 64 sets, and a cache line is 64 bytes.
 * A full wrap-around of the L1 cache is exactly 64 * 64 = 4096 bytes (which
 * also perfectly aligns with a standard 4KB memory page).
 *
 * - By adding a random multiple of 4096 bytes to our jump:
 * 1. The physical memory address changes wildly, breaking the constant stride.
 * 2. The modulo-64 cache math is completely unaffected ((X + 4096) % 64 == X % 64).
 *
 * The IP-based stride prefetcher monitors the cross-disk jumps and sees a
 * chaotic sequence of massive, fluctuating deltas (e.g., +275KB, +267KB, +288KB).
 * Because the stride is never constant, the prefetcher's internal confidence
 * counter never builds up. The prefetcher safely disables itself for the inner
 * loop.
 *
 * With the noisy prefetcher silenced, the memory bus is cleared. The CPU's L2
 * stream prefetcher is left alone to perfectly track the independent +64 byte
 * forward reads for each individual disk. The AVX/SIMD execution engine receives
 * 100% of the bandwidth, yielding maximum physical throughput.
 *
 * These are the results in MB/s with no stride noise on a Zen 5 CPU:
 *
 * RAID functions used for computing the parity with 'sync':
 *             best    int8   int32   int64    sse2   sse2e   ssse3  ssse3e    avx2   avx2e  avx512    gfni gfni512
 *     gen1  avx512           48947   75608   26465                           70482           73848
 *     gen2  avx512            9989   19702   28144   34140                   42712           22374   30712   68590
 *     genz   avx2e            5935   11653   17763   19398                           38789
 *     gen3   avx2e    2214                                   20620   23859           33947   24109   20045   31187
 *     gen4   avx2e    1622                                   13843   17071           28194   25679   18826   20780
 *     gen5   avx2e    1360                                   11775   12980           20285   20377   17509   16149
 *     gen6   avx2e    1133                                    8478   11189           19951   16831   18965   13912
 *
 * These are the results in MB/s with stride noise on a Zen 5 CPU:
 *
 * RAID functions used for computing the parity with 'sync':
 *             best    int8   int32   int64    sse2   sse2e   ssse3  ssse3e    avx2   avx2e  avx512    gfni gfni512
 *     gen1  avx512           48161   88091  110896                          122684          121917
 *     gen2  avx512            9970   19776   43223   45989                   86221           68418  110524  112067
 *     genz   avx2e            5953   11730   18598   19475                           39411
 *     gen3   avx2e    2219                                   21784   24347           49416   43295   95555  100458
 *     gen4   avx2e    1624                                   14216   17408           38422   29275   73658   84268
 *     gen5   avx2e    1361                                   12369   13585           28328   22052   63672   68036
 *     gen6   avx2e    1131                                    8399   11692           23918   17703   53620   56806
 */
static const unsigned STRIDE_NOISE[16] = {
	0, 3, 1, 6, 2, 5, 7, 4,
	1, 4, 0, 7, 3, 6, 2, 5
};

void **raid_malloc_vector_align(int n, size_t size, size_t align_size, ssize_t displacement_size, void **freeptr)
{
	void **v;
	unsigned char *va;
	int i;

	BUG_ON(n <= 0);

	v = malloc(n * sizeof(void *));
	if (!v) {
		/* LCOV_EXCL_START */
		return 0;
		/* LCOV_EXCL_STOP */
	}

	/*
	 * The allocated buffer must safely hold the disk chunks, the L1 fixed displacement,
	 * and the variable STRIDE_NOISE. Because the maximum noise multiplier in the array
	 * is 7, reserving 8 * L1_WRAP_SIZE per disk guarantees the pointer will never overflow
	 * the allocated memory block.
	 */
	va = raid_malloc_align(n * (size + displacement_size + 8 * L1_WRAP_SIZE), align_size, freeptr);
	if (!va) {
		/* LCOV_EXCL_START */
		free(v);
		return 0;
		/* LCOV_EXCL_STOP */
	}

	for (i = 0; i < n; ++i) {
		v[i] = va;

		/* move past the active disk buffer */
		va += size;

		/* apply the optimal L1 cache spacing */
		va += displacement_size;

		/* inject the variable noise multiplier to blind the stride prefetcher */
		va += STRIDE_NOISE[i % 16] * L1_WRAP_SIZE;
	}

	return v;
}

void **raid_malloc_vector(int n, size_t size, void **freeptr)
{
	return raid_malloc_vector_align(n, size, RAID_MALLOC_ALIGN, raid_optimal_displacement(n), freeptr);
}

void raid_mrand_vector(unsigned seed, int n, size_t size, void **vv)
{
	unsigned char **v = (unsigned char **)vv;
	int i;
	size_t j;

	for (i = 0; i < n; ++i)
		for (j = 0; j < size; ++j) {
			/* basic C99/C11 linear congruential generator */
			seed = seed * 1103515245U + 12345U;

			v[i][j] = seed >> 16;
		}
}

int raid_mtest_vector(int n, size_t size, void **vv)
{
	unsigned char **v = (unsigned char **)vv;
	int i;
	size_t j;
	unsigned k;
	unsigned char d;
	unsigned char p;

	/* fill with 0 */
	d = 0;
	for (i = 0; i < n; ++i)
		for (j = 0; j < size; ++j)
			v[i][j] = d;

	/* test with all the byte patterns */
	for (k = 1; k < 256; ++k) {
		p = d;
		d = k;

		/* forward fill */
		for (i = 0; i < n; ++i) {
			for (j = 0; j < size; ++j) {
				if (v[i][j] != p) {
					/* LCOV_EXCL_START */
					return -1;
					/* LCOV_EXCL_STOP */
				}
				v[i][j] = d;
			}
		}

		p = d;
		d = ~p;
		/* backward fill with complement */
		for (i = 0; i < n; ++i) {
			for (j = size; j > 0; --j) {
				if (v[i][j - 1] != p) {
					/* LCOV_EXCL_START */
					return -1;
					/* LCOV_EXCL_STOP */
				}
				v[i][j - 1] = d;
			}
		}
	}

	return 0;
}
