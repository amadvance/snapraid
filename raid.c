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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * The RAID5/RAID6 support was originally derived from the libraid6 library
 * by H. Peter Anvin released with license "GPL2 or any later version"
 * used in the Linux Kernel 2.6.38.
 * This support was later completely rewritten (many times), but the
 * H. Peter Anvin's Copyright may still apply.
 *
 * The RAIDTP/RAIDQP/RAIDPP/RAIDHP support is original work implemented 
 * from scratch.
 *
 * The RAID5 and RAID6 support works like the one implemented in the Linux
 * Kernel RAID and it's based on the Anvin's paper "The mathematics of RAID-6" [1].
 *
 * We compute the parity in the Galois Field GF(2^8) with the primitive
 * polynomial x^8 + x^4 + x^3 + x^2 + 1 (285 decimal), starting from a set 
 * of n disk Di with 0<=i<n, using the equations:
 *
 * P = sum(Di)
 * Q = sum(2^i * Di) with 0<=i<n
 *
 * To support RAIDTP (Triple Parity), it was first evaluated (and then dropped),
 * to use an extension of the same approach, with additional parity
 * coefficients set as powers of 4 with equations:
 *
 * P = sum(Di)
 * Q = sum(2^i * Di)
 * R = sum(4^i * Di) with 0<=i<n
 *
 * This method is the same used by ZFS to implement its RAIDTP support,
 * it works well, and it's very efficient.
 * 
 * Unfortunately, the same approach doesn't work for RAIDQP (Quad Parity).
 * Using the parity coefficients set as power of 8 with equations:
 *
 * P = sum(Di)
 * Q = sum(2^i * Di)
 * R = sum(4^i * Di)
 * S = sum(8^i * Di) with 0<=i<n
 *
 * we don't have a system of independent linear equations, and in some cases
 * the equations are not solvable.
 *
 * This approach is expected to fail at some point, because the Vandermonde 
 * matrix used to compute the parity has no guarantee to have all its 
 * submatrices not singular [2, Chap 11, Problem 7] and this is a requirement
 * to have a MDS code [2, Chap 11, Theorem 8].
 * 
 * If it surprises that even using a Vandermonde matrix we don't have a 
 * MDS code, consider that the matrix A we use to compute the parity, is 
 * not the MDS generator matrix G, but only a submatrix of it. The generator 
 * matrix G is the concatenation of I and A, as G = [I | A], where I is 
 * the identity matrix.
 * Setting the matrix G as Vandermonde matrix would have guaranteed to have 
 * a MDS code, but setting only A as a Vandermonde matrix doesn't give 
 * this guarantee.
 *
 * Using the primitive polynomial 285, RAIDQP works for up to 21 data disks
 * with parity coefficients "1,2,4,8". Changing polynomial to one of 391/451/463/487,
 * it works for up to 27 disks with the same parity coefficients.
 * Using different parity coefficients "5,13,27,35" it's possible to
 * make it working for up to 33 disks. But no more.
 *
 * To support more disks it's possible to use the Galois Field GF(2^16) with
 * primitive polynomial 100087 or 122563 that supports up to Hexa (6) Parity 
 * with parity coefficients 1,2,4,8,16,32 for up to 89 disks.
 *
 * To overcome these limitations we instead use an Extended Cauchy Matrix [3][4] 
 * to compute the parity. 
 * Such matrix has the mathematical property to have all the square 
 * submatrices not singular, resulting in always solvable equations, for
 * any number of parities and for any number of disks.
 *
 * The problem of this approach is that the coefficients of the equations 
 * are not powers of the same value easy to compute.
 * We need to use multiplication tables to implement the parity computation, 
 * instead of the parallel approach described in [1].
 *
 * Hopefully there is a method to implement parallel multiplication
 * with tables using SSSE3 instructions [1][5]. Method competitive with
 * the RAIDTP parity computation using power coefficients.
 *
 * Another important property of the Extended Cauchy matrix is that we can 
 * setup the first two rows with coeffients equal at the RAID5 and RAID6 
 * approach described in [1], resulting in a compatible extension.
 * 
 * We also "normalize" the matrix, multipling each row for a constant
 * factor to make the first column with all 1.
 *
 * This results in the "normalized" Extended Cauchy matrix A[row,col] defined as:
 * 
 * 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 ...
 * 01 02 04 08 10 20 40 80 1d 3a 74 e8 cd 87 13 26 4c 98 2d 5a b4 75 ea c9 8f 03 06 ...
 * 01 f5 d2 c4 9a 71 f1 7f fc 87 c1 c6 19 2f 40 55 3d ba 53 04 9c 61 34 8c 46 68 70 ...
 * 01 bb a6 d7 c7 07 ce 82 4a 2f a5 9b b6 60 f1 ad e7 f4 06 d2 df 2e ca 65 5c 48 21 ...
 * 01 97 7f 9c 7c 18 bd a2 58 1a da 74 70 a3 e5 47 29 07 f5 80 23 e9 fa 46 54 a0 99 ...
 * 01 2b 3f cf 73 2c d6 ed cb 74 15 78 8a c1 17 c9 89 68 21 ab 76 3b 4b 5a 6e 0e b9 ...
 * (see tables.h for the full matrix)
 *
 * with one row for each parity level, and one column for each data disk.
 *
 * This matrix supports 6 level of parity, one for each row, for up to 251
 * data disks, one for each column. 
 * 
 * This matrix can be extended to support any number of parities, just adding
 * additional rows, (but removing one column for each row added), without
 * the need to changes the already existing rows, and always maintaining the
 * guarantee to always have all square submatrices not singular.
 * (see mktables.c for more details in how the matrix is generated)
 * 
 * Resulting speed in x64, with 24 data disks, using a stripe of 256 KiB,
 * for a Core i7-3740QM CPU @ 2.7GHz is:
 * 
 *          int8  int32  int64    mmx   sse2  sse2e  ssse3 ssse3e
 *    raid5        9042  16698  19978  26734
 *    raid6        3205   5478   7911  13987  16287
 *   raidTP  828                                      6802   7186
 *   raidQP  637                                      5483   5843
 *   raidPP  513                                      4325   4629
 *   raidHP  428                                      3479   3866
 *
 * Values are in MiB/s of data read and processed, not counting written parity.
 *
 * For comparison, a similar implementation using the power coeffients approach
 * in the same condition, is only a little faster (16%) for RAIDTP with power
 * coefficient "4", and equal for RAIDQP that gets faster (10%) only using
 * AVX instructions.
 *
 *                int32  int64    mmx   sse2  sse2e  ssse3 ssse3e   avxe
 *   raidTP        1406   2522   3788   7240   7886   8071   8358
 *   raidQP         773   1375   2277   4573   4678   5457   5778   6468
 * 
 * In details parity is computed as:
 *
 * P = sum(Di)
 * Q = sum(2^i *  Di)
 * R = sum(A[2,i] * Di)
 * S = sum(A[3,i] * Di)
 * T = sum(A[4,i] * Di)
 * O = sum(A[5,i] * Di) with 0<=i<n
 *
 * To recover from a failure of six disks at indexes x,y,z,v,u,w,
 * with 0<=x<y<z<v<u<w<n, we compute the parity of the available n-6 disks as:
 *
 * Pa = sum(Di)
 * Qa = sum(2^i * Di)
 * Ra = sum(A[2,i] * Di)
 * Sa = sum(A[3,i] * Di)
 * Ta = sum(A[4,i] * Di)
 * Oa = sum(A[5,i] * Di) with 0<=i<n,i!=x,i!=y,i!=z,i!=v,i!=u,i!=w.
 *
 * And if we define:
 *
 * Pd = Pa + P
 * Qd = Qa + Q
 * Rd = Ra + R
 * Sd = Sa + S
 * Td = Ta + T
 * Od = Oa + O
 *
 * we can sum these two set equations, obtaining:
 *
 * Pd =          Dx +          Dy +          Dz +          Dv +          Du +          Dw
 * Qd =    2^x * Dx +    2^y * Dy +    2^z * Dz +    2^v * Dv +    2^u * Du +    2^w * Dw
 * Rd = A[2,x] * Dx + A[2,y] * Dy + A[2,z] * Dz + A[2,v] * Dv + A[2,u] * Du + A[2,w] * Dw
 * Sd = A[3,x] * Dx + A[3,y] * Dy + A[3,z] * Dz + A[3,v] * Dv + A[3,u] * Du + A[3,w] * Dw
 * Td = A[4,x] * Dx + A[4,y] * Dy + A[4,z] * Dz + A[4,v] * Dv + A[4,u] * Du + A[4,w] * Dw
 * Od = A[5,x] * Dx + A[5,y] * Dy + A[5,z] * Dz + A[5,v] * Dv + A[5,u] * Du + A[5,w] * Dw
 *
 * A linear system always solvable because the coefficients matrix is always
 * not singular due the properties of the matrix A[].
 *
 * References:
 * [1] Anvin, "The mathematics of RAID-6", 2004
 * [2] MacWilliams, Sloane, "The Theory of Error-Correcting Codes", 1977
 * [3] Blömer, "An XOR-Based Erasure-Resilient Coding Scheme", 1995
 * [4] Vinocha, Bhullar, Brar, "On Generator Cauchy Matrices of GDRS/GTRS Codes", 2012
 * [5] Plank, "Screaming Fast Galois Field Arithmetic Using Intel SIMD Instructions", 2013
 */

#include "portable.h"

#include "raid.h"
#include "cpu.h"
#include "tables.h"

/**
 * Dereference as uint8_t
 */
#define v_8(p) (*(uint8_t*)&(p))

/**
 * Dereference as uint32_t
 */
#define v_32(p) (*(uint32_t*)&(p))

/**
 * Dereference as uint64_t
 */
#define v_64(p) (*(uint64_t*)&(p))

/****************************************************************************/
/* specialized parity generation */

/*
 * RAID5 32bit C implementation
 */
void raid5_int32(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	int d, l;
	unsigned off;

	uint32_t p0;
	uint32_t p1;

	l = data - 1;
	p = vbuf[data];

	for(off=0;off<size;off+=8) {
		p0 = v_32(vbuf[l][off]);
		p1 = v_32(vbuf[l][off+4]);
		for(d=l-1;d>=0;--d) {
			p0 ^= v_32(vbuf[d][off]);
			p1 ^= v_32(vbuf[d][off+4]);
		}
		v_32(p[off]) = p0;
		v_32(p[off+4]) = p1;
	}
}

/*
 * RAID5 64bit C implementation
 */
