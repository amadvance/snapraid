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

		if (count != ibc(RAID_PARITY_MAX, r)) {
			/* LCOV_EXCL_START */
			return -1;
			/* LCOV_EXCL_STOP */
		}
	}

	for (r = 1; r <= RAID_PARITY_MAX; ++r) {
		/* count permutation (r of RAID_PARITY_MAX) elements */
		count = 0;
		permutation_first(r, RAID_PARITY_MAX, p);

		do {
			++count;
		} while (permutation_next(r, RAID_PARITY_MAX, p));

		if (count != ipow(RAID_PARITY_MAX, r)) {
			/* LCOV_EXCL_START */
			return -1;
			/* LCOV_EXCL_STOP */
		}
	}

	return 0;
}

int raid_test_insert(void)
{
	int p[RAID_PARITY_MAX];
	int r;

	for (r = 1; r <= RAID_PARITY_MAX; ++r) {
		permutation_first(r, RAID_PARITY_MAX, p);
		do {
			int i[RAID_PARITY_MAX];
			int j;

			/* insert in order */
			for (j = 0; j < r; ++j)
				raid_insert(j, i, p[j]);

			/* check order */
			for (j = 1; j < r; ++j) {
				if (i[j - 1] > i[j]) {
					/* LCOV_EXCL_START */
					return -1;
					/* LCOV_EXCL_STOP */
				}
			}
		} while (permutation_next(r, RAID_PARITY_MAX, p));
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
			for (j = 1; j < r; ++j) {
				if (i[j - 1] > i[j]) {
					/* LCOV_EXCL_START */
					return -1;
					/* LCOV_EXCL_STOP */
				}
			}
		} while (permutation_next(r, RAID_PARITY_MAX, p));
	}

	return 0;
}

