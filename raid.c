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
 * Copyright notes:
 *
 * The RAID5 and RAID6 support was originally derived from the
 * H. Peter Anvin paper "The mathematics of RAID-6" [1] and the
 * libraid6 library, also by H. Peter Anvin, released with license
 * "GPLv2 or any later version" inside the Linux Kernel 2.6.38.
 *
 * This support was later completely rewritten (many times), but
 * the H. Peter Anvin's Copyright may still apply.
 *
 * The others RAID levels and the recovering based on matrix
 * inversion is original work implemented from scratch.
 */

/*
 * The RAID5 and RAID6 support is implemented using the Galois Field
 * GF(2^8) with the primitive polynomial x^8 + x^4 + x^3 + x^2 + 1 
 * (285 decimal).
 *
 * The parity P and Q of a set of N disk Di with 0<=i<N, is computed
 * using the equations:
 *
 * P = sum(Di)
 * Q = sum(2^i * Di) with 0<=i<N
 *
 * This approach is the same used by the Linux Kernel RAID, and by ZFS
 * RAIDZ2, better described in the H. Peter Anvin paper "The mathematics 
 * of RAID-6" [1]. 
 *
 * To support triple parity, it was first evaluated and then dropped, an
 * extension of the same approach, with additional parity coefficients set
 * as powers of 2^-1, with equations:
 *
 * P = sum(Di)
 * Q = sum(2^i * Di)
 * R = sum(2^-i * Di) with 0<=i<N
 *
 * This approach works well for triple parity and it's very efficient,
 * because we can implement very fast parallel multiplications and
 * divisions by 2 in GF(2^8).
 *
 * It's also similar at the approach used by ZFS RAIDZ3, with the 
 * difference that ZFS uses powers of 4 instead of 2^-1.
 *
 * Unfortunately it doesn't work beyond triple parity, because whatever
 * value we choose to generate the power coefficients to compute other
 * parities, the resulting equations are not solvable for some
 * combinations of missing disks.
 *
 * This is expected, because the Vandermonde matrix used to compute the
 * parity has no guarantee to have all submatrices not singular
 * [2, Chap 11, Problem 7] and this is a requirement to have
 * a MDS (Maximum Distance Separable) code [2, Chap 11, Theorem 8].
 *
 * To overcome this limitation, we use a Cauchy matrix [3][4] to compute 
 * the parity. A Cauchy matrix has the property to have all the square
 * submatrices not singular, resulting in always solvable equations,
 * for any combination of missing disks.
 *
 * The problem of this approach is that it requires the use of
 * generic multiplications, and not only by 2 or 2^-1, potentially 
 * affecting badly the performance.
 *
 * Hopefully there is a method to implement parallel multiplications
 * using SSSE3 instructions [1][5]. Method competitive with the
 * computation of triple parity using power coefficients.
 *
 * Another important property of the Cauchy matrix is that we can setup
 * the first two rows with coeffients equal at the RAID5 and RAID6 approach
 * decribed, resulting in a compatible extension, and requiring SSSE3
 * instructions only if triple parity or beyond is used.
 * 
 * The matrix is also adjusted, multipling each row by a constant factor
 * to make the first column of all 1, to optimize the computation for
 * the first disk.
 *
 * This results in the matrix A[row,col] defined as:
 * 
 * 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01...
 * 01 02 04 08 10 20 40 80 1d 3a 74 e8 cd 87 13 26 4c 98 2d 5a b4 75...
 * 01 f5 d2 c4 9a 71 f1 7f fc 87 c1 c6 19 2f 40 55 3d ba 53 04 9c 61...
 * 01 bb a6 d7 c7 07 ce 82 4a 2f a5 9b b6 60 f1 ad e7 f4 06 d2 df 2e...
 * 01 97 7f 9c 7c 18 bd a2 58 1a da 74 70 a3 e5 47 29 07 f5 80 23 e9...
 * 01 2b 3f cf 73 2c d6 ed cb 74 15 78 8a c1 17 c9 89 68 21 ab 76 3b...
 * (see tables.h for the matrix with all the columns)
 *
 * This matrix supports 6 level of parity, one for each row, for up to 251
 * data disks, one for each column, with all the 377,342,351,231 square
 * submatrices not singular, verified also with brute-force.
 * 
 * This matrix can be extended to support any number of parities, just 
 * adding additional rows, and removing one column for each new row.
 * (see mktables.c for more details in how the matrix is generated)
 * 
 * In details, parity is computed as:
 *
 * P = sum(Di)
 * Q = sum(2^i *  Di)
 * R = sum(A[2,i] * Di)
 * S = sum(A[3,i] * Di)
 * T = sum(A[4,i] * Di)
 * U = sum(A[5,i] * Di) with 0<=i<N
 *
 * To recover from a failure of six disks at indexes x,y,z,h,v,w,
 * with 0<=x<y<z<h<v<w<N, we compute the parity of the available N-6 
 * disks as:
 *
 * Pa = sum(Di)
 * Qa = sum(2^i * Di)
 * Ra = sum(A[2,i] * Di)
 * Sa = sum(A[3,i] * Di)
 * Ta = sum(A[4,i] * Di)
 * Ua = sum(A[5,i] * Di) with 0<=i<N,i!=x,i!=y,i!=z,i!=h,i!=v,i!=w.
 *
 * And if we define:
 *
 * Pd = Pa + P
 * Qd = Qa + Q
 * Rd = Ra + R
 * Sd = Sa + S
 * Td = Ta + T
 * Ud = Ua + U
 *
 * we can sum these two sets of equations, obtaining:
 *
 * Pd =          Dx +          Dy +          Dz +          Dh +          Dv +          Dw
 * Qd =    2^x * Dx +    2^y * Dy +    2^z * Dz +    2^h * Dh +    2^v * Dv +    2^w * Dw
 * Rd = A[2,x] * Dx + A[2,y] * Dy + A[2,z] * Dz + A[2,h] * Dh + A[2,v] * Dv + A[2,w] * Dw
 * Sd = A[3,x] * Dx + A[3,y] * Dy + A[3,z] * Dz + A[3,h] * Dh + A[3,v] * Dv + A[3,w] * Dw
 * Td = A[4,x] * Dx + A[4,y] * Dy + A[4,z] * Dz + A[4,h] * Dh + A[4,v] * Dv + A[4,w] * Dw
 * Ud = A[5,x] * Dx + A[5,y] * Dy + A[5,z] * Dz + A[5,h] * Dh + A[5,v] * Dv + A[5,w] * Dw
 *
 * A linear system always solvable because the coefficients matrix is
 * always not singular due the properties of the matrix A[].
 *
 * Resulting speed in x64, with 8 data disks, using a stripe of 256 KiB,
 * for a Core i7-3740QM CPU @ 2.7GHz is:
 * 
 *             int8   int32   int64    sse2   sse2e   ssse3  ssse3e
 *   par1             11927   22075   36004
 *   par2              3378    5874   18235   19164
 *   par3       844                                    8814    9419
 *   par4       665                                    6836    7415
 *   par5       537                                    5388    5686
 *   par6       449                                    4307    4789
 *
 * Values are in MiB/s of data processed, not counting generated parity.
 *
 * You can replicate these results in your machine using the "snapraid -T"
 * command.
 *
 * For comparison, the triple parity computation using the power
 * coeffients "1,2,2^-1" is only a little faster than the one based on
 * the Cauchy matrix if SSSE3 is present.
 *
 *             int8   int32   int64    sse2   sse2e   ssse3  ssse3e
 *   parz              2112    3118    9589   10304
 *
 *
 * In conclusion, the use of power coefficients, and specifically powers
 * of 1,2,2^-1, is the best option to implement triple parity in CPUs 
 * without SSSE3.
 * But if a modern CPU with SSSE3 (or similar) is available, the Cauchy
 * matrix is the best option because it provides a fast and general 
 * approach working for any number of parities.
 *
 * References:
 * [1] Anvin, "The mathematics of RAID-6", 2004
 * [2] MacWilliams, Sloane, "The Theory of Error-Correcting Codes", 1977
 * [3] Blomer, "An XOR-Based Erasure-Resilient Coding Scheme", 1995
 * [4] Roth, "Introduction to Coding Theory", 2006
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
 * PAR1 (RAID5 with xor) 32bit C implementation
 */
void raid_par1_int32(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	int d, l;
	unsigned i;

	uint32_t p0;
	uint32_t p1;

	l = data - 1;
	p = vbuf[data];

	for(i=0;i<size;i+=8) {
		p0 = v_32(vbuf[l][i]);
		p1 = v_32(vbuf[l][i+4]);
		/* accessing disks in backward order because the buffers */
		/* are also in backward order */
		for(d=l-1;d>=0;--d) {
			p0 ^= v_32(vbuf[d][i]);
			p1 ^= v_32(vbuf[d][i+4]);
		}
		v_32(p[i]) = p0;
		v_32(p[i+4]) = p1;
	}
}

/*
 * PAR1 (RAID5 with xor) 64bit C implementation
 */
void raid_par1_int64(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	int d, l;
	unsigned i;

	uint64_t p0;
	uint64_t p1;

	l = data - 1;
	p = vbuf[data];

	for(i=0;i<size;i+=16) {
		p0 = v_64(vbuf[l][i]);
		p1 = v_64(vbuf[l][i+8]);
		/* accessing disks in backward order because the buffers */
		/* are also in backward order */
		for(d=l-1;d>=0;--d) {
			p0 ^= v_64(vbuf[d][i]);
			p1 ^= v_64(vbuf[d][i+8]);
		}
		v_64(p[i]) = p0;
		v_64(p[i+8]) = p1;
	}
}

#if defined(__i386__) || defined(__x86_64__)
/*
 * PAR1 (RAID5 with xor) SSE2 implementation
 *
 * Note that we don't have the corresponding x64 sse2ext function using more
 * registers because processing a block of 64 bytes already fills
 * the typical cache block, and processing 128 bytes doesn't increase
 * performance.
 */
void raid_par1_sse2(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	int d, l;
	unsigned i;

	l = data - 1;
	p = vbuf[data];

	for(i=0;i<size;i+=64) {
		asm volatile("movdqa %0,%%xmm0" : : "m" (vbuf[l][i]));
		asm volatile("movdqa %0,%%xmm1" : : "m" (vbuf[l][i+16]));
		asm volatile("movdqa %0,%%xmm2" : : "m" (vbuf[l][i+32]));
		asm volatile("movdqa %0,%%xmm3" : : "m" (vbuf[l][i+48]));
		/* accessing disks in backward order because the buffers */
		/* are also in backward order */
		for(d=l-1;d>=0;--d) {
			asm volatile("movdqa %0,%%xmm4" : : "m" (vbuf[d][i]));
			asm volatile("movdqa %0,%%xmm5" : : "m" (vbuf[d][i+16]));
			asm volatile("movdqa %0,%%xmm6" : : "m" (vbuf[d][i+32]));
			asm volatile("movdqa %0,%%xmm7" : : "m" (vbuf[d][i+48]));
			asm volatile("pxor %xmm4,%xmm0");
			asm volatile("pxor %xmm5,%xmm1");
			asm volatile("pxor %xmm6,%xmm2");
			asm volatile("pxor %xmm7,%xmm3");
		}
		asm volatile("movntdq %%xmm0,%0" : "=m" (p[i]));
		asm volatile("movntdq %%xmm1,%0" : "=m" (p[i+16]));
		asm volatile("movntdq %%xmm2,%0" : "=m" (p[i+32]));
		asm volatile("movntdq %%xmm3,%0" : "=m" (p[i+48]));
	}

	asm volatile("sfence" : : : "memory");
}
#endif

/**
 * Multiply each byte of a uint32 by 2 in the GF(2^8).
 */
__attribute__((always_inline))
static inline uint32_t x2_32(uint32_t v)
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
__attribute__((always_inline))
static inline uint64_t x2_64(uint64_t v)
{
	uint64_t mask = v & 0x8080808080808080ULL;
	mask = (mask << 1) - (mask >> 7);
	v = (v << 1) & 0xfefefefefefefefeULL;
	v ^= mask & 0x1d1d1d1d1d1d1d1dULL;
	return v;
}

/**
 * Divide each byte of a uint32 by 2 in the GF(2^8).
 */
__attribute__((always_inline))
static inline uint32_t d2_32(uint32_t v)
{
	uint32_t mask = v & 0x01010101U;
	mask = (mask << 8) - mask;
	v = (v >> 1) & 0x7f7f7f7fU;
	v ^= mask & 0x8e8e8e8eU;
	return v;
}

/**
 * Divide each byte of a uint64 by 2 in the GF(2^8).
 */
__attribute__((always_inline))
static inline uint64_t d2_64(uint64_t v)
{
	uint64_t mask = v & 0x0101010101010101U;
	mask = (mask << 8) - mask;
	v = (v >> 1) & 0x7f7f7f7f7f7f7f7fU;
	v ^= mask & 0x8e8e8e8e8e8e8e8eU;
	return v;
}

/*
 * PAR2 (RAID6 with powers of 2) 32bit C implementation
 */
