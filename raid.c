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

/*
 * The RAID5/RAID6 support was originally derived by the libraid6 library
 * by H. Peter Anvin released with license "GPL2 or any later version"
 * used by the Linux Kernel 2.6.38.
 *
 * This support was later completely rewritten (many times), but the
 * H. Peter Anvin's Copyright may still apply.
 *
 * The RAIDTP/RAIDQP support is original work implemented from scratch.
 * For some notes about the method used, see the "recovering" section
 * in this file.
 */

#include "portable.h"

#include "raid.h"
#include "cpu.h"
#include "tables.h"

/****************************************************************************/
/* specialized parity generation */

/**
 * Dereference as uint32_t
 */
#define v_32(p) (*(uint32_t*)&(p))

/**
 * Dereference as uint64_t
 */
#define v_64(p) (*(uint64_t*)&(p))

/*
 * RAID5 32bit C implementation
 */
void raid5_int32(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	int d, l;
	unsigned o;

	uint32_t p0;
	uint32_t p1;

	l = data - 1;
	p = vbuf[data];

	for(o=0;o<size;o+=8) {
		p0 = v_32(vbuf[l][o]);
		p1 = v_32(vbuf[l][o+4]);
		for(d=l-1;d>=0;--d) {
			p0 ^= v_32(vbuf[d][o]);
			p1 ^= v_32(vbuf[d][o+4]);
		}
		v_32(p[o]) = p0;
		v_32(p[o+4]) = p1;
	}
}

/*
 * RAID5 64bit C implementation
 */
void raid5_int64(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	int d, l;
	unsigned o;

	uint64_t p0;
	uint64_t p1;

	l = data - 1;
	p = vbuf[data];

	for(o=0;o<size;o+=16) {
		p0 = v_64(vbuf[l][o]);
		p1 = v_64(vbuf[l][o+8]);
		for(d=l-1;d>=0;--d) {
			p0 ^= v_64(vbuf[d][o]);
			p1 ^= v_64(vbuf[d][o+8]);
		}
		v_64(p[o]) = p0;
		v_64(p[o+8]) = p1;
	}
}

#if defined(__i386__) || defined(__x86_64__)
/*
 * RAID5 MMX implementation
 */
void raid5_mmx(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	int d, l;
	unsigned o;

	l = data - 1;
	p = vbuf[data];

	for(o=0;o<size;o+=32) {
		asm volatile("movq %0,%%mm0" : : "m" (vbuf[l][o]));
		asm volatile("movq %0,%%mm1" : : "m" (vbuf[l][o+8]));
		asm volatile("movq %0,%%mm2" : : "m" (vbuf[l][o+16]));
		asm volatile("movq %0,%%mm3" : : "m" (vbuf[l][o+24]));
		for(d=l-1;d>=0;--d) {
			asm volatile("movq %0,%%mm4" : : "m" (vbuf[d][o]));
			asm volatile("movq %0,%%mm5" : : "m" (vbuf[d][o+8]));
			asm volatile("movq %0,%%mm6" : : "m" (vbuf[d][o+16]));
			asm volatile("movq %0,%%mm7" : : "m" (vbuf[d][o+24]));
			asm volatile("pxor %mm4,%mm0");
			asm volatile("pxor %mm5,%mm1");
			asm volatile("pxor %mm6,%mm2");
			asm volatile("pxor %mm7,%mm3");
		}
		asm volatile("movq %%mm0,%0" : "=m" (p[o]));
		asm volatile("movq %%mm1,%0" : "=m" (p[o+8]));
		asm volatile("movq %%mm2,%0" : "=m" (p[o+16]));
		asm volatile("movq %%mm3,%0" : "=m" (p[o+24]));
	}

	asm volatile("emms" : : : "memory");
}

/*
 * RAID5 SSE2 implementation
 *
 * Note that we don't have the corresponding x64 sse2ext function using more
 * registers because processing a block of 64 bytes already fills
 * the typical cache block, and processing 128 bytes doesn't increase
 * performance.
 */
void raid5_sse2(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	int d, l;
	unsigned o;

	l = data - 1;
	p = vbuf[data];

	for(o=0;o<size;o+=64) {
		asm volatile("movdqa %0,%%xmm0" : : "m" (vbuf[l][o]));
		asm volatile("movdqa %0,%%xmm1" : : "m" (vbuf[l][o+16]));
		asm volatile("movdqa %0,%%xmm2" : : "m" (vbuf[l][o+32]));
		asm volatile("movdqa %0,%%xmm3" : : "m" (vbuf[l][o+48]));
		for(d=l-1;d>=0;--d) {
			asm volatile("movdqa %0,%%xmm4" : : "m" (vbuf[d][o]));
			asm volatile("movdqa %0,%%xmm5" : : "m" (vbuf[d][o+16]));
			asm volatile("movdqa %0,%%xmm6" : : "m" (vbuf[d][o+32]));
			asm volatile("movdqa %0,%%xmm7" : : "m" (vbuf[d][o+48]));
			asm volatile("pxor %xmm4,%xmm0");
			asm volatile("pxor %xmm5,%xmm1");
			asm volatile("pxor %xmm6,%xmm2");
			asm volatile("pxor %xmm7,%xmm3");
		}
		asm volatile("movntdq %%xmm0,%0" : "=m" (p[o]));
		asm volatile("movntdq %%xmm1,%0" : "=m" (p[o+16]));
		asm volatile("movntdq %%xmm2,%0" : "=m" (p[o+32]));
		asm volatile("movntdq %%xmm3,%0" : "=m" (p[o+48]));
	}

	asm volatile("sfence" : : : "memory");
}
#endif

/**
 * Multiply each byte of a uint32 by 2 in the GF(2^8).
 */
__attribute__((always_inline)) static inline uint32_t x2_32(uint32_t v)
{
	uint32_t mask = v & 0x80808080U;
	mask = (mask << 1) - (mask >> 7);
	v = (v << 1) & 0xfefefefeU;
	v ^= mask & 0x1d1d1d1dU;
	return v;
}

/**
 * Multiply each byte of a uint64 by 2 in the GF(2^8).
 */
__attribute__((always_inline)) static inline uint64_t x2_64(uint64_t v)
{
	uint64_t mask = v & 0x8080808080808080ULL;
	mask = (mask << 1) - (mask >> 7);
	v = (v << 1) & 0xfefefefefefefefeULL;
	v ^= mask & 0x1d1d1d1d1d1d1d1dULL;
	return v;
}

/*
 * RAID6 32bit C implementation
 */
void raid6_int32(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	int d, l;
	unsigned o;

	uint32_t d0, q0, p0;
	uint32_t d1, q1, p1;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];

	for(o=0;o<size;o+=8) {
		q0 = p0 = v_32(vbuf[l][o]);
		q1 = p1 = v_32(vbuf[l][o+4]);
		for(d=l-1;d>=0;--d) {
			d0 = v_32(vbuf[d][o]);
			d1 = v_32(vbuf[d][o+4]);

			p0 ^= d0;
			p1 ^= d1;

			q0 = x2_32(q0);
			q1 = x2_32(q1);

			q0 ^= d0;
			q1 ^= d1;
		}
		v_32(p[o]) = p0;
		v_32(p[o+4]) = p1;
		v_32(q[o]) = q0;
		v_32(q[o+4]) = q1;
	}
}

/*
 * RAID6 64bit C implementation
 */
void raid6_int64(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	int d, l;
	unsigned o;

	uint64_t d0, q0, p0;
	uint64_t d1, q1, p1;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];

	for(o=0;o<size;o+=16) {
		q0 = p0 = v_64(vbuf[l][o]);
		q1 = p1 = v_64(vbuf[l][o+8]);
		for(d=l-1;d>=0;--d) {
			d0 = v_64(vbuf[d][o]);
			d1 = v_64(vbuf[d][o+8]);

			p0 ^= d0;
			p1 ^= d1;

			q0 = x2_64(q0);
			q1 = x2_64(q1);

			q0 ^= d0;
			q1 ^= d1;
		}
		v_64(p[o]) = p0;
		v_64(p[o+8]) = p1;
		v_64(q[o]) = q0;
		v_64(q[o+8]) = q1;
	}
}

#if defined(__i386__) || defined(__x86_64__)
static const struct raid_8_const {
	unsigned char poly[8];
} raid_8_const __attribute__((aligned(8))) = {
	GFPOLY8
};

/*
 * RAID6 MMX implementation
 */
void raid6_mmx(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	int d, l;
	unsigned o;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];

	asm volatile("movq %0,%%mm7" : : "m" (raid_8_const.poly[0]));

	for(o=0;o<size;o+=16) {
		asm volatile("movq %0,%%mm0" : : "m" (vbuf[l][o]));
		asm volatile("movq %0,%%mm1" : : "m" (vbuf[l][o+8]));
		asm volatile("movq %mm0,%mm2");
		asm volatile("movq %mm1,%mm3");
		for(d=l-1;d>=0;--d) {
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

			asm volatile("movq %0,%%mm4" : : "m" (vbuf[d][o]));
			asm volatile("movq %0,%%mm5" : : "m" (vbuf[d][o+8]));
			asm volatile("pxor %mm4,%mm0");
			asm volatile("pxor %mm5,%mm1");
			asm volatile("pxor %mm4,%mm2");
			asm volatile("pxor %mm5,%mm3");
		}
		asm volatile("movq %%mm0,%0" : "=m" (p[o]));
		asm volatile("movq %%mm1,%0" : "=m" (p[o+8]));
		asm volatile("movq %%mm2,%0" : "=m" (q[o]));
		asm volatile("movq %%mm3,%0" : "=m" (q[o+8]));
	}

	asm volatile("emms" : : : "memory");
}

static const struct raid_16_const {
	unsigned char poly[16];
	unsigned char low_nibble[16];
	unsigned char x8l[16];
	unsigned char x8h[16];
	unsigned char x4l[16];
	unsigned char x4h[16];
} raid_16_const  __attribute__((aligned(16))) = {
	GFPOLY16,
	GFMASK16,
	GFX8L,
	GFX8H,
	GFX4L,
	GFX4H,
};

/*
 * RAID6 SSE2 implementation
 */
