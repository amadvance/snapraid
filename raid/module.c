// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2013 Andrea Mazzoleni

#include "internal.h"
#include "memory.h"
#include "cpu.h"

/**
 * Forwarders for parity generation.
 *
 * Set by the raid_mode() call.
 *
 * Index 0 is for one parity, 1 for the two parities, and so on.
 */
static raid_gen_fn *raid_gen_ptr[RAID_PARITY_MAX];

struct raid_gen_algo {
	raid_gen_fn *gen;
	const char *tag; /**< Hardware descriptive tag. */
};

/**
 * Registered algorithms for parity generation.
 *
 * Set by the raid_gen_register() calls.
 *
 * Indexes are the RAID_ALGO_* constants
 */
static struct raid_gen_algo raid_gen_algo[RAID_ALGO_MAX];

void raid_gen(int nd, int np, size_t size, void **v)
{
	/* enforce limit on size */
	BUG_ON(size % 64 != 0);

	/* enforce limit on number of failures */
	BUG_ON(np < 1);
	BUG_ON(np > RAID_PARITY_MAX);

	raid_gen_ptr[np - 1](nd, size, v);
}

/**
 * Forwarders for data recovery.
 *
 * Set by the raid_mode() call.
 *
 * Index 0 is for one parity, 1 for the two parities, and so on.
 */
static raid_rec_fn *raid_rec_ptr[RAID_PARITY_MAX];

struct raid_rec_algo {
	raid_rec_fn *rec;
	const char *tag;  /**< Hardware descriptive tag. */
};

/**
 * Registered algorithms for data recovery.
 *
 * Set by the raid_rec_register() calls.
 *
 * Indexes are the RAID_ALGO_* constants
 */
static struct raid_rec_algo raid_rec_algo[RAID_PARITY_MAX];

void raid_rec(int nr, int *ir, int nd, int np, size_t size, void **v)
{
	int nrd; /* number of data blocks to recover */
	int nrp; /* number of parity blocks to recover */

	/* enforce limit on size */
	BUG_ON(size % 64 != 0);

	/* enforce limit on number of failures */
	BUG_ON(nr > np);
	BUG_ON(np > RAID_PARITY_MAX);

	/* enforce order in index vector */
	BUG_ON(nr >= 2 && ir[0] >= ir[1]);
	BUG_ON(nr >= 3 && ir[1] >= ir[2]);
	BUG_ON(nr >= 4 && ir[2] >= ir[3]);
	BUG_ON(nr >= 5 && ir[3] >= ir[4]);
	BUG_ON(nr >= 6 && ir[4] >= ir[5]);

	/* enforce limit on index vector */
	BUG_ON(nr > 0 && ir[nr - 1] >= nd + np);

	/* count the number of data blocks to recover */
	nrd = 0;
	while (nrd < nr && ir[nrd] < nd)
		++nrd;

	/* all the remaining are parity */
	nrp = nr - nrd;

	/* enforce limit on number of failures */
	BUG_ON(nrd > nd);
	BUG_ON(nrp > np);

	/* if failed data is present */
	if (nrd != 0) {
		int ip[RAID_PARITY_MAX];
		int i, j, k;

		/* setup the vector of parities to use */
		for (i = 0, j = 0, k = 0; i < np; ++i) {
			if (j < nrp && ir[nrd + j] == nd + i) {
				/* this parity has to be recovered */
				++j;
			} else {
				/* this parity is used for recovering */
				ip[k] = i;
				++k;
			}
		}

		/*
		 * Recover the nrd data blocks specified in ir[],
		 * using the first nrd parity in ip[] for recovering
		 */
		raid_rec_ptr[nrd - 1](nrd, ir, ip, nd, size, v);
	}

	/* recompute all the parities up to the last bad one */
	if (nrp != 0)
		raid_gen(nd, ir[nr - 1] - nd + 1, size, v);
}

void raid_data(int nr, int *id, int *ip, int nd, size_t size, void **v)
{
	/* enforce limit on size */
	BUG_ON(size % 64 != 0);

	/* enforce limit on number of failures */
	BUG_ON(nr > nd);
	BUG_ON(nr > RAID_PARITY_MAX);

	/* enforce order in index vector for data */
	BUG_ON(nr >= 2 && id[0] >= id[1]);
	BUG_ON(nr >= 3 && id[1] >= id[2]);
	BUG_ON(nr >= 4 && id[2] >= id[3]);
	BUG_ON(nr >= 5 && id[3] >= id[4]);
	BUG_ON(nr >= 6 && id[4] >= id[5]);

	/* enforce limit on index vector for data */
	BUG_ON(nr > 0 && id[nr - 1] >= nd);

	/* enforce order in index vector for parity */
	BUG_ON(nr >= 2 && ip[0] >= ip[1]);
	BUG_ON(nr >= 3 && ip[1] >= ip[2]);
	BUG_ON(nr >= 4 && ip[2] >= ip[3]);
	BUG_ON(nr >= 5 && ip[3] >= ip[4]);
	BUG_ON(nr >= 6 && ip[4] >= ip[5]);

	/* if failed data is present */
	if (nr != 0)
		raid_rec_algo[nr - 1].rec(nr, id, ip, nd, size, v);
}

const char *raid_gen_tag(int na)
{
	BUG_ON(na < 0 || na >= RAID_ALGO_MAX);

	return raid_gen_algo[na].tag;
}

