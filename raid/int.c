// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2013 Andrea Mazzoleni

#include "internal.h"
#include "gf.h"

/*
 * Generate one parity block (RAID5 with XOR) using 32bit C implementation.
 */
void raid_gen1_int32(int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	int d, l;
	size_t i;

	uint32_t p0;
	uint32_t p1;

	l = nd - 1;
	p = v[nd];

	for (i = 0; i < size; i += 8) {
		p0 = v_read32(&v[l][i]);
		p1 = v_read32(&v[l][i + 4]);
		for (d = l - 1; d >= 0; --d) {
			p0 ^= v_read32(&v[d][i]);
			p1 ^= v_read32(&v[d][i + 4]);
		}
		v_write32(&p[i], p0);
		v_write32(&p[i + 4], p1);
	}
}

/*
 * Generate one parity block (RAID5 with XOR) using 64bit C implementation.
 */
void raid_gen1_int64(int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	int d, l;
	size_t i;

	uint64_t p0;
	uint64_t p1;

	l = nd - 1;
	p = v[nd];

	for (i = 0; i < size; i += 16) {
		p0 = v_read64(&v[l][i]);
		p1 = v_read64(&v[l][i + 8]);
		for (d = l - 1; d >= 0; --d) {
			p0 ^= v_read64(&v[d][i]);
			p1 ^= v_read64(&v[d][i + 8]);
		}
		v_write64(&p[i], p0);
		v_write64(&p[i + 8], p1);
	}
}

/*
 * Generate two parity blocks (RAID6 with powers of 2 or 3) using 32bit C implementation.
 */
static __always_inline void raid_gen2_int32_gen(int nd, size_t size, void **vv, int generator)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *q;
	int d, l;
	size_t i;
	const uint32_t poly_32 = raid_poly_32;

	uint32_t d0, q0, p0;
	uint32_t d1, q1, p1;
	uint32_t qold;

	l = nd - 1;
	p = v[nd];
	q = v[nd + 1];

	for (i = 0; i < size; i += 8) {
		q0 = p0 = v_read32(&v[l][i]);
		q1 = p1 = v_read32(&v[l][i + 4]);
		for (d = l - 1; d >= 0; --d) {
			d0 = v_read32(&v[d][i]);
			d1 = v_read32(&v[d][i + 4]);

			p0 ^= d0;
			p1 ^= d1;

			qold = q0;
			q0 = x2_32(q0, poly_32);
			if (generator == 3)
				q0 ^= qold;

			qold = q1;
			q1 = x2_32(q1, poly_32);
			if (generator == 3)
				q1 ^= qold;

			q0 ^= d0;
			q1 ^= d1;
		}
		v_write32(&p[i], p0);
		v_write32(&p[i + 4], p1);
		v_write32(&q[i], q0);
		v_write32(&q[i + 4], q1);
	}
}

void raid_gen2_int32_raid(int nd, size_t size, void **vv)
{
	raid_gen2_int32_gen(nd, size, vv, 2);
}

void raid_gen2_int32_aes(int nd, size_t size, void **vv)
{
	raid_gen2_int32_gen(nd, size, vv, 3);
}

/*
 * Generate two parity blocks (RAID6 with powers of 2 or 3) using 64bit C implementation.
 */
static __always_inline void raid_gen2_int64_gen(int nd, size_t size, void **vv, int generator)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *q;
	int d, l;
	size_t i;
	const uint64_t poly_64 = raid_poly_64;

	uint64_t d0, q0, p0;
	uint64_t d1, q1, p1;
	uint64_t qold;

	l = nd - 1;
	p = v[nd];
	q = v[nd + 1];

	for (i = 0; i < size; i += 16) {
		q0 = p0 = v_read64(&v[l][i]);
		q1 = p1 = v_read64(&v[l][i + 8]);
		for (d = l - 1; d >= 0; --d) {
			d0 = v_read64(&v[d][i]);
			d1 = v_read64(&v[d][i + 8]);

			p0 ^= d0;
			p1 ^= d1;

			qold = q0;
			q0 = x2_64(q0, poly_64);
			if (generator == 3)
				q0 ^= qold;

			qold = q1;
			q1 = x2_64(q1, poly_64);
			if (generator == 3)
				q1 ^= qold;

			q0 ^= d0;
			q1 ^= d1;
		}
		v_write64(&p[i], p0);
		v_write64(&p[i + 8], p1);
		v_write64(&q[i], q0);
		v_write64(&q[i + 8], q1);
	}
}