void raid_par2_int32(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	int d, l;
	unsigned i;

	uint32_t d0, q0, p0;
	uint32_t d1, q1, p1;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];

	for(i=0;i<size;i+=8) {
		q0 = p0 = v_32(vbuf[l][i]);
		q1 = p1 = v_32(vbuf[l][i+4]);
		for(d=l-1;d>=0;--d) {
			d0 = v_32(vbuf[d][i]);
			d1 = v_32(vbuf[d][i+4]);

			p0 ^= d0;
			p1 ^= d1;

			q0 = x2_32(q0);
			q1 = x2_32(q1);

			q0 ^= d0;
			q1 ^= d1;
		}
		v_32(p[i]) = p0;
		v_32(p[i+4]) = p1;
		v_32(q[i]) = q0;
		v_32(q[i+4]) = q1;
	}
}

/*
 * PAR2 (RAID6 with powers of 2) 64bit C implementation
 */
void raid_par2_int64(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	int d, l;
	unsigned i;

	uint64_t d0, q0, p0;
	uint64_t d1, q1, p1;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];

	for(i=0;i<size;i+=16) {
		q0 = p0 = v_64(vbuf[l][i]);
		q1 = p1 = v_64(vbuf[l][i+8]);
		for(d=l-1;d>=0;--d) {
			d0 = v_64(vbuf[d][i]);
			d1 = v_64(vbuf[d][i+8]);

			p0 ^= d0;
			p1 ^= d1;

			q0 = x2_64(q0);
			q1 = x2_64(q1);

			q0 ^= d0;
			q1 ^= d1;
		}
		v_64(p[i]) = p0;
		v_64(p[i+8]) = p1;
		v_64(q[i]) = q0;
		v_64(q[i+8]) = q1;
	}
}

#if defined(__i386__) || defined(__x86_64__)
static const struct gfconst16 {
	unsigned char poly[16];
	unsigned char low4[16];
	unsigned char half[16];
	unsigned char low7[16];
} gfconst16  __attribute__((aligned(64))) = {
	{ 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d },
	{ 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f },
	{ 0x8e, 0x8e, 0x8e, 0x8e, 0x8e, 0x8e, 0x8e, 0x8e, 0x8e, 0x8e, 0x8e, 0x8e, 0x8e, 0x8e, 0x8e, 0x8e },
	{ 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f }
};

/*
 * PAR2 (RAID6 with powers of 2) SSE2 implementation
 */
void raid_par2_sse2(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	int d, l;
	unsigned i;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];

	asm volatile("movdqa %0,%%xmm7" : : "m" (gfconst16.poly[0]));

	for(i=0;i<size;i+=32) {
		asm volatile("movdqa %0,%%xmm0" : : "m" (vbuf[l][i]));
		asm volatile("movdqa %0,%%xmm1" : : "m" (vbuf[l][i+16]));
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

			asm volatile("movdqa %0,%%xmm4" : : "m" (vbuf[d][i]));
			asm volatile("movdqa %0,%%xmm5" : : "m" (vbuf[d][i+16]));
			asm volatile("pxor %xmm4,%xmm0");
			asm volatile("pxor %xmm5,%xmm1");
			asm volatile("pxor %xmm4,%xmm2");
			asm volatile("pxor %xmm5,%xmm3");
		}
		asm volatile("movntdq %%xmm0,%0" : "=m" (p[i]));
		asm volatile("movntdq %%xmm1,%0" : "=m" (p[i+16]));
		asm volatile("movntdq %%xmm2,%0" : "=m" (q[i]));
		asm volatile("movntdq %%xmm3,%0" : "=m" (q[i+16]));
	}

	asm volatile("sfence" : : : "memory");
}
#endif

#if defined(__x86_64__)
/*
 * PAR2 (RAID6 with powers of 2) SSE2 implementation
 *
 * Note that it uses 16 registers, meaning that x64 is required.
 */
void raid_par2_sse2ext(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	int d, l;
	unsigned i;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];

	asm volatile("movdqa %0,%%xmm15" : : "m" (gfconst16.poly[0]));

	for(i=0;i<size;i+=64) {
		asm volatile("movdqa %0,%%xmm0" : : "m" (vbuf[l][i]));
		asm volatile("movdqa %0,%%xmm1" : : "m" (vbuf[l][i+16]));
		asm volatile("movdqa %0,%%xmm2" : : "m" (vbuf[l][i+32]));
		asm volatile("movdqa %0,%%xmm3" : : "m" (vbuf[l][i+48]));
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

			asm volatile("movdqa %0,%%xmm8" : : "m" (vbuf[d][i]));
			asm volatile("movdqa %0,%%xmm9" : : "m" (vbuf[d][i+16]));
			asm volatile("movdqa %0,%%xmm10" : : "m" (vbuf[d][i+32]));
			asm volatile("movdqa %0,%%xmm11" : : "m" (vbuf[d][i+48]));
			asm volatile("pxor %xmm8,%xmm0");
			asm volatile("pxor %xmm9,%xmm1");
			asm volatile("pxor %xmm10,%xmm2");
			asm volatile("pxor %xmm11,%xmm3");
			asm volatile("pxor %xmm8,%xmm4");
			asm volatile("pxor %xmm9,%xmm5");
			asm volatile("pxor %xmm10,%xmm6");
			asm volatile("pxor %xmm11,%xmm7");
		}
		asm volatile("movntdq %%xmm0,%0" : "=m" (p[i]));
		asm volatile("movntdq %%xmm1,%0" : "=m" (p[i+16]));
		asm volatile("movntdq %%xmm2,%0" : "=m" (p[i+32]));
		asm volatile("movntdq %%xmm3,%0" : "=m" (p[i+48]));
		asm volatile("movntdq %%xmm4,%0" : "=m" (q[i]));
		asm volatile("movntdq %%xmm5,%0" : "=m" (q[i+16]));
		asm volatile("movntdq %%xmm6,%0" : "=m" (q[i+32]));
		asm volatile("movntdq %%xmm7,%0" : "=m" (q[i+48]));
	}

	asm volatile("sfence" : : : "memory");
}
#endif

/*
 * PARz (triple parity with powers of 2^-1) 32bit C implementation
 */
void raid_parz_int32(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	int d, l;
	unsigned i;

	uint32_t d0, r0, q0, p0;
	uint32_t d1, r1, q1, p1;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];
	r = vbuf[data+2];

	for(i=0;i<size;i+=8) {
		r0 = q0 = p0 = v_32(vbuf[l][i]);
		r1 = q1 = p1 = v_32(vbuf[l][i+4]);
		for(d=l-1;d>=0;--d) {
			d0 = v_32(vbuf[d][i]);
			d1 = v_32(vbuf[d][i+4]);

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
		v_32(p[i+4]) = p1;
		v_32(q[i]) = q0;
		v_32(q[i+4]) = q1;
		v_32(r[i]) = r0;
		v_32(r[i+4]) = r1;
	}
}

/*
 * PARz (triple parity with powers of 2^-1) 64bit C implementation
 */
void raid_parz_int64(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	int d, l;
	unsigned i;

	uint64_t d0, r0, q0, p0;
	uint64_t d1, r1, q1, p1;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];
	r = vbuf[data+2];

	for(i=0;i<size;i+=16) {
		r0 = q0 = p0 = v_64(vbuf[l][i]);
		r1 = q1 = p1 = v_64(vbuf[l][i+8]);
		for(d=l-1;d>=0;--d) {
			d0 = v_64(vbuf[d][i]);
			d1 = v_64(vbuf[d][i+8]);

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
		v_64(p[i+8]) = p1;
		v_64(q[i]) = q0;
		v_64(q[i+8]) = q1;
		v_64(r[i]) = r0;
		v_64(r[i+8]) = r1;
	}
}

#if defined(__i386__) || defined(__x86_64__)
/*
 * PARz (triple parity with powers of 2^-1) SSE2 implementation
 */
void raid_parz_sse2(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	int d, l;
	unsigned i;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];
	r = vbuf[data+2];

	asm volatile("movdqa %0,%%xmm7" : : "m" (gfconst16.poly[0]));
	asm volatile("movdqa %0,%%xmm3" : : "m" (gfconst16.half[0]));
	asm volatile("movdqa %0,%%xmm6" : : "m" (gfconst16.low7[0]));

	for(i=0;i<size;i+=16) {
		asm volatile("movdqa %0,%%xmm0" : : "m" (vbuf[l][i]));
		asm volatile("movdqa %xmm0,%xmm1");
		asm volatile("movdqa %xmm0,%xmm2");
		for(d=l-1;d>=0;--d) {
			asm volatile("pxor %xmm4,%xmm4");
			asm volatile("pcmpgtb %xmm1,%xmm4");
			asm volatile("paddb %xmm1,%xmm1");
			asm volatile("pand %xmm7,%xmm4");
			asm volatile("pxor %xmm4,%xmm1");

			asm volatile("movdqa %xmm2,%xmm4");
			asm volatile("pxor %xmm5,%xmm5");
			asm volatile("psllw $7,%xmm4");
			asm volatile("psrlw $1,%xmm2");
			asm volatile("pcmpgtb %xmm4,%xmm5");
			asm volatile("pand %xmm6,%xmm2");
			asm volatile("pand %xmm3,%xmm5");
			asm volatile("pxor %xmm5,%xmm2");

			asm volatile("movdqa %0,%%xmm4" : : "m" (vbuf[d][i]));
			asm volatile("pxor %xmm4,%xmm0");
			asm volatile("pxor %xmm4,%xmm1");
			asm volatile("pxor %xmm4,%xmm2");
		}
		asm volatile("movntdq %%xmm0,%0" : "=m" (p[i]));
		asm volatile("movntdq %%xmm1,%0" : "=m" (q[i]));
		asm volatile("movntdq %%xmm2,%0" : "=m" (r[i]));
	}

	asm volatile("sfence" : : : "memory");
}
#endif

#if defined(__x86_64__)
/*
 * PARz (triple parity with powers of 2^-1) SSE2 implementation
 *
 * Note that it uses 16 registers, meaning that x64 is required.
 */
void raid_parz_sse2ext(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	int d, l;
	unsigned i;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];
	r = vbuf[data+2];

	asm volatile("movdqa %0,%%xmm7" : : "m" (gfconst16.poly[0]));
	asm volatile("movdqa %0,%%xmm3" : : "m" (gfconst16.half[0]));
	asm volatile("movdqa %0,%%xmm11" : : "m" (gfconst16.low7[0]));

	for(i=0;i<size;i+=32) {
		asm volatile("movdqa %0,%%xmm0" : : "m" (vbuf[l][i]));
		asm volatile("movdqa %0,%%xmm8" : : "m" (vbuf[l][i+16]));
		asm volatile("movdqa %xmm0,%xmm1");
		asm volatile("movdqa %xmm8,%xmm9");
		asm volatile("movdqa %xmm0,%xmm2");
		asm volatile("movdqa %xmm8,%xmm10");
		for(d=l-1;d>=0;--d) {
			asm volatile("movdqa %xmm2,%xmm6");
			asm volatile("movdqa %xmm10,%xmm14");
			asm volatile("pxor %xmm4,%xmm4");
			asm volatile("pxor %xmm12,%xmm12");
			asm volatile("pxor %xmm5,%xmm5");
			asm volatile("pxor %xmm13,%xmm13");
			asm volatile("psllw $7,%xmm6");
			asm volatile("psllw $7,%xmm14");
			asm volatile("psrlw $1,%xmm2");
			asm volatile("psrlw $1,%xmm10");
			asm volatile("pcmpgtb %xmm1,%xmm4");
			asm volatile("pcmpgtb %xmm9,%xmm12");
			asm volatile("pcmpgtb %xmm6,%xmm5");
			asm volatile("pcmpgtb %xmm14,%xmm13");
			asm volatile("paddb %xmm1,%xmm1");
			asm volatile("paddb %xmm9,%xmm9");
			asm volatile("pand %xmm11,%xmm2");
			asm volatile("pand %xmm11,%xmm10");
			asm volatile("pand %xmm7,%xmm4");
			asm volatile("pand %xmm7,%xmm12");
			asm volatile("pand %xmm3,%xmm5");
			asm volatile("pand %xmm3,%xmm13");
			asm volatile("pxor %xmm4,%xmm1");
			asm volatile("pxor %xmm12,%xmm9");
			asm volatile("pxor %xmm5,%xmm2");
			asm volatile("pxor %xmm13,%xmm10");

			asm volatile("movdqa %0,%%xmm4" : : "m" (vbuf[d][i]));
			asm volatile("movdqa %0,%%xmm12" : : "m" (vbuf[d][i+16]));
			asm volatile("pxor %xmm4,%xmm0");
			asm volatile("pxor %xmm4,%xmm1");
			asm volatile("pxor %xmm4,%xmm2");
			asm volatile("pxor %xmm12,%xmm8");
			asm volatile("pxor %xmm12,%xmm9");
			asm volatile("pxor %xmm12,%xmm10");
		}
		asm volatile("movntdq %%xmm0,%0" : "=m" (p[i]));
		asm volatile("movntdq %%xmm8,%0" : "=m" (p[i+16]));
		asm volatile("movntdq %%xmm1,%0" : "=m" (q[i]));
		asm volatile("movntdq %%xmm9,%0" : "=m" (q[i+16]));
		asm volatile("movntdq %%xmm2,%0" : "=m" (r[i]));
		asm volatile("movntdq %%xmm10,%0" : "=m" (r[i+16]));
	}

	asm volatile("sfence" : : : "memory");
}
#endif