void raid6_sse2(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	int d, l;
	unsigned o;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];

	asm volatile("movdqa %0,%%xmm7" : : "m" (raid_16_const.poly[0]));

	for(o=0;o<size;o+=32) {
		asm volatile("movdqa %0,%%xmm0" : : "m" (vbuf[l][o]));
		asm volatile("movdqa %0,%%xmm1" : : "m" (vbuf[l][o+16]));
		asm volatile("movdqa %xmm0,%xmm2");
		asm volatile("movdqa %xmm1,%xmm3");
		for(d=l-1;d>=0;--d) {
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

			asm volatile("movdqa %0,%%xmm4" : : "m" (vbuf[d][o]));
			asm volatile("movdqa %0,%%xmm5" : : "m" (vbuf[d][o+16]));
			asm volatile("pxor %xmm4,%xmm0");
			asm volatile("pxor %xmm5,%xmm1");
			asm volatile("pxor %xmm4,%xmm2");
			asm volatile("pxor %xmm5,%xmm3");
		}
		asm volatile("movntdq %%xmm0,%0" : "=m" (p[o]));
		asm volatile("movntdq %%xmm1,%0" : "=m" (p[o+16]));
		asm volatile("movntdq %%xmm2,%0" : "=m" (q[o]));
		asm volatile("movntdq %%xmm3,%0" : "=m" (q[o+16]));
	}

	asm volatile("sfence" : : : "memory");
}
#endif

#if defined(__x86_64__)
/*
 * RAID6 SSE2 implementation
 * Note that it uses 16 registers, meaning that x64 is required.
 */
void raid6_sse2ext(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	int d, l;
	unsigned o;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];

	asm volatile("movdqa %0,%%xmm15" : : "m" (raid_16_const.poly[0]));

	for(o=0;o<size;o+=64) {
		asm volatile("movdqa %0,%%xmm0" : : "m" (vbuf[l][o]));
		asm volatile("movdqa %0,%%xmm1" : : "m" (vbuf[l][o+16]));
		asm volatile("movdqa %0,%%xmm2" : : "m" (vbuf[l][o+32]));
		asm volatile("movdqa %0,%%xmm3" : : "m" (vbuf[l][o+48]));
		asm volatile("movdqa %xmm0,%xmm4");
		asm volatile("movdqa %xmm1,%xmm5");
		asm volatile("movdqa %xmm2,%xmm6");
		asm volatile("movdqa %xmm3,%xmm7");
		for(d=l-1;d>=0;--d) {
			asm volatile("pxor %xmm8,%xmm8");
			asm volatile("pxor %xmm9,%xmm9");
			asm volatile("pxor %xmm10,%xmm10");
			asm volatile("pxor %xmm11,%xmm11");
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

			asm volatile("movdqa %0,%%xmm8" : : "m" (vbuf[d][o]));
			asm volatile("movdqa %0,%%xmm9" : : "m" (vbuf[d][o+16]));
			asm volatile("movdqa %0,%%xmm10" : : "m" (vbuf[d][o+32]));
			asm volatile("movdqa %0,%%xmm11" : : "m" (vbuf[d][o+48]));
			asm volatile("pxor %xmm8,%xmm0");
			asm volatile("pxor %xmm9,%xmm1");
			asm volatile("pxor %xmm10,%xmm2");
			asm volatile("pxor %xmm11,%xmm3");
			asm volatile("pxor %xmm8,%xmm4");
			asm volatile("pxor %xmm9,%xmm5");
			asm volatile("pxor %xmm10,%xmm6");
			asm volatile("pxor %xmm11,%xmm7");
		}
		asm volatile("movntdq %%xmm0,%0" : "=m" (p[o]));
		asm volatile("movntdq %%xmm1,%0" : "=m" (p[o+16]));
		asm volatile("movntdq %%xmm2,%0" : "=m" (p[o+32]));
		asm volatile("movntdq %%xmm3,%0" : "=m" (p[o+48]));
		asm volatile("movntdq %%xmm4,%0" : "=m" (q[o]));
		asm volatile("movntdq %%xmm5,%0" : "=m" (q[o+16]));
		asm volatile("movntdq %%xmm6,%0" : "=m" (q[o+32]));
		asm volatile("movntdq %%xmm7,%0" : "=m" (q[o+48]));
	}

	asm volatile("sfence" : : : "memory");
}
#endif

/*
 * RAIDTP 32bit C implementation
 */
void raidTP_int32(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	int d, l;
	unsigned o;

	uint32_t d0, r0, q0, p0;
	uint32_t d1, r1, q1, p1;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];
	r = vbuf[data+2];

	for(o=0;o<size;o+=8) {
		r0 = q0 = p0 = v_32(vbuf[l][o]);
		r1 = q1 = p1 = v_32(vbuf[l][o+4]);
		for(d=l-1;d>=0;--d) {
			d0 = v_32(vbuf[d][o]);
			d1 = v_32(vbuf[d][o+4]);

			p0 ^= d0;
			p1 ^= d1;

			q0 = x2_32(q0);
			q1 = x2_32(q1);

			q0 ^= d0;
			q1 ^= d1;

			r0 = x2_32(r0);
			r1 = x2_32(r1);
			r0 = x2_32(r0);
			r1 = x2_32(r1);

			r0 ^= d0;
			r1 ^= d1;
		}
		v_32(p[o]) = p0;
		v_32(p[o+4]) = p1;
		v_32(q[o]) = q0;
		v_32(q[o+4]) = q1;
		v_32(r[o]) = r0;
		v_32(r[o+4]) = r1;
	}
}

/*
 * RAIDTP 64bit C implementation
 */
void raidTP_int64(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	int d, l;
	unsigned o;

	uint64_t d0, r0, q0, p0;
	uint64_t d1, r1, q1, p1;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];
	r = vbuf[data+2];

	for(o=0;o<size;o+=16) {
		r0 = q0 = p0 = v_64(vbuf[l][o]);
		r1 = q1 = p1 = v_64(vbuf[l][o+8]);
		for(d=l-1;d>=0;--d) {
			d0 = v_64(vbuf[d][o]);
			d1 = v_64(vbuf[d][o+8]);

			p0 ^= d0;
			p1 ^= d1;

			q0 = x2_64(q0);
			q1 = x2_64(q1);

			q0 ^= d0;
			q1 ^= d1;

			r0 = x2_64(r0);
			r1 = x2_64(r1);
			r0 = x2_64(r0);
			r1 = x2_64(r1);

			r0 ^= d0;
			r1 ^= d1;
		}
		v_64(p[o]) = p0;
		v_64(p[o+8]) = p1;
		v_64(q[o]) = q0;
		v_64(q[o+8]) = q1;
		v_64(r[o]) = r0;
		v_64(r[o+8]) = r1;
	}
}

#if defined(__i386__) || defined(__x86_64__)
/*
 * RAIDTP MMX implementation
 */
void raidTP_mmx(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	int d, l;
	unsigned o;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];
	r = vbuf[data+2];

	asm volatile("movq %0,%%mm7" : : "m" (raid_8_const.poly[0]));

	for(o=0;o<size;o+=8) {
		asm volatile("movq %0,%%mm0" : : "m" (vbuf[l][o]));
		asm volatile("movq %mm0,%mm1");
		asm volatile("movq %mm0,%mm2");
		for(d=l-1;d>=0;--d) {
			asm volatile("pxor %mm4,%mm4");
			asm volatile("pxor %mm5,%mm5");
			asm volatile("pxor %mm6,%mm6");
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

			asm volatile("movq %0,%%mm3" : : "m" (vbuf[d][o]));
			asm volatile("pxor %mm3,%mm0");
			asm volatile("pxor %mm3,%mm1");
			asm volatile("pxor %mm3,%mm2");
		}
		asm volatile("movq %%mm0,%0" : "=m" (p[o]));
		asm volatile("movq %%mm1,%0" : "=m" (q[o]));
		asm volatile("movq %%mm2,%0" : "=m" (r[o]));
	}

	asm volatile("emms" : : : "memory");
}
#endif

#if defined(__i386__) || defined(__x86_64__)
/*
 * RAIDTP SSE2 implementation
 */
void raidTP_sse2(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	int d, l;
	unsigned o;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];
	r = vbuf[data+2];

	asm volatile("movdqa %0,%%xmm7" : : "m" (raid_16_const.poly[0]));

	for(o=0;o<size;o+=16) {
		asm volatile("movdqa %0,%%xmm0" : : "m" (vbuf[l][o]));
		asm volatile("movdqa %xmm0,%xmm1");
		asm volatile("movdqa %xmm0,%xmm2");
		for(d=l-1;d>=0;--d) {
			asm volatile("pxor %xmm4,%xmm4");
			asm volatile("pxor %xmm5,%xmm5");
			asm volatile("pxor %xmm6,%xmm6");
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

			asm volatile("movdqa %0,%%xmm3" : : "m" (vbuf[d][o]));
			asm volatile("pxor %xmm3,%xmm0");
			asm volatile("pxor %xmm3,%xmm1");
			asm volatile("pxor %xmm3,%xmm2");
		}
		asm volatile("movntdq %%xmm0,%0" : "=m" (p[o]));
		asm volatile("movntdq %%xmm1,%0" : "=m" (q[o]));
		asm volatile("movntdq %%xmm2,%0" : "=m" (r[o]));
	}

	asm volatile("sfence" : : : "memory");
}
#endif

#if defined(__i386__) || defined(__x86_64__)
/*
 * RAIDTP SSSE3 implementation
 */
void raidTP_ssse3(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	int d, l;
	unsigned o;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];
	r = vbuf[data+2];

	asm volatile("movdqa %0,%%xmm3" : : "m" (raid_16_const.low_nibble[0]));
	asm volatile("movdqa %0,%%xmm5" : : "m" (raid_16_const.x4l[0]));
	asm volatile("movdqa %0,%%xmm7" : : "m" (raid_16_const.poly[0]));

	for(o=0;o<size;o+=16) {
		asm volatile("movdqa %0,%%xmm0" : : "m" (vbuf[l][o]));
		asm volatile("movdqa %xmm0,%xmm1");
		asm volatile("movdqa %xmm0,%xmm2");
		for(d=l-1;d>=0;--d) {
			asm volatile("pxor %xmm4,%xmm4");
			asm volatile("pcmpgtb %xmm1,%xmm4");
			asm volatile("paddb %xmm1,%xmm1");
			asm volatile("pand %xmm7,%xmm4");
			asm volatile("pxor %xmm4,%xmm1");

			asm volatile("movdqa %0,%%xmm6" : : "m" (raid_16_const.x4h[0]));
			asm volatile("movdqa %xmm2,%xmm4");
			asm volatile("psraw  $4,%xmm2");
			asm volatile("pand   %xmm3,%xmm4");
			asm volatile("pand   %xmm3,%xmm2");
			asm volatile("pshufb %xmm2,%xmm6");
			asm volatile("movdqa %xmm5,%xmm2");
			asm volatile("pshufb %xmm4,%xmm2");
			asm volatile("pxor   %xmm6,%xmm2");

			asm volatile("movdqa %0,%%xmm4" : : "m" (vbuf[d][o]));
			asm volatile("pxor %xmm4,%xmm0");
			asm volatile("pxor %xmm4,%xmm1");
			asm volatile("pxor %xmm4,%xmm2");
		}
		asm volatile("movntdq %%xmm0,%0" : "=m" (p[o]));
		asm volatile("movntdq %%xmm1,%0" : "=m" (q[o]));
		asm volatile("movntdq %%xmm2,%0" : "=m" (r[o]));
	}

	asm volatile("sfence" : : : "memory");
}
#endif

