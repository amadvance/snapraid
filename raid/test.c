// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2013 Andrea Mazzoleni

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

static void meminc(void *dst, size_t size)
{
	size_t i;
	unsigned char *p = dst;

	for (i = 0; i < size; ++i)
		++p[i];
}

/**
 * Simulate the bit-level semantics of VGF2P8AFFINEQB with imm8=0.
 */
static uint8_t raid_gfaffine_mul(uint8_t c, uint8_t x)
{
	uint8_t v = 0;
	int j;

	for (j = 0; j < 8; ++j) {
		uint8_t row = raid_gfaffine_raid[c][7 - j] & x;

		row ^= row >> 4;
		row ^= row >> 2;
		row ^= row >> 1;
		v |= (row & 1U) << j;
	}

	return v;
}

int raid_test_gfaffine(void)
{
	int c, x;

	/* all coefficients, including 0, 1, 2, and 0xff, are mandatory */
	for (c = 0; c < 256; ++c)
		for (x = 0; x < 256; ++x)
			if (raid_gfaffine_mul((uint8_t)c, (uint8_t)x) != raid_gfmul_raid[c][x])
				return -1;

	return 0;
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

/*
 * Tests the mathematical properties required by a Cauchy RAID mode.
 *
 * This is intentionally only a fast sanity check. It does not prove that
 * all square submatrices are nonsingular. The exhaustive verification of
 * the MDS property is performed by raid/test/invtest.
 *
 * SnapRAID uses two Extended Cauchy constructions:
 *
 *   RAID_MODE_CAUCHY_RAID:
 *       polynomial 0x11d
 *       primitive generator g=2
 *
 *   RAID_MODE_CAUCHY_AES:
 *       polynomial 0x11b
 *       G23 Extended Cauchy construction
 *
 * The parity generation tests compare optimized implementations against
 * raid_gen_ref(), but both use the same active generator matrix. Therefore
 * they can agree even if the matrix itself is mathematically unsuitable.
 *
 * This test instead constructs the expected Q sequence independently and
 * checks the generated matrix coefficients.
 */
int raid_test_poly(unsigned mode)
{
	uint8_t q[255];
	uint8_t v;
	int i, j;
	int p, d;
	int g23_count;

	/*
	 * This test is defined only for the two Cauchy modes.
	 *
	 * Vandermonde parity intentionally uses a different construction and
	 * is not part of this test.
	 */
	if (mode != RAID_MODE_CAUCHY_RAID
		&& mode != RAID_MODE_CAUCHY_AES)
		return -1;

	/*
	 * Select the mode first so that:
	 *
	 *     raid_gfcauchy
	 *     raid_gfmul
	 *     raid_gfinv
	 *
	 * all refer to the tables of the field being tested.
	 *
	 * This also verifies the normal raid_mode() table selection path
	 * instead of accessing the *_raid or *_aes tables directly.
	 */
	raid_mode(mode);

	/*
	 * Each Cauchy mode has a fixed reducing polynomial and matrix
	 * construction, which together determine the parity format.
	 */
	if (mode == RAID_MODE_CAUCHY_RAID) {
		if (raid_poly_byte != RAID_POLY_RAID)
			return -1;
	} else {
		if (raid_poly_byte != RAID_POLY_AES)
			return -1;
	}

	/*
	 * Build the expected Q sequence independently of raid_g23_boundary()
	 * and the optimized generator implementations.
	 */
	q[0] = 1;
	g23_count = 51;
	for (i = 0; i < 254; ++i) {
		uint8_t f = 2;

		if (mode == RAID_MODE_CAUCHY_AES && --g23_count == 0) {
			f = 3;
			g23_count = 51;
		}
		q[i + 1] = mul(f, q[i]);
	}

	/* Both constructions enumerate distinct nonzero Q coefficients. */
	for (i = 0; i < 255; ++i) {
		if (q[i] == 0)
			return -1;
		for (j = i + 1; j < 255; ++j)
			if (q[i] == q[j])
				return -1;
	}

	for (d = 0; d < RAID_DATA_MAX; ++d) {
		if (raid_gfcauchy[0][d] != 1)
			return -1;
		if (raid_gfcauchy[1][d] != q[d])
			return -1;
	}

	if (mode == RAID_MODE_CAUCHY_AES) {
		/* In AES, 2 has order 51 and 3 has order 255. */
		v = 1;
		for (i = 1; i <= 51; ++i) {
			v = mul(2, v);
			if ((i == 3 || i == 17) && v == 1)
				return -1;
		}
		if (v != 1)
			return -1;

		v = 1;
		for (i = 1; i <= 255; ++i) {
			v = mul(3, v);
			if ((i == 15 || i == 51 || i == 85) && v == 1)
				return -1;
		}
		if (v != 1)
			return -1;

		if (q[251] != 0xb8 || q[252] != 0x6b
			|| q[253] != 0xd6 || q[254] != 0xb7)
			return -1;
		if (inv(q[251]) != 0xa5 || inv(q[252]) != 0xdf
			|| inv(q[253]) != 0xe2 || inv(q[254]) != 0x71)
			return -1;

		/* the descending Y allocation keeps hypothetical higher-parity matrices nested */
		for (i = 6; i <= 9; ++i) {
			int disk = 257 - i;

			for (j = 1; j < i - 1; ++j)
				if (255 - j < disk || 255 - j > 254)
					return -1;
			if (255 - (i - 2) != disk)
				return -1;
		}

		/* independently reconstruct and normalize the four Cauchy rows */
		for (p = 2; p < RAID_PARITY_MAX; ++p) {
			uint8_t y = inv(q[256 - p]);
			uint8_t f = inv(inv(q[0]) ^ y);
			uint8_t scale = inv(f);

			for (d = 0; d < RAID_DATA_MAX; ++d) {
				uint8_t c = inv(inv(q[d]) ^ y);

				if (raid_gfcauchy[p][d] != mul(c, scale))
					return -1;
			}
		}
	}

	/*
	 * Every coefficient of the active 6x251 Extended Cauchy generator matrix must
	 * be nonzero.
	 *
	 * A single coefficient is itself a 1x1 submatrix, so a zero
	 * coefficient would make that submatrix singular and immediately
	 * disprove the MDS property.
	 *
	 * Absence of zero coefficients is only a necessary condition. It does
	 * not prove that every larger square submatrix is nonsingular. That
	 * exhaustive verification remains the responsibility of invtest.
	 */
	for (p = 0; p < RAID_PARITY_MAX; ++p)
		for (d = 0; d < RAID_DATA_MAX; ++d)
			if (raid_gfcauchy[p][d] == 0)
				return -1;

	return 0;
}

#define test_setup(i) f[i - 1][nf[i - 1]++]

int raid_test_rec(int mode, int nd, size_t size)
{
	void (*f[RAID_PARITY_MAX][32])(int nr, int *id, int *ip, int nd, size_t size, void **vbuf);
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
	if (mode == RAID_MODE_VANDERMONDE_RAID)
		np = 3;
	else
		np = RAID_PARITY_MAX;

	nv = nd + np * 2 + 2;

	v = raid_malloc_vector(nv, size, &v_alloc);
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

	/* counters of functions for each parity level */
	for (i = 0; i < np; ++i)
		nf[i] = 0;

	/* setup recov functions */
	for (i = 1; i <= np; ++i) {
		if (i == 1) {
			test_setup(i) = raid_rec1_int8;
#ifdef CONFIG_X86
			if (raid_cpu_has_ssse3())
				test_setup(i) = raid_rec1_ssse3;
			if (raid_cpu_has_avx2())
				test_setup(i) = raid_rec1_avx2;
#ifdef CONFIG_X86_64
			if (mode == RAID_MODE_CAUCHY_RAID || mode == RAID_MODE_CAUCHY_AES) {
				if (raid_cpu_has_avx2gfni()) {
					if (mode == RAID_MODE_CAUCHY_AES)
						test_setup(i) = raid_rec1_avx2gfni_aes;
					else
						test_setup(i) = raid_rec1_avx2gfni_raid;
				}
				if (raid_cpu_has_avx512gfni()) {
					if (mode == RAID_MODE_CAUCHY_AES)
						test_setup(i) = raid_rec1_avx512gfni_aes;
					else
						test_setup(i) = raid_rec1_avx512gfni_raid;
				}
			}
#endif
#endif
#ifdef CONFIG_NEON
			test_setup(i) = raid_rec1_neon;
#endif
#ifdef CONFIG_NEON32
			test_setup(i) = raid_rec1_neon32;
#endif
		} else if (i == 2) {
			test_setup(i) = raid_rec2_int8;
#ifdef CONFIG_X86
			if (raid_cpu_has_ssse3())
				test_setup(i) = raid_rec2_ssse3;
			if (raid_cpu_has_avx2())
				test_setup(i) = raid_rec2_avx2;
#ifdef CONFIG_X86_64
			if (raid_cpu_has_avx512bw())
				test_setup(i) = raid_rec2_avx512bw;
			if (mode == RAID_MODE_CAUCHY_RAID || mode == RAID_MODE_CAUCHY_AES) {
				if (raid_cpu_has_avx2gfni()) {
					if (mode == RAID_MODE_CAUCHY_AES)
						test_setup(i) = raid_rec2_avx2gfni_aes;
					else
						test_setup(i) = raid_rec2_avx2gfni_raid;
				}
				if (raid_cpu_has_avx512gfni()) {
					if (mode == RAID_MODE_CAUCHY_AES)
						test_setup(i) = raid_rec2_avx512gfni_aes;
					else
						test_setup(i) = raid_rec2_avx512gfni_raid;
				}
			}
#endif
#endif
#ifdef CONFIG_NEON
			test_setup(i) = raid_rec2_neon;
#endif
#ifdef CONFIG_NEON32
			test_setup(i) = raid_rec2_neon32;
#endif
		} else {
			test_setup(i) = raid_recX_int8;
#ifdef CONFIG_X86
			if (raid_cpu_has_ssse3())
				test_setup(i) = raid_recX_ssse3;
			if (raid_cpu_has_avx2())
				test_setup(i) = raid_recX_avx2;
#ifdef CONFIG_X86_64
			if (raid_cpu_has_avx512bw())
				test_setup(i) = raid_recX_avx512bw;
			if (mode == RAID_MODE_CAUCHY_RAID || mode == RAID_MODE_CAUCHY_AES) {
				if (raid_cpu_has_avx2gfni()) {
					if (mode == RAID_MODE_CAUCHY_AES)
						test_setup(i) = raid_recX_avx2gfni_aes;
					else
						test_setup(i) = raid_recX_avx2gfni_raid;
				}
				if (raid_cpu_has_avx512gfni()) {
					if (mode == RAID_MODE_CAUCHY_AES)
						test_setup(i) = raid_recX_avx512gfni_aes;
					else
						test_setup(i) = raid_recX_avx512gfni_raid;
				}
			}
#endif
#endif
#ifdef CONFIG_NEON
			test_setup(i) = raid_recX_neon;
#endif
#ifdef CONFIG_NEON32
			test_setup(i) = raid_recX_neon32;
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

int raid_test_rec2_g23(size_t size)
{
	static const int missing[][2] = {
		{ 1, 20 },  /* within the first coset */
		{ 50, 51 }, /* across the first G23 boundary */
		{ 10, 204 } /* across multiple cosets */
	};
	void *v_alloc;
	void **v;
	void *save[2];
	int ip[2] = { 0, 1 };
	int nv = RAID_DATA_MAX + RAID_PARITY_MAX + 3;
	int i, j;

	raid_mode(RAID_MODE_CAUCHY_AES);
	v = raid_malloc_vector(nv, size, &v_alloc);
	if (!v)
		return -1;

	memset(v[nv - 1], 0, size);
	raid_zero(v[nv - 1]);
	raid_mrand_vector(3, RAID_DATA_MAX, size, v);
	raid_gen_ref(RAID_DATA_MAX, RAID_PARITY_MAX, size, v);

	for (j = 0; j < (int)(sizeof(missing) / sizeof(missing[0])); ++j) {
		int id[2] = { missing[j][0], missing[j][1] };

		for (i = 0; i < 2; ++i) {
			save[i] = v[id[i]];
			v[id[i]] = v[RAID_DATA_MAX + RAID_PARITY_MAX + i];
		}

		raid_rec2_int8(2, id, ip, RAID_DATA_MAX, size, v);

		for (i = 0; i < 2; ++i) {
			if (memcmp(v[id[i]], save[i], size) != 0)
				goto bail;
			v[id[i]] = save[i];
		}
	}

	free(v_alloc);
	free(v);
	return 0;

bail:
	free(v_alloc);
	free(v);
	return -1;
}

int raid_test_tail(int mode, int nd_max, size_t size)
{
	void (*f[RAID_PARITY_MAX][32])(int nr, int *id, int *ip, int nd, size_t size, void **vbuf);
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
	int nd;

	raid_mode(mode);
	if (mode == RAID_MODE_VANDERMONDE_RAID)
		np = 3;
	else
		np = RAID_PARITY_MAX;

	nv = nd_max + np * 2 + 2;

	v = raid_malloc_vector(nv, size, &v_alloc);
	if (!v) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	data = v;
	parity = v + nd_max;
	test = v + nd_max + np;

	for (i = 0; i < np; ++i)
		parity_save[i] = parity[i];

	memset(v[nv - 2], 0, size);
	raid_zero(v[nv - 2]);

	waste = v[nv - 1];

	/* fill with pseudo-random data with the arbitrary seed "1" */
	raid_mrand_vector(1, nd_max, size, v);

	/* counters of functions for each parity level */
	for (i = 0; i < np; ++i)
		nf[i] = 0;

	/* setup recov functions */
	for (i = 1; i <= np; ++i) {
		if (i == 1) {
			test_setup(i) = raid_rec1_int8;
#ifdef CONFIG_X86
			if (raid_cpu_has_ssse3())
				test_setup(i) = raid_rec1_ssse3;
			if (raid_cpu_has_avx2())
				test_setup(i) = raid_rec1_avx2;
#ifdef CONFIG_X86_64
			if (mode == RAID_MODE_CAUCHY_RAID || mode == RAID_MODE_CAUCHY_AES) {
				if (raid_cpu_has_avx2gfni()) {
					if (mode == RAID_MODE_CAUCHY_AES)
						test_setup(i) = raid_rec1_avx2gfni_aes;
					else
						test_setup(i) = raid_rec1_avx2gfni_raid;
				}
				if (raid_cpu_has_avx512gfni()) {
					if (mode == RAID_MODE_CAUCHY_AES)
						test_setup(i) = raid_rec1_avx512gfni_aes;
					else
						test_setup(i) = raid_rec1_avx512gfni_raid;
				}
			}
#endif
#endif
#ifdef CONFIG_NEON
			test_setup(i) = raid_rec1_neon;
#endif
#ifdef CONFIG_NEON32
			test_setup(i) = raid_rec1_neon32;
#endif
		} else if (i == 2) {
			test_setup(i) = raid_rec2_int8;
#ifdef CONFIG_X86
			if (raid_cpu_has_ssse3())
				test_setup(i) = raid_rec2_ssse3;
			if (raid_cpu_has_avx2())
				test_setup(i) = raid_rec2_avx2;
#ifdef CONFIG_X86_64
			if (raid_cpu_has_avx512bw())
				test_setup(i) = raid_rec2_avx512bw;
			if (mode == RAID_MODE_CAUCHY_RAID || mode == RAID_MODE_CAUCHY_AES) {
				if (raid_cpu_has_avx2gfni()) {
					if (mode == RAID_MODE_CAUCHY_AES)
						test_setup(i) = raid_rec2_avx2gfni_aes;
					else
						test_setup(i) = raid_rec2_avx2gfni_raid;
				}
				if (raid_cpu_has_avx512gfni()) {
					if (mode == RAID_MODE_CAUCHY_AES)
						test_setup(i) = raid_rec2_avx512gfni_aes;
					else
						test_setup(i) = raid_rec2_avx512gfni_raid;
				}
			}
#endif
#endif
#ifdef CONFIG_NEON
			test_setup(i) = raid_rec2_neon;
#endif
#ifdef CONFIG_NEON32
			test_setup(i) = raid_rec2_neon32;
#endif
		} else {
			test_setup(i) = raid_recX_int8;
#ifdef CONFIG_X86
			if (raid_cpu_has_ssse3())
				test_setup(i) = raid_recX_ssse3;
			if (raid_cpu_has_avx2())
				test_setup(i) = raid_recX_avx2;
#ifdef CONFIG_X86_64
			if (raid_cpu_has_avx512bw())
				test_setup(i) = raid_recX_avx512bw;
			if (mode == RAID_MODE_CAUCHY_RAID || mode == RAID_MODE_CAUCHY_AES) {
				if (raid_cpu_has_avx2gfni()) {
					if (mode == RAID_MODE_CAUCHY_AES)
						test_setup(i) = raid_recX_avx2gfni_aes;
					else
						test_setup(i) = raid_recX_avx2gfni_raid;
				}
				if (raid_cpu_has_avx512gfni()) {
					if (mode == RAID_MODE_CAUCHY_AES)
						test_setup(i) = raid_recX_avx512gfni_aes;
					else
						test_setup(i) = raid_recX_avx512gfni_raid;
				}
			}
#endif
#endif
#ifdef CONFIG_NEON
			test_setup(i) = raid_recX_neon;
#endif
#ifdef CONFIG_NEON32
			test_setup(i) = raid_recX_neon32;
#endif
		}
	}

	/* test all disk counts from 1 to nd_max */
	for (nd = 1; nd <= nd_max; ++nd) {
		/* set data pointers */
		for (i = 0; i < nd; ++i)
			v[i] = data[i];

		/* set parity pointers */
		for (i = 0; i < np; ++i)
			v[nd + i] = parity_save[i];

		/* compute the parity for this disk count */
		raid_gen_ref(nd, np, size, v);

		/* set all the parity in v to the waste v */
		for (i = 0; i < np; ++i)
			v[nd + i] = waste;

		/* all parity levels up to min(nd, np) */
		for (nr = 1; nr <= np && nr <= nd; ++nr) {
			/* only test the tail: the last nr data disks */
			for (i = 0; i < nr; ++i)
				id[i] = nd - nr + i;

			/* all combinations (nr of np) parities */
			combination_first(nr, np, ip);
			do {
				/* for each recover function */
				for (j = 0; j < nf[nr - 1]; ++j) {
					/* set */
					for (i = 0; i < nr; ++i) {
						/* remove the missing data */
						data_save[i] = data[id[i]];
						v[id[i]] = test[i];
						/* set the parity to use */
						v[nd + ip[i]] = parity_save[ip[i]];
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
						v[id[i]] = data_save[i];
						/* restore the parity to waste */
						v[nd + ip[i]] = waste;
					}
				}
			} while (combination_next(nr, np, ip));
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

int raid_test_par(int mode, int nd, size_t size)
{
	void (*f[RAID_PARITY_MAX][32])(int nd, size_t size, void **vbuf);
	void *v_alloc;
	void **v;
	int nv;
	int i, j, k;
	int nf[RAID_PARITY_MAX];
	int np;

	raid_mode(mode);
	if (mode == RAID_MODE_VANDERMONDE_RAID)
		np = 3;
	else
		np = RAID_PARITY_MAX;

	nv = nd + np * 2;

	v = raid_malloc_vector(nv, size, &v_alloc);
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

	/* counters of functions for each parity level */
	for (i = 0; i < np; ++i)
		nf[i] = 0;

	/* setup all the available functions */
	test_setup(1) = raid_gen1_int32;
	test_setup(1) = raid_gen1_int64;
	if (mode == RAID_MODE_CAUCHY_AES) {
		test_setup(2) = raid_gen2_int32_aes;
		test_setup(2) = raid_gen2_int64_aes;
	} else {
		test_setup(2) = raid_gen2_int32_raid;
		test_setup(2) = raid_gen2_int64_raid;
	}

#ifdef CONFIG_NEON
	test_setup(1) = raid_gen1_neon;
	if (mode == RAID_MODE_CAUCHY_AES)
		test_setup(2) = raid_gen2_neon_aes;
	else
		test_setup(2) = raid_gen2_neon_raid;
#endif
#ifdef CONFIG_NEON32
	test_setup(1) = raid_gen1_neon32;
	if (mode == RAID_MODE_CAUCHY_AES)
		test_setup(2) = raid_gen2_neon32_aes;
	else
		test_setup(2) = raid_gen2_neon32_raid;
#endif

#ifdef CONFIG_X86
	if (raid_cpu_has_sse2()) {
		test_setup(1) = raid_gen1_sse2;
		if (mode == RAID_MODE_CAUCHY_AES)
			test_setup(2) = raid_gen2_sse2_aes;
		else
			test_setup(2) = raid_gen2_sse2_raid;
#ifdef CONFIG_X86_64
		if (mode == RAID_MODE_CAUCHY_AES)
			test_setup(2) = raid_gen2_sse2ext_aes;
		else
			test_setup(2) = raid_gen2_sse2ext_raid;
#endif
	}
	if (raid_cpu_has_avx2()) {
		test_setup(1) = raid_gen1_avx2;
		if (mode == RAID_MODE_CAUCHY_AES)
			test_setup(2) = raid_gen2_avx2_aes;
		else
			test_setup(2) = raid_gen2_avx2_raid;
#ifdef CONFIG_X86_64
		if (mode == RAID_MODE_CAUCHY_AES)
			test_setup(2) = raid_gen2_avx2ext_aes;
		else
			test_setup(2) = raid_gen2_avx2ext_raid;
#endif
	}
#ifdef CONFIG_X86_64
	if (raid_cpu_has_avx512bw()) {
		test_setup(1) = raid_gen1_avx512bw;
		test_setup(2) = raid_gen2_avx512bw;
	}
	if (raid_cpu_has_avx2gfni()) {
		if (mode == RAID_MODE_CAUCHY_AES)
			test_setup(2) = raid_gen2_avx2gfni_aes;
		else
			test_setup(2) = raid_gen2_avx2gfni_raid;
	}
	if (raid_cpu_has_avx512gfni()) {
		if (mode == RAID_MODE_CAUCHY_AES)
			test_setup(2) = raid_gen2_avx512gfni_aes;
		else
			test_setup(2) = raid_gen2_avx512gfni_raid;
	}
#endif
#endif

	if (mode == RAID_MODE_CAUCHY_RAID || mode == RAID_MODE_CAUCHY_AES) {
		test_setup(3) = raid_gen3_int8;
		test_setup(4) = raid_gen4_int8;
		test_setup(5) = raid_gen5_int8;
		test_setup(6) = raid_gen6_int8;

#ifdef CONFIG_NEON
		if (mode == RAID_MODE_CAUCHY_AES) {
			test_setup(3) = raid_gen3_neon_aes;
			test_setup(4) = raid_gen4_neon_aes;
			test_setup(5) = raid_gen5_neon_aes;
			test_setup(6) = raid_gen6_neon_aes;
		} else {
			test_setup(3) = raid_gen3_neon_raid;
			test_setup(4) = raid_gen4_neon_raid;
			test_setup(5) = raid_gen5_neon_raid;
			test_setup(6) = raid_gen6_neon_raid;
		}
#endif
#ifdef CONFIG_NEON32
		if (mode == RAID_MODE_CAUCHY_AES) {
			test_setup(3) = raid_gen3_neon32_aes;
			test_setup(4) = raid_gen4_neon32_aes;
			test_setup(5) = raid_gen5_neon32_aes;
			test_setup(6) = raid_gen6_neon32_aes;
		} else {
			test_setup(3) = raid_gen3_neon32_raid;
			test_setup(4) = raid_gen4_neon32_raid;
			test_setup(5) = raid_gen5_neon32_raid;
			test_setup(6) = raid_gen6_neon32_raid;
		}
#endif

#ifdef CONFIG_X86
		if (raid_cpu_has_ssse3()) {
			if (mode == RAID_MODE_CAUCHY_AES) {
				test_setup(3) = raid_gen3_ssse3_aes;
				test_setup(4) = raid_gen4_ssse3_aes;
				test_setup(5) = raid_gen5_ssse3_aes;
				test_setup(6) = raid_gen6_ssse3_aes;
			} else {
				test_setup(3) = raid_gen3_ssse3_raid;
				test_setup(4) = raid_gen4_ssse3_raid;
				test_setup(5) = raid_gen5_ssse3_raid;
				test_setup(6) = raid_gen6_ssse3_raid;
			}
#ifdef CONFIG_X86_64
			if (mode == RAID_MODE_CAUCHY_AES) {
				test_setup(3) = raid_gen3_ssse3ext_aes;
				test_setup(4) = raid_gen4_ssse3ext_aes;
				test_setup(5) = raid_gen5_ssse3ext_aes;
				test_setup(6) = raid_gen6_ssse3ext_aes;
			} else {
				test_setup(3) = raid_gen3_ssse3ext_raid;
				test_setup(4) = raid_gen4_ssse3ext_raid;
				test_setup(5) = raid_gen5_ssse3ext_raid;
				test_setup(6) = raid_gen6_ssse3ext_raid;
			}
#endif
		}
#ifdef CONFIG_X86_64
		if (raid_cpu_has_avx2()) {
			if (mode == RAID_MODE_CAUCHY_AES) {
				test_setup(3) = raid_gen3_avx2ext_aes;
				test_setup(4) = raid_gen4_avx2ext_aes;
				test_setup(5) = raid_gen5_avx2ext_aes;
				test_setup(6) = raid_gen6_avx2ext_aes;
			} else {
				test_setup(3) = raid_gen3_avx2ext_raid;
				test_setup(4) = raid_gen4_avx2ext_raid;
				test_setup(5) = raid_gen5_avx2ext_raid;
				test_setup(6) = raid_gen6_avx2ext_raid;
			}
		}
		if (raid_cpu_has_avx512bw()) {
			test_setup(3) = raid_gen3_avx512bw;
			test_setup(4) = raid_gen4_avx512bw;
			test_setup(5) = raid_gen5_avx512bw;
			test_setup(6) = raid_gen6_avx512bw;
		}
		if (raid_cpu_has_avx2gfni()) {
			if (mode == RAID_MODE_CAUCHY_AES) {
				test_setup(3) = raid_gen3_avx2gfni_aes;
				test_setup(4) = raid_gen4_avx2gfni_aes;
				test_setup(5) = raid_gen5_avx2gfni_aes;
				test_setup(6) = raid_gen6_avx2gfni_aes;
			} else {
				test_setup(3) = raid_gen3_avx2gfni_raid;
				test_setup(4) = raid_gen4_avx2gfni_raid;
				test_setup(5) = raid_gen5_avx2gfni_raid;
				test_setup(6) = raid_gen6_avx2gfni_raid;
			}
		}
		if (raid_cpu_has_avx512gfni()) {
			if (mode == RAID_MODE_CAUCHY_AES) {
				test_setup(3) = raid_gen3_avx512gfni_aes;
				test_setup(4) = raid_gen4_avx512gfni_aes;
				test_setup(5) = raid_gen5_avx512gfni_aes;
				test_setup(6) = raid_gen6_avx512gfni_aes;
			} else {
				test_setup(3) = raid_gen3_avx512gfni_raid;
				test_setup(4) = raid_gen4_avx512gfni_raid;
				test_setup(5) = raid_gen5_avx512gfni_raid;
				test_setup(6) = raid_gen6_avx512gfni_raid;
			}
		}
#endif
#endif
	} else {
		test_setup(3) = raid_genz_int32_raid;
		test_setup(3) = raid_genz_int64_raid;

#ifdef CONFIG_NEON
		test_setup(3) = raid_genz_neon_raid;
#endif
#ifdef CONFIG_NEON32
		test_setup(3) = raid_genz_neon32_raid;
#endif

#ifdef CONFIG_X86
		if (raid_cpu_has_sse2()) {
			test_setup(3) = raid_genz_sse2_raid;
#ifdef CONFIG_X86_64
			test_setup(3) = raid_genz_sse2ext_raid;
#endif
		}
#ifdef CONFIG_X86_64
		if (raid_cpu_has_avx2())
			test_setup(3) = raid_genz_avx2ext_raid;
#endif
#endif
	}

	/* check all the functions */
	for (k = 0; k < np; ++k) {
		for (j = 0; j < nf[k]; ++j) {
			/* change the buffers incrementing all values */
			for (i = 0; i <= k; ++i)
				meminc(v[nd + i], size);

			/* compute parity */
			f[k][j](nd, size, v);

			/* check it */
			for (i = 0; i < np; ++i) {
				if (memcmp(v[nd + np + i], v[nd + i], size) != 0) {
					/* LCOV_EXCL_START */
					goto bail;
					/* LCOV_EXCL_STOP */
				}
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