/*
 * PAR3 (triple parity with Cauchy matrix) 8bit C implementation
 * 
 * Note that instead of a generic multiplicationt table, likely resulting
 * in multiple cache misses, a precomputed table could be used. 
 * But this is only a kind of reference function, and we are not really
 * interested in speed.
 */
void raid_par3_int8(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	int d, l;
	unsigned i;

	uint8_t d0, r0, q0, p0;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];
	r = vbuf[data+2];

	for(i=0;i<size;i+=1) {
		p0 = q0 = r0 = 0;
		for(d=l;d>0;--d) {
			d0 = v_8(vbuf[d][i]);

			p0 ^= d0;
			q0 ^= gfmul[d0][gfcauchy[1][d]];
			r0 ^= gfmul[d0][gfcauchy[2][d]];
		}

		/* first disk with all coefficients at 1 */
		d0 = v_8(vbuf[0][i]);

		p0 ^= d0;
		q0 ^= d0;
		r0 ^= d0;

		v_8(p[i]) = p0;
		v_8(q[i]) = q0;
		v_8(r[i]) = r0;
	}
}

#if defined(__i386__) || defined(__x86_64__)
/*
 * PAR3 (triple parity with Cauchy matrix) SSSE3 implementation
 */
void raid_par3_ssse3(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	int d, l;
	unsigned i;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];
	r = vbuf[data+2];

	asm volatile("movdqa %0,%%xmm3" : : "m" (gfconst16.poly[0]));
	asm volatile("movdqa %0,%%xmm7" : : "m" (gfconst16.low4[0]));

	for(i=0;i<size;i+=16) {
		/* last disk without the by two multiplication */
		asm volatile("movdqa %0,%%xmm4" : : "m" (vbuf[l][i]));

		asm volatile("movdqa %xmm4,%xmm0");
		asm volatile("movdqa %xmm4,%xmm1");

		asm volatile("movdqa %xmm4,%xmm5");
		asm volatile("psrlw  $4,%xmm5");
		asm volatile("pand   %xmm7,%xmm4");
		asm volatile("pand   %xmm7,%xmm5");

		asm volatile("movdqa %0,%%xmm2" : : "m" (gfgenpshufb[l][0][0][0]));
		asm volatile("movdqa %0,%%xmm6" : : "m" (gfgenpshufb[l][0][1][0]));
		asm volatile("pshufb %xmm4,%xmm2");
		asm volatile("pshufb %xmm5,%xmm6");
		asm volatile("pxor   %xmm6,%xmm2");

		/* intermediate disks */
		for(d=l-1;d>0;--d) {
			asm volatile("movdqa %0,%%xmm4" : : "m" (vbuf[d][i]));

			asm volatile("pxor %xmm5,%xmm5");
			asm volatile("pcmpgtb %xmm1,%xmm5");
			asm volatile("paddb %xmm1,%xmm1");
			asm volatile("pand %xmm3,%xmm5");
			asm volatile("pxor %xmm5,%xmm1");

			asm volatile("pxor %xmm4,%xmm0");
			asm volatile("pxor %xmm4,%xmm1");

			asm volatile("movdqa %xmm4,%xmm5");
			asm volatile("psrlw  $4,%xmm5");
			asm volatile("pand   %xmm7,%xmm4");
			asm volatile("pand   %xmm7,%xmm5");

			asm volatile("movdqa %0,%%xmm6" : : "m" (gfgenpshufb[d][0][0][0]));
			asm volatile("pshufb %xmm4,%xmm6");
			asm volatile("pxor   %xmm6,%xmm2");
			asm volatile("movdqa %0,%%xmm6" : : "m" (gfgenpshufb[d][0][1][0]));
			asm volatile("pshufb %xmm5,%xmm6");
			asm volatile("pxor   %xmm6,%xmm2");
		}

		/* first disk with all coefficients at 1 */
		asm volatile("movdqa %0,%%xmm4" : : "m" (vbuf[0][i]));

		asm volatile("pxor %xmm5,%xmm5");
		asm volatile("pcmpgtb %xmm1,%xmm5");
		asm volatile("paddb %xmm1,%xmm1");
		asm volatile("pand %xmm3,%xmm5");
		asm volatile("pxor %xmm5,%xmm1");

		asm volatile("pxor %xmm4,%xmm0");
		asm volatile("pxor %xmm4,%xmm1");
		asm volatile("pxor %xmm4,%xmm2");
		
		asm volatile("movntdq %%xmm0,%0" : "=m" (p[i]));
		asm volatile("movntdq %%xmm1,%0" : "=m" (q[i]));
		asm volatile("movntdq %%xmm2,%0" : "=m" (r[i]));
	}

	asm volatile("sfence" : : : "memory");
}
#endif

#if defined(__x86_64__)
/*
 * PAR3 (triple parity with Cauchy matrix) SSSE3 implementation
 *
 * Note that it uses 16 registers, meaning that x64 is required.
 */
void raid_par3_ssse3ext(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	int d, l;
	unsigned i;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];
	r = vbuf[data+2];

	asm volatile("movdqa %0,%%xmm3" : : "m" (gfconst16.poly[0]));
	asm volatile("movdqa %0,%%xmm11" : : "m" (gfconst16.low4[0]));

	for(i=0;i<size;i+=32) {
		/* last disk without the by two multiplication */
		asm volatile("movdqa %0,%%xmm4" : : "m" (vbuf[l][i]));
		asm volatile("movdqa %0,%%xmm12" : : "m" (vbuf[l][i+16]));

		asm volatile("movdqa %xmm4,%xmm0");
		asm volatile("movdqa %xmm4,%xmm1");
		asm volatile("movdqa %xmm12,%xmm8");
		asm volatile("movdqa %xmm12,%xmm9");

		asm volatile("movdqa %xmm4,%xmm5");
		asm volatile("movdqa %xmm12,%xmm13");
		asm volatile("psrlw  $4,%xmm5");
		asm volatile("psrlw  $4,%xmm13");
		asm volatile("pand   %xmm11,%xmm4");
		asm volatile("pand   %xmm11,%xmm12");
		asm volatile("pand   %xmm11,%xmm5");
		asm volatile("pand   %xmm11,%xmm13");

		asm volatile("movdqa %0,%%xmm2" : : "m" (gfgenpshufb[l][0][0][0]));
		asm volatile("movdqa %0,%%xmm7" : : "m" (gfgenpshufb[l][0][1][0]));
		asm volatile("movdqa %xmm2,%xmm10");
		asm volatile("movdqa %xmm7,%xmm15");
		asm volatile("pshufb %xmm4,%xmm2");
		asm volatile("pshufb %xmm12,%xmm10");
		asm volatile("pshufb %xmm5,%xmm7");
		asm volatile("pshufb %xmm13,%xmm15");
		asm volatile("pxor   %xmm7,%xmm2");
		asm volatile("pxor   %xmm15,%xmm10");

		/* intermediate disks */
		for(d=l-1;d>0;--d) {
			asm volatile("movdqa %0,%%xmm4" : : "m" (vbuf[d][i]));
			asm volatile("movdqa %0,%%xmm12" : : "m" (vbuf[d][i+16]));

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
			asm volatile("psrlw  $4,%xmm5");
			asm volatile("psrlw  $4,%xmm13");
			asm volatile("pand   %xmm11,%xmm4");
			asm volatile("pand   %xmm11,%xmm12");
			asm volatile("pand   %xmm11,%xmm5");
			asm volatile("pand   %xmm11,%xmm13");

			asm volatile("movdqa %0,%%xmm6" : : "m" (gfgenpshufb[d][0][0][0]));
			asm volatile("movdqa %0,%%xmm7" : : "m" (gfgenpshufb[d][0][1][0]));
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

		/* first disk with all coefficients at 1 */
		asm volatile("movdqa %0,%%xmm4" : : "m" (vbuf[0][i]));
		asm volatile("movdqa %0,%%xmm12" : : "m" (vbuf[0][i+16]));

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
		asm volatile("pxor %xmm4,%xmm2");
		asm volatile("pxor %xmm12,%xmm8");
		asm volatile("pxor %xmm12,%xmm9");
		asm volatile("pxor %xmm12,%xmm10");

		asm volatile("movntdq %%xmm0,%0" : "=m" (p[i]));
		asm volatile("movntdq %%xmm8,%0" : "=m" (p[i+16]));
		asm volatile("movntdq %%xmm1,%0" : "=m" (q[i]));
		asm volatile("movntdq %%xmm9,%0" : "=m" (q[i+16]));
		asm volatile("movntdq %%xmm2,%0" : "=m" (r[i]));
		asm volatile("movntdq %%xmm10,%0" : "=m" (r[i+16]));
	}

	asm volatile("sfence" : : : "memory");
}
#endif

/*
 * PAR4 (quad parity with Cauchy matrix) 8bit C implementation
 * 
 * Note that instead of a generic multiplicationt table, likely resulting
 * in multiple cache misses, a precomputed table could be used. 
 * But this is only a kind of reference function, and we are not really
 * interested in speed.
 */
void raid_par4_int8(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	unsigned char* s;
	int d, l;
	unsigned i;

	uint8_t d0, s0, r0, q0, p0;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];
	r = vbuf[data+2];
	s = vbuf[data+3];

	for(i=0;i<size;i+=1) {
		p0 = q0 = r0 = s0 = 0;
		for(d=l;d>0;--d) {
			d0 = v_8(vbuf[d][i]);

			p0 ^= d0;
			q0 ^= gfmul[d0][gfcauchy[1][d]];
			r0 ^= gfmul[d0][gfcauchy[2][d]];
			s0 ^= gfmul[d0][gfcauchy[3][d]];
		}

		/* first disk with all coefficients at 1 */
		d0 = v_8(vbuf[0][i]);

		p0 ^= d0;
		q0 ^= d0;
		r0 ^= d0;
		s0 ^= d0;

		v_8(p[i]) = p0;
		v_8(q[i]) = q0;
		v_8(r[i]) = r0;
		v_8(s[i]) = s0;
	}
}

#if defined(__i386__) || defined(__x86_64__)
/*
 * PAR4 (quad parity with Cauchy matrix) SSSE3 implementation
 */
void raid_par4_ssse3(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	unsigned char* s;
	int d, l;
	unsigned i;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];
	r = vbuf[data+2];
	s = vbuf[data+3];

	for(i=0;i<size;i+=16) {
		/* last disk without the by two multiplication */
		asm volatile("movdqa %0,%%xmm7" : : "m" (gfconst16.low4[0]));
		asm volatile("movdqa %0,%%xmm4" : : "m" (vbuf[l][i]));

		asm volatile("movdqa %xmm4,%xmm0");
		asm volatile("movdqa %xmm4,%xmm1");

		asm volatile("movdqa %xmm4,%xmm5");
		asm volatile("psrlw  $4,%xmm5");
		asm volatile("pand   %xmm7,%xmm4");
		asm volatile("pand   %xmm7,%xmm5");

		asm volatile("movdqa %0,%%xmm2" : : "m" (gfgenpshufb[l][0][0][0]));
		asm volatile("movdqa %0,%%xmm7" : : "m" (gfgenpshufb[l][0][1][0]));
		asm volatile("pshufb %xmm4,%xmm2");
		asm volatile("pshufb %xmm5,%xmm7");
		asm volatile("pxor   %xmm7,%xmm2");

		asm volatile("movdqa %0,%%xmm3" : : "m" (gfgenpshufb[l][1][0][0]));
		asm volatile("movdqa %0,%%xmm7" : : "m" (gfgenpshufb[l][1][1][0]));
		asm volatile("pshufb %xmm4,%xmm3");
		asm volatile("pshufb %xmm5,%xmm7");
		asm volatile("pxor   %xmm7,%xmm3");

		/* intermediate disks */
		for(d=l-1;d>0;--d) {
			asm volatile("movdqa %0,%%xmm7" : : "m" (gfconst16.poly[0]));
			asm volatile("movdqa %0,%%xmm4" : : "m" (vbuf[d][i]));

			asm volatile("pxor %xmm5,%xmm5");
			asm volatile("pcmpgtb %xmm1,%xmm5");
			asm volatile("paddb %xmm1,%xmm1");
			asm volatile("pand %xmm7,%xmm5");
			asm volatile("pxor %xmm5,%xmm1");

			asm volatile("movdqa %0,%%xmm7" : : "m" (gfconst16.low4[0]));

			asm volatile("pxor %xmm4,%xmm0");
			asm volatile("pxor %xmm4,%xmm1");

			asm volatile("movdqa %xmm4,%xmm5");
			asm volatile("psrlw  $4,%xmm5");
			asm volatile("pand   %xmm7,%xmm4");
			asm volatile("pand   %xmm7,%xmm5");

			asm volatile("movdqa %0,%%xmm6" : : "m" (gfgenpshufb[d][0][0][0]));
			asm volatile("movdqa %0,%%xmm7" : : "m" (gfgenpshufb[d][0][1][0]));
			asm volatile("pshufb %xmm4,%xmm6");
			asm volatile("pshufb %xmm5,%xmm7");
			asm volatile("pxor   %xmm6,%xmm2");
			asm volatile("pxor   %xmm7,%xmm2");

			asm volatile("movdqa %0,%%xmm6" : : "m" (gfgenpshufb[d][1][0][0]));
			asm volatile("movdqa %0,%%xmm7" : : "m" (gfgenpshufb[d][1][1][0]));
			asm volatile("pshufb %xmm4,%xmm6");
			asm volatile("pshufb %xmm5,%xmm7");
			asm volatile("pxor   %xmm6,%xmm3");
			asm volatile("pxor   %xmm7,%xmm3");
		}

		/* first disk with all coefficients at 1 */
		asm volatile("movdqa %0,%%xmm7" : : "m" (gfconst16.poly[0]));
		asm volatile("movdqa %0,%%xmm4" : : "m" (vbuf[0][i]));

		asm volatile("pxor %xmm5,%xmm5");
		asm volatile("pcmpgtb %xmm1,%xmm5");
		asm volatile("paddb %xmm1,%xmm1");
		asm volatile("pand %xmm7,%xmm5");
		asm volatile("pxor %xmm5,%xmm1");

		asm volatile("pxor %xmm4,%xmm0");
		asm volatile("pxor %xmm4,%xmm1");
		asm volatile("pxor %xmm4,%xmm2");
		asm volatile("pxor %xmm4,%xmm3");

		asm volatile("movntdq %%xmm0,%0" : "=m" (p[i]));
		asm volatile("movntdq %%xmm1,%0" : "=m" (q[i]));
		asm volatile("movntdq %%xmm2,%0" : "=m" (r[i]));
		asm volatile("movntdq %%xmm3,%0" : "=m" (s[i]));
	}

	asm volatile("sfence" : : : "memory");
}
#endif

