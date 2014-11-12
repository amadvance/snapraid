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
 * GENz (triple parity with powers of 2^-1) 32bit C implementation
 */
void raid_genz_int32(int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t**)vv;
	uint8_t *p;
	uint8_t *q;
	uint8_t *r;
	int d, l;
	size_t i;

	uint32_t d0, r0, q0, p0;
	uint32_t d1, r1, q1, p1;

	l = nd - 1;
	p = v[nd];
	q = v[nd + 1];
	r = v[nd + 2];

	for (i = 0; i < size; i += 8) {
		r0 = q0 = p0 = v_32(v[l][i]);
		r1 = q1 = p1 = v_32(v[l][i + 4]);
		for (d = l - 1; d >= 0; --d) {
			d0 = v_32(v[d][i]);
			d1 = v_32(v[d][i + 4]);

			p0 ^= d0;
			p1 ^= d1;

			q0 = x2_32(q0);
			q1 = x2_32(q1);

			q0 ^= d0;
			q1 ^= d1;

			r0 = d2_32(r0);
			r1 = d2_32(r1);

			r0 ^= d0;
			r1 ^= d1;
		}
		v_32(p[i]) = p0;
		v_32(p[i + 4]) = p1;
		v_32(q[i]) = q0;
		v_32(q[i + 4]) = q1;
		v_32(r[i]) = r0;
		v_32(r[i + 4]) = r1;
	}
}

/*
 * GENz (triple parity with powers of 2^-1) 64bit C implementation
 */
void raid_genz_int64(int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t**)vv;
	uint8_t *p;
	uint8_t *q;
	uint8_t *r;
	int d, l;
	size_t i;

	uint64_t d0, r0, q0, p0;
	uint64_t d1, r1, q1, p1;

	l = nd - 1;
	p = v[nd];
	q = v[nd + 1];
	r = v[nd + 2];

	for (i = 0; i < size; i += 16) {
		r0 = q0 = p0 = v_64(v[l][i]);
		r1 = q1 = p1 = v_64(v[l][i + 8]);
		for (d = l - 1; d >= 0; --d) {
			d0 = v_64(v[d][i]);
			d1 = v_64(v[d][i + 8]);

			p0 ^= d0;
			p1 ^= d1;

			q0 = x2_64(q0);
			q1 = x2_64(q1);

			q0 ^= d0;
			q1 ^= d1;

			r0 = d2_64(r0);
			r1 = d2_64(r1);

			r0 ^= d0;
			r1 ^= d1;
		}
		v_64(p[i]) = p0;
		v_64(p[i + 8]) = p1;
		v_64(q[i]) = q0;
		v_64(q[i + 8]) = q1;
		v_64(r[i]) = r0;
		v_64(r[i + 8]) = r1;
	}
}

