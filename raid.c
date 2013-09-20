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
	asm volatile("pxor %mm6,%mm6");

	for(d=0;d<size;d+=8) {
		asm volatile("movq %0,%%mm0" : : "m" (buffer[z0][d])); /* P[0] */
		asm volatile("movq %mm0,%mm1"); /* Q[0] */
		asm volatile("movq %mm0,%mm2"); /* R[0] */
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

/*
 * RAIDQP unrolled-by-2 C implementation
 */
void raidQP_int32r2(unsigned char** buffer, unsigned diskmax, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	unsigned char* s;
	int z, z0;
	unsigned d;

	uint32_t wd0, ws0, wr0, wq0, wp0, w10, w20;
	uint32_t wd1, ws1, wr1, wq1, wp1, w11, w21;

	z0 = diskmax - 1;
	p = buffer[diskmax];
	q = buffer[diskmax+1];
	r = buffer[diskmax+2];
	s = buffer[diskmax+3];

	for(d=0;d<size;d+=8) {
		ws0 = wr0 = wq0 = wp0 = *(uint32_t *)&buffer[z0][d+0*4];
		ws1 = wr1 = wq1 = wp1 = *(uint32_t *)&buffer[z0][d+1*4];
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

			/* w1 = ws * {02} */
			w20 = MASK(ws0);
			w21 = MASK(ws1);
			w10 = SHLBYTE(ws0);
			w11 = SHLBYTE(ws1);
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

			/* w1 = w1 * {02} */
			w20 = MASK(w10);
			w21 = MASK(w11);
			w10 = SHLBYTE(w10);
			w11 = SHLBYTE(w11);
			w20 &= NBYTES(0x1d);
			w21 &= NBYTES(0x1d);
			w10 ^= w20;
			w11 ^= w21;

			/* ws = w1 + wd */
			ws0 = w10 ^ wd0;
			ws1 = w11 ^ wd1;

		}
		*(uint32_t *)&p[d+4*0] = wp0;
		*(uint32_t *)&p[d+4*1] = wp1;
		*(uint32_t *)&q[d+4*0] = wq0;
		*(uint32_t *)&q[d+4*1] = wq1;
		*(uint32_t *)&r[d+4*0] = wr0;
		*(uint32_t *)&r[d+4*1] = wr1;
		*(uint32_t *)&s[d+4*0] = ws0;
		*(uint32_t *)&s[d+4*1] = ws1;
	}
}

#if defined(__i386__) || defined(__x86_64__)
/*
 * RAIDQP unrolled-by-1 MMX implementation
 */
void raidQP_mmxr1(unsigned char** buffer, unsigned diskmax, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	unsigned char* s;
	int z, z0;
	unsigned d;

	z0 = diskmax - 1;
	p = buffer[diskmax];
	q = buffer[diskmax+1];
	r = buffer[diskmax+2];
	s = buffer[diskmax+3];

	asm volatile("movq %0,%%mm7" : : "m" (raid6_mmx_constants.x1d));
	asm volatile("pxor %mm4,%mm4");
	asm volatile("pxor %mm5,%mm5");

	for(d=0;d<size;d+=8) {
		asm volatile("movq %0,%%mm0" : : "m" (buffer[z0][d])); /* P[0] */
		asm volatile("movq %mm0,%mm1"); /* Q[0] */
		asm volatile("movq %mm0,%mm2"); /* R[0] */
		asm volatile("movq %mm0,%mm3"); /* S[0] */
		for(z=z0-1;z>=0;--z) {
			asm volatile("pcmpgtb %mm1,%mm4");
			asm volatile("pcmpgtb %mm3,%mm5");
			asm volatile("paddb %mm1,%mm1");
			asm volatile("paddb %mm3,%mm3");
			asm volatile("pand %mm7,%mm4");
			asm volatile("pand %mm7,%mm5");
			asm volatile("pxor %mm4,%mm1");
			asm volatile("pxor %mm5,%mm3");

			asm volatile("pxor %mm4,%mm4");
			asm volatile("pxor %mm5,%mm5");

			asm volatile("pcmpgtb %mm2,%mm4");
			asm volatile("pcmpgtb %mm3,%mm5");
			asm volatile("paddb %mm2,%mm2");
			asm volatile("paddb %mm3,%mm3");
			asm volatile("pand %mm7,%mm4");
			asm volatile("pand %mm7,%mm5");
			asm volatile("pxor %mm4,%mm2");
			asm volatile("pxor %mm5,%mm3");

			asm volatile("pxor %mm4,%mm4");
			asm volatile("pxor %mm5,%mm5");

			asm volatile("pcmpgtb %mm2,%mm4");
			asm volatile("pcmpgtb %mm3,%mm5");
			asm volatile("paddb %mm2,%mm2");
			asm volatile("paddb %mm3,%mm3");
			asm volatile("pand %mm7,%mm4");
			asm volatile("pand %mm7,%mm5");
			asm volatile("pxor %mm4,%mm2");
			asm volatile("pxor %mm5,%mm3");

			asm volatile("movq %0,%%mm6" : : "m" (buffer[z][d]));
			asm volatile("pxor %mm6,%mm0");
			asm volatile("pxor %mm6,%mm1");
			asm volatile("pxor %mm6,%mm2");
			asm volatile("pxor %mm6,%mm3");

			asm volatile("pxor %mm4,%mm4");
			asm volatile("pxor %mm5,%mm5");
		}
		asm volatile("movq %%mm0,%0" : "=m" (p[d]));
		asm volatile("movq %%mm1,%0" : "=m" (q[d]));
		asm volatile("movq %%mm2,%0" : "=m" (r[d]));
		asm volatile("movq %%mm3,%0" : "=m" (s[d]));
	}

	asm volatile("emms" : : : "memory");
}
#endif

#if defined(__i386__) || defined(__x86_64__)
/*
 * RAIDQP unrolled-by-1 SSE2 implementation
 */
