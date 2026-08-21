// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2013 Andrea Mazzoleni

/*
 * Matrix inversion multithread test for the RAID library.
 *
 * Verifies that all square submatrices of the Extended Cauchy matrix are
 * nonsingular (invertible) for both operating modes:
 *
 *   RAID: GF polynomial 0x11d, primitive generator g=2
 *   AES:  GF polynomial 0x11b, G23 Extended Cauchy construction
 */

#include "internal.h"

#include "combo.h"
#include "gf.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

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

#define TEST_BATCH (1024 * 1024)

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

static uint64_t bc_table[RAID_DATA_MAX + 1][RAID_PARITY_MAX + 1];

static void bc_init(void)
{
	int n, k;

	for (n = 0; n <= RAID_DATA_MAX; ++n) {
		bc_table[n][0] = 1;
		for (k = 1; k <= RAID_PARITY_MAX; ++k) {
			if (k > n)
				bc_table[n][k] = 0;
			else
				bc_table[n][k] = bc_table[n - 1][k - 1] + bc_table[n - 1][k];
		}
	}
}

static void combination_unrank(int r, int n, uint64_t idx, int *c)
{
	int v = 0;
	int i;

	for (i = 0; i < r; ++i) {
		while (1) {
			uint64_t count = bc_table[n - 1 - v][r - 1 - i];

			if (idx < count) {
				c[i] = v;
				++v;
				break;
			}
			idx -= count;
			++v;
		}
	}
}

struct worker_arg {
	int nr;
	const char *name;
	uint64_t start_idx;
	uint64_t end_idx;
};

static int64_t shared_count;
static int has_failed;
static int active_threads;

static void *worker_thread(void *ptr)
{
	struct worker_arg *arg = (struct worker_arg *)ptr;
	int nr = arg->nr;
	const char *name = arg->name;
	uint64_t start_idx = arg->start_idx;
	uint64_t end_idx = arg->end_idx;
	uint64_t count_d = end_idx - start_idx;
	int np = RAID_PARITY_MAX;
	int nd = RAID_DATA_MAX;
	int ip[RAID_PARITY_MAX];
	int id[RAID_DATA_MAX];
	uint8_t M[RAID_PARITY_MAX * RAID_PARITY_MAX];
	int local_step = 0;
	uint64_t d;

	if (count_d == 0) {
		__atomic_fetch_sub(&active_threads, 1, __ATOMIC_SEQ_CST);
		return 0;
	}

	combination_unrank(nr, nd, start_idx, id);

	for (d = 0; d < count_d; ++d) {
		if (__atomic_load_n(&has_failed, __ATOMIC_SEQ_CST))
			break;

		combination_first(nr, np, ip);
		do {
			int i, j;

			/* setup the submatrix */
			for (i = 0; i < nr; ++i)
				for (j = 0; j < nr; ++j)
					M[i * nr + j] = raid_gfgen[ip[i]][id[j]];

			/* invert */
			if (raid_invert_fast(M, nr) != 0) {
				if (!__atomic_exchange_n(&has_failed, 1, __ATOMIC_SEQ_CST)) {
					int k;
					printf("\nFAILED %s %ux%u submatrix\n", name, nr, nr);
					printf("parity:");
					for (k = 0; k < nr; ++k)
						printf(" %d", ip[k]);
					printf("\ndata:");
					for (k = 0; k < nr; ++k)
						printf(" %d", id[k]);
					printf("\n");
				}
				__atomic_fetch_sub(&active_threads, 1, __ATOMIC_SEQ_CST);
				return (void *)(intptr_t)-1;
			}

			if (++local_step >= TEST_BATCH) {
				__atomic_fetch_add(&shared_count, local_step, __ATOMIC_SEQ_CST);
				local_step = 0;
			}
		} while (combination_next(nr, np, ip));

		if (d + 1 < count_d)
			combination_next(nr, nd, id);
	}

	if (local_step > 0)
		__atomic_fetch_add(&shared_count, local_step, __ATOMIC_SEQ_CST);

	__atomic_fetch_sub(&active_threads, 1, __ATOMIC_SEQ_CST);
	return 0;
}