void raid_gen2_int64_raid(int nd, size_t size, void **vv)
{
	raid_gen2_int64_gen(nd, size, vv, 2);
}

void raid_gen2_int64_aes(int nd, size_t size, void **vv)
{
	raid_gen2_int64_gen(nd, size, vv, 3);
}

void raid_gen2_int8(int nd, size_t size, void **vv)
{
	raid_gen_ref(nd, 2, size, vv);
}

/*
 * Generate three parity blocks with powers of 2^-1 using 32bit C implementation.
 */
void raid_genz_int32_raid(int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *q;
	uint8_t *r;
	int d, l;
	size_t i;
	const uint32_t poly_32 = raid_poly_32;
	const uint32_t inv2_32 = raid_inv2_32;

	uint32_t d0, r0, q0, p0;
	uint32_t d1, r1, q1, p1;

	l = nd - 1;
	p = v[nd];
	q = v[nd + 1];
	r = v[nd + 2];

	for (i = 0; i < size; i += 8) {
		r0 = q0 = p0 = v_read32(&v[l][i]);
		r1 = q1 = p1 = v_read32(&v[l][i + 4]);
		for (d = l - 1; d >= 0; --d) {
			d0 = v_read32(&v[d][i]);
			d1 = v_read32(&v[d][i + 4]);

			p0 ^= d0;
			p1 ^= d1;

			q0 = x2_32(q0, poly_32);
			q1 = x2_32(q1, poly_32);

			q0 ^= d0;
			q1 ^= d1;

			r0 = d2_32(r0, inv2_32);
			r1 = d2_32(r1, inv2_32);

			r0 ^= d0;
			r1 ^= d1;
		}
		v_write32(&p[i], p0);
		v_write32(&p[i + 4], p1);
		v_write32(&q[i], q0);
		v_write32(&q[i + 4], q1);
		v_write32(&r[i], r0);
		v_write32(&r[i + 4], r1);
	}
}

/*
 * Generate three parity blocks with powers of 2^-1 using 64bit C implementation.
 */
void raid_genz_int64_raid(int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *q;
	uint8_t *r;
	int d, l;
	size_t i;
	const uint64_t poly_64 = raid_poly_64;
	const uint64_t inv2_64 = raid_inv2_64;

	uint64_t d0, r0, q0, p0;
	uint64_t d1, r1, q1, p1;

	l = nd - 1;
	p = v[nd];
	q = v[nd + 1];
	r = v[nd + 2];

	for (i = 0; i < size; i += 16) {
		r0 = q0 = p0 = v_read64(&v[l][i]);
		r1 = q1 = p1 = v_read64(&v[l][i + 8]);
		for (d = l - 1; d >= 0; --d) {
			d0 = v_read64(&v[d][i]);
			d1 = v_read64(&v[d][i + 8]);

			p0 ^= d0;
			p1 ^= d1;

			q0 = x2_64(q0, poly_64);
			q1 = x2_64(q1, poly_64);

			q0 ^= d0;
			q1 ^= d1;

			r0 = d2_64(r0, inv2_64);
			r1 = d2_64(r1, inv2_64);

			r0 ^= d0;
			r1 ^= d1;
		}
		v_write64(&p[i], p0);
		v_write64(&p[i + 8], p1);
		v_write64(&q[i], q0);
		v_write64(&q[i + 8], q1);
		v_write64(&r[i], r0);
		v_write64(&r[i + 8], r1);
	}
}

/*
 * Generate three parity blocks with Cauchy matrix using 8bit C implementation.
 *
 * Note that instead of a generic multiplication table, likely resulting
 * in multiple cache misses, a precomputed table could be used.
 * But this is only a kind of reference function, and we are not really
 * interested in speed.
 */
void raid_gen3_int8(int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *q;
	uint8_t *r;
	int d, l;
	size_t i;

	uint8_t d0, r0, q0, p0;

	l = nd - 1;
	p = v[nd];
	q = v[nd + 1];
	r = v[nd + 2];

	for (i = 0; i < size; i += 1) {
		p0 = q0 = r0 = 0;
		for (d = l; d > 0; --d) {
			d0 = v[d][i];

			p0 ^= d0;
			q0 ^= raid_gfmul[d0][raid_gfgen[1][d]];
			r0 ^= raid_gfmul[d0][raid_gfgen[2][d]];
		}

		/* first disk with all coefficients at 1 */
		d0 = v[0][i];

		p0 ^= d0;
		q0 ^= d0;
		r0 ^= d0;

		p[i] = p0;
		q[i] = q0;
		r[i] = r0;
	}
}