#if defined(__x86_64__)
/*
 * RAIDTP SSE2 implementation
 * Note that it uses 16 registers, meaning that x64 is required.
 */
void raidTP_sse2ext(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	int d, l;
	unsigned o;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];
	r = vbuf[data+2];

	asm volatile("movdqa %0,%%xmm7" : : "m" (raid_16_const.poly[0]));

	for(o=0;o<size;o+=32) {
		asm volatile("movdqa %0,%%xmm0" : : "m" (vbuf[l][o]));
		asm volatile("movdqa %0,%%xmm8" : : "m" (vbuf[l][o+16]));
		asm volatile("movdqa %xmm0,%xmm1");
		asm volatile("movdqa %xmm8,%xmm9");
		asm volatile("movdqa %xmm0,%xmm2");
		asm volatile("movdqa %xmm8,%xmm10");
		for(d=l-1;d>=0;--d) {
			asm volatile("pxor %xmm4,%xmm4");
			asm volatile("pxor %xmm5,%xmm5");
			asm volatile("pxor %xmm6,%xmm6");
			asm volatile("pxor %xmm12,%xmm12");
			asm volatile("pxor %xmm13,%xmm13");
			asm volatile("pxor %xmm14,%xmm14");
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

			asm volatile("movdqa %0,%%xmm3" : : "m" (vbuf[d][o]));
			asm volatile("movdqa %0,%%xmm11" : : "m" (vbuf[d][o+16]));
			asm volatile("pxor %xmm3,%xmm0");
			asm volatile("pxor %xmm3,%xmm1");
			asm volatile("pxor %xmm3,%xmm2");
			asm volatile("pxor %xmm11,%xmm8");
			asm volatile("pxor %xmm11,%xmm9");
			asm volatile("pxor %xmm11,%xmm10");
		}
		asm volatile("movntdq %%xmm0,%0" : "=m" (p[o]));
		asm volatile("movntdq %%xmm8,%0" : "=m" (p[o+16]));
		asm volatile("movntdq %%xmm1,%0" : "=m" (q[o]));
		asm volatile("movntdq %%xmm9,%0" : "=m" (q[o+16]));
		asm volatile("movntdq %%xmm2,%0" : "=m" (r[o]));
		asm volatile("movntdq %%xmm10,%0" : "=m" (r[o+16]));
	}

	asm volatile("sfence" : : : "memory");
}
#endif

#if defined(__x86_64__)
/*
 * RAIDTP SSSE3 implementation
 * Note that it uses 16 registers, meaning that x64 is required.
 */
void raidTP_ssse3ext(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	int d, l;
	unsigned o;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];
	r = vbuf[data+2];

	asm volatile("movdqa %0,%%xmm3" : : "m" (raid_16_const.low_nibble[0]));
	asm volatile("movdqa %0,%%xmm5" : : "m" (raid_16_const.x4l[0]));
	asm volatile("movdqa %0,%%xmm6" : : "m" (raid_16_const.x4h[0]));
	asm volatile("movdqa %0,%%xmm7" : : "m" (raid_16_const.poly[0]));

	for(o=0;o<size;o+=32) {
		asm volatile("movdqa %0,%%xmm0" : : "m" (vbuf[l][o]));
		asm volatile("movdqa %0,%%xmm8" : : "m" (vbuf[l][o+16]));
		asm volatile("movdqa %xmm0,%xmm1");
		asm volatile("movdqa %xmm8,%xmm9");
		asm volatile("movdqa %xmm0,%xmm2");
		asm volatile("movdqa %xmm8,%xmm10");
		for(d=l-1;d>=0;--d) {
			asm volatile("pxor %xmm4,%xmm4");
			asm volatile("pxor %xmm12,%xmm12");
			asm volatile("pcmpgtb %xmm1,%xmm4");
			asm volatile("pcmpgtb %xmm9,%xmm12");
			asm volatile("paddb %xmm1,%xmm1");
			asm volatile("paddb %xmm9,%xmm9");
			asm volatile("pand %xmm7,%xmm4");
			asm volatile("pand %xmm7,%xmm12");
			asm volatile("pxor %xmm4,%xmm1");
			asm volatile("pxor %xmm12,%xmm9");

			asm volatile("movdqa %xmm6,%xmm13");
			asm volatile("movdqa %xmm6,%xmm14");
			asm volatile("movdqa %xmm2,%xmm4");
			asm volatile("movdqa %xmm10,%xmm12");
			asm volatile("psraw  $4,%xmm2");
			asm volatile("psraw  $4,%xmm10");
			asm volatile("pand   %xmm3,%xmm4");
			asm volatile("pand   %xmm3,%xmm12");
			asm volatile("pand   %xmm3,%xmm2");
			asm volatile("pand   %xmm3,%xmm10");
			asm volatile("pshufb %xmm2,%xmm13");
			asm volatile("pshufb %xmm10,%xmm14");
			asm volatile("movdqa %xmm5,%xmm2");
			asm volatile("movdqa %xmm5,%xmm10");
			asm volatile("pshufb %xmm4,%xmm2");
			asm volatile("pshufb %xmm12,%xmm10");
			asm volatile("pxor   %xmm13,%xmm2");
			asm volatile("pxor   %xmm14,%xmm10");

			asm volatile("movdqa %0,%%xmm4" : : "m" (vbuf[d][o]));
			asm volatile("movdqa %0,%%xmm12" : : "m" (vbuf[d][o+16]));
			asm volatile("pxor %xmm4,%xmm0");
			asm volatile("pxor %xmm4,%xmm1");
			asm volatile("pxor %xmm4,%xmm2");
			asm volatile("pxor %xmm12,%xmm8");
			asm volatile("pxor %xmm12,%xmm9");
			asm volatile("pxor %xmm12,%xmm10");
		}
		asm volatile("movntdq %%xmm0,%0" : "=m" (p[o]));
		asm volatile("movntdq %%xmm8,%0" : "=m" (p[o+16]));
		asm volatile("movntdq %%xmm1,%0" : "=m" (q[o]));
		asm volatile("movntdq %%xmm9,%0" : "=m" (q[o+16]));
		asm volatile("movntdq %%xmm2,%0" : "=m" (r[o]));
		asm volatile("movntdq %%xmm10,%0" : "=m" (r[o+16]));
	}

	asm volatile("sfence" : : : "memory");
}
#endif

/*
 * RAIDQP 32bit C implementation
 */
void raidQP_int32(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	unsigned char* s;
	int d, l;
	unsigned o;

	uint32_t d0, s0, r0, q0, p0;
	uint32_t d1, s1, r1, q1, p1;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];
	r = vbuf[data+2];
	s = vbuf[data+3];

	for(o=0;o<size;o+=8) {
		s0 = r0 = q0 = p0 = v_32(vbuf[l][o]);
		s1 = r1 = q1 = p1 = v_32(vbuf[l][o+4]);
		for(d=l-1;d>=0;--d) {
			d0 = v_32(vbuf[d][o]);
			d1 = v_32(vbuf[d][o+4]);

			p0 ^= d0;
			p1 ^= d1;

			q0 = x2_32(q0);
			q1 = x2_32(q1);

			q0 ^= d0;
			q1 ^= d1;

			r0 = x2_32(r0);
			r1 = x2_32(r1);
			r0 = x2_32(r0);
			r1 = x2_32(r1);

			r0 ^= d0;
			r1 ^= d1;

			s0 = x2_32(s0);
			s1 = x2_32(s1);
			s0 = x2_32(s0);
			s1 = x2_32(s1);
			s0 = x2_32(s0);
			s1 = x2_32(s1);

			s0 ^= d0;
			s1 ^= d1;
		}
		v_32(p[o]) = p0;
		v_32(p[o+4]) = p1;
		v_32(q[o]) = q0;
		v_32(q[o+4]) = q1;
		v_32(r[o]) = r0;
		v_32(r[o+4]) = r1;
		v_32(s[o]) = s0;
		v_32(s[o+4]) = s1;
	}
}

/*
 * RAIDQP 64bit C implementation
 */
void raidQP_int64(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	unsigned char* s;
	int d, l;
	unsigned o;

	uint64_t d0, s0, r0, q0, p0;
	uint64_t d1, s1, r1, q1, p1;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];
	r = vbuf[data+2];
	s = vbuf[data+3];

	for(o=0;o<size;o+=16) {
		s0 = r0 = q0 = p0 = v_64(vbuf[l][o]);
		s1 = r1 = q1 = p1 = v_64(vbuf[l][o+8]);
		for(d=l-1;d>=0;--d) {
			d0 = v_64(vbuf[d][o]);
			d1 = v_64(vbuf[d][o+8]);

			p0 ^= d0;
			p1 ^= d1;

			q0 = x2_64(q0);
			q1 = x2_64(q1);

			q0 ^= d0;
			q1 ^= d1;

			r0 = x2_64(r0);
			r1 = x2_64(r1);
			r0 = x2_64(r0);
			r1 = x2_64(r1);

			r0 ^= d0;
			r1 ^= d1;

			s0 = x2_64(s0);
			s1 = x2_64(s1);
			s0 = x2_64(s0);
			s1 = x2_64(s1);
			s0 = x2_64(s0);
			s1 = x2_64(s1);

			s0 ^= d0;
			s1 ^= d1;
		}
		v_64(p[o]) = p0;
		v_64(p[o+8]) = p1;
		v_64(q[o]) = q0;
		v_64(q[o+8]) = q1;
		v_64(r[o]) = r0;
		v_64(r[o+8]) = r1;
		v_64(s[o]) = s0;
		v_64(s[o+8]) = s1;
	}
}

