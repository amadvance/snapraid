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

#include "internal.h"
#include "cpu.h"
#include "combo.h"
#include "memory.h"

/**
 * Binomial coefficient of n over r.
 */
static int ibc(int n, int r)
{
	if (r == 0 || n == r)
		return 1;
	else
		return ibc(n - 1, r - 1) + ibc(n - 1, r);
}

/**
 * Power n ^ r;
 */
static int ipow(int n, int r)
{
	int v = 1;
	while (r) {
		v *= n;
		--r;
	}
	return v;
}

int raid_test_combo(void)
{
	int r;
	int count;
	int p[RAID_PARITY_MAX];

	for (r = 1; r <= RAID_PARITY_MAX; ++r) {
		/* count combination (r of RAID_PARITY_MAX) elements */
		count = 0;
		combination_first(r, RAID_PARITY_MAX, p);

		do {
			++count;
		} while (combination_next(r, RAID_PARITY_MAX, p));

		if (count != ibc(RAID_PARITY_MAX, r))
			return -1;
	}

	for (r = 1; r <= RAID_PARITY_MAX; ++r) {
		/* count permutation (r of RAID_PARITY_MAX) elements */
		count = 0;
		permutation_first(r, RAID_PARITY_MAX, p);

		do {
			++count;
		} while (permutation_next(r, RAID_PARITY_MAX, p));

		if (count != ipow(RAID_PARITY_MAX, r))
			return -1;
	}

	return 0;
}

int raid_test_sort(void)
{
	int p[RAID_PARITY_MAX];
	int r;

	for (r = 1; r <= RAID_PARITY_MAX; ++r) {
		permutation_first(r, RAID_PARITY_MAX, p);
		do {
			int i[RAID_PARITY_MAX];
			int j;

			/* make a copy */
			for (j = 0; j < r; ++j)
				i[j] = p[j];

			raid_sort(r, i);

			/* check order */
			for (j = 1; j < r; ++j)
				if (i[j-1] > i[j])
					return -1;
		} while (permutation_next(r, RAID_PARITY_MAX, p));
	}

	return 0;
}

int raid_test_rec(int mode, int nd, size_t size)
{
	void *buffer_alloc;
	void **buffer;
	void **data;
	void **parity;
	void **test;
	void *data_save[RAID_PARITY_MAX];
	void *parity_save[RAID_PARITY_MAX];
	void *zero;
	void *waste;
	int buffermax;
	int id[RAID_PARITY_MAX];
	int ip[RAID_PARITY_MAX];
	int i;
	int j;
	int nr;
	void (*map[RAID_PARITY_MAX][4])(int nr, const int *id, const int *ip, int nd, size_t size, void **vbuf);
	int mac[RAID_PARITY_MAX];
	int np;

	raid_mode(mode);
	if (mode == RAID_MODE_CAUCHY)
		np = RAID_PARITY_MAX;
	else
		np = 3;

	buffermax = nd + np * 2 + 2;

	buffer = raid_malloc_vector(nd, buffermax, size, &buffer_alloc);
	if (!buffer)
		return -1;

	data = buffer;
	parity = buffer + nd;
	test = buffer + nd + np;

	for (i = 0; i < np; ++i)
		parity_save[i] = parity[i];

	zero = buffer[buffermax-2];
	memset(zero, 0, size);
	raid_zero(zero);

	waste = buffer[buffermax-1];

	/* fill data disk with random */
	raid_mrand_vector(buffer, nd, size);

	/* setup recov functions */
	for (i = 0; i < np; ++i) {
		mac[i] = 0;
		if (i == 0) {
			map[i][mac[i]++] = raid_rec1_int8;
#ifdef CONFIG_X86
			if (raid_cpu_has_ssse3())
				map[i][mac[i]++] = raid_rec1_ssse3;
#endif
		} else if (i == 1) {
			map[i][mac[i]++] = raid_rec2_int8;
#ifdef CONFIG_X86
			if (raid_cpu_has_ssse3())
				map[i][mac[i]++] = raid_rec2_ssse3;
#endif
		} else {
			map[i][mac[i]++] = raid_recX_int8;
#ifdef CONFIG_X86
			if (raid_cpu_has_ssse3())
				map[i][mac[i]++] = raid_recX_ssse3;
#endif
		}
	}

	/* compute the parity */
	raid_par(nd, np, size, buffer);

	/* set all the parity to the waste buffer */
	for (i = 0; i < np; ++i)
		parity[i] = waste;

	/* all parity levels */
	for (nr = 1; nr <= np; ++nr) {
		/* all combinations (nr of nd) disks */
		combination_first(nr, nd, id);
		do {
			/* all combinations (nr of np) parities */
			combination_first(nr, np, ip);
			do {
				/* for each recover function */
				for (j = 0; j < mac[nr-1]; ++j) {
					/* set */
					for (i = 0; i < nr; ++i) {
						/* remove the missing data */
						data_save[i] = data[id[i]];
						data[id[i]] = test[i];
						/* set the parity to use */
						parity[ip[i]] = parity_save[ip[i]];
					}

					/* recover */
					map[nr-1][j](nr, id, ip, nd, size, buffer);

					/* check */
					for (i = 0; i < nr; ++i)
						if (memcmp(test[i], data_save[i], size) != 0)
							return -1;

					/* restore */
					for (i = 0; i < nr; ++i) {
						/* restore the data */
						data[id[i]] = data_save[i];
						/* restore the parity */
						parity[ip[i]] = waste;
					}
				}
			} while (combination_next(nr, np, ip));
		} while (combination_next(nr, nd, id));
	}

	free(buffer_alloc);
	free(buffer);

	return 0;
}

