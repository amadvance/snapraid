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

#ifndef __RAID_INTERNAL_H
#define __RAID_INTERNAL_H

/*
 * Includes anything required for compatibility.
 */
#include "../portable.h"
#define BUG_ON(a) assert(!(a))

/*
 * Includes the main header.
 */
#include "raid.h"

/*
 * Internal functions.
 *
 * These are intented to provide access for testing.
 */
int raid_selftest(void);
void raid_gen_ref(int nd, int np, size_t size, void **vv);
void raid_invert(uint8_t *M, uint8_t *V, int n);
void raid_delta_gen(int nr, int *id, int *ip, int nd, size_t size, void **v);
void raid_rec1of1(int *id, int nd, size_t size, void **v);
void raid_rec2of2_int8(int *id, int *ip, int nd, size_t size, void **vv);
void raid_gen1_int32(int nd, size_t size, void **vv);
void raid_gen1_int64(int nd, size_t size, void **vv);
void raid_gen1_sse2(int nd, size_t size, void **vv);
void raid_gen2_int32(int nd, size_t size, void **vv);
void raid_gen2_int64(int nd, size_t size, void **vv);
void raid_gen2_sse2(int nd, size_t size, void **vv);
void raid_gen2_sse2ext(int nd, size_t size, void **vv);
void raid_genz_int32(int nd, size_t size, void **vv);
void raid_genz_int64(int nd, size_t size, void **vv);
void raid_genz_sse2(int nd, size_t size, void **vv);
void raid_genz_sse2ext(int nd, size_t size, void **vv);
void raid_gen3_int8(int nd, size_t size, void **vv);
void raid_gen3_ssse3(int nd, size_t size, void **vv);
void raid_gen3_ssse3ext(int nd, size_t size, void **vv);
void raid_gen4_int8(int nd, size_t size, void **vv);
void raid_gen4_ssse3(int nd, size_t size, void **vv);
void raid_gen4_ssse3ext(int nd, size_t size, void **vv);
void raid_gen5_int8(int nd, size_t size, void **vv);
void raid_gen5_ssse3(int nd, size_t size, void **vv);
void raid_gen5_ssse3ext(int nd, size_t size, void **vv);
void raid_gen6_int8(int nd, size_t size, void **vv);
void raid_gen6_ssse3(int nd, size_t size, void **vv);
void raid_gen6_ssse3ext(int nd, size_t size, void **vv);
void raid_rec1_int8(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec2_int8(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_recX_int8(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec1_ssse3(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_rec2_ssse3(int nr, int *id, int *ip, int nd, size_t size, void **vv);
void raid_recX_ssse3(int nr, int *id, int *ip, int nd, size_t size, void **vv);

/*
 * Internal naming.
 *
 * These are intented to provide access for testing.
 */
const char *raid_gen1_tag(void);
const char *raid_gen2_tag(void);
const char *raid_genz_tag(void);
const char *raid_gen3_tag(void);
const char *raid_gen4_tag(void);
const char *raid_gen5_tag(void);
const char *raid_gen6_tag(void);
const char *raid_rec1_tag(void);
const char *raid_rec2_tag(void);
const char *raid_recX_tag(void);

/*
 * Internal forwarders.
 */
extern void (*raid_gen3_ptr)(int nd, size_t size, void **vv);
extern void (*raid_genz_ptr)(int nd, size_t size, void **vv);
extern void (*raid_gen_ptr[RAID_PARITY_MAX])(
	int nd, size_t size, void **vv);
extern void (*raid_rec_ptr[RAID_PARITY_MAX])(
	int nr, int *id, int *ip, int nd, size_t size, void **vv);

/*
 * Tables.
 */
extern const uint8_t raid_gfmul[256][256] __aligned(256);
extern const uint8_t raid_gfexp[256] __aligned(256);
extern const uint8_t raid_gfinv[256] __aligned(256);
extern const uint8_t raid_gfvandermonde[3][256] __aligned(256);
extern const uint8_t raid_gfcauchy[6][256] __aligned(256);
extern const uint8_t raid_gfcauchypshufb[251][4][2][16] __aligned(256);
extern const uint8_t raid_gfmulpshufb[256][2][16] __aligned(256);
extern const uint8_t (*raid_gfgen)[256];
#define gfmul raid_gfmul
#define gfexp raid_gfexp
#define gfinv raid_gfinv
#define gfvandermonde raid_gfvandermonde
#define gfcauchy raid_gfcauchy
#define gfgenpshufb raid_gfcauchypshufb
#define gfmulpshufb raid_gfmulpshufb
#define gfgen raid_gfgen

/*
 * Assembler blocks.
 */
#ifdef CONFIG_X86
static __always_inline void raid_asm_begin(void)
{
}

static __always_inline void raid_asm_end(void)
{
	asm volatile("sfence" : : : "memory");
}
#endif

#endif

