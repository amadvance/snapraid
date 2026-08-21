// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2013 Andrea Mazzoleni

/* Full sanity test for the RAID library */

#include "internal.h"
#include "test.h"
#include "cpu.h"

#include <stdio.h>
#include <stdlib.h>

/*
 * Size of the blocks to test.
 */
#define TEST_SIZE 256

/**
 * Number of disks in the long parity test.
 */
#ifdef COVERAGE
#define TEST_COUNT 10
#else
#define TEST_COUNT 32
#endif

int main(void)
{
	printf("Full sanity test for the RAID Cauchy library\n\n");

	raid_init();

#ifdef CONFIG_NEON
	printf("Including ARM NEON\n");
#endif
#ifdef CONFIG_NEON32
	printf("Including ARM NEON\n");
#endif
#ifdef CONFIG_X86
	if (raid_cpu_has_sse2())
		printf("Including x86 SSE2\n");
	if (raid_cpu_has_ssse3())
		printf("Including x86 SSSE3\n");
	if (raid_cpu_has_avx2())
		printf("Including x86 AVX2\n");
#ifdef CONFIG_X86_64
	if (raid_cpu_has_avx512bw())
		printf("Including x86 AVX512BW\n");
	if (raid_cpu_has_avx2gfni())
		printf("Including x86 AVX2GFNI\n");
	if (raid_cpu_has_avx512gfni())
		printf("Including x86 AVX512GFNI\n");
#endif
#endif

	printf("\nPlease wait about 60 seconds...\n\n");

	printf("Test sorting...\n");
	if (raid_test_sort() != 0) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}

	printf("Test insertion...\n");
	if (raid_test_insert() != 0) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}

	printf("Test combinations/permutations...\n");
	if (raid_test_combo() != 0) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}

	printf("Test GFNI affine matrices...\n");
	if (raid_test_gfaffine() != 0) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}

	printf("Test Vandermonde RAID parity generation with %u data disks...\n", RAID_DATA_MAX);
	if (raid_test_par(RAID_MODE_VANDERMONDE_RAID, RAID_DATA_MAX, TEST_SIZE) != 0) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}

	printf("Test Cauchy RAID parity generation with 1-%u data disks...\n", RAID_DATA_MAX);
	for (int i = 1; i <= RAID_DATA_MAX; ++i) {
		if (raid_test_par(RAID_MODE_CAUCHY_RAID, i, TEST_SIZE) != 0) {
			/* LCOV_EXCL_START */
			goto bail;
			/* LCOV_EXCL_STOP */
		}
	}
	printf("Test Cauchy AES parity generation with 1-%u data disks...\n", RAID_DATA_MAX);
	for (int i = 1; i <= RAID_DATA_MAX; ++i) {
		if (raid_test_par(RAID_MODE_CAUCHY_AES, i, TEST_SIZE) != 0) {
			/* LCOV_EXCL_START */
			goto bail;
			/* LCOV_EXCL_STOP */
		}
	}

	printf("Test Vandermonde RAID tail recovering with 1-%u data disks...\n", RAID_DATA_MAX);
	if (raid_test_tail(RAID_MODE_VANDERMONDE_RAID, RAID_DATA_MAX, TEST_SIZE) != 0) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}

	printf("Test Cauchy RAID tail recovering with 1-%u data disks...\n", RAID_DATA_MAX);
	if (raid_test_tail(RAID_MODE_CAUCHY_RAID, RAID_DATA_MAX, TEST_SIZE) != 0) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}

	printf("Test Cauchy AES tail recovering with 1-%u data disks...\n", RAID_DATA_MAX);
	if (raid_test_tail(RAID_MODE_CAUCHY_AES, RAID_DATA_MAX, TEST_SIZE) != 0) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}

	printf("Test Vandermonde RAID recovering with all combinations of %u data and 3 parity blocks...\n", TEST_COUNT);
	if (raid_test_rec(RAID_MODE_VANDERMONDE_RAID, TEST_COUNT, TEST_SIZE) != 0) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}


	printf("Test Cauchy RAID recovering with all combinations of %u data and 6 parity blocks...\n", TEST_COUNT);
	if (raid_test_rec(RAID_MODE_CAUCHY_RAID, TEST_COUNT, TEST_SIZE) != 0) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}


	printf("Test Cauchy AES recovering with all combinations of %u data and 6 parity blocks...\n", TEST_COUNT);
	if (raid_test_rec(RAID_MODE_CAUCHY_AES, TEST_COUNT, TEST_SIZE) != 0) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}

	printf("OK\n");
	return 0;

bail:
	/* LCOV_EXCL_START */
	printf("FAILED!\n");
	exit(EXIT_FAILURE);
	/* LCOV_EXCL_STOP */
}