/*
 * Generate four parity blocks with Cauchy matrix using 8bit C implementation.
 *
 * Note that instead of a generic multiplication table, likely resulting
 * in multiple cache misses, a precomputed table could be used.
 * But this is only a kind of reference function, and we are not really
 * interested in speed.
 */
void raid_gen4_int8(int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *q;
	uint8_t *r;
	uint8_t *s;
	int d, l;
	size_t i;

	uint8_t d0, s0, r0, q0, p0;

	l = nd - 1;
	p = v[nd];
	q = v[nd + 1];
	r = v[nd + 2];
	s = v[nd + 3];

	for (i = 0; i < size; i += 1) {
		p0 = q0 = r0 = s0 = 0;
		for (d = l; d > 0; --d) {
			d0 = v[d][i];

			p0 ^= d0;
			q0 ^= raid_gfmul[d0][raid_gfgen[1][d]];
			r0 ^= raid_gfmul[d0][raid_gfgen[2][d]];
			s0 ^= raid_gfmul[d0][raid_gfgen[3][d]];
		}

		/* first disk with all coefficients at 1 */
		d0 = v[0][i];

		p0 ^= d0;
		q0 ^= d0;
		r0 ^= d0;
		s0 ^= d0;

		p[i] = p0;
		q[i] = q0;
		r[i] = r0;
		s[i] = s0;
	}
}

/*
 * Generate five parity blocks with Cauchy matrix using 8bit C implementation.
 *
 * Note that instead of a generic multiplication table, likely resulting
 * in multiple cache misses, a precomputed table could be used.
 * But this is only a kind of reference function, and we are not really
 * interested in speed.
 */
void raid_gen5_int8(int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *q;
	uint8_t *r;
	uint8_t *s;
	uint8_t *t;
	int d, l;
	size_t i;

	uint8_t d0, t0, s0, r0, q0, p0;

	l = nd - 1;
	p = v[nd];
	q = v[nd + 1];
	r = v[nd + 2];
	s = v[nd + 3];
	t = v[nd + 4];

	for (i = 0; i < size; i += 1) {
		p0 = q0 = r0 = s0 = t0 = 0;
		for (d = l; d > 0; --d) {
			d0 = v[d][i];

			p0 ^= d0;
			q0 ^= raid_gfmul[d0][raid_gfgen[1][d]];
			r0 ^= raid_gfmul[d0][raid_gfgen[2][d]];
			s0 ^= raid_gfmul[d0][raid_gfgen[3][d]];
			t0 ^= raid_gfmul[d0][raid_gfgen[4][d]];
		}

		/* first disk with all coefficients at 1 */
		d0 = v[0][i];

		p0 ^= d0;
		q0 ^= d0;
		r0 ^= d0;
		s0 ^= d0;
		t0 ^= d0;

		p[i] = p0;
		q[i] = q0;
		r[i] = r0;
		s[i] = s0;
		t[i] = t0;
	}
}

/*
 * Generate six parity blocks with Cauchy matrix using 8bit C implementation.
 *
 * Note that instead of a generic multiplication table, likely resulting
 * in multiple cache misses, a precomputed table could be used.
 * But this is only a kind of reference function, and we are not really
 * interested in speed.
 */
void raid_gen6_int8(int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *q;
	uint8_t *r;
	uint8_t *s;
	uint8_t *t;
	uint8_t *u;
	int d, l;
	size_t i;

	uint8_t d0, u0, t0, s0, r0, q0, p0;

	l = nd - 1;
	p = v[nd];
	q = v[nd + 1];
	r = v[nd + 2];
	s = v[nd + 3];
	t = v[nd + 4];
	u = v[nd + 5];

	for (i = 0; i < size; i += 1) {
		p0 = q0 = r0 = s0 = t0 = u0 = 0;
		for (d = l; d > 0; --d) {
			d0 = v[d][i];

			p0 ^= d0;
			q0 ^= raid_gfmul[d0][raid_gfgen[1][d]];
			r0 ^= raid_gfmul[d0][raid_gfgen[2][d]];
			s0 ^= raid_gfmul[d0][raid_gfgen[3][d]];
			t0 ^= raid_gfmul[d0][raid_gfgen[4][d]];
			u0 ^= raid_gfmul[d0][raid_gfgen[5][d]];
		}

		/* first disk with all coefficients at 1 */
		d0 = v[0][i];

		p0 ^= d0;
		q0 ^= d0;
		r0 ^= d0;
		s0 ^= d0;
		t0 ^= d0;
		u0 ^= d0;

		p[i] = p0;
		q[i] = q0;
		r[i] = r0;
		s[i] = s0;
		t[i] = t0;
		u[i] = u0;
	}
}