const char *raid_rec_tag(int na)
{
	/* there is no custom recover for vandermonde */
	if (na == RAID_ALGO_VANDERMONDE_PAR3)
		na = RAID_ALGO_CAUCHY_PAR3;

	BUG_ON(na < 0 || na >= RAID_PARITY_MAX);

	return raid_rec_algo[na].tag;
}

/**
 * Generator matrix currently used.
 */
const uint8_t(*raid_gfgen)[256];

void raid_mode(int mode)
{
	BUG_ON(mode != RAID_MODE_VANDERMONDE && mode != RAID_MODE_CAUCHY);

	if (mode == RAID_MODE_VANDERMONDE) {
		raid_gen_ptr[0] = raid_gen_algo[RAID_ALGO_CAUCHY_PAR1].gen;
		raid_gen_ptr[1] = raid_gen_algo[RAID_ALGO_CAUCHY_PAR2].gen;
		raid_gen_ptr[2] = raid_gen_algo[RAID_ALGO_VANDERMONDE_PAR3].gen;
		raid_gen_ptr[3] = 0;
		raid_gen_ptr[4] = 0;
		raid_gen_ptr[5] = 0;
		raid_rec_ptr[0] = raid_rec_algo[RAID_ALGO_CAUCHY_PAR1].rec;
		raid_rec_ptr[1] = raid_rec_algo[RAID_ALGO_CAUCHY_PAR2].rec;
		raid_rec_ptr[2] = raid_rec_algo[RAID_ALGO_CAUCHY_PAR3].rec;
		raid_rec_ptr[3] = 0;
		raid_rec_ptr[4] = 0;
		raid_rec_ptr[5] = 0;
		raid_gfgen = gfvandermonde;
	} else {
		raid_gen_ptr[0] = raid_gen_algo[RAID_ALGO_CAUCHY_PAR1].gen;
		raid_gen_ptr[1] = raid_gen_algo[RAID_ALGO_CAUCHY_PAR2].gen;
		raid_gen_ptr[2] = raid_gen_algo[RAID_ALGO_CAUCHY_PAR3].gen;
		raid_gen_ptr[3] = raid_gen_algo[RAID_ALGO_CAUCHY_PAR4].gen;
		raid_gen_ptr[4] = raid_gen_algo[RAID_ALGO_CAUCHY_PAR5].gen;
		raid_gen_ptr[5] = raid_gen_algo[RAID_ALGO_CAUCHY_PAR6].gen;
		raid_rec_ptr[0] = raid_rec_algo[RAID_ALGO_CAUCHY_PAR1].rec;
		raid_rec_ptr[1] = raid_rec_algo[RAID_ALGO_CAUCHY_PAR2].rec;
		raid_rec_ptr[2] = raid_rec_algo[RAID_ALGO_CAUCHY_PAR3].rec;
		raid_rec_ptr[3] = raid_rec_algo[RAID_ALGO_CAUCHY_PAR4].rec;
		raid_rec_ptr[4] = raid_rec_algo[RAID_ALGO_CAUCHY_PAR5].rec;
		raid_rec_ptr[5] = raid_rec_algo[RAID_ALGO_CAUCHY_PAR6].rec;
		raid_gfgen = gfcauchy;
	}
}

void raid_gen_force(int np, raid_gen_fn *fn)
{
	BUG_ON(np < 1 || np > RAID_PARITY_MAX);

	raid_gen_ptr[np - 1] = fn;
}

void raid_gen_register(int na, const char *tag, raid_gen_fn *gen)
{
	BUG_ON(na < 0 || na >= RAID_ALGO_MAX);

	raid_gen_algo[na].tag = tag;
	raid_gen_algo[na].gen = gen;
}

void raid_rec_register(int na, const char *tag, raid_rec_fn *rec)
{
	BUG_ON(na < 0 || na >= RAID_PARITY_MAX);

	raid_rec_algo[na].tag = tag;
	raid_rec_algo[na].rec = rec;
}

/*
 * Initializes and selects the best algorithm.
 */
void raid_init(void)
{
	raid_register_int();
#if defined(CONFIG_X86) && defined(CONFIG_SSE2)
	raid_register_x86();
#endif
#if defined(CONFIG_X86_64) && defined(CONFIG_AVX512GFNI)
	raid_register_avx512gfni();
#endif

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
			/*
			 * Make blocks bad
			 * we cannot fill them with 0, because the original
			 * data may be already filled with 0
			 */
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

#ifdef __KERNEL__ /* to build the user mode test */
static int speedtest = 1;

static int __init raid_cauchy_init(void)
{
	int ret;

	raid_init();

	pr_info("raid: Using xor_blocks\n");
#ifdef RAID_USE_RAID6_PQ
	pr_info("raid: Using raid6\n");
#endif

	ret = raid_selftest();
	if (ret != 0)
		return ret;

	pr_info("raid: Self test passed\n");

	if (speedtest) {
		pr_info("raid: Speed test\n");
		raid_speedtest(0);
		pr_info("raid: Speed test with optimized memory layout\n");
		raid_speedtest(64); /* 64 is the typical cache line size */
	}

	return 0;
}

static void raid_cauchy_exit(void)
{
}

subsys_initcall(raid_cauchy_init);
module_exit(raid_cauchy_exit);
module_param(speedtest, int, 0);
MODULE_PARM_DESC(speedtest, "Runs a startup speed test");
MODULE_AUTHOR("Andrea Mazzoleni <amadvance@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("RAID Cauchy functions");
#endif

