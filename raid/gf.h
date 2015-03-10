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

#ifndef __RAID_GF_H
#define __RAID_GF_H

/*
 * Galois field operations.
 *
 * Basic range checks are implemented using BUG_ON().
 */

/*
 * GF a*b.
 */
static __always_inline uint8_t mul(uint8_t a, uint8_t b)
{
	return gfmul[a][b];
}

/*
 * GF 1/a.
 * Not defined for a == 0.
 */
static __always_inline uint8_t inv(uint8_t v)
{
	BUG_ON(v == 0); /* division by zero */

	return gfinv[v];
}

/*
 * GF 2^a.
 */
static __always_inline uint8_t pow2(int v)
{
	BUG_ON(v < 0 || v > 254); /* invalid exponent */

	return gfexp[v];
}

/*
 * Gets the multiplication table for a specified value.
 */
static __always_inline const uint8_t *table(uint8_t v)
{
	return gfmul[v];
}

/*
 * Gets the generator matrix coefficient for parity 'p' and disk 'd'.
 */
static __always_inline uint8_t A(int p, int d)
{
	return gfgen[p][d];
}

/*
 * Dereference as uint8_t
 */
#define v_8(p) (*(uint8_t *)&(p))

/*
 * Dereference as uint32_t
 */
#define v_32(p) (*(uint32_t *)&(p))

/*
 * Dereference as uint64_t
 */
#define v_64(p) (*(uint64_t *)&(p))

/*
 * Multiply each byte of a uint32 by 2 in the GF(2^8).
 */
static __always_inline uint32_t x2_32(uint32_t v)
{
	uint32_t mask = v & 0x80808080U;

	mask = (mask << 1) - (mask >> 7);
	v = (v << 1) & 0xfefefefeU;
	v ^= mask & 0x1d1d1d1dU;
	return v;
}

/*
 * Multiply each byte of a uint64 by 2 in the GF(2^8).
 */
static __always_inline uint64_t x2_64(uint64_t v)
{
	uint64_t mask = v & 0x8080808080808080ULL;

	mask = (mask << 1) - (mask >> 7);
	v = (v << 1) & 0xfefefefefefefefeULL;
	v ^= mask & 0x1d1d1d1d1d1d1d1dULL;
	return v;
}

/*
 * Divide each byte of a uint32 by 2 in the GF(2^8).
 */
static __always_inline uint32_t d2_32(uint32_t v)
{
	uint32_t mask = v & 0x01010101U;

	mask = (mask << 8) - mask;
	v = (v >> 1) & 0x7f7f7f7fU;
	v ^= mask & 0x8e8e8e8eU;
	return v;
}

/*
 * Divide each byte of a uint64 by 2 in the GF(2^8).
 */
static __always_inline uint64_t d2_64(uint64_t v)
{
	uint64_t mask = v & 0x0101010101010101ULL;

	mask = (mask << 8) - mask;
	v = (v >> 1) & 0x7f7f7f7f7f7f7f7fULL;
	v ^= mask & 0x8e8e8e8e8e8e8e8eULL;
	return v;
}

#endif

