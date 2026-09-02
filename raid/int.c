// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2013 Andrea Mazzoleni

#include "internal.h"
#include "gf.h"

/*
 * Generate one parity block (RAID5 with XOR) using 32bit C implementation.
 *
 * Uses 16-byte chunks across four 32-bit words.
 */
void raid_gen1_int32(int nd, size_t size, void **vv, int streaming)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	int d, l;
	size_t i;

	uint32_t p0;
	uint32_t p1;
	uint32_t p2;
	uint32_t p3;

	(void)streaming;

	l = nd - 1;
	p = v[nd];

	for (i = 0; i < size; i += 16) {
		p0 = v_read32(&v[l][i]);
		p1 = v_read32(&v[l][i + 4]);
		p2 = v_read32(&v[l][i + 8]);
		p3 = v_read32(&v[l][i + 12]);
		for (d = l - 1; d >= 0; --d) {
			p0 ^= v_read32(&v[d][i]);
			p1 ^= v_read32(&v[d][i + 4]);
			p2 ^= v_read32(&v[d][i + 8]);
			p3 ^= v_read32(&v[d][i + 12]);
		}
		v_write32(&p[i], p0);
		v_write32(&p[i + 4], p1);
		v_write32(&p[i + 8], p2);
		v_write32(&p[i + 12], p3);
	}
}

/*
 * Generate one parity block (RAID5 with XOR) using 64bit C implementation.
 *
 * Uses 32-byte chunks across four 64-bit words.
 */
void raid_gen1_int64(int nd, size_t size, void **vv, int streaming)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	int d, l;
	size_t i;

	uint64_t p0;
	uint64_t p1;
	uint64_t p2;
	uint64_t p3;

	(void)streaming;

	l = nd - 1;
	p = v[nd];

	for (i = 0; i < size; i += 32) {
		p0 = v_read64(&v[l][i]);
		p1 = v_read64(&v[l][i + 8]);
		p2 = v_read64(&v[l][i + 16]);
		p3 = v_read64(&v[l][i + 24]);
		for (d = l - 1; d >= 0; --d) {
			p0 ^= v_read64(&v[d][i]);
			p1 ^= v_read64(&v[d][i + 8]);
			p2 ^= v_read64(&v[d][i + 16]);
			p3 ^= v_read64(&v[d][i + 24]);
		}
		v_write64(&p[i], p0);
		v_write64(&p[i + 8], p1);
		v_write64(&p[i + 16], p2);
		v_write64(&p[i + 24], p3);
	}
}

/*
 * Generate two parity blocks (RAID6 with powers of 2 or 3) using 32bit C implementation.
 *
 * Uses Horner's method with 8-byte chunks across two 32-bit words.
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

/*
 * Generate two parity blocks (RAID6 with powers of 2 or 3) using 64bit C implementation.
 *
 * Uses Horner's method with 16-byte chunks across two 64-bit words.
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

/*
 * Generate three parity blocks (with powers of 2^-1) using 32bit C implementation.
 *
 * Uses Horner's method with 8-byte chunks across two 32-bit words.
 */
void raid_genz_int32_raid(int nd, size_t size, void **vv, int streaming)
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

	(void)streaming;

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
 * Generate three parity blocks (with powers of 2^-1) using 64bit C implementation.
 *
 * Uses Horner's method with 16-byte chunks across two 64-bit words.
 */
void raid_genz_int64_raid(int nd, size_t size, void **vv, int streaming)
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

	(void)streaming;

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
 * Generate two parity blocks (RAID6 with Cauchy matrix) using 8bit C implementation.
 *
 * Precomputes per-disk multiplication tables and processes 2-byte chunks.
 */
