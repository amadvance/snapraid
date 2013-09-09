/*
 * Copyright (C) 2011 Andrea Mazzoleni
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

/****************************************************************************/
/* internal computation */

/*
 * RAID5 unrolled-by-2 C implementation
 */
void raid5_int32r2(unsigned char** buffer, unsigned diskmax, unsigned size)
{
	unsigned char* p;
	int z, z0;
	unsigned d;

	uint32_t wp0;
	uint32_t wp1;

	z0 = diskmax - 1;
	p = buffer[diskmax];

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
 * RAID5 unrolled-by-2 MMX implementation
 */
void raid5_mmxr2(unsigned char** buffer, unsigned diskmax, unsigned size)
{
	unsigned char* p;
	int z, z0;
	unsigned d;

	z0 = diskmax - 1;
	p = buffer[diskmax];

	for(d=0;d<size;d+=16) {
		asm volatile("movq %0,%%mm0" : : "m" (buffer[z0][d]));
		asm volatile("movq %0,%%mm1" : : "m" (buffer[z0][d+8]));
		for(z=z0-1;z>=0;--z) {
			asm volatile("movq %0,%%mm4" : : "m" (buffer[z][d]));
			asm volatile("movq %0,%%mm5" : : "m" (buffer[z][d+8]));
			asm volatile("pxor %mm4,%mm0");
			asm volatile("pxor %mm5,%mm1");
		}
		asm volatile("movq %%mm0,%0" : "=m" (p[d]));
		asm volatile("movq %%mm1,%0" : "=m" (p[d+8]));
	}

	asm volatile("emms" : : : "memory");
}

/*
 * RAID5 unrolled-by-4 MMX implementation
 */
void raid5_mmxr4(unsigned char** buffer, unsigned diskmax, unsigned size)
{
	unsigned char* p;
	int z, z0;
	unsigned d;

	z0 = diskmax - 1;
	p = buffer[diskmax];

	for(d=0;d<size;d+=32) {
		asm volatile("movq %0,%%mm0" : : "m" (buffer[z0][d]));
		asm volatile("movq %0,%%mm1" : : "m" (buffer[z0][d+8]));
		asm volatile("movq %0,%%mm2" : : "m" (buffer[z0][d+16]));
		asm volatile("movq %0,%%mm3" : : "m" (buffer[z0][d+24]));
		for(z=z0-1;z>=0;--z) {
			asm volatile("movq %0,%%mm4" : : "m" (buffer[z][d]));
			asm volatile("movq %0,%%mm5" : : "m" (buffer[z][d+8]));
			asm volatile("movq %0,%%mm6" : : "m" (buffer[z][d+16]));
			asm volatile("movq %0,%%mm7" : : "m" (buffer[z][d+24]));
			asm volatile("pxor %mm4,%mm0");
			asm volatile("pxor %mm5,%mm1");
			asm volatile("pxor %mm6,%mm2");
			asm volatile("pxor %mm7,%mm3");
		}
		asm volatile("movq %%mm0,%0" : "=m" (p[d]));
		asm volatile("movq %%mm1,%0" : "=m" (p[d+8]));
		asm volatile("movq %%mm2,%0" : "=m" (p[d+16]));
		asm volatile("movq %%mm3,%0" : "=m" (p[d+24]));
	}

	asm volatile("emms" : : : "memory");
}

/*
 * RAID5 unrolled-by-2 SSE2 implementation
 */
void raid5_sse2r2(unsigned char** buffer, unsigned diskmax, unsigned size)
{
	unsigned char* p;
	int z, z0;
	unsigned d;

	z0 = diskmax - 1;
	p = buffer[diskmax];

	for(d=0;d<size;d+=32) {
		asm volatile("movdqa %0,%%xmm0" : : "m" (buffer[z0][d]));
		asm volatile("movdqa %0,%%xmm1" : : "m" (buffer[z0][d+16]));
		for(z=z0-1;z>=0;--z) {
			asm volatile("movdqa %0,%%xmm4" : : "m" (buffer[z][d]));
			asm volatile("movdqa %0,%%xmm5" : : "m" (buffer[z][d+16]));
			asm volatile("pxor %xmm4,%xmm0");
			asm volatile("pxor %xmm5,%xmm1");
		}
		asm volatile("movntdq %%xmm0,%0" : "=m" (p[d]));
		asm volatile("movntdq %%xmm1,%0" : "=m" (p[d+16]));
	}

	asm volatile("sfence" : : : "memory");
}

/*
 * RAID5 unrolled-by-4 SSE2 implementation
 */
void raid5_sse2r4(unsigned char** buffer, unsigned diskmax, unsigned size)
{
	unsigned char* p;
	int z, z0;
	unsigned d;

	z0 = diskmax - 1;
	p = buffer[diskmax];

	for(d=0;d<size;d+=64) {
		asm volatile("movdqa %0,%%xmm0" : : "m" (buffer[z0][d]));
		asm volatile("movdqa %0,%%xmm1" : : "m" (buffer[z0][d+16]));
		asm volatile("movdqa %0,%%xmm2" : : "m" (buffer[z0][d+32]));
		asm volatile("movdqa %0,%%xmm3" : : "m" (buffer[z0][d+48]));
		for(z=z0-1;z>=0;--z) {
			asm volatile("movdqa %0,%%xmm4" : : "m" (buffer[z][d]));
			asm volatile("movdqa %0,%%xmm5" : : "m" (buffer[z][d+16]));
			asm volatile("movdqa %0,%%xmm6" : : "m" (buffer[z][d+32]));
			asm volatile("movdqa %0,%%xmm7" : : "m" (buffer[z][d+48]));
			asm volatile("pxor %xmm4,%xmm0");
			asm volatile("pxor %xmm5,%xmm1");
			asm volatile("pxor %xmm6,%xmm2");
			asm volatile("pxor %xmm7,%xmm3");
		}
		asm volatile("movntdq %%xmm0,%0" : "=m" (p[d]));
		asm volatile("movntdq %%xmm1,%0" : "=m" (p[d+16]));
		asm volatile("movntdq %%xmm2,%0" : "=m" (p[d+32]));
		asm volatile("movntdq %%xmm3,%0" : "=m" (p[d+48]));
	}

	asm volatile("sfence" : : : "memory");
}
#endif

#if defined(__x86_64__)
/*
 * RAID5 unrolled-by-8 SSE2 implementation
 * Note that it uses all the 16 registers, meaning that x64 is required.
 */
void raid5_sse2r8(unsigned char** buffer, unsigned diskmax, unsigned size)
{
	unsigned char* p;
	int z, z0;
	unsigned d;

	z0 = diskmax - 1;
	p = buffer[diskmax];

	for(d=0;d<size;d+=128) {
		asm volatile("movdqa %0,%%xmm0" : : "m" (buffer[z0][d]));
		asm volatile("movdqa %0,%%xmm1" : : "m" (buffer[z0][d+16]));
		asm volatile("movdqa %0,%%xmm2" : : "m" (buffer[z0][d+32]));
		asm volatile("movdqa %0,%%xmm3" : : "m" (buffer[z0][d+48]));
		asm volatile("movdqa %0,%%xmm4" : : "m" (buffer[z0][d+64]));
		asm volatile("movdqa %0,%%xmm5" : : "m" (buffer[z0][d+80]));
		asm volatile("movdqa %0,%%xmm6" : : "m" (buffer[z0][d+96]));
		asm volatile("movdqa %0,%%xmm7" : : "m" (buffer[z0][d+112]));
		for(z=z0-1;z>=0;--z) {
			asm volatile("movdqa %0,%%xmm8" : : "m" (buffer[z][d]));
			asm volatile("movdqa %0,%%xmm9" : : "m" (buffer[z][d+16]));
			asm volatile("movdqa %0,%%xmm10" : : "m" (buffer[z][d+32]));
			asm volatile("movdqa %0,%%xmm11" : : "m" (buffer[z][d+48]));
			asm volatile("movdqa %0,%%xmm12" : : "m" (buffer[z][d+64]));
			asm volatile("movdqa %0,%%xmm13" : : "m" (buffer[z][d+80]));
			asm volatile("movdqa %0,%%xmm14" : : "m" (buffer[z][d+96]));
			asm volatile("movdqa %0,%%xmm15" : : "m" (buffer[z][d+112]));
			asm volatile("pxor %xmm8,%xmm0");
			asm volatile("pxor %xmm9,%xmm1");
			asm volatile("pxor %xmm10,%xmm2");
			asm volatile("pxor %xmm11,%xmm3");
			asm volatile("pxor %xmm12,%xmm4");
			asm volatile("pxor %xmm13,%xmm5");
			asm volatile("pxor %xmm14,%xmm6");
			asm volatile("pxor %xmm15,%xmm7");
		}
		asm volatile("movntdq %%xmm0,%0" : "=m" (p[d]));
		asm volatile("movntdq %%xmm1,%0" : "=m" (p[d+16]));
		asm volatile("movntdq %%xmm2,%0" : "=m" (p[d+32]));
		asm volatile("movntdq %%xmm3,%0" : "=m" (p[d+48]));
		asm volatile("movntdq %%xmm4,%0" : "=m" (p[d+64]));
		asm volatile("movntdq %%xmm5,%0" : "=m" (p[d+80]));
		asm volatile("movntdq %%xmm6,%0" : "=m" (p[d+96]));
		asm volatile("movntdq %%xmm7,%0" : "=m" (p[d+112]));
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
 * RAID6 unrolled-by-2 C implementation
 */
void raid6_int32r2(unsigned char** buffer, unsigned diskmax, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	int z, z0;
	unsigned d;

	uint32_t wd0, wq0, wp0, w10, w20;
	uint32_t wd1, wq1, wp1, w11, w21;

	z0 = diskmax - 1;
	p = buffer[diskmax];
	q = buffer[diskmax+1];

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
 * RAID6 unrolled-by-2 MMX implementation
 */
void raid6_mmxr2(unsigned char** buffer, unsigned diskmax, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	int z, z0;
	unsigned d;

	z0 = diskmax - 1;
	p = buffer[diskmax];
	q = buffer[diskmax+1];

	asm volatile("movq %0,%%mm0" : : "m" (raid6_mmx_constants.x1d));
	asm volatile("pxor %mm5,%mm5");
	asm volatile("pxor %mm7,%mm7");

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
 * RAID6 unrolled-by-2 SSE2 implementation
 */
void raid6_sse2r2(unsigned char** buffer, unsigned diskmax, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	int z, z0;
	unsigned d;

	z0 = diskmax - 1;
	p = buffer[diskmax];
	q = buffer[diskmax+1];

	asm volatile("movdqa %0,%%xmm7" : : "m" (raid6_sse_constants.x1d[0]));
	asm volatile("pxor %xmm4,%xmm4");
	asm volatile("pxor %xmm5,%xmm5");

	for(d=0;d<size;d+=32) {
		asm volatile("movdqa %0,%%xmm0" : : "m" (buffer[z0][d])); /* P[0] */
		asm volatile("movdqa %0,%%xmm1" : : "m" (buffer[z0][d+16])); /* P[1] */
		asm volatile("movdqa %xmm0,%xmm2"); /* Q[0] */
		asm volatile("movdqa %xmm1,%xmm3"); /* Q[1] */
		for(z=z0-1;z>=0;--z) {
			asm volatile("pcmpgtb %xmm2,%xmm4");
			asm volatile("pcmpgtb %xmm3,%xmm5");
			asm volatile("paddb %xmm2,%xmm2");
			asm volatile("paddb %xmm3,%xmm3");
			asm volatile("pand %xmm7,%xmm4");
			asm volatile("pand %xmm7,%xmm5");
			asm volatile("pxor %xmm4,%xmm2");
			asm volatile("pxor %xmm5,%xmm3");
			asm volatile("movdqa %0,%%xmm4" : : "m" (buffer[z][d]));
			asm volatile("movdqa %0,%%xmm5" : : "m" (buffer[z][d+16]));
			asm volatile("pxor %xmm4,%xmm0");
			asm volatile("pxor %xmm5,%xmm1");
			asm volatile("pxor %xmm4,%xmm2");
			asm volatile("pxor %xmm5,%xmm3");
			asm volatile("pxor %xmm4,%xmm4");
			asm volatile("pxor %xmm5,%xmm5");
		}
		asm volatile("movntdq %%xmm0,%0" : "=m" (p[d]));
		asm volatile("movntdq %%xmm1,%0" : "=m" (p[d+16]));
		asm volatile("movntdq %%xmm2,%0" : "=m" (q[d]));
		asm volatile("movntdq %%xmm3,%0" : "=m" (q[d+16]));
	}

	asm volatile("sfence" : : : "memory");
}
#endif

#if defined(__x86_64__)
/*
 * RAID6 unrolled-by-4 SSE2 implementation
 * Note that it uses all the 16 registers, meaning that x64 is required.
 */
void raid6_sse2r4(unsigned char** buffer, unsigned diskmax, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	int z, z0;
	unsigned d;

	z0 = diskmax - 1;
	p = buffer[diskmax];
	q = buffer[diskmax+1];

	asm volatile("movdqa %0,%%xmm15" : : "m" (raid6_sse_constants.x1d[0]));
	asm volatile("pxor %xmm8,%xmm8");
	asm volatile("pxor %xmm9,%xmm9");
	asm volatile("pxor %xmm10,%xmm10");
	asm volatile("pxor %xmm11,%xmm11");

	for(d=0;d<size;d+=64) {
		asm volatile("movdqa %0,%%xmm0" : : "m" (buffer[z0][d])); /* P[0] */
		asm volatile("movdqa %0,%%xmm1" : : "m" (buffer[z0][d+16])); /* P[1] */
		asm volatile("movdqa %0,%%xmm2" : : "m" (buffer[z0][d+32])); /* P[2] */
		asm volatile("movdqa %0,%%xmm3" : : "m" (buffer[z0][d+48])); /* P[3] */
		asm volatile("movdqa %xmm0,%xmm4"); /* Q[0] */
		asm volatile("movdqa %xmm1,%xmm5"); /* Q[1] */
		asm volatile("movdqa %xmm2,%xmm6"); /* Q[2] */
		asm volatile("movdqa %xmm3,%xmm7"); /* Q[3] */
		for(z=z0-1;z>=0;--z) {
			asm volatile("pcmpgtb %xmm4,%xmm8");
			asm volatile("pcmpgtb %xmm5,%xmm9");
			asm volatile("pcmpgtb %xmm6,%xmm10");
			asm volatile("pcmpgtb %xmm7,%xmm11");
			asm volatile("paddb %xmm4,%xmm4");
			asm volatile("paddb %xmm5,%xmm5");
			asm volatile("paddb %xmm6,%xmm6");
			asm volatile("paddb %xmm7,%xmm7");
			asm volatile("pand %xmm15,%xmm8");
			asm volatile("pand %xmm15,%xmm9");
			asm volatile("pand %xmm15,%xmm10");
			asm volatile("pand %xmm15,%xmm11");
			asm volatile("pxor %xmm8,%xmm4");
			asm volatile("pxor %xmm9,%xmm5");
			asm volatile("pxor %xmm10,%xmm6");
			asm volatile("pxor %xmm11,%xmm7");
			asm volatile("movdqa %0,%%xmm8" : : "m" (buffer[z][d]));
			asm volatile("movdqa %0,%%xmm9" : : "m" (buffer[z][d+16]));
			asm volatile("movdqa %0,%%xmm10" : : "m" (buffer[z][d+32]));
			asm volatile("movdqa %0,%%xmm11" : : "m" (buffer[z][d+48]));
			asm volatile("pxor %xmm8,%xmm0");
			asm volatile("pxor %xmm9,%xmm1");
			asm volatile("pxor %xmm10,%xmm2");
			asm volatile("pxor %xmm11,%xmm3");
			asm volatile("pxor %xmm8,%xmm4");
			asm volatile("pxor %xmm9,%xmm5");
			asm volatile("pxor %xmm10,%xmm6");
			asm volatile("pxor %xmm11,%xmm7");
			asm volatile("pxor %xmm8,%xmm8");
			asm volatile("pxor %xmm9,%xmm9");
			asm volatile("pxor %xmm10,%xmm10");
			asm volatile("pxor %xmm11,%xmm11");
		}
		asm volatile("movntdq %%xmm0,%0" : "=m" (p[d]));
		asm volatile("movntdq %%xmm1,%0" : "=m" (p[d+16]));
		asm volatile("movntdq %%xmm2,%0" : "=m" (p[d+32]));
		asm volatile("movntdq %%xmm3,%0" : "=m" (p[d+48]));
		asm volatile("movntdq %%xmm4,%0" : "=m" (q[d]));
		asm volatile("movntdq %%xmm5,%0" : "=m" (q[d+16]));
		asm volatile("movntdq %%xmm6,%0" : "=m" (q[d+32]));
		asm volatile("movntdq %%xmm7,%0" : "=m" (q[d+48]));
	}

	asm volatile("sfence" : : : "memory");
}
#endif

/*
 * RAIDTP unrolled-by-2 C implementation
 */
void raidTP_int32r2(unsigned char** buffer, unsigned diskmax, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	int z, z0;
	unsigned d;

	uint32_t wd0, wr0, wq0, wp0, w10, w20;
	uint32_t wd1, wr1, wq1, wp1, w11, w21;

	z0 = diskmax - 1;
	p = buffer[diskmax];
	q = buffer[diskmax+1];
	r = buffer[diskmax+2];

	for(d=0;d<size;d+=8) {
		wr0 = wq0 = wp0 = *(uint32_t *)&buffer[z0][d+0*4];
		wr1 = wq1 = wp1 = *(uint32_t *)&buffer[z0][d+1*4];
		for (z=z0-1;z>= 0;--z) {
			wd0 = *(uint32_t *)&buffer[z][d+0*4];
			wd1 = *(uint32_t *)&buffer[z][d+1*4];

			/* wp += wd */
			wp0 ^= wd0;
			wp1 ^= wd1;

			/* w1 = wq * {02} */
			w20 = MASK(wq0);
			w21 = MASK(wq1);
			w10 = SHLBYTE(wq0);
			w11 = SHLBYTE(wq1);
			w20 &= NBYTES(0x1d);
			w21 &= NBYTES(0x1d);
			w10 ^= w20;
			w11 ^= w21;

			/* wq = w1 + wd */
			wq0 = w10 ^ wd0;
			wq1 = w11 ^ wd1;

			/* w1 = wr * {02} */
			w20 = MASK(wr0);
			w21 = MASK(wr1);
			w10 = SHLBYTE(wr0);
			w11 = SHLBYTE(wr1);
			w20 &= NBYTES(0x1d);
			w21 &= NBYTES(0x1d);
			w10 ^= w20;
			w11 ^= w21;

			/* w1 = w1 * {02} */
			w20 = MASK(w10);
			w21 = MASK(w11);
			w10 = SHLBYTE(w10);
			w11 = SHLBYTE(w11);
			w20 &= NBYTES(0x1d);
			w21 &= NBYTES(0x1d);
			w10 ^= w20;
			w11 ^= w21;

			/* wr = w1 + wd */
			wr0 = w10 ^ wd0;
			wr1 = w11 ^ wd1;
		}
		*(uint32_t *)&p[d+4*0] = wp0;
		*(uint32_t *)&p[d+4*1] = wp1;
		*(uint32_t *)&q[d+4*0] = wq0;
		*(uint32_t *)&q[d+4*1] = wq1;
		*(uint32_t *)&r[d+4*0] = wr0;
		*(uint32_t *)&r[d+4*1] = wr1;
	}
}

#if defined(__i386__) || defined(__x86_64__)
/*
 * RAIDTP unrolled-by-1 MMX implementation
 */
void raidTP_mmxr1(unsigned char** buffer, unsigned diskmax, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	int z, z0;
	unsigned d;

	z0 = diskmax - 1;
	p = buffer[diskmax];
	q = buffer[diskmax+1];
	r = buffer[diskmax+2];

	asm volatile("movq %0,%%mm7" : : "m" (raid6_mmx_constants.x1d));
	asm volatile("pxor %mm4,%mm4");
	asm volatile("pxor %mm5,%mm5");

	for(d=0;d<size;d+=8) {
		asm volatile("movq %0,%%mm0" : : "m" (buffer[z0][d])); /* P[0] */
		asm volatile("movq %mm0,%mm1"); /* Q[0] */
		asm volatile("movq %mm0,%mm2"); /* Q[0] */
		for(z=z0-1;z>=0;--z) {
			asm volatile("pcmpgtb %mm1,%mm4");
			asm volatile("pcmpgtb %mm2,%mm5");
			asm volatile("paddb %mm1,%mm1");
			asm volatile("paddb %mm2,%mm2");
			asm volatile("pand %mm7,%mm4");
			asm volatile("pand %mm7,%mm5");
			asm volatile("pxor %mm4,%mm1");
			asm volatile("pxor %mm5,%mm2");
			asm volatile("pcmpgtb %mm2,%mm6");
			asm volatile("paddb %mm2,%mm2");
			asm volatile("pand %mm7,%mm6");
			asm volatile("pxor %mm6,%mm2");
			asm volatile("movq %0,%%mm3" : : "m" (buffer[z][d]));
			asm volatile("pxor %mm3,%mm0");
			asm volatile("pxor %mm3,%mm1");
			asm volatile("pxor %mm3,%mm2");
			asm volatile("pxor %mm4,%mm4");
			asm volatile("pxor %mm5,%mm5");
			asm volatile("pxor %mm6,%mm6");
		}
		asm volatile("movq %%mm0,%0" : "=m" (p[d]));
		asm volatile("movq %%mm1,%0" : "=m" (q[d]));
		asm volatile("movq %%mm2,%0" : "=m" (r[d]));
	}

	asm volatile("emms" : : : "memory");
}
#endif

#if defined(__i386__) || defined(__x86_64__)
/*
 * RAIDTP unrolled-by-1 SSE2 implementation
 */
void raidTP_sse2r1(unsigned char** buffer, unsigned diskmax, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	int z, z0;
	unsigned d;

	z0 = diskmax - 1;
	p = buffer[diskmax];
	q = buffer[diskmax+1];
	r = buffer[diskmax+2];

	asm volatile("movdqa %0,%%xmm7" : : "m" (raid6_sse_constants.x1d[0]));
	asm volatile("pxor %xmm4,%xmm4");
	asm volatile("pxor %xmm5,%xmm5");
	asm volatile("pxor %xmm6,%xmm6");

	for(d=0;d<size;d+=16) {
		asm volatile("movdqa %0,%%xmm0" : : "m" (buffer[z0][d])); /* P[0] */
		asm volatile("movdqa %xmm0,%xmm1"); /* Q[0] */
		asm volatile("movdqa %xmm0,%xmm2"); /* R[0] */
		for(z=z0-1;z>=0;--z) {
			asm volatile("pcmpgtb %xmm1,%xmm4");
			asm volatile("pcmpgtb %xmm2,%xmm5");
			asm volatile("paddb %xmm1,%xmm1");
			asm volatile("paddb %xmm2,%xmm2");
			asm volatile("pand %xmm7,%xmm4");
			asm volatile("pand %xmm7,%xmm5");
			asm volatile("pxor %xmm4,%xmm1");
			asm volatile("pxor %xmm5,%xmm2");
			asm volatile("pcmpgtb %xmm2,%xmm6");
			asm volatile("paddb %xmm2,%xmm2");
			asm volatile("pand %xmm7,%xmm6");
			asm volatile("pxor %xmm6,%xmm2");
			asm volatile("movdqa %0,%%xmm3" : : "m" (buffer[z][d]));
			asm volatile("pxor %xmm3,%xmm0");
			asm volatile("pxor %xmm3,%xmm1");
			asm volatile("pxor %xmm3,%xmm2");
			asm volatile("pxor %xmm4,%xmm4");
			asm volatile("pxor %xmm5,%xmm5");
			asm volatile("pxor %xmm6,%xmm6");
		}
		asm volatile("movntdq %%xmm0,%0" : "=m" (p[d]));
		asm volatile("movntdq %%xmm1,%0" : "=m" (q[d]));
		asm volatile("movntdq %%xmm2,%0" : "=m" (r[d]));
	}

	asm volatile("sfence" : : : "memory");
}
#endif


#if defined(__x86_64__)
/*
 * RAIDTP unrolled-by-2 SSE2 implementation
 * Note that it uses all the 16 registers, meaning that x64 is required.
 */
void raidTP_sse2r2(unsigned char** buffer, unsigned diskmax, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	int z, z0;
	unsigned d;

	z0 = diskmax - 1;
	p = buffer[diskmax];
	q = buffer[diskmax+1];
	r = buffer[diskmax+2];

	asm volatile("movdqa %0,%%xmm7" : : "m" (raid6_sse_constants.x1d[0]));
	asm volatile("pxor %xmm4,%xmm4");
	asm volatile("pxor %xmm5,%xmm5");
	asm volatile("pxor %xmm6,%xmm6");
	asm volatile("pxor %xmm12,%xmm12");
	asm volatile("pxor %xmm13,%xmm13");
	asm volatile("pxor %xmm14,%xmm14");

	for(d=0;d<size;d+=32) {
		asm volatile("movdqa %0,%%xmm0" : : "m" (buffer[z0][d])); /* P[0] */
		asm volatile("movdqa %0,%%xmm8" : : "m" (buffer[z0][d+16])); /* P[1] */
		asm volatile("movdqa %xmm0,%xmm1"); /* Q[0] */
		asm volatile("movdqa %xmm8,%xmm9"); /* Q[1] */
		asm volatile("movdqa %xmm0,%xmm2"); /* R[0] */
		asm volatile("movdqa %xmm8,%xmm10"); /* R[1] */
		for(z=z0-1;z>=0;--z) {
			asm volatile("pcmpgtb %xmm1,%xmm4");
			asm volatile("pcmpgtb %xmm2,%xmm5");
			asm volatile("pcmpgtb %xmm9,%xmm12");
			asm volatile("pcmpgtb %xmm10,%xmm13");
			asm volatile("paddb %xmm1,%xmm1");
			asm volatile("paddb %xmm2,%xmm2");
			asm volatile("paddb %xmm9,%xmm9");
			asm volatile("paddb %xmm10,%xmm10");
			asm volatile("pand %xmm7,%xmm4");
			asm volatile("pand %xmm7,%xmm5");
			asm volatile("pand %xmm7,%xmm12");
			asm volatile("pand %xmm7,%xmm13");
			asm volatile("pxor %xmm4,%xmm1");
			asm volatile("pxor %xmm5,%xmm2");
			asm volatile("pxor %xmm12,%xmm9");
			asm volatile("pxor %xmm13,%xmm10");
			asm volatile("pcmpgtb %xmm2,%xmm6");
			asm volatile("pcmpgtb %xmm10,%xmm14");
			asm volatile("paddb %xmm2,%xmm2");
			asm volatile("paddb %xmm10,%xmm10");
			asm volatile("pand %xmm7,%xmm6");
			asm volatile("pand %xmm7,%xmm14");
			asm volatile("pxor %xmm6,%xmm2");
			asm volatile("pxor %xmm14,%xmm10");
			asm volatile("movdqa %0,%%xmm3" : : "m" (buffer[z][d]));
			asm volatile("movdqa %0,%%xmm11" : : "m" (buffer[z][d+16]));
			asm volatile("pxor %xmm3,%xmm0");
			asm volatile("pxor %xmm3,%xmm1");
			asm volatile("pxor %xmm3,%xmm2");
			asm volatile("pxor %xmm11,%xmm8");
			asm volatile("pxor %xmm11,%xmm9");
			asm volatile("pxor %xmm11,%xmm10");
			asm volatile("pxor %xmm4,%xmm4");
			asm volatile("pxor %xmm5,%xmm5");
			asm volatile("pxor %xmm6,%xmm6");
			asm volatile("pxor %xmm12,%xmm12");
			asm volatile("pxor %xmm13,%xmm13");
			asm volatile("pxor %xmm14,%xmm14");
		}
		asm volatile("movntdq %%xmm0,%0" : "=m" (p[d]));
		asm volatile("movntdq %%xmm8,%0" : "=m" (p[d+16]));
		asm volatile("movntdq %%xmm1,%0" : "=m" (q[d]));
		asm volatile("movntdq %%xmm9,%0" : "=m" (q[d+16]));
		asm volatile("movntdq %%xmm2,%0" : "=m" (r[d]));
		asm volatile("movntdq %%xmm10,%0" : "=m" (r[d+16]));
	}

	asm volatile("sfence" : : : "memory");
}
#endif

/****************************************************************************/
/* generic computation */

/* internal forwarder */
static void (*raid5_gen)(unsigned char** buffer, unsigned diskmax, unsigned size);
static void (*raid6_gen)(unsigned char** buffer, unsigned diskmax, unsigned size);
static void (*raidTP_gen)(unsigned char** buffer, unsigned diskmax, unsigned size);

void raid_gen(unsigned level, unsigned char** buffer, unsigned diskmax, unsigned size)
{
	switch (level) {
	case 1 : raid5_gen(buffer, diskmax, size); break;
	case 2 : raid6_gen(buffer, diskmax, size); break;
	case 3 : raidTP_gen(buffer, diskmax, size); break;
	}
}

/****************************************************************************/
/* recovering */

/**
 * GF a*b.
 */
static inline unsigned char mul(unsigned char a, unsigned char b)
{
	return gfmul[a][b];
}

/**
 * GF 1/a.
 * Not defined for a == 0.
 */
static inline unsigned char inv(unsigned char a)
{
#ifdef CHECKER
	if (a == 0) {
		fprintf(stderr, "GF division by zero\n");
		exit(EXIT_FAILURE);
	}
#endif
	return gfinv[a];
}

/**
 * GF 2^a.
 * Not defined for a == 255.
 */
static inline unsigned char pow2(int v)
{
#ifdef CHECKER
	if (v < 0 || v > 254) {
		fprintf(stderr, "GF invalid exponent\n");
		exit(EXIT_FAILURE);
	}
#endif
	return gfexp2[v];
}

/**
 * GF 4^a.
 * Not defined for a == 255.
 */
static inline unsigned char pow4(int v)
{
#ifdef CHECKER
	if (v < 0 || v > 254) {
		fprintf(stderr, "GF invalid exponent\n");
		exit(EXIT_FAILURE);
	}
#endif
	return gfexp4[v];
}

/**
 * GF multiplication table.
 */
static inline const unsigned char* table(unsigned char a)
{
	return gfmul[a];
}

void raid5_recov_data(unsigned char** dptrs, unsigned diskmax, unsigned size, int faila)
{
	unsigned char* p;
	unsigned char* dp;

	dp = dptrs[faila];
	p = dptrs[diskmax];

	/* compute syndrome using parity for the missing data page */
	dptrs[faila] = p;
	dptrs[diskmax] = dp;

	raid5_gen(dptrs, diskmax, size);

	/* restore pointers */
	dptrs[faila] = dp;
	dptrs[diskmax] = p;
}

void raid6_recov_2data(unsigned char** dptrs, unsigned diskmax, unsigned size, int faila, int failb, unsigned char* zero)
{
	unsigned char* p;
	unsigned char* dp;
	unsigned char* q;
	unsigned char* dq;
	const unsigned char* pbmul; /* P multiplier to compute B */
	const unsigned char* qbmul; /* Q multiplier to compute B */

	p = dptrs[diskmax];
	q = dptrs[diskmax+1];

	/* compute syndrome with zero for the missing data pages */
	/* use the dead data pages as temporary storage for delta p and delta q. */
	dp = dptrs[faila];
	dptrs[faila] = zero;
	dptrs[diskmax] = dp;
	dq = dptrs[failb];
	dptrs[failb] = zero;
	dptrs[diskmax+1] = dq;

	raid6_gen(dptrs, diskmax, size);

	/* restore pointers */
	dptrs[faila] = dp;
	dptrs[failb] = dq;
	dptrs[diskmax] = p;
	dptrs[diskmax+1] = q;

	/* select tables */
	pbmul = table( inv(pow2(failb-faila) ^ 1) );
	qbmul = table( inv(pow2(faila) ^ pow2(failb)) );

	while (size--) {
		/* delta */
		unsigned char pd = *p ^ *dp;
		unsigned char qd = *q ^ *dq;

		/* addends to reconstruct B */
		unsigned char pbm = pbmul[pd];
		unsigned char qbm = qbmul[qd];

		/* reconstruct B */
		unsigned char b = pbm ^ qbm;

		/* reconstruct A */
		unsigned char a = pd ^ b;

		/* set */
		*dp = a;
		*dq = b;

		++p;
		++dp;
		++q;
		++dq;
	}
}

void raid6_recov_datap(unsigned char** dptrs, unsigned diskmax, unsigned size, int faila, unsigned char* zero)
{
	unsigned char* q;
	unsigned char* dq;
	const unsigned char* qamul; /* Q multiplier to compute A */

	q = dptrs[diskmax+1];

	/* compute syndrome with zero for the missing data page */
	/* use the dead data page as temporary storage for delta q */
	dq = dptrs[faila];
	dptrs[faila] = zero;
	dptrs[diskmax+1] = dq;

	raid6_gen(dptrs, diskmax, size);

	/* restore pointers */
	dptrs[faila] = dq;
	dptrs[diskmax+1] = q;

	/* select tables */
	qamul = table( inv(pow2(faila)) );

	while (size--) {
		/* delta */
		unsigned char qd = *q ^ *dq;

		/* addends to reconstruct A */
		unsigned char qam = qamul[qd];

		/* reconstruct A */
		unsigned char a = qam;

		/* set */
		*dq = a;

		++q;
		++dq;
	}
}

void raidTP_recov_datapq(unsigned char** dptrs, unsigned diskmax, unsigned size, int faila, unsigned char* zero)
{
	unsigned char* r;
	unsigned char* dr;
	const unsigned char* ramul; /* R multiplier to compute A */

	r = dptrs[diskmax+2];

	/* compute syndrome with zero for the missing data page */
	/* use the dead data page as temporary storage for delta r */
	dr = dptrs[faila];
	dptrs[faila] = zero;
	dptrs[diskmax+2] = dr;

	raidTP_gen(dptrs, diskmax, size);

	/* restore pointers */
	dptrs[faila] = dr;
	dptrs[diskmax+2] = r;

	/* select tables */
	ramul = table( inv(pow4(faila)) );

	while (size--) {
		/* delta */
		unsigned char rd = *r ^ *dr;

		/* addends to reconstruct A */
		unsigned char ram = ramul[rd];

		/* reconstruct A */
		unsigned char a = ram;

		/* set */
		*dr = a;

		++r;
		++dr;
	}
}

void raidTP_recov_2dataq(unsigned char** dptrs, unsigned diskmax, unsigned size, int faila, int failb, unsigned char* zero)
{
	unsigned char* p;
	unsigned char* dp;
	unsigned char* r;
	unsigned char* dr;
	const unsigned char* pbmul; /* Q multiplier to compute B */
	const unsigned char* rbmul; /* R multiplier to compute B */
	unsigned char c1;

	p = dptrs[diskmax];
	r = dptrs[diskmax+2];

	/* compute syndrome with zero for the missing data page */
	/* use the dead data page as temporary storage for delta p and r */
	dp = dptrs[faila];
	dr = dptrs[failb];
	dptrs[faila] = zero;
	dptrs[failb] = zero;
	dptrs[diskmax] = dp;
	dptrs[diskmax+2] = dr;

	raidTP_gen(dptrs, diskmax, size);

	/* restore pointers */
	dptrs[faila] = dp;
	dptrs[failb] = dr;
	dptrs[diskmax] = p;
	dptrs[diskmax+2] = r;

	/* select tables */
	c1 = inv(pow4(failb-faila) ^ 1);
	pbmul = table( c1 );
	rbmul = table( mul(c1, inv(pow4(faila))) );

	while (size--) {
		/* delta */
		unsigned char pd = *p ^ *dp;
		unsigned char rd = *r ^ *dr;

		/* addends to reconstruct B */
		unsigned char pbm = pbmul[pd];
		unsigned char rbm = rbmul[rd];

		/* reconstruct B */
		unsigned char b = pbm ^ rbm;

		/* reconstruct A */
		unsigned char a = pd ^ b;

		/* set */
		*dp = a;
		*dr = b;

		++p;
		++dp;
		++r;
		++dr;
	}
}

void raidTP_recov_2datap(unsigned char** dptrs, unsigned diskmax, unsigned size, int faila, int failb, unsigned char* zero)
{
	unsigned char* q;
	unsigned char* dq;
	unsigned char* r;
	unsigned char* dr;
	const unsigned char* qbmul; /* Q multiplier to compute B */
	const unsigned char* rbmul; /* R multiplier to compute B */
	const unsigned char* qamul; /* Q multiplier to compute A */
	const unsigned char* bamul; /* B multiplier to compute A */
	unsigned char c1;

	q = dptrs[diskmax+1];
	r = dptrs[diskmax+2];

	/* compute syndrome with zero for the missing data page */
	/* use the dead data page as temporary storage for delta q and r */
	dq = dptrs[faila];
	dr = dptrs[failb];
	dptrs[faila] = zero;
	dptrs[failb] = zero;
	dptrs[diskmax+1] = dq;
	dptrs[diskmax+2] = dr;

	raidTP_gen(dptrs, diskmax, size);

	/* restore pointers */
	dptrs[faila] = dq;
	dptrs[failb] = dr;
	dptrs[diskmax+1] = q;
	dptrs[diskmax+2] = r;

	/* select tables */
	c1 = inv(pow2(failb-faila) ^ pow4(failb-faila));
	qbmul = table( mul(c1, inv(pow2(faila))) );
	rbmul = table( mul(c1, inv(pow4(faila))) );
	qamul = table( inv(pow2(faila)) );
	bamul = table( pow2(failb-faila) );

	while (size--) {
		/* delta */
		unsigned char qd = *q ^ *dq;
		unsigned char rd = *r ^ *dr;

		/* addends to reconstruct B */
		unsigned char qbm = qbmul[qd];
		unsigned char rbm = rbmul[rd];

		/* reconstruct B */
		unsigned char b = qbm ^ rbm;

		/* addends to reconstruct A */
		unsigned char qam = qamul[qd];
		unsigned char bam = bamul[b];

		/* reconstruct A */
		unsigned char a = qam ^ bam;

		/* set */
		*dq = a;
		*dr = b;

		++q;
		++dq;
		++r;
		++dr;
	}
}

void raidTP_recov_3data(unsigned char** dptrs, unsigned diskmax, unsigned size, int faila, int failb, int failc, unsigned char* zero)
{
	unsigned char* p;
	unsigned char* dp;
	unsigned char* q;
	unsigned char* dq;
	unsigned char* r;
	unsigned char* dr;
	const unsigned char* pcmul; /* P multiplier to compute C */
	const unsigned char* qcmul; /* Q multiplier to compute C */
	const unsigned char* rcmul; /* R multiplier to compute C */
	const unsigned char* pbmul; /* P multiplier to compute B */
	const unsigned char* qbmul; /* Q multiplier to compute B */
	const unsigned char* cbmul; /* C multiplier to compute B */
	unsigned char c1, c2, c3;

	p = dptrs[diskmax];
	q = dptrs[diskmax+1];
	r = dptrs[diskmax+2];

	/* compute syndrome with zero for the missing data page */
	/* use the dead data page as temporary storage for delta p, q and r */
	dp = dptrs[faila];
	dq = dptrs[failb];
	dr = dptrs[failc];
	dptrs[faila] = zero;
	dptrs[failb] = zero;
	dptrs[failc] = zero;
	dptrs[diskmax] = dp;
	dptrs[diskmax+1] = dq;
	dptrs[diskmax+2] = dr;

	raidTP_gen(dptrs, diskmax, size);

	/* restore pointers */
	dptrs[faila] = dp;
	dptrs[failb] = dq;
	dptrs[failc] = dr;
	dptrs[diskmax] = p;
	dptrs[diskmax+1] = q;
	dptrs[diskmax+2] = r;

	/* select tables */
	c1 = inv(pow2(failb-faila) ^ 1);
	c2 = inv(pow4(failb-faila) ^ 1);
	c3 = inv(mul(pow2(failc-faila) ^ 1, c1) ^ mul(pow4(failc-faila) ^ 1, c2));
	pcmul = table( mul(c3, c1) ^ mul(c3, c2) );
	qcmul = table( mul(c3, inv(pow2(failb) ^ pow2(faila))) );
	rcmul = table( mul(c3, inv(pow4(failb) ^ pow4(faila))) );
	pbmul = table( c1 );
	qbmul = table( mul(c1, inv(pow2(faila))) );
	cbmul = table( mul(c1, pow2(failc-faila) ^ 1) );

	while (size--) {
		/* delta */
		unsigned char pd = *p ^ *dp;
		unsigned char qd = *q ^ *dq;
		unsigned char rd = *r ^ *dr;

		/* addends to reconstruct C */
		unsigned char pcm = pcmul[pd];
		unsigned char qcm = qcmul[qd];
		unsigned char rcm = rcmul[rd];

		/* reconstruct C */
		unsigned char c = pcm ^ qcm ^ rcm;

		/* addends to reconstruct B */
		unsigned char pbm = pbmul[pd];
		unsigned char qbm = qbmul[qd];
		unsigned char cbm = cbmul[c];

		/* reconstruct B */
		unsigned char b = pbm ^ qbm ^ cbm;

		/* reconstruct A */
		unsigned char a = pd ^ b ^ c;

		/* set */
		*dp = a;
		*dq = b;
		*dr = c;

		++p;
		++dp;
		++q;
		++dq;
		++r;
		++dr;
	}
}

/****************************************************************************/
/* init/done */

void raid_init(void)
{
	raid5_gen = raid5_int32r2;
	raid6_gen = raid6_int32r2;
	raidTP_gen = raidTP_int32r2;
#if defined(__i386__) || defined(__x86_64__)
	if (cpu_has_mmx()) {
		raid5_gen = raid5_mmxr4;
		raid6_gen = raid6_mmxr2;
		raidTP_gen = raidTP_mmxr1;
	}
	if (cpu_has_sse2()) {
#if defined(__x86_64__)
		raid5_gen = raid5_sse2r8;
		raid6_gen = raid6_sse2r4;
		raidTP_gen = raidTP_sse2r2;
#else
		raid5_gen = raid5_sse2r4;
		raid6_gen = raid6_sse2r2;
		raidTP_gen = raidTP_sse2r1;
#endif
	}
#endif
}