#if defined(__x86_64__)
/*
 * PAR4 (quad parity with Cauchy matrix) SSSE3 implementation
 *
 * Note that it uses 16 registers, meaning that x64 is required.
 */
void raid_par4_ssse3ext(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	unsigned char* s;
	int d, l;
	unsigned i;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];
	r = vbuf[data+2];
	s = vbuf[data+3];

	for(i=0;i<size;i+=32) {
		/* last disk without the by two multiplication */
		asm volatile("movdqa %0,%%xmm15" : : "m" (gfconst16.low4[0]));
		asm volatile("movdqa %0,%%xmm4" : : "m" (vbuf[l][i]));
		asm volatile("movdqa %0,%%xmm12" : : "m" (vbuf[l][i+16]));

		asm volatile("movdqa %xmm4,%xmm0");
		asm volatile("movdqa %xmm4,%xmm1");
		asm volatile("movdqa %xmm12,%xmm8");
		asm volatile("movdqa %xmm12,%xmm9");

		asm volatile("movdqa %xmm4,%xmm5");
		asm volatile("movdqa %xmm12,%xmm13");
		asm volatile("psrlw  $4,%xmm5");
		asm volatile("psrlw  $4,%xmm13");
		asm volatile("pand   %xmm15,%xmm4");
		asm volatile("pand   %xmm15,%xmm12");
		asm volatile("pand   %xmm15,%xmm5");
		asm volatile("pand   %xmm15,%xmm13");

		asm volatile("movdqa %0,%%xmm2" : : "m" (gfgenpshufb[l][0][0][0]));
		asm volatile("movdqa %0,%%xmm7" : : "m" (gfgenpshufb[l][0][1][0]));
		asm volatile("movdqa %xmm2,%xmm10");
		asm volatile("movdqa %xmm7,%xmm15");
		asm volatile("pshufb %xmm4,%xmm2");
		asm volatile("pshufb %xmm12,%xmm10");
		asm volatile("pshufb %xmm5,%xmm7");
		asm volatile("pshufb %xmm13,%xmm15");
		asm volatile("pxor   %xmm7,%xmm2");
		asm volatile("pxor   %xmm15,%xmm10");

		asm volatile("movdqa %0,%%xmm3" : : "m" (gfgenpshufb[l][1][0][0]));
		asm volatile("movdqa %0,%%xmm7" : : "m" (gfgenpshufb[l][1][1][0]));
		asm volatile("movdqa %xmm3,%xmm11");
		asm volatile("movdqa %xmm7,%xmm15");
		asm volatile("pshufb %xmm4,%xmm3");
		asm volatile("pshufb %xmm12,%xmm11");
		asm volatile("pshufb %xmm5,%xmm7");
		asm volatile("pshufb %xmm13,%xmm15");
		asm volatile("pxor   %xmm7,%xmm3");
		asm volatile("pxor   %xmm15,%xmm11");

		/* intermediate disks */
		for(d=l-1;d>0;--d) {
			asm volatile("movdqa %0,%%xmm7" : : "m" (gfconst16.poly[0]));
			asm volatile("movdqa %0,%%xmm15" : : "m" (gfconst16.low4[0]));
			asm volatile("movdqa %0,%%xmm4" : : "m" (vbuf[d][i]));
			asm volatile("movdqa %0,%%xmm12" : : "m" (vbuf[d][i+16]));

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
			asm volatile("psrlw  $4,%xmm5");
			asm volatile("psrlw  $4,%xmm13");
			asm volatile("pand   %xmm15,%xmm4");
			asm volatile("pand   %xmm15,%xmm12");
			asm volatile("pand   %xmm15,%xmm5");
			asm volatile("pand   %xmm15,%xmm13");

			asm volatile("movdqa %0,%%xmm6" : : "m" (gfgenpshufb[d][0][0][0]));
			asm volatile("movdqa %0,%%xmm7" : : "m" (gfgenpshufb[d][0][1][0]));
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

			asm volatile("movdqa %0,%%xmm6" : : "m" (gfgenpshufb[d][1][0][0]));
			asm volatile("movdqa %0,%%xmm7" : : "m" (gfgenpshufb[d][1][1][0]));
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

		/* first disk with all coefficients at 1 */
		asm volatile("movdqa %0,%%xmm7" : : "m" (gfconst16.poly[0]));
		asm volatile("movdqa %0,%%xmm15" : : "m" (gfconst16.low4[0]));
		asm volatile("movdqa %0,%%xmm4" : : "m" (vbuf[0][i]));
		asm volatile("movdqa %0,%%xmm12" : : "m" (vbuf[0][i+16]));

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
		asm volatile("pxor %xmm4,%xmm2");
		asm volatile("pxor %xmm4,%xmm3");
		asm volatile("pxor %xmm12,%xmm8");
		asm volatile("pxor %xmm12,%xmm9");
		asm volatile("pxor %xmm12,%xmm10");
		asm volatile("pxor %xmm12,%xmm11");

		asm volatile("movntdq %%xmm0,%0" : "=m" (p[i]));
		asm volatile("movntdq %%xmm8,%0" : "=m" (p[i+16]));
		asm volatile("movntdq %%xmm1,%0" : "=m" (q[i]));
		asm volatile("movntdq %%xmm9,%0" : "=m" (q[i+16]));
		asm volatile("movntdq %%xmm2,%0" : "=m" (r[i]));
		asm volatile("movntdq %%xmm10,%0" : "=m" (r[i+16]));
		asm volatile("movntdq %%xmm3,%0" : "=m" (s[i]));
		asm volatile("movntdq %%xmm11,%0" : "=m" (s[i+16]));
	}

	asm volatile("sfence" : : : "memory");
}
#endif

/*
 * PAR5 (penta parity with Cauchy matrix) 8bit C implementation
 * 
 * Note that instead of a generic multiplicationt table, likely resulting
 * in multiple cache misses, a precomputed table could be used. 
 * But this is only a kind of reference function, and we are not really
 * interested in speed.
 */
void raid_par5_int8(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	unsigned char* s;
	unsigned char* t;
	int d, l;
	unsigned i;

	uint8_t d0, t0, s0, r0, q0, p0;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];
	r = vbuf[data+2];
	s = vbuf[data+3];
	t = vbuf[data+4];

	for(i=0;i<size;i+=1) {
		p0 = q0 = r0 = s0 = t0 = 0;
		for(d=l;d>0;--d) {
			d0 = v_8(vbuf[d][i]);

			p0 ^= d0;
			q0 ^= gfmul[d0][gfcauchy[1][d]];
			r0 ^= gfmul[d0][gfcauchy[2][d]];
			s0 ^= gfmul[d0][gfcauchy[3][d]];
			t0 ^= gfmul[d0][gfcauchy[4][d]];
		}
		
		/* first disk with all coefficients at 1 */
		d0 = v_8(vbuf[0][i]);

		p0 ^= d0;
		q0 ^= d0;
		r0 ^= d0;
		s0 ^= d0;
		t0 ^= d0;

		v_8(p[i]) = p0;
		v_8(q[i]) = q0;
		v_8(r[i]) = r0;
		v_8(s[i]) = s0;
		v_8(t[i]) = t0;
	}
}

#if defined(__i386__) || defined(__x86_64__)
/*
 * PAR5 (penta parity with Cauchy matrix) SSSE3 implementation
 */
#ifdef _WIN32
/* ensures that stack is aligned at 16 bytes because we allocate SSE registers in it */
__attribute__((force_align_arg_pointer))
#endif
void raid_par5_ssse3(unsigned char** vbuf, unsigned data, unsigned size)
/* ensures that stack is aligned at 16 bytes because we allocate SSE registers in it */
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	unsigned char* s;
	unsigned char* t;
	int d, l;
	unsigned i;
	unsigned char p0[16] __attribute__((aligned(16)));

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];
	r = vbuf[data+2];
	s = vbuf[data+3];
	t = vbuf[data+4];

	for(i=0;i<size;i+=16) {
		/* last disk without the by two multiplication */
		asm volatile("movdqa %0,%%xmm4" : : "m" (vbuf[l][i]));

		asm volatile("movdqa %xmm4,%xmm0");
		asm volatile("movdqa %%xmm4,%0" : "=m" (p0[0]));

		asm volatile("movdqa %0,%%xmm7" : : "m" (gfconst16.low4[0]));
		asm volatile("movdqa %xmm4,%xmm5");
		asm volatile("psrlw  $4,%xmm5");
		asm volatile("pand   %xmm7,%xmm4");
		asm volatile("pand   %xmm7,%xmm5");

		asm volatile("movdqa %0,%%xmm1" : : "m" (gfgenpshufb[l][0][0][0]));
		asm volatile("movdqa %0,%%xmm7" : : "m" (gfgenpshufb[l][0][1][0]));
		asm volatile("pshufb %xmm4,%xmm1");
		asm volatile("pshufb %xmm5,%xmm7");
		asm volatile("pxor   %xmm7,%xmm1");

		asm volatile("movdqa %0,%%xmm2" : : "m" (gfgenpshufb[l][1][0][0]));
		asm volatile("movdqa %0,%%xmm7" : : "m" (gfgenpshufb[l][1][1][0]));
		asm volatile("pshufb %xmm4,%xmm2");
		asm volatile("pshufb %xmm5,%xmm7");
		asm volatile("pxor   %xmm7,%xmm2");

		asm volatile("movdqa %0,%%xmm3" : : "m" (gfgenpshufb[l][2][0][0]));
		asm volatile("movdqa %0,%%xmm7" : : "m" (gfgenpshufb[l][2][1][0]));
		asm volatile("pshufb %xmm4,%xmm3");
		asm volatile("pshufb %xmm5,%xmm7");
		asm volatile("pxor   %xmm7,%xmm3");

		/* intermediate disks */
		for(d=l-1;d>0;--d) {
			asm volatile("movdqa %0,%%xmm4" : : "m" (vbuf[d][i]));
			asm volatile("movdqa %0,%%xmm6" : : "m" (p0[0]));
			asm volatile("movdqa %0,%%xmm7" : : "m" (gfconst16.poly[0]));

			asm volatile("pxor %xmm5,%xmm5");
			asm volatile("pcmpgtb %xmm0,%xmm5");
			asm volatile("paddb %xmm0,%xmm0");
			asm volatile("pand %xmm7,%xmm5");
			asm volatile("pxor %xmm5,%xmm0");

			asm volatile("pxor %xmm4,%xmm0");
			asm volatile("pxor %xmm4,%xmm6");
			asm volatile("movdqa %%xmm6,%0" : "=m" (p0[0]));

			asm volatile("movdqa %0,%%xmm7" : : "m" (gfconst16.low4[0]));
			asm volatile("movdqa %xmm4,%xmm5");
			asm volatile("psrlw  $4,%xmm5");
			asm volatile("pand   %xmm7,%xmm4");
			asm volatile("pand   %xmm7,%xmm5");

			asm volatile("movdqa %0,%%xmm6" : : "m" (gfgenpshufb[d][0][0][0]));
			asm volatile("movdqa %0,%%xmm7" : : "m" (gfgenpshufb[d][0][1][0]));
			asm volatile("pshufb %xmm4,%xmm6");
			asm volatile("pshufb %xmm5,%xmm7");
			asm volatile("pxor   %xmm6,%xmm1");
			asm volatile("pxor   %xmm7,%xmm1");

			asm volatile("movdqa %0,%%xmm6" : : "m" (gfgenpshufb[d][1][0][0]));
			asm volatile("movdqa %0,%%xmm7" : : "m" (gfgenpshufb[d][1][1][0]));
			asm volatile("pshufb %xmm4,%xmm6");
			asm volatile("pshufb %xmm5,%xmm7");
			asm volatile("pxor   %xmm6,%xmm2");
			asm volatile("pxor   %xmm7,%xmm2");

			asm volatile("movdqa %0,%%xmm6" : : "m" (gfgenpshufb[d][2][0][0]));
			asm volatile("movdqa %0,%%xmm7" : : "m" (gfgenpshufb[d][2][1][0]));
			asm volatile("pshufb %xmm4,%xmm6");
			asm volatile("pshufb %xmm5,%xmm7");
			asm volatile("pxor   %xmm6,%xmm3");
			asm volatile("pxor   %xmm7,%xmm3");
		}

		/* first disk with all coefficients at 1 */
		asm volatile("movdqa %0,%%xmm4" : : "m" (vbuf[0][i]));
		asm volatile("movdqa %0,%%xmm6" : : "m" (p0[0]));
		asm volatile("movdqa %0,%%xmm7" : : "m" (gfconst16.poly[0]));

		asm volatile("pxor %xmm5,%xmm5");
		asm volatile("pcmpgtb %xmm0,%xmm5");
		asm volatile("paddb %xmm0,%xmm0");
		asm volatile("pand %xmm7,%xmm5");
		asm volatile("pxor %xmm5,%xmm0");

		asm volatile("pxor %xmm4,%xmm0");
		asm volatile("pxor %xmm4,%xmm1");
		asm volatile("pxor %xmm4,%xmm2");
		asm volatile("pxor %xmm4,%xmm3");
		asm volatile("pxor %xmm4,%xmm6");

		asm volatile("movntdq %%xmm6,%0" : "=m" (p[i]));
		asm volatile("movntdq %%xmm0,%0" : "=m" (q[i]));
		asm volatile("movntdq %%xmm1,%0" : "=m" (r[i]));
		asm volatile("movntdq %%xmm2,%0" : "=m" (s[i]));
		asm volatile("movntdq %%xmm3,%0" : "=m" (t[i]));
	}

	asm volatile("sfence" : : : "memory");
}
#endif

