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
#include "memory.h"
#include "cpu.h"

/*
 * Initializes and selects the best algorithm.
 */
void raid_init(void)
{
	raid_gen3_ptr = raid_gen3_int8;
	raid_gen_ptr[3] = raid_gen4_int8;
	raid_gen_ptr[4] = raid_gen5_int8;
	raid_gen_ptr[5] = raid_gen6_int8;

	if (sizeof(void *) == 4) {
		raid_gen_ptr[0] = raid_gen1_int32;
		raid_gen_ptr[1] = raid_gen2_int32;
		raid_genz_ptr = raid_genz_int32;
	} else {
		raid_gen_ptr[0] = raid_gen1_int64;
		raid_gen_ptr[1] = raid_gen2_int64;
		raid_genz_ptr = raid_genz_int64;
	}

	raid_rec_ptr[0] = raid_rec1_int8;
	raid_rec_ptr[1] = raid_rec2_int8;
	raid_rec_ptr[2] = raid_recX_int8;
	raid_rec_ptr[3] = raid_recX_int8;
	raid_rec_ptr[4] = raid_recX_int8;
	raid_rec_ptr[5] = raid_recX_int8;

#ifdef CONFIG_X86
#ifdef CONFIG_SSE2
	if (raid_cpu_has_sse2()) {
		raid_gen_ptr[0] = raid_gen1_sse2;
#ifdef CONFIG_X86_64
		if (raid_cpu_has_slowextendedreg()) {
			raid_gen_ptr[1] = raid_gen2_sse2;
		} else {
			raid_gen_ptr[1] = raid_gen2_sse2ext;
		}
		/* note that raid_cpu_has_slowextendedreg() doesn't affect parz */
		raid_genz_ptr = raid_genz_sse2ext;
#else
		raid_gen_ptr[1] = raid_gen2_sse2;
		raid_genz_ptr = raid_genz_sse2;
#endif
	}
#endif

#ifdef CONFIG_SSSE3
	if (raid_cpu_has_ssse3()) {
#ifdef CONFIG_X86_64
		if (raid_cpu_has_slowextendedreg()) {
			raid_gen3_ptr = raid_gen3_ssse3;
			raid_gen_ptr[3] = raid_gen4_ssse3;
			raid_gen_ptr[4] = raid_gen5_ssse3;
			raid_gen_ptr[5] = raid_gen6_ssse3;
		} else {
			raid_gen3_ptr = raid_gen3_ssse3ext;
			raid_gen_ptr[3] = raid_gen4_ssse3ext;
			raid_gen_ptr[4] = raid_gen5_ssse3ext;
			raid_gen_ptr[5] = raid_gen6_ssse3ext;
		}
#else
		raid_gen3_ptr = raid_gen3_ssse3;
		raid_gen_ptr[3] = raid_gen4_ssse3;
		raid_gen_ptr[4] = raid_gen5_ssse3;
		raid_gen_ptr[5] = raid_gen6_ssse3;
#endif
		raid_rec_ptr[0] = raid_rec1_ssse3;
		raid_rec_ptr[1] = raid_rec2_ssse3;
		raid_rec_ptr[2] = raid_recX_ssse3;
		raid_rec_ptr[3] = raid_recX_ssse3;
		raid_rec_ptr[4] = raid_recX_ssse3;
		raid_rec_ptr[5] = raid_recX_ssse3;
	}
#endif

#ifdef CONFIG_AVX2
	if (raid_cpu_has_avx2()) {
		raid_gen_ptr[0] = raid_gen1_avx2;
		raid_gen_ptr[1] = raid_gen2_avx2;
#ifdef CONFIG_X86_64
		raid_gen3_ptr = raid_gen3_avx2ext;
		raid_genz_ptr = raid_genz_avx2ext;
		raid_gen_ptr[3] = raid_gen4_avx2ext;
		raid_gen_ptr[4] = raid_gen5_avx2ext;
		raid_gen_ptr[5] = raid_gen6_avx2ext;
#endif
		raid_rec_ptr[0] = raid_rec1_avx2;
		raid_rec_ptr[1] = raid_rec2_avx2;
		raid_rec_ptr[2] = raid_recX_avx2;
		raid_rec_ptr[3] = raid_recX_avx2;
		raid_rec_ptr[4] = raid_recX_avx2;
		raid_rec_ptr[5] = raid_recX_avx2;
	}
#endif
#endif /* CONFIG_X86 */

	/* set the default mode */
	raid_mode(RAID_MODE_CAUCHY);
}

/*
 * Reference parity computation.
 */