void raid5_int64(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	int d, l;
	unsigned off;

	uint64_t p0;
	uint64_t p1;

	l = data - 1;
	p = vbuf[data];

	for(off=0;off<size;off+=16) {
		p0 = v_64(vbuf[l][off]);
		p1 = v_64(vbuf[l][off+8]);
		for(d=l-1;d>=0;--d) {
			p0 ^= v_64(vbuf[d][off]);
			p1 ^= v_64(vbuf[d][off+8]);
		}
		v_64(p[off]) = p0;
		v_64(p[off+8]) = p1;
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
	unsigned off;

	l = data - 1;
	p = vbuf[data];

	for(off=0;off<size;off+=32) {
		asm volatile("movq %0,%%mm0" : : "m" (vbuf[l][off]));
		asm volatile("movq %0,%%mm1" : : "m" (vbuf[l][off+8]));
		asm volatile("movq %0,%%mm2" : : "m" (vbuf[l][off+16]));
		asm volatile("movq %0,%%mm3" : : "m" (vbuf[l][off+24]));
		for(d=l-1;d>=0;--d) {
			asm volatile("movq %0,%%mm4" : : "m" (vbuf[d][off]));
			asm volatile("movq %0,%%mm5" : : "m" (vbuf[d][off+8]));
			asm volatile("movq %0,%%mm6" : : "m" (vbuf[d][off+16]));
			asm volatile("movq %0,%%mm7" : : "m" (vbuf[d][off+24]));
			asm volatile("pxor %mm4,%mm0");
			asm volatile("pxor %mm5,%mm1");
			asm volatile("pxor %mm6,%mm2");
			asm volatile("pxor %mm7,%mm3");
		}
		asm volatile("movq %%mm0,%0" : "=m" (p[off]));
		asm volatile("movq %%mm1,%0" : "=m" (p[off+8]));
		asm volatile("movq %%mm2,%0" : "=m" (p[off+16]));
		asm volatile("movq %%mm3,%0" : "=m" (p[off+24]));
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
	unsigned off;

	l = data - 1;
	p = vbuf[data];

	for(off=0;off<size;off+=64) {
		asm volatile("movdqa %0,%%xmm0" : : "m" (vbuf[l][off]));
		asm volatile("movdqa %0,%%xmm1" : : "m" (vbuf[l][off+16]));
		asm volatile("movdqa %0,%%xmm2" : : "m" (vbuf[l][off+32]));
		asm volatile("movdqa %0,%%xmm3" : : "m" (vbuf[l][off+48]));
		for(d=l-1;d>=0;--d) {
			asm volatile("movdqa %0,%%xmm4" : : "m" (vbuf[d][off]));
			asm volatile("movdqa %0,%%xmm5" : : "m" (vbuf[d][off+16]));
			asm volatile("movdqa %0,%%xmm6" : : "m" (vbuf[d][off+32]));
			asm volatile("movdqa %0,%%xmm7" : : "m" (vbuf[d][off+48]));
			asm volatile("pxor %xmm4,%xmm0");
			asm volatile("pxor %xmm5,%xmm1");
			asm volatile("pxor %xmm6,%xmm2");
			asm volatile("pxor %xmm7,%xmm3");
		}
		asm volatile("movntdq %%xmm0,%0" : "=m" (p[off]));
		asm volatile("movntdq %%xmm1,%0" : "=m" (p[off+16]));
		asm volatile("movntdq %%xmm2,%0" : "=m" (p[off+32]));
		asm volatile("movntdq %%xmm3,%0" : "=m" (p[off+48]));
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
	unsigned off;

	uint32_t d0, q0, p0;
	uint32_t d1, q1, p1;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];

	for(off=0;off<size;off+=8) {
		q0 = p0 = v_32(vbuf[l][off]);
		q1 = p1 = v_32(vbuf[l][off+4]);
		for(d=l-1;d>=0;--d) {
			d0 = v_32(vbuf[d][off]);
			d1 = v_32(vbuf[d][off+4]);

			p0 ^= d0;
			p1 ^= d1;

			q0 = x2_32(q0);
			q1 = x2_32(q1);

			q0 ^= d0;
			q1 ^= d1;
		}
		v_32(p[off]) = p0;
		v_32(p[off+4]) = p1;
		v_32(q[off]) = q0;
		v_32(q[off+4]) = q1;
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
	unsigned off;

	uint64_t d0, q0, p0;
	uint64_t d1, q1, p1;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];

	for(off=0;off<size;off+=16) {
		q0 = p0 = v_64(vbuf[l][off]);
		q1 = p1 = v_64(vbuf[l][off+8]);
		for(d=l-1;d>=0;--d) {
			d0 = v_64(vbuf[d][off]);
			d1 = v_64(vbuf[d][off+8]);

			p0 ^= d0;
			p1 ^= d1;

			q0 = x2_64(q0);
			q1 = x2_64(q1);

			q0 ^= d0;
			q1 ^= d1;
		}
		v_64(p[off]) = p0;
		v_64(p[off+8]) = p1;
		v_64(q[off]) = q0;
		v_64(q[off+8]) = q1;
	}
}

#if defined(__i386__) || defined(__x86_64__)
static const struct raid_8_const {
	unsigned char poly[8];
} raid_8_const __attribute__((aligned(8))) = {
	{ 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d } 
};

/*
 * RAID6 MMX implementation
 */
void raid6_mmx(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	int d, l;
	unsigned off;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];

	asm volatile("movq %0,%%mm7" : : "m" (raid_8_const.poly[0]));

	for(off=0;off<size;off+=16) {
		asm volatile("movq %0,%%mm0" : : "m" (vbuf[l][off]));
		asm volatile("movq %0,%%mm1" : : "m" (vbuf[l][off+8]));
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

			asm volatile("movq %0,%%mm4" : : "m" (vbuf[d][off]));
			asm volatile("movq %0,%%mm5" : : "m" (vbuf[d][off+8]));
			asm volatile("pxor %mm4,%mm0");
			asm volatile("pxor %mm5,%mm1");
			asm volatile("pxor %mm4,%mm2");
			asm volatile("pxor %mm5,%mm3");
		}
		asm volatile("movq %%mm0,%0" : "=m" (p[off]));
		asm volatile("movq %%mm1,%0" : "=m" (p[off+8]));
		asm volatile("movq %%mm2,%0" : "=m" (q[off]));
		asm volatile("movq %%mm3,%0" : "=m" (q[off+8]));
	}

	asm volatile("emms" : : : "memory");
}

static const struct raid_16_const {
	unsigned char poly[16];
	unsigned char mask[16];
} raid_16_const  __attribute__((aligned(32))) = {
	{ 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d },
	{ 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f }
};

/*
 * RAID6 SSE2 implementation
 */
void raid6_sse2(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	int d, l;
	unsigned off;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];

	asm volatile("movdqa %0,%%xmm7" : : "m" (raid_16_const.poly[0]));

	for(off=0;off<size;off+=32) {
		asm volatile("movdqa %0,%%xmm0" : : "m" (vbuf[l][off]));
		asm volatile("movdqa %0,%%xmm1" : : "m" (vbuf[l][off+16]));
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

			asm volatile("movdqa %0,%%xmm4" : : "m" (vbuf[d][off]));
			asm volatile("movdqa %0,%%xmm5" : : "m" (vbuf[d][off+16]));
			asm volatile("pxor %xmm4,%xmm0");
			asm volatile("pxor %xmm5,%xmm1");
			asm volatile("pxor %xmm4,%xmm2");
			asm volatile("pxor %xmm5,%xmm3");
		}
		asm volatile("movntdq %%xmm0,%0" : "=m" (p[off]));
		asm volatile("movntdq %%xmm1,%0" : "=m" (p[off+16]));
		asm volatile("movntdq %%xmm2,%0" : "=m" (q[off]));
		asm volatile("movntdq %%xmm3,%0" : "=m" (q[off+16]));
	}

	asm volatile("sfence" : : : "memory");
}
#endif

#if defined(__x86_64__)
/*
 * RAID6 SSE2 implementation
 *
 * Note that it uses 16 registers, meaning that x64 is required.
 */
void raid6_sse2ext(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	int d, l;
	unsigned off;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];

	asm volatile("movdqa %0,%%xmm15" : : "m" (raid_16_const.poly[0]));

	for(off=0;off<size;off+=64) {
		asm volatile("movdqa %0,%%xmm0" : : "m" (vbuf[l][off]));
		asm volatile("movdqa %0,%%xmm1" : : "m" (vbuf[l][off+16]));
		asm volatile("movdqa %0,%%xmm2" : : "m" (vbuf[l][off+32]));
		asm volatile("movdqa %0,%%xmm3" : : "m" (vbuf[l][off+48]));
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

			asm volatile("movdqa %0,%%xmm8" : : "m" (vbuf[d][off]));
			asm volatile("movdqa %0,%%xmm9" : : "m" (vbuf[d][off+16]));
			asm volatile("movdqa %0,%%xmm10" : : "m" (vbuf[d][off+32]));
			asm volatile("movdqa %0,%%xmm11" : : "m" (vbuf[d][off+48]));
			asm volatile("pxor %xmm8,%xmm0");
			asm volatile("pxor %xmm9,%xmm1");
			asm volatile("pxor %xmm10,%xmm2");
			asm volatile("pxor %xmm11,%xmm3");
			asm volatile("pxor %xmm8,%xmm4");
			asm volatile("pxor %xmm9,%xmm5");
			asm volatile("pxor %xmm10,%xmm6");
			asm volatile("pxor %xmm11,%xmm7");
		}
		asm volatile("movntdq %%xmm0,%0" : "=m" (p[off]));
		asm volatile("movntdq %%xmm1,%0" : "=m" (p[off+16]));
		asm volatile("movntdq %%xmm2,%0" : "=m" (p[off+32]));
		asm volatile("movntdq %%xmm3,%0" : "=m" (p[off+48]));
		asm volatile("movntdq %%xmm4,%0" : "=m" (q[off]));
		asm volatile("movntdq %%xmm5,%0" : "=m" (q[off+16]));
		asm volatile("movntdq %%xmm6,%0" : "=m" (q[off+32]));
		asm volatile("movntdq %%xmm7,%0" : "=m" (q[off+48]));
	}

	asm volatile("sfence" : : : "memory");
}
#endif

/*
 * RAIDTP 8bit C implementation
 * 
 * Note that instead of a generic multiplicationt table, likely resulting in multiple cache misses,
 * a precomputed table could be used. But this is only a kind of reference function,
 * and we are not really interested on speed.
 */
void raidTP_int8(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	int d, l;
	unsigned off;

	uint8_t d0, r0, q0, p0;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];
	r = vbuf[data+2];

	for(off=0;off<size;off+=1) {
		r0 = q0 = p0 = 0;
		for(d=l;d>=0;--d) {
			d0 = v_8(vbuf[d][off]);

			p0 ^= d0;
			q0 ^= gfmul[d0][gfmatrix[1][d]];
			r0 ^= gfmul[d0][gfmatrix[2][d]];
		}
		v_8(p[off]) = p0;
		v_8(q[off]) = q0;
		v_8(r[off]) = r0;
	}
}

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
	unsigned off;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];
	r = vbuf[data+2];

	asm volatile("movdqa %0,%%xmm3" : : "m" (raid_16_const.poly[0]));
	asm volatile("movdqa %0,%%xmm7" : : "m" (raid_16_const.mask[0]));

	for(off=0;off<size;off+=16) {
		asm volatile("pxor %xmm0,%xmm0");
		asm volatile("pxor %xmm1,%xmm1");
		asm volatile("pxor %xmm2,%xmm2");
		for(d=l;d>=0;--d) {
			asm volatile("movdqa %0,%%xmm4" : : "m" (vbuf[d][off]));

			asm volatile("pxor %xmm5,%xmm5");
			asm volatile("pcmpgtb %xmm1,%xmm5");
			asm volatile("paddb %xmm1,%xmm1");
			asm volatile("pand %xmm3,%xmm5");
			asm volatile("pxor %xmm5,%xmm1");

			asm volatile("pxor %xmm4,%xmm0");
			asm volatile("pxor %xmm4,%xmm1");

			asm volatile("movdqa %xmm4,%xmm5");
			asm volatile("psraw  $4,%xmm5");
			asm volatile("pand   %xmm7,%xmm4");
			asm volatile("pand   %xmm7,%xmm5");

			asm volatile("movdqa %0,%%xmm6" : : "m" (gfpshufb[d][0][0][0]));
			asm volatile("pshufb %xmm4,%xmm6");
			asm volatile("pxor   %xmm6,%xmm2");
			asm volatile("movdqa %0,%%xmm6" : : "m" (gfpshufb[d][0][1][0]));
			asm volatile("pshufb %xmm5,%xmm6");
			asm volatile("pxor   %xmm6,%xmm2");
		}
		asm volatile("movntdq %%xmm0,%0" : "=m" (p[off]));
		asm volatile("movntdq %%xmm1,%0" : "=m" (q[off]));
		asm volatile("movntdq %%xmm2,%0" : "=m" (r[off]));
	}

	asm volatile("sfence" : : : "memory");
}
#endif

