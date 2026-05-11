// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2013 Andrea Mazzoleni

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
 * Polynomial-dependent XOR masks for GF(2^8) multiply and divide by 2.
 *
 * RAID_POLY_32 and RAID_POLY_64 are RAID_POLY repeated in every byte
 * of a uint32/uint64, used as the conditional XOR in the multiply-by-2
 * path: when the MSB of a byte is set before the left shift, the
 * reducing polynomial is XORed in to reduce the result back into the
 * field.
 *
 * RAID_INV2_32 and RAID_INV2_64 are the GF(2^8) inverse of 2 (i.e.
 * 2^{-1} mod RAID_POLY) repeated in every byte, used in the
 * divide-by-2 path. The inverse depends on the polynomial:
 *
 *   RAID_POLY = 0x1d  ->  2^{-1} = 0x8e  (standard RAID)
 *   RAID_POLY = 0x1b  ->  2^{-1} = 0x8d  (AES polynomial)
 *
 * Both are computed from RAID_POLY via the macro RAID_INV2_BYTE:
 * in GF(2^8), 2^{-1} is the value x such that 2*x = 1, which for
 * any polynomial of the form x^8 + ... + 1 resolves to:
 *
 *   2^{-1} = (RAID_POLY >> 1) | 0x80
 *
 * since right-shifting the polynomial by 1 and setting the MSB gives
 * the unique solution in both cases.
 */
#define RAID_INV2_BYTE (((RAID_POLY) >> 1) | 0x80)

#define RAID_POLY_32 ((uint32_t)RAID_POLY * 0x01010101U)
#define RAID_POLY_64 ((uint64_t)RAID_POLY * 0x0101010101010101ULL)
#define RAID_INV2_32 ((uint32_t)RAID_INV2_BYTE * 0x01010101U)
#define RAID_INV2_64 ((uint64_t)RAID_INV2_BYTE * 0x0101010101010101ULL)

/*
 * Multiply each byte of a uint32 by 2 in GF(2^8).
 */
static __always_inline uint32_t x2_32(uint32_t v)
{
	uint32_t mask = v & 0x80808080U;

	mask = (mask << 1) - (mask >> 7);
	v = (v << 1) & 0xfefefefeU;
	v ^= mask & RAID_POLY_32;
	return v;
}

/*
 * Multiply each byte of a uint64 by 2 in GF(2^8).
 */
static __always_inline uint64_t x2_64(uint64_t v)
{
	uint64_t mask = v & 0x8080808080808080ULL;

	mask = (mask << 1) - (mask >> 7);
	v = (v << 1) & 0xfefefefefefefefeULL;
	v ^= mask & RAID_POLY_64;
	return v;
}

/*
 * Divide each byte of a uint32 by 2 in GF(2^8).
 */
static __always_inline uint32_t d2_32(uint32_t v)
{
	uint32_t mask = v & 0x01010101U;

	mask = (mask << 8) - mask;
	v = (v >> 1) & 0x7f7f7f7fU;
	v ^= mask & RAID_INV2_32;
	return v;
}

/*
 * Divide each byte of a uint64 by 2 in GF(2^8).
 */
static __always_inline uint64_t d2_64(uint64_t v)
{
	uint64_t mask = v & 0x0101010101010101ULL;

	mask = (mask << 8) - mask;
	v = (v >> 1) & 0x7f7f7f7f7f7f7f7fULL;
	v ^= mask & RAID_INV2_64;
	return v;
}

#endif