void raid_gen_ref(int nd, int np, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	size_t i;

	for (i = 0; i < size; ++i) {
		uint8_t p[RAID_PARITY_MAX];
		int j, d;

		for (j = 0; j < np; ++j)
			p[j] = 0;

		for (d = 0; d < nd; ++d) {
			uint8_t b = v[d][i];

			for (j = 0; j < np; ++j)
				p[j] ^= gfmul[b][gfgen[j][d]];
		}

		for (j = 0; j < np; ++j)
			v[nd + j][i] = p[j];
	}
}

/*
 * Size of the blocks to test.
 */
#define TEST_SIZE 4096

/*
 * Number of data blocks to test.
 */
#define TEST_COUNT (65536 / TEST_SIZE)

/*
 * Parity generation test.
 */
static int raid_test_par(int nd, int np, size_t size, void **v, void **ref)
{
	int i;
	void *t[TEST_COUNT + RAID_PARITY_MAX];

	/* setup data */
	for (i = 0; i < nd; ++i)
		t[i] = ref[i];

	/* setup parity */
	for (i = 0; i < np; ++i)
		t[nd + i] = v[nd + i];

	raid_gen(nd, np, size, t);

	/* compare parity */
	for (i = 0; i < np; ++i) {
		if (memcmp(t[nd + i], ref[nd + i], size) != 0) {
			/* LCOV_EXCL_START */
			return -1;
			/* LCOV_EXCL_STOP */
		}
	}

	return 0;
}

/*
 * Recovering test.
 */
static int raid_test_rec(int nr, int *ir, int nd, int np, size_t size, void **v, void **ref)
{
	int i, j;
	void *t[TEST_COUNT + RAID_PARITY_MAX];

	/* setup data and parity vector */
	for (i = 0, j = 0; i < nd + np; ++i) {
		if (j < nr && ir[j] == i) {
			/* this block has to be recovered */
			t[i] = v[i];
			++j;
		} else {
			/* this block is used for recovering */
			t[i] = ref[i];
		}
	}

	raid_rec(nr, ir, nd, np, size, t);

	/* compare all data and parity */
	for (i = 0; i < nd + np; ++i) {
		if (t[i] != ref[i]
			&& memcmp(t[i], ref[i], size) != 0) {
			/* LCOV_EXCL_START */
			return -1;
			/* LCOV_EXCL_STOP */
		}
	}

	return 0;
}

/*
 * Recovering test for data.
 */
static int raid_test_data(int nr, int *id, int *ip, int nd, int np, size_t size, void **v, void **ref)
{
	int i, j;
	void *t[TEST_COUNT + RAID_PARITY_MAX];

	/* setup data vector */
	for (i = 0, j = 0; i < nd; ++i) {
		if (j < nr && id[j] == i) {
			/* this block has to be recovered */
			t[i] = v[i];
			++j;
		} else {
			/* this block is left unchanged */
			t[i] = ref[i];
		}
	}

	/* setup parity vector */
	for (i = 0, j = 0; i < np; ++i) {
		if (j < nr && ip[j] == i) {
			/* this block is used for recovering */
			t[nd + i] = ref[nd + i];
			++j;
		} else {
			/* this block should not be read or written */
			t[nd + i] = 0;
		}
	}

	raid_data(nr, id, ip, nd, size, t);

	/* compare all data and parity */
	for (i = 0; i < nd; ++i) {
		if (t[i] != ref[i]
			&& t[i] != 0
			&& memcmp(t[i], ref[i], size) != 0) {
			/* LCOV_EXCL_START */
			return -1;
			/* LCOV_EXCL_STOP */
		}
	}

	return 0;
}

/*
 * Scan test.
 */
static int raid_test_scan(int nr, int *ir, int nd, int np, size_t size, void **v, void **ref)
{
	int i, j, ret;
	void *t[TEST_COUNT + RAID_PARITY_MAX];
	int is[RAID_PARITY_MAX];

	/* setup data and parity vector */
	for (i = 0, j = 0; i < nd + np; ++i) {
		if (j < nr && ir[j] == i) {
			/* this block is bad */
			t[i] = v[i];
			++j;
		} else {
			/* this block is used for recovering */
			t[i] = ref[i];
		}
	}

	ret = raid_scan(is, nd, np, size, t);

	/* compare identified bad blocks */
	if (ret != nr)
		return -1;
	for (i = 0; i < nr; ++i) {
		if (ir[i] != is[i]) {
			/* LCOV_EXCL_START */
			return -1;
			/* LCOV_EXCL_STOP */
		}
	}

	return 0;
}

/*
 * Basic functionality self test.
 */