#if defined(__x86_64__)
/*
 * PAR5 (penta parity with Cauchy matrix) SSSE3 implementation
 *
 * Note that it uses 16 registers, meaning that x64 is required.
 */
void raid_par5_ssse3ext(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	unsigned char* s;
	unsigned char* t;
	int d, l;
	unsigned i;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];
	r = vbuf[data+2];
	s = vbuf[data+3];
	t = vbuf[data+4];

	asm volatile("movdqa %0,%%xmm14" : : "m" (gfconst16.poly[0]));
	asm volatile("movdqa %0,%%xmm15" : : "m" (gfconst16.low4[0]));

	for(i=0;i<size;i+=16) {
		/* last disk without the by two multiplication */
		asm volatile("movdqa %0,%%xmm10" : : "m" (vbuf[l][i]));

		asm volatile("movdqa %xmm10,%xmm0");
		asm volatile("movdqa %xmm10,%xmm1");

		asm volatile("movdqa %xmm10,%xmm11");
		asm volatile("psrlw  $4,%xmm11");
		asm volatile("pand   %xmm15,%xmm10");
		asm volatile("pand   %xmm15,%xmm11");

		asm volatile("movdqa %0,%%xmm2" : : "m" (gfgenpshufb[l][0][0][0]));
		asm volatile("movdqa %0,%%xmm13" : : "m" (gfgenpshufb[l][0][1][0]));
		asm volatile("pshufb %xmm10,%xmm2");
		asm volatile("pshufb %xmm11,%xmm13");
		asm volatile("pxor   %xmm13,%xmm2");

		asm volatile("movdqa %0,%%xmm3" : : "m" (gfgenpshufb[l][1][0][0]));
		asm volatile("movdqa %0,%%xmm13" : : "m" (gfgenpshufb[l][1][1][0]));
		asm volatile("pshufb %xmm10,%xmm3");
		asm volatile("pshufb %xmm11,%xmm13");
		asm volatile("pxor   %xmm13,%xmm3");

		asm volatile("movdqa %0,%%xmm4" : : "m" (gfgenpshufb[l][2][0][0]));
		asm volatile("movdqa %0,%%xmm13" : : "m" (gfgenpshufb[l][2][1][0]));
		asm volatile("pshufb %xmm10,%xmm4");
		asm volatile("pshufb %xmm11,%xmm13");
		asm volatile("pxor   %xmm13,%xmm4");

		/* intermediate disks */
		for(d=l-1;d>0;--d) {
			asm volatile("movdqa %0,%%xmm10" : : "m" (vbuf[d][i]));

			asm volatile("pxor %xmm11,%xmm11");
			asm volatile("pcmpgtb %xmm1,%xmm11");
			asm volatile("paddb %xmm1,%xmm1");
			asm volatile("pand %xmm14,%xmm11");
			asm volatile("pxor %xmm11,%xmm1");

			asm volatile("pxor %xmm10,%xmm0");
			asm volatile("pxor %xmm10,%xmm1");

			asm volatile("movdqa %xmm10,%xmm11");
			asm volatile("psrlw  $4,%xmm11");
			asm volatile("pand   %xmm15,%xmm10");
			asm volatile("pand   %xmm15,%xmm11");

			asm volatile("movdqa %0,%%xmm12" : : "m" (gfgenpshufb[d][0][0][0]));
			asm volatile("movdqa %0,%%xmm13" : : "m" (gfgenpshufb[d][0][1][0]));
			asm volatile("pshufb %xmm10,%xmm12");
			asm volatile("pshufb %xmm11,%xmm13");
			asm volatile("pxor   %xmm12,%xmm2");
			asm volatile("pxor   %xmm13,%xmm2");

			asm volatile("movdqa %0,%%xmm12" : : "m" (gfgenpshufb[d][1][0][0]));
			asm volatile("movdqa %0,%%xmm13" : : "m" (gfgenpshufb[d][1][1][0]));
			asm volatile("pshufb %xmm10,%xmm12");
			asm volatile("pshufb %xmm11,%xmm13");
			asm volatile("pxor   %xmm12,%xmm3");
			asm volatile("pxor   %xmm13,%xmm3");

			asm volatile("movdqa %0,%%xmm12" : : "m" (gfgenpshufb[d][2][0][0]));
			asm volatile("movdqa %0,%%xmm13" : : "m" (gfgenpshufb[d][2][1][0]));
			asm volatile("pshufb %xmm10,%xmm12");
			asm volatile("pshufb %xmm11,%xmm13");
			asm volatile("pxor   %xmm12,%xmm4");
			asm volatile("pxor   %xmm13,%xmm4");
		}

		/* first disk with all coefficients at 1 */
		asm volatile("movdqa %0,%%xmm10" : : "m" (vbuf[0][i]));

		asm volatile("pxor %xmm11,%xmm11");
		asm volatile("pcmpgtb %xmm1,%xmm11");
		asm volatile("paddb %xmm1,%xmm1");
		asm volatile("pand %xmm14,%xmm11");
		asm volatile("pxor %xmm11,%xmm1");

		asm volatile("pxor %xmm10,%xmm0");
		asm volatile("pxor %xmm10,%xmm1");
		asm volatile("pxor %xmm10,%xmm2");
		asm volatile("pxor %xmm10,%xmm3");
		asm volatile("pxor %xmm10,%xmm4");

		asm volatile("movntdq %%xmm0,%0" : "=m" (p[i]));
		asm volatile("movntdq %%xmm1,%0" : "=m" (q[i]));
		asm volatile("movntdq %%xmm2,%0" : "=m" (r[i]));
		asm volatile("movntdq %%xmm3,%0" : "=m" (s[i]));
		asm volatile("movntdq %%xmm4,%0" : "=m" (t[i]));
	}

	asm volatile("sfence" : : : "memory");
}
#endif

/*
 * PAR6 (hexa parity with Cauchy matrix) 8bit C implementation
 * 
 * Note that instead of a generic multiplicationt table, likely resulting
 * in multiple cache misses, a precomputed table could be used. 
 * But this is only a kind of reference function, and we are not really
 * interested in speed.
 */
void raid_par6_int8(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	unsigned char* s;
	unsigned char* t;
	unsigned char* u;
	int d, l;
	unsigned i;

	uint8_t d0, u0, t0, s0, r0, q0, p0;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];
	r = vbuf[data+2];
	s = vbuf[data+3];
	t = vbuf[data+4];
	u = vbuf[data+5];

	for(i=0;i<size;i+=1) {
		p0 = q0 = r0 = s0 = t0 = u0 = 0;
		for(d=l;d>0;--d) {
			d0 = v_8(vbuf[d][i]);

			p0 ^= d0;
			q0 ^= gfmul[d0][gfcauchy[1][d]];
			r0 ^= gfmul[d0][gfcauchy[2][d]];
			s0 ^= gfmul[d0][gfcauchy[3][d]];
			t0 ^= gfmul[d0][gfcauchy[4][d]];
			u0 ^= gfmul[d0][gfcauchy[5][d]];
		}
		
		/* first disk with all coefficients at 1 */
		d0 = v_8(vbuf[0][i]);

		p0 ^= d0;
		q0 ^= d0;
		r0 ^= d0;
		s0 ^= d0;
		t0 ^= d0;
		u0 ^= d0;
		
		v_8(p[i]) = p0;
		v_8(q[i]) = q0;
		v_8(r[i]) = r0;
		v_8(s[i]) = s0;
		v_8(t[i]) = t0;
		v_8(u[i]) = u0;
	}
}

#if defined(__i386__) || defined(__x86_64__)
/*
 * PAR6 (hexa parity with Cauchy matrix) SSSE3 implementation
 */