#if defined(__x86_64__)
/*
 * RAIDTP SSSE3 implementation
 *
 * Note that it uses 16 registers, meaning that x64 is required.
 */
void raidTP_ssse3ext(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	int d, l;
	unsigned off;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];
	r = vbuf[data+2];

	asm volatile("movdqa %0,%%xmm3" : : "m" (raid_16_const.poly[0]));
	asm volatile("movdqa %0,%%xmm11" : : "m" (raid_16_const.mask[0]));

	for(off=0;off<size;off+=32) {
		asm volatile("pxor %xmm0,%xmm0");
		asm volatile("pxor %xmm1,%xmm1");
		asm volatile("pxor %xmm2,%xmm2");
		asm volatile("pxor %xmm8,%xmm8");
		asm volatile("pxor %xmm9,%xmm9");
		asm volatile("pxor %xmm10,%xmm10");
		for(d=l;d>=0;--d) {
			asm volatile("movdqa %0,%%xmm4" : : "m" (vbuf[d][off]));
			asm volatile("movdqa %0,%%xmm12" : : "m" (vbuf[d][off+16]));

			asm volatile("pxor %xmm5,%xmm5");
			asm volatile("pxor %xmm13,%xmm13");
			asm volatile("pcmpgtb %xmm1,%xmm5");
			asm volatile("pcmpgtb %xmm9,%xmm13");
			asm volatile("paddb %xmm1,%xmm1");
			asm volatile("paddb %xmm9,%xmm9");
			asm volatile("pand %xmm3,%xmm5");
			asm volatile("pand %xmm3,%xmm13");
			asm volatile("pxor %xmm5,%xmm1");
			asm volatile("pxor %xmm13,%xmm9");

			asm volatile("pxor %xmm4,%xmm0");
			asm volatile("pxor %xmm4,%xmm1");
			asm volatile("pxor %xmm12,%xmm8");
			asm volatile("pxor %xmm12,%xmm9");

			asm volatile("movdqa %xmm4,%xmm5");
			asm volatile("movdqa %xmm12,%xmm13");
			asm volatile("psraw  $4,%xmm5");
			asm volatile("psraw  $4,%xmm13");
			asm volatile("pand   %xmm11,%xmm4");
			asm volatile("pand   %xmm11,%xmm12");
			asm volatile("pand   %xmm11,%xmm5");
			asm volatile("pand   %xmm11,%xmm13");

			asm volatile("movdqa %0,%%xmm6" : : "m" (gfpshufb[d][0][0][0]));
			asm volatile("movdqa %0,%%xmm7" : : "m" (gfpshufb[d][0][1][0]));
			asm volatile("movdqa %xmm6,%xmm14");
			asm volatile("movdqa %xmm7,%xmm15");
			asm volatile("pshufb %xmm4,%xmm6");
			asm volatile("pshufb %xmm12,%xmm14");
			asm volatile("pshufb %xmm5,%xmm7");
			asm volatile("pshufb %xmm13,%xmm15");
			asm volatile("pxor   %xmm6,%xmm2");
			asm volatile("pxor   %xmm14,%xmm10");
			asm volatile("pxor   %xmm7,%xmm2");
			asm volatile("pxor   %xmm15,%xmm10");
		}
		asm volatile("movntdq %%xmm0,%0" : "=m" (p[off]));
		asm volatile("movntdq %%xmm8,%0" : "=m" (p[off+16]));
		asm volatile("movntdq %%xmm1,%0" : "=m" (q[off]));
		asm volatile("movntdq %%xmm9,%0" : "=m" (q[off+16]));
		asm volatile("movntdq %%xmm2,%0" : "=m" (r[off]));
		asm volatile("movntdq %%xmm10,%0" : "=m" (r[off+16]));
	}

	asm volatile("sfence" : : : "memory");
}
#endif

/*
 * RAIDQP 8bit C implementation
 * 
 * Note that instead of a generic multiplicationt table, likely resulting in multiple cache misses,
 * a precomputed table could be used. But this is only a kind of reference function,
 * and we are not really interested on speed.
 */
void raidQP_int8(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	unsigned char* s;
	int d, l;
	unsigned off;

	uint8_t d0, s0, r0, q0, p0;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];
	r = vbuf[data+2];
	s = vbuf[data+3];

	for(off=0;off<size;off+=1) {
		s0 = r0 = q0 = p0 = 0;
		for(d=l;d>=0;--d) {
			d0 = v_8(vbuf[d][off]);

			p0 ^= d0;
			q0 ^= gfmul[d0][gfmatrix[1][d]];
			r0 ^= gfmul[d0][gfmatrix[2][d]];
			s0 ^= gfmul[d0][gfmatrix[3][d]];
		}
		v_8(p[off]) = p0;
		v_8(q[off]) = q0;
		v_8(r[off]) = r0;
		v_8(s[off]) = s0;
	}
}

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
	unsigned off;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];
	r = vbuf[data+2];
	s = vbuf[data+3];

	for(off=0;off<size;off+=16) {
		asm volatile("pxor %xmm0,%xmm0");
		asm volatile("pxor %xmm1,%xmm1");
		asm volatile("pxor %xmm2,%xmm2");
		asm volatile("pxor %xmm3,%xmm3");
		for(d=l;d>=0;--d) {
			asm volatile("movdqa %0,%%xmm7" : : "m" (raid_16_const.poly[0]));
			asm volatile("movdqa %0,%%xmm4" : : "m" (vbuf[d][off]));

			asm volatile("pxor %xmm5,%xmm5");
			asm volatile("pcmpgtb %xmm1,%xmm5");
			asm volatile("paddb %xmm1,%xmm1");
			asm volatile("pand %xmm7,%xmm5");
			asm volatile("pxor %xmm5,%xmm1");

			asm volatile("movdqa %0,%%xmm7" : : "m" (raid_16_const.mask[0]));

			asm volatile("pxor %xmm4,%xmm0");
			asm volatile("pxor %xmm4,%xmm1");

			asm volatile("movdqa %xmm4,%xmm5");
			asm volatile("psraw  $4,%xmm5");
			asm volatile("pand   %xmm7,%xmm4");
			asm volatile("pand   %xmm7,%xmm5");

			asm volatile("movdqa %0,%%xmm6" : : "m" (gfpshufb[d][0][0][0]));
			asm volatile("movdqa %0,%%xmm7" : : "m" (gfpshufb[d][0][1][0]));
			asm volatile("pshufb %xmm4,%xmm6");
			asm volatile("pshufb %xmm5,%xmm7");
			asm volatile("pxor   %xmm6,%xmm2");
			asm volatile("pxor   %xmm7,%xmm2");

			asm volatile("movdqa %0,%%xmm6" : : "m" (gfpshufb[d][1][0][0]));
			asm volatile("movdqa %0,%%xmm7" : : "m" (gfpshufb[d][1][1][0]));
			asm volatile("pshufb %xmm4,%xmm6");
			asm volatile("pshufb %xmm5,%xmm7");
			asm volatile("pxor   %xmm6,%xmm3");
			asm volatile("pxor   %xmm7,%xmm3");
		}
		asm volatile("movntdq %%xmm0,%0" : "=m" (p[off]));
		asm volatile("movntdq %%xmm1,%0" : "=m" (q[off]));
		asm volatile("movntdq %%xmm2,%0" : "=m" (r[off]));
		asm volatile("movntdq %%xmm3,%0" : "=m" (s[off]));
	}

	asm volatile("sfence" : : : "memory");
}
#endif

#if defined(__x86_64__)
/*
 * RAIDQP SSSE3 implementation
 *
 * Note that it uses 16 registers, meaning that x64 is required.
 */
void raidQP_ssse3ext(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	unsigned char* s;
	int d, l;
	unsigned off;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];
	r = vbuf[data+2];
	s = vbuf[data+3];

	for(off=0;off<size;off+=32) {
		asm volatile("pxor %xmm0,%xmm0");
		asm volatile("pxor %xmm1,%xmm1");
		asm volatile("pxor %xmm2,%xmm2");
		asm volatile("pxor %xmm3,%xmm3");
		asm volatile("pxor %xmm8,%xmm8");
		asm volatile("pxor %xmm9,%xmm9");
		asm volatile("pxor %xmm10,%xmm10");
		asm volatile("pxor %xmm11,%xmm11");
		for(d=l;d>=0;--d) {
			asm volatile("movdqa %0,%%xmm7" : : "m" (raid_16_const.poly[0]));
			asm volatile("movdqa %0,%%xmm15" : : "m" (raid_16_const.mask[0]));
			asm volatile("movdqa %0,%%xmm4" : : "m" (vbuf[d][off]));
			asm volatile("movdqa %0,%%xmm12" : : "m" (vbuf[d][off+16]));

			asm volatile("pxor %xmm5,%xmm5");
			asm volatile("pxor %xmm13,%xmm13");
			asm volatile("pcmpgtb %xmm1,%xmm5");
			asm volatile("pcmpgtb %xmm9,%xmm13");
			asm volatile("paddb %xmm1,%xmm1");
			asm volatile("paddb %xmm9,%xmm9");
			asm volatile("pand %xmm7,%xmm5");
			asm volatile("pand %xmm7,%xmm13");
			asm volatile("pxor %xmm5,%xmm1");
			asm volatile("pxor %xmm13,%xmm9");

			asm volatile("pxor %xmm4,%xmm0");
			asm volatile("pxor %xmm4,%xmm1");
			asm volatile("pxor %xmm12,%xmm8");
			asm volatile("pxor %xmm12,%xmm9");

			asm volatile("movdqa %xmm4,%xmm5");
			asm volatile("movdqa %xmm12,%xmm13");
			asm volatile("psraw  $4,%xmm5");
			asm volatile("psraw  $4,%xmm13");
			asm volatile("pand   %xmm15,%xmm4");
			asm volatile("pand   %xmm15,%xmm12");
			asm volatile("pand   %xmm15,%xmm5");
			asm volatile("pand   %xmm15,%xmm13");

			asm volatile("movdqa %0,%%xmm6" : : "m" (gfpshufb[d][0][0][0]));
			asm volatile("movdqa %0,%%xmm7" : : "m" (gfpshufb[d][0][1][0]));
			asm volatile("movdqa %xmm6,%xmm14");
			asm volatile("movdqa %xmm7,%xmm15");
			asm volatile("pshufb %xmm4,%xmm6");
			asm volatile("pshufb %xmm12,%xmm14");
			asm volatile("pshufb %xmm5,%xmm7");
			asm volatile("pshufb %xmm13,%xmm15");
			asm volatile("pxor   %xmm6,%xmm2");
			asm volatile("pxor   %xmm14,%xmm10");
			asm volatile("pxor   %xmm7,%xmm2");
			asm volatile("pxor   %xmm15,%xmm10");

			asm volatile("movdqa %0,%%xmm6" : : "m" (gfpshufb[d][1][0][0]));
			asm volatile("movdqa %0,%%xmm7" : : "m" (gfpshufb[d][1][1][0]));
			asm volatile("movdqa %xmm6,%xmm14");
			asm volatile("movdqa %xmm7,%xmm15");
			asm volatile("pshufb %xmm4,%xmm6");
			asm volatile("pshufb %xmm12,%xmm14");
			asm volatile("pshufb %xmm5,%xmm7");
			asm volatile("pshufb %xmm13,%xmm15");
			asm volatile("pxor   %xmm6,%xmm3");
			asm volatile("pxor   %xmm14,%xmm11");
			asm volatile("pxor   %xmm7,%xmm3");
			asm volatile("pxor   %xmm15,%xmm11");
		}
		asm volatile("movntdq %%xmm0,%0" : "=m" (p[off]));
		asm volatile("movntdq %%xmm8,%0" : "=m" (p[off+16]));
		asm volatile("movntdq %%xmm1,%0" : "=m" (q[off]));
		asm volatile("movntdq %%xmm9,%0" : "=m" (q[off+16]));
		asm volatile("movntdq %%xmm2,%0" : "=m" (r[off]));
		asm volatile("movntdq %%xmm10,%0" : "=m" (r[off+16]));
		asm volatile("movntdq %%xmm3,%0" : "=m" (s[off]));
		asm volatile("movntdq %%xmm11,%0" : "=m" (s[off+16]));
	}

	asm volatile("sfence" : : : "memory");
}
#endif

