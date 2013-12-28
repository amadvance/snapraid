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

#include "internal.h"
#include "gf.h"

/**
 * Generator matrix currently used.
 */
const uint8_t (*raid_gen)[256];

void raid_mode(int mode)
{
	if (mode == RAID_MODE_VANDERMONDE) {
		raid_par_ptr[2] = raid_parz_ptr;
		raid_gen = gfvandermonde;
	} else {
		raid_par_ptr[2] = raid_par3_ptr;
		raid_gen = gfcauchy;
	}
}

/**
 * Buffer filled with 0 used in recovering.
 */
static void *raid_empty_zero_block;

void raid_zero(void *zero)
{
	raid_empty_zero_block = zero;
}

/* internal forwarder */
void (*raid_par_ptr[RAID_PARITY_MAX])(int nd, size_t size, void **vv);
void (*raid_par3_ptr)(int nd, size_t size, void **vv);
void (*raid_parz_ptr)(int nd, size_t size, void **vv);

void raid_par(int np, int nd, size_t size, void **v)
{
	BUG_ON(np < 1 || np > RAID_PARITY_MAX);
	BUG_ON(size % 64 != 0);

	raid_par_ptr[np - 1](nd, size, v);
}

/**
 * Inverts the square matrix M of size nxn into V.
 * We use Gauss elimination to invert.
 */
void raid_invert(uint8_t *M, uint8_t *V, int n)
{
	int i, j, k;

	/* set the identity matrix in V */
	for (i = 0; i < n; ++i)
		for (j = 0; j < n; ++j)
			V[i*n+j] = i == j;

	/* for each element in the diagonal */
	for (k = 0; k < n; ++k) {
		uint8_t f;

		/* the diagonal element cannot be 0 because */
		/* we are inverting matrices with all the square submatrices */
		/* not singular */
		BUG_ON(M[k*n+k] == 0);

		/* make the diagonal element to be 1 */
		f = inv(M[k*n+k]);
		for (j = 0; j < n; ++j) {
			M[k*n+j] = mul(f, M[k*n+j]);
			V[k*n+j] = mul(f, V[k*n+j]);
		}

		/* make all the elements over and under the diagonal to be 0 */
		for (i = 0; i < n; ++i) {
			if (i == k)
				continue;
			f = M[i*n+k];
			for (j = 0; j < n; ++j) {
				M[i*n+j] ^= mul(f, M[k*n+j]);
				V[i*n+j] ^= mul(f, V[k*n+j]);
			}
		}
	}
}

/**
 * Computes the parity without the missing data blocks
 * and store it in the buffers of such data blocks.
 *
 * This is the parity expressed as Pa,Qa,Ra,Sa,Ta,Ua
 * in the equations.
 *
 * Note that all the other parities not in the ip[] vector
 * are destroyed.
 */
void raid_delta_gen(int nr, const int *id, const int *ip, int nd, size_t size, void **v)
{
	void *p[RAID_PARITY_MAX];
	void *pa[RAID_PARITY_MAX];
	int i;

	for (i = 0; i < nr; ++i) {
		/* keep a copy of the parity buffer */
		p[i] = v[nd+ip[i]];

		/* buffer for missing data blocks */
		pa[i] = v[id[i]];

		/* set at zero the missing data blocks */
		v[id[i]] = raid_empty_zero_block;

		/* compute the parity over the missing data blocks */
		v[nd+ip[i]] = pa[i];
	}

	/* recompute the minimal parity required */
	raid_par(ip[nr - 1] + 1, nd, size, v);

	for (i = 0; i < nr; ++i) {
		/* restore disk buffers as before */
		v[id[i]] = pa[i];

		/* restore parity buffers as before */
		v[nd+ip[i]] = p[i];
	}
}

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
void raid_rec1_par1(const int *id, int nd, size_t size, void **v)
{
	void *p;
	void *pa;

	/* for PAR1 we can directly compute the missing block */
	/* and we don't need to use the zero buffer */
	p = v[nd];
	pa = v[id[0]];

	/* use the parity as missing data block */
	v[id[0]] = p;

	/* compute the parity over the missing data block */
	v[nd] = pa;

	/* compute */
	raid_par(1, nd, size, v);

	/* restore as before */
	v[id[0]] = pa;
	v[nd] = p;
}

/**
 * Recover failure of two data blocks for PAR2.
 *
 * Starting from the equations:
 *
 * Pd = Dx + Dy
 * Qd = 2^id[0] * Dx + 2^id[1] * Dy
 *
 * and solving we get:
 *
 *               1                     2^(-id[0])
 * Dy = ------------------- * Pd + ------------------- * Qd
 *      2^(id[1]-id[0]) + 1        2^(id[1]-id[0]) + 1
 *
 * Dx = Dy + Pd
 *
 * with conditions:
 *
 * 2^id[0] != 0
 * 2^(id[1]-id[0]) + 1 != 0
 *
 * That are always satisfied for any 0<=id[0]<id[1]<255.
 */
void raid_rec2_par2(const int *id, const int *ip, int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	size_t i;
	uint8_t *p;
	uint8_t *pa;
	uint8_t *q;
	uint8_t *qa;
	const uint8_t *T[2];

	/* get multiplication tables */
	T[0] = table(inv(pow2(id[1]-id[0]) ^ 1));
	T[1] = table(inv(pow2(id[0]) ^ pow2(id[1])));

	/* compute delta parity */
	raid_delta_gen(2, id, ip, nd, size, vv);

	p = v[nd];
	q = v[nd+1];
	pa = v[id[0]];
	qa = v[id[1]];

	for (i = 0; i < size; ++i) {
		/* delta */
		uint8_t Pd = p[i] ^ pa[i];
		uint8_t Qd = q[i] ^ qa[i];

		/* reconstruct */
		uint8_t Dy = T[0][Pd] ^ T[1][Qd];
		uint8_t Dx = Pd ^ Dy;

		/* set */
		pa[i] = Dx;
		qa[i] = Dy;
	}
}

/* internal forwarder */
void (*raid_rec_ptr[RAID_PARITY_MAX])(int nr, const int *id, const int *ip, int nd, size_t size, void **vv);

void raid_rec(int nr, const int *id, const int *ip, int nd, size_t size, void **v)
{
	BUG_ON(nr > nd);
	BUG_ON(size % 64 != 0);

	/* if failed data is present */
	if (nr != 0)
		raid_rec_ptr[nr - 1](nr, id, ip, nd, size, v);
}

void raid_recpar(int nrd, const int *id, int nrp, int *ip, int np, int nd, size_t size, void **v)
{
	BUG_ON(nrd > nd);
	BUG_ON(nrd + nrp > np);
	BUG_ON(size % 64 != 0);

	/* if failed data is present */
	if (nrd != 0) {
		int iu[RAID_PARITY_MAX];
		int i, j, k;

		/* setup the vector of parities to use */
		for (i = 0, j = 0, k = 0; i < np; ++i) {
			if (ip[j] == i) {
				++j;
			} else {
				iu[k] = i;
				++k;
			}
		}

		/* recover the data, and limit the parity use to needed ones */
		raid_rec_ptr[nrd - 1](nrd, id, iu, nd, size, v);
	}

	/* recompute all the parities up to the last bad one */
	if (nrp != 0)
		raid_par(ip[nrp - 1] + 1, nd, size, v);
}