int raid_test_par(int mode, int nd, size_t size)
{
	void *buffer_alloc;
	void **buffer;
	int buffermax;
	int i, j;
	void (*map[64])(int nd, size_t size, void **vbuf);
	int mac;
	int np;

	raid_mode(mode);
	if (mode == RAID_MODE_CAUCHY)
		np = RAID_PARITY_MAX;
	else
		np = 3;

	buffermax = nd + np * 2;

	buffer = raid_malloc_vector(nd, buffermax, size, &buffer_alloc);
	if (!buffer)
		return -1;

	/* fill with random */
	raid_mrand_vector(buffer, buffermax, size);

	/* compute the parity */
	raid_par(nd, np, size, buffer);

	/* copy in back buffers */
	for (i = 0; i < np; ++i)
		memcpy(buffer[nd + np + i], buffer[nd + i], size);

	/* load all the available functions */
	mac = 0;

	map[mac++] = raid_par1_int32;
	map[mac++] = raid_par1_int64;
	map[mac++] = raid_par2_int32;
	map[mac++] = raid_par2_int64;

#ifdef CONFIG_X86
	if (raid_cpu_has_sse2()) {
		map[mac++] = raid_par1_sse2;
		map[mac++] = raid_par2_sse2;
#ifdef CONFIG_X86_64
		map[mac++] = raid_par2_sse2ext;
#endif
	}
#endif

	if (mode == RAID_MODE_CAUCHY) {
		map[mac++] = raid_par3_int8;
		map[mac++] = raid_par4_int8;
		map[mac++] = raid_par5_int8;
		map[mac++] = raid_par6_int8;

#ifdef CONFIG_X86
		if (raid_cpu_has_ssse3()) {
			map[mac++] = raid_par3_ssse3;
			map[mac++] = raid_par4_ssse3;
			map[mac++] = raid_par5_ssse3;
			map[mac++] = raid_par6_ssse3;
#ifdef CONFIG_X86_64
			map[mac++] = raid_par3_ssse3ext;
			map[mac++] = raid_par4_ssse3ext;
			map[mac++] = raid_par5_ssse3ext;
			map[mac++] = raid_par6_ssse3ext;
#endif
		}
#endif
	} else {
		map[mac++] = raid_parz_int32;
		map[mac++] = raid_parz_int64;

#ifdef CONFIG_X86
		if (raid_cpu_has_sse2()) {
			map[mac++] = raid_parz_sse2;
#ifdef CONFIG_X86_64
			map[mac++] = raid_parz_sse2ext;
#endif
		}
#endif
	}

	/* check all the functions */
	for (j = 0; j < mac; ++j) {
		/* compute parity */
		map[j](nd, size, buffer);

		/* check it */
		for (i = 0; i < np; ++i)
			if (memcmp(buffer[nd + np + i], buffer[nd + i], size) != 0)
				return -1;
	}

	free(buffer_alloc);
	free(buffer);

	return 0;
}