void raid_gen2_int8(int nd, size_t size, void **vv, int streaming)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *q;
	const uint8_t *T[RAID_DATA_MAX];
	int d, l;
	size_t i;

	uint8_t d0, d1, q0, q1, p0, p1;

	(void)streaming;

	l = nd - 1;
	p = v[nd];
	q = v[nd + 1];

	for (d = 1; d <= l; ++d)
		T[d] = table(A(1, d));

	for (i = 0; i < size; i += 2) {
		p0 = q0 = 0;
		p1 = q1 = 0;
		for (d = l; d > 0; --d) {
			d0 = v[d][i];
			d1 = v[d][i + 1];

			p0 ^= d0;
			p1 ^= d1;
			q0 ^= T[d][d0];
			q1 ^= T[d][d1];
		}

		/* first disk with all coefficients at 1 */
		d0 = v[0][i];
		d1 = v[0][i + 1];

		p0 ^= d0;
		p1 ^= d1;
		q0 ^= d0;
		q1 ^= d1;

		p[i] = p0;
		p[i + 1] = p1;
		q[i] = q0;
		q[i + 1] = q1;
	}
}

/*
 * Generate three parity blocks (with Cauchy matrix) using 8bit C implementation.
 *
 * Precomputes per-disk multiplication tables and processes 2-byte chunks.
 */
void raid_gen3_int8(int nd, size_t size, void **vv, int streaming)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *q;
	uint8_t *r;
	const uint8_t *T[RAID_DATA_MAX][2];
	int d, l;
	size_t i;

	uint8_t d0, d1, r0, r1, q0, q1, p0, p1;

	(void)streaming;

	l = nd - 1;
	p = v[nd];
	q = v[nd + 1];
	r = v[nd + 2];

	for (d = 1; d <= l; ++d) {
		T[d][0] = table(A(1, d));
		T[d][1] = table(A(2, d));
	}

	for (i = 0; i < size; i += 2) {
		p0 = q0 = r0 = 0;
		p1 = q1 = r1 = 0;
		for (d = l; d > 0; --d) {
			d0 = v[d][i];
			d1 = v[d][i + 1];

			p0 ^= d0;
			p1 ^= d1;
			q0 ^= T[d][0][d0];
			q1 ^= T[d][0][d1];
			r0 ^= T[d][1][d0];
			r1 ^= T[d][1][d1];
		}

		/* first disk with all coefficients at 1 */
		d0 = v[0][i];
		d1 = v[0][i + 1];

		p0 ^= d0;
		p1 ^= d1;
		q0 ^= d0;
		q1 ^= d1;
		r0 ^= d0;
		r1 ^= d1;

		p[i] = p0;
		p[i + 1] = p1;
		q[i] = q0;
		q[i + 1] = q1;
		r[i] = r0;
		r[i + 1] = r1;
	}
}

/*
 * Generate four parity blocks (with Cauchy matrix) using 8bit C implementation.
 *
 * Precomputes per-disk multiplication tables and processes 2-byte chunks.
 */
void raid_gen4_int8(int nd, size_t size, void **vv, int streaming)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *q;
	uint8_t *r;
	uint8_t *s;
	const uint8_t *T[RAID_DATA_MAX][3];
	int d, l;
	size_t i;

	uint8_t d0, d1, s0, s1, r0, r1, q0, q1, p0, p1;

	(void)streaming;

	l = nd - 1;
	p = v[nd];
	q = v[nd + 1];
	r = v[nd + 2];
	s = v[nd + 3];

	for (d = 1; d <= l; ++d) {
		T[d][0] = table(A(1, d));
		T[d][1] = table(A(2, d));
		T[d][2] = table(A(3, d));
	}

	for (i = 0; i < size; i += 2) {
		p0 = q0 = r0 = s0 = 0;
		p1 = q1 = r1 = s1 = 0;
		for (d = l; d > 0; --d) {
			d0 = v[d][i];
			d1 = v[d][i + 1];

			p0 ^= d0;
			p1 ^= d1;
			q0 ^= T[d][0][d0];
			q1 ^= T[d][0][d1];
			r0 ^= T[d][1][d0];
			r1 ^= T[d][1][d1];
			s0 ^= T[d][2][d0];
			s1 ^= T[d][2][d1];
		}

		/* first disk with all coefficients at 1 */
		d0 = v[0][i];
		d1 = v[0][i + 1];

		p0 ^= d0;
		p1 ^= d1;
		q0 ^= d0;
		q1 ^= d1;
		r0 ^= d0;
		r1 ^= d1;
		s0 ^= d0;
		s1 ^= d1;

		p[i] = p0;
		p[i + 1] = p1;
		q[i] = q0;
		q[i + 1] = q1;
		r[i] = r0;
		r[i + 1] = r1;
		s[i] = s0;
		s[i + 1] = s1;
	}
}

