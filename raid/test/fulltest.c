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
#define TEST_COUNT 24
#endif

int main(int argc, char *argv[])
{
	int test_count = TEST_COUNT;
	int test_size = TEST_SIZE;

	if (argc > 1) {
		test_count = atoi(argv[1]);
		if (test_count < RAID_PARITY_MAX || test_count > RAID_DATA_MAX) {
			printf("Invalid TEST_COUNT %d (must be between %d and %d)\n",
				test_count, RAID_PARITY_MAX, RAID_DATA_MAX);
			return EXIT_FAILURE;
		}
	}

	if (argc > 2) {
		test_size = atoi(argv[2]);
		if (test_size <= 0 || (test_size % 64) != 0) {
			printf("Invalid TEST_SIZE %d (must be positive and multiple of 64)\n",
				test_size);
			return EXIT_FAILURE;
		}
	}

	printf("Full sanity test for the RAID library\n\n");

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

	if (test_count >= 32)
		printf("\nPlease wait about 60 seconds...\n\n");
	else
		printf("\nPlease wait...\n\n");

	/* array sorting helper */
	printf("Test sorting...\n");
	if (raid_test_sort() != 0) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}

	/* sorted array insertion helper */
	printf("Test insertion...\n");
	if (raid_test_insert() != 0) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}

	/* permutation and combination generators */
	printf("Test combinations/permutations...\n");
	if (raid_test_combo() != 0) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}

	/* gfni affine transformation matrices */
	printf("Test GFNI affine matrices...\n");
	if (raid_test_gfaffine() != 0) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}

	/* cauchy raid polynomial checks */
	printf("Test Cauchy RAID polynomials...\n");
	if (raid_test_poly(RAID_MODE_CAUCHY_RAID) != 0) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}

	/* cauchy aes polynomial checks */
	printf("Test Cauchy AES polynomials...\n");
	if (raid_test_poly(RAID_MODE_CAUCHY_AES) != 0) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}

	/* cauchy raid module self-test */
	printf("Test Cauchy RAID self-test...\n");
	raid_mode(RAID_MODE_CAUCHY_RAID);
	if (raid_selftest() != 0) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}

	/* cauchy aes module self-test */
	printf("Test Cauchy AES self-test...\n");
	raid_mode(RAID_MODE_CAUCHY_AES);
	if (raid_selftest() != 0) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}

	/* vandermonde raid module self-test */
	printf("Test Vandermonde RAID self-test...\n");
	raid_mode(RAID_MODE_VANDERMONDE_RAID);
	if (raid_selftest() != 0) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}

	/* vandermonde raid parity generation across all disk counts from 1 to maximum */
	printf("Test Vandermonde RAID parity generation with 1-%u data disks...\n", RAID_DATA_MAX);
	for (int i = 1; i <= RAID_DATA_MAX; ++i) {
		if (raid_test_par(RAID_MODE_VANDERMONDE_RAID, i, test_size) != 0) {
			/* LCOV_EXCL_START */
			goto bail;
			/* LCOV_EXCL_STOP */
		}
	}

	/* cauchy raid parity generation across all disk counts from 1 to maximum */
	printf("Test Cauchy RAID parity generation with 1-%u data disks...\n", RAID_DATA_MAX);
	for (int i = 1; i <= RAID_DATA_MAX; ++i) {
		if (raid_test_par(RAID_MODE_CAUCHY_RAID, i, test_size) != 0) {
			/* LCOV_EXCL_START */
			goto bail;
			/* LCOV_EXCL_STOP */
		}
	}

	/* cauchy aes parity generation across all disk counts from 1 to maximum */
	printf("Test Cauchy AES parity generation with 1-%u data disks...\n", RAID_DATA_MAX);
	for (int i = 1; i <= RAID_DATA_MAX; ++i) {
		if (raid_test_par(RAID_MODE_CAUCHY_AES, i, test_size) != 0) {
			/* LCOV_EXCL_START */
			goto bail;
			/* LCOV_EXCL_STOP */
		}
	}

	/* vandermonde raid tail recovery across all data disk counts from 1 to maximum */
	printf("Test Vandermonde RAID tail recovering with 1-%u data disks...\n", RAID_DATA_MAX);
	if (raid_test_tail(RAID_MODE_VANDERMONDE_RAID, RAID_DATA_MAX, test_size) != 0) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}

	/* cauchy raid tail recovery across all data disk counts from 1 to maximum */
	printf("Test Cauchy RAID tail recovering with 1-%u data disks...\n", RAID_DATA_MAX);
	if (raid_test_tail(RAID_MODE_CAUCHY_RAID, RAID_DATA_MAX, test_size) != 0) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}

	/* cauchy aes tail recovery across all data disk counts from 1 to maximum */
	printf("Test Cauchy AES tail recovering with 1-%u data disks...\n", RAID_DATA_MAX);
	if (raid_test_tail(RAID_MODE_CAUCHY_AES, RAID_DATA_MAX, test_size) != 0) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}

	/* vandermonde raid recovery with all combinations of data disks and 3 parity blocks */
	printf("Test Vandermonde RAID recovering with all combinations of %u data and 3 parity blocks...\n", test_count);
	if (raid_test_rec(RAID_MODE_VANDERMONDE_RAID, test_count, test_size) != 0) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}

	/* cauchy raid recovery with all combinations of data disks and 6 parity blocks */
	printf("Test Cauchy RAID recovering with all combinations of %u data and 6 parity blocks...\n", test_count);
	if (raid_test_rec(RAID_MODE_CAUCHY_RAID, test_count, test_size) != 0) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}

	/* cauchy aes recovery with all combinations of data disks and 6 parity blocks */
	printf("Test Cauchy AES recovering with all combinations of %u data and 6 parity blocks...\n", test_count);
	if (raid_test_rec(RAID_MODE_CAUCHY_AES, test_count, test_size) != 0) {
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
