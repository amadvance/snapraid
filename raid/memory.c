// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2013 Andrea Mazzoleni

#include "internal.h"
#include "cpu.h"
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

void raid_mcache_flush(void *ptr, size_t size)
{
#ifdef CONFIG_X86
	uint8_t *p = ptr;
	size_t cache_line_size;
	size_t offset;
	int has_clflushopt;

	/* MFENCE is used to complete CLFLUSHOPT before the caller reads again. */
	if (size == 0 || !raid_cpu_has_sse2() || !raid_cpu_has_clflush())
		return;

	cache_line_size = raid_cpu_clflush_size();
	if (cache_line_size == 0)
		return;

	has_clflushopt = raid_cpu_has_clflushopt();
	if (has_clflushopt) {
		for (offset = 0; offset < size; offset += cache_line_size)
			asm volatile ("clflushopt %0" : "+m" (*(volatile uint8_t *)(p + offset)));
		asm volatile ("clflushopt %0" : "+m" (*(volatile uint8_t *)(p + size - 1)));
	} else {
		for (offset = 0; offset < size; offset += cache_line_size)
			asm volatile ("clflush %0" : "+m" (*(volatile uint8_t *)(p + offset)));
		asm volatile ("clflush %0" : "+m" (*(volatile uint8_t *)(p + size - 1)));
	}

	/* Cache invalidation must be globally visible before returning. */
	asm volatile ("mfence" : : : "memory");
#else
	(void)ptr;
	(void)size;
#endif
}

static uintptr_t raid_mtest_address_pattern(const void *ptr)
{
	uintptr_t value = (uintptr_t)ptr;

	/* Mix every address bit while retaining a one-to-one word mapping. */
#if UINTPTR_MAX > 0xffffffffU
	value ^= value >> 33;
	value *= (uintptr_t)0xff51afd7ed558ccdULL;
	value ^= value >> 33;
	value *= (uintptr_t)0xc4ceb9fe1a85ec53ULL;
	value ^= value >> 33;
#else
	value ^= value >> 17;
	value *= (uintptr_t)0xed5ad4bbU;
	value ^= value >> 11;
	value *= (uintptr_t)0xac4c1b51U;
	value ^= value >> 15;
#endif

	return value;
}

/**
 * Compare memory against a repeated byte value.
 *
 * Return <0 if ptr is less than value, 0 if equal, >0 if greater.
 */
static __always_inline int raid_membcmp(const void *ptr, uint8_t value, size_t size)
{
	const uint8_t *p = ptr;

	if (size == 0)
		return 0;
	if (p[0] != value)
		return (int)p[0] - (int)value;

	/*
	 * If the buffer contains value up to index k-1, then at offset k-1
	 * memcmp compares (p + 1)[k - 1] = p[k] against p[k - 1] = value.
	 * The first differing adjacent pair yields the exact sign (p[k] - value).
	 */
	return memcmp(p + 1, p, size - 1);
}

int raid_mtest_vector(int n, size_t size, void **vv)
{
	static const uint8_t pattern[] = {
		0x00, 0xff, /* solid bits: minimum vs maximum cell charge and VDD droop */
		0x55, 0xaa, /* 1-bit alternate: worst-case crosstalk between adjacent lines */
		0x33, 0xcc, /* 2-bit alternate: coupling across line pairs (00110011 / 11001100) */
		0x0f, 0xf0  /* 4-bit alternate: coupling across nibbles */
	};
	uint8_t **v = (uint8_t **)vv;
	size_t remainder = size % sizeof(uintptr_t);
	size_t aligned_size = size - remainder;

	/*
	 * Complementary pairs exercise minimum/maximum cell charge and
	 * alternating bit coupling across 1-bit, 2-bit, and 4-bit pin strides.
	 */
	for (size_t k = 0; k < sizeof(pattern) / sizeof(pattern[0]); ++k) {
		uint8_t d = pattern[k];

		for (int i = 0; i < n; ++i)
			memset(v[i], d, size);

		for (int i = 0; i < n; ++i)
			raid_mcache_flush(v[i], size);

		for (int i = 0; i < n; ++i) {
			if (raid_membcmp(v[i], d, size) != 0) {
				/* LCOV_EXCL_START */
				return -1;
				/* LCOV_EXCL_STOP */
			}
		}
	}

	/*
	 * Equal byte patterns cannot reveal two memory locations aliasing each
	 * other. Give every word a value derived from its address, then repeat
	 * the verification with the complement to exercise both bit values.
	 */
	for (int i = 0; i < n; ++i) {
		uint8_t *p = v[i];
		uint8_t *limit = p + aligned_size;
		while (p < limit) {
			uintptr_t value = raid_mtest_address_pattern(p);
			memcpy(p, &value, sizeof(uintptr_t));
			p += sizeof(uintptr_t);
		}
		if (remainder) {
			uintptr_t value = raid_mtest_address_pattern(p);
			memcpy(p, &value, remainder);
		}

		raid_mcache_flush(v[i], size);
	}

	/*
	 * Verify forward pass: confirm that the initial address patterns are
	 * intact across all buffers, proving no cross-buffer aliasing occurred.
	 * Replace each word with its bitwise complement in-place to test bit
	 * transition faults (0 -> 1 and 1 -> 0) and catch stuck-at bits.
	 */
	for (int i = 0; i < n; ++i) {
		uint8_t *p = v[i];
		uint8_t *limit = p + aligned_size;
		while (p < limit) {
			uintptr_t value = raid_mtest_address_pattern(p);
			uintptr_t actual;
			memcpy(&actual, p, sizeof(uintptr_t));
			if (actual != value) {
				/* LCOV_EXCL_START */
				return -1;
				/* LCOV_EXCL_STOP */
			}
			value = ~value;
			memcpy(p, &value, sizeof(uintptr_t));
			p += sizeof(uintptr_t);
		}
		if (remainder) {
			uintptr_t value = raid_mtest_address_pattern(p);
			if (memcmp(p, &value, remainder) != 0) {
				/* LCOV_EXCL_START */
				return -1;
				/* LCOV_EXCL_STOP */
			}
			value = ~value;
			memcpy(p, &value, remainder);
		}

		raid_mcache_flush(v[i], size);
	}

	/*
	 * Verify backward pass: traverse memory and buffers in strictly descending
	 * order to detect directional coupling faults and address-line bridging.
	 * Confirm that the inverted patterns survived the cache flush, then
	 * restore the zero-filled state required by callers.
	 */
	for (int i = n - 1; i >= 0; --i) {
		uint8_t *p = v[i] + aligned_size;
		uint8_t *limit = v[i];
		if (remainder) {
			uintptr_t value = ~raid_mtest_address_pattern(p);
			if (memcmp(p, &value, remainder) != 0) {
				/* LCOV_EXCL_START */
				return -1;
				/* LCOV_EXCL_STOP */
			}
			memset(p, 0, remainder);
		}
		while (p > limit) {
			p -= sizeof(uintptr_t);
			uintptr_t value = ~raid_mtest_address_pattern(p);
			uintptr_t actual;
			memcpy(&actual, p, sizeof(uintptr_t));
			if (actual != value) {
				/* LCOV_EXCL_START */
				return -1;
				/* LCOV_EXCL_STOP */
			}
			memset(p, 0, sizeof(uintptr_t));
		}
	}

	return 0;
}