/*
 * Generate five parity blocks (with Cauchy matrix) using 8bit C implementation.
 *
 * Precomputes per-disk multiplication tables and processes 2-byte chunks.
 */
void raid_gen5_int8(int nd, size_t size, void **vv, int streaming)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *q;
	uint8_t *r;
	uint8_t *s;
	uint8_t *t;
	const uint8_t *T[RAID_DATA_MAX][4];
	int d, l;
	size_t i;

	uint8_t d0, d1, t0, t1, s0, s1, r0, r1, q0, q1, p0, p1;

	(void)streaming;

	l = nd - 1;
	p = v[nd];
	q = v[nd + 1];
	r = v[nd + 2];
	s = v[nd + 3];
	t = v[nd + 4];

	for (d = 1; d <= l; ++d) {
		T[d][0] = table(A(1, d));
		T[d][1] = table(A(2, d));
		T[d][2] = table(A(3, d));
		T[d][3] = table(A(4, d));
	}

	for (i = 0; i < size; i += 2) {
		p0 = q0 = r0 = s0 = t0 = 0;
		p1 = q1 = r1 = s1 = t1 = 0;
		for (d = l; d > 0; --d) {
			d0 = v[d][i];
			d1 = v[d][i + 1];

			p0 ^= d0;
			p1 ^= d1;
			q0 ^= T[d][0][d0];
			q1 ^= T[d][0][d1];
			r0 ^= T[d][1][d0];
			r1 ^= T[d][1][d1];
			s0 ^= T[d][2][d0];
			s1 ^= T[d][2][d1];
			t0 ^= T[d][3][d0];
			t1 ^= T[d][3][d1];
		}

		/* first disk with all coefficients at 1 */
		d0 = v[0][i];
		d1 = v[0][i + 1];

		p0 ^= d0;
		p1 ^= d1;
		q0 ^= d0;
		q1 ^= d1;
		r0 ^= d0;
		r1 ^= d1;
		s0 ^= d0;
		s1 ^= d1;
		t0 ^= d0;
		t1 ^= d1;

		p[i] = p0;
		p[i + 1] = p1;
		q[i] = q0;
		q[i + 1] = q1;
		r[i] = r0;
		r[i + 1] = r1;
		s[i] = s0;
		s[i + 1] = s1;
		t[i] = t0;
		t[i + 1] = t1;
	}
}

/*
 * Generate six parity blocks (with Cauchy matrix) using 8bit C implementation.
 *
 * Precomputes per-disk multiplication tables and processes 2-byte chunks.
 */