/*
 * RAIDPP 8bit C implementation
 * 
 * Note that instead of a generic multiplicationt table, likely resulting in multiple cache misses,
 * a precomputed table could be used. But this is only a kind of reference function,
 * and we are not really interested on speed.
 */
void raidPP_int8(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	unsigned char* s;
	unsigned char* t;
	int d, l;
	unsigned off;

	uint8_t d0, t0, s0, r0, q0, p0;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];
	r = vbuf[data+2];
	s = vbuf[data+3];
	t = vbuf[data+4];

	for(off=0;off<size;off+=1) {
		t0 = s0 = r0 = q0 = p0 = 0;
		for(d=l;d>=0;--d) {
			d0 = v_8(vbuf[d][off]);

			p0 ^= d0;
			q0 ^= gfmul[d0][gfmatrix[1][d]];
			r0 ^= gfmul[d0][gfmatrix[2][d]];
			s0 ^= gfmul[d0][gfmatrix[3][d]];
			t0 ^= gfmul[d0][gfmatrix[4][d]];
		}
		v_8(p[off]) = p0;
		v_8(q[off]) = q0;
		v_8(r[off]) = r0;
		v_8(s[off]) = s0;
		v_8(t[off]) = t0;
	}
}

#if defined(__i386__) || defined(__x86_64__)
/*
 * RAIDPP SSSE3 implementation
 */
#ifdef _WIN32
/* ensures that stack is aligned at 16 bytes because we allocate SSE registers in it */
__attribute__((force_align_arg_pointer))
#endif
void raidPP_ssse3(unsigned char** vbuf, unsigned data, unsigned size)
/* ensures that stack is aligned at 16 bytes because we allocate SSE registers in it */
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	unsigned char* s;
	unsigned char* t;
	int d, l;
	unsigned off;
	unsigned char p0[16] __attribute__((aligned(16)));

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];
	r = vbuf[data+2];
	s = vbuf[data+3];
	t = vbuf[data+4];

	for(off=0;off<size;off+=16) {
		asm volatile("pxor %xmm0,%xmm0");
		asm volatile("pxor %xmm1,%xmm1");
		asm volatile("pxor %xmm2,%xmm2");
		asm volatile("pxor %xmm3,%xmm3");
		asm volatile("movdqa %%xmm0,%0" : "=m" (p0[0]));
		for(d=l;d>=0;--d) {
			asm volatile("movdqa %0,%%xmm4" : : "m" (vbuf[d][off]));
			asm volatile("movdqa %0,%%xmm6" : : "m" (p0[0]));
			asm volatile("movdqa %0,%%xmm7" : : "m" (raid_16_const.poly[0]));

			asm volatile("pxor %xmm5,%xmm5");
			asm volatile("pcmpgtb %xmm0,%xmm5");
			asm volatile("paddb %xmm0,%xmm0");
			asm volatile("pand %xmm7,%xmm5");
			asm volatile("pxor %xmm5,%xmm0");

			asm volatile("pxor %xmm4,%xmm0");
			asm volatile("pxor %xmm4,%xmm6");
			asm volatile("movdqa %%xmm6,%0" : "=m" (p0));

			asm volatile("movdqa %0,%%xmm7" : : "m" (raid_16_const.mask[0]));
			asm volatile("movdqa %xmm4,%xmm5");
			asm volatile("psraw  $4,%xmm5");
			asm volatile("pand   %xmm7,%xmm4");
			asm volatile("pand   %xmm7,%xmm5");

			asm volatile("movdqa %0,%%xmm6" : : "m" (gfpshufb[d][0][0][0]));
			asm volatile("movdqa %0,%%xmm7" : : "m" (gfpshufb[d][0][1][0]));
			asm volatile("pshufb %xmm4,%xmm6");
			asm volatile("pshufb %xmm5,%xmm7");
			asm volatile("pxor   %xmm6,%xmm1");
			asm volatile("pxor   %xmm7,%xmm1");

			asm volatile("movdqa %0,%%xmm6" : : "m" (gfpshufb[d][1][0][0]));
			asm volatile("movdqa %0,%%xmm7" : : "m" (gfpshufb[d][1][1][0]));
			asm volatile("pshufb %xmm4,%xmm6");
			asm volatile("pshufb %xmm5,%xmm7");
			asm volatile("pxor   %xmm6,%xmm2");
			asm volatile("pxor   %xmm7,%xmm2");

			asm volatile("movdqa %0,%%xmm6" : : "m" (gfpshufb[d][2][0][0]));
			asm volatile("movdqa %0,%%xmm7" : : "m" (gfpshufb[d][2][1][0]));
			asm volatile("pshufb %xmm4,%xmm6");
			asm volatile("pshufb %xmm5,%xmm7");
			asm volatile("pxor   %xmm6,%xmm3");
			asm volatile("pxor   %xmm7,%xmm3");
		}
		asm volatile("movdqa %0,%%xmm6" : : "m" (p0[0]));
		asm volatile("movntdq %%xmm6,%0" : "=m" (p[off]));
		asm volatile("movntdq %%xmm0,%0" : "=m" (q[off]));
		asm volatile("movntdq %%xmm1,%0" : "=m" (r[off]));
		asm volatile("movntdq %%xmm2,%0" : "=m" (s[off]));
		asm volatile("movntdq %%xmm3,%0" : "=m" (t[off]));
	}

	asm volatile("sfence" : : : "memory");
}
#endif

#if defined(__x86_64__)
/*
 * RAIDPP SSSE3 implementation
 *
 * Note that it uses 16 registers, meaning that x64 is required.
 */
void raidPP_ssse3ext(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	unsigned char* s;
	unsigned char* t;
	int d, l;
	unsigned off;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];
	r = vbuf[data+2];
	s = vbuf[data+3];
	t = vbuf[data+4];

	asm volatile("movdqa %0,%%xmm14" : : "m" (raid_16_const.poly[0]));
	asm volatile("movdqa %0,%%xmm15" : : "m" (raid_16_const.mask[0]));

	for(off=0;off<size;off+=16) {
		asm volatile("pxor %xmm0,%xmm0");
		asm volatile("pxor %xmm1,%xmm1");
		asm volatile("pxor %xmm2,%xmm2");
		asm volatile("pxor %xmm3,%xmm3");
		asm volatile("pxor %xmm4,%xmm4");
		for(d=l;d>=0;--d) {
			asm volatile("movdqa %0,%%xmm10" : : "m" (vbuf[d][off]));

			asm volatile("pxor %xmm11,%xmm11");
			asm volatile("pcmpgtb %xmm1,%xmm11");
			asm volatile("paddb %xmm1,%xmm1");
			asm volatile("pand %xmm14,%xmm11");
			asm volatile("pxor %xmm11,%xmm1");

			asm volatile("pxor %xmm10,%xmm0");
			asm volatile("pxor %xmm10,%xmm1");

			asm volatile("movdqa %xmm10,%xmm11");
			asm volatile("psraw  $4,%xmm11");
			asm volatile("pand   %xmm15,%xmm10");
			asm volatile("pand   %xmm15,%xmm11");

			asm volatile("movdqa %0,%%xmm12" : : "m" (gfpshufb[d][0][0][0]));
			asm volatile("movdqa %0,%%xmm13" : : "m" (gfpshufb[d][0][1][0]));
			asm volatile("pshufb %xmm10,%xmm12");
			asm volatile("pshufb %xmm11,%xmm13");
			asm volatile("pxor   %xmm12,%xmm2");
			asm volatile("pxor   %xmm13,%xmm2");

			asm volatile("movdqa %0,%%xmm12" : : "m" (gfpshufb[d][1][0][0]));
			asm volatile("movdqa %0,%%xmm13" : : "m" (gfpshufb[d][1][1][0]));
			asm volatile("pshufb %xmm10,%xmm12");
			asm volatile("pshufb %xmm11,%xmm13");
			asm volatile("pxor   %xmm12,%xmm3");
			asm volatile("pxor   %xmm13,%xmm3");

			asm volatile("movdqa %0,%%xmm12" : : "m" (gfpshufb[d][2][0][0]));
			asm volatile("movdqa %0,%%xmm13" : : "m" (gfpshufb[d][2][1][0]));
			asm volatile("pshufb %xmm10,%xmm12");
			asm volatile("pshufb %xmm11,%xmm13");
			asm volatile("pxor   %xmm12,%xmm4");
			asm volatile("pxor   %xmm13,%xmm4");
		}
		asm volatile("movntdq %%xmm0,%0" : "=m" (p[off]));
		asm volatile("movntdq %%xmm1,%0" : "=m" (q[off]));
		asm volatile("movntdq %%xmm2,%0" : "=m" (r[off]));
		asm volatile("movntdq %%xmm3,%0" : "=m" (s[off]));
		asm volatile("movntdq %%xmm4,%0" : "=m" (t[off]));
	}

	asm volatile("sfence" : : : "memory");
}
#endif

/*
 * RAIDHP 8bit C implementation
 * 
 * Note that instead of a generic multiplicationt table, likely resulting in multiple cache misses,
 * a precomputed table could be used. But this is only a kind of reference function,
 * and we are not really interested on speed.
 */
void raidHP_int8(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	unsigned char* s;
	unsigned char* t;
	unsigned char* o;
	int d, l;
	unsigned off;

	uint8_t d0, u0, t0, s0, r0, q0, p0;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];
	r = vbuf[data+2];
	s = vbuf[data+3];
	t = vbuf[data+4];
	o = vbuf[data+5];

	for(off=0;off<size;off+=1) {
		u0 = t0 = s0 = r0 = q0 = p0 = 0;
		for(d=l;d>=0;--d) {
			d0 = v_8(vbuf[d][off]);

			p0 ^= d0;
			q0 ^= gfmul[d0][gfmatrix[1][d]];
			r0 ^= gfmul[d0][gfmatrix[2][d]];
			s0 ^= gfmul[d0][gfmatrix[3][d]];
			t0 ^= gfmul[d0][gfmatrix[4][d]];
			u0 ^= gfmul[d0][gfmatrix[5][d]];
		}
		v_8(p[off]) = p0;
		v_8(q[off]) = q0;
		v_8(r[off]) = r0;
		v_8(s[off]) = s0;
		v_8(t[off]) = t0;
		v_8(o[off]) = u0;
	}
}

