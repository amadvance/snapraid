/*
 * Copyright (C) 2011 Andrea Mazzoleni
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright 2002-2004 H. Peter Anvin - All Rights Reserved
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 53 Temple Place Ste 330,
 *   Boston MA 02111-1307, USA; either version 2 of the License, or
 *   (at your option) any later version; incorporated herein by reference.
 *
 * ----------------------------------------------------------------------- */

#include "portable.h"

#include "raid.h"
#include "cpu.h"
#include "tables.h"

/* internal forwarder */
static void (*raid5_gen)(unsigned char** buffer, unsigned diskmax, unsigned size);
static void (*raid6_gen)(unsigned char** buffer, unsigned diskmax, unsigned size);

/*
 * 2-way unrolled portable integer math RAID-5 instruction set
 */
void raid5_int32r2(unsigned char** buffer, unsigned diskmax, unsigned size)
{
	unsigned char* p;
	int z, z0;
	unsigned d;

	uint32_t wp0;
	uint32_t wp1;

	z0 = diskmax - 1; /* Highest data disk */
	p = buffer[z0+1]; /* XOR parity */

	for(d=0;d<size;d+=8) {
		wp0 = *(uint32_t *)&buffer[z0][d+0*4];
		wp1 = *(uint32_t *)&buffer[z0][d+1*4];
		for(z=z0-1;z>=0;--z) {
			wp0 ^= *(uint32_t *)&buffer[z][d+0*4];
			wp1 ^= *(uint32_t *)&buffer[z][d+1*4];
		}
		*(uint32_t *)&p[d+4*0] = wp0;
		*(uint32_t *)&p[d+4*1] = wp1;
	}
}

#if defined(__i386__) || defined(__x86_64__)

/*
 * Unrolled-by-2 MMX implementation
 */
void raid5_mmxr2(unsigned char** buffer, unsigned diskmax, unsigned size)
{
	unsigned char* p;
	int z, z0;
	unsigned d;

	z0 = diskmax - 1; /* Highest data disk */
	p = buffer[z0+1]; /* XOR parity */

	for(d=0;d<size;d+=16) {
		asm volatile("movq %0,%%mm2" : : "m" (buffer[z0][d]));
		asm volatile("movq %0,%%mm3" : : "m" (buffer[z0][d+8]));
		for(z=z0-1;z>=0;--z) {
			asm volatile("movq %0,%%mm5" : : "m" (buffer[z][d]));
			asm volatile("movq %0,%%mm7" : : "m" (buffer[z][d+8]));
			asm volatile("pxor %mm5,%mm2");
			asm volatile("pxor %mm7,%mm3");
		}
		asm volatile("movq %%mm2,%0" : "=m" (p[d]));
		asm volatile("movq %%mm3,%0" : "=m" (p[d+8]));
	}

	asm volatile("emms" : : : "memory");
}

void raid5_sse2r2(unsigned char** buffer, unsigned diskmax, unsigned size)
{
	unsigned char* p;
	int z, z0;
	unsigned d;

	z0 = diskmax - 1; /* Highest data disk */
	p = buffer[z0+1]; /* XOR parity */

	for(d=0;d<size;d+=32) {
		asm volatile("movdqa %0,%%xmm2" : : "m" (buffer[z0][d]));
		asm volatile("movdqa %0,%%xmm3" : : "m" (buffer[z0][d+16]));
		for(z=z0-1;z>=0;--z) {
			asm volatile("movdqa %0,%%xmm5" : : "m" (buffer[z][d]));
			asm volatile("movdqa %0,%%xmm7" : : "m" (buffer[z][d+16]));
			asm volatile("pxor %xmm5,%xmm2");
			asm volatile("pxor %xmm7,%xmm3");
		}
		asm volatile("movntdq %%xmm2,%0" : "=m" (p[d]));
		asm volatile("movntdq %%xmm3,%0" : "=m" (p[d+16]));
	}

	asm volatile("sfence" : : : "memory");
}
#endif