void raid_gen6_int8(int nd, size_t size, void **vv, int streaming)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *q;
	uint8_t *r;
	uint8_t *s;
	uint8_t *t;
	uint8_t *u;
	const uint8_t *T[RAID_DATA_MAX][5];
	int d, l;
	size_t i;

	uint8_t d0, d1, u0, u1, t0, t1, s0, s1, r0, r1, q0, q1, p0, p1;

	(void)streaming;

	l = nd - 1;
	p = v[nd];
	q = v[nd + 1];
	r = v[nd + 2];
	s = v[nd + 3];
	t = v[nd + 4];
	u = v[nd + 5];

	for (d = 1; d <= l; ++d) {
		T[d][0] = table(A(1, d));
		T[d][1] = table(A(2, d));
		T[d][2] = table(A(3, d));
		T[d][3] = table(A(4, d));
		T[d][4] = table(A(5, d));
	}

	for (i = 0; i < size; i += 2) {
		p0 = q0 = r0 = s0 = t0 = u0 = 0;
		p1 = q1 = r1 = s1 = t1 = u1 = 0;
		for (d = l; d > 0; --d) {
			d0 = v[d][i];
			d1 = v[d][i + 1];

			p0 ^= d0;
			p1 ^= d1;
			q0 ^= T[d][0][d0];
			q1 ^= T[d][0][d1];
			r0 ^= T[d][1][d0];
			r1 ^= T[d][1][d1];
			s0 ^= T[d][2][d0];
			s1 ^= T[d][2][d1];
			t0 ^= T[d][3][d0];
			t1 ^= T[d][3][d1];
			u0 ^= T[d][4][d0];
			u1 ^= T[d][4][d1];
		}

		/* first disk with all coefficients at 1 */
		d0 = v[0][i];
		d1 = v[0][i + 1];

		p0 ^= d0;
		p1 ^= d1;
		q0 ^= d0;
		q1 ^= d1;
		r0 ^= d0;
		r1 ^= d1;
		s0 ^= d0;
		s1 ^= d1;
		t0 ^= d0;
		t1 ^= d1;
		u0 ^= d0;
		u1 ^= d1;

		p[i] = p0;
		p[i + 1] = p1;
		q[i] = q0;
		q[i + 1] = q1;
		r[i] = r0;
		r[i + 1] = r1;
		s[i] = s0;
		s[i + 1] = s1;
		t[i] = t0;
		t[i + 1] = t1;
		u[i] = u0;
		u[i + 1] = u1;
	}
}

/*
 * Recover failure of one data block at index id[0] using parity at index
 * ip[0] for any RAID level using 8bit C implementation.
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
 * indexes ip[0],ip[1] for any RAID level using 8bit C implementation.
 *
 * If P is available, reconstructs only the first missing block through the
 * inverse matrix and derives the second missing block from the P delta by XOR.
 */
static __always_inline void raid_rec2_int8_gen(int nr, int has_p, int *id, int *ip, int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *q;
	uint8_t *pa;
	uint8_t *qa;
	const uint8_t *T[2][2];
	uint8_t G[2 * 2];
	uint8_t V[2 * 2];
	size_t i;
	int j, k;

	(void)nr; /* unused, it's always 2 */

	/* setup the coefficients matrix */
	for (j = 0; j < 2; ++j)
		for (k = 0; k < 2; ++k)
			G[j * 2 + k] = A(ip[j], id[k]);

	/* invert it to solve the system of linear equations */
	raid_invert(G, V, 2);

	/* get multiplication tables */
	for (j = 0; j < 2 - has_p; ++j)
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
		if (has_p) {
			uint8_t Dx = T[0][0][Pd] ^ T[0][1][Qd];

			pa[i] = Dx;
			qa[i] = Pd ^ Dx;
		} else {
			pa[i] = T[0][0][Pd] ^ T[0][1][Qd];
			qa[i] = T[1][0][Pd] ^ T[1][1][Qd];
		}
	}
}

/*
 * Recover failure of N data blocks at indexes id[N] using parity at indexes
 * ip[N] for any RAID level using 8bit C implementation.
 *
 * Precomputes surviving syndrome and inverse-matrix multiplication table
 * pointers in C, then accumulates syndromes and reconstructs missing blocks
 * in a single fused pass over 2-byte chunks.
 *
 * Uses explicit if (nr >= N) branches for compile-time loop unrolling.
 *
 * If P is available, reconstructs only nr - 1 missing blocks through the
 * inverse matrix and derives the last missing block from the P delta by XOR.
 */