#if defined(__i386__) || defined(__x86_64__)
/*
 * RAIDHP SSSE3 implementation
 */
#ifdef _WIN32
/* ensures that stack is aligned at 16 bytes because we allocate SSE registers in it */
__attribute__((force_align_arg_pointer))
#endif
void raidHP_ssse3(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	unsigned char* s;
	unsigned char* t;
	unsigned char* o;
	int d, l;
	unsigned off;
	unsigned char p0[16] __attribute__((aligned(16)));
	unsigned char q0[16] __attribute__((aligned(16))); 

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];
	r = vbuf[data+2];
	s = vbuf[data+3];
	t = vbuf[data+4];
	o = vbuf[data+5];

	for(off=0;off<size;off+=16) {
		asm volatile("pxor %xmm0,%xmm0");
		asm volatile("pxor %xmm1,%xmm1");
		asm volatile("pxor %xmm2,%xmm2");
		asm volatile("pxor %xmm3,%xmm3");
		asm volatile("movdqa %%xmm0,%0" : "=m" (p0[0]));
		asm volatile("movdqa %%xmm0,%0" : "=m" (q0[0]));
		for(d=l;d>=0;--d) {
			asm volatile("movdqa %0,%%xmm5" : : "m" (p0[0]));
			asm volatile("movdqa %0,%%xmm6" : : "m" (q0[0]));
			asm volatile("movdqa %0,%%xmm7" : : "m" (raid_16_const.poly[0]));

			asm volatile("pxor %xmm4,%xmm4");
			asm volatile("pcmpgtb %xmm6,%xmm4");
			asm volatile("paddb %xmm6,%xmm6");
			asm volatile("pand %xmm7,%xmm4");
			asm volatile("pxor %xmm4,%xmm6");

			asm volatile("movdqa %0,%%xmm4" : : "m" (vbuf[d][off]));

			asm volatile("pxor %xmm4,%xmm5");
			asm volatile("pxor %xmm4,%xmm6");
			asm volatile("movdqa %%xmm5,%0" : "=m" (p0));
			asm volatile("movdqa %%xmm6,%0" : "=m" (q0));

			asm volatile("movdqa %0,%%xmm7" : : "m" (raid_16_const.mask[0]));
			asm volatile("movdqa %xmm4,%xmm5");
			asm volatile("psraw  $4,%xmm5");
			asm volatile("pand   %xmm7,%xmm4");
			asm volatile("pand   %xmm7,%xmm5");

			asm volatile("movdqa %0,%%xmm6" : : "m" (gfpshufb[d][0][0][0]));
			asm volatile("movdqa %0,%%xmm7" : : "m" (gfpshufb[d][0][1][0]));
			asm volatile("pshufb %xmm4,%xmm6");
			asm volatile("pshufb %xmm5,%xmm7");
			asm volatile("pxor   %xmm6,%xmm0");
			asm volatile("pxor   %xmm7,%xmm0");

			asm volatile("movdqa %0,%%xmm6" : : "m" (gfpshufb[d][1][0][0]));
			asm volatile("movdqa %0,%%xmm7" : : "m" (gfpshufb[d][1][1][0]));
			asm volatile("pshufb %xmm4,%xmm6");
			asm volatile("pshufb %xmm5,%xmm7");
			asm volatile("pxor   %xmm6,%xmm1");
			asm volatile("pxor   %xmm7,%xmm1");

			asm volatile("movdqa %0,%%xmm6" : : "m" (gfpshufb[d][2][0][0]));
			asm volatile("movdqa %0,%%xmm7" : : "m" (gfpshufb[d][2][1][0]));
			asm volatile("pshufb %xmm4,%xmm6");
			asm volatile("pshufb %xmm5,%xmm7");
			asm volatile("pxor   %xmm6,%xmm2");
			asm volatile("pxor   %xmm7,%xmm2");

			asm volatile("movdqa %0,%%xmm6" : : "m" (gfpshufb[d][3][0][0]));
			asm volatile("movdqa %0,%%xmm7" : : "m" (gfpshufb[d][3][1][0]));
			asm volatile("pshufb %xmm4,%xmm6");
			asm volatile("pshufb %xmm5,%xmm7");
			asm volatile("pxor   %xmm6,%xmm3");
			asm volatile("pxor   %xmm7,%xmm3");
		}
		asm volatile("movdqa %0,%%xmm5" : : "m" (p0[0]));
		asm volatile("movdqa %0,%%xmm6" : : "m" (q0[0]));
		asm volatile("movntdq %%xmm5,%0" : "=m" (p[off]));
		asm volatile("movntdq %%xmm6,%0" : "=m" (q[off]));
		asm volatile("movntdq %%xmm0,%0" : "=m" (r[off]));
		asm volatile("movntdq %%xmm1,%0" : "=m" (s[off]));
		asm volatile("movntdq %%xmm2,%0" : "=m" (t[off]));
		asm volatile("movntdq %%xmm3,%0" : "=m" (o[off]));
	}

	asm volatile("sfence" : : : "memory");
}
#endif

#if defined(__x86_64__)
/*
 * RAIDHP SSSE3 implementation
 *
 * Note that it uses 16 registers, meaning that x64 is required.
 */