/* 
 * Repeats an 8 bit nibble to a full 32 bits value.
 */
#define NBYTES(x) ((x) * 0x01010101U)

/*
 * These sub-operations are separate inlines since they can sometimes be
 * specially optimized using architecture-specific hacks.
 */

/*
 * The SHLBYTE() operation shifts each byte left by 1, *not*
 * rolling over into the next byte.
 */
static inline uint32_t SHLBYTE(uint32_t v)
{
	uint32_t vv;

	vv = (v << 1) & NBYTES(0xfe);
	return vv;
}

/*
 * The MASK() operation returns 0xFF in any byte for which the high
 * bit is 1, 0x00 for any byte for which the high bit is 0.
 */
static inline uint32_t MASK(uint32_t v)
{
	uint32_t vv;

	vv = v & NBYTES(0x80);
	vv = (vv << 1) - (vv >> 7); /* Overflow on the top bit is OK */
	return vv;
}

/*
 * 2-way unrolled portable integer math RAID-6 instruction set
 */
void raid6_int32r2(unsigned char** buffer, unsigned diskmax, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	int z, z0;
	unsigned d;

	uint32_t wd0, wq0, wp0, w10, w20;
	uint32_t wd1, wq1, wp1, w11, w21;

	z0 = diskmax - 1; /* Highest data disk */
	p = buffer[z0+1]; /* XOR parity */
	q = buffer[z0+2]; /* RS qarity */

	for(d=0;d<size;d+=8) {
		wq0 = wp0 = *(uint32_t *)&buffer[z0][d+0*4];
		wq1 = wp1 = *(uint32_t *)&buffer[z0][d+1*4];
		for (z=z0-1;z>= 0;--z) {
			wd0 = *(uint32_t *)&buffer[z][d+0*4];
			wd1 = *(uint32_t *)&buffer[z][d+1*4];
			wp0 ^= wd0;
			wp1 ^= wd1;
			w20 = MASK(wq0);
			w21 = MASK(wq1);
			w10 = SHLBYTE(wq0);
			w11 = SHLBYTE(wq1);
			w20 &= NBYTES(0x1d);
			w21 &= NBYTES(0x1d);
			w10 ^= w20;
			w11 ^= w21;
			wq0 = w10 ^ wd0;
			wq1 = w11 ^ wd1;
		}
		*(uint32_t *)&p[d+4*0] = wp0;
		*(uint32_t *)&p[d+4*1] = wp1;
		*(uint32_t *)&q[d+4*0] = wq0;
		*(uint32_t *)&q[d+4*1] = wq1;
	}
}

#if defined(__i386__) || defined(__x86_64__)

static const struct raid6_mmx_constants {
	uint64_t x1d;
} raid6_mmx_constants = {
	0x1d1d1d1d1d1d1d1dULL,
};

/*
 * Unrolled-by-2 MMX implementation
 */
void raid6_mmxr2(unsigned char** buffer, unsigned diskmax, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	int z, z0;
	unsigned d;

	z0 = diskmax - 1; /* Highest data disk */
	p = buffer[z0+1]; /* XOR parity */
	q = buffer[z0+2]; /* RS qarity */

	asm volatile("movq %0,%%mm0" : : "m" (raid6_mmx_constants.x1d));
	asm volatile("pxor %mm5,%mm5"); /* Zero temp */
	asm volatile("pxor %mm7,%mm7"); /* Zero temp */

	for(d=0;d<size;d+=16) {
		asm volatile("movq %0,%%mm2" : : "m" (buffer[z0][d]));
		asm volatile("movq %0,%%mm3" : : "m" (buffer[z0][d+8]));
		asm volatile("movq %mm2,%mm4");
		asm volatile("movq %mm3,%mm6");
		for(z=z0-1;z>=0;--z) {
			asm volatile("pcmpgtb %mm4,%mm5");
			asm volatile("pcmpgtb %mm6,%mm7");
			asm volatile("paddb %mm4,%mm4");
			asm volatile("paddb %mm6,%mm6");
			asm volatile("pand %mm0,%mm5");
			asm volatile("pand %mm0,%mm7");
			asm volatile("pxor %mm5,%mm4");
			asm volatile("pxor %mm7,%mm6");
			asm volatile("movq %0,%%mm5" : : "m" (buffer[z][d]));
			asm volatile("movq %0,%%mm7" : : "m" (buffer[z][d+8]));
			asm volatile("pxor %mm5,%mm2");
			asm volatile("pxor %mm7,%mm3");
			asm volatile("pxor %mm5,%mm4");
			asm volatile("pxor %mm7,%mm6");
			asm volatile("pxor %mm5,%mm5");
			asm volatile("pxor %mm7,%mm7");
		}
		asm volatile("movq %%mm2,%0" : "=m" (p[d]));
		asm volatile("movq %%mm3,%0" : "=m" (p[d+8]));
		asm volatile("movq %%mm4,%0" : "=m" (q[d]));
		asm volatile("movq %%mm6,%0" : "=m" (q[d+8]));
	}

	asm volatile("emms" : : : "memory");
}