static __always_inline void raid_recX_int8(int nr, int has_p, int *id, int *ip, int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p[RAID_PARITY_MAX];
	uint8_t *pa[RAID_PARITY_MAX];
	uint8_t *src[RAID_DATA_MAX];
	uint8_t G[RAID_PARITY_MAX * RAID_PARITY_MAX];
	uint8_t V[RAID_PARITY_MAX * RAID_PARITY_MAX];
	const uint8_t *S[RAID_DATA_MAX][RAID_PARITY_MAX];
	const uint8_t *R[RAID_PARITY_MAX][RAID_PARITY_MAX];
	size_t i;
	int d, j, k, s;
	int ns;

	BUG_ON(nr < 1 || nr > RAID_PARITY_MAX);

	/* setup the coefficients matrix */
	for (j = 0; j < nr; ++j)
		for (k = 0; k < nr; ++k)
			G[j * nr + k] = A(ip[j], id[k]);

	/* invert it to solve the system of linear equations */
	raid_invert(G, V, nr);

	/* setup selected parity and destination pointers */
	p[0] = v[nd + ip[0]];
	pa[0] = v[id[0]];
	if (nr >= 2) {
		p[1] = v[nd + ip[1]];
		pa[1] = v[id[1]];
	}
	if (nr >= 3) {
		p[2] = v[nd + ip[2]];
		pa[2] = v[id[2]];
	}
	if (nr >= 4) {
		p[3] = v[nd + ip[3]];
		pa[3] = v[id[3]];
	}
	if (nr >= 5) {
		p[4] = v[nd + ip[4]];
		pa[4] = v[id[4]];
	}
	if (nr >= 6) {
		p[5] = v[nd + ip[5]];
		pa[5] = v[id[5]];
	}

	/* build the compact surviving-data list and its syndrome tables */
	ns = 0;
	k = 0;
	for (d = 0; d < nd; ++d) {
		if (k < nr && d == id[k]) {
			++k;
			continue;
		}

		src[ns] = v[d];

		S[ns][0] = table(A(ip[0], d));
		if (nr >= 2)
			S[ns][1] = table(A(ip[1], d));
		if (nr >= 3)
			S[ns][2] = table(A(ip[2], d));
		if (nr >= 4)
			S[ns][3] = table(A(ip[3], d));
		if (nr >= 5)
			S[ns][4] = table(A(ip[4], d));
		if (nr >= 6)
			S[ns][5] = table(A(ip[5], d));

		++ns;
	}

	BUG_ON(k != nr);
	BUG_ON(ns != nd - nr);

	/* precompute inverse-matrix multiplication table pointers */
	if (!has_p) {
		R[0][0] = table(V[0 * nr + 0]);
		if (nr >= 2)
			R[0][1] = table(V[0 * nr + 1]);
		if (nr >= 3)
			R[0][2] = table(V[0 * nr + 2]);
		if (nr >= 4)
			R[0][3] = table(V[0 * nr + 3]);
		if (nr >= 5)
			R[0][4] = table(V[0 * nr + 4]);
		if (nr >= 6)
			R[0][5] = table(V[0 * nr + 5]);
	}
	if (nr >= 2) {
		int rj = 1 - has_p;

		R[rj][0] = table(V[rj * nr + 0]);
		if (nr >= 2)
			R[rj][1] = table(V[rj * nr + 1]);
		if (nr >= 3)
			R[rj][2] = table(V[rj * nr + 2]);
		if (nr >= 4)
			R[rj][3] = table(V[rj * nr + 3]);
		if (nr >= 5)
			R[rj][4] = table(V[rj * nr + 4]);
		if (nr >= 6)
			R[rj][5] = table(V[rj * nr + 5]);
	}
	if (nr >= 3) {
		int rj = 2 - has_p;

		R[rj][0] = table(V[rj * nr + 0]);
		if (nr >= 2)
			R[rj][1] = table(V[rj * nr + 1]);
		if (nr >= 3)
			R[rj][2] = table(V[rj * nr + 2]);
		if (nr >= 4)
			R[rj][3] = table(V[rj * nr + 3]);
		if (nr >= 5)
			R[rj][4] = table(V[rj * nr + 4]);
		if (nr >= 6)
			R[rj][5] = table(V[rj * nr + 5]);
	}
	if (nr >= 4) {
		int rj = 3 - has_p;

		R[rj][0] = table(V[rj * nr + 0]);
		if (nr >= 2)
			R[rj][1] = table(V[rj * nr + 1]);
		if (nr >= 3)
			R[rj][2] = table(V[rj * nr + 2]);
		if (nr >= 4)
			R[rj][3] = table(V[rj * nr + 3]);
		if (nr >= 5)
			R[rj][4] = table(V[rj * nr + 4]);
		if (nr >= 6)
			R[rj][5] = table(V[rj * nr + 5]);
	}
	if (nr >= 5) {
		int rj = 4 - has_p;

		R[rj][0] = table(V[rj * nr + 0]);
		if (nr >= 2)
			R[rj][1] = table(V[rj * nr + 1]);
		if (nr >= 3)
			R[rj][2] = table(V[rj * nr + 2]);
		if (nr >= 4)
			R[rj][3] = table(V[rj * nr + 3]);
		if (nr >= 5)
			R[rj][4] = table(V[rj * nr + 4]);
		if (nr >= 6)
			R[rj][5] = table(V[rj * nr + 5]);
	}
	if (nr >= 6) {
		int rj = 5 - has_p;

		R[rj][0] = table(V[rj * nr + 0]);
		if (nr >= 2)
			R[rj][1] = table(V[rj * nr + 1]);
		if (nr >= 3)
			R[rj][2] = table(V[rj * nr + 2]);
		if (nr >= 4)
			R[rj][3] = table(V[rj * nr + 3]);
		if (nr >= 5)
			R[rj][4] = table(V[rj * nr + 4]);
		if (nr >= 6)
			R[rj][5] = table(V[rj * nr + 5]);
	}

	for (i = 0; i < size; i += 2) {
		uint8_t syn0a, syn1a, syn2a, syn3a, syn4a, syn5a;
		uint8_t syn0b, syn1b, syn2b, syn3b, syn4b, syn5b;
		uint8_t p_deltaa, p_deltab;

		/* load syndromes from parity */
		syn0a = p[0][i];
		syn0b = p[0][i + 1];
		if (nr >= 2) {
			syn1a = p[1][i];
			syn1b = p[1][i + 1];
		}
		if (nr >= 3) {
			syn2a = p[2][i];
			syn2b = p[2][i + 1];
		}
		if (nr >= 4) {
			syn3a = p[3][i];
			syn3b = p[3][i + 1];
		}
		if (nr >= 5) {
			syn4a = p[4][i];
			syn4b = p[4][i + 1];
		}
		if (nr >= 6) {
			syn5a = p[5][i];
			syn5b = p[5][i + 1];
		}

		/* accumulate syndromes over surviving data */
		for (s = 0; s < ns; ++s) {
			uint8_t d0 = src[s][i];
			uint8_t d1 = src[s][i + 1];

			if (has_p) {
				syn0a ^= d0;
				syn0b ^= d1;
			} else {
				syn0a ^= S[s][0][d0];
				syn0b ^= S[s][0][d1];
			}

			if (nr >= 2) {
				syn1a ^= S[s][1][d0];
				syn1b ^= S[s][1][d1];
			}
			if (nr >= 3) {
				syn2a ^= S[s][2][d0];
				syn2b ^= S[s][2][d1];
			}
			if (nr >= 4) {
				syn3a ^= S[s][3][d0];
				syn3b ^= S[s][3][d1];
			}
			if (nr >= 5) {
				syn4a ^= S[s][4][d0];
				syn4b ^= S[s][4][d1];
			}
			if (nr >= 6) {
				syn5a ^= S[s][5][d0];
				syn5b ^= S[s][5][d1];
			}
		}

		p_deltaa = syn0a;
		p_deltab = syn0b;

		/* reconstruct missing blocks via inverse matrix */
		if (!has_p) {
			uint8_t ba = R[0][0][syn0a];
			uint8_t bb = R[0][0][syn0b];

			if (nr >= 2) {
				ba ^= R[0][1][syn1a];
				bb ^= R[0][1][syn1b];
			}
			if (nr >= 3) {
				ba ^= R[0][2][syn2a];
				bb ^= R[0][2][syn2b];
			}
			if (nr >= 4) {
				ba ^= R[0][3][syn3a];
				bb ^= R[0][3][syn3b];
			}
			if (nr >= 5) {
				ba ^= R[0][4][syn4a];
				bb ^= R[0][4][syn4b];
			}
			if (nr >= 6) {
				ba ^= R[0][5][syn5a];
				bb ^= R[0][5][syn5b];
			}

			pa[0][i] = ba;
			pa[0][i + 1] = bb;
		}
		if (nr >= 2) {
			int rj = 1 - has_p;
			uint8_t ba = R[rj][0][syn0a];
			uint8_t bb = R[rj][0][syn0b];

			if (nr >= 2) {
				ba ^= R[rj][1][syn1a];
				bb ^= R[rj][1][syn1b];
			}
			if (nr >= 3) {
				ba ^= R[rj][2][syn2a];
				bb ^= R[rj][2][syn2b];
			}
			if (nr >= 4) {
				ba ^= R[rj][3][syn3a];
				bb ^= R[rj][3][syn3b];
			}
			if (nr >= 5) {
				ba ^= R[rj][4][syn4a];
				bb ^= R[rj][4][syn4b];
			}
			if (nr >= 6) {
				ba ^= R[rj][5][syn5a];
				bb ^= R[rj][5][syn5b];
			}

			if (has_p) {
				p_deltaa ^= ba;
				p_deltab ^= bb;
			}

			pa[rj][i] = ba;
			pa[rj][i + 1] = bb;
		}
		if (nr >= 3) {
			int rj = 2 - has_p;
			uint8_t ba = R[rj][0][syn0a];
			uint8_t bb = R[rj][0][syn0b];

			if (nr >= 2) {
				ba ^= R[rj][1][syn1a];
				bb ^= R[rj][1][syn1b];
			}
			if (nr >= 3) {
				ba ^= R[rj][2][syn2a];
				bb ^= R[rj][2][syn2b];
			}
			if (nr >= 4) {
				ba ^= R[rj][3][syn3a];
				bb ^= R[rj][3][syn3b];
			}
			if (nr >= 5) {
				ba ^= R[rj][4][syn4a];
				bb ^= R[rj][4][syn4b];
			}
			if (nr >= 6) {
				ba ^= R[rj][5][syn5a];
				bb ^= R[rj][5][syn5b];
			}

			if (has_p) {
				p_deltaa ^= ba;
				p_deltab ^= bb;
			}

			pa[rj][i] = ba;
			pa[rj][i + 1] = bb;
		}
		if (nr >= 4) {
			int rj = 3 - has_p;
			uint8_t ba = R[rj][0][syn0a];
			uint8_t bb = R[rj][0][syn0b];

			if (nr >= 2) {
				ba ^= R[rj][1][syn1a];
				bb ^= R[rj][1][syn1b];
			}
			if (nr >= 3) {
				ba ^= R[rj][2][syn2a];
				bb ^= R[rj][2][syn2b];
			}
			if (nr >= 4) {
				ba ^= R[rj][3][syn3a];
				bb ^= R[rj][3][syn3b];
			}
			if (nr >= 5) {
				ba ^= R[rj][4][syn4a];
				bb ^= R[rj][4][syn4b];
			}
			if (nr >= 6) {
				ba ^= R[rj][5][syn5a];
				bb ^= R[rj][5][syn5b];
			}

			if (has_p) {
				p_deltaa ^= ba;
				p_deltab ^= bb;
			}

			pa[rj][i] = ba;
			pa[rj][i + 1] = bb;
		}
		if (nr >= 5) {
			int rj = 4 - has_p;
			uint8_t ba = R[rj][0][syn0a];
			uint8_t bb = R[rj][0][syn0b];

			if (nr >= 2) {
				ba ^= R[rj][1][syn1a];
				bb ^= R[rj][1][syn1b];
			}
			if (nr >= 3) {
				ba ^= R[rj][2][syn2a];
				bb ^= R[rj][2][syn2b];
			}
			if (nr >= 4) {
				ba ^= R[rj][3][syn3a];
				bb ^= R[rj][3][syn3b];
			}
			if (nr >= 5) {
				ba ^= R[rj][4][syn4a];
				bb ^= R[rj][4][syn4b];
			}
			if (nr >= 6) {
				ba ^= R[rj][5][syn5a];
				bb ^= R[rj][5][syn5b];
			}

			if (has_p) {
				p_deltaa ^= ba;
				p_deltab ^= bb;
			}

			pa[rj][i] = ba;
			pa[rj][i + 1] = bb;
		}
		if (nr >= 6) {
			int rj = 5 - has_p;
			uint8_t ba = R[rj][0][syn0a];
			uint8_t bb = R[rj][0][syn0b];

			if (nr >= 2) {
				ba ^= R[rj][1][syn1a];
				bb ^= R[rj][1][syn1b];
			}
			if (nr >= 3) {
				ba ^= R[rj][2][syn2a];
				bb ^= R[rj][2][syn2b];
			}
			if (nr >= 4) {
				ba ^= R[rj][3][syn3a];
				bb ^= R[rj][3][syn3b];
			}
			if (nr >= 5) {
				ba ^= R[rj][4][syn4a];
				bb ^= R[rj][4][syn4b];
			}
			if (nr >= 6) {
				ba ^= R[rj][5][syn5a];
				bb ^= R[rj][5][syn5b];
			}

			if (has_p) {
				p_deltaa ^= ba;
				p_deltab ^= bb;
			}

			pa[rj][i] = ba;
			pa[rj][i + 1] = bb;
		}

		if (has_p) {
			pa[nr - 1][i] = p_deltaa;
			pa[nr - 1][i + 1] = p_deltab;
		}
	}
}