void raidHP_ssse3ext(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	unsigned char* s;
	unsigned char* t;
	unsigned char* o;
	int d, l;
	unsigned off;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];
	r = vbuf[data+2];
	s = vbuf[data+3];
	t = vbuf[data+4];
	o = vbuf[data+5];

	asm volatile("movdqa %0,%%xmm14" : : "m" (raid_16_const.poly[0]));
	asm volatile("movdqa %0,%%xmm15" : : "m" (raid_16_const.mask[0]));

	for(off=0;off<size;off+=16) {
		asm volatile("pxor %xmm0,%xmm0");
		asm volatile("pxor %xmm1,%xmm1");
		asm volatile("pxor %xmm2,%xmm2");
		asm volatile("pxor %xmm3,%xmm3");
		asm volatile("pxor %xmm4,%xmm4");
		asm volatile("pxor %xmm5,%xmm5");
		for(d=l;d>=0;--d) {
			asm volatile("movdqa %0,%%xmm10" : : "m" (vbuf[d][off]));

			asm volatile("pxor %xmm11,%xmm11");
			asm volatile("pcmpgtb %xmm1,%xmm11");
			asm volatile("paddb %xmm1,%xmm1");
			asm volatile("pand %xmm14,%xmm11");
			asm volatile("pxor %xmm11,%xmm1");

			asm volatile("pxor %xmm10,%xmm0");
			asm volatile("pxor %xmm10,%xmm1");

			asm volatile("movdqa %xmm10,%xmm11");
			asm volatile("psraw  $4,%xmm11");
			asm volatile("pand   %xmm15,%xmm10");
			asm volatile("pand   %xmm15,%xmm11");

			asm volatile("movdqa %0,%%xmm12" : : "m" (gfpshufb[d][0][0][0]));
			asm volatile("movdqa %0,%%xmm13" : : "m" (gfpshufb[d][0][1][0]));
			asm volatile("pshufb %xmm10,%xmm12");
			asm volatile("pshufb %xmm11,%xmm13");
			asm volatile("pxor   %xmm12,%xmm2");
			asm volatile("pxor   %xmm13,%xmm2");

			asm volatile("movdqa %0,%%xmm12" : : "m" (gfpshufb[d][1][0][0]));
			asm volatile("movdqa %0,%%xmm13" : : "m" (gfpshufb[d][1][1][0]));
			asm volatile("pshufb %xmm10,%xmm12");
			asm volatile("pshufb %xmm11,%xmm13");
			asm volatile("pxor   %xmm12,%xmm3");
			asm volatile("pxor   %xmm13,%xmm3");

			asm volatile("movdqa %0,%%xmm12" : : "m" (gfpshufb[d][2][0][0]));
			asm volatile("movdqa %0,%%xmm13" : : "m" (gfpshufb[d][2][1][0]));
			asm volatile("pshufb %xmm10,%xmm12");
			asm volatile("pshufb %xmm11,%xmm13");
			asm volatile("pxor   %xmm12,%xmm4");
			asm volatile("pxor   %xmm13,%xmm4");

			asm volatile("movdqa %0,%%xmm12" : : "m" (gfpshufb[d][3][0][0]));
			asm volatile("movdqa %0,%%xmm13" : : "m" (gfpshufb[d][3][1][0]));
			asm volatile("pshufb %xmm10,%xmm12");
			asm volatile("pshufb %xmm11,%xmm13");
			asm volatile("pxor   %xmm12,%xmm5");
			asm volatile("pxor   %xmm13,%xmm5");
		}
		asm volatile("movntdq %%xmm0,%0" : "=m" (p[off]));
		asm volatile("movntdq %%xmm1,%0" : "=m" (q[off]));
		asm volatile("movntdq %%xmm2,%0" : "=m" (r[off]));
		asm volatile("movntdq %%xmm3,%0" : "=m" (s[off]));
		asm volatile("movntdq %%xmm4,%0" : "=m" (t[off]));
		asm volatile("movntdq %%xmm5,%0" : "=m" (o[off]));
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
static void (*raidPP_gen)(unsigned char** vbuf, unsigned data, unsigned size);
static void (*raidHP_gen)(unsigned char** vbuf, unsigned data, unsigned size);

void raid_gen(unsigned level, unsigned char** vbuf, unsigned data, unsigned size)
{
	switch (level) {
	case 1 : raid5_gen(vbuf, data, size); break;
	case 2 : raid6_gen(vbuf, data, size); break;
	case 3 : raidTP_gen(vbuf, data, size); break;
	case 4 : raidQP_gen(vbuf, data, size); break;
	case 5 : raidPP_gen(vbuf, data, size); break;
	case 6 : raidHP_gen(vbuf, data, size); break;
	default:
		fprintf(stderr, "Invalid raid gen level\n");
		exit(EXIT_FAILURE);
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
static void raid_delta_2data(unsigned int x, int y, int i, int j, unsigned char** vbuf, unsigned data, unsigned char* zero, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* pa;
	unsigned char* qa;

	/* parity buffer */
	p = vbuf[data+i];
	q = vbuf[data+j];

	/* missing data blocks */
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
static void raid_delta_3data(unsigned int x, int y, int z, int i, int j, int k, unsigned char** vbuf, unsigned data, unsigned char* zero, unsigned size)
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

	/* missing data blocks */
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
static void raid_delta_4data(unsigned int x, int y, int z, int v, int i, int j, int k, int l, unsigned char** vbuf, unsigned data, unsigned char* zero, unsigned size)
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

	/* missing data blocks */
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
 * Computes the parity without the missing data blocks
 * and store it in the buffers of such data blocks.
 */
static void raid_delta_5data(unsigned int x, int y, int z, int v, int u, int i, int j, int k, int l, int m, unsigned char** vbuf, unsigned data, unsigned char* zero, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	unsigned char* s;
	unsigned char* t;
	unsigned char* pa;
	unsigned char* qa;
	unsigned char* ra;
	unsigned char* sa;
	unsigned char* ta;

	/* parity buffer */
	p = vbuf[data+i];
	q = vbuf[data+j];
	r = vbuf[data+k];
	s = vbuf[data+l];
	t = vbuf[data+m];

	/* missing data blocks */
	pa = vbuf[x];
	qa = vbuf[y];
	ra = vbuf[z];
	sa = vbuf[v];
	ta = vbuf[u];

	/* set at zero the missing data blocks */
	vbuf[x] = zero;
	vbuf[y] = zero;
	vbuf[z] = zero;
	vbuf[v] = zero;
	vbuf[u] = zero;

	/* compute the parity over the missing data blocks */
	vbuf[data+i] = pa;
	vbuf[data+j] = qa;
	vbuf[data+k] = ra;
	vbuf[data+l] = sa;
	vbuf[data+m] = ta;

	/* compute only for the level we really need */
	raid_gen(m + 1, vbuf, data, size);

	/* restore as before */
	vbuf[x] = pa;
	vbuf[y] = qa;
	vbuf[z] = ra;
	vbuf[v] = sa;
	vbuf[u] = ta;
	vbuf[data+i] = p;
	vbuf[data+j] = q;
	vbuf[data+k] = r;
	vbuf[data+l] = s;
	vbuf[data+m] = t;
}

/**
 * Computes the parity without the missing data blocks
 * and store it in the buffers of such data blocks.
 */
static void raid_delta_6data(unsigned int x, int y, int z, int v, int u, int w, int i, int j, int k, int l, int m, int n, unsigned char** vbuf, unsigned data, unsigned char* zero, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	unsigned char* s;
	unsigned char* t;
	unsigned char* o;
	unsigned char* pa;
	unsigned char* qa;
	unsigned char* ra;
	unsigned char* sa;
	unsigned char* ta;
	unsigned char* oa;

	/* parity buffer */
	p = vbuf[data+i];
	q = vbuf[data+j];
	r = vbuf[data+k];
	s = vbuf[data+l];
	t = vbuf[data+m];
	o = vbuf[data+n];

	/* missing data blocks */
	pa = vbuf[x];
	qa = vbuf[y];
	ra = vbuf[z];
	sa = vbuf[v];
	ta = vbuf[u];
	oa = vbuf[w];

	/* set at zero the missing data blocks */
	vbuf[x] = zero;
	vbuf[y] = zero;
	vbuf[z] = zero;
	vbuf[v] = zero;
	vbuf[u] = zero;
	vbuf[w] = zero;

	/* compute the parity over the missing data blocks */
	vbuf[data+i] = pa;
	vbuf[data+j] = qa;
	vbuf[data+k] = ra;
	vbuf[data+l] = sa;
	vbuf[data+m] = ta;
	vbuf[data+n] = oa;

	/* compute only for the level we really need */
	raid_gen(n + 1, vbuf, data, size);

	/* restore as before */
	vbuf[x] = pa;
	vbuf[y] = qa;
	vbuf[z] = ra;
	vbuf[v] = sa;
	vbuf[u] = ta;
	vbuf[w] = oa;
	vbuf[data+i] = p;
	vbuf[data+j] = q;
	vbuf[data+k] = r;
	vbuf[data+l] = s;
	vbuf[data+m] = t;
	vbuf[data+n] = o;
}

/****************************************************************************/
/* specialized raid recovering */

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

/****************************************************************************/
/* generic matrix recovering */

/**
 * Gets the generator matrix coefficient for parity 'p' and disk 'd'.
 */
static unsigned char A(unsigned p, unsigned d)
{
	return gfmatrix[p][d];
}

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
 * Pd = A[i,x] * Dx
 *
 * and solving we get:
 *
 * Dx = A[i,x]^-1 * Pd
 */
static void raid_recov_1data(int x, int i, unsigned char** vbuf, unsigned data, unsigned char* zero, unsigned size)
{
	unsigned off;
	unsigned char* p;
	unsigned char* pa;
	const unsigned char* pxf; /* P factor to compute Dx */

	/* if it's RAID5 uses the dedicated and faster function */
	if (i == 0) {
		raid5_recov_data(x, vbuf, data, size);
		return;
	}

	/* select tables */
	pxf = table( inv(A(i,x)) );

	/* compute delta parity */
	raid_delta_1data(x, i, vbuf, data, zero, size);

	p = vbuf[data+i];
	pa = vbuf[x];

	for(off=0;off<size;++off) {
		/* delta */
		unsigned char Pd = p[off] ^ pa[off];

		/* addends to reconstruct Dx */
		unsigned char pxm = pxf[Pd];

		/* reconstruct Dx */
		unsigned char Dx = pxm;

		/* set */
		pa[off] = Dx;
	}
}

/**
 * Recover failure of two data blocks x,y using parity i,j for any RAID level.
 *
 * Starting from the equations:
 *
 * Pd = A[i,x] * Dx + A[i,y] * Dy
 * Qd = A[j,x] * Dx + A[j,y] * Dy
 *
 * we solve inverting the coefficients matrix.
 */
static void raid_recov_2data(int x, int y, int i, int j, unsigned char** vbuf, unsigned data, unsigned char* zero, unsigned size)
{
	unsigned off;
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
	unsigned N = 2;

	/* if it's RAID6 uses the dedicated and faster function */
	if (i == 0 && j == 1) {
		raid6_recov_2data(x, y, vbuf, data, zero, size);
		return;
	}

	/* setup the generator matrix */
	G[0*N+0] = A(i,x); /* row 1 for P */
	G[0*N+1] = A(i,y);
	G[1*N+0] = A(j,x); /* row 2 for Q */
	G[1*N+1] = A(j,y);

	/* invert it to solve the system of linear equations */
	invert(G, V, N);

	/* select tables */
	pxf = table( V[0*N+0] );
	qxf = table( V[0*N+1] );
	pyf = table( V[1*N+0] );
	qyf = table( V[1*N+1] );

	/* compute delta parity */
	raid_delta_2data(x, y, i, j, vbuf, data, zero, size);

	p = vbuf[data+i];
	q = vbuf[data+j];
	pa = vbuf[x];
	qa = vbuf[y];

	for(off=0;off<size;++off) {
		/* delta */
		unsigned char Pd = p[off] ^ pa[off];
		unsigned char Qd = q[off] ^ qa[off];

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
		pa[off] = Dx;
		qa[off] = Dy;
	}
}

/**
 * Recover failure of three data blocks x,y,z using parity i,j,k for any RAID level.
 *
 * Starting from the equations:
 *
 * Pd = A[i,x] * Dx + A[i,y] * Dy + A[i,z] * Dz
 * Qd = A[j,x] * Dx + A[j,y] * Dy + A[j,z] * Dz
 * Rd = A[k,x] * Dx + A[k,y] * Dy + A[k,z] * Dz
 *
 * we solve inverting the coefficients matrix.
 */
static void raid_recov_3data(int x, int y, int z, int i, int j, int k, unsigned char** vbuf, unsigned data, unsigned char* zero, unsigned size)
{
	unsigned off;
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
	unsigned N = 3;

	/* setup the generator matrix */
	G[0*N+0] = A(i,x); /* row 1 for P */
	G[0*N+1] = A(i,y);
	G[0*N+2] = A(i,z);
	G[1*N+0] = A(j,x); /* row 2 for Q */
	G[1*N+1] = A(j,y);
	G[1*N+2] = A(j,z);
	G[2*N+0] = A(k,x); /* row 3 for R */
	G[2*N+1] = A(k,y);
	G[2*N+2] = A(k,z);

	/* invert it to solve the system of linear equations */
	invert(G, V, N);

	/* select tables */
	pxf = table( V[0*N+0] );
	qxf = table( V[0*N+1] );
	rxf = table( V[0*N+2] );
	pyf = table( V[1*N+0] );
	qyf = table( V[1*N+1] );
	ryf = table( V[1*N+2] );
	pzf = table( V[2*N+0] );
	qzf = table( V[2*N+1] );
	rzf = table( V[2*N+2] );

	/* compute delta parity */
	raid_delta_3data(x, y, z, i, j, k, vbuf, data, zero, size);

	p = vbuf[data+i];
	q = vbuf[data+j];
	r = vbuf[data+k];
	pa = vbuf[x];
	qa = vbuf[y];
	ra = vbuf[z];

	for(off=0;off<size;++off) {
		/* delta */
		unsigned char Pd = p[off] ^ pa[off];
		unsigned char Qd = q[off] ^ qa[off];
		unsigned char Rd = r[off] ^ ra[off];

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
		pa[off] = Dx;
		qa[off] = Dy;
		ra[off] = Dz;
	}
}

/**
 * Recover failure of four data blocks x,y,z,v using parity i,j,k,l for any RAID level.
 *
 * Starting from the equations:
 *
 * Pd = A[i,x] * Dx + A[i,y] * Dy + A[i,z] * Dz + A[i,v] * Dv
 * Qd = A[j,x] * Dx + A[j,y] * Dy + A[j,z] * Dz + A[j,v] * Dv
 * Rd = A[k,x] * Dx + A[k,y] * Dy + A[k,z] * Dz + A[k,v] * Dv
 * Sd = A[l,x] * Dx + A[l,y] * Dy + A[l,z] * Dz + A[l,v] * Dv
 *
 * we solve inverting the coefficients matrix.
 */
static void raid_recov_4data(int x, int y, int z, int v, int i, int j, int k, int l, unsigned char** vbuf, unsigned data, unsigned char* zero, unsigned size)
{
	unsigned off;
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
	unsigned N = 4;

	/* setup the generator matrix */
	G[0*N+0] = A(i,x); /* row 1 for P */
	G[0*N+1] = A(i,y);
	G[0*N+2] = A(i,z);
	G[0*N+3] = A(i,v);
	G[1*N+0] = A(j,x); /* row 2 for Q */
	G[1*N+1] = A(j,y);
	G[1*N+2] = A(j,z);
	G[1*N+3] = A(j,v);
	G[2*N+0] = A(k,x); /* row 3 for R */
	G[2*N+1] = A(k,y);
	G[2*N+2] = A(k,z);
	G[2*N+3] = A(k,v);
	G[3*N+0] = A(l,x); /* row 4 for S */
	G[3*N+1] = A(l,y);
	G[3*N+2] = A(l,z);
	G[3*N+3] = A(l,v);

	/* invert it to solve the system of linear equations */
	invert(G, V, N);

	/* select tables */
	pxf = table( V[0*N+0] );
	qxf = table( V[0*N+1] );
	rxf = table( V[0*N+2] );
	sxf = table( V[0*N+3] );
	pyf = table( V[1*N+0] );
	qyf = table( V[1*N+1] );
	ryf = table( V[1*N+2] );
	syf = table( V[1*N+3] );
	pzf = table( V[2*N+0] );
	qzf = table( V[2*N+1] );
	rzf = table( V[2*N+2] );
	szf = table( V[2*N+3] );
	pvf = table( V[3*N+0] );
	qvf = table( V[3*N+1] );
	rvf = table( V[3*N+2] );
	svf = table( V[3*N+3] );

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

	for(off=0;off<size;++off) {
		/* delta */
		unsigned char Pd = p[off] ^ pa[off];
		unsigned char Qd = q[off] ^ qa[off];
		unsigned char Rd = r[off] ^ ra[off];
		unsigned char Sd = s[off] ^ sa[off];

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
		pa[off] = Dx;
		qa[off] = Dy;
		ra[off] = Dz;
		sa[off] = Dv;
	}
}

/**
 * Recover failure of five data blocks x,y,z,v,u using parity i,j,k,l,m for any RAID level.
 *
 * Starting from the equations:
 *
 * Pd = A[i,x] * Dx + A[i,y] * Dy + A[i,z] * Dz + A[i,v] * Dv + A[i,u] * Du
 * Qd = A[j,x] * Dx + A[j,y] * Dy + A[j,z] * Dz + A[j,v] * Dv + A[j,u] * Du
 * Rd = A[k,x] * Dx + A[k,y] * Dy + A[k,z] * Dz + A[k,v] * Dv + A[k,u] * Du
 * Sd = A[l,x] * Dx + A[l,y] * Dy + A[l,z] * Dz + A[l,v] * Dv + A[l,u] * Du
 * Td = A[m,x] * Dx + A[m,y] * Dy + A[m,z] * Dz + A[m,v] * Dv + A[m,u] * Du
 *
 * we solve inverting the coefficients matrix.
 */
static void raid_recov_5data(int x, int y, int z, int v, int u, int i, int j, int k, int l, int m, unsigned char** vbuf, unsigned data, unsigned char* zero, unsigned size)
{
	unsigned off;
	unsigned char* p;
	unsigned char* pa;
	unsigned char* q;
	unsigned char* qa;
	unsigned char* r;
	unsigned char* ra;
	unsigned char* s;
	unsigned char* sa;
	unsigned char* t;
	unsigned char* ta;
	const unsigned char* pxf; /* P factor to compute Dx */
	const unsigned char* qxf; /* Q factor to compute Dx */
	const unsigned char* rxf; /* R factor to compute Dx */
	const unsigned char* sxf; /* S factor to compute Dx */
	const unsigned char* txf; /* T factor to compute Dx */
	const unsigned char* pyf; /* P factor to compute Dy */
	const unsigned char* qyf; /* Q factor to compute Dy */
	const unsigned char* ryf; /* R factor to compute Dy */
	const unsigned char* syf; /* S factor to compute Dy */
	const unsigned char* tyf; /* T factor to compute Dy */
	const unsigned char* pzf; /* P factor to compute Dz */
	const unsigned char* qzf; /* Q factor to compute Dz */
	const unsigned char* rzf; /* R factor to compute Dz */
	const unsigned char* szf; /* S factor to compute Dz */
	const unsigned char* tzf; /* T factor to compute Dz */
	const unsigned char* pvf; /* P factor to compute Dv */
	const unsigned char* qvf; /* Q factor to compute Dv */
	const unsigned char* rvf; /* R factor to compute Dv */
	const unsigned char* svf; /* S factor to compute Dv */
	const unsigned char* tvf; /* T factor to compute Dv */
	const unsigned char* puf; /* P factor to compute Du */
	const unsigned char* quf; /* Q factor to compute Du */
	const unsigned char* ruf; /* R factor to compute Du */
	const unsigned char* suf; /* S factor to compute Du */
	const unsigned char* tuf; /* T factor to compute Du */
	unsigned char G[5*5];
	unsigned char V[5*5];
	unsigned N = 5;

	/* setup the generator matrix */
	G[0*N+0] = A(i,x); /* row 1 for P */
	G[0*N+1] = A(i,y);
	G[0*N+2] = A(i,z);
	G[0*N+3] = A(i,v);
	G[0*N+4] = A(i,u);
	G[1*N+0] = A(j,x); /* row 2 for Q */
	G[1*N+1] = A(j,y);
	G[1*N+2] = A(j,z);
	G[1*N+3] = A(j,v);
	G[1*N+4] = A(j,u);
	G[2*N+0] = A(k,x); /* row 3 for R */
	G[2*N+1] = A(k,y);
	G[2*N+2] = A(k,z);
	G[2*N+3] = A(k,v);
	G[2*N+4] = A(k,u);
	G[3*N+0] = A(l,x); /* row 4 for S */
	G[3*N+1] = A(l,y);
	G[3*N+2] = A(l,z);
	G[3*N+3] = A(l,v);
	G[3*N+4] = A(l,u);
	G[4*N+0] = A(m,x); /* row 5 for T */
	G[4*N+1] = A(m,y);
	G[4*N+2] = A(m,z);
	G[4*N+3] = A(m,v);
	G[4*N+4] = A(m,u);

	/* invert it to solve the system of linear equations */
	invert(G, V, N);

	/* select tables */
	pxf = table( V[0*N+0] );
	qxf = table( V[0*N+1] );
	rxf = table( V[0*N+2] );
	sxf = table( V[0*N+3] );
	txf = table( V[0*N+4] );
	pyf = table( V[1*N+0] );
	qyf = table( V[1*N+1] );
	ryf = table( V[1*N+2] );
	syf = table( V[1*N+3] );
	tyf = table( V[1*N+4] );
	pzf = table( V[2*N+0] );
	qzf = table( V[2*N+1] );
	rzf = table( V[2*N+2] );
	szf = table( V[2*N+3] );
	tzf = table( V[2*N+4] );
	pvf = table( V[3*N+0] );
	qvf = table( V[3*N+1] );
	rvf = table( V[3*N+2] );
	svf = table( V[3*N+3] );
	tvf = table( V[3*N+4] );
	puf = table( V[4*N+0] );
	quf = table( V[4*N+1] );
	ruf = table( V[4*N+2] );
	suf = table( V[4*N+3] );
	tuf = table( V[4*N+4] );

	/* compute delta parity */
	raid_delta_5data(x, y, z, v, u, i, j, k, l, m, vbuf, data, zero, size);

	p = vbuf[data+i];
	q = vbuf[data+j];
	r = vbuf[data+k];
	s = vbuf[data+l];
	t = vbuf[data+m];
	pa = vbuf[x];
	qa = vbuf[y];
	ra = vbuf[z];
	sa = vbuf[v];
	ta = vbuf[u];

	for(off=0;off<size;++off) {
		/* delta */
		unsigned char Pd = p[off] ^ pa[off];
		unsigned char Qd = q[off] ^ qa[off];
		unsigned char Rd = r[off] ^ ra[off];
		unsigned char Sd = s[off] ^ sa[off];
		unsigned char Td = t[off] ^ ta[off];

		/* addends to reconstruct Du */
		unsigned char pum = puf[Pd];
		unsigned char qum = quf[Qd];
		unsigned char rum = ruf[Rd];
		unsigned char sum = suf[Sd];
		unsigned char tum = tuf[Td];

		/* reconstruct Du */
		unsigned char Du = pum ^ qum ^ rum ^ sum ^ tum;

		/* addends to reconstruct Dv */
		unsigned char pvm = pvf[Pd];
		unsigned char qvm = qvf[Qd];
		unsigned char rvm = rvf[Rd];
		unsigned char svm = svf[Sd];
		unsigned char tvm = tvf[Td];

		/* reconstruct Dv */
		unsigned char Dv = pvm ^ qvm ^ rvm ^ svm ^ tvm;

		/* addends to reconstruct Dz */
		unsigned char pzm = pzf[Pd];
		unsigned char qzm = qzf[Qd];
		unsigned char rzm = rzf[Rd];
		unsigned char szm = szf[Sd];
		unsigned char tzm = tzf[Td];

		/* reconstruct Dz */
		unsigned char Dz = pzm ^ qzm ^ rzm ^ szm ^ tzm;

		/* addends to reconstruct Dy */
		unsigned char pym = pyf[Pd];
		unsigned char qym = qyf[Qd];
		unsigned char rym = ryf[Rd];
		unsigned char sym = syf[Sd];
		unsigned char tym = tyf[Td];

		/* reconstruct Dy */
		unsigned char Dy = pym ^ qym ^ rym ^ sym ^ tym;

		/* reconstruct Dx */
		unsigned char Dx;

		/* if i is P, take the fast way */
		if (i == 0) {
			Dx = Pd ^ Dy ^ Dz ^ Dv ^ Du;
		} else {
			/* addends to reconstruct Dx */
			unsigned char pxm = pxf[Pd];
			unsigned char qxm = qxf[Qd];
			unsigned char rxm = rxf[Rd];
			unsigned char sxm = sxf[Sd];
			unsigned char txm = txf[Td];

			/* reconstruct Dx */
			Dx = pxm ^ qxm ^ rxm ^ sxm ^ txm;
		}

		/* set */
		pa[off] = Dx;
		qa[off] = Dy;
		ra[off] = Dz;
		sa[off] = Dv;
		ta[off] = Du;
	}
}

/**
 * Recover failure of six data blocks x,y,z,v,u,w using parity i,j,k,l,m,n for any RAID level.
 *
 * Starting from the equations:
 *
 * Pd = A[i,x] * Dx + A[i,y] * Dy + A[i,z] * Dz + A[i,v] * Dv + A[i,u] * Du + A[i,w] * Dw
 * Qd = A[j,x] * Dx + A[j,y] * Dy + A[j,z] * Dz + A[j,v] * Dv + A[j,u] * Du + A[j,w] * Dw
 * Rd = A[k,x] * Dx + A[k,y] * Dy + A[k,z] * Dz + A[k,v] * Dv + A[k,u] * Du + A[k,w] * Dw
 * Sd = A[l,x] * Dx + A[l,y] * Dy + A[l,z] * Dz + A[l,v] * Dv + A[l,u] * Du + A[l,w] * Dw
 * Td = A[m,x] * Dx + A[m,y] * Dy + A[m,z] * Dz + A[m,v] * Dv + A[m,u] * Du + A[m,w] * Dw
 * Od = A[n,x] * Dx + A[n,y] * Dy + A[n,z] * Dz + A[n,v] * Dv + A[n,u] * Du + A[n,w] * Dw
 *
 * we solve inverting the coefficients matrix.
 */
static void raid_recov_6data(int x, int y, int z, int v, int u, int w, int i, int j, int k, int l, int m, int n, unsigned char** vbuf, unsigned data, unsigned char* zero, unsigned size)
{
	unsigned off;
	unsigned char* p;
	unsigned char* pa;
	unsigned char* q;
	unsigned char* qa;
	unsigned char* r;
	unsigned char* ra;
	unsigned char* s;
	unsigned char* sa;
	unsigned char* t;
	unsigned char* ta;
	unsigned char* o;
	unsigned char* oa;
	const unsigned char* pxf; /* P factor to compute Dx */
	const unsigned char* qxf; /* Q factor to compute Dx */
	const unsigned char* rxf; /* R factor to compute Dx */
	const unsigned char* sxf; /* S factor to compute Dx */
	const unsigned char* txf; /* T factor to compute Dx */
	const unsigned char* oxf; /* O factor to compute Dx */
	const unsigned char* pyf; /* P factor to compute Dy */
	const unsigned char* qyf; /* Q factor to compute Dy */
	const unsigned char* ryf; /* R factor to compute Dy */
	const unsigned char* syf; /* S factor to compute Dy */
	const unsigned char* tyf; /* T factor to compute Dy */
	const unsigned char* oyf; /* O factor to compute Dy */
	const unsigned char* pzf; /* P factor to compute Dz */
	const unsigned char* qzf; /* Q factor to compute Dz */
	const unsigned char* rzf; /* R factor to compute Dz */
	const unsigned char* szf; /* S factor to compute Dz */
	const unsigned char* tzf; /* T factor to compute Dz */
	const unsigned char* ozf; /* O factor to compute Dz */
	const unsigned char* pvf; /* P factor to compute Dv */
	const unsigned char* qvf; /* Q factor to compute Dv */
	const unsigned char* rvf; /* R factor to compute Dv */
	const unsigned char* svf; /* S factor to compute Dv */
	const unsigned char* tvf; /* T factor to compute Dv */
	const unsigned char* ovf; /* O factor to compute Dv */
	const unsigned char* puf; /* P factor to compute Du */
	const unsigned char* quf; /* Q factor to compute Du */
	const unsigned char* ruf; /* R factor to compute Du */
	const unsigned char* suf; /* S factor to compute Du */
	const unsigned char* tuf; /* T factor to compute Du */
	const unsigned char* ouf; /* O factor to compute Du */
	const unsigned char* pwf; /* P factor to compute Dw */
	const unsigned char* qwf; /* Q factor to compute Dw */
	const unsigned char* rwf; /* R factor to compute Dw */
	const unsigned char* swf; /* S factor to compute Dw */
	const unsigned char* twf; /* T factor to compute Dw */
	const unsigned char* owf; /* O factor to compute Dw */
	unsigned char G[6*6];
	unsigned char V[6*6];
	unsigned N = 6;

	/* setup the generator matrix */
	G[0*N+0] = A(i,x); /* row 1 for P */
	G[0*N+1] = A(i,y);
	G[0*N+2] = A(i,z);
	G[0*N+3] = A(i,v);
	G[0*N+4] = A(i,u);
	G[0*N+5] = A(i,w);
	G[1*N+0] = A(j,x); /* row 2 for Q */
	G[1*N+1] = A(j,y);
	G[1*N+2] = A(j,z);
	G[1*N+3] = A(j,v);
	G[1*N+4] = A(j,u);
	G[1*N+5] = A(j,w);
	G[2*N+0] = A(k,x); /* row 3 for R */
	G[2*N+1] = A(k,y);
	G[2*N+2] = A(k,z);
	G[2*N+3] = A(k,v);
	G[2*N+4] = A(k,u);
	G[2*N+5] = A(k,w);
	G[3*N+0] = A(l,x); /* row 4 for S */
	G[3*N+1] = A(l,y);
	G[3*N+2] = A(l,z);
	G[3*N+3] = A(l,v);
	G[3*N+4] = A(l,u);
	G[3*N+5] = A(l,w);
	G[4*N+0] = A(m,x); /* row 5 for T */
	G[4*N+1] = A(m,y);
	G[4*N+2] = A(m,z);
	G[4*N+3] = A(m,v);
	G[4*N+4] = A(m,u);
	G[4*N+5] = A(m,w);
	G[5*N+0] = A(n,x); /* row 6 for O */
	G[5*N+1] = A(n,y);
	G[5*N+2] = A(n,z);
	G[5*N+3] = A(n,v);
	G[5*N+4] = A(n,u);
	G[5*N+5] = A(n,w);

	/* invert it to solve the system of linear equations */
	invert(G, V, N);

	/* select tables */
	pxf = table( V[0*N+0] );
	qxf = table( V[0*N+1] );
	rxf = table( V[0*N+2] );
	sxf = table( V[0*N+3] );
	txf = table( V[0*N+4] );
	oxf = table( V[0*N+5] );
	pyf = table( V[1*N+0] );
	qyf = table( V[1*N+1] );
	ryf = table( V[1*N+2] );
	syf = table( V[1*N+3] );
	tyf = table( V[1*N+4] );
	oyf = table( V[1*N+5] );
	pzf = table( V[2*N+0] );
	qzf = table( V[2*N+1] );
	rzf = table( V[2*N+2] );
	szf = table( V[2*N+3] );
	tzf = table( V[2*N+4] );
	ozf = table( V[2*N+5] );
	pvf = table( V[3*N+0] );
	qvf = table( V[3*N+1] );
	rvf = table( V[3*N+2] );
	svf = table( V[3*N+3] );
	tvf = table( V[3*N+4] );
	ovf = table( V[3*N+5] );
	puf = table( V[4*N+0] );
	quf = table( V[4*N+1] );
	ruf = table( V[4*N+2] );
	suf = table( V[4*N+3] );
	tuf = table( V[4*N+4] );
	ouf = table( V[4*N+5] );
	pwf = table( V[5*N+0] );
	qwf = table( V[5*N+1] );
	rwf = table( V[5*N+2] );
	swf = table( V[5*N+3] );
	twf = table( V[5*N+4] );
	owf = table( V[5*N+5] );

	/* compute delta parity */
	raid_delta_6data(x, y, z, v, u, w, i, j, k, l, m, n, vbuf, data, zero, size);

	p = vbuf[data+i];
	q = vbuf[data+j];
	r = vbuf[data+k];
	s = vbuf[data+l];
	t = vbuf[data+m];
	o = vbuf[data+n];
	pa = vbuf[x];
	qa = vbuf[y];
	ra = vbuf[z];
	sa = vbuf[v];
	ta = vbuf[u];
	oa = vbuf[w];

	for(off=0;off<size;++off) {
		/* delta */
		unsigned char Pd = p[off] ^ pa[off];
		unsigned char Qd = q[off] ^ qa[off];
		unsigned char Rd = r[off] ^ ra[off];
		unsigned char Sd = s[off] ^ sa[off];
		unsigned char Td = t[off] ^ ta[off];
		unsigned char Od = o[off] ^ oa[off];

		/* addends to reconstruct Dw */
		unsigned char pwm = pwf[Pd];
		unsigned char qwm = qwf[Qd];
		unsigned char rwm = rwf[Rd];
		unsigned char swm = swf[Sd];
		unsigned char twm = twf[Td];
		unsigned char owm = owf[Od];

		/* reconstruct Dw */
		unsigned char Dw = pwm ^ qwm ^ rwm ^ swm ^ twm ^ owm;

		/* addends to reconstruct Du */
		unsigned char pum = puf[Pd];
		unsigned char qum = quf[Qd];
		unsigned char rum = ruf[Rd];
		unsigned char sum = suf[Sd];
		unsigned char tum = tuf[Td];
		unsigned char oum = ouf[Od];

		/* reconstruct Du */
		unsigned char Du = pum ^ qum ^ rum ^ sum ^ tum ^ oum;

		/* addends to reconstruct Dv */
		unsigned char pvm = pvf[Pd];
		unsigned char qvm = qvf[Qd];
		unsigned char rvm = rvf[Rd];
		unsigned char svm = svf[Sd];
		unsigned char tvm = tvf[Td];
		unsigned char ovm = ovf[Od];

		/* reconstruct Dv */
		unsigned char Dv = pvm ^ qvm ^ rvm ^ svm ^ tvm ^ ovm;

		/* addends to reconstruct Dz */
		unsigned char pzm = pzf[Pd];
		unsigned char qzm = qzf[Qd];
		unsigned char rzm = rzf[Rd];
		unsigned char szm = szf[Sd];
		unsigned char tzm = tzf[Td];
		unsigned char ozm = ozf[Od];

		/* reconstruct Dz */
		unsigned char Dz = pzm ^ qzm ^ rzm ^ szm ^ tzm ^ ozm;

		/* addends to reconstruct Dy */
		unsigned char pym = pyf[Pd];
		unsigned char qym = qyf[Qd];
		unsigned char rym = ryf[Rd];
		unsigned char sym = syf[Sd];
		unsigned char tym = tyf[Td];
		unsigned char oym = oyf[Od];

		/* reconstruct Dy */
		unsigned char Dy = pym ^ qym ^ rym ^ sym ^ tym ^ oym;

		/* reconstruct Dx */
		unsigned char Dx;

		/* if i is P, take the fast way */
		if (i == 0) {
			Dx = Pd ^ Dy ^ Dz ^ Dv ^ Du ^ Dw;
		} else {
			/* addends to reconstruct Dx */
			unsigned char pxm = pxf[Pd];
			unsigned char qxm = qxf[Qd];
			unsigned char rxm = rxf[Rd];
			unsigned char sxm = sxf[Sd];
			unsigned char txm = txf[Td];
			unsigned char oxm = oxf[Od];

			/* reconstruct Dx */
			Dx = pxm ^ qxm ^ rxm ^ sxm ^ txm ^ oxm;
		}

		/* set */
		pa[off] = Dx;
		qa[off] = Dy;
		ra[off] = Dz;
		sa[off] = Dv;
		ta[off] = Du;
		oa[off] = Dw;
	}
}

void raid_recov(unsigned level, int* d, int* p, unsigned char** vbuf, unsigned data, unsigned char* zero, unsigned size)
{
	switch (level) {
	case 1 :
		raid_recov_1data(d[0], p[0], vbuf, data, zero, size);
		break;
	case 2 :
		raid_recov_2data(d[0], d[1], p[0], p[1], vbuf, data, zero, size);
		break;
	case 3 :
		raid_recov_3data(d[0], d[1], d[2], p[0], p[1], p[2], vbuf, data, zero, size);
		break;
	case 4 :
		raid_recov_4data(d[0], d[1], d[2], d[3], p[0], p[1], p[2], p[3], vbuf, data, zero, size);
		break;
	case 5 :
		raid_recov_5data(d[0], d[1], d[2], d[3], d[4], p[0], p[1], p[2], p[3], p[4], vbuf, data, zero, size);
		break;
	case 6 :
		raid_recov_6data(d[0], d[1], d[2], d[3], d[4], d[5], p[0], p[1], p[2], p[3], p[4], p[5], vbuf, data, zero, size);
		break;
	default:
		fprintf(stderr, "Invalid raid recov level\n");
		exit(EXIT_FAILURE);
	}
}

/****************************************************************************/
/* init/done */

void raid_init(void)
{
	raidTP_gen = raidTP_int8;
	raidQP_gen = raidQP_int8;
	raidPP_gen = raidPP_int8;
	raidHP_gen = raidHP_int8;

	if (sizeof(void*) == 4) {
		raid5_gen = raid5_int32;
		raid6_gen = raid6_int32;
	} else {
		raid5_gen = raid5_int64;
		raid6_gen = raid6_int64;
	}

#if defined(__i386__) || defined(__x86_64__)
	if (cpu_has_mmx()) {
		raid5_gen = raid5_mmx;
		raid6_gen = raid6_mmx;
	}

	if (cpu_has_sse2()) {
		raid5_gen = raid5_sse2;
#if defined(__x86_64__)
		if (cpu_has_slowextendedreg()) {
			raid6_gen = raid6_sse2;
		} else {
			raid6_gen = raid6_sse2ext;
		}
#else
		raid6_gen = raid6_sse2;
#endif
	}

	if (cpu_has_ssse3() && !cpu_has_slowpshufb()) {
#if defined(__x86_64__)
		raidTP_gen = raidTP_ssse3ext;
		raidQP_gen = raidQP_ssse3ext;
		raidPP_gen = raidPP_ssse3ext;
		raidHP_gen = raidHP_ssse3ext;
#else
		raidTP_gen = raidTP_ssse3;
		raidQP_gen = raidQP_ssse3;
		raidPP_gen = raidPP_ssse3;
		raidHP_gen = raidHP_ssse3;
#endif
	}
#endif
}

static struct raid_func {
	const char* name;
	void* p;
} RAID_FUNC[] = {
	{ "int8", raidTP_int8 },
	{ "int8", raidQP_int8 },
	{ "int8", raidPP_int8 },
	{ "int8", raidHP_int8 },
	{ "int32", raid5_int32 },
	{ "int64", raid5_int64 },
	{ "int32", raid6_int32 },
	{ "int64", raid6_int64 },

#if defined(__i386__) || defined(__x86_64__)
	{ "mmx", raid5_mmx },
	{ "mmx", raid6_mmx },
	{ "sse2", raid5_sse2 },
	{ "sse2", raid6_sse2 },
	{ "ssse3", raidTP_ssse3 },
	{ "ssse3", raidQP_ssse3 },
	{ "ssse3", raidPP_ssse3 },
	{ "ssse3", raidHP_ssse3 },
#endif

#if defined(__x86_64__)
	{ "sse2e", raid6_sse2ext },
	{ "ssse3e", raidTP_ssse3ext },
	{ "ssse3e", raidQP_ssse3ext },
	{ "ssse3e", raidPP_ssse3ext },
	{ "ssse3e", raidHP_ssse3ext },
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

const char* raidPP_tag(void)
{
	return raid_tag(raidPP_gen);
}

const char* raidHP_tag(void)
{
	return raid_tag(raidHP_gen);
}