/*
 * Recover failure of one data block at index id[0] using parity at index
 * ip[0] for any RAID level.
 *
 * Starting from the equation:
 *
 * Pd = A[ip[0],id[0]] * Dx
 *
 * and solving we get:
 *
 * Dx = A[ip[0],id[0]]^-1 * Pd
 */
void raid_rec1_int8(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *pa;
	const uint8_t *T;
	uint8_t G;
	uint8_t V;
	size_t i;

	(void)nr; /* unused, it's always 1 */

	/* if recovering with P uses the delta function */
	if (ip[0] == 0) {
		raid_rec1of1(id, nd, size, vv);
		return;
	}

	/* setup the coefficients matrix */
	G = A(ip[0], id[0]);

	/* invert it to solve the system of linear equations */
	V = inv(G);

	/* get multiplication tables */
	T = table(V);

	/* compute delta parity */
	raid_delta_gen(1, id, ip, nd, size, vv);

	p = v[nd + ip[0]];
	pa = v[id[0]];

	for (i = 0; i < size; ++i) {
		/* delta */
		uint8_t Pd = p[i] ^ pa[i];

		/* reconstruct */
		pa[i] = T[Pd];
	}
}

/*
 * Recover failure of two data blocks at indexes id[0],id[1] using parity at
 * indexes ip[0],ip[1] for any RAID level.
 *
 * Starting from the equations:
 *
 * Pd = A[ip[0],id[0]] * Dx + A[ip[0],id[1]] * Dy
 * Qd = A[ip[1],id[0]] * Dx + A[ip[1],id[1]] * Dy
 *
 * we solve inverting the coefficients matrix.
 */
void raid_rec2_int8(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *pa;
	uint8_t *q;
	uint8_t *qa;
	const uint8_t *T[2][2];
	uint8_t G[2 * 2];
	uint8_t V[2 * 2];
	size_t i;
	int j, k;

	(void)nr; /* unused, it's always 2 */

	/* if it's RAID6, recovering with P and Q uses the faster function */
	if (ip[0] == 0 && ip[1] == 1) {
		raid_rec2of2_int8(id, ip, nd, size, vv);
		return;
	}

	/* setup the coefficients matrix */
	for (j = 0; j < 2; ++j)
		for (k = 0; k < 2; ++k)
			G[j * 2 + k] = A(ip[j], id[k]);

	/* invert it to solve the system of linear equations */
	raid_invert(G, V, 2);

	/* get multiplication tables */
	for (j = 0; j < 2; ++j)
		for (k = 0; k < 2; ++k)
			T[j][k] = table(V[j * 2 + k]);

	/* compute delta parity */
	raid_delta_gen(2, id, ip, nd, size, vv);

	p = v[nd + ip[0]];
	q = v[nd + ip[1]];
	pa = v[id[0]];
	qa = v[id[1]];

	for (i = 0; i < size; ++i) {
		/* delta */
		uint8_t Pd = p[i] ^ pa[i];
		uint8_t Qd = q[i] ^ qa[i];

		/* reconstruct */
		pa[i] = T[0][0][Pd] ^ T[0][1][Qd];
		qa[i] = T[1][0][Pd] ^ T[1][1][Qd];
	}
}

/*
 * Recover failure of N data blocks at indexes id[N] using parity at indexes
 * ip[N] for any RAID level.
 *
 * Starting from the N equations, with 0<=i<N :
 *
 * PD[i] = sum(A[ip[i],id[j]] * D[i]) 0<=j<N
 *
 * we solve inverting the coefficients matrix.
 *
 * Note that referring to previous equations you have:
 * PD[0] = Pd, PD[1] = Qd, PD[2] = Rd, ...
 * D[0] = Dx, D[1] = Dy, D[2] = Dz, ...
 */