void raid_gen2_int32_raid(int nd, size_t size, void **vv, int streaming)
{
	(void)streaming;
	raid_gen2_int32_gen(nd, size, vv, 2);
}

void raid_gen2_int32_aes(int nd, size_t size, void **vv, int streaming)
{
	(void)streaming;
	raid_gen2_int32_gen(nd, size, vv, 3);
}

void raid_gen2_int64_raid(int nd, size_t size, void **vv, int streaming)
{
	(void)streaming;
	raid_gen2_int64_gen(nd, size, vv, 2);
}

void raid_gen2_int64_aes(int nd, size_t size, void **vv, int streaming)
{
	(void)streaming;
	raid_gen2_int64_gen(nd, size, vv, 3);
}

void raid_rec2_int8(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 2);

	if (ip[0] == 0)
		raid_rec2_int8_gen(2, 1, id, ip, nd, size, vv);
	else
		raid_rec2_int8_gen(2, 0, id, ip, nd, size, vv);
}

void raid_rec3_int8(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 3);
	if (ip[0] == 0)
		raid_recX_int8(3, 1, id, ip, nd, size, vv);
	else
		raid_recX_int8(3, 0, id, ip, nd, size, vv);
}

void raid_rec4_int8(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 4);
	if (ip[0] == 0)
		raid_recX_int8(4, 1, id, ip, nd, size, vv);
	else
		raid_recX_int8(4, 0, id, ip, nd, size, vv);
}

void raid_rec5_int8(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 5);
	if (ip[0] == 0)
		raid_recX_int8(5, 1, id, ip, nd, size, vv);
	else
		raid_recX_int8(5, 0, id, ip, nd, size, vv);
}

void raid_rec6_int8(int nr, int *id, int *ip, int nd, size_t size, void **vv)
{
	BUG_ON(nr != 6);
	if (ip[0] == 0)
		raid_recX_int8(6, 1, id, ip, nd, size, vv);
	else
		raid_recX_int8(6, 0, id, ip, nd, size, vv);
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
	raid_rec_register(RAID_ALGO_CAUCHY_PAR3, "int8", raid_rec3_int8, RAID_POLY_ANY);
	raid_rec_register(RAID_ALGO_CAUCHY_PAR4, "int8", raid_rec4_int8, RAID_POLY_ANY);
	raid_rec_register(RAID_ALGO_CAUCHY_PAR5, "int8", raid_rec5_int8, RAID_POLY_ANY);
	raid_rec_register(RAID_ALGO_CAUCHY_PAR6, "int8", raid_rec6_int8, RAID_POLY_ANY);
}
