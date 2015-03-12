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
#include "gf.h"

/*
 * GEN1 (RAID5 with xor) 32bit C implementation
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
		p0 = v_32(v[l][i]);
		p1 = v_32(v[l][i + 4]);
		for (d = l - 1; d >= 0; --d) {
			p0 ^= v_32(v[d][i]);
			p1 ^= v_32(v[d][i + 4]);
		}
		v_32(p[i]) = p0;
		v_32(p[i + 4]) = p1;
	}
}

/*
 * GEN1 (RAID5 with xor) 64bit C implementation
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
		p0 = v_64(v[l][i]);
		p1 = v_64(v[l][i + 8]);
		for (d = l - 1; d >= 0; --d) {
			p0 ^= v_64(v[d][i]);
			p1 ^= v_64(v[d][i + 8]);
		}
		v_64(p[i]) = p0;
		v_64(p[i + 8]) = p1;
	}
}

/*
 * GEN2 (RAID6 with powers of 2) 32bit C implementation
 */
void raid_gen2_int32(int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *q;
	int d, l;
	size_t i;

	uint32_t d0, q0, p0;
	uint32_t d1, q1, p1;

	l = nd - 1;
	p = v[nd];
	q = v[nd + 1];

	for (i = 0; i < size; i += 8) {
		q0 = p0 = v_32(v[l][i]);
		q1 = p1 = v_32(v[l][i + 4]);
		for (d = l - 1; d >= 0; --d) {
			d0 = v_32(v[d][i]);
			d1 = v_32(v[d][i + 4]);

			p0 ^= d0;
			p1 ^= d1;

			q0 = x2_32(q0);
			q1 = x2_32(q1);

			q0 ^= d0;
			q1 ^= d1;
		}
		v_32(p[i]) = p0;
		v_32(p[i + 4]) = p1;
		v_32(q[i]) = q0;
		v_32(q[i + 4]) = q1;
	}
}

/*
 * GEN2 (RAID6 with powers of 2) 64bit C implementation
 */
void raid_gen2_int64(int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *q;
	int d, l;
	size_t i;

	uint64_t d0, q0, p0;
	uint64_t d1, q1, p1;

	l = nd - 1;
	p = v[nd];
	q = v[nd + 1];

	for (i = 0; i < size; i += 16) {
		q0 = p0 = v_64(v[l][i]);
		q1 = p1 = v_64(v[l][i + 8]);
		for (d = l - 1; d >= 0; --d) {
			d0 = v_64(v[d][i]);
			d1 = v_64(v[d][i + 8]);

			p0 ^= d0;
			p1 ^= d1;

			q0 = x2_64(q0);
			q1 = x2_64(q1);

			q0 ^= d0;
			q1 ^= d1;
		}
		v_64(p[i]) = p0;
		v_64(p[i + 8]) = p1;
		v_64(q[i]) = q0;
		v_64(q[i + 8]) = q1;
	}
}

/*
 * GEN3 (triple parity with Cauchy matrix) 8bit C implementation
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
			d0 = v_8(v[d][i]);

			p0 ^= d0;
			q0 ^= gfmul[d0][gfgen[1][d]];
			r0 ^= gfmul[d0][gfgen[2][d]];
		}

		/* first disk with all coefficients at 1 */
		d0 = v_8(v[0][i]);

		p0 ^= d0;
		q0 ^= d0;
		r0 ^= d0;

		v_8(p[i]) = p0;
		v_8(q[i]) = q0;
		v_8(r[i]) = r0;
	}
}

/*
 * GEN4 (quad parity with Cauchy matrix) 8bit C implementation
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
			d0 = v_8(v[d][i]);

			p0 ^= d0;
			q0 ^= gfmul[d0][gfgen[1][d]];
			r0 ^= gfmul[d0][gfgen[2][d]];
			s0 ^= gfmul[d0][gfgen[3][d]];
		}

		/* first disk with all coefficients at 1 */
		d0 = v_8(v[0][i]);

		p0 ^= d0;
		q0 ^= d0;
		r0 ^= d0;
		s0 ^= d0;

		v_8(p[i]) = p0;
		v_8(q[i]) = q0;
		v_8(r[i]) = r0;
		v_8(s[i]) = s0;
	}
}

/*
 * GEN5 (penta parity with Cauchy matrix) 8bit C implementation
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
			d0 = v_8(v[d][i]);

			p0 ^= d0;
			q0 ^= gfmul[d0][gfgen[1][d]];
			r0 ^= gfmul[d0][gfgen[2][d]];
			s0 ^= gfmul[d0][gfgen[3][d]];
			t0 ^= gfmul[d0][gfgen[4][d]];
		}

		/* first disk with all coefficients at 1 */
		d0 = v_8(v[0][i]);

		p0 ^= d0;
		q0 ^= d0;
		r0 ^= d0;
		s0 ^= d0;
		t0 ^= d0;

		v_8(p[i]) = p0;
		v_8(q[i]) = q0;
		v_8(r[i]) = r0;
		v_8(s[i]) = s0;
		v_8(t[i]) = t0;
	}
}

/*
 * GEN6 (hexa parity with Cauchy matrix) 8bit C implementation
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
			d0 = v_8(v[d][i]);

			p0 ^= d0;
			q0 ^= gfmul[d0][gfgen[1][d]];
			r0 ^= gfmul[d0][gfgen[2][d]];
			s0 ^= gfmul[d0][gfgen[3][d]];
			t0 ^= gfmul[d0][gfgen[4][d]];
			u0 ^= gfmul[d0][gfgen[5][d]];
		}

		/* first disk with all coefficients at 1 */
		d0 = v_8(v[0][i]);

		p0 ^= d0;
		q0 ^= d0;
		r0 ^= d0;
		s0 ^= d0;
		t0 ^= d0;
		u0 ^= d0;

		v_8(p[i]) = p0;
		v_8(q[i]) = q0;
		v_8(r[i]) = r0;
		v_8(s[i]) = s0;
		v_8(t[i]) = t0;
		v_8(u[i]) = u0;
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

	/* if it's RAID5 uses the faster function */
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
	const int N = 2;
	const uint8_t *T[N][N];
	uint8_t G[N * N];
	uint8_t V[N * N];
	size_t i;
	int j, k;

	(void)nr; /* unused, it's always 2 */

	/* if it's RAID6 recovering with P and Q uses the faster function */
	if (ip[0] == 0 && ip[1] == 1) {
		raid_rec2of2_int8(id, ip, nd, size, vv);
		return;
	}

	/* setup the coefficients matrix */
	for (j = 0; j < N; ++j)
		for (k = 0; k < N; ++k)
			G[j * N + k] = A(ip[j], id[k]);

	/* invert it to solve the system of linear equations */
	raid_invert(G, V, N);

	/* get multiplication tables */
	for (j = 0; j < N; ++j)
		for (k = 0; k < N; ++k)
			T[j][k] = table(V[j * N + k]);

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
 * Note that referring at previous equations you have:
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