int raid_test_rec(int mode, int nd, size_t size)
{
	void (*f[RAID_PARITY_MAX][4])(
		int nr, int *id, int *ip, int nd, size_t size, void **vbuf);
	void *v_alloc;
	void **v;
	void **data;
	void **parity;
	void **test;
	void *data_save[RAID_PARITY_MAX];
	void *parity_save[RAID_PARITY_MAX];
	void *waste;
	int nv;
	int id[RAID_PARITY_MAX];
	int ip[RAID_PARITY_MAX];
	int i;
	int j;
	int nr;
	int nf[RAID_PARITY_MAX];
	int np;

	raid_mode(mode);
	if (mode == RAID_MODE_CAUCHY)
		np = RAID_PARITY_MAX;
	else
		np = 3;

	nv = nd + np * 2 + 2;

	v = raid_malloc_vector(nd, nv, size, &v_alloc);
	if (!v) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	data = v;
	parity = v + nd;
	test = v + nd + np;

	for (i = 0; i < np; ++i)
		parity_save[i] = parity[i];

	memset(v[nv - 2], 0, size);
	raid_zero(v[nv - 2]);

	waste = v[nv - 1];

	/* fill with pseudo-random data with the arbitrary seed "1" */
	raid_mrand_vector(1, nd, size, v);

	/* setup recov functions */
	for (i = 0; i < np; ++i) {
		nf[i] = 0;
		if (i == 0) {
			f[i][nf[i]++] = raid_rec1_int8;
#ifdef CONFIG_X86
#ifdef CONFIG_SSSE3
			if (raid_cpu_has_ssse3())
				f[i][nf[i]++] = raid_rec1_ssse3;
#endif
#ifdef CONFIG_AVX2
			if (raid_cpu_has_avx2())
				f[i][nf[i]++] = raid_rec1_avx2;
#endif
#endif
		} else if (i == 1) {
			f[i][nf[i]++] = raid_rec2_int8;
#ifdef CONFIG_X86
#ifdef CONFIG_SSSE3
			if (raid_cpu_has_ssse3())
				f[i][nf[i]++] = raid_rec2_ssse3;
#endif
#ifdef CONFIG_AVX2
			if (raid_cpu_has_avx2())
				f[i][nf[i]++] = raid_rec2_avx2;
#endif
#endif
		} else {
			f[i][nf[i]++] = raid_recX_int8;
#ifdef CONFIG_X86
#ifdef CONFIG_SSSE3
			if (raid_cpu_has_ssse3())
				f[i][nf[i]++] = raid_recX_ssse3;
#endif
#ifdef CONFIG_AVX2
			if (raid_cpu_has_avx2())
				f[i][nf[i]++] = raid_recX_avx2;
#endif
#endif
		}
	}

	/* compute the parity */
	raid_gen_ref(nd, np, size, v);

	/* set all the parity to the waste v */
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
				for (j = 0; j < nf[nr - 1]; ++j) {
					/* set */
					for (i = 0; i < nr; ++i) {
						/* remove the missing data */
						data_save[i] = data[id[i]];
						data[id[i]] = test[i];
						/* set the parity to use */
						parity[ip[i]] = parity_save[ip[i]];
					}

					/* recover */
					f[nr - 1][j](nr, id, ip, nd, size, v);

					/* check */
					for (i = 0; i < nr; ++i) {
						if (memcmp(test[i], data_save[i], size) != 0) {
							/* LCOV_EXCL_START */
							goto bail;
							/* LCOV_EXCL_STOP */
						}
					}

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

	free(v_alloc);
	free(v);
	return 0;

bail:
	/* LCOV_EXCL_START */
	free(v_alloc);
	free(v);
	return -1;
	/* LCOV_EXCL_STOP */
}

int raid_test_par(int mode, int nd, size_t size)
{
	void (*f[64])(int nd, size_t size, void **vbuf);
	void *v_alloc;
	void **v;
	int nv;
	int i, j;
	int nf;
	int np;

	raid_mode(mode);
	if (mode == RAID_MODE_CAUCHY)
		np = RAID_PARITY_MAX;
	else
		np = 3;

	nv = nd + np * 2;

	v = raid_malloc_vector(nd, nv, size, &v_alloc);
	if (!v) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/* check memory */
	if (raid_mtest_vector(nv, size, v) != 0) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}

	/* fill with pseudo-random data with the arbitrary seed "2" */
	raid_mrand_vector(2, nv, size, v);

	/* compute the parity */
	raid_gen_ref(nd, np, size, v);

	/* copy in back buffers */
	for (i = 0; i < np; ++i)
		memcpy(v[nd + np + i], v[nd + i], size);

	/* load all the available functions */
	nf = 0;

	f[nf++] = raid_gen1_int32;
	f[nf++] = raid_gen1_int64;
	f[nf++] = raid_gen2_int32;
	f[nf++] = raid_gen2_int64;

#ifdef CONFIG_X86
#ifdef CONFIG_SSE2
	if (raid_cpu_has_sse2()) {
		f[nf++] = raid_gen1_sse2;
		f[nf++] = raid_gen2_sse2;
#ifdef CONFIG_X86_64
		f[nf++] = raid_gen2_sse2ext;
#endif
	}
#endif

#ifdef CONFIG_AVX2
	if (raid_cpu_has_avx2()) {
		f[nf++] = raid_gen1_avx2;
		f[nf++] = raid_gen2_avx2;
	}
#endif
#endif /* CONFIG_X86 */

	if (mode == RAID_MODE_CAUCHY) {
		f[nf++] = raid_gen3_int8;
		f[nf++] = raid_gen4_int8;
		f[nf++] = raid_gen5_int8;
		f[nf++] = raid_gen6_int8;

#ifdef CONFIG_X86
#ifdef CONFIG_SSSE3
		if (raid_cpu_has_ssse3()) {
			f[nf++] = raid_gen3_ssse3;
			f[nf++] = raid_gen4_ssse3;
			f[nf++] = raid_gen5_ssse3;
			f[nf++] = raid_gen6_ssse3;
#ifdef CONFIG_X86_64
			f[nf++] = raid_gen3_ssse3ext;
			f[nf++] = raid_gen4_ssse3ext;
			f[nf++] = raid_gen5_ssse3ext;
			f[nf++] = raid_gen6_ssse3ext;
#endif
		}
#endif

#ifdef CONFIG_AVX2
#ifdef CONFIG_X86_64
		if (raid_cpu_has_avx2()) {
			f[nf++] = raid_gen3_avx2ext;
			f[nf++] = raid_gen4_avx2ext;
			f[nf++] = raid_gen5_avx2ext;
			f[nf++] = raid_gen6_avx2ext;
		}
#endif
#endif
#endif /* CONFIG_X86 */
	} else {
		f[nf++] = raid_genz_int32;
		f[nf++] = raid_genz_int64;

#ifdef CONFIG_X86
#ifdef CONFIG_SSE2
		if (raid_cpu_has_sse2()) {
			f[nf++] = raid_genz_sse2;
#ifdef CONFIG_X86_64
			f[nf++] = raid_genz_sse2ext;
#endif
		}
#endif

#ifdef CONFIG_AVX2
#ifdef CONFIG_X86_64
		if (raid_cpu_has_avx2())
			f[nf++] = raid_genz_avx2ext;
#endif
#endif
#endif /* CONFIG_X86 */
	}

	/* check all the functions */
	for (j = 0; j < nf; ++j) {
		/* compute parity */
		f[j](nd, size, v);

		/* check it */
		for (i = 0; i < np; ++i) {
			if (memcmp(v[nd + np + i], v[nd + i], size) != 0) {
				/* LCOV_EXCL_START */
				goto bail;
				/* LCOV_EXCL_STOP */
			}
		}
	}

	free(v_alloc);
	free(v);
	return 0;

bail:
	/* LCOV_EXCL_START */
	free(v_alloc);
	free(v);
	return -1;
	/* LCOV_EXCL_STOP */
}

