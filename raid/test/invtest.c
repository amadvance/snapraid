// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2013 Andrea Mazzoleni

/*
 * Matrix inversion test for the RAID library.
 *
 * Verifies that all square submatrices of the Extended Cauchy matrix are
 * nonsingular (invertible) for both operating modes:
 *
 *   RAID: GF polynomial 0x11d, primitive generator g=2
 *   AES:  GF polynomial 0x11b, primitive generator g=3
 */

#include "internal.h"

#include "combo.h"
#include "gf.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

/**
 * Like raid_invert() but optimized to only check if the matrix is
 * invertible.
 */
static __always_inline int raid_invert_fast(uint8_t *M, int n)
{
	int i, j, k;

	/* for each element in the diagonal */
	for (k = 0; k < n; ++k) {
		uint8_t f;

		/*
		 * The diagonal element cannot be 0 because
		 * we are inverting matrices with all the square
		 * submatrices not singular
		 */
		if (M[k * n + k] == 0)
			return -1;

		/* make the diagonal element to be 1 */
		f = inv(M[k * n + k]);
		for (j = 0; j < n; ++j)
			M[k * n + j] = mul(f, M[k * n + j]);

		/*
		 * Make all the elements over and under the diagonal
		 * to be zero
		 */
		for (i = 0; i < n; ++i) {
			if (i == k)
				continue;
			f = M[i * n + k];
			for (j = 0; j < n; ++j)
				M[i * n + j] ^= mul(f, M[k * n + j]);
		}
	}

	return 0;
}

#define TEST_REFRESH (4 * 1024 * 1024)

/**
 * Precomputed number of square submatrices of size nr.
 *
 * It's bc(np,nr) * bc(nd,nr)
 *
 * With 1<=nr<=6 and bc(n, r) == binomial coefficient of (n over r).
 */
long long EXPECTED[RAID_PARITY_MAX] = {
	1506LL,
	470625LL,
	52082500LL,
	2421836250LL,
	47855484300LL,
	327012476050LL
};

static __always_inline int test_sub_matrix(int nr, int mode, const char *name, int64_t *total)
{
	uint8_t M[RAID_PARITY_MAX * RAID_PARITY_MAX];
	int np = RAID_PARITY_MAX;
	int nd = RAID_DATA_MAX;
	int ip[RAID_PARITY_MAX];
	int id[RAID_DATA_MAX];
	int64_t count;
	int64_t expected;

	/* select the operating mode before reading raid_gfgen or using inv()/mul() */
	raid_mode(mode);

	printf("\n%ux%u %s\n", nr, nr, name);

	count = 0;
	expected = EXPECTED[nr - 1];

	/* all combinations (nr of nd) disks */
	combination_first(nr, nd, id);
	do {
		/* all combinations (nr of np) parities */
		combination_first(nr, np, ip);
		do {
			int i, j;

			/* setup the submatrix */
			for (i = 0; i < nr; ++i)
				for (j = 0; j < nr; ++j)
					M[i * nr + j] = raid_gfgen[ip[i]][id[j]];

			/* invert */
			if (raid_invert_fast(M, nr) != 0) {
				int k;
				printf("\nFAILED %s %ux%u submatrix\n", name, nr, nr);
				printf("parity:");
				for (k = 0; k < nr; ++k)
					printf(" %d", ip[k]);
				printf("\ndata:");
				for (k = 0; k < nr; ++k)
					printf(" %d", id[k]);
				printf("\n");
				return -1;
			}

			if (++count % TEST_REFRESH == 0) {
				printf("\r%.3f %%", count * (double)100 / expected);
				fflush(stdout);
			}
		} while (combination_next(nr, np, ip));
	} while (combination_next(nr, nd, id));

	if (count != expected) {
		printf("\nFAILED %s %ux%u count mismatch: %" PRIi64 " != %" PRIi64 "\n", name, nr, nr, count, expected);
		return -1;
	}

	printf("\rTested %" PRIi64 " matrix\n", count);

	*total += count;

	return 0;
}

int test_all_sub_matrix(void)
{
	int64_t total_raid;
	int64_t total_aes;

	printf("Invert all square submatrices of the %dx%d Cauchy matrices in RAID and AES modes\n",
		RAID_PARITY_MAX, RAID_DATA_MAX);

	printf("\nThis exhaustive test may take days...\n");

	total_raid = 0;
	total_aes = 0;

	/* force inlining of everything */
	if (test_sub_matrix(1, RAID_MODE_CAUCHY_RAID, "RAID", &total_raid) != 0)
		return -1;
	if (test_sub_matrix(1, RAID_MODE_CAUCHY_AES, "AES", &total_aes) != 0)
		return -1;
	if (test_sub_matrix(2, RAID_MODE_CAUCHY_RAID, "RAID", &total_raid) != 0)
		return -1;
	if (test_sub_matrix(2, RAID_MODE_CAUCHY_AES, "AES", &total_aes) != 0)
		return -1;
	if (test_sub_matrix(3, RAID_MODE_CAUCHY_RAID, "RAID", &total_raid) != 0)
		return -1;
	if (test_sub_matrix(3, RAID_MODE_CAUCHY_AES, "AES", &total_aes) != 0)
		return -1;
	if (test_sub_matrix(4, RAID_MODE_CAUCHY_RAID, "RAID", &total_raid) != 0)
		return -1;
	if (test_sub_matrix(4, RAID_MODE_CAUCHY_AES, "AES", &total_aes) != 0)
		return -1;
	if (test_sub_matrix(5, RAID_MODE_CAUCHY_RAID, "RAID", &total_raid) != 0)
		return -1;
	if (test_sub_matrix(5, RAID_MODE_CAUCHY_AES, "AES", &total_aes) != 0)
		return -1;
	if (test_sub_matrix(6, RAID_MODE_CAUCHY_RAID, "RAID", &total_raid) != 0)
		return -1;
	if (test_sub_matrix(6, RAID_MODE_CAUCHY_AES, "AES", &total_aes) != 0)
		return -1;

	printf("\nTested in total %" PRIi64 " RAID matrix\n", total_raid);
	printf("Tested in total %" PRIi64 " AES matrix\n", total_aes);

	return 0;
}

int main(void)
{
	printf("Matrix inversion test for the RAID Cauchy library\n\n");

	/* required to set the raid tables */
	raid_init();

	if (test_all_sub_matrix() != 0) {
		printf("FAILED!\n");
		exit(EXIT_FAILURE);
	}
	printf("OK\n");

	return 0;
}