#ifdef _WIN32
/* ensures that stack is aligned at 16 bytes because we allocate SSE registers in it */
__attribute__((force_align_arg_pointer))
#endif
void raid_par6_ssse3(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	unsigned char* s;
	unsigned char* t;
	unsigned char* u;
	int d, l;
	unsigned i;
	unsigned char p0[16] __attribute__((aligned(16)));
	unsigned char q0[16] __attribute__((aligned(16))); 

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];
	r = vbuf[data+2];
	s = vbuf[data+3];
	t = vbuf[data+4];
	u = vbuf[data+5];

	for(i=0;i<size;i+=16) {
		/* last disk without the by two multiplication */
		asm volatile("movdqa %0,%%xmm4" : : "m" (vbuf[l][i]));

		asm volatile("movdqa %%xmm4,%0" : "=m" (p0[0]));
		asm volatile("movdqa %%xmm4,%0" : "=m" (q0[0]));

		asm volatile("movdqa %0,%%xmm7" : : "m" (gfconst16.low4[0]));
		asm volatile("movdqa %xmm4,%xmm5");
		asm volatile("psrlw  $4,%xmm5");
		asm volatile("pand   %xmm7,%xmm4");
		asm volatile("pand   %xmm7,%xmm5");

		asm volatile("movdqa %0,%%xmm0" : : "m" (gfgenpshufb[l][0][0][0]));
		asm volatile("movdqa %0,%%xmm7" : : "m" (gfgenpshufb[l][0][1][0]));
		asm volatile("pshufb %xmm4,%xmm0");
		asm volatile("pshufb %xmm5,%xmm7");
		asm volatile("pxor   %xmm7,%xmm0");

		asm volatile("movdqa %0,%%xmm1" : : "m" (gfgenpshufb[l][1][0][0]));
		asm volatile("movdqa %0,%%xmm7" : : "m" (gfgenpshufb[l][1][1][0]));
		asm volatile("pshufb %xmm4,%xmm1");
		asm volatile("pshufb %xmm5,%xmm7");
		asm volatile("pxor   %xmm7,%xmm1");

		asm volatile("movdqa %0,%%xmm2" : : "m" (gfgenpshufb[l][2][0][0]));
		asm volatile("movdqa %0,%%xmm7" : : "m" (gfgenpshufb[l][2][1][0]));
		asm volatile("pshufb %xmm4,%xmm2");
		asm volatile("pshufb %xmm5,%xmm7");
		asm volatile("pxor   %xmm7,%xmm2");

		asm volatile("movdqa %0,%%xmm3" : : "m" (gfgenpshufb[l][3][0][0]));
		asm volatile("movdqa %0,%%xmm7" : : "m" (gfgenpshufb[l][3][1][0]));
		asm volatile("pshufb %xmm4,%xmm3");
		asm volatile("pshufb %xmm5,%xmm7");
		asm volatile("pxor   %xmm7,%xmm3");

		/* intermediate disks */
		for(d=l-1;d>0;--d) {
			asm volatile("movdqa %0,%%xmm5" : : "m" (p0[0]));
			asm volatile("movdqa %0,%%xmm6" : : "m" (q0[0]));
			asm volatile("movdqa %0,%%xmm7" : : "m" (gfconst16.poly[0]));

			asm volatile("pxor %xmm4,%xmm4");
			asm volatile("pcmpgtb %xmm6,%xmm4");
			asm volatile("paddb %xmm6,%xmm6");
			asm volatile("pand %xmm7,%xmm4");
			asm volatile("pxor %xmm4,%xmm6");

			asm volatile("movdqa %0,%%xmm4" : : "m" (vbuf[d][i]));

			asm volatile("pxor %xmm4,%xmm5");
			asm volatile("pxor %xmm4,%xmm6");
			asm volatile("movdqa %%xmm5,%0" : "=m" (p0[0]));
			asm volatile("movdqa %%xmm6,%0" : "=m" (q0[0]));

			asm volatile("movdqa %0,%%xmm7" : : "m" (gfconst16.low4[0]));
			asm volatile("movdqa %xmm4,%xmm5");
			asm volatile("psrlw  $4,%xmm5");
			asm volatile("pand   %xmm7,%xmm4");
			asm volatile("pand   %xmm7,%xmm5");

			asm volatile("movdqa %0,%%xmm6" : : "m" (gfgenpshufb[d][0][0][0]));
			asm volatile("movdqa %0,%%xmm7" : : "m" (gfgenpshufb[d][0][1][0]));
			asm volatile("pshufb %xmm4,%xmm6");
			asm volatile("pshufb %xmm5,%xmm7");
			asm volatile("pxor   %xmm6,%xmm0");
			asm volatile("pxor   %xmm7,%xmm0");

			asm volatile("movdqa %0,%%xmm6" : : "m" (gfgenpshufb[d][1][0][0]));
			asm volatile("movdqa %0,%%xmm7" : : "m" (gfgenpshufb[d][1][1][0]));
			asm volatile("pshufb %xmm4,%xmm6");
			asm volatile("pshufb %xmm5,%xmm7");
			asm volatile("pxor   %xmm6,%xmm1");
			asm volatile("pxor   %xmm7,%xmm1");

			asm volatile("movdqa %0,%%xmm6" : : "m" (gfgenpshufb[d][2][0][0]));
			asm volatile("movdqa %0,%%xmm7" : : "m" (gfgenpshufb[d][2][1][0]));
			asm volatile("pshufb %xmm4,%xmm6");
			asm volatile("pshufb %xmm5,%xmm7");
			asm volatile("pxor   %xmm6,%xmm2");
			asm volatile("pxor   %xmm7,%xmm2");

			asm volatile("movdqa %0,%%xmm6" : : "m" (gfgenpshufb[d][3][0][0]));
			asm volatile("movdqa %0,%%xmm7" : : "m" (gfgenpshufb[d][3][1][0]));
			asm volatile("pshufb %xmm4,%xmm6");
			asm volatile("pshufb %xmm5,%xmm7");
			asm volatile("pxor   %xmm6,%xmm3");
			asm volatile("pxor   %xmm7,%xmm3");
		}

		/* first disk with all coefficients at 1 */
		asm volatile("movdqa %0,%%xmm5" : : "m" (p0[0]));
		asm volatile("movdqa %0,%%xmm6" : : "m" (q0[0]));
		asm volatile("movdqa %0,%%xmm7" : : "m" (gfconst16.poly[0]));

		asm volatile("pxor %xmm4,%xmm4");
		asm volatile("pcmpgtb %xmm6,%xmm4");
		asm volatile("paddb %xmm6,%xmm6");
		asm volatile("pand %xmm7,%xmm4");
		asm volatile("pxor %xmm4,%xmm6");

		asm volatile("movdqa %0,%%xmm4" : : "m" (vbuf[0][i]));
		asm volatile("pxor %xmm4,%xmm0");
		asm volatile("pxor %xmm4,%xmm1");
		asm volatile("pxor %xmm4,%xmm2");
		asm volatile("pxor %xmm4,%xmm3");
		asm volatile("pxor %xmm4,%xmm5");
		asm volatile("pxor %xmm4,%xmm6");

		asm volatile("movntdq %%xmm5,%0" : "=m" (p[i]));
		asm volatile("movntdq %%xmm6,%0" : "=m" (q[i]));
		asm volatile("movntdq %%xmm0,%0" : "=m" (r[i]));
		asm volatile("movntdq %%xmm1,%0" : "=m" (s[i]));
		asm volatile("movntdq %%xmm2,%0" : "=m" (t[i]));
		asm volatile("movntdq %%xmm3,%0" : "=m" (u[i]));
	}

	asm volatile("sfence" : : : "memory");
}
#endif

#if defined(__x86_64__)
/*
 * PAR6 (hexa parity with Cauchy matrix) SSSE3 implementation
 *
 * Note that it uses 16 registers, meaning that x64 is required.
 */
void raid_par6_ssse3ext(unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* q;
	unsigned char* r;
	unsigned char* s;
	unsigned char* t;
	unsigned char* u;
	int d, l;
	unsigned i;

	l = data - 1;
	p = vbuf[data];
	q = vbuf[data+1];
	r = vbuf[data+2];
	s = vbuf[data+3];
	t = vbuf[data+4];
	u = vbuf[data+5];

	asm volatile("movdqa %0,%%xmm14" : : "m" (gfconst16.poly[0]));
	asm volatile("movdqa %0,%%xmm15" : : "m" (gfconst16.low4[0]));

	for(i=0;i<size;i+=16) {
		/* last disk without the by two multiplication */
		asm volatile("movdqa %0,%%xmm10" : : "m" (vbuf[l][i]));

		asm volatile("movdqa %xmm10,%xmm0");
		asm volatile("movdqa %xmm10,%xmm1");

		asm volatile("movdqa %xmm10,%xmm11");
		asm volatile("psrlw  $4,%xmm11");
		asm volatile("pand   %xmm15,%xmm10");
		asm volatile("pand   %xmm15,%xmm11");

		asm volatile("movdqa %0,%%xmm2" : : "m" (gfgenpshufb[l][0][0][0]));
		asm volatile("movdqa %0,%%xmm13" : : "m" (gfgenpshufb[l][0][1][0]));
		asm volatile("pshufb %xmm10,%xmm2");
		asm volatile("pshufb %xmm11,%xmm13");
		asm volatile("pxor   %xmm13,%xmm2");

		asm volatile("movdqa %0,%%xmm3" : : "m" (gfgenpshufb[l][1][0][0]));
		asm volatile("movdqa %0,%%xmm13" : : "m" (gfgenpshufb[l][1][1][0]));
		asm volatile("pshufb %xmm10,%xmm3");
		asm volatile("pshufb %xmm11,%xmm13");
		asm volatile("pxor   %xmm13,%xmm3");

		asm volatile("movdqa %0,%%xmm4" : : "m" (gfgenpshufb[l][2][0][0]));
		asm volatile("movdqa %0,%%xmm13" : : "m" (gfgenpshufb[l][2][1][0]));
		asm volatile("pshufb %xmm10,%xmm4");
		asm volatile("pshufb %xmm11,%xmm13");
		asm volatile("pxor   %xmm13,%xmm4");

		asm volatile("movdqa %0,%%xmm5" : : "m" (gfgenpshufb[l][3][0][0]));
		asm volatile("movdqa %0,%%xmm13" : : "m" (gfgenpshufb[l][3][1][0]));
		asm volatile("pshufb %xmm10,%xmm5");
		asm volatile("pshufb %xmm11,%xmm13");
		asm volatile("pxor   %xmm13,%xmm5");

		/* intermediate disks */
		for(d=l-1;d>0;--d) {
			asm volatile("movdqa %0,%%xmm10" : : "m" (vbuf[d][i]));

			asm volatile("pxor %xmm11,%xmm11");
			asm volatile("pcmpgtb %xmm1,%xmm11");
			asm volatile("paddb %xmm1,%xmm1");
			asm volatile("pand %xmm14,%xmm11");
			asm volatile("pxor %xmm11,%xmm1");

			asm volatile("pxor %xmm10,%xmm0");
			asm volatile("pxor %xmm10,%xmm1");

			asm volatile("movdqa %xmm10,%xmm11");
			asm volatile("psrlw  $4,%xmm11");
			asm volatile("pand   %xmm15,%xmm10");
			asm volatile("pand   %xmm15,%xmm11");

			asm volatile("movdqa %0,%%xmm12" : : "m" (gfgenpshufb[d][0][0][0]));
			asm volatile("movdqa %0,%%xmm13" : : "m" (gfgenpshufb[d][0][1][0]));
			asm volatile("pshufb %xmm10,%xmm12");
			asm volatile("pshufb %xmm11,%xmm13");
			asm volatile("pxor   %xmm12,%xmm2");
			asm volatile("pxor   %xmm13,%xmm2");

			asm volatile("movdqa %0,%%xmm12" : : "m" (gfgenpshufb[d][1][0][0]));
			asm volatile("movdqa %0,%%xmm13" : : "m" (gfgenpshufb[d][1][1][0]));
			asm volatile("pshufb %xmm10,%xmm12");
			asm volatile("pshufb %xmm11,%xmm13");
			asm volatile("pxor   %xmm12,%xmm3");
			asm volatile("pxor   %xmm13,%xmm3");

			asm volatile("movdqa %0,%%xmm12" : : "m" (gfgenpshufb[d][2][0][0]));
			asm volatile("movdqa %0,%%xmm13" : : "m" (gfgenpshufb[d][2][1][0]));
			asm volatile("pshufb %xmm10,%xmm12");
			asm volatile("pshufb %xmm11,%xmm13");
			asm volatile("pxor   %xmm12,%xmm4");
			asm volatile("pxor   %xmm13,%xmm4");

			asm volatile("movdqa %0,%%xmm12" : : "m" (gfgenpshufb[d][3][0][0]));
			asm volatile("movdqa %0,%%xmm13" : : "m" (gfgenpshufb[d][3][1][0]));
			asm volatile("pshufb %xmm10,%xmm12");
			asm volatile("pshufb %xmm11,%xmm13");
			asm volatile("pxor   %xmm12,%xmm5");
			asm volatile("pxor   %xmm13,%xmm5");
		}

		/* first disk with all coefficients at 1 */
		asm volatile("movdqa %0,%%xmm10" : : "m" (vbuf[0][i]));

		asm volatile("pxor %xmm11,%xmm11");
		asm volatile("pcmpgtb %xmm1,%xmm11");
		asm volatile("paddb %xmm1,%xmm1");
		asm volatile("pand %xmm14,%xmm11");
		asm volatile("pxor %xmm11,%xmm1");

		asm volatile("pxor %xmm10,%xmm0");
		asm volatile("pxor %xmm10,%xmm1");
		asm volatile("pxor %xmm10,%xmm2");
		asm volatile("pxor %xmm10,%xmm3");
		asm volatile("pxor %xmm10,%xmm4");
		asm volatile("pxor %xmm10,%xmm5");

		asm volatile("movntdq %%xmm0,%0" : "=m" (p[i]));
		asm volatile("movntdq %%xmm1,%0" : "=m" (q[i]));
		asm volatile("movntdq %%xmm2,%0" : "=m" (r[i]));
		asm volatile("movntdq %%xmm3,%0" : "=m" (s[i]));
		asm volatile("movntdq %%xmm4,%0" : "=m" (t[i]));
		asm volatile("movntdq %%xmm5,%0" : "=m" (u[i]));
	}

	asm volatile("sfence" : : : "memory");
}
#endif

/****************************************************************************/
/* mode */

static unsigned raid_mode = RAID_MODE_CAUCHY; /**< RAID mode. */

void raid_set(unsigned mode)
{
	raid_mode = mode;
}

/****************************************************************************/
/* parity generation */

/* internal forwarder */
static void (*raid_par1_ptr)(unsigned char** vbuf, unsigned data, unsigned size);
static void (*raid_par2_ptr)(unsigned char** vbuf, unsigned data, unsigned size);
static void (*raid_parz_ptr)(unsigned char** vbuf, unsigned data, unsigned size);
static void (*raid_par3_ptr)(unsigned char** vbuf, unsigned data, unsigned size);
static void (*raid_par4_ptr)(unsigned char** vbuf, unsigned data, unsigned size);
static void (*raid_par5_ptr)(unsigned char** vbuf, unsigned data, unsigned size);
static void (*raid_par6_ptr)(unsigned char** vbuf, unsigned data, unsigned size);