#if defined(__i386__) || defined(__x86_64__)
/*
 * RAIDQP MMX implementation
 */
void raidQP_mmx(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	unsigned char* s;
	int d, l;
	unsigned o;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];
	r = vbuf[data+2];
	s = vbuf[data+3];

	asm volatile("movq %0,%%mm7" : : "m" (raid_8_const.poly[0]));

	for(o=0;o<size;o+=8) {
		asm volatile("movq %0,%%mm0" : : "m" (vbuf[l][o]));
		asm volatile("movq %mm0,%mm1");
		asm volatile("movq %mm0,%mm2");
		asm volatile("movq %mm0,%mm3");
		for(d=l-1;d>=0;--d) {
			asm volatile("pxor %mm4,%mm4");
			asm volatile("pxor %mm5,%mm5");
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

			asm volatile("movq %0,%%mm6" : : "m" (vbuf[d][o]));
			asm volatile("pxor %mm6,%mm0");
			asm volatile("pxor %mm6,%mm1");
			asm volatile("pxor %mm6,%mm2");
			asm volatile("pxor %mm6,%mm3");
		}
		asm volatile("movq %%mm0,%0" : "=m" (p[o]));
		asm volatile("movq %%mm1,%0" : "=m" (q[o]));
		asm volatile("movq %%mm2,%0" : "=m" (r[o]));
		asm volatile("movq %%mm3,%0" : "=m" (s[o]));
	}

	asm volatile("emms" : : : "memory");
}
#endif

#if defined(__i386__) || defined(__x86_64__)
/*
 * RAIDQP SSE2 implementation
 */
void raidQP_sse2(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	unsigned char* s;
	int d, l;
	unsigned o;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];
	r = vbuf[data+2];
	s = vbuf[data+3];

	asm volatile("movdqa %0,%%xmm7" : : "m" (raid_16_const.poly[0]));

	for(o=0;o<size;o+=16) {
		asm volatile("movdqa %0,%%xmm0" : : "m" (vbuf[l][o]));
		asm volatile("movdqa %xmm0,%xmm1");
		asm volatile("movdqa %xmm0,%xmm2");
		asm volatile("movdqa %xmm0,%xmm3");
		for(d=l-1;d>=0;--d) {
			asm volatile("pxor %xmm4,%xmm4");
			asm volatile("pxor %xmm5,%xmm5");
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

			asm volatile("movdqa %0,%%xmm4" : : "m" (vbuf[d][o]));
			asm volatile("pxor %xmm4,%xmm0");
			asm volatile("pxor %xmm4,%xmm1");
			asm volatile("pxor %xmm4,%xmm2");
			asm volatile("pxor %xmm4,%xmm3");
		}
		asm volatile("movntdq %%xmm0,%0" : "=m" (p[o]));
		asm volatile("movntdq %%xmm1,%0" : "=m" (q[o]));
		asm volatile("movntdq %%xmm2,%0" : "=m" (r[o]));
		asm volatile("movntdq %%xmm3,%0" : "=m" (s[o]));
	}

	asm volatile("sfence" : : : "memory");
}
#endif

#if defined(__i386__) || defined(__x86_64__)
/*
 * RAIDQP SSSE3 implementation
 */
void raidQP_ssse3(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	unsigned char* s;
	int d, l;
	unsigned o;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];
	r = vbuf[data+2];
	s = vbuf[data+3];

	asm volatile("movdqa %0,%%xmm7" : : "m" (raid_16_const.poly[0]));

	for(o=0;o<size;o+=16) {
		asm volatile("movdqa %0,%%xmm0" : : "m" (vbuf[l][o]));
		asm volatile("movdqa %xmm0,%xmm1");
		asm volatile("movdqa %xmm0,%xmm2");
		asm volatile("movdqa %xmm0,%xmm3");
		for(d=l-1;d>=0;--d) {
			asm volatile("pxor %xmm4,%xmm4");
			asm volatile("pxor %xmm5,%xmm5");
			asm volatile("pcmpgtb %xmm1,%xmm4");
			asm volatile("pcmpgtb %xmm2,%xmm5");
			asm volatile("paddb %xmm1,%xmm1");
			asm volatile("paddb %xmm2,%xmm2");
			asm volatile("pand %xmm7,%xmm4");
			asm volatile("pand %xmm7,%xmm5");
			asm volatile("pxor %xmm4,%xmm1");
			asm volatile("pxor %xmm5,%xmm2");

			asm volatile("pxor %xmm4,%xmm4");
			asm volatile("pcmpgtb %xmm2,%xmm4");
			asm volatile("paddb %xmm2,%xmm2");
			asm volatile("pand %xmm7,%xmm4");
			asm volatile("pxor %xmm4,%xmm2");

			asm volatile("movdqa %0,%%xmm5" : : "m" (raid_16_const.x8l[0]));
			asm volatile("movdqa %0,%%xmm6" : : "m" (raid_16_const.x8h[0]));
			asm volatile("movdqa %xmm3,%xmm4");
			asm volatile("psraw  $4,%xmm3");
			asm volatile("pand   %0,%%xmm4" : : "m" (raid_16_const.low_nibble[0]));
			asm volatile("pand   %0,%%xmm3" : : "m" (raid_16_const.low_nibble[0]));
			asm volatile("pshufb %xmm3,%xmm6");
			asm volatile("movdqa %xmm5,%xmm3");
			asm volatile("pshufb %xmm4,%xmm3");
			asm volatile("pxor   %xmm6,%xmm3");

			asm volatile("movdqa %0,%%xmm4" : : "m" (vbuf[d][o]));
			asm volatile("pxor %xmm4,%xmm0");
			asm volatile("pxor %xmm4,%xmm1");
			asm volatile("pxor %xmm4,%xmm2");
			asm volatile("pxor %xmm4,%xmm3");
		}
		asm volatile("movntdq %%xmm0,%0" : "=m" (p[o]));
		asm volatile("movntdq %%xmm1,%0" : "=m" (q[o]));
		asm volatile("movntdq %%xmm2,%0" : "=m" (r[o]));
		asm volatile("movntdq %%xmm3,%0" : "=m" (s[o]));
	}

	asm volatile("sfence" : : : "memory");
}
#endif

#if defined(__x86_64__)
/*
 * RAIDQP SSE2 implementation
 * Note that it uses 16 registers, meaning that x64 is required.
 */
void raidQP_sse2ext(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	unsigned char* s;
	int d, l;
	unsigned o;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];
	r = vbuf[data+2];
	s = vbuf[data+3];

	asm volatile("movdqa %0,%%xmm7" : : "m" (raid_16_const.poly[0]));

	for(o=0;o<size;o+=32) {
		asm volatile("movdqa %0,%%xmm0" : : "m" (vbuf[l][o]));
		asm volatile("movdqa %0,%%xmm8" : : "m" (vbuf[l][o+16]));
		asm volatile("movdqa %xmm0,%xmm1");
		asm volatile("movdqa %xmm8,%xmm9");
		asm volatile("movdqa %xmm0,%xmm2");
		asm volatile("movdqa %xmm8,%xmm10");
		asm volatile("movdqa %xmm0,%xmm3");
		asm volatile("movdqa %xmm8,%xmm11");
		for(d=l-1;d>=0;--d) {
			asm volatile("pxor %xmm4,%xmm4");
			asm volatile("pxor %xmm5,%xmm5");
			asm volatile("pxor %xmm12,%xmm12");
			asm volatile("pxor %xmm13,%xmm13");
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

			asm volatile("movdqa %0,%%xmm4" : : "m" (vbuf[d][o]));
			asm volatile("movdqa %0,%%xmm12" : : "m" (vbuf[d][o+16]));
			asm volatile("pxor %xmm4,%xmm0");
			asm volatile("pxor %xmm4,%xmm1");
			asm volatile("pxor %xmm4,%xmm2");
			asm volatile("pxor %xmm4,%xmm3");
			asm volatile("pxor %xmm12,%xmm8");
			asm volatile("pxor %xmm12,%xmm9");
			asm volatile("pxor %xmm12,%xmm10");
			asm volatile("pxor %xmm12,%xmm11");
		}
		asm volatile("movntdq %%xmm0,%0" : "=m" (p[o]));
		asm volatile("movntdq %%xmm8,%0" : "=m" (p[o+16]));
		asm volatile("movntdq %%xmm1,%0" : "=m" (q[o]));
		asm volatile("movntdq %%xmm9,%0" : "=m" (q[o+16]));
		asm volatile("movntdq %%xmm2,%0" : "=m" (r[o]));
		asm volatile("movntdq %%xmm10,%0" : "=m" (r[o+16]));
		asm volatile("movntdq %%xmm3,%0" : "=m" (s[o]));
		asm volatile("movntdq %%xmm11,%0" : "=m" (s[o+16]));
	}

	asm volatile("sfence" : : : "memory");
}
#endif

#if defined(__x86_64__)
/*
 * RAIDQP SSSE3 implementation
 * Note that it uses 16 registers, meaning that x64 is required.
 */
