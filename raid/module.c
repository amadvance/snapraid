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
	raid_par3_ptr = raid_par3_int8;
	raid_par_ptr[3] = raid_par4_int8;
	raid_par_ptr[4] = raid_par5_int8;
	raid_par_ptr[5] = raid_par6_int8;

	if (sizeof(void *) == 4) {
		raid_par_ptr[0] = raid_par1_int32;
		raid_par_ptr[1] = raid_par2_int32;
		raid_parz_ptr = raid_parz_int32;
	} else {
		raid_par_ptr[0] = raid_par1_int64;
		raid_par_ptr[1] = raid_par2_int64;
		raid_parz_ptr = raid_parz_int64;
	}

	raid_rec_ptr[0] = raid_rec1_int8;
	raid_rec_ptr[1] = raid_rec2_int8;
	raid_rec_ptr[2] = raid_recX_int8;
	raid_rec_ptr[3] = raid_recX_int8;
	raid_rec_ptr[4] = raid_recX_int8;
	raid_rec_ptr[5] = raid_recX_int8;

#ifdef CONFIG_X86
	if (raid_cpu_has_sse2()) {
		raid_par_ptr[0] = raid_par1_sse2;
#ifdef CONFIG_X86_64
		if (raid_cpu_has_slowextendedreg()) {
			raid_par_ptr[1] = raid_par2_sse2;
			raid_parz_ptr = raid_parz_sse2;
		} else {
			raid_par_ptr[1] = raid_par2_sse2ext;
			raid_parz_ptr = raid_parz_sse2ext;
		}
#else
		raid_par_ptr[1] = raid_par2_sse2;
		raid_parz_ptr = raid_parz_sse2;
#endif
	}

	if (raid_cpu_has_ssse3()) {
#ifdef CONFIG_X86_64
		raid_par3_ptr = raid_par3_ssse3ext;
		raid_par_ptr[3] = raid_par4_ssse3ext;
		raid_par_ptr[4] = raid_par5_ssse3ext;
		raid_par_ptr[5] = raid_par6_ssse3ext;
#else
		raid_par3_ptr = raid_par3_ssse3;
		raid_par_ptr[3] = raid_par4_ssse3;
		raid_par_ptr[4] = raid_par5_ssse3;
		raid_par_ptr[5] = raid_par6_ssse3;
#endif
		raid_rec_ptr[0] = raid_rec1_ssse3;
		raid_rec_ptr[1] = raid_rec2_ssse3;
		raid_rec_ptr[2] = raid_recX_ssse3;
		raid_rec_ptr[3] = raid_recX_ssse3;
		raid_rec_ptr[4] = raid_recX_ssse3;
		raid_rec_ptr[5] = raid_recX_ssse3;
	}
#endif

	/* set the default mode */
	raid_mode(RAID_MODE_CAUCHY);
}

/*
 * Refence parity computation.
 */
void raid_par_ref(int nd, int np, size_t size, void **vv)
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
		t[nd+i] = v[nd+i];

	raid_par(nd, np, size, t);

	/* compare parity */
	for (i = 0; i < np; ++i)
		if (memcmp(t[nd+i], ref[nd+i], size) != 0)
			return -1;

	return 0;
}

/*
 * Recovering test.
 */