void raidQP_sse2r1(unsigned char** buffer, unsigned diskmax, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	unsigned char* s;
	int z, z0;
	unsigned d;

	z0 = diskmax - 1;
	p = buffer[diskmax];
	q = buffer[diskmax+1];
	r = buffer[diskmax+2];
	s = buffer[diskmax+3];

	asm volatile("movdqa %0,%%xmm7" : : "m" (raid6_sse_constants.x1d[0]));
	asm volatile("pxor %xmm4,%xmm4");
	asm volatile("pxor %xmm5,%xmm5");

	for(d=0;d<size;d+=16) {
		asm volatile("movdqa %0,%%xmm0" : : "m" (buffer[z0][d])); /* P[0] */
		asm volatile("movdqa %xmm0,%xmm1"); /* Q[0] */
		asm volatile("movdqa %xmm0,%xmm2"); /* R[0] */
		asm volatile("movdqa %xmm0,%xmm3"); /* S[0] */
		for(z=z0-1;z>=0;--z) {
			asm volatile("pcmpgtb %xmm1,%xmm4");
			asm volatile("pcmpgtb %xmm3,%xmm5");
			asm volatile("paddb %xmm1,%xmm1");
			asm volatile("paddb %xmm3,%xmm3");
			asm volatile("pand %xmm7,%xmm4");
			asm volatile("pand %xmm7,%xmm5");
			asm volatile("pxor %xmm4,%xmm1");
			asm volatile("pxor %xmm5,%xmm3");

			asm volatile("pxor %xmm4,%xmm4");
			asm volatile("pxor %xmm5,%xmm5");

			asm volatile("pcmpgtb %xmm2,%xmm4");
			asm volatile("pcmpgtb %xmm3,%xmm5");
			asm volatile("paddb %xmm2,%xmm2");
			asm volatile("paddb %xmm3,%xmm3");
			asm volatile("pand %xmm7,%xmm4");
			asm volatile("pand %xmm7,%xmm5");
			asm volatile("pxor %xmm4,%xmm2");
			asm volatile("pxor %xmm5,%xmm3");

			asm volatile("pxor %xmm4,%xmm4");
			asm volatile("pxor %xmm5,%xmm5");

			asm volatile("pcmpgtb %xmm2,%xmm4");
			asm volatile("pcmpgtb %xmm3,%xmm5");
			asm volatile("paddb %xmm2,%xmm2");
			asm volatile("paddb %xmm3,%xmm3");
			asm volatile("pand %xmm7,%xmm4");
			asm volatile("pand %xmm7,%xmm5");
			asm volatile("pxor %xmm4,%xmm2");
			asm volatile("pxor %xmm5,%xmm3");

			asm volatile("movdqa %0,%%xmm6" : : "m" (buffer[z][d]));
			asm volatile("pxor %xmm6,%xmm0");
			asm volatile("pxor %xmm6,%xmm1");
			asm volatile("pxor %xmm6,%xmm2");
			asm volatile("pxor %xmm6,%xmm3");

			asm volatile("pxor %xmm4,%xmm4");
			asm volatile("pxor %xmm5,%xmm5");
		}
		asm volatile("movntdq %%xmm0,%0" : "=m" (p[d]));
		asm volatile("movntdq %%xmm1,%0" : "=m" (q[d]));
		asm volatile("movntdq %%xmm2,%0" : "=m" (r[d]));
		asm volatile("movntdq %%xmm3,%0" : "=m" (s[d]));
	}

	asm volatile("sfence" : : : "memory");
}
#endif

#if defined(__x86_64__)
/*
 * RAIDQP unrolled-by-2 SSE2 implementation
 * Note that it uses all the 16 registers, meaning that x64 is required.
 */