static const struct raid6_sse_constants {
	uint64_t x1d[2];
} raid6_sse_constants  __attribute__((aligned(16))) = {
	{ 0x1d1d1d1d1d1d1d1dULL, 0x1d1d1d1d1d1d1d1dULL },
};

/*
 * Unrolled-by-2 SSE2 implementation
 */
void raid6_sse2r2(unsigned char** buffer, unsigned diskmax, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	int z, z0;
	unsigned d;

	z0 = diskmax - 1; /* Highest data disk */
	p = buffer[z0+1]; /* XOR parity */
	q = buffer[z0+2]; /* RS qarity */

	asm volatile("movdqa %0,%%xmm0" : : "m" (raid6_sse_constants.x1d[0]));
	asm volatile("pxor %xmm5,%xmm5"); /* Zero temp */
	asm volatile("pxor %xmm7,%xmm7"); /* Zero temp */

	/* We uniformly assume a single prefetch covers at least 32 bytes */
	for(d=0;d<size;d+=32) {
		asm volatile("prefetchnta %0" : : "m" (buffer[z0][d]));
		asm volatile("movdqa %0,%%xmm2" : : "m" (buffer[z0][d]));    /* P[0] */
		asm volatile("movdqa %0,%%xmm3" : : "m" (buffer[z0][d+16])); /* P[1] */
		asm volatile("movdqa %xmm2,%xmm4"); /* Q[0] */
		asm volatile("movdqa %xmm3,%xmm6"); /* Q[1] */
		for(z=z0-1;z>=0;--z) {
			asm volatile("prefetchnta %0" : : "m" (buffer[z][d]));
			asm volatile("pcmpgtb %xmm4,%xmm5");
			asm volatile("pcmpgtb %xmm6,%xmm7");
			asm volatile("paddb %xmm4,%xmm4");
			asm volatile("paddb %xmm6,%xmm6");
			asm volatile("pand %xmm0,%xmm5");
			asm volatile("pand %xmm0,%xmm7");
			asm volatile("pxor %xmm5,%xmm4");
			asm volatile("pxor %xmm7,%xmm6");
			asm volatile("movdqa %0,%%xmm5" : : "m" (buffer[z][d]));
			asm volatile("movdqa %0,%%xmm7" : : "m" (buffer[z][d+16]));
			asm volatile("pxor %xmm5,%xmm2");
			asm volatile("pxor %xmm7,%xmm3");
			asm volatile("pxor %xmm5,%xmm4");
			asm volatile("pxor %xmm7,%xmm6");
			asm volatile("pxor %xmm5,%xmm5");
			asm volatile("pxor %xmm7,%xmm7");
		}
		asm volatile("movntdq %%xmm2,%0" : "=m" (p[d]));
		asm volatile("movntdq %%xmm3,%0" : "=m" (p[d+16]));
		asm volatile("movntdq %%xmm4,%0" : "=m" (q[d]));
		asm volatile("movntdq %%xmm6,%0" : "=m" (q[d+16]));
	}

	asm volatile("sfence" : : : "memory");
}
#endif

