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

/* Matrix inversion test for the RAID library */

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

		/* the diagonal element cannot be 0 because */
		/* we are inverting matrices with all the square */
		/* submatrices not singular */
		if (M[k * n + k] == 0)
			return -1;

		/* make the diagonal element to be 1 */
		f = inv(M[k * n + k]);
		for (j = 0; j < n; ++j)
			M[k * n + j] = mul(f, M[k * n + j]);

		/* make all the elements over and under the diagonal */
		/* to be zero */
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

static __always_inline int test_sub_matrix(int nr, long long *total)
{
	uint8_t M[RAID_PARITY_MAX * RAID_PARITY_MAX];
	int np = RAID_PARITY_MAX;
	int nd = RAID_DATA_MAX;
	int ip[RAID_PARITY_MAX];
	int id[RAID_DATA_MAX];
	long long count;
	long long expected;

	printf("\n%ux%u\n", nr, nr);

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
					M[i * nr + j] = gfgen[ip[i]][id[j]];

			/* invert */
			if (raid_invert_fast(M, nr) != 0)
				return -1;

			if (++count % TEST_REFRESH == 0) {
				printf("\r%.3f %%", count * (double)100 / expected);
				fflush(stdout);
			}
		} while (combination_next(nr, np, ip));
	} while (combination_next(nr, nd, id));

	if (count != expected)
		return -1;

	printf("\rTested %" PRIi64 " matrix\n", count);

	*total += count;

	return 0;
}

int test_all_sub_matrix(void)
{
	long long total;

	printf("Invert all square submatrices of the %dx%d Cauchy matrix\n",
		RAID_PARITY_MAX, RAID_DATA_MAX);

	printf("\nPlease wait about 2 days...\n");

	total = 0;

	/* force inlining of everything */
	if (test_sub_matrix(1, &total) != 0)
		return -1;
	if (test_sub_matrix(2, &total) != 0)
		return -1;
	if (test_sub_matrix(3, &total) != 0)
		return -1;
	if (test_sub_matrix(4, &total) != 0)
		return -1;
	if (test_sub_matrix(5, &total) != 0)
		return -1;
	if (test_sub_matrix(6, &total) != 0)
		return -1;

	printf("\nTested in total %" PRIi64 " matrix\n", total);

	return 0;
}

int main(void)
{
	printf("Matrix inversion test for the RAID Cauchy library\n\n");

	/* required to set the gfgen table */
	raid_init();

	if (test_all_sub_matrix() != 0) {
		printf("FAILED!\n");
		exit(EXIT_FAILURE);
	}
	printf("OK\n");

	return 0;
}