static __always_inline int test_sub_matrix(int nr, int mode, const char *name, int64_t *total, int nthreads)
{
	pthread_t threads[nthreads];
	struct worker_arg args[nthreads];
	int64_t expected;
	int64_t count;
	uint64_t total_disk_comb;
	struct timespec ts;
	int t;
	int failed = 0;

	/* select the operating mode before reading raid_gfgen or using inv()/mul() */
	raid_mode(mode);

	printf("\n%ux%u %s\n", nr, nr, name);

	expected = EXPECTED[nr - 1];
	total_disk_comb = bc_table[RAID_DATA_MAX][nr];

	__atomic_store_n(&shared_count, 0, __ATOMIC_SEQ_CST);
	__atomic_store_n(&has_failed, 0, __ATOMIC_SEQ_CST);
	__atomic_store_n(&active_threads, nthreads, __ATOMIC_SEQ_CST);

	for (t = 0; t < nthreads; ++t) {
		args[t].nr = nr;
		args[t].name = name;
		args[t].start_idx = t * total_disk_comb / nthreads;
		args[t].end_idx = (t + 1) * total_disk_comb / nthreads;

		if (pthread_create(&threads[t], 0, worker_thread, &args[t]) != 0) {
			fprintf(stderr, "Failed to create thread %d\n", t);
			exit(EXIT_FAILURE);
		}
	}

	ts.tv_sec = 0;
	ts.tv_nsec = 50 * 1000 * 1000; /* 50 ms */

	while (1) {
		int64_t cur;

		nanosleep(&ts, 0);

		if (__atomic_load_n(&active_threads, __ATOMIC_SEQ_CST) == 0)
			break;

		if (__atomic_load_n(&has_failed, __ATOMIC_SEQ_CST))
			break;

		cur = __atomic_load_n(&shared_count, __ATOMIC_SEQ_CST);
		if (cur > expected)
			cur = expected;

		printf("\r%.3f %%", cur * (double)100 / expected);
		fflush(stdout);
	}

	for (t = 0; t < nthreads; ++t) {
		void *ret;
		pthread_join(threads[t], &ret);
		if (ret != 0)
			failed = 1;
	}

	if (failed || __atomic_load_n(&has_failed, __ATOMIC_SEQ_CST))
		return -1;

	count = __atomic_load_n(&shared_count, __ATOMIC_SEQ_CST);

	if (count != expected) {
		printf("\nFAILED %s %ux%u count mismatch: %" PRIi64 " != %" PRIi64 "\n", name, nr, nr, count, expected);
		return -1;
	}

	printf("\rTested %" PRIi64 " matrix\n", count);

	*total += count;

	return 0;
}

int test_all_sub_matrix(int nthreads)
{
	int64_t total_raid;
	int64_t total_aes;

	printf("Invert all square submatrices of the %dx%d Cauchy matrices in RAID and AES modes\n",
		RAID_PARITY_MAX, RAID_DATA_MAX);

	printf("Using %d threads\n", nthreads);

	printf("\nThis exhaustive test may take hours...\n");

	total_raid = 0;
	total_aes = 0;

	/* force inlining of everything */
	if (test_sub_matrix(1, RAID_MODE_CAUCHY_RAID, "RAID", &total_raid, nthreads) != 0)
		return -1;
	if (test_sub_matrix(1, RAID_MODE_CAUCHY_AES, "AES", &total_aes, nthreads) != 0)
		return -1;
	if (test_sub_matrix(2, RAID_MODE_CAUCHY_RAID, "RAID", &total_raid, nthreads) != 0)
		return -1;
	if (test_sub_matrix(2, RAID_MODE_CAUCHY_AES, "AES", &total_aes, nthreads) != 0)
		return -1;
	if (test_sub_matrix(3, RAID_MODE_CAUCHY_RAID, "RAID", &total_raid, nthreads) != 0)
		return -1;
	if (test_sub_matrix(3, RAID_MODE_CAUCHY_AES, "AES", &total_aes, nthreads) != 0)
		return -1;
	if (test_sub_matrix(4, RAID_MODE_CAUCHY_RAID, "RAID", &total_raid, nthreads) != 0)
		return -1;
	if (test_sub_matrix(4, RAID_MODE_CAUCHY_AES, "AES", &total_aes, nthreads) != 0)
		return -1;
	if (test_sub_matrix(5, RAID_MODE_CAUCHY_RAID, "RAID", &total_raid, nthreads) != 0)
		return -1;
	if (test_sub_matrix(5, RAID_MODE_CAUCHY_AES, "AES", &total_aes, nthreads) != 0)
		return -1;
	if (test_sub_matrix(6, RAID_MODE_CAUCHY_RAID, "RAID", &total_raid, nthreads) != 0)
		return -1;
	if (test_sub_matrix(6, RAID_MODE_CAUCHY_AES, "AES", &total_aes, nthreads) != 0)
		return -1;

	printf("\nTested in total %" PRIi64 " RAID matrix\n", total_raid);
	printf("Tested in total %" PRIi64 " AES matrix\n", total_aes);

	return 0;
}

int main(void)
{
	int nthreads;

	printf("Matrix inversion test for the RAID Cauchy library\n\n");

	bc_init();

	/* required to set the raid tables */
	raid_init();

	nthreads = sysconf(_SC_NPROCESSORS_ONLN);
	if (nthreads < 1)
		nthreads = 1;

	if (test_all_sub_matrix(nthreads) != 0) {
		printf("FAILED!\n");
		exit(EXIT_FAILURE);
	}
	printf("OK\n");

	return 0;
}