void raidQP_ssse3ext(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	unsigned char* s;
	int d, l;
	unsigned o;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];
	r = vbuf[data+2];
	s = vbuf[data+3];

	asm volatile("movdqa %0,%%xmm7" : : "m" (raid_16_const.poly[0]));
	asm volatile("movdqa %0,%%xmm15" : : "m" (raid_16_const.low_nibble[0]));

	for(o=0;o<size;o+=32) {
		asm volatile("movdqa %0,%%xmm0" : : "m" (vbuf[l][o]));
		asm volatile("movdqa %0,%%xmm8" : : "m" (vbuf[l][o+16]));
		asm volatile("movdqa %xmm0,%xmm1");
		asm volatile("movdqa %xmm8,%xmm9");
		asm volatile("movdqa %xmm0,%xmm2");
		asm volatile("movdqa %xmm8,%xmm10");
		asm volatile("movdqa %xmm0,%xmm3");
		asm volatile("movdqa %xmm8,%xmm11");
		for(d=l-1;d>=0;--d) {
			asm volatile("pxor %xmm4,%xmm4");
			asm volatile("pxor %xmm12,%xmm12");
			asm volatile("pcmpgtb %xmm1,%xmm4");
			asm volatile("pcmpgtb %xmm9,%xmm12");
			asm volatile("paddb %xmm1,%xmm1");
			asm volatile("paddb %xmm9,%xmm9");
			asm volatile("pand %xmm7,%xmm4");
			asm volatile("pand %xmm7,%xmm12");
			asm volatile("pxor %xmm4,%xmm1");
			asm volatile("pxor %xmm12,%xmm9");

			asm volatile("movdqa %0,%%xmm5" : : "m" (raid_16_const.x4l[0]));
			asm volatile("movdqa %0,%%xmm6" : : "m" (raid_16_const.x4h[0]));
			asm volatile("movdqa %xmm5,%xmm13");
			asm volatile("movdqa %xmm6,%xmm14");
			asm volatile("movdqa %xmm2,%xmm4");
			asm volatile("movdqa %xmm10,%xmm12");
			asm volatile("psraw  $4,%xmm2");
			asm volatile("psraw  $4,%xmm10");
			asm volatile("pand   %xmm15,%xmm4");
			asm volatile("pand   %xmm15,%xmm12");
			asm volatile("pand   %xmm15,%xmm2");
			asm volatile("pand   %xmm15,%xmm10");
			asm volatile("pshufb %xmm2,%xmm6");
			asm volatile("pshufb %xmm10,%xmm14");
			asm volatile("movdqa %xmm5,%xmm2");
			asm volatile("movdqa %xmm13,%xmm10");
			asm volatile("pshufb %xmm4,%xmm2");
			asm volatile("pshufb %xmm12,%xmm10");
			asm volatile("pxor   %xmm6,%xmm2");
			asm volatile("pxor   %xmm14,%xmm10");

			asm volatile("movdqa %0,%%xmm5" : : "m" (raid_16_const.x8l[0]));
			asm volatile("movdqa %0,%%xmm6" : : "m" (raid_16_const.x8h[0]));
			asm volatile("movdqa %xmm5,%xmm13");
			asm volatile("movdqa %xmm6,%xmm14");
			asm volatile("movdqa %xmm3,%xmm4");
			asm volatile("movdqa %xmm11,%xmm12");
			asm volatile("psraw  $4,%xmm3");
			asm volatile("psraw  $4,%xmm11");
			asm volatile("pand   %xmm15,%xmm4");
			asm volatile("pand   %xmm15,%xmm12");
			asm volatile("pand   %xmm15,%xmm3");
			asm volatile("pand   %xmm15,%xmm11");
			asm volatile("pshufb %xmm3,%xmm6");
			asm volatile("pshufb %xmm11,%xmm14");
			asm volatile("movdqa %xmm5,%xmm3");
			asm volatile("movdqa %xmm13,%xmm11");
			asm volatile("pshufb %xmm4,%xmm3");
			asm volatile("pshufb %xmm12,%xmm11");
			asm volatile("pxor   %xmm6,%xmm3");
			asm volatile("pxor   %xmm14,%xmm11");

			asm volatile("movdqa %0,%%xmm4" : : "m" (vbuf[d][o]));
			asm volatile("movdqa %0,%%xmm12" : : "m" (vbuf[d][o+16]));
			asm volatile("pxor %xmm4,%xmm0");
			asm volatile("pxor %xmm4,%xmm1");
			asm volatile("pxor %xmm4,%xmm2");
			asm volatile("pxor %xmm4,%xmm3");
			asm volatile("pxor %xmm12,%xmm8");
			asm volatile("pxor %xmm12,%xmm9");
			asm volatile("pxor %xmm12,%xmm10");
			asm volatile("pxor %xmm12,%xmm11");
		}
		asm volatile("movntdq %%xmm0,%0" : "=m" (p[o]));
		asm volatile("movntdq %%xmm8,%0" : "=m" (p[o+16]));
		asm volatile("movntdq %%xmm1,%0" : "=m" (q[o]));
		asm volatile("movntdq %%xmm9,%0" : "=m" (q[o+16]));
		asm volatile("movntdq %%xmm2,%0" : "=m" (r[o]));
		asm volatile("movntdq %%xmm10,%0" : "=m" (r[o+16]));
		asm volatile("movntdq %%xmm3,%0" : "=m" (s[o]));
		asm volatile("movntdq %%xmm11,%0" : "=m" (s[o+16]));
	}

	asm volatile("sfence" : : : "memory");
}

/*
 * RAIDQP AVX implementation
 * Note that it uses 16 registers, meaning that x64 is required.
 */
void raidQP_avxext(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	unsigned char* s;
	int d, l;
	unsigned o;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];
	r = vbuf[data+2];
	s = vbuf[data+3];

	asm volatile("movdqa %0,%%xmm5" : : "m" (raid_16_const.x8l[0]));
	asm volatile("movdqa %0,%%xmm6" : : "m" (raid_16_const.x8h[0]));
	asm volatile("movdqa %0,%%xmm7" : : "m" (raid_16_const.poly[0]));
	asm volatile("movdqa %0,%%xmm13" : : "m" (raid_16_const.x4l[0]));
	asm volatile("movdqa %0,%%xmm14" : : "m" (raid_16_const.x4h[0]));
	asm volatile("movdqa %0,%%xmm15" : : "m" (raid_16_const.low_nibble[0]));

	for(o=0;o<size;o+=32) {
		asm volatile("movdqa %0,%%xmm0" : : "m" (vbuf[l][o]));
		asm volatile("movdqa %0,%%xmm8" : : "m" (vbuf[l][o+16]));
		asm volatile("movdqa %xmm0,%xmm1");
		asm volatile("movdqa %xmm8,%xmm9");
		asm volatile("movdqa %xmm0,%xmm2");
		asm volatile("movdqa %xmm8,%xmm10");
		asm volatile("movdqa %xmm0,%xmm3");
		asm volatile("movdqa %xmm8,%xmm11");
		for(d=l-1;d>=0;--d) {
			asm volatile("pxor %xmm4,%xmm4");
			asm volatile("pxor %xmm12,%xmm12");
			asm volatile("pcmpgtb %xmm1,%xmm4");
			asm volatile("pcmpgtb %xmm9,%xmm12");
			asm volatile("paddb %xmm1,%xmm1");
			asm volatile("paddb %xmm9,%xmm9");
			asm volatile("pand %xmm7,%xmm4");
			asm volatile("pand %xmm7,%xmm12");
			asm volatile("pxor %xmm4,%xmm1");
			asm volatile("pxor %xmm12,%xmm9");

			asm volatile("movdqa %xmm2,%xmm4");
			asm volatile("movdqa %xmm10,%xmm12");
			asm volatile("psraw  $4,%xmm2");
			asm volatile("psraw  $4,%xmm10");
			asm volatile("pand   %xmm15,%xmm4");
			asm volatile("pand   %xmm15,%xmm12");
			asm volatile("pand   %xmm15,%xmm2");
			asm volatile("pand   %xmm15,%xmm10");
			asm volatile("vpshufb %xmm2,%xmm14,%xmm2");
			asm volatile("vpshufb %xmm10,%xmm14,%xmm10");
			asm volatile("vpshufb %xmm4,%xmm13,%xmm4");
			asm volatile("vpshufb %xmm12,%xmm13,%xmm12");
			asm volatile("pxor   %xmm4,%xmm2");
			asm volatile("pxor   %xmm12,%xmm10");

			asm volatile("movdqa %xmm3,%xmm4");
			asm volatile("movdqa %xmm11,%xmm12");
			asm volatile("psraw  $4,%xmm3");
			asm volatile("psraw  $4,%xmm11");
			asm volatile("pand   %xmm15,%xmm4");
			asm volatile("pand   %xmm15,%xmm12");
			asm volatile("pand   %xmm15,%xmm3");
			asm volatile("pand   %xmm15,%xmm11");
			asm volatile("vpshufb %xmm3,%xmm6,%xmm3");
			asm volatile("vpshufb %xmm11,%xmm6,%xmm11");
			asm volatile("vpshufb %xmm4,%xmm5,%xmm4");
			asm volatile("vpshufb %xmm12,%xmm5,%xmm12");
			asm volatile("pxor   %xmm4,%xmm3");
			asm volatile("pxor   %xmm12,%xmm11");

			asm volatile("movdqa %0,%%xmm4" : : "m" (vbuf[d][o]));
			asm volatile("movdqa %0,%%xmm12" : : "m" (vbuf[d][o+16]));
			asm volatile("pxor %xmm4,%xmm0");
			asm volatile("pxor %xmm4,%xmm1");
			asm volatile("pxor %xmm4,%xmm2");
			asm volatile("pxor %xmm4,%xmm3");
			asm volatile("pxor %xmm12,%xmm8");
			asm volatile("pxor %xmm12,%xmm9");
			asm volatile("pxor %xmm12,%xmm10");
			asm volatile("pxor %xmm12,%xmm11");
		}
		asm volatile("movntdq %%xmm0,%0" : "=m" (p[o]));
		asm volatile("movntdq %%xmm8,%0" : "=m" (p[o+16]));
		asm volatile("movntdq %%xmm1,%0" : "=m" (q[o]));
		asm volatile("movntdq %%xmm9,%0" : "=m" (q[o+16]));
		asm volatile("movntdq %%xmm2,%0" : "=m" (r[o]));
		asm volatile("movntdq %%xmm10,%0" : "=m" (r[o+16]));
		asm volatile("movntdq %%xmm3,%0" : "=m" (s[o]));
		asm volatile("movntdq %%xmm11,%0" : "=m" (s[o+16]));
	}

	asm volatile("sfence" : : : "memory");
}
#endif

/****************************************************************************/
/* parity generation */

/* internal forwarder */
static void (*raid5_gen)(unsigned char** vbuf, unsigned data, unsigned size);
static void (*raid6_gen)(unsigned char** vbuf, unsigned data, unsigned size);
static void (*raidTP_gen)(unsigned char** vbuf, unsigned data, unsigned size);
static void (*raidQP_gen)(unsigned char** vbuf, unsigned data, unsigned size);

void raid_gen(unsigned level, unsigned char** vbuf, unsigned data, unsigned size)
{
	switch (level) {
	case 1 : raid5_gen(vbuf, data, size); break;
	case 2 : raid6_gen(vbuf, data, size); break;
	case 3 : raidTP_gen(vbuf, data, size); break;
	case 4 : raidQP_gen(vbuf, data, size); break;
	}
}

/****************************************************************************/
/* recovering */