static int raid_test_rec(int nrd, int *id, int nrp, int *ip, int nd, int np, size_t size, void **v, void **ref)
{
	int i, j;
	void *t[TEST_COUNT + RAID_PARITY_MAX];

	/* setup data */
	for (i = 0, j = 0; i < nd; ++i) {
		if (j < nrd && id[j] == i) {
			t[i] = v[i];
			++j;
		} else {
			t[i] = ref[i];
		}
	}

	/* setup parity */
	for (i = 0, j = 0; i < np; ++i) {
		if (j < nrp && ip[j] == i) {
			t[nd+i] = v[nd+i];
			++j;
		} else {
			t[nd+i] = ref[nd+i];
		}
	}

	raid_rec(nrd, id, nrp, ip, nd, np, size, t);

	/* compare all data and parity */
	for (i = 0; i < nd+np; ++i)
		if (t[i] != ref[i] && memcmp(t[i], ref[i], size) != 0)
			return -1;

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
	int id[RAID_PARITY_MAX];
	int ip[RAID_PARITY_MAX];
	int i, np;
	int ret = 0;

	/* ensure to have enough space for data */
	BUG_ON(nd * size > 65536);

	v = raid_malloc_vector(nd, nv, size, &v_alloc);
	if (!v)
		return -1;

	memset(v[nv-1], 0, size);
	raid_zero(v[nv-1]);

	/* use the multiplication table as data */
	for (i = 0; i < nd; ++i)
		ref[i] = ((uint8_t *)gfmul) + size * i;

	/* setup reference parity */
	for (i = 0; i < RAID_PARITY_MAX; ++i)
		ref[nd+i] = v[nd+RAID_PARITY_MAX+i];

	/* compute reference parity */
	raid_par_ref(nd, RAID_PARITY_MAX, size, ref);

	/* test for each parity level */
	for (np = 1; np <= RAID_PARITY_MAX; ++np) {
		/* test parity generation */
		ret = raid_test_par(nd, np, size, v, ref);
		if (ret != 0)
			goto bail;

		/* test recovering with full broken data disks */
		for (i = 0; i < np; ++i)
			id[i] = nd - np + i;

		ret = raid_test_rec(np, id, 0, ip, nd, np, size, v, ref);
		if (ret != 0)
			goto bail;

		/* test recovering with half broken data and ending parity */
		for (i = 0; i < np / 2; ++i)
			id[i] = i;

		for (i = 0; i < (np + 1) / 2; ++i)
			ip[i] = np - np / 2 + i;

		ret = raid_test_rec(np / 2, id, np / 2, ip, nd, np, size, v, ref);
		if (ret != 0)
			goto bail;

		/* test recovering with half broken data and leading parity */
		for (i = 0; i < np / 2; ++i)
			id[i] = i;

		for (i = 0; i < (np + 1) / 2; ++i)
			ip[i] = i;

		ret = raid_test_rec(np / 2, id, np / 2, ip, nd, np, size, v, ref);
		if (ret != 0)
			goto bail;
	}

bail:
	free(v_alloc);

	return ret;
}

static struct raid_func {
	const char *name;
	void *p;
} RAID_FUNC[] = {
	{ "int8", raid_par3_int8 },
	{ "int8", raid_par4_int8 },
	{ "int8", raid_par5_int8 },
	{ "int8", raid_par6_int8 },
	{ "int32", raid_par1_int32 },
	{ "int64", raid_par1_int64 },
	{ "int32", raid_par2_int32 },
	{ "int64", raid_par2_int64 },
	{ "int32", raid_parz_int32 },
	{ "int64", raid_parz_int64 },
	{ "int8", raid_rec1_int8 },
	{ "int8", raid_rec2_int8 },
	{ "int8", raid_recX_int8 },

#ifdef CONFIG_X86
	{ "sse2", raid_par1_sse2 },
	{ "sse2", raid_par2_sse2 },
	{ "sse2", raid_parz_sse2 },
	{ "ssse3", raid_par3_ssse3 },
	{ "ssse3", raid_par4_ssse3 },
	{ "ssse3", raid_par5_ssse3 },
	{ "ssse3", raid_par6_ssse3 },
	{ "ssse3", raid_rec1_ssse3 },
	{ "ssse3", raid_rec2_ssse3 },
	{ "ssse3", raid_recX_ssse3 },
#endif

#ifdef CONFIG_X86_64
	{ "sse2e", raid_par2_sse2ext },
	{ "sse2e", raid_parz_sse2ext },
	{ "ssse3e", raid_par3_ssse3ext },
	{ "ssse3e", raid_par4_ssse3ext },
	{ "ssse3e", raid_par5_ssse3ext },
	{ "ssse3e", raid_par6_ssse3ext },
#endif
	{ 0, 0 }
};

static const char *raid_tag(void *func)
{
	struct raid_func *i = RAID_FUNC;
	while (i->name != 0) {
		if (i->p == func)
			return i->name;
		++i;
	}
	return "unknown";
}

const char *raid_par1_tag(void)
{
	return raid_tag(raid_par_ptr[0]);
}

const char *raid_par2_tag(void)
{
	return raid_tag(raid_par_ptr[1]);
}

const char *raid_parz_tag(void)
{
	return raid_tag(raid_parz_ptr);
}

const char *raid_par3_tag(void)
{
	return raid_tag(raid_par_ptr[2]);
}

const char *raid_par4_tag(void)
{
	return raid_tag(raid_par_ptr[3]);
}

const char *raid_par5_tag(void)
{
	return raid_tag(raid_par_ptr[4]);
}

const char *raid_par6_tag(void)
{
	return raid_tag(raid_par_ptr[5]);
}

const char *raid_rec1_tag(void)
{
	return raid_tag(raid_rec_ptr[0]);
}

const char *raid_rec2_tag(void)
{
	return raid_tag(raid_rec_ptr[1]);
}

const char *raid_recX_tag(void)
{
	return raid_tag(raid_rec_ptr[2]);
}

