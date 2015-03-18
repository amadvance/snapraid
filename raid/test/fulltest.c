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

#ifdef CONFIG_X86
	if (raid_cpu_has_sse2())
		printf("Including x86 SSE2 functions\n");
	if (raid_cpu_has_ssse3())
		printf("Including x86 SSSE3 functions\n");
	if (raid_cpu_has_avx2())
		printf("Including x86 AVX2 functions\n");
#endif
#ifdef CONFIG_X86_64
	printf("Including x64 extended SSE register set\n");
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

	printf("Test Cauchy parity generation with %u data disks...\n", RAID_DATA_MAX);
	if (raid_test_par(RAID_MODE_CAUCHY, RAID_DATA_MAX, TEST_SIZE) != 0) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}

	printf("Test Cauchy parity generation with 1 data disk...\n");
	if (raid_test_par(RAID_MODE_CAUCHY, 1, TEST_SIZE) != 0) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}

	printf("Test Cauchy recovering with all combinations of %u data and 6 parity blocks...\n", TEST_COUNT);
	if (raid_test_rec(RAID_MODE_CAUCHY, TEST_COUNT, TEST_SIZE) != 0) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}

	printf("Test Vandermonde parity generation with %u data disks...\n", RAID_DATA_MAX);
	if (raid_test_par(RAID_MODE_VANDERMONDE, RAID_DATA_MAX, TEST_SIZE) != 0) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}

	printf("Test Vandermonde recovering with all combinations of %u data and 3 parity blocks...\n", TEST_COUNT);
	if (raid_test_rec(RAID_MODE_VANDERMONDE, TEST_COUNT, TEST_SIZE) != 0) {
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