void raid_par(unsigned level, unsigned char** vbuf, unsigned data, unsigned size)
{
	switch (level) {
	case 1 : raid_par1_ptr(vbuf, data, size); break;
	case 2 : raid_par2_ptr(vbuf, data, size); break;
	case 3 :
		if (raid_mode == RAID_MODE_CAUCHY)
			raid_par3_ptr(vbuf, data, size);
		else
			raid_parz_ptr(vbuf, data, size);
		break;
	case 4 : raid_par4_ptr(vbuf, data, size); break;
	case 5 : raid_par5_ptr(vbuf, data, size); break;
	case 6 : raid_par6_ptr(vbuf, data, size); break;
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
__attribute__((always_inline))
static inline unsigned char mul(unsigned char a, unsigned char b)
{
	return gfmul[a][b];
}

/**
 * GF 1/a.
 * Not defined for a == 0.
 */
__attribute__((always_inline)) 
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
__attribute__((always_inline))
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
__attribute__((always_inline)) 
static inline const unsigned char* table(unsigned char v)
{
	return gfmul[v];
}

/**
 * Computes the parity without the missing data blocks
 * and store it in the buffers of such data blocks.
 *
 * This is the parity expressed as Pa,Qa,Ra,Sa,Ta,Ua
 * in the equations.
 *
 * Note that all the other parities not in the c[] vector
 * are destroyed.
 */
static inline void raid_delta_gen(unsigned level, const int* d, const int* c, unsigned char** vbuf, unsigned data, unsigned char* zero, unsigned size)
{
	unsigned char* p[RAID_PARITY_CAUCHY_MAX];
	unsigned char* pa[RAID_PARITY_CAUCHY_MAX];
	unsigned i;

	assert(level > 0 && level <= RAID_PARITY_CAUCHY_MAX);

	for(i=0;i<level;++i) {
		/* keep a copy of the parity buffer */
		p[i] = vbuf[data+c[i]];

		/* buffer for missing data blocks */
		pa[i] = vbuf[d[i]];

		/* set at zero the missing data blocks */
		vbuf[d[i]] = zero;

		/* compute the parity over the missing data blocks */
		vbuf[data+c[i]] = pa[i];
	}

	/* compute only for the level we really need */
	raid_par(c[level - 1] + 1, vbuf, data, size);

	for(i=0;i<level;++i) {
		/* restore disk buffers as before */
		vbuf[d[i]] = pa[i];

		/* restore parity buffers as before */
		vbuf[data+c[i]] = p[i];
	}
}

/****************************************************************************/
/* specialized raid recovering */

/**
 * Recover failure of one data block for PAR1.
 *
 * Starting from the equation:
 *
 * Pd = Dx
 *
 * and solving we get:
 *
 * Dx = Pd
 */
static void raid_rec1_par1(const int* d, unsigned char** vbuf, unsigned data, unsigned size)
{
	unsigned char* p;
	unsigned char* pa;

	/* for PAR1 we can directly compute the missing block */
	/* and we don't need to use the zero buffer */
	p = vbuf[data];
	pa = vbuf[d[0]];

	/* use the parity as missing data block */
	vbuf[d[0]] = p;

	/* compute the parity over the missing data block */
	vbuf[data] = pa;

	/* compute */
	raid_par1_ptr(vbuf, data, size);

	/* restore as before */
	vbuf[d[0]] = pa;
	vbuf[data] = p;
}

/**
 * Recover failure of two data blocks for PAR2.
 *
 * Starting from the equations:
 *
 * Pd = Dx + Dy
 * Qd = 2^d[0] * Dx + 2^d[1] * Dy
 *
 * and solving we get:
 *
 *            1               2^(-d[0])
 * Dy = ----------- * Pd + ----------- * Qd
 *      2^(d[1]-d[0]) + 1        2^(d[1]-d[0]) + 1
 *
 * Dx = Dy + Pd
 *
 * with conditions:
 *
 * 2^d[0] != 0
 * 2^(d[1]-d[0]) + 1 != 0
 *
 * That are always satisfied for any 0<=d[0]<d[1]<255.
 */
static void raid_rec2_par2(const int* d, const int* c, unsigned char** vbuf, unsigned data, unsigned char* zero, unsigned size)
{
	unsigned i;
	unsigned char* p;
	unsigned char* pa;
	unsigned char* q;
	unsigned char* qa;
	const unsigned char* T[2];

	/* get multiplication tables */
	T[0] = table( inv(pow2(d[1]-d[0]) ^ 1) );
	T[1] = table( inv(pow2(d[0]) ^ pow2(d[1])) );

	/* compute delta parity */
	raid_delta_gen(2, d, c, vbuf, data, zero, size);

	p = vbuf[data];
	q = vbuf[data+1];
	pa = vbuf[d[0]];
	qa = vbuf[d[1]];

	for(i=0;i<size;++i) {
		/* delta */
		unsigned char Pd = p[i] ^ pa[i];
		unsigned char Qd = q[i] ^ qa[i];

		/* reconstruct */
		unsigned char Dy = T[0][Pd] ^ T[1][Qd];
		unsigned char Dx = Pd ^ Dy;

		/* set */
		pa[i] = Dx;
		qa[i] = Dy;
	}
}

/****************************************************************************/
/* generic matrix recovering */

/**
 * Gets the generator matrix coefficient for parity 'p' and disk 'd'.
 */
static unsigned char A(unsigned p, unsigned d)
{
	if (raid_mode == RAID_MODE_CAUCHY)
		return gfcauchy[p][d];
	else
		return gfvandermonde[p][d];
}

/**
 * Inverts the square matrix M of size nxn into V.
 * We use Gauss elimination to invert.
 */
static inline void invert(unsigned char* M, unsigned char* V, unsigned n)
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
 * Recover failure of one data block at index d[0] using parity at index c[0] for any RAID level.
 *
 * Starting from the equation:
 *
 * Pd = A[c[0],d[0]] * Dx
 *
 * and solving we get:
 *
 * Dx = A[c[0],d[0]]^-1 * Pd
 */
void raid_rec1_int8(unsigned level, const int* d, const int* c, unsigned char** vbuf, unsigned data, unsigned char* zero, unsigned size)
{
	unsigned i;
	unsigned char* p;
	unsigned char* pa;
	const unsigned char* T;
	unsigned char G;
	unsigned char V;

	(void)level; /* unused */

	/* if it's PAR1 uses the dedicated and fastest function */
	if (c[0] == 0) {
		raid_rec1_par1(d, vbuf, data, size);
		return;
	}

	/* setup the coefficients matrix */
	G = A(c[0],d[0]);

	/* invert it to solve the system of linear equations */
	V = inv(G);

	/* get multiplication tables */
	T = table(V);

	/* compute delta parity */
	raid_delta_gen(1, d, c, vbuf, data, zero, size);

	p = vbuf[data+c[0]];
	pa = vbuf[d[0]];

	for(i=0;i<size;++i) {
		/* delta */
		unsigned char Pd = p[i] ^ pa[i];

		/* reconstruct */
		pa[i] = T[Pd];
	}
}

/**
 * Recover failure of two data blocks at indexes d[0],d[1] using parity at indexes c[0],c[1] for any RAID level.
 *
 * Starting from the equations:
 *
 * Pd = A[c[0],d[0]] * Dx + A[c[0],d[1]] * Dy
 * Qd = A[c[1],d[0]] * Dx + A[c[1],d[1]] * Dy
 *
 * we solve inverting the coefficients matrix.
 */
void raid_rec2_int8(unsigned level, const int* d, const int* c, unsigned char** vbuf, unsigned data, unsigned char* zero, unsigned size)
{
	unsigned char* p;
	unsigned char* pa;
	unsigned char* q;
	unsigned char* qa;
	const unsigned N = 2;
	const unsigned char* T[N][N];
	unsigned char G[N*N];
	unsigned char V[N*N];
	unsigned i, j;

	(void)level; /* unused */

	/* if it's PAR2 uses the dedicated and fastest function */
	if (c[0] == 0 && c[1] == 1) {
		raid_rec2_par2(d, c, vbuf, data, zero, size);
		return;
	}

	/* setup the coefficients matrix */
	for(i=0;i<N;++i) {
		for(j=0;j<N;++j) {
			G[i*N+j] = A(c[i],d[j]);
		}
	}

	/* invert it to solve the system of linear equations */
	invert(G, V, N);

	/* get multiplication tables */
	for(i=0;i<N;++i) {
		for(j=0;j<N;++j) {
			T[i][j] = table( V[i*N+j] );
		}
	}

	/* compute delta parity */
	raid_delta_gen(2, d, c, vbuf, data, zero, size);

	p = vbuf[data+c[0]];
	q = vbuf[data+c[1]];
	pa = vbuf[d[0]];
	qa = vbuf[d[1]];

	for(i=0;i<size;++i) {
		/* delta */
		unsigned char Pd = p[i] ^ pa[i];
		unsigned char Qd = q[i] ^ qa[i];

		/* reconstruct */
		pa[i] = T[0][0][Pd] ^ T[0][1][Qd];
		qa[i] = T[1][0][Pd] ^ T[1][1][Qd];
	}
}

/**
 * Recover failure of N data blocks at indexes d[N] using parity at indexes c[N] for any RAID level.
 *
 * Starting from the N equations, with 0<=i<N :
 *
 * PD[i] = sum(A[c[i],d[j]] * D[i]) 0<=j<N
 *
 * we solve inverting the coefficients matrix.
 *
 * Note that referring at previous equations you have:
 * PD[0] = Pd, PD[1] = Qd, PD[2] = Rd, ...
 * D[0] = Dx, D[1] = Dy, D[2] = Dz, ...
 */
void raid_recX_int8(unsigned level, const int* d, const int* c, unsigned char** vbuf, unsigned data, unsigned char* zero, unsigned size)
{
	unsigned char* p[RAID_PARITY_CAUCHY_MAX];
	unsigned char* pa[RAID_PARITY_CAUCHY_MAX];
	const unsigned char* T[RAID_PARITY_CAUCHY_MAX][RAID_PARITY_CAUCHY_MAX];
	unsigned char G[RAID_PARITY_CAUCHY_MAX*RAID_PARITY_CAUCHY_MAX];
	unsigned char V[RAID_PARITY_CAUCHY_MAX*RAID_PARITY_CAUCHY_MAX];
	unsigned i, j, k;

	/* setup the coefficients matrix */
	for(i=0;i<level;++i) {
		for(j=0;j<level;++j) {
			G[i*level+j] = A(c[i],d[j]);
		}
	}

	/* invert it to solve the system of linear equations */
	invert(G, V, level);

	/* get multiplication tables */
	for(i=0;i<level;++i) {
		for(j=0;j<level;++j) {
			T[i][j] = table( V[i*level+j] );
		}
	}

	/* compute delta parity */
	raid_delta_gen(level, d, c, vbuf, data, zero, size);

	for(i=0;i<level;++i) {
		p[i] = vbuf[data+c[i]];
		pa[i] = vbuf[d[i]];
	}

	for(i=0;i<size;++i) {
		unsigned char PD[RAID_PARITY_CAUCHY_MAX];

		/* delta */
		for(j=0;j<level;++j)
			PD[j] = p[j][i] ^ pa[j][i];

		/* reconstruct */
		for(j=0;j<level;++j) {
			unsigned char b = 0;
			for(k=0;k<level;++k)
				b ^= T[j][k][PD[k]];
			pa[j][i] = b;
		}
	}
}

#if defined(__i386__) || defined(__x86_64__)
/*
 * RAID recovering for one disk SSSE3 implementation
 */
void raid_rec1_ssse3(unsigned level, const int* d, const int* c, unsigned char** vbuf, unsigned data, unsigned char* zero, unsigned size)
{
	unsigned char* p;
	unsigned char* pa;
	unsigned char G;
	unsigned char V;
	unsigned i;

	(void)level; /* unused */

	/* if it's PAR1 uses the dedicated and fastest function */
	if (c[0] == 0) {
		raid_rec1_par1(d, vbuf, data, size);
		return;
	}

	/* setup the coefficients matrix */
	G = A(c[0],d[0]);

	/* invert it to solve the system of linear equations */
	V = inv(G);

	/* compute delta parity */
	raid_delta_gen(1, d, c, vbuf, data, zero, size);

	p = vbuf[data+c[0]];
	pa = vbuf[d[0]];

	asm volatile("movdqa %0,%%xmm7" : : "m" (gfconst16.low4[0]));
	asm volatile("movdqa %0,%%xmm4" : : "m" (gfmulpshufb[V][0][0]));
	asm volatile("movdqa %0,%%xmm5" : : "m" (gfmulpshufb[V][1][0]));

	for(i=0;i<size;i+=16) {
		asm volatile("movdqa %0,%%xmm0" : : "m" (p[i]));
		asm volatile("movdqa %0,%%xmm1" : : "m" (pa[i]));
		asm volatile("movdqa %xmm4,%xmm2");
		asm volatile("movdqa %xmm5,%xmm3");
		asm volatile("pxor   %xmm0,%xmm1");
		asm volatile("movdqa %xmm1,%xmm0");
		asm volatile("psrlw  $4,%xmm1");
		asm volatile("pand   %xmm7,%xmm0");
		asm volatile("pand   %xmm7,%xmm1");
		asm volatile("pshufb %xmm0,%xmm2");
		asm volatile("pshufb %xmm1,%xmm3");
		asm volatile("pxor   %xmm3,%xmm2");
		asm volatile("movdqa %%xmm2,%0" : "=m" (pa[i]));
	}

	asm volatile("sfence" : : : "memory");
}
#endif

#if defined(__i386__) || defined(__x86_64__)
/*
 * RAID recovering for two disks SSSE3 implementation
 */
void raid_rec2_ssse3(unsigned level, const int* d, const int* c, unsigned char** vbuf, unsigned data, unsigned char* zero, unsigned size)
{
	const unsigned N = 2;
	unsigned char* p[N];
	unsigned char* pa[N];
	unsigned char G[N*N];
	unsigned char V[N*N];
	unsigned i, j;

	(void)level; /* unused */

	/* setup the coefficients matrix */
	for(i=0;i<N;++i) {
		for(j=0;j<N;++j) {
			G[i*N+j] = A(c[i],d[j]);
		}
	}

	/* invert it to solve the system of linear equations */
	invert(G, V, N);

	/* compute delta parity */
	raid_delta_gen(N, d, c, vbuf, data, zero, size);

	for(i=0;i<N;++i) {
		p[i] = vbuf[data+c[i]];
		pa[i] = vbuf[d[i]];
	}

	asm volatile("movdqa %0,%%xmm7" : : "m" (gfconst16.low4[0]));

	for(i=0;i<size;i+=16) {
		asm volatile("movdqa %0,%%xmm0" : : "m" (p[0][i]));
		asm volatile("movdqa %0,%%xmm2" : : "m" (pa[0][i]));
		asm volatile("movdqa %0,%%xmm1" : : "m" (p[1][i]));
		asm volatile("movdqa %0,%%xmm3" : : "m" (pa[1][i]));
		asm volatile("pxor   %xmm2,%xmm0");
		asm volatile("pxor   %xmm3,%xmm1");

		asm volatile("pxor %xmm6,%xmm6");

		asm volatile("movdqa %0,%%xmm2" : : "m" (gfmulpshufb[V[0]][0][0]));
		asm volatile("movdqa %0,%%xmm3" : : "m" (gfmulpshufb[V[0]][1][0]));
		asm volatile("movdqa %xmm0,%xmm4");
		asm volatile("movdqa %xmm0,%xmm5");
		asm volatile("psrlw  $4,%xmm5");
		asm volatile("pand   %xmm7,%xmm4");
		asm volatile("pand   %xmm7,%xmm5");
		asm volatile("pshufb %xmm4,%xmm2");
		asm volatile("pshufb %xmm5,%xmm3");
		asm volatile("pxor   %xmm2,%xmm6");
		asm volatile("pxor   %xmm3,%xmm6");

		asm volatile("movdqa %0,%%xmm2" : : "m" (gfmulpshufb[V[1]][0][0]));
		asm volatile("movdqa %0,%%xmm3" : : "m" (gfmulpshufb[V[1]][1][0]));
		asm volatile("movdqa %xmm1,%xmm4");
		asm volatile("movdqa %xmm1,%xmm5");
		asm volatile("psrlw  $4,%xmm5");
		asm volatile("pand   %xmm7,%xmm4");
		asm volatile("pand   %xmm7,%xmm5");
		asm volatile("pshufb %xmm4,%xmm2");
		asm volatile("pshufb %xmm5,%xmm3");
		asm volatile("pxor   %xmm2,%xmm6");
		asm volatile("pxor   %xmm3,%xmm6");

		asm volatile("movdqa %%xmm6,%0" : "=m" (pa[0][i]));

		asm volatile("pxor %xmm6,%xmm6");

		asm volatile("movdqa %0,%%xmm2" : : "m" (gfmulpshufb[V[2]][0][0]));
		asm volatile("movdqa %0,%%xmm3" : : "m" (gfmulpshufb[V[2]][1][0]));
		asm volatile("movdqa %xmm0,%xmm4");
		asm volatile("movdqa %xmm0,%xmm5");
		asm volatile("psrlw  $4,%xmm5");
		asm volatile("pand   %xmm7,%xmm4");
		asm volatile("pand   %xmm7,%xmm5");
		asm volatile("pshufb %xmm4,%xmm2");
		asm volatile("pshufb %xmm5,%xmm3");
		asm volatile("pxor   %xmm2,%xmm6");
		asm volatile("pxor   %xmm3,%xmm6");

		asm volatile("movdqa %0,%%xmm2" : : "m" (gfmulpshufb[V[3]][0][0]));
		asm volatile("movdqa %0,%%xmm3" : : "m" (gfmulpshufb[V[3]][1][0]));
		asm volatile("movdqa %xmm1,%xmm4");
		asm volatile("movdqa %xmm1,%xmm5");
		asm volatile("psrlw  $4,%xmm5");
		asm volatile("pand   %xmm7,%xmm4");
		asm volatile("pand   %xmm7,%xmm5");
		asm volatile("pshufb %xmm4,%xmm2");
		asm volatile("pshufb %xmm5,%xmm3");
		asm volatile("pxor   %xmm2,%xmm6");
		asm volatile("pxor   %xmm3,%xmm6");

		asm volatile("movdqa %%xmm6,%0" : "=m" (pa[1][i]));
	}

	asm volatile("sfence" : : : "memory");
}
#endif

#if defined(__i386__) || defined(__x86_64__)
/*
 * RAID recovering SSSE3 implementation
 */
void raid_recX_ssse3(unsigned level, const int* d, const int* c, unsigned char** vbuf, unsigned data, unsigned char* zero, unsigned size)
{
	unsigned N = level;
	unsigned char* p[RAID_PARITY_CAUCHY_MAX];
	unsigned char* pa[RAID_PARITY_CAUCHY_MAX];
	unsigned char G[RAID_PARITY_CAUCHY_MAX*RAID_PARITY_CAUCHY_MAX];
	unsigned char V[RAID_PARITY_CAUCHY_MAX*RAID_PARITY_CAUCHY_MAX];
	unsigned i, j, k;

	/* setup the coefficients matrix */
	for(i=0;i<N;++i) {
		for(j=0;j<N;++j) {
			G[i*N+j] = A(c[i],d[j]);
		}
	}

	/* invert it to solve the system of linear equations */
	invert(G, V, N);

	/* compute delta parity */
	raid_delta_gen(N, d, c, vbuf, data, zero, size);

	for(i=0;i<N;++i) {
		p[i] = vbuf[data+c[i]];
		pa[i] = vbuf[d[i]];
	}

	asm volatile("movdqa %0,%%xmm7" : : "m" (gfconst16.low4[0]));

	for(i=0;i<size;i+=16) {
		unsigned char PD[RAID_PARITY_CAUCHY_MAX][16] __attribute__((aligned(16)));

		/* delta */
		for(j=0;j<N;++j) {
			asm volatile("movdqa %0,%%xmm0" : : "m" (p[j][i]));
			asm volatile("movdqa %0,%%xmm1" : : "m" (pa[j][i]));
			asm volatile("pxor   %xmm1,%xmm0");
			asm volatile("movdqa %%xmm0,%0" : "=m" (PD[j][0]));
		}

		/* reconstruct */
		for(j=0;j<N;++j) {
			asm volatile("pxor %xmm0,%xmm0");
			asm volatile("pxor %xmm1,%xmm1");

			for(k=0;k<N;++k) {
				unsigned char m = V[j*N+k];

				asm volatile("movdqa %0,%%xmm2" : : "m" (gfmulpshufb[m][0][0]));
				asm volatile("movdqa %0,%%xmm3" : : "m" (gfmulpshufb[m][1][0]));
				asm volatile("movdqa %0,%%xmm4" : : "m" (PD[k][0]));
				asm volatile("movdqa %xmm4,%xmm5");
				asm volatile("psrlw  $4,%xmm5");
				asm volatile("pand   %xmm7,%xmm4");
				asm volatile("pand   %xmm7,%xmm5");
				asm volatile("pshufb %xmm4,%xmm2");
				asm volatile("pshufb %xmm5,%xmm3");
				asm volatile("pxor   %xmm2,%xmm0");
				asm volatile("pxor   %xmm3,%xmm1");
				
			}

			asm volatile("pxor %xmm1,%xmm0");
			asm volatile("movdqa %%xmm0,%0" : "=m" (pa[j][i]));
		}
	}

	asm volatile("sfence" : : : "memory");
}
#endif

/* internal forwarder */
static void (*raid_rec1_ptr)(unsigned level, const int* d, const int* c, unsigned char** vbuf, unsigned data, unsigned char* zero, unsigned size);
static void (*raid_rec2_ptr)(unsigned level, const int* d, const int* c, unsigned char** vbuf, unsigned data, unsigned char* zero, unsigned size);
static void (*raid_recX_ptr)(unsigned level, const int* d, const int* c, unsigned char** vbuf, unsigned data, unsigned char* zero, unsigned size);

void raid_rec(unsigned level, const int* d, const int* c, unsigned char** vbuf, unsigned data, unsigned char* zero, unsigned size)
{
	switch (level) {
	case 1 :
		raid_rec1_ptr(level, d, c, vbuf, data, zero, size);
		break;
	case 2 :
		raid_rec2_ptr(level, d, c, vbuf, data, zero, size);
		break;
	case 3 :
	case 4 :
	case 5 :
	case 6 :
		raid_recX_ptr(level, d, c, vbuf, data, zero, size);
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
	raid_par3_ptr = raid_par3_int8;
	raid_par4_ptr = raid_par4_int8;
	raid_par5_ptr = raid_par5_int8;
	raid_par6_ptr = raid_par6_int8;

	if (sizeof(void*) == 4) {
		raid_par1_ptr = raid_par1_int32;
		raid_par2_ptr = raid_par2_int32;
		raid_parz_ptr = raid_parz_int32;
	} else {
		raid_par1_ptr = raid_par1_int64;
		raid_par2_ptr = raid_par2_int64;
		raid_parz_ptr = raid_parz_int64;
	}

	raid_rec1_ptr = raid_rec1_int8;
	raid_rec2_ptr = raid_rec2_int8;
	raid_recX_ptr = raid_recX_int8;

#if defined(__i386__) || defined(__x86_64__)
	if (cpu_has_sse2()) {
		raid_par1_ptr = raid_par1_sse2;
#if defined(__x86_64__)
		if (cpu_has_slowextendedreg()) {
			raid_par2_ptr = raid_par2_sse2;
			raid_parz_ptr = raid_parz_sse2;
		} else {
			raid_par2_ptr = raid_par2_sse2ext;
			raid_parz_ptr = raid_parz_sse2ext;
		}
#else
		raid_par2_ptr = raid_par2_sse2;
		raid_parz_ptr = raid_parz_sse2;
#endif
	}

	if (cpu_has_ssse3()) {
#if defined(__x86_64__)
		raid_par3_ptr = raid_par3_ssse3ext;
		raid_par4_ptr = raid_par4_ssse3ext;
		raid_par5_ptr = raid_par5_ssse3ext;
		raid_par6_ptr = raid_par6_ssse3ext;
#else
		raid_par3_ptr = raid_par3_ssse3;
		raid_par4_ptr = raid_par4_ssse3;
		raid_par5_ptr = raid_par5_ssse3;
		raid_par6_ptr = raid_par6_ssse3;
#endif
		raid_rec1_ptr = raid_rec1_ssse3;
		raid_rec2_ptr = raid_rec2_ssse3;
		raid_recX_ptr = raid_recX_ssse3;
	}
#endif
}

static struct raid_func {
	const char* name;
	void* p;
} RAID_FUNC[] = {
	{ "int8", raid_par3_int8 },
	{ "int8", raid_par4_int8 },
	{ "int8", raid_par5_int8 },
	{ "int8", raid_par6_int8 },
	{ "int32", raid_par1_int32 },
	{ "int64", raid_par1_int64 },
	{ "int32", raid_par2_int32 },
	{ "int64", raid_par2_int64 },
	{ "int32", raid_parz_int32 },
	{ "int64", raid_parz_int64 },
	{ "int8", raid_rec1_int8 },
	{ "int8", raid_rec2_int8 },
	{ "int8", raid_recX_int8 },

#if defined(__i386__) || defined(__x86_64__)
	{ "sse2", raid_par1_sse2 },
	{ "sse2", raid_par2_sse2 },
	{ "sse2", raid_parz_sse2 },
	{ "ssse3", raid_par3_ssse3 },
	{ "ssse3", raid_par4_ssse3 },
	{ "ssse3", raid_par5_ssse3 },
	{ "ssse3", raid_par6_ssse3 },
	{ "ssse3", raid_rec1_ssse3 },
	{ "ssse3", raid_rec2_ssse3 },
	{ "ssse3", raid_recX_ssse3 },
#endif

#if defined(__x86_64__)
	{ "sse2e", raid_par2_sse2ext },
	{ "sse2e", raid_parz_sse2ext },
	{ "ssse3e", raid_par3_ssse3ext },
	{ "ssse3e", raid_par4_ssse3ext },
	{ "ssse3e", raid_par5_ssse3ext },
	{ "ssse3e", raid_par6_ssse3ext },
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

const char* raid_par1_tag(void)
{
	return raid_tag(raid_par1_ptr);
}

const char* raid_par2_tag(void)
{
	return raid_tag(raid_par2_ptr);
}

const char* raid_parz_tag(void)
{
	return raid_tag(raid_parz_ptr);
}

const char* raid_par3_tag(void)
{
	return raid_tag(raid_par3_ptr);
}

const char* raid_par4_tag(void)
{
	return raid_tag(raid_par4_ptr);
}

const char* raid_par5_tag(void)
{
	return raid_tag(raid_par5_ptr);
}

const char* raid_par6_tag(void)
{
	return raid_tag(raid_par6_ptr);
}

const char* raid_rec1_tag(void)
{
	return raid_tag(raid_rec1_ptr);
}

const char* raid_rec2_tag(void)
{
	return raid_tag(raid_rec2_ptr);
}

const char* raid_recX_tag(void)
{
	return raid_tag(raid_recX_ptr);
}

