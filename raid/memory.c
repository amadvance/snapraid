// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2013 Andrea Mazzoleni

#include "internal.h"
#include "memory.h"

void *raid_malloc_align(size_t size, size_t align_size, void **freeptr)
{
	uint8_t *ptr;
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
#define RAID_WRAP_SIZE 4096

/*
 * PREFETCHER MITIGATION WITH 4K STRIDE PERTURBATION
 *
 * STRIDE_NOISE is a sequence of small, non-linear multipliers used to add
 * variable 4096-byte increments to the virtual distance between consecutive
 * disk buffers during allocation.
 *
 * The parity generation loops repeatedly access corresponding offsets in each
 * disk buffer. If the distance between buffers is constant, the inner loop
 * produces a regular cross-buffer stride in addition to the unit-stride access
 * within each buffer. A two-dimensional prefetcher can recognize this access
 * pattern as a regular grid of disk buffers and offsets.
 *
 * This additional prefetching is not necessarily useful. The cross-buffer
 * accesses already form independent forward streams, and excessive look-ahead
 * can cause cache pollution or pressure on cache-fill and memory-request
 * resources.
 *
 * raid_optimal_displacement() provides the fixed displacement that separates
 * buffer starts among L1 cache sets. This cache-set separation is the most
 * likely cause of the broad performance improvement over contiguous buffers.
 * STRIDE_NOISE is an additional perturbation and must retain that mapping.
 *
 * On common x86 L1 data caches there are 64 sets and cache lines are 64 bytes.
 * A full set-index cycle is therefore 64 * 64 = 4096 bytes, which also matches
 * the usual memory-page size.
 *
 * By adding a varying multiple of 4096 bytes to the buffer spacing:
 *
 * 1. The virtual cross-buffer stride varies instead of remaining constant.
 * 2. The L1 set index is unchanged because adding 4096 bytes preserves address
 *    bits 6 through 11.
 *
 * STRIDE_NOISE is deterministic, not random. Its non-uniform sequence can make
 * the cross-buffer pattern less suitable for stride-based prediction while
 * leaving the sequential access pattern of each buffer unchanged.
 *
 * With the fixed displacement already applied, STRIDE_NOISE had no material
 * effect on the tested Intel processor. On Zen 5, which has a two-dimensional
 * prefetcher, disabling it caused a large throughput reduction. This points to
 * two-dimensional prefetching as the likely trigger, rather than to an Intel
 * or AMD vendor difference.
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

void **raid_malloc_vector_align(int n, size_t size, size_t align_size, size_t displacement_size, size_t wrap_size, void **freeptr)
{
	void **v;
	uint8_t *va;
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
	 * is 7, reserving 8 * RAID_WRAP_SIZE per disk guarantees the pointer will never overflow
	 * the allocated memory block.
	 */
	va = raid_malloc_align(n * (size + displacement_size + 8 * wrap_size), align_size, freeptr);
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
		va += STRIDE_NOISE[i % 16] * wrap_size;
	}

	return v;
}

void **raid_malloc_vector(int n, size_t size, void **freeptr)
{
	return raid_malloc_vector_align(n, size, RAID_MALLOC_ALIGN, raid_optimal_displacement(n), RAID_WRAP_SIZE, freeptr);
}

void raid_mrand_vector(unsigned seed, int n, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
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
	uint8_t **v = (uint8_t **)vv;
	int i;
	size_t j;
	unsigned k;
	uint8_t d;
	uint8_t p;

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