void raidQP_sse2r2(unsigned char** buffer, unsigned diskmax, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	unsigned char* s;
	int z, z0;
	unsigned d;

	z0 = diskmax - 1;
	p = buffer[diskmax];
	q = buffer[diskmax+1];
	r = buffer[diskmax+2];
	s = buffer[diskmax+3];

	asm volatile("movdqa %0,%%xmm7" : : "m" (raid6_sse_constants.x1d[0]));
	asm volatile("pxor %xmm4,%xmm4");
	asm volatile("pxor %xmm5,%xmm5");
	asm volatile("pxor %xmm12,%xmm12");
	asm volatile("pxor %xmm13,%xmm13");

	for(d=0;d<size;d+=32) {
		asm volatile("movdqa %0,%%xmm0" : : "m" (buffer[z0][d])); /* P[0] */
		asm volatile("movdqa %0,%%xmm8" : : "m" (buffer[z0][d+16])); /* P[1] */
		asm volatile("movdqa %xmm0,%xmm1"); /* Q[0] */
		asm volatile("movdqa %xmm8,%xmm9"); /* Q[1] */
		asm volatile("movdqa %xmm0,%xmm2"); /* R[0] */
		asm volatile("movdqa %xmm8,%xmm10"); /* R[1] */
		asm volatile("movdqa %xmm0,%xmm3"); /* S[0] */
		asm volatile("movdqa %xmm8,%xmm11"); /* S[1] */
		for(z=z0-1;z>=0;--z) {
			asm volatile("pcmpgtb %xmm1,%xmm4");
			asm volatile("pcmpgtb %xmm3,%xmm5");
			asm volatile("pcmpgtb %xmm9,%xmm12");
			asm volatile("pcmpgtb %xmm11,%xmm13");
			asm volatile("paddb %xmm1,%xmm1");
			asm volatile("paddb %xmm3,%xmm3");
			asm volatile("paddb %xmm9,%xmm9");
			asm volatile("paddb %xmm11,%xmm11");
			asm volatile("pand %xmm7,%xmm4");
			asm volatile("pand %xmm7,%xmm5");
			asm volatile("pand %xmm7,%xmm12");
			asm volatile("pand %xmm7,%xmm13");
			asm volatile("pxor %xmm4,%xmm1");
			asm volatile("pxor %xmm5,%xmm3");
			asm volatile("pxor %xmm12,%xmm9");
			asm volatile("pxor %xmm13,%xmm11");

			asm volatile("pxor %xmm4,%xmm4");
			asm volatile("pxor %xmm5,%xmm5");
			asm volatile("pxor %xmm12,%xmm12");
			asm volatile("pxor %xmm13,%xmm13");

			asm volatile("pcmpgtb %xmm2,%xmm4");
			asm volatile("pcmpgtb %xmm3,%xmm5");
			asm volatile("pcmpgtb %xmm10,%xmm12");
			asm volatile("pcmpgtb %xmm11,%xmm13");
			asm volatile("paddb %xmm2,%xmm2");
			asm volatile("paddb %xmm3,%xmm3");
			asm volatile("paddb %xmm10,%xmm10");
			asm volatile("paddb %xmm11,%xmm11");
			asm volatile("pand %xmm7,%xmm4");
			asm volatile("pand %xmm7,%xmm5");
			asm volatile("pand %xmm7,%xmm12");
			asm volatile("pand %xmm7,%xmm13");
			asm volatile("pxor %xmm4,%xmm2");
			asm volatile("pxor %xmm5,%xmm3");
			asm volatile("pxor %xmm12,%xmm10");
			asm volatile("pxor %xmm13,%xmm11");

			asm volatile("pxor %xmm4,%xmm4");
			asm volatile("pxor %xmm5,%xmm5");
			asm volatile("pxor %xmm12,%xmm12");
			asm volatile("pxor %xmm13,%xmm13");

			asm volatile("pcmpgtb %xmm2,%xmm4");
			asm volatile("pcmpgtb %xmm3,%xmm5");
			asm volatile("pcmpgtb %xmm10,%xmm12");
			asm volatile("pcmpgtb %xmm11,%xmm13");
			asm volatile("paddb %xmm2,%xmm2");
			asm volatile("paddb %xmm3,%xmm3");
			asm volatile("paddb %xmm10,%xmm10");
			asm volatile("paddb %xmm11,%xmm11");
			asm volatile("pand %xmm7,%xmm4");
			asm volatile("pand %xmm7,%xmm5");
			asm volatile("pand %xmm7,%xmm12");
			asm volatile("pand %xmm7,%xmm13");
			asm volatile("pxor %xmm4,%xmm2");
			asm volatile("pxor %xmm5,%xmm3");
			asm volatile("pxor %xmm12,%xmm10");
			asm volatile("pxor %xmm13,%xmm11");

			asm volatile("movdqa %0,%%xmm6" : : "m" (buffer[z][d]));
			asm volatile("movdqa %0,%%xmm14" : : "m" (buffer[z][d+16]));
			asm volatile("pxor %xmm6,%xmm0");
			asm volatile("pxor %xmm6,%xmm1");
			asm volatile("pxor %xmm6,%xmm2");
			asm volatile("pxor %xmm6,%xmm3");
			asm volatile("pxor %xmm14,%xmm8");
			asm volatile("pxor %xmm14,%xmm9");
			asm volatile("pxor %xmm14,%xmm10");
			asm volatile("pxor %xmm14,%xmm11");

			asm volatile("pxor %xmm4,%xmm4");
			asm volatile("pxor %xmm5,%xmm5");
			asm volatile("pxor %xmm12,%xmm12");
			asm volatile("pxor %xmm13,%xmm13");
		}
		asm volatile("movntdq %%xmm0,%0" : "=m" (p[d]));
		asm volatile("movntdq %%xmm8,%0" : "=m" (p[d+16]));
		asm volatile("movntdq %%xmm1,%0" : "=m" (q[d]));
		asm volatile("movntdq %%xmm9,%0" : "=m" (q[d+16]));
		asm volatile("movntdq %%xmm2,%0" : "=m" (r[d]));
		asm volatile("movntdq %%xmm10,%0" : "=m" (r[d+16]));
		asm volatile("movntdq %%xmm3,%0" : "=m" (s[d]));
		asm volatile("movntdq %%xmm11,%0" : "=m" (s[d+16]));
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
static void (*raidQP_gen)(unsigned char** buffer, unsigned diskmax, unsigned size);

void raid_gen(unsigned level, unsigned char** buffer, unsigned diskmax, unsigned size)
{
	switch (level) {
	case 1 : raid5_gen(buffer, diskmax, size); break;
	case 2 : raid6_gen(buffer, diskmax, size); break;
	case 3 : raidTP_gen(buffer, diskmax, size); break;
	case 4 : raidQP_gen(buffer, diskmax, size); break;
	}
}

/****************************************************************************/
/* recovering */

/**
 * The data recovering is based on the paper "The mathematics of RAID-6" [1],
 * that covers the RAID5 and RAID6 computations in the Galois Field GF(2^8)
 * with the primitive polynomial x^8 + x^4 + x^3 + x^2 + 1 (285 decimal),
 * using the following equations to compute syndrome P and Q from a set
 * of n disk Di with 0<=i<n:
 *
 * P = sum(Di) 0<=i<n
 * Q = sum(2^i * Di) 0<=i<n
 *
 * To support RAIDTP (Triple Parity), we use an extension of the same approach,
 * also described in the paper "Multiple-parity RAID" [2], with the additional
 * parity coefficient "4".
 * This method is also the same used by ZFS to implement its RAIDTP support.
 *
 * P = sum(Di) 0<=i<n
 * Q = sum(2^i * Di) 0<=i<n
 * R = sum(4^i * Di) 0<=i<n
 *
 * To support RAIDQP (Quad Parity) we go further using also the parity coefficient "8".
 *
 * P = sum(Di) 0<=i<n
 * Q = sum(2^i * Di) 0<=i<n
 * R = sum(4^i * Di) 0<=i<n
 * S = sum(8^i * Di) 0<=i<n
 *
 * Note that in this case we don't have the guarantee to always have
 * a system of independent linear equations, and in some cases
 * the equations are no not solvable.
 * This is expected because the Vandermonde matrix we use to compute the parity
 * has no guarantee to have all its submatrixes not singular [3, Chap 11, Problem 7]
 * and this is a requirement to have a MDS code [3, Chap 11, Theorem 8].
 *
 * Using the primitive polynomial 285, Quad Parity works for up to 21 data disks
 * with parity coefficients "1,2,4,8". Changing polynomial to one of 391/451/463/487,
 * it works for up to 27 disks with the same parity coefficients.
 * Using different parity coefficients "5,13,27,35" it's possible to
 * make it working for up to 33 disks. But no more.
 *
 * To support more disks it's possible to use the Galois Field GF(2^16) with
 * primitive polynomial 100087 or 122563 that supports Hexa (6) parity with
 * parity coeffiecients 1,2,4,8,16,32 for up to 89 disks.
 *
 * A general method working for any number of disks, is to use a a Cauchy matrix [4],
 * but with a slower computational performance, because the coefficients of
 * the equations are arbitrarely chosen, and not powers of the same coeffiecient.
 * This means that you need to use multiplication tables to implement the
 * syndrome computation, instead of the fast approach described in [1].
 * Note anyway, that there is also a way to implement multiplication tables
 * in a very fast way with SSE instructions [5].
 *
 * In details, Triple Parity is implemented for n disks Di, computing
 * the syndromes P,Q,R with:
 *
 * P = sum(Di) 0<=i<n
 * Q = sum(2^i * Di) 0<=i<n
 * R = sum(4^i * Di) 0<=i<n
 *
 * To recover from a failure of three disks at indexes x,y,z,
 * with 0<=x<y<z<n, we compute the syndromes of the available disks:
 *
 * Pa = sum(Di) 0<=i<n,i!=x,i!=y,i!=z
 * Qa = sum(2^i * Di) 0<=i<n,i!=x,i!=y,i!=z
 * Ra = sum(4^i * Di) 0<=i<n,i!=x,i!=y,i!=z
 *
 * and if we define:
 *
 * Pd = Pa + P
 * Qd = Qa + Q
 * Rd = Ra + R
 *
 * we can sum these two set equations, obtaining:
 *
 * Pd =       Dx +       Dy +       Dz
 * Qd = 2^x * Dx + 2^y * Dy + 2^z * Dz
 * Rd = 4^x * Dx + 4^y * Dy + 4^z * Dz
 *
 * A linear system always solvable because the coefficients matrix is always
 * not singular, including all its submatrix.
 * We can prove that by brute-force, trying all the possible combinations
 * of x,y,z with 0<=x<y<z<255.
 *
 * The other recovering cases are a simplification of the general one,
 * with some equations or addends removed.
 *
 * For Quad Parity we follow the same method starting with:
 *
 * P = sum(Di) 0<=i<n
 * Q = sum(2^i * Di) 0<=i<n
 * R = sum(4^i * Di) 0<=i<n
 * S = sum(8^i * Di) 0<=i<n
 *
 * obtaining:
 *
 * Pd =       Dx +       Dy +       Dz +       Dv
 * Qd = 2^x * Dx + 2^y * Dy + 2^z * Dz + 2^v * Dv
 * Rd = 4^x * Dx + 4^y * Dy + 4^z * Dz + 4^v * Dv
 * Sd = 8^x * Dx + 8^y * Dy + 8^z * Dz + 8^v * Dv
 *
 * In this case the coefficients matrix and all its submatrix are not singular
 * only for up to 21 data disk (0<=x<y<z<v<21 with GF primitive poly 285).
 *
 * References:
 * [1] Anvin, "The mathematics of RAID-6", 2004
 * [2] Brown, "Multiple-parity RAID", 2011
 * [3] MacWilliams, Sloane, "The Theory of Error-Correcting Codes", 1977
 * [4] Blömer, "An XOR-Based Erasure-Resilient Coding Scheme", 1995
 * [5] Plank, "Screaming Fast Galois Field Arithmetic Using Intel SIMD Instructions", 2013
 */

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
	if (a == 0) {
		fprintf(stderr, "GF division by zero\n");
		exit(EXIT_FAILURE);
	}

	return gfinv[a];
}

/**
 * GF 2^a.
 */
static inline unsigned char pow2(int v)
{
	if (v < 0 || v > 254) {
		fprintf(stderr, "GF invalid exponent\n");
		exit(EXIT_FAILURE);
	}

	return gfexp2[v];
}
/**
 * GF 4^a.
 */
static inline unsigned char pow4(int v)
{
	if (v < 0 || v > 254) {
		fprintf(stderr, "GF invalid exponent\n");
		exit(EXIT_FAILURE);
	}

	return gfexp4[v];
}

/**
 * GF 8^a.
 */
static inline unsigned char pow8(int v)
{
	if (v < 0 || v > 254) {
		fprintf(stderr, "GF invalid exponent\n");
		exit(EXIT_FAILURE);
	}

	return gfexp8[v];
}

/**
 * GF 2^b^a.
 */
static inline unsigned char pown(int b, int a)
{
	switch (b) {
	case 0 : return 1;
	case 1 : return pow2(a);
	case 2 : return pow4(a);
	case 3 : return pow8(a);
	}

	fprintf(stderr, "GF invalid base\n");
	exit(EXIT_FAILURE);
}

/**
 * GF multiplication table.
 */
static inline const unsigned char* table(unsigned char a)
{
	return gfmul[a];
}

/**
 * Recover failure of one data block for RAID5.
 *
 * Starting from the equation:
 *
 * Pd = Dx
 *
 * and solving we get:
 *
 * Dx = Pd (this one is easy :D)
 */
static void raid5_recov_data(unsigned char** dptrs, unsigned diskmax, unsigned size, int x)
{
	unsigned char* p;
	unsigned char* dp;

	dp = dptrs[x];
	p = dptrs[diskmax];

	/* compute syndrome using parity for the missing data page */
	dptrs[x] = p;
	dptrs[diskmax] = dp;

	raid5_gen(dptrs, diskmax, size);

	/* restore pointers */
	dptrs[x] = dp;
	dptrs[diskmax] = p;
}

/**
 * Recover failure of two data blocks for RAID6.
 *
 * Starting from the equations:
 *
 * Pd = Dx + Dy
 * Qd = 2^x * Dx + 2^y * Dy
 *
 * and solving we get:
 *
 *            1               2^(-x)
 * Dy = ----------- * Pd + ----------- * Qd
 *      2^(y-x) + 1        2^(y-x) + 1
 *
 * Dx = Dy + Pd
 *
 * with conditions:
 *
 * 2^x != 0
 * 2^(y-x) + 1 != 0
 *
 * That are always satisfied for any 0<=x<y<255.
 */
static void raid6_recov_2data(unsigned char** dptrs, unsigned diskmax, unsigned size, int x, int y, unsigned char* zero)
{
	unsigned char* p;
	unsigned char* dp;
	unsigned char* q;
	unsigned char* dq;
	const unsigned char* pymul; /* P multiplier to compute Dy */
	const unsigned char* qymul; /* Q multiplier to compute Dy */

	p = dptrs[diskmax];
	q = dptrs[diskmax+1];

	/* compute syndrome with zero for the missing data pages */
	/* use the dead data pages as temporary storage for delta p and delta q. */
	dp = dptrs[x];
	dptrs[x] = zero;
	dptrs[diskmax] = dp;
	dq = dptrs[y];
	dptrs[y] = zero;
	dptrs[diskmax+1] = dq;

	raid6_gen(dptrs, diskmax, size);

	/* restore pointers */
	dptrs[x] = dp;
	dptrs[y] = dq;
	dptrs[diskmax] = p;
	dptrs[diskmax+1] = q;

	/* select tables */
	pymul = table( inv(pow2(y-x) ^ 1) );
	qymul = table( inv(pow2(x) ^ pow2(y)) );

	while (size--) {
		/* delta */
		unsigned char pd = *p ^ *dp;
		unsigned char qd = *q ^ *dq;

		/* addends to reconstruct Dy */
		unsigned char pbm = pymul[pd];
		unsigned char qbm = qymul[qd];

		/* reconstruct Dy */
		unsigned char Dy = pbm ^ qbm;

		/* reconstruct Dx */
		unsigned char Dx = pd ^ Dy;

		/* set */
		*dp = Dx;
		*dq = Dy;

		++p;
		++dp;
		++q;
		++dq;
	}
}

/**
 * Recover failure of two data blocks without Q for RAIDTP.
 *
 * Starting from the equations:
 *
 * Pd = Dx + Dy
 * Rd = 4^x * Dx + 4^y * Dy
 *
 * and solving we get:
 *
 *            1               4^(-x)
 * Dy = ----------- * Pd + ----------- * Rd
 *      4^(y-x) + 1        4^(y-x) + 1
 *
 * Dx = Dy + Pd
 *
 * with conditions:
 *
 * 4^x != 0
 * 4^(y-x) + 1 != 0
 *
 * That are always satisfied for any 0<=x<y<255.
 */
static void raidTP_recov_2dataq(unsigned char** dptrs, unsigned diskmax, unsigned size, int x, int y, unsigned char* zero)
{
	unsigned char* p;
	unsigned char* dp;
	unsigned char* r;
	unsigned char* dr;
	const unsigned char* pymul; /* Q multiplier to compute Dy */
	const unsigned char* rymul; /* R multiplier to compute Dy */
	unsigned char c1;

	p = dptrs[diskmax];
	r = dptrs[diskmax+2];

	/* compute syndrome with zero for the missing data page */
	/* use the dead data page as temporary storage for delta p and r */
	dp = dptrs[x];
	dr = dptrs[y];
	dptrs[x] = zero;
	dptrs[y] = zero;
	dptrs[diskmax] = dp;
	dptrs[diskmax+2] = dr;

	raidTP_gen(dptrs, diskmax, size);

	/* restore pointers */
	dptrs[x] = dp;
	dptrs[y] = dr;
	dptrs[diskmax] = p;
	dptrs[diskmax+2] = r;

	/* select tables */
	c1 = inv(pow4(y-x) ^ 1);
	pymul = table( c1 );
	rymul = table( mul(c1, inv(pow4(x))) );

	while (size--) {
		/* delta */
		unsigned char Pd = *p ^ *dp;
		unsigned char Rd = *r ^ *dr;

		/* addends to reconstruct Dy */
		unsigned char pym = pymul[Pd];
		unsigned char rym = rymul[Rd];

		/* reconstruct Dy */
		unsigned char Dy = pym ^ rym;

		/* reconstruct Dx */
		unsigned char Dx = Pd ^ Dy;

		/* set */
		*dp = Dx;
		*dr = Dy;

		++p;
		++dp;
		++r;
		++dr;
	}
}

/**
 * Recover failure of two data blocks without P for RAIDTP.
 *
 * Starting from the equations:
 *
 * Qd = 2^x * Dx + 2^y * Dy
 * Rd = 4^x * Dx + 4^y * Dy
 *
 * and solving we get:
 *
 *            2^(-x)                  4^(-x)
 * Dy = ----------------- * Qd + ----------------- * Rd
 *      2^(y-x) + 4^(y-x)        2^(y-z) + 4^(y-x)
 *
 * Dx = 2^(y-x) * Dy + 2^(-x) * Qd
 *
 * with conditions:
 *
 * 2^x != 0
 * 4^x != 0
 * 2^(y-x) + 4^(y-x) != 0
 *
 * That are always satisfied for any 0<=x<y<255.
 */
static void raidTP_recov_2datap(unsigned char** dptrs, unsigned diskmax, unsigned size, int x, int y, unsigned char* zero)
{
	unsigned char* q;
	unsigned char* dq;
	unsigned char* r;
	unsigned char* dr;
	const unsigned char* qymul; /* Q multiplier to compute Dy */
	const unsigned char* rymul; /* R multiplier to compute Dy */
	const unsigned char* qxmul; /* Q multiplier to compute Dx */
	const unsigned char* yxmul; /* Dy multiplier to compute Dx */
	unsigned char c1;

	q = dptrs[diskmax+1];
	r = dptrs[diskmax+2];

	/* compute syndrome with zero for the missing data page */
	/* use the dead data page as temporary storage for delta q and r */
	dq = dptrs[x];
	dr = dptrs[y];
	dptrs[x] = zero;
	dptrs[y] = zero;
	dptrs[diskmax+1] = dq;
	dptrs[diskmax+2] = dr;

	raidTP_gen(dptrs, diskmax, size);

	/* restore pointers */
	dptrs[x] = dq;
	dptrs[y] = dr;
	dptrs[diskmax+1] = q;
	dptrs[diskmax+2] = r;

	/* select tables */
	c1 = inv(pow2(y-x) ^ pow4(y-x));
	qymul = table( mul(c1, inv(pow2(x))) );
	rymul = table( mul(c1, inv(pow4(x))) );
	qxmul = table( inv(pow2(x)) );
	yxmul = table( pow2(y-x) );

	while (size--) {
		/* delta */
		unsigned char Qd = *q ^ *dq;
		unsigned char Rd = *r ^ *dr;

		/* addends to reconstruct Dy */
		unsigned char qym = qymul[Qd];
		unsigned char rym = rymul[Rd];

		/* reconstruct Dy */
		unsigned char Dy = qym ^ rym;

		/* addends to reconstruct Dx */
		unsigned char qxm = qxmul[Qd];
		unsigned char bxm = yxmul[Dy];

		/* reconstruct Dx */
		unsigned char Dx = qxm ^ bxm;

		/* set */
		*dq = Dx;
		*dr = Dy;

		++q;
		++dq;
		++r;
		++dr;
	}
}

/**
 * Recover failure of three data blocks for RAIDTP.
 *
 * Starting from the equations:
 *
 * Pd = Dx + Dy + Dz
 * Qd = 2^x * Dx + 2^y * Dy + 2^z * Dz
 * Rd = 4^x * Dx + 4^y * Dy + 4^z * Dz
 *
 * and solving we get:
 *
 *                  1                      1                                  1
 * Dz = ------------------------- * ( ----------- * (Pd + 2^(-x) * Qd) + ----------- * (Pd + 4^(-x) * Rd) )
 *      2^(z-x) + 1   4^(z-x) + 1     2^(y-x) + 1                        4^(y-x) + 1
 *      ----------- + -----------
 *      2^(y-x) + 1   4^(y-x) + 1
 *
 *      2^(z-x) + 1             1                2^(-x)
 * Dy = ----------- * Dz + ----------- * Pd + ----------- * Qd
 *      2^(y-x) + 1        2^(y-x) + 1        2^(y-z) + 1
 *
 * Dx = Dy + Dz + Pd
 *
 * with conditions:
 *
 * 2^x != 0
 * 4^x != 0
 * 2^(y-x) + 1 != 0
 * 4^(y-x) + 1 != 0
 * 2^(z-x) + 1   4^(z-x) + 1
 * ----------- + ----------- != 0
 * 2^(y-x) + 1   4^(y-x) + 1
 *
 * That are always satisfied for any 0<=x<y<z<255.
 */
static void raidTP_recov_3data(unsigned char** dptrs, unsigned diskmax, unsigned size, int x, int y, int z, unsigned char* zero)
{
	unsigned char* p;
	unsigned char* dp;
	unsigned char* q;
	unsigned char* dq;
	unsigned char* r;
	unsigned char* dr;
	const unsigned char* pzmul; /* P multiplier to compute Dz */
	const unsigned char* qzmul; /* Q multiplier to compute Dz */
	const unsigned char* rzmul; /* R multiplier to compute Dz */
	const unsigned char* pymul; /* P multiplier to compute Dy */
	const unsigned char* qymul; /* Q multiplier to compute Dy */
	const unsigned char* zymul; /* Dz multiplier to compute Dy */
	unsigned char c1, c2, c3;

	p = dptrs[diskmax];
	q = dptrs[diskmax+1];
	r = dptrs[diskmax+2];

	/* compute syndrome with zero for the missing data page */
	/* use the dead data page as temporary storage for delta p, q and r */
	dp = dptrs[x];
	dq = dptrs[y];
	dr = dptrs[z];
	dptrs[x] = zero;
	dptrs[y] = zero;
	dptrs[z] = zero;
	dptrs[diskmax] = dp;
	dptrs[diskmax+1] = dq;
	dptrs[diskmax+2] = dr;

	raidTP_gen(dptrs, diskmax, size);

	/* restore pointers */
	dptrs[x] = dp;
	dptrs[y] = dq;
	dptrs[z] = dr;
	dptrs[diskmax] = p;
	dptrs[diskmax+1] = q;
	dptrs[diskmax+2] = r;

	/* select tables */
	c1 = inv(pow2(y-x) ^ 1);
	c2 = inv(pow4(y-x) ^ 1);
	c3 = inv(mul(pow2(z-x) ^ 1, c1) ^ mul(pow4(z-x) ^ 1, c2));
	pzmul = table( mul(c3, c1) ^ mul(c3, c2) );
	qzmul = table( mul(c3, inv(pow2(y) ^ pow2(x))) );
	rzmul = table( mul(c3, inv(pow4(y) ^ pow4(x))) );
	pymul = table( c1 );
	qymul = table( mul(c1, inv(pow2(x))) );
	zymul = table( mul(c1, pow2(z-x) ^ 1) );

	while (size--) {
		/* delta */
		unsigned char Pd = *p ^ *dp;
		unsigned char Qd = *q ^ *dq;
		unsigned char Rd = *r ^ *dr;

		/* addends to reconstruct Dz */
		unsigned char pzm = pzmul[Pd];
		unsigned char qzm = qzmul[Qd];
		unsigned char rzm = rzmul[Rd];

		/* reconstruct Dz */
		unsigned char Dz = pzm ^ qzm ^ rzm;

		/* addends to reconstruct Dy */
		unsigned char pym = pymul[Pd];
		unsigned char qym = qymul[Qd];
		unsigned char zym = zymul[Dz];

		/* reconstruct Dy */
		unsigned char Dy = pym ^ qym ^ zym;

		/* reconstruct Dx */
		unsigned char Dx = Pd ^ Dy ^ Dz;

		/* set */
		*dp = Dx;
		*dq = Dy;
		*dr = Dz;

		++p;
		++dp;
		++q;
		++dq;
		++r;
		++dr;
	}
}

/****************************************************************************/
/* matrix recovering  */

/**
 * Inverts the square matrix M of size nxn into V.
 * We use Gauss elimination to invert.
 */
static void invert(unsigned char* M, unsigned char* V, unsigned n)
{
	unsigned i,j,k;

	/* set the identity matrix in V */
	for(i=0;i<n;++i) {
		for(j=0;j<n;++j) {
			V[i*n+j] = i == j;
		}
	}

	/* for each element in the diagonal */
	for(k=0;k<n;++k) {
		unsigned char f;

		/* if the diagonal element is 0 */
		if (M[k*n+k] == 0) {
			/* find the next row with element at the same position not 0 */
			for(i=k+1;i<n;++i)
				if (M[i*n+k] != 0)
					break;

			/* if not found, the matrix is singular */
			if (i == n) {
				fprintf(stderr, "GF inversion of a singular matrix\n");
				exit(EXIT_FAILURE);
			}

			/* sum the row at the present one to make the diagonal element different than 0 */
			/* here usually the two rows are swapped, but summing is also ok */
			for(j=0;j<n;++j) {
				M[k*n+j] ^= M[i*n+j];
				V[k*n+j] ^= V[i*n+j];
			}
		}

		/* make the diagonal element to be 1 */
		f = inv(M[k*n+k]);
		for(j=0;j<n;++j) {
			M[k*n+j] = mul(f, M[k*n+j]);
			V[k*n+j] = mul(f, V[k*n+j]);
		}

		/* make all the elements over and under the diagonal to be 0 */
		for(i=0;i<n;++i) {
			if (i == k)
				continue;
			f = M[i*n+k];
			for(j=0;j<n;++j) {
				M[i*n+j] ^= mul(f, M[k*n+j]);
				V[i*n+j] ^= mul(f, V[k*n+j]);
			}
		}
	}
}

/**
 * Recover failure of one data block x using parity i for any RAID level.
 *
 * Starting from the equation:
 *
 * Sd = (2^i)^x * Dx
 *
 * and solving we get:
 *
 * Dx = (2^i)^(-x) * Sd
 *
 * with conditions:
 *
 * (2^i)^x != 0
 *
 * That are always satisfied for any 0<=x<y<255.
 */
void raid_recov_1data(unsigned char** dptrs, unsigned diskmax, unsigned size, int x, int i, unsigned char* zero)
{
	unsigned char* p;
	unsigned char* dp;
	const unsigned char* pxmul; /* P multiplier to compute Dx */

	/* if it's RAID5 uses the dedicated and faster function */
	if (i == 0) {
		raid5_recov_data(dptrs, diskmax, size, x);
		return;
	}

	p = dptrs[diskmax+i];

	/* compute syndrome with zero for the missing data page */
	/* use the dead data page as temporary storage for delta p */
	dp = dptrs[x];
	dptrs[x] = zero;
	dptrs[diskmax+i] = dp;

	raid_gen(i + 1, dptrs, diskmax, size);

	/* restore pointers */
	dptrs[x] = dp;
	dptrs[diskmax+i] = p;

	/* select tables */
	pxmul = table( inv(pown(i,x)) );

	while (size--) {
		/* delta */
		unsigned char Pd = *p ^ *dp;

		/* addends to reconstruct Dx */
		unsigned char pxm = pxmul[Pd];

		/* reconstruct Dx */
		unsigned char Dx = pxm;

		/* set */
		*dp = Dx;

		++p;
		++dp;
	}
}

/**
 * Recover failure of two data blocks x,y using parity i,j for any RAID level.
 *
 * Starting from the equations:
 *
 * Pd = (2^i)^x * Dx + (2^i)^y * Dy
 * Qd = (2^j)^x * Dx + (2^j)^y * Dy
 *
 * we solve inverting the coefficients matrix.
 */
void raid_recov_2data(unsigned char** dptrs, unsigned diskmax, unsigned size, int x, int y, int i, int j, unsigned char* zero)
{
	unsigned char* p;
	unsigned char* dp;
	unsigned char* q;
	unsigned char* dq;
	const unsigned char* pxmul; /* P multiplier to compute Dx */
	const unsigned char* qxmul; /* Q multiplier to compute Dx */
	const unsigned char* pymul; /* P multiplier to compute Dy */
	const unsigned char* qymul; /* Q multiplier to compute Dy */
	unsigned char G[2*2];
	unsigned char V[2*2];
	unsigned n = 2;

	/* if it's RAID6 uses the dedicated and faster function */
	if (i == 0 && j == 1) {
		raid6_recov_2data(dptrs, diskmax, size, x, y, zero);
		return;
	}
	/* if it's RAIDTP uses the dedicated and faster function */
	if (i == 0 && j == 2) {
		raidTP_recov_2dataq(dptrs, diskmax, size, x, y, zero);
		return;
	}
	/* if it's RAIDTP uses the dedicated and faster function */
	if (i == 1 && j == 2) {
		raidTP_recov_2datap(dptrs, diskmax, size, x, y, zero);
		return;
	}

	p = dptrs[diskmax+i];
	q = dptrs[diskmax+j];

	/* compute syndrome with zero for the missing data page */
	/* use the dead data page as temporary storage for delta p and q */
	dp = dptrs[x];
	dq = dptrs[y];
	dptrs[x] = zero;
	dptrs[y] = zero;
	dptrs[diskmax+i] = dp;
	dptrs[diskmax+j] = dq;

	raid_gen(j + 1, dptrs, diskmax, size);

	/* restore pointers */
	dptrs[x] = dp;
	dptrs[y] = dq;
	dptrs[diskmax+i] = p;
	dptrs[diskmax+j] = q;

	/* setup the generator matrix */
	G[0*n+0] = pown(i,x); /* row 1 for P */
	G[0*n+1] = pown(i,y);
	G[1*n+0] = pown(j,x); /* row 2 for Q */
	G[1*n+1] = pown(j,y);

	/* invert it to solve the system of linear equations */
	invert(G, V, n);

	/* select tables */
	pxmul = table( V[0*n+0] );
	qxmul = table( V[0*n+1] );
	pymul = table( V[1*n+0] );
	qymul = table( V[1*n+1] );

	while (size--) {
		/* delta */
		unsigned char Pd = *p ^ *dp;
		unsigned char Qd = *q ^ *dq;

		/* addends to reconstruct Dy */
		unsigned char pym = pymul[Pd];
		unsigned char qym = qymul[Qd];

		/* reconstruct Dy */
		unsigned char Dy = pym ^ qym;

		/* reconstruct Dx */
		unsigned char Dx;

		/* if i is P, take the fast way */
		if (i == 0) {
			Dx = Pd ^ Dy;
		} else {
			/* addends to reconstruct Dx */
			unsigned char pxm = pxmul[Pd];
			unsigned char qxm = qxmul[Qd];

			/* reconstruct Dx */
			Dx = pxm ^ qxm;
		}

		/* set */
		*dp = Dx;
		*dq = Dy;

		++p;
		++dp;
		++q;
		++dq;
	}
}

/**
 * Recover failure of three data blocks x,y,z using parity i,j,k for any RAID level.
 *
 * Starting from the equations:
 *
 * Pd = (2^i)^x * Dx + (2^i)^y * Dy + (2^i)^z * Dz
 * Qd = (2^j)^x * Dx + (2^j)^y * Dy + (2^j)^z * Dz
 * Rd = (2^k)^x * Dx + (2^k)^y * Dy + (2^k)^z * Dz
 *
 * we solve inverting the coefficients matrix.
 */
void raid_recov_3data(unsigned char** dptrs, unsigned diskmax, unsigned size, int x, int y, int z, int i, int j, int k, unsigned char* zero)
{
	unsigned char* p;
	unsigned char* dp;
	unsigned char* q;
	unsigned char* dq;
	unsigned char* r;
	unsigned char* dr;
	const unsigned char* pxmul; /* P multiplier to compute Dx */
	const unsigned char* qxmul; /* Q multiplier to compute Dx */
	const unsigned char* rxmul; /* R multiplier to compute Dx */
	const unsigned char* pymul; /* P multiplier to compute Dy */
	const unsigned char* qymul; /* Q multiplier to compute Dy */
	const unsigned char* rymul; /* R multiplier to compute Dy */
	const unsigned char* pzmul; /* P multiplier to compute Dz */
	const unsigned char* qzmul; /* Q multiplier to compute Dz */
	const unsigned char* rzmul; /* R multiplier to compute Dz */
	unsigned char G[3*3];
	unsigned char V[3*3];
	unsigned n = 3;

	/* if it's RAIDTP uses the dedicated and faster function */
	if (i == 0 && j == 1 && k == 2) {
		raidTP_recov_3data(dptrs, diskmax, size, x, y, z, zero);
		return;
	}

	p = dptrs[diskmax+i];
	q = dptrs[diskmax+j];
	r = dptrs[diskmax+k];

	/* compute syndrome with zero for the missing data page */
	/* use the dead data page as temporary storage for delta p, q and r */
	dp = dptrs[x];
	dq = dptrs[y];
	dr = dptrs[z];
	dptrs[x] = zero;
	dptrs[y] = zero;
	dptrs[z] = zero;
	dptrs[diskmax+i] = dp;
	dptrs[diskmax+j] = dq;
	dptrs[diskmax+k] = dr;

	raid_gen(k + 1, dptrs, diskmax, size);

	/* restore pointers */
	dptrs[x] = dp;
	dptrs[y] = dq;
	dptrs[z] = dr;
	dptrs[diskmax+i] = p;
	dptrs[diskmax+j] = q;
	dptrs[diskmax+k] = r;

	/* setup the generator matrix */
	G[0*n+0] = pown(i,x); /* row 1 for P */
	G[0*n+1] = pown(i,y);
	G[0*n+2] = pown(i,z);
	G[1*n+0] = pown(j,x); /* row 2 for Q */
	G[1*n+1] = pown(j,y);
	G[1*n+2] = pown(j,z);
	G[2*n+0] = pown(k,x); /* row 3 for R */
	G[2*n+1] = pown(k,y);
	G[2*n+2] = pown(k,z);

	/* invert it to solve the system of linear equations */
	invert(G, V, n);

	/* select tables */
	pxmul = table( V[0*n+0] );
	qxmul = table( V[0*n+1] );
	rxmul = table( V[0*n+2] );
	pymul = table( V[1*n+0] );
	qymul = table( V[1*n+1] );
	rymul = table( V[1*n+2] );
	pzmul = table( V[2*n+0] );
	qzmul = table( V[2*n+1] );
	rzmul = table( V[2*n+2] );

	while (size--) {
		/* delta */
		unsigned char Pd = *p ^ *dp;
		unsigned char Qd = *q ^ *dq;
		unsigned char Rd = *r ^ *dr;

		/* addends to reconstruct Dz */
		unsigned char pzm = pzmul[Pd];
		unsigned char qzm = qzmul[Qd];
		unsigned char rzm = rzmul[Rd];

		/* reconstruct Dz */
		unsigned char Dz = pzm ^ qzm ^ rzm;

		/* addends to reconstruct Dy */
		unsigned char pym = pymul[Pd];
		unsigned char qym = qymul[Qd];
		unsigned char rym = rymul[Rd];

		/* reconstruct Dy */
		unsigned char Dy = pym ^ qym ^ rym;

		/* reconstruct Dx */
		unsigned char Dx;

		/* if i is P, take the fast way */
		if (i == 0) {
			Dx = Pd ^ Dy ^ Dz;
		} else {
			/* addends to reconstruct Dx */
			unsigned char pxm = pxmul[Pd];
			unsigned char qxm = qxmul[Qd];
			unsigned char rxm = rxmul[Rd];

			/* reconstruct Dx */
			Dx = pxm ^ qxm ^ rxm;
		}

		/* set */
		*dp = Dx;
		*dq = Dy;
		*dr = Dz;

		++p;
		++dp;
		++q;
		++dq;
		++r;
		++dr;
	}
}

/**
 * Recover failure of four data blocks x,y,z,v using parity i,j,k,l for any RAID level.
 *
 * Starting from the equations:
 *
 * Pd = (2^i)^x * Dx + (2^i)^y * Dy + (2^i)^z * Dz + (2^i)^v * Dv
 * Qd = (2^j)^x * Dx + (2^j)^y * Dy + (2^j)^z * Dz + (2^j)^v * Dv
 * Rd = (2^k)^x * Dx + (2^k)^y * Dy + (2^k)^z * Dz + (2^k)^v * Dv
 * Sd = (2^l)^x * Dx + (2^l)^y * Dy + (2^l)^z * Dz + (2^l)^v * Dv
 *
 * we solve inverting the coefficients matrix.
 */
void raid_recov_4data(unsigned char** dptrs, unsigned diskmax, unsigned size, int x, int y, int z, int v, int i, int j, int k, int l, unsigned char* zero)
{
	unsigned char* p;
	unsigned char* dp;
	unsigned char* q;
	unsigned char* dq;
	unsigned char* r;
	unsigned char* dr;
	unsigned char* s;
	unsigned char* ds;
	const unsigned char* pxmul; /* P multiplier to compute Dx */
	const unsigned char* qxmul; /* Q multiplier to compute Dx */
	const unsigned char* rxmul; /* R multiplier to compute Dx */
	const unsigned char* sxmul; /* S multiplier to compute Dx */
	const unsigned char* pymul; /* P multiplier to compute Dy */
	const unsigned char* qymul; /* Q multiplier to compute Dy */
	const unsigned char* rymul; /* R multiplier to compute Dy */
	const unsigned char* symul; /* S multiplier to compute Dy */
	const unsigned char* pzmul; /* P multiplier to compute Dz */
	const unsigned char* qzmul; /* Q multiplier to compute Dz */
	const unsigned char* rzmul; /* R multiplier to compute Dz */
	const unsigned char* szmul; /* S multiplier to compute Dz */
	const unsigned char* pvmul; /* P multiplier to compute Dv */
	const unsigned char* qvmul; /* Q multiplier to compute Dv */
	const unsigned char* rvmul; /* R multiplier to compute Dv */
	const unsigned char* svmul; /* S multiplier to compute Dv */
	unsigned char G[4*4];
	unsigned char V[4*4];
	unsigned n = 4;

	p = dptrs[diskmax+i];
	q = dptrs[diskmax+j];
	r = dptrs[diskmax+k];
	s = dptrs[diskmax+l];

	/* compute syndrome with zero for the missing data page */
	/* use the dead data page as temporary storage for delta p, q, r and s */
	dp = dptrs[x];
	dq = dptrs[y];
	dr = dptrs[z];
	ds = dptrs[v];
	dptrs[x] = zero;
	dptrs[y] = zero;
	dptrs[z] = zero;
	dptrs[v] = zero;
	dptrs[diskmax+i] = dp;
	dptrs[diskmax+j] = dq;
	dptrs[diskmax+k] = dr;
	dptrs[diskmax+l] = ds;

	raid_gen(l + 1, dptrs, diskmax, size);

	/* restore pointers */
	dptrs[x] = dp;
	dptrs[y] = dq;
	dptrs[z] = dr;
	dptrs[v] = ds;
	dptrs[diskmax+i] = p;
	dptrs[diskmax+j] = q;
	dptrs[diskmax+k] = r;
	dptrs[diskmax+l] = s;

	/* setup the generator matrix */
	G[0*n+0] = pown(i,x); /* row 1 for P */
	G[0*n+1] = pown(i,y);
	G[0*n+2] = pown(i,z);
	G[0*n+3] = pown(i,v);
	G[1*n+0] = pown(j,x); /* row 2 for Q */
	G[1*n+1] = pown(j,y);
	G[1*n+2] = pown(j,z);
	G[1*n+3] = pown(j,v);
	G[2*n+0] = pown(k,x); /* row 3 for R */
	G[2*n+1] = pown(k,y);
	G[2*n+2] = pown(k,z);
	G[2*n+3] = pown(k,v);
	G[3*n+0] = pown(l,x); /* row 4 for S */
	G[3*n+1] = pown(l,y);
	G[3*n+2] = pown(l,z);
	G[3*n+3] = pown(l,v);

	/* invert it to solve the system of linear equations */
	invert(G, V, n);

	/* select tables */
	pxmul = table( V[0*n+0] );
	qxmul = table( V[0*n+1] );
	rxmul = table( V[0*n+2] );
	sxmul = table( V[0*n+3] );
	pymul = table( V[1*n+0] );
	qymul = table( V[1*n+1] );
	rymul = table( V[1*n+2] );
	symul = table( V[1*n+3] );
	pzmul = table( V[2*n+0] );
	qzmul = table( V[2*n+1] );
	rzmul = table( V[2*n+2] );
	szmul = table( V[2*n+3] );
	pvmul = table( V[3*n+0] );
	qvmul = table( V[3*n+1] );
	rvmul = table( V[3*n+2] );
	svmul = table( V[3*n+3] );

	while (size--) {
		/* delta */
		unsigned char Pd = *p ^ *dp;
		unsigned char Qd = *q ^ *dq;
		unsigned char Rd = *r ^ *dr;
		unsigned char Sd = *s ^ *ds;

		/* addends to reconstruct Dv */
		unsigned char pvm = pvmul[Pd];
		unsigned char qvm = qvmul[Qd];
		unsigned char rvm = rvmul[Rd];
		unsigned char svm = svmul[Sd];

		/* reconstruct Dv */
		unsigned char Dv = pvm ^ qvm ^ rvm ^ svm;

		/* addends to reconstruct Dz */
		unsigned char pzm = pzmul[Pd];
		unsigned char qzm = qzmul[Qd];
		unsigned char rzm = rzmul[Rd];
		unsigned char szm = szmul[Sd];

		/* reconstruct Dz */
		unsigned char Dz = pzm ^ qzm ^ rzm ^ szm;

		/* addends to reconstruct Dy */
		unsigned char pym = pymul[Pd];
		unsigned char qym = qymul[Qd];
		unsigned char rym = rymul[Rd];
		unsigned char sym = symul[Sd];

		/* reconstruct Dy */
		unsigned char Dy = pym ^ qym ^ rym ^ sym;

		/* reconstruct Dx */
		unsigned char Dx;

		/* if i is P, take the fast way */
		if (i == 0) {
			Dx = Pd ^ Dy ^ Dz ^ Dv;
		} else {
			/* addends to reconstruct Dx */
			unsigned char pxm = pxmul[Pd];
			unsigned char qxm = qxmul[Qd];
			unsigned char rxm = rxmul[Rd];
			unsigned char sxm = sxmul[Sd];

			/* reconstruct Dx */
			Dx = pxm ^ qxm ^ rxm ^ sxm;
		}

		/* set */
		*dp = Dx;
		*dq = Dy;
		*dr = Dz;
		*ds = Dv;

		++p;
		++dp;
		++q;
		++dq;
		++r;
		++dr;
		++s;
		++ds;
	}
}

/****************************************************************************/
/* init/done */

void raid_init(void)
{
	raid5_gen = raid5_int32r2;
	raid6_gen = raid6_int32r2;
	raidTP_gen = raidTP_int32r2;
	raidQP_gen = raidQP_int32r2;
#if defined(__i386__) || defined(__x86_64__)
	if (cpu_has_mmx()) {
		raid5_gen = raid5_mmxr4;
		raid6_gen = raid6_mmxr2;
		raidTP_gen = raidTP_mmxr1;
		raidQP_gen = raidQP_mmxr1;
	}
	if (cpu_has_sse2()) {
#if defined(__x86_64__)
		raid5_gen = raid5_sse2r8;
		raid6_gen = raid6_sse2r4;
		raidTP_gen = raidTP_sse2r2;
		raidQP_gen = raidQP_sse2r2;
#else
		raid5_gen = raid5_sse2r4;
		raid6_gen = raid6_sse2r2;
		raidTP_gen = raidTP_sse2r1;
		raidQP_gen = raidQP_sse2r1;
#endif
	}
#endif
}