/**
 * The data recovering is based on the paper "The mathematics of RAID-6" [1],
 * that covers the RAID5 and RAID6 cases.
 *
 * In such cases we compute the parities in the Galois Field GF(2^8) with
 * the primitive polynomial x^8 + x^4 + x^3 + x^2 + 1 (285 decimal),
 * using the following equations and starting from a set of n disk Di
 * with 0<=i<n:
 *
 * P = sum(Di) 0<=i<n
 * Q = sum(2^i * Di) 0<=i<n
 *
 * To support RAIDTP (Triple Parity), we use an extension of the same approach,
 * described in the paper "Multiple-parity RAID" [2], with the additional
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
 * For RAIDQP we don't have the guarantee to always have a system of independent linear
 * equations, and in some cases the equations are not solvable.
 *
 * This is expected because the Vandermonde matrix used to compute the parity
 * has no guarantee to have all its submatrixes not singular [3, Chap 11, Problem 7]
 * and this is a requirement to have a MDS code [3, Chap 11, Theorem 8].
 *
 * If it surprises that even using a Vandermonde matrix we don't have a MDS code,
 * consider that the matrix A we use to compute the parity, is not the MDS generator
 * matrix G, but only a submatrix of it. The generator matrix G is the concatenation
 * of I and A, as G = [I | A], where I is the identity matrix.
 * Setting the matrix G as Vandermonde matrix would have guaranteed to have a MDS code,
 * but setting only A as a Vandermonde matrix doesn't give this guarantee.
 *
 * Using the primitive polynomial 285, RAIDQP works for up to 21 data disks
 * with parity coefficients "1,2,4,8". Changing polynomial to one of 391/451/463/487,
 * it works for up to 27 disks with the same parity coefficients.
 * Using different parity coefficients "5,13,27,35" it's possible to
 * make it working for up to 33 disks. But no more.
 *
 * To support more disks it's possible to use the Galois Field GF(2^16) with
 * primitive polynomial 100087 or 122563 that supports Hexa (6) Parity with
 * parity coefficients 1,2,4,8,16,32 for up to 89 disks.
 *
 * A general method working for any number of disks is to setup the matrix
 * used to compute the parity as a Cauchy matrix [4] or Extended Cauchy matrix [5].
 *
 * But with a such matrix, we would obtain a slower performance, because the
 * coefficients of the equations are not powers of the same value.
 * This means that we would need to use multiplication tables to implement the
 * parity computation, instead of the parallel approach described in [1].
 *
 * Note anyway, that there is also a method to implement parallel multiplication
 * with tables using SSSE3 instructions [6] that it's already competitive with
 * the RAIDTP parity computation.
 *
 * In details, RAIDTP is implemented for n disks Di, computing the parities
 * P,Q,R with:
 *
 * P = sum(Di) 0<=i<n
 * Q = sum(2^i * Di) 0<=i<n
 * R = sum(4^i * Di) 0<=i<n
 *
 * To recover from a failure of three disks at indexes x,y,z,
 * with 0<=x<y<z<n, we compute the parities of the available disks:
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
 * For RAIDQP we follow the same method starting with:
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
 * only for up to 21 data disk (0<=x<y<z<v<21 with the primitive poly 285).
 *
 * References:
 * [1] Anvin, "The mathematics of RAID-6", 2004
 * [2] Brown, "Multiple-parity RAID", 2011
 * [3] MacWilliams, Sloane, "The Theory of Error-Correcting Codes", 1977
 * [4] Blömer, "An XOR-Based Erasure-Resilient Coding Scheme", 1995
 * [5] Vinocha, Bhullar, Brar, "On Generator Cauchy Matrices of GDRS/GTRS Codes", 2012
 * [6] Plank, "Screaming Fast Galois Field Arithmetic Using Intel SIMD Instructions", 2013
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
static inline unsigned char inv(unsigned char v)
{
	if (v == 0) {
		fprintf(stderr, "GF division by zero\n");
		exit(EXIT_FAILURE);
	}

	return gfinv[v];
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
static inline unsigned char pown(int b, int e)
{
	switch (b) {
	case 0 : return 1;
	case 1 : return pow2(e);
	case 2 : return pow4(e);
	case 3 : return pow8(e);
	}

	fprintf(stderr, "GF invalid exponent\n");
	exit(EXIT_FAILURE);
}

/**
 * Gets the multiplication table for a specified value.
 */
static inline const unsigned char* table(unsigned char v)
{
	return gfmul[v];
}

/**
 * Computes the parity without the missing data blocks
 * and store it in the buffers of such data blocks.
 */
static void raid_delta_1data(int x, int i, unsigned char** vbuf, unsigned data, unsigned char* zero, unsigned size)
{
	unsigned char* p;
	unsigned char* pa;

	/* parity buffer */
	p = vbuf[data+i];

	/* missing data blcks */
	pa = vbuf[x];

	/* set at zero the missing data blocks */
	vbuf[x] = zero;

	/* compute only for the level we really need */
	vbuf[data+i] = pa;

	/* compute the parity */
	raid_gen(i + 1, vbuf, data, size);

	/* restore as before */
	vbuf[x] = pa;
	vbuf[data+i] = p;
}

/**
 * Computes the parity without the missing data blocks
 * and store it in the buffers of such data blocks.
 */
static void raid_delta_2data(int x, int y, int i, int j, unsigned char** vbuf, unsigned data, unsigned char* zero, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* pa;
	unsigned char* qa;

	/* parity buffer */
	p = vbuf[data+i];
	q = vbuf[data+j];

	/* missing data blcks */
	pa = vbuf[x];
	qa = vbuf[y];

	/* set at zero the missing data blocks */
	vbuf[x] = zero;
	vbuf[y] = zero;

	/* compute the parity over the missing data blocks */
	vbuf[data+i] = pa;
	vbuf[data+j] = qa;

	/* compute only for the level we really need */
	raid_gen(j + 1, vbuf, data, size);

	/* restore as before */
	vbuf[x] = pa;
	vbuf[y] = qa;
	vbuf[data+i] = p;
	vbuf[data+j] = q;
}

/**
 * Computes the parity without the missing data blocks
 * and store it in the buffers of such data blocks.
 */
static void raid_delta_3data(int x, int y, int z, int i, int j, int k, unsigned char** vbuf, unsigned data, unsigned char* zero, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	unsigned char* pa;
	unsigned char* qa;
	unsigned char* ra;

	/* parity buffer */
	p = vbuf[data+i];
	q = vbuf[data+j];
	r = vbuf[data+k];

	/* missing data blcks */
	pa = vbuf[x];
	qa = vbuf[y];
	ra = vbuf[z];

	/* set at zero the missing data blocks */
	vbuf[x] = zero;
	vbuf[y] = zero;
	vbuf[z] = zero;

	/* compute the parity over the missing data blocks */
	vbuf[data+i] = pa;
	vbuf[data+j] = qa;
	vbuf[data+k] = ra;

	/* compute only for the level we really need */
	raid_gen(k + 1, vbuf, data, size);

	/* restore as before */
	vbuf[x] = pa;
	vbuf[y] = qa;
	vbuf[z] = ra;
	vbuf[data+i] = p;
	vbuf[data+j] = q;
	vbuf[data+k] = r;
}

/**
 * Computes the parity without the missing data blocks
 * and store it in the buffers of such data blocks.
 */