void raid_recX_int8(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p[RAID_PARITY_MAX];
	uint8_t *pa[RAID_PARITY_MAX];
	const uint8_t *T[RAID_PARITY_MAX][RAID_PARITY_MAX];
	uint8_t G[RAID_PARITY_MAX * RAID_PARITY_MAX];
	uint8_t V[RAID_PARITY_MAX * RAID_PARITY_MAX];
	size_t i;
	int j, k;

	/* setup the coefficients matrix */
	for (j = 0; j < nr; ++j)
		for (k = 0; k < nr; ++k)
			G[j * nr + k] = A(ip[j], id[k]);

	/* invert it to solve the system of linear equations */
	raid_invert(G, V, nr);

	/* get multiplication tables */
	for (j = 0; j < nr; ++j)
		for (k = 0; k < nr; ++k)
			T[j][k] = table(V[j * nr + k]);

	/* compute delta parity */
	raid_delta_gen(nr, id, ip, nd, size, vv);

	for (j = 0; j < nr; ++j) {
		p[j] = v[nd + ip[j]];
		pa[j] = v[id[j]];
	}

	for (i = 0; i < size; ++i) {
		uint8_t PD[RAID_PARITY_MAX];

		/* delta */
		for (j = 0; j < nr; ++j)
			PD[j] = p[j][i] ^ pa[j][i];

		/* reconstruct */
		for (j = 0; j < nr; ++j) {
			uint8_t b = 0;

			for (k = 0; k < nr; ++k)
				b ^= T[j][k][PD[k]];
			pa[j][i] = b;
		}
	}
}

void raid_register_int(void)
{
	raid_gen_register(RAID_ALGO_CAUCHY_PAR2, "int8", raid_gen2_int8, RAID_POLY_ANY);
	raid_gen_register(RAID_ALGO_CAUCHY_PAR3, "int8", raid_gen3_int8, RAID_POLY_ANY);
	raid_gen_register(RAID_ALGO_CAUCHY_PAR4, "int8", raid_gen4_int8, RAID_POLY_ANY);
	raid_gen_register(RAID_ALGO_CAUCHY_PAR5, "int8", raid_gen5_int8, RAID_POLY_ANY);
	raid_gen_register(RAID_ALGO_CAUCHY_PAR6, "int8", raid_gen6_int8, RAID_POLY_ANY);

	if (sizeof(void *) == 4) {
		raid_gen_register(RAID_ALGO_CAUCHY_PAR1, "int32", raid_gen1_int32, RAID_POLY_ANY);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR2, "int32", raid_gen2_int32_raid, RAID_POLY_RAID);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR2, "int32", raid_gen2_int32_aes, RAID_POLY_AES);
		raid_gen_register(RAID_ALGO_VANDERMONDE_PAR3, "int32", raid_genz_int32_raid, RAID_POLY_RAID);
	} else {
		raid_gen_register(RAID_ALGO_CAUCHY_PAR1, "int64", raid_gen1_int64, RAID_POLY_ANY);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR2, "int64", raid_gen2_int64_raid, RAID_POLY_RAID);
		raid_gen_register(RAID_ALGO_CAUCHY_PAR2, "int64", raid_gen2_int64_aes, RAID_POLY_AES);
		raid_gen_register(RAID_ALGO_VANDERMONDE_PAR3, "int64", raid_genz_int64_raid, RAID_POLY_RAID);
	}

	raid_rec_register(RAID_ALGO_CAUCHY_PAR1, "int8", raid_rec1_int8, RAID_POLY_ANY);
	raid_rec_register(RAID_ALGO_CAUCHY_PAR2, "int8", raid_rec2_int8, RAID_POLY_ANY);
	raid_rec_register(RAID_ALGO_CAUCHY_PAR3, "int8", raid_recX_int8, RAID_POLY_ANY);
	raid_rec_register(RAID_ALGO_CAUCHY_PAR4, "int8", raid_recX_int8, RAID_POLY_ANY);
	raid_rec_register(RAID_ALGO_CAUCHY_PAR5, "int8", raid_recX_int8, RAID_POLY_ANY);
	raid_rec_register(RAID_ALGO_CAUCHY_PAR6, "int8", raid_recX_int8, RAID_POLY_ANY);
}
