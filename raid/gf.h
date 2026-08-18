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
	return raid_gfmul[a][b];
}

/*
 * GF 1/a.
 * Not defined for a == 0.
 */
static __always_inline uint8_t inv(uint8_t v)
{
	BUG_ON(v == 0); /* division by zero */

	return raid_gfinv[v];
}

/*
 * GF 2^a.
 */
static __always_inline uint8_t pow2(int v)
{
	BUG_ON(v < 0 || v > 254); /* invalid exponent */

	return raid_gfexp[v];
}

/*
 * Gets the multiplication table for a specified value.
 */
static __always_inline const uint8_t *table(uint8_t v)
{
	return raid_gfmul[v];
}

/*
 * Gets the generator matrix coefficient for parity 'p' and disk 'd'.
 */
static __always_inline uint8_t A(int p, int d)
{
	return raid_gfgen[p][d];
}

/*
 * Safe memory read/write helpers avoiding strict-aliasing and unaligned access UB.
 */
static __always_inline uint32_t v_read32(const void *ptr)
{
	uint32_t v;

	memcpy(&v, ptr, sizeof(v));
	return v;
}

static __always_inline uint64_t v_read64(const void *ptr)
{
	uint64_t v;

	memcpy(&v, ptr, sizeof(v));
	return v;
}

static __always_inline void v_write32(void *ptr, uint32_t v)
{
	memcpy(ptr, &v, sizeof(v));
}

static __always_inline void v_write64(void *ptr, uint64_t v)
{
	memcpy(ptr, &v, sizeof(v));
}

/*
 * Galois field reduction polynomials.
 *
 * RAID_POLY_ANY  - Wildcard matching algorithms compatible with any polynomial.
 * RAID_POLY_RAID - Standard RAID polynomial 0x1d (x^8 + x^4 + x^3 + x^2 + 1).
 * RAID_POLY_AES  - AES polynomial 0x1b (x^8 + x^4 + x^3 + x + 1) for GFNI acceleration.
 */
#define RAID_POLY_ANY 0
#define RAID_POLY_RAID 0x1d
#define RAID_POLY_AES 0x1b

/*
 * Polynomial-dependent XOR masks for GF(2^8) multiply and divide by 2.
 *
 * raid_poly_32 and raid_poly_64 are RAID_POLY repeated in every byte
 * of a uint32/uint64, used as the conditional XOR in the multiply-by-2
 * path: when the MSB of a byte is set before the left shift, the
 * reducing polynomial is XORed in to reduce the result back into the
 * field.
 *
 * raid_inv2_32 and raid_inv2_64 are the GF(2^8) inverse of 2 (i.e.
 * 2^{-1} mod RAID_POLY) repeated in every byte, used in the
 * divide-by-2 path. The inverse depends on the polynomial:
 *
 *   RAID_POLY = 0x1d  ->  2^{-1} = 0x8e  (standard RAID)
 *   RAID_POLY = 0x1b  ->  2^{-1} = 0x8d  (AES polynomial)
 *
 * Both RAID_INV2_RAID and RAID_INV2_AES are computed from their respective RAID_POLY:
 * in GF(2^8), 2^{-1} is the value x such that 2*x = 1, which for
 * any polynomial of the form x^8 + ... + 1 resolves to:
 *
 *   2^{-1} = (RAID_POLY >> 1) | 0x80
 *
 * since right-shifting the polynomial by 1 and setting the MSB gives
 * the unique solution in both cases.
 */
#define RAID_INV2_RAID (((RAID_POLY_RAID) >> 1) | 0x80)
#define RAID_POLY_32_RAID ((uint32_t)RAID_POLY_RAID * 0x01010101U)
#define RAID_POLY_64_RAID ((uint64_t)RAID_POLY_RAID * 0x0101010101010101ULL)
#define RAID_INV2_32_RAID ((uint32_t)RAID_INV2_RAID * 0x01010101U)
#define RAID_INV2_64_RAID ((uint64_t)RAID_INV2_RAID * 0x0101010101010101ULL)

#define RAID_INV2_AES (((RAID_POLY_AES) >> 1) | 0x80)
#define RAID_POLY_32_AES ((uint32_t)RAID_POLY_AES * 0x01010101U)
#define RAID_POLY_64_AES ((uint64_t)RAID_POLY_AES * 0x0101010101010101ULL)
#define RAID_INV2_32_AES ((uint32_t)RAID_INV2_AES * 0x01010101U)
#define RAID_INV2_64_AES ((uint64_t)RAID_INV2_AES * 0x0101010101010101ULL)

/*
 * Multiply each byte of a uint32 by 2 in GF(2^8).
 */
static __always_inline uint32_t x2_32(uint32_t v, uint32_t poly_32)
{
	uint32_t mask = v & 0x80808080U;

	mask = (mask << 1) - (mask >> 7);
	v = (v << 1) & 0xfefefefeU;
	v ^= mask & poly_32;
	return v;
}

/*
 * Multiply each byte of a uint64 by 2 in GF(2^8).
 */
static __always_inline uint64_t x2_64(uint64_t v, uint64_t poly_64)
{
	uint64_t mask = v & 0x8080808080808080ULL;

	mask = (mask << 1) - (mask >> 7);
	v = (v << 1) & 0xfefefefefefefefeULL;
	v ^= mask & poly_64;
	return v;
}

/*
 * Divide each byte of a uint32 by 2 in GF(2^8).
 */
static __always_inline uint32_t d2_32(uint32_t v, uint32_t inv2_32)
{
	uint32_t mask = v & 0x01010101U;

	mask = (mask << 8) - mask;
	v = (v >> 1) & 0x7f7f7f7fU;
	v ^= mask & inv2_32;
	return v;
}

/*
 * Divide each byte of a uint64 by 2 in GF(2^8).
 */
static __always_inline uint64_t d2_64(uint64_t v, uint64_t inv2_64)
{
	uint64_t mask = v & 0x0101010101010101ULL;

	mask = (mask << 8) - mask;
	v = (v >> 1) & 0x7f7f7f7f7f7f7f7fULL;
	v ^= mask & inv2_64;
	return v;
}

#endif