static void raid_delta_4data(int x, int y, int z, int v, int i, int j, int k, int l, unsigned char** vbuf, unsigned data, unsigned char* zero, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	unsigned char* s;
	unsigned char* pa;
	unsigned char* qa;
	unsigned char* ra;
	unsigned char* sa;

	/* parity buffer */
	p = vbuf[data+i];
	q = vbuf[data+j];
	r = vbuf[data+k];
	s = vbuf[data+l];

	/* missing data blcks */
	pa = vbuf[x];
	qa = vbuf[y];
	ra = vbuf[z];
	sa = vbuf[v];

	/* set at zero the missing data blocks */
	vbuf[x] = zero;
	vbuf[y] = zero;
	vbuf[z] = zero;
	vbuf[v] = zero;

	/* compute the parity over the missing data blocks */
	vbuf[data+i] = pa;
	vbuf[data+j] = qa;
	vbuf[data+k] = ra;
	vbuf[data+l] = sa;

	/* compute only for the level we really need */
	raid_gen(l + 1, vbuf, data, size);

	/* restore as before */
	vbuf[x] = pa;
	vbuf[y] = qa;
	vbuf[z] = ra;
	vbuf[v] = sa;
	vbuf[data+i] = p;
	vbuf[data+j] = q;
	vbuf[data+k] = r;
	vbuf[data+l] = s;
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
static void raid5_recov_data(int x, unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* pa;

	/* for RAID5 we can directly compute the mssing block */
	/* and we don't need to use the zero buffer */
	p = vbuf[data];
	pa = vbuf[x];

	/* use the parity as missing data block */
	vbuf[x] = p;

	/* compute the parity over the missing data block */
	vbuf[data] = pa;

	/* compute */
	raid5_gen(vbuf, data, size);

	/* restore as before */
	vbuf[x] = pa;
	vbuf[data] = p;
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
static void raid6_recov_2data(int x, int y, unsigned char** vbuf, unsigned data, unsigned char* zero, unsigned size)
{
	unsigned o;
	unsigned char* p;
	unsigned char* pa;
	unsigned char* q;
	unsigned char* qa;
	const unsigned char* pyf; /* P factor to compute Dy */
	const unsigned char* qyf; /* Q factor to compute Dy */

	/* select tables */
	pyf = table( inv(pow2(y-x) ^ 1) );
	qyf = table( inv(pow2(x) ^ pow2(y)) );

	/* compute delta parity */
	raid_delta_2data(x, y, 0, 1, vbuf, data, zero, size);

	p = vbuf[data];
	q = vbuf[data+1];
	pa = vbuf[x];
	qa = vbuf[y];

	for(o=0;o<size;++o) {
		/* delta */
		unsigned char Pd = p[o] ^ pa[o];
		unsigned char Qd = q[o] ^ qa[o];

		/* addends to reconstruct Dy */
		unsigned char pbm = pyf[Pd];
		unsigned char qbm = qyf[Qd];

		/* reconstruct Dy */
		unsigned char Dy = pbm ^ qbm;

		/* reconstruct Dx */
		unsigned char Dx = Pd ^ Dy;

		/* set */
		pa[o] = Dx;
		qa[o] = Dy;
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
static void raidTP_recov_2dataq(int x, int y, unsigned char** vbuf, unsigned data, unsigned char* zero, unsigned size)
{
	unsigned o;
	unsigned char* p;
	unsigned char* pa;
	unsigned char* r;
	unsigned char* ra;
	const unsigned char* pyf; /* Q factor to compute Dy */
	const unsigned char* ryf; /* R factor to compute Dy */
	unsigned char c1;

	/* select tables */
	c1 = inv(pow4(y-x) ^ 1);
	pyf = table( c1 );
	ryf = table( mul(c1, inv(pow4(x))) );

	/* compute delta parity */
	raid_delta_2data(x, y, 0, 2, vbuf, data, zero, size);

	p = vbuf[data];
	r = vbuf[data+2];
	pa = vbuf[x];
	ra = vbuf[y];

	for(o=0;o<size;++o) {
		/* delta */
		unsigned char Pd = p[o] ^ pa[o];
		unsigned char Rd = r[o] ^ ra[o];

		/* addends to reconstruct Dy */
		unsigned char pym = pyf[Pd];
		unsigned char rym = ryf[Rd];

		/* reconstruct Dy */
		unsigned char Dy = pym ^ rym;

		/* reconstruct Dx */
		unsigned char Dx = Pd ^ Dy;

		/* set */
		pa[o] = Dx;
		ra[o] = Dy;
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
static void raidTP_recov_2datap(int x, int y, unsigned char** vbuf, unsigned data, unsigned char* zero, unsigned size)
{
	unsigned o;
	unsigned char* q;
	unsigned char* qa;
	unsigned char* r;
	unsigned char* ra;
	const unsigned char* qyf; /* Q factor to compute Dy */
	const unsigned char* ryf; /* R factor to compute Dy */
	const unsigned char* qxf; /* Q factor to compute Dx */
	const unsigned char* yxf; /* Dy factor to compute Dx */
	unsigned char c1;

	/* select tables */
	c1 = inv(pow2(y-x) ^ pow4(y-x));
	qyf = table( mul(c1, inv(pow2(x))) );
	ryf = table( mul(c1, inv(pow4(x))) );
	qxf = table( inv(pow2(x)) );
	yxf = table( pow2(y-x) );

	/* compute delta parity */
	raid_delta_2data(x, y, 1, 2, vbuf, data, zero, size);

	q = vbuf[data+1];
	r = vbuf[data+2];
	qa = vbuf[x];
	ra = vbuf[y];

	for(o=0;o<size;++o) {
		/* delta */
		unsigned char Qd = q[o] ^ qa[o];
		unsigned char Rd = r[o] ^ ra[o];

		/* addends to reconstruct Dy */
		unsigned char qym = qyf[Qd];
		unsigned char rym = ryf[Rd];

		/* reconstruct Dy */
		unsigned char Dy = qym ^ rym;

		/* addends to reconstruct Dx */
		unsigned char qxm = qxf[Qd];
		unsigned char bxm = yxf[Dy];

		/* reconstruct Dx */
		unsigned char Dx = qxm ^ bxm;

		/* set */
		qa[o] = Dx;
		ra[o] = Dy;
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
static void raidTP_recov_3data(int x, int y, int z, unsigned char** vbuf, unsigned data, unsigned char* zero, unsigned size)
{
	unsigned o;
	unsigned char* p;
	unsigned char* pa;
	unsigned char* q;
	unsigned char* qa;
	unsigned char* r;
	unsigned char* ra;
	const unsigned char* pzf; /* P factor to compute Dz */
	const unsigned char* qzf; /* Q factor to compute Dz */
	const unsigned char* rzf; /* R factor to compute Dz */
	const unsigned char* pyf; /* P factor to compute Dy */
	const unsigned char* qyf; /* Q factor to compute Dy */
	const unsigned char* zyf; /* Dz factor to compute Dy */
	unsigned char c1, c2, c3;

	/* select tables */
	c1 = inv(pow2(y-x) ^ 1);
	c2 = inv(pow4(y-x) ^ 1);
	c3 = inv(mul(pow2(z-x) ^ 1, c1) ^ mul(pow4(z-x) ^ 1, c2));
	pzf = table( mul(c3, c1) ^ mul(c3, c2) );
	qzf = table( mul(c3, inv(pow2(y) ^ pow2(x))) );
	rzf = table( mul(c3, inv(pow4(y) ^ pow4(x))) );
	pyf = table( c1 );
	qyf = table( mul(c1, inv(pow2(x))) );
	zyf = table( mul(c1, pow2(z-x) ^ 1) );

	/* compute delta parity */
	raid_delta_3data(x, y, z, 0, 1, 2, vbuf, data, zero, size);

	p = vbuf[data];
	q = vbuf[data+1];
	r = vbuf[data+2];
	pa = vbuf[x];
	qa = vbuf[y];
	ra = vbuf[z];

	for(o=0;o<size;++o) {
		/* delta */
		unsigned char Pd = p[o] ^ pa[o];
		unsigned char Qd = q[o] ^ qa[o];
		unsigned char Rd = r[o] ^ ra[o];

		/* addends to reconstruct Dz */
		unsigned char pzm = pzf[Pd];
		unsigned char qzm = qzf[Qd];
		unsigned char rzm = rzf[Rd];

		/* reconstruct Dz */
		unsigned char Dz = pzm ^ qzm ^ rzm;

		/* addends to reconstruct Dy */
		unsigned char pym = pyf[Pd];
		unsigned char qym = qyf[Qd];
		unsigned char zym = zyf[Dz];

		/* reconstruct Dy */
		unsigned char Dy = pym ^ qym ^ zym;

		/* reconstruct Dx */
		unsigned char Dx = Pd ^ Dy ^ Dz;

		/* set */
		pa[o] = Dx;
		qa[o] = Dy;
		ra[o] = Dz;
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
void raid_recov_1data(int x, int i, unsigned char** vbuf, unsigned data, unsigned char* zero, unsigned size)
{
	unsigned o;
	unsigned char* p;
	unsigned char* pa;
	const unsigned char* pxf; /* P factor to compute Dx */

	/* if it's RAID5 uses the dedicated and faster function */
	if (i == 0) {
		raid5_recov_data(x, vbuf, data, size);
		return;
	}

	/* select tables */
	pxf = table( inv(pown(i,x)) );

	/* compute delta parity */
	raid_delta_1data(x, i, vbuf, data, zero, size);

	p = vbuf[data+i];
	pa = vbuf[x];

	for(o=0;o<size;++o) {
		/* delta */
		unsigned char Pd = p[o] ^ pa[o];

		/* addends to reconstruct Dx */
		unsigned char pxm = pxf[Pd];

		/* reconstruct Dx */
		unsigned char Dx = pxm;

		/* set */
		pa[o] = Dx;
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
void raid_recov_2data(int x, int y, int i, int j, unsigned char** vbuf, unsigned data, unsigned char* zero, unsigned size)
{
	unsigned o;
	unsigned char* p;
	unsigned char* pa;
	unsigned char* q;
	unsigned char* qa;
	const unsigned char* pxf; /* P factor to compute Dx */
	const unsigned char* qxf; /* Q factor to compute Dx */
	const unsigned char* pyf; /* P factor to compute Dy */
	const unsigned char* qyf; /* Q factor to compute Dy */
	unsigned char G[2*2];
	unsigned char V[2*2];
	unsigned n = 2;

	/* if it's RAID6 uses the dedicated and faster function */
	if (i == 0 && j == 1) {
		raid6_recov_2data(x, y, vbuf, data, zero, size);
		return;
	}
	/* if it's RAIDTP uses the dedicated and faster function */
	if (i == 0 && j == 2) {
		raidTP_recov_2dataq(x, y, vbuf, data, zero, size);
		return;
	}
	/* if it's RAIDTP uses the dedicated and faster function */
	if (i == 1 && j == 2) {
		raidTP_recov_2datap(x, y, vbuf, data, zero, size);
		return;
	}

	/* setup the generator matrix */
	G[0*n+0] = pown(i,x); /* row 1 for P */
	G[0*n+1] = pown(i,y);
	G[1*n+0] = pown(j,x); /* row 2 for Q */
	G[1*n+1] = pown(j,y);

	/* invert it to solve the system of linear equations */
	invert(G, V, n);

	/* select tables */
	pxf = table( V[0*n+0] );
	qxf = table( V[0*n+1] );
	pyf = table( V[1*n+0] );
	qyf = table( V[1*n+1] );

	/* compute delta parity */
	raid_delta_2data(x, y, i, j, vbuf, data, zero, size);

	p = vbuf[data+i];
	q = vbuf[data+j];
	pa = vbuf[x];
	qa = vbuf[y];

	for(o=0;o<size;++o) {
		/* delta */
		unsigned char Pd = p[o] ^ pa[o];
		unsigned char Qd = q[o] ^ qa[o];

		/* addends to reconstruct Dy */
		unsigned char pym = pyf[Pd];
		unsigned char qym = qyf[Qd];

		/* reconstruct Dy */
		unsigned char Dy = pym ^ qym;

		/* reconstruct Dx */
		unsigned char Dx;

		/* if i is P, take the fast way */
		if (i == 0) {
			Dx = Pd ^ Dy;
		} else {
			/* addends to reconstruct Dx */
			unsigned char pxm = pxf[Pd];
			unsigned char qxm = qxf[Qd];

			/* reconstruct Dx */
			Dx = pxm ^ qxm;
		}

		/* set */
		pa[o] = Dx;
		qa[o] = Dy;
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
void raid_recov_3data(int x, int y, int z, int i, int j, int k, unsigned char** vbuf, unsigned data, unsigned char* zero, unsigned size)
{
	unsigned o;
	unsigned char* p;
	unsigned char* pa;
	unsigned char* q;
	unsigned char* qa;
	unsigned char* r;
	unsigned char* ra;
	const unsigned char* pxf; /* P factor to compute Dx */
	const unsigned char* qxf; /* Q factor to compute Dx */
	const unsigned char* rxf; /* R factor to compute Dx */
	const unsigned char* pyf; /* P factor to compute Dy */
	const unsigned char* qyf; /* Q factor to compute Dy */
	const unsigned char* ryf; /* R factor to compute Dy */
	const unsigned char* pzf; /* P factor to compute Dz */
	const unsigned char* qzf; /* Q factor to compute Dz */
	const unsigned char* rzf; /* R factor to compute Dz */
	unsigned char G[3*3];
	unsigned char V[3*3];
	unsigned n = 3;

	/* if it's RAIDTP uses the dedicated and faster function */
	if (i == 0 && j == 1 && k == 2) {
		raidTP_recov_3data(x, y, z, vbuf, data, zero, size);
		return;
	}

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
	pxf = table( V[0*n+0] );
	qxf = table( V[0*n+1] );
	rxf = table( V[0*n+2] );
	pyf = table( V[1*n+0] );
	qyf = table( V[1*n+1] );
	ryf = table( V[1*n+2] );
	pzf = table( V[2*n+0] );
	qzf = table( V[2*n+1] );
	rzf = table( V[2*n+2] );

	/* compute delta parity */
	raid_delta_3data(x, y, z, i, j, k, vbuf, data, zero, size);

	p = vbuf[data+i];
	q = vbuf[data+j];
	r = vbuf[data+k];
	pa = vbuf[x];
	qa = vbuf[y];
	ra = vbuf[z];

	for(o=0;o<size;++o) {
		/* delta */
		unsigned char Pd = p[o] ^ pa[o];
		unsigned char Qd = q[o] ^ qa[o];
		unsigned char Rd = r[o] ^ ra[o];

		/* addends to reconstruct Dz */
		unsigned char pzm = pzf[Pd];
		unsigned char qzm = qzf[Qd];
		unsigned char rzm = rzf[Rd];

		/* reconstruct Dz */
		unsigned char Dz = pzm ^ qzm ^ rzm;

		/* addends to reconstruct Dy */
		unsigned char pym = pyf[Pd];
		unsigned char qym = qyf[Qd];
		unsigned char rym = ryf[Rd];

		/* reconstruct Dy */
		unsigned char Dy = pym ^ qym ^ rym;

		/* reconstruct Dx */
		unsigned char Dx;

		/* if i is P, take the fast way */
		if (i == 0) {
			Dx = Pd ^ Dy ^ Dz;
		} else {
			/* addends to reconstruct Dx */
			unsigned char pxm = pxf[Pd];
			unsigned char qxm = qxf[Qd];
			unsigned char rxm = rxf[Rd];

			/* reconstruct Dx */
			Dx = pxm ^ qxm ^ rxm;
		}

		/* set */
		pa[o] = Dx;
		qa[o] = Dy;
		ra[o] = Dz;
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
void raid_recov_4data(int x, int y, int z, int v, int i, int j, int k, int l, unsigned char** vbuf, unsigned data, unsigned char* zero, unsigned size)
{
	unsigned o;
	unsigned char* p;
	unsigned char* pa;
	unsigned char* q;
	unsigned char* qa;
	unsigned char* r;
	unsigned char* ra;
	unsigned char* s;
	unsigned char* sa;
	const unsigned char* pxf; /* P factor to compute Dx */
	const unsigned char* qxf; /* Q factor to compute Dx */
	const unsigned char* rxf; /* R factor to compute Dx */
	const unsigned char* sxf; /* S factor to compute Dx */
	const unsigned char* pyf; /* P factor to compute Dy */
	const unsigned char* qyf; /* Q factor to compute Dy */
	const unsigned char* ryf; /* R factor to compute Dy */
	const unsigned char* syf; /* S factor to compute Dy */
	const unsigned char* pzf; /* P factor to compute Dz */
	const unsigned char* qzf; /* Q factor to compute Dz */
	const unsigned char* rzf; /* R factor to compute Dz */
	const unsigned char* szf; /* S factor to compute Dz */
	const unsigned char* pvf; /* P factor to compute Dv */
	const unsigned char* qvf; /* Q factor to compute Dv */
	const unsigned char* rvf; /* R factor to compute Dv */
	const unsigned char* svf; /* S factor to compute Dv */
	unsigned char G[4*4];
	unsigned char V[4*4];
	unsigned n = 4;

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
	pxf = table( V[0*n+0] );
	qxf = table( V[0*n+1] );
	rxf = table( V[0*n+2] );
	sxf = table( V[0*n+3] );
	pyf = table( V[1*n+0] );
	qyf = table( V[1*n+1] );
	ryf = table( V[1*n+2] );
	syf = table( V[1*n+3] );
	pzf = table( V[2*n+0] );
	qzf = table( V[2*n+1] );
	rzf = table( V[2*n+2] );
	szf = table( V[2*n+3] );
	pvf = table( V[3*n+0] );
	qvf = table( V[3*n+1] );
	rvf = table( V[3*n+2] );
	svf = table( V[3*n+3] );

	/* compute delta parity */
	raid_delta_4data(x, y, z, v, i, j, k, l, vbuf, data, zero, size);

	p = vbuf[data+i];
	q = vbuf[data+j];
	r = vbuf[data+k];
	s = vbuf[data+l];
	pa = vbuf[x];
	qa = vbuf[y];
	ra = vbuf[z];
	sa = vbuf[v];

	for(o=0;o<size;++o) {
		/* delta */
		unsigned char Pd = p[o] ^ pa[o];
		unsigned char Qd = q[o] ^ qa[o];
		unsigned char Rd = r[o] ^ ra[o];
		unsigned char Sd = s[o] ^ sa[o];

		/* addends to reconstruct Dv */
		unsigned char pvm = pvf[Pd];
		unsigned char qvm = qvf[Qd];
		unsigned char rvm = rvf[Rd];
		unsigned char svm = svf[Sd];

		/* reconstruct Dv */
		unsigned char Dv = pvm ^ qvm ^ rvm ^ svm;

		/* addends to reconstruct Dz */
		unsigned char pzm = pzf[Pd];
		unsigned char qzm = qzf[Qd];
		unsigned char rzm = rzf[Rd];
		unsigned char szm = szf[Sd];

		/* reconstruct Dz */
		unsigned char Dz = pzm ^ qzm ^ rzm ^ szm;

		/* addends to reconstruct Dy */
		unsigned char pym = pyf[Pd];
		unsigned char qym = qyf[Qd];
		unsigned char rym = ryf[Rd];
		unsigned char sym = syf[Sd];

		/* reconstruct Dy */
		unsigned char Dy = pym ^ qym ^ rym ^ sym;

		/* reconstruct Dx */
		unsigned char Dx;

		/* if i is P, take the fast way */
		if (i == 0) {
			Dx = Pd ^ Dy ^ Dz ^ Dv;
		} else {
			/* addends to reconstruct Dx */
			unsigned char pxm = pxf[Pd];
			unsigned char qxm = qxf[Qd];
			unsigned char rxm = rxf[Rd];
			unsigned char sxm = sxf[Sd];

			/* reconstruct Dx */
			Dx = pxm ^ qxm ^ rxm ^ sxm;
		}

		/* set */
		pa[o] = Dx;
		qa[o] = Dy;
		ra[o] = Dz;
		sa[o] = Dv;
	}
}

/****************************************************************************/
/* init/done */

void raid_init(void)
{
	if (sizeof(void*) == 4) {
		raid5_gen = raid5_int32;
		raid6_gen = raid6_int32;
		raidTP_gen = raidTP_int32;
		raidQP_gen = raidQP_int32;
	} else {
		raid5_gen = raid5_int64;
		raid6_gen = raid6_int64;
		raidTP_gen = raidTP_int64;
		raidQP_gen = raidQP_int64;
	}

#if defined(__i386__) || defined(__x86_64__)
	if (cpu_has_mmx()) {
		raid5_gen = raid5_mmx;
		raid6_gen = raid6_mmx;
		raidTP_gen = raidTP_mmx;
		raidQP_gen = raidQP_mmx;
	}

	if (cpu_has_sse2()) {
		raid5_gen = raid5_sse2;
#if defined(__x86_64__)
		if (cpu_has_slowextendedreg()) {
			raid6_gen = raid6_sse2;
		} else {
			raid6_gen = raid6_sse2ext;
		}
		raidTP_gen = raidTP_sse2ext;
		raidQP_gen = raidQP_sse2ext;
#else
		raid6_gen = raid6_sse2;
		raidTP_gen = raidTP_sse2;
		raidQP_gen = raidQP_sse2;
#endif
	}

	if (cpu_has_ssse3() && !cpu_has_slowpshufb()) {
#if defined(__x86_64__)
		raidTP_gen = raidTP_ssse3ext;
		raidQP_gen = raidQP_ssse3ext;
#else
		raidTP_gen = raidTP_ssse3;
		raidQP_gen = raidQP_ssse3;
#endif
	}

	if (cpu_has_avx()) {
#if defined(__x86_64__)
		raidQP_gen = raidQP_avxext;
#endif
	}

#endif
}

static struct raid_func {
	const char* name;
	void* p;
} RAID_FUNC[] = {
	{ "int32", raid5_int32 },
	{ "int64", raid5_int64 },
	{ "int32", raid6_int32 },
	{ "int64", raid6_int64 },
	{ "int32", raidTP_int32 },
	{ "int64", raidTP_int64 },
	{ "int32", raidQP_int32 },
	{ "int64", raidQP_int64 },

#if defined(__i386__) || defined(__x86_64__)
	{ "mmx", raid5_mmx },
	{ "mmx", raid6_mmx },
	{ "mmx", raidTP_mmx },
	{ "mmx", raidQP_mmx },
	{ "sse2", raid5_sse2 },
	{ "sse2", raid6_sse2 },
	{ "sse2", raidTP_sse2 },
	{ "sse2", raidQP_sse2 },
	{ "ssse3", raidTP_ssse3 },
	{ "ssse3", raidQP_ssse3 },
#endif

#if defined(__x86_64__)
	{ "sse2ext", raid6_sse2ext },
	{ "sse2ext", raidTP_sse2ext },
	{ "sse2ext", raidQP_sse2ext },
	{ "ssse3ext", raidTP_ssse3ext },
	{ "ssse3ext", raidQP_ssse3ext },
	{ "avxext", raidQP_avxext },
#endif
	{ 0, 0 }
};

static const char* raid_tag(void* func)
{
	struct raid_func* i = RAID_FUNC;
	while (i->name != 0) {
		if (i->p == func)
			return i->name;
		++i;
	}
	return "unknown";
}

const char* raid5_tag(void)
{
	return raid_tag(raid5_gen);
}

const char* raid6_tag(void)
{
	return raid_tag(raid6_gen);
}

const char* raidTP_tag(void)
{
	return raid_tag(raidTP_gen);
}

const char* raidQP_tag(void)
{
	return raid_tag(raidQP_gen);
}