void raid5_recov_data(unsigned char** dptrs, unsigned diskmax, unsigned size, int faila)
{
	unsigned char *p, *d;

	d = dptrs[faila];
	p = dptrs[diskmax];

	/* Compute syndrome using parity for the missing data page. */
	dptrs[faila] = p;
	dptrs[diskmax] = d;

	raid5_gen(dptrs, diskmax, size);

	/* Restore pointer table */
	dptrs[faila] = d;
	dptrs[diskmax] = p;
}

void raid6_recov_2data(unsigned char** dptrs, unsigned diskmax, unsigned size, int faila, int failb, unsigned char* zero)
{
	unsigned char *p, *q, *dp, *dq;
	unsigned char px, qx, db;
	const unsigned char *pbmul; /* P multiplier table for B data */
	const unsigned char *qmul; /* Q multiplier table (for both) */

	p = dptrs[diskmax];
	q = dptrs[diskmax+1];

	/* Compute syndrome with zero for the missing data pages. */
	/* Use the dead data pages as temporary storage for delta p and delta q. */
	dp = dptrs[faila];
	dptrs[faila] = zero;
	dptrs[diskmax] = dp;
	dq = dptrs[failb];
	dptrs[failb] = zero;
	dptrs[diskmax+1] = dq;

	raid6_gen(dptrs, diskmax, size);

	/* Restore pointer table */
	dptrs[faila] = dp;
	dptrs[failb] = dq;
	dptrs[diskmax] = p;
	dptrs[diskmax+1] = q;

	/* Now, pick the proper data tables */
	pbmul = raid6_gfmul[raid6_gfexi[failb-faila]];
	qmul = raid6_gfmul[raid6_gfinv[raid6_gfexp[faila]^raid6_gfexp[failb]]];

	/* Now do it... */
	while (size--) {
		px = *p ^ *dp;
		qx = qmul[*q ^ *dq];
		*dq++ = db = pbmul[px] ^ qx; /* Reconstructed B */
		*dp++ = db ^ px; /* Reconstructed A */
		p++;
		q++;
	}
}

void raid6_recov_datap(unsigned char** dptrs, unsigned diskmax, unsigned size, int faila, unsigned char* zero)
{
	unsigned char *p, *q, *dq;
	const unsigned char* qmul; /* Q multiplier table */

	p = dptrs[diskmax];
	q = dptrs[diskmax+1];

	/* Compute syndrome with zero for the missing data page. */
	/* Use the dead data page as temporary storage for delta q. */
	dq = dptrs[faila];
	dptrs[faila] = zero;
	dptrs[diskmax+1] = dq;

	raid6_gen(dptrs, diskmax, size);

	/* Restore pointer table */
	dptrs[faila] = dq;
	dptrs[diskmax+1] = q;

	/* Now, pick the proper data tables */
	qmul = raid6_gfmul[raid6_gfinv[raid6_gfexp[faila]]];

	/* Now do it... */
	while (size--) {
		*p++ ^= *dq = qmul[*q ^ *dq];
		q++;
		dq++;
	}
}

void raid_gen(unsigned level, unsigned char** buffer, unsigned diskmax, unsigned size)
{
	if (level == 1) {
		raid5_gen(buffer, diskmax, size);
	} else {
		raid6_gen(buffer, diskmax, size);
	}
}

void raid_init(void)
{
	raid5_gen = raid5_int32r2;
	raid6_gen = raid6_int32r2;
#if defined(__i386__) || defined(__x86_64__)
	if (cpu_has_mmx()) {
		raid5_gen = raid5_mmxr2;
		raid6_gen = raid6_mmxr2;
	}
	if (cpu_has_sse2()) {
		raid5_gen = raid5_sse2r2;
		raid6_gen = raid6_sse2r2;
	}
#endif
}