int raid_selftest(void)
{
	const int nd = TEST_COUNT;
	const size_t size = TEST_SIZE;
	const int nv = nd + RAID_PARITY_MAX * 2 + 1;
	void *v_alloc;
	void **v;
	void *ref[nd + RAID_PARITY_MAX];
	int ir[RAID_PARITY_MAX];
	int ip[RAID_PARITY_MAX];
	int i, np;
	int ret = 0;

	/* ensure to have enough space for data */
	BUG_ON(nd * size > 65536);

	v = raid_malloc_vector(nd, nv, size, &v_alloc);
	if (!v) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	memset(v[nv - 1], 0, size);
	raid_zero(v[nv - 1]);

	/* use the multiplication table as data */
	for (i = 0; i < nd; ++i)
		ref[i] = ((uint8_t *)gfmul) + size * i;

	/* setup reference parity */
	for (i = 0; i < RAID_PARITY_MAX; ++i)
		ref[nd + i] = v[nd + RAID_PARITY_MAX + i];

	/* compute reference parity */
	raid_gen_ref(nd, RAID_PARITY_MAX, size, ref);

	/* test for each parity level */
	for (np = 1; np <= RAID_PARITY_MAX; ++np) {
		/* test parity generation */
		ret = raid_test_par(nd, np, size, v, ref);
		if (ret != 0) {
			/* LCOV_EXCL_START */
			goto bail;
			/* LCOV_EXCL_STOP */
		}

		/* test recovering with broken ending data disks */
		for (i = 0; i < np; ++i) {
			/* bad data */
			ir[i] = nd - np + i;

			/* good parity */
			ip[i] = i;
		}

		ret = raid_test_rec(np, ir, nd, np, size, v, ref);
		if (ret != 0) {
			/* LCOV_EXCL_START */
			goto bail;
			/* LCOV_EXCL_STOP */
		}

		ret = raid_test_data(np, ir, ip, nd, np, size, v, ref);
		if (ret != 0) {
			/* LCOV_EXCL_START */
			goto bail;
			/* LCOV_EXCL_STOP */
		}

		/* test recovering with broken leading data and broken leading parity */
		for (i = 0; i < np / 2; ++i) {
			/* bad data */
			ir[i] = i;

			/* good parity */
			ip[i] = (np + 1) / 2 + i;
		}

		/* bad parity */
		for (i = 0; i < (np + 1) / 2; ++i)
			ir[np / 2 + i] = nd + i;

		ret = raid_test_rec(np, ir, nd, np, size, v, ref);
		if (ret != 0) {
			/* LCOV_EXCL_START */
			goto bail;
			/* LCOV_EXCL_STOP */
		}

		ret = raid_test_data(np / 2, ir, ip, nd, np, size, v, ref);
		if (ret != 0) {
			/* LCOV_EXCL_START */
			goto bail;
			/* LCOV_EXCL_STOP */
		}

		/* test recovering with broken leading data and broken ending parity */
		for (i = 0; i < np / 2; ++i) {
			/* bad data */
			ir[i] = i;

			/* good parity */
			ip[i] = i;
		}

		/* bad parity */
		for (i = 0; i < (np + 1) / 2; ++i)
			ir[np / 2 + i] = nd + np - (np + 1) / 2 + i;

		ret = raid_test_rec(np, ir, nd, np, size, v, ref);
		if (ret != 0) {
			/* LCOV_EXCL_START */
			goto bail;
			/* LCOV_EXCL_STOP */
		}

		ret = raid_test_data(np / 2, ir, ip, nd, np, size, v, ref);
		if (ret != 0) {
			/* LCOV_EXCL_START */
			goto bail;
			/* LCOV_EXCL_STOP */
		}

		/* scan test with broken data and parity */
		for (i = 0; i < np / 2; ++i) {
			/* bad data */
			ir[i] = i;
		}
		for (i = 0; i < (np - 1) / 2; ++i) {
			/* bad parity */
			ir[np / 2 + i] = nd + i;
		}
		for (i = 0; i < np - 1; ++i) {
			/* make blocks bad */
			/* we cannot fill them with 0, because the original */
			/* data may be already filled with 0 */
			memset(v[ir[i]], 0x55, size);
		}

		ret = raid_test_scan(np - 1, ir, nd, np, size, v, ref);
		if (ret != 0) {
			/* LCOV_EXCL_START */
			goto bail;
			/* LCOV_EXCL_STOP */
		}
	}

	/* scan test with no parity */
	ret = raid_test_scan(0, 0, nd, 0, size, v, ref);
	if (ret != -1) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}

	ret = 0;

bail:
	free(v);
	free(v_alloc);

	return ret;
}

