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

#ifdef CONFIG_X86_64
	int d, p;

	for (d = 0; d < RAID_DATA_MAX; ++d) {
		for (p = 1; p < RAID_PARITY_MAX; ++p) {
			if (memcmp(raid_gfcauchyaffine_raid[d][p - 1],
				raid_gfaffine_raid[raid_gfcauchy_raid[p][d]],
				8) != 0)
				return -1;
		}
	}
#endif

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
 *       primitive generator g=3
 *
 * The parity generation tests compare optimized implementations against
 * raid_gen_ref(), but both use the same active generator matrix. Therefore
 * they can agree even if the matrix itself is mathematically unsuitable.
 *
 * This test instead checks a few fundamental properties of the field
 * generator and of the generated Cauchy matrix.
 */
int raid_test_poly(unsigned mode)
{
	int i, j;
	int p, d;

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
	 *     raid_gfexp
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
	 * Primitive order alone is not enough to protect parity format
	 * compatibility. Each Cauchy mode has a fixed reducing polynomial
	 * and primitive generator, which determine the actual matrix coefficients.
	 */
	if (mode == RAID_MODE_CAUCHY_RAID) {
		if (raid_poly_byte != RAID_POLY_RAID)
			return -1;
		if (raid_gfexp[1] != 2)
			return -1;
	} else if (mode == RAID_MODE_CAUCHY_AES) {
		if (raid_poly_byte != RAID_POLY_AES)
			return -1;
		if (raid_gfexp[1] != 3)
			return -1;
	}

	/*
	 * raid_gfexp[a] contains g^a for the primitive generator associated
	 * with the active Cauchy mode.
	 *
	 * The multiplicative group GF(2^8)* contains 255 elements:
	 *
	 *     255 = 3 * 5 * 17
	 *
	 * The order of every nonzero field element divides 255.
	 *
	 * To prove that g has order exactly 255, it is therefore sufficient
	 * to verify:
	 *
	 *     g^255 == 1
	 *
	 * and to exclude the three maximal proper divisors:
	 *
	 *     255 / 3  = 85
	 *     255 / 5  = 51
	 *     255 / 17 = 15
	 *
	 * Hence a primitive generator must satisfy:
	 *
	 *     g^85 != 1
	 *     g^51 != 1
	 *     g^15 != 1
	 */
	if (raid_gfexp[0] != 1)
		return -1;
	if (raid_gfexp[255] != 1)
		return -1;
	if (raid_gfexp[85] == 1)
		return -1;
	if (raid_gfexp[51] == 1)
		return -1;
	if (raid_gfexp[15] == 1)
		return -1;

	/*
	 * The Q row of the Extended Cauchy matrix contains:
	 *
	 *     1, g, g^2, g^3, ...
	 *
	 * SnapRAID supports RAID_DATA_MAX data disks, so all generator powers
	 * corresponding to those columns must be distinct.
	 *
	 * A primitive generator of order 255 guarantees this, but checking it
	 * explicitly is inexpensive and also verifies the generated
	 * raid_gfexp[] table itself.
	 */
	for (i = 0; i < RAID_DATA_MAX; ++i)
		for (j = i + 1; j < RAID_DATA_MAX; ++j)
			if (raid_gfexp[i] == raid_gfexp[j])
				return -1;

	/*
	 * The second row of the Extended Cauchy matrix is exactly g^d.
	 *
	 * Verify that the exponent table and the actual matrix used for
	 * parity generation agree for every supported data column.
	 *
	 * This makes sure that the primitive generator checked above is the
	 * same generator actually used by the Q row.
	 */
	for (d = 0; d < RAID_DATA_MAX; ++d)
		if (raid_gfcauchy[1][d] != raid_gfexp[d])
			return -1;

	/*
	 * Every coefficient of the active 6x251 Cauchy generator matrix must
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
		switch (i) {
		case 1:
			test_setup(i) = raid_rec1_int8;
			break;
		case 2:
			test_setup(i) = raid_rec2_int8;
			break;
		default:
			test_setup(i) = raid_recX_int8;
			break;
		}
#ifdef CONFIG_X86
		if (raid_cpu_has_ssse3()) {
			switch (i) {
			case 1:
				test_setup(i) = raid_rec1_ssse3;
				break;
			case 2:
				test_setup(i) = raid_rec2_ssse3;
				break;
			case 3:
				test_setup(i) = raid_rec3_ssse3;
				break;
			case 4:
				test_setup(i) = raid_rec4_ssse3;
				break;
			case 5:
				test_setup(i) = raid_rec5_ssse3;
				break;
			case 6:
				test_setup(i) = raid_rec6_ssse3;
				break;
			}
#ifdef CONFIG_X86_64
			switch (i) {
			case 1:
				test_setup(i) = raid_rec1_ssse3ext;
				break;
			case 2:
				test_setup(i) = raid_rec2_ssse3ext;
				break;
			case 3:
				test_setup(i) = raid_rec3_ssse3ext;
				break;
			case 4:
				test_setup(i) = raid_rec4_ssse3ext;
				break;
			case 5:
				test_setup(i) = raid_rec5_ssse3ext;
				break;
			case 6:
				test_setup(i) = raid_rec6_ssse3ext;
				break;
			}
#endif
		}
		if (raid_cpu_has_avx2()) {
			switch (i) {
			case 1:
				test_setup(i) = raid_rec1_avx2;
				break;
			case 2:
				test_setup(i) = raid_rec2_avx2;
				break;
			case 3:
				test_setup(i) = raid_rec3_avx2;
				break;
			case 4:
				test_setup(i) = raid_rec4_avx2;
				break;
			case 5:
				test_setup(i) = raid_rec5_avx2;
				break;
			case 6:
				test_setup(i) = raid_rec6_avx2;
				break;
			}
		}
#ifdef CONFIG_X86_64
		if (raid_cpu_has_avx512bw()) {
			switch (i) {
			case 1:
				test_setup(i) = raid_rec1_avx512bw;
				break;
			case 2:
				test_setup(i) = raid_rec2_avx512bw;
				break;
			case 3:
				test_setup(i) = raid_rec3_avx512bw;
				break;
			case 4:
				test_setup(i) = raid_rec4_avx512bw;
				break;
			case 5:
				test_setup(i) = raid_rec5_avx512bw;
				break;
			case 6:
				test_setup(i) = raid_rec6_avx512bw;
				break;
			}
		}
		if (mode == RAID_MODE_CAUCHY_RAID || mode == RAID_MODE_CAUCHY_AES) {
			if (raid_cpu_has_avx2gfni()) {
				if (mode == RAID_MODE_CAUCHY_AES) {
					switch (i) {
					case 1:
						test_setup(i) = raid_rec1_avx2gfni_aes;
						break;
					case 2:
						test_setup(i) = raid_rec2_avx2gfni_aes;
						break;
					case 3:
						test_setup(i) = raid_rec3_avx2gfni_aes;
						break;
					case 4:
						test_setup(i) = raid_rec4_avx2gfni_aes;
						break;
					case 5:
						test_setup(i) = raid_rec5_avx2gfni_aes;
						break;
					case 6:
						test_setup(i) = raid_rec6_avx2gfni_aes;
						break;
					}
				} else {
					switch (i) {
					case 1:
						test_setup(i) = raid_rec1_avx2gfni_raid;
						break;
					case 2:
						test_setup(i) = raid_rec2_avx2gfni_raid;
						break;
					case 3:
						test_setup(i) = raid_rec3_avx2gfni_raid;
						break;
					case 4:
						test_setup(i) = raid_rec4_avx2gfni_raid;
						break;
					case 5:
						test_setup(i) = raid_rec5_avx2gfni_raid;
						break;
					case 6:
						test_setup(i) = raid_rec6_avx2gfni_raid;
						break;
					}
				}
			}
			if (raid_cpu_has_avx512gfni()) {
				if (mode == RAID_MODE_CAUCHY_AES) {
					switch (i) {
					case 1:
						test_setup(i) = raid_rec1_avx512gfni_aes;
						break;
					case 2:
						test_setup(i) = raid_rec2_avx512gfni_aes;
						break;
					case 3:
						test_setup(i) = raid_rec3_avx512gfni_aes;
						break;
					case 4:
						test_setup(i) = raid_rec4_avx512gfni_aes;
						break;
					case 5:
						test_setup(i) = raid_rec5_avx512gfni_aes;
						break;
					case 6:
						test_setup(i) = raid_rec6_avx512gfni_aes;
						break;
					}
				} else {
					switch (i) {
					case 1:
						test_setup(i) = raid_rec1_avx512gfni_raid;
						break;
					case 2:
						test_setup(i) = raid_rec2_avx512gfni_raid;
						break;
					case 3:
						test_setup(i) = raid_rec3_avx512gfni_raid;
						break;
					case 4:
						test_setup(i) = raid_rec4_avx512gfni_raid;
						break;
					case 5:
						test_setup(i) = raid_rec5_avx512gfni_raid;
						break;
					case 6:
						test_setup(i) = raid_rec6_avx512gfni_raid;
						break;
					}
				}
			}
		}
#endif
#endif
#ifdef CONFIG_NEON
		switch (i) {
		case 1:
			test_setup(i) = raid_rec1_neon;
			break;
		case 2:
			test_setup(i) = raid_rec2_neon;
			break;
		case 3:
			test_setup(i) = raid_rec3_neon;
			break;
		case 4:
			test_setup(i) = raid_rec4_neon;
			break;
		case 5:
			test_setup(i) = raid_rec5_neon;
			break;
		case 6:
			test_setup(i) = raid_rec6_neon;
			break;
		}
#endif
#ifdef CONFIG_NEON32
		switch (i) {
		case 1:
			test_setup(i) = raid_rec1_neon32;
			break;
		case 2:
			test_setup(i) = raid_rec2_neon32;
			break;
		case 3:
			test_setup(i) = raid_rec3_neon32;
			break;
		case 4:
			test_setup(i) = raid_rec4_neon32;
			break;
		case 5:
			test_setup(i) = raid_rec5_neon32;
			break;
		case 6:
			test_setup(i) = raid_rec6_neon32;
			break;
		}
#endif
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
		switch (i) {
		case 1:
			test_setup(i) = raid_rec1_int8;
			break;
		case 2:
			test_setup(i) = raid_rec2_int8;
			break;
		default:
			test_setup(i) = raid_recX_int8;
			break;
		}
#ifdef CONFIG_X86
		if (raid_cpu_has_ssse3()) {
			switch (i) {
			case 1:
				test_setup(i) = raid_rec1_ssse3;
				break;
			case 2:
				test_setup(i) = raid_rec2_ssse3;
				break;
			case 3:
				test_setup(i) = raid_rec3_ssse3;
				break;
			case 4:
				test_setup(i) = raid_rec4_ssse3;
				break;
			case 5:
				test_setup(i) = raid_rec5_ssse3;
				break;
			case 6:
				test_setup(i) = raid_rec6_ssse3;
				break;
			}
#ifdef CONFIG_X86_64
			switch (i) {
			case 1:
				test_setup(i) = raid_rec1_ssse3ext;
				break;
			case 2:
				test_setup(i) = raid_rec2_ssse3ext;
				break;
			case 3:
				test_setup(i) = raid_rec3_ssse3ext;
				break;
			case 4:
				test_setup(i) = raid_rec4_ssse3ext;
				break;
			case 5:
				test_setup(i) = raid_rec5_ssse3ext;
				break;
			case 6:
				test_setup(i) = raid_rec6_ssse3ext;
				break;
			}
#endif
		}
		if (raid_cpu_has_avx2()) {
			switch (i) {
			case 1:
				test_setup(i) = raid_rec1_avx2;
				break;
			case 2:
				test_setup(i) = raid_rec2_avx2;
				break;
			case 3:
				test_setup(i) = raid_rec3_avx2;
				break;
			case 4:
				test_setup(i) = raid_rec4_avx2;
				break;
			case 5:
				test_setup(i) = raid_rec5_avx2;
				break;
			case 6:
				test_setup(i) = raid_rec6_avx2;
				break;
			}
		}
#ifdef CONFIG_X86_64
		if (raid_cpu_has_avx512bw()) {
			switch (i) {
			case 1:
				test_setup(i) = raid_rec1_avx512bw;
				break;
			case 2:
				test_setup(i) = raid_rec2_avx512bw;
				break;
			case 3:
				test_setup(i) = raid_rec3_avx512bw;
				break;
			case 4:
				test_setup(i) = raid_rec4_avx512bw;
				break;
			case 5:
				test_setup(i) = raid_rec5_avx512bw;
				break;
			case 6:
				test_setup(i) = raid_rec6_avx512bw;
				break;
			}
		}
		if (mode == RAID_MODE_CAUCHY_RAID || mode == RAID_MODE_CAUCHY_AES) {
			if (raid_cpu_has_avx2gfni()) {
				if (mode == RAID_MODE_CAUCHY_AES) {
					switch (i) {
					case 1:
						test_setup(i) = raid_rec1_avx2gfni_aes;
						break;
					case 2:
						test_setup(i) = raid_rec2_avx2gfni_aes;
						break;
					case 3:
						test_setup(i) = raid_rec3_avx2gfni_aes;
						break;
					case 4:
						test_setup(i) = raid_rec4_avx2gfni_aes;
						break;
					case 5:
						test_setup(i) = raid_rec5_avx2gfni_aes;
						break;
					case 6:
						test_setup(i) = raid_rec6_avx2gfni_aes;
						break;
					}
				} else {
					switch (i) {
					case 1:
						test_setup(i) = raid_rec1_avx2gfni_raid;
						break;
					case 2:
						test_setup(i) = raid_rec2_avx2gfni_raid;
						break;
					case 3:
						test_setup(i) = raid_rec3_avx2gfni_raid;
						break;
					case 4:
						test_setup(i) = raid_rec4_avx2gfni_raid;
						break;
					case 5:
						test_setup(i) = raid_rec5_avx2gfni_raid;
						break;
					case 6:
						test_setup(i) = raid_rec6_avx2gfni_raid;
						break;
					}
				}
			}
			if (raid_cpu_has_avx512gfni()) {
				if (mode == RAID_MODE_CAUCHY_AES) {
					switch (i) {
					case 1:
						test_setup(i) = raid_rec1_avx512gfni_aes;
						break;
					case 2:
						test_setup(i) = raid_rec2_avx512gfni_aes;
						break;
					case 3:
						test_setup(i) = raid_rec3_avx512gfni_aes;
						break;
					case 4:
						test_setup(i) = raid_rec4_avx512gfni_aes;
						break;
					case 5:
						test_setup(i) = raid_rec5_avx512gfni_aes;
						break;
					case 6:
						test_setup(i) = raid_rec6_avx512gfni_aes;
						break;
					}
				} else {
					switch (i) {
					case 1:
						test_setup(i) = raid_rec1_avx512gfni_raid;
						break;
					case 2:
						test_setup(i) = raid_rec2_avx512gfni_raid;
						break;
					case 3:
						test_setup(i) = raid_rec3_avx512gfni_raid;
						break;
					case 4:
						test_setup(i) = raid_rec4_avx512gfni_raid;
						break;
					case 5:
						test_setup(i) = raid_rec5_avx512gfni_raid;
						break;
					case 6:
						test_setup(i) = raid_rec6_avx512gfni_raid;
						break;
					}
				}
			}
		}
#endif
#endif
#ifdef CONFIG_NEON
		switch (i) {
		case 1:
			test_setup(i) = raid_rec1_neon;
			break;
		case 2:
			test_setup(i) = raid_rec2_neon;
			break;
		case 3:
			test_setup(i) = raid_rec3_neon;
			break;
		case 4:
			test_setup(i) = raid_rec4_neon;
			break;
		case 5:
			test_setup(i) = raid_rec5_neon;
			break;
		case 6:
			test_setup(i) = raid_rec6_neon;
			break;
		}
#endif
#ifdef CONFIG_NEON32
		switch (i) {
		case 1:
			test_setup(i) = raid_rec1_neon32;
			break;
		case 2:
			test_setup(i) = raid_rec2_neon32;
			break;
		case 3:
			test_setup(i) = raid_rec3_neon32;
			break;
		case 4:
			test_setup(i) = raid_rec4_neon32;
			break;
		case 5:
			test_setup(i) = raid_rec5_neon32;
			break;
		case 6:
			test_setup(i) = raid_rec6_neon32;
			break;
		}
#endif
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
	test_setup(2) = raid_gen2_int8;
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
