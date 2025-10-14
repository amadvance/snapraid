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

#include "internal.h"
#include "gf.h"

/*
 * Cauchy Matrix Construction for MDS RAID with Arbitrary Parity Levels in GF(2^8)
 *
 * This is a RAID implementation operating in the Galois Field GF(2^8) with
 * the primitive polynomial x^8 + x^4 + x^3 + x^2 + 1 (285 decimal), supporting
 * up to six parity levels.
 *
 * For RAID5 and RAID6, it follows the method described in H. Peter Anvin's
 * paper "The mathematics of RAID-6" [1]. Please refer to this paper for a
 * complete explanation.
 *
 * Triple parity was first evaluated using an extension of the same approach,
 * with additional parity coefficients set as powers of 2^-1, defined as:
 *
 *   P = sum(Di)
 *   Q = sum(2^i * Di)
 *   R = sum(2^-i * Di) for 0 <= i < N
 *
 * This approach works efficiently for triple parity because multiplications
 * and divisions by 2 in GF(2^8) are very fast. It is similar to ZFS RAIDZ3,
 * which uses powers of 4 instead of 2^-1.
 *
 * Unfortunately, this method does not extend beyond triple parity because
 * no choice of power coefficients guarantees solvable equations for all
 * combinations of missing disks. This is expected: a Vandermonde matrix,
 * used in this method, does not ensure that all submatrices are nonsingular
 * [2, Chap. 11, Problem 7], which is required for an MDS (Maximum Distance
 * Separable) code [2, Chap. 11, Theorem 8].
 *
 * To overcome this limitation, a Cauchy matrix [3][4] is used for parity
 * computation. A Cauchy matrix ensures that all square submatrices are
 * nonsingular, so the linear system is always solvable for any combination
 * of missing disks. This guarantees an MDS code.
 *
 * How the matrix is obtained:
 *
 *   The matrix is constructed to match Linux RAID coefficients for the
 *   first two rows, while ensuring that all square submatrices are nonsingular.
 *
 *   1. Start by forming a Cauchy matrix with elements defined as 1/(x_i + y_j),
 *      where all x_i and y_j are distinct elements (the textbook definition
 *      of a Cauchy matrix).
 *
 *   2. For the first row (j=0), set x_i = 2^-i and y_0 = 0, resulting in:
 *
 *        row j=0 -> 1/(x_i + y_0) = 1/(2^-i + 0) = 2^i
 *
 *      which reproduces the RAID-6 coefficients.
 *
 *   3. For subsequent rows (j>0), set y_j = 2^j, yielding:
 *
 *        rows j>0 -> 1/(x_i + y_j) = 1/(2^-i + 2^j)
 *
 *      ensuring x_i != y_j for any i >= 0, j >= 1, and i + j < 255.
 *
 *   4. Place a row filled with 1 at the top of the matrix, transforming it
 *      into an Extended Cauchy Matrix. This preserves the nonsingularity
 *      of all square submatrices.
 *
 *      This first row reproduces the RAID-5 coefficients.
 *
 *   5. Adjust each row with a factor so that the first column contains all 1s.
 *      This transformation also preserves the nonsingularity property,
 *      maintaining the MDS code property.
 *
 * Concrete example in GF(256) for k=6, m=4:
 *
 *   First, create a 3x6 Cauchy matrix using x_i = 2^-i and y_0 = 0, y_j = 2^j for j>0:
 *
 *     x = { 1, 142, 71, 173, 216, 108 }
 *     y = { 0, 2, 4 }
 *
 *   The Cauchy matrix is:
 *
 *     1    2    4    8   16   32
 *   244   83   78  183  118   47
 *   167   39  213   59  153   82
 *
 *   Divide row 2 by 244 and row 3 by 167. Then extend it with a row of ones
 *   on top, and it remains MDS. This forms the code for m=4, with RAID-6 as
 *   a subset:
 *
 *     1    1    1    1    1    1 
 *     1    2    4    8   16   32
 *     1  245  210  196  154  113
 *     1  187  166  215  199    7
 *
 * Extending the same computation to k=251 and m=6 gives:
 *
 *   This results in the matrix A[row, col] defined as:
 *
 * 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01...
 * 01 02 04 08 10 20 40 80 1d 3a 74 e8 cd 87 13 26 4c 98 2d 5a b4 75...
 * 01 f5 d2 c4 9a 71 f1 7f fc 87 c1 c6 19 2f 40 55 3d ba 53 04 9c 61...
 * 01 bb a6 d7 c7 07 ce 82 4a 2f a5 9b b6 60 f1 ad e7 f4 06 d2 df 2e...
 * 01 97 7f 9c 7c 18 bd a2 58 1a da 74 70 a3 e5 47 29 07 f5 80 23 e9...
 * 01 2b 3f cf 73 2c d6 ed cb 74 15 78 8a c1 17 c9 89 68 21 ab 76 3b...
 *
 * This matrix supports six levels of parity, one for each row, for up to 251
 * data disks, one for each column. All the 377,342,351,231 square submatrices
 * are nonsingular, verified also by brute-force testing.
 *
 * This matrix can be extended to support any number of parities by simply
 * adding additional rows and removing one column for each new row to maintain
 * the condition i + j < 255.
 * (See mktables.c for more details on how the matrix is generated.)
 *
 * The downside is that generic multiplications are required to compute the
 * parity, not only fast multiplications by 2^n or 2^-n, which negatively
 * affect performance. However, optimized parallel multiplications using SSSE3
 * or AVX2 instructions [1][5] make this approach competitive with the triple
 * parity computation using power coefficients.
 *
 * Another advantage of the Cauchy matrix is that the first two rows can
 * replicate the RAID5 and RAID6 approach, resulting in a compatible extension.
 * SSSE3 or AVX2 instructions are only needed for triple parity or beyond.
 *
 * Parity computation is as follows:
 *
 *   P = sum(Di)
 *   Q = sum(2^i * Di)
 *   R = sum(A[2,i] * Di)
 *   S = sum(A[3,i] * Di)
 *   T = sum(A[4,i] * Di)
 *   U = sum(A[5,i] * Di) for 0 <= i < N
 *
 * Recovery from six disk failures at indices x, y, z, h, v, w (0 <= x < y < z < h < v < w < N)
 * involves computing parity of the remaining N-6 disks:
 *
 *   Pa = sum(Di)
 *   Qa = sum(2^i * Di)
 *   Ra = sum(A[2,i] * Di)
 *   Sa = sum(A[3,i] * Di)
 *   Ta = sum(A[4,i] * Di)
 *   Ua = sum(A[5,i] * Di) for i != x,y,z,h,v,w
 *
 * Defining:
 *
 *   Pd = Pa + P
 *   Qd = Qa + Q
 *   Rd = Ra + R
 *   Sd = Sa + S
 *   Td = Ta + T
 *   Ud = Ua + U
 *
 * yields:
 *
 *   Pd =          Dx +          Dy +          Dz +          Dh +          Dv +          Dw
 *   Qd =    2^x * Dx +    2^y * Dy +    2^z * Dz +    2^h * Dh +    2^v * Dv +    2^w * Dw
 *   Rd = A[2,x] * Dx + A[2,y] * Dy + A[2,z] * Dz + A[2,h] * Dh + A[2,v] * Dv + A[2,w] * Dw
 *   Sd = A[3,x] * Dx + A[3,y] * Dy + A[3,z] * Dz + A[3,h] * Dh + A[3,v] * Dv + A[3,w] * Dw
 *   Td = A[4,x] * Dx + A[4,y] * Dy + A[4,z] * Dz + A[4,h] * Dh + A[4,v] * Dv + A[4,w] * Dw
 *   Ud = A[5,x] * Dx + A[5,y] * Dy + A[5,z] * Dz + A[5,h] * Dh + A[5,v] * Dv + A[5,w] * Dw
 *
 * This linear system is always solvable since the coefficient matrix is
 * nonsingular due to the properties of A[].
 *
 * Example performance on a Intel Core i7-10700 CPU @ 2.90GHz, stripe 256 KiB, 8 data disks:
 *
 *        int8   int32   int64    sse2   sse2e   ssse3  ssse3e    avx2   avx2e
 *  gen1         24995   46562   62683                           77069
 *  gen2          8243   15879   25788   27292                   44907
 *  genz          5509   10588   14485   13920                           25175
 *  gen3  1190                                   13201   14307           27268
 *  gen4   914                                   10061   11006           21328
 *  gen5   746                                    7970    8828           17040
 *  gen6   630                                    6627    7516           14584
 *
 * Values are in MiB/s of data processed by a single thread.
 * Results can be reproduced using "raid/test/speedtest.c".
 *
 * Triple parity with power coefficients "1, 2, 2^-1" is slightly faster than
 * the Cauchy matrix computation if SSSE3 or AVX2 are available:
 *
 *             int8   int32   int64   sse2    ssse3   avx2
 *   genz              2337    2874   10920           18944
 *
 * In conclusion, the use of power coefficients, specifically powers
 * of 1, 2, and 2^-1, is the best option to implement triple parity on CPUs
 * without SSSE3 and AVX2.
 * On modern CPUs with SSSE3 or AVX2, the Cauchy matrix is the best
 * option because it provides a fast and general approach that works for any
 * number of parities.
 *
 * References:
 * [1] Anvin, "The mathematics of RAID-6", 2004
 * [2] MacWilliams, Sloane, "The Theory of Error-Correcting Codes", 1977
 * [3] Blomer, "An XOR-Based Erasure-Resilient Coding Scheme", 1995
 * [4] Roth, "Introduction to Coding Theory", 2006
 * [5] Plank, "Screaming Fast Galois Field Arithmetic Using Intel SIMD Instructions", 2013
 */

/**
 * Generator matrix currently used.
 */
const uint8_t (*raid_gfgen)[256];

void raid_mode(int mode)
{
	if (mode == RAID_MODE_VANDERMONDE) {
		raid_gen_ptr[2] = raid_genz_ptr;
		raid_gfgen = gfvandermonde;
	} else {
		raid_gen_ptr[2] = raid_gen3_ptr;
		raid_gfgen = gfcauchy;
	}
}

/**
 * Buffer filled with 0 used in recovering.
 */
static void *raid_zero_block;

void raid_zero(void *zero)
{
	raid_zero_block = zero;
}

/*
 * Forwarders for parity computation.
 *
 * These functions compute the parity blocks from the provided data.
 *
 * The number of parities to compute is implicit in the position in the
 * forwarder vector. Position at index #i, computes (#i+1) parities.
 *
 * All these functions give the guarantee that parities are written
 * in order. First parity P, then parity Q, and so on.
 * This allows to specify the same memory buffer for multiple parities
 * knowing that you'll get the latest written one.
 * This characteristic is used by the raid_delta_gen() function to
 * avoid to damage unused parities in recovering.
 *
 * @nd Number of data blocks
 * @size Size of the blocks pointed by @v. It must be a multiplier of 64.
 * @v Vector of pointers to the blocks of data and parity.
 *   It has (@nd + #parities) elements. The starting elements are the blocks
 *   for data, following with the parity blocks.
 *   Each block has @size bytes.
 */
void (*raid_gen_ptr[RAID_PARITY_MAX])(int nd, size_t size, void **vv);
void (*raid_gen3_ptr)(int nd, size_t size, void **vv);
void (*raid_genz_ptr)(int nd, size_t size, void **vv);

void raid_gen(int nd, int np, size_t size, void **v)
{
	/* enforce limit on size */
	BUG_ON(size % 64 != 0);

	/* enforce limit on number of failures */
	BUG_ON(np < 1);
	BUG_ON(np > RAID_PARITY_MAX);

	raid_gen_ptr[np - 1](nd, size, v);
}

/**
 * Inverts the square matrix M of size nxn into V.
 *
 * This is not a general matrix inversion because we assume the matrix M
 * to have all the square submatrix not singular.
 * We use Gauss elimination to invert.
 *
 * @M Matrix to invert with @n rows and @n columns.
 * @V Destination matrix where the result is put.
 * @n Number of rows and columns of the matrix.
 */
void raid_invert(uint8_t *M, uint8_t *V, int n)
{
	int i, j, k;

	/* set the identity matrix in V */
	for (i = 0; i < n; ++i)
		for (j = 0; j < n; ++j)
			V[i * n + j] = i == j;

	/* for each element in the diagonal */
	for (k = 0; k < n; ++k) {
		uint8_t f;

		/* the diagonal element cannot be 0 because */
		/* we are inverting matrices with all the square */
		/* submatrices not singular */
		BUG_ON(M[k * n + k] == 0);

		/* make the diagonal element to be 1 */
		f = inv(M[k * n + k]);
		for (j = 0; j < n; ++j) {
			M[k * n + j] = mul(f, M[k * n + j]);
			V[k * n + j] = mul(f, V[k * n + j]);
		}

		/* make all the elements over and under the diagonal */
		/* to be zero */
		for (i = 0; i < n; ++i) {
			if (i == k)
				continue;
			f = M[i * n + k];
			for (j = 0; j < n; ++j) {
				M[i * n + j] ^= mul(f, M[k * n + j]);
				V[i * n + j] ^= mul(f, V[k * n + j]);
			}
		}
	}
}

/**
 * Computes the parity without the missing data blocks
 * and store it in the buffers of such data blocks.
 *
 * This is the parity expressed as Pa,Qa,Ra,Sa,Ta,Ua in the equations.
 */
void raid_delta_gen(int nr, int *id, int *ip, int nd, size_t size, void **v)
{
	void *p[RAID_PARITY_MAX];
	void *pa[RAID_PARITY_MAX];
	int i, j;
	int np;
	void *latest;

	/* total number of parities we are going to process */
	/* they are both the used and the unused ones */
	np = ip[nr - 1] + 1;

	/* latest missing data block */
	latest = v[id[nr - 1]];

	/* setup pointers for delta computation */
	for (i = 0, j = 0; i < np; ++i) {
		/* keep a copy of the original parity vector */
		p[i] = v[nd + i];

		if (ip[j] == i) {
			/*
			 * Set used parities to point to the missing
			 * data blocks.
			 *
			 * The related data blocks are instead set
			 * to point to the "zero" buffer.
			 */

			/* the latest parity to use ends the for loop and */
			/* then it cannot happen to process more of them */
			BUG_ON(j >= nr);

			/* buffer for missing data blocks */
			pa[j] = v[id[j]];

			/* set at zero the missing data blocks */
			v[id[j]] = raid_zero_block;

			/* compute the parity over the missing data blocks */
			v[nd + i] = pa[j];

			/* check for the next used entry */
			++j;
		} else {
			/*
			 * Unused parities are going to be rewritten with
			 * not significative data, because we don't have
			 * functions able to compute only a subset of
			 * parities.
			 *
			 * To avoid this, we reuse parity buffers,
			 * assuming that all the parity functions write
			 * parities in order.
			 *
			 * We assign the unused parity block to the same
			 * block of the latest used parity that we know it
			 * will be written.
			 *
			 * This means that this block will be written
			 * multiple times and only the latest write will
			 * contain the correct data.
			 */
			v[nd + i] = latest;
		}
	}

	/* all the parities have to be processed */
	BUG_ON(j != nr);

	/* recompute the parity, note that np may be smaller than the */
	/* total number of parities available */
	raid_gen(nd, np, size, v);

	/* restore data buffers as before */
	for (j = 0; j < nr; ++j)
		v[id[j]] = pa[j];

	/* restore parity buffers as before */
	for (i = 0; i < np; ++i)
		v[nd + i] = p[i];
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
void raid_rec1of1(int *id, int nd, size_t size, void **v)
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
	raid_gen(nd, 1, size, v);

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
void raid_rec2of2_int8(int *id, int *ip, int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	size_t i;
	uint8_t *p;
	uint8_t *pa;
	uint8_t *q;
	uint8_t *qa;
	const uint8_t *T[2];

	/* get multiplication tables */
	T[0] = table(inv(pow2(id[1] - id[0]) ^ 1));
	T[1] = table(inv(pow2(id[0]) ^ pow2(id[1])));

	/* compute delta parity */
	raid_delta_gen(2, id, ip, nd, size, vv);

	p = v[nd];
	q = v[nd + 1];
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

/*
 * Forwarders for data recovery.
 *
 * These functions recover data blocks using the specified parity
 * to recompute the missing data.
 *
 * Note that the format of vectors @id/@ip is different than raid_rec().
 * For example, in the vector @ip the first parity is represented with the
 * value 0 and not @nd.
 *
 * @nr Number of failed data blocks to recover.
 * @id[] Vector of @nr indexes of the data blocks to recover.
 *   The indexes start from 0. They must be in order.
 * @ip[] Vector of @nr indexes of the parity blocks to use in the recovering.
 *   The indexes start from 0. They must be in order.
 * @nd Number of data blocks.
 * @np Number of parity blocks.
 * @size Size of the blocks pointed by @v. It must be a multiplier of 64.
 * @v Vector of pointers to the blocks of data and parity.
 *   It has (@nd + @np) elements. The starting elements are the blocks
 *   for data, following with the parity blocks.
 *   Each block has @size bytes.
 */
void (*raid_rec_ptr[RAID_PARITY_MAX])(
	int nr, int *id, int *ip, int nd, size_t size, void **vv);

void raid_rec(int nr, int *ir, int nd, int np, size_t size, void **v)
{
	int nrd; /* number of data blocks to recover */
	int nrp; /* number of parity blocks to recover */

	/* enforce limit on size */
	BUG_ON(size % 64 != 0);

	/* enforce limit on number of failures */
	BUG_ON(nr > np);
	BUG_ON(np > RAID_PARITY_MAX);

	/* enforce order in index vector */
	BUG_ON(nr >= 2 && ir[0] >= ir[1]);
	BUG_ON(nr >= 3 && ir[1] >= ir[2]);
	BUG_ON(nr >= 4 && ir[2] >= ir[3]);
	BUG_ON(nr >= 5 && ir[3] >= ir[4]);
	BUG_ON(nr >= 6 && ir[4] >= ir[5]);

	/* enforce limit on index vector */
	BUG_ON(nr > 0 && ir[nr-1] >= nd + np);

	/* count the number of data blocks to recover */
	nrd = 0;
	while (nrd < nr && ir[nrd] < nd)
		++nrd;

	/* all the remaining are parity */
	nrp = nr - nrd;

	/* enforce limit on number of failures */
	BUG_ON(nrd > nd);
	BUG_ON(nrp > np);

	/* if failed data is present */
	if (nrd != 0) {
		int ip[RAID_PARITY_MAX];
		int i, j, k;

		/* setup the vector of parities to use */
		for (i = 0, j = 0, k = 0; i < np; ++i) {
			if (j < nrp && ir[nrd + j] == nd + i) {
				/* this parity has to be recovered */
				++j;
			} else {
				/* this parity is used for recovering */
				ip[k] = i;
				++k;
			}
		}

		/* recover the nrd data blocks specified in ir[], */
		/* using the first nrd parity in ip[] for recovering */
		raid_rec_ptr[nrd - 1](nrd, ir, ip, nd, size, v);
	}

	/* recompute all the parities up to the last bad one */
	if (nrp != 0)
		raid_gen(nd, ir[nr - 1] - nd + 1, size, v);
}

void raid_data(int nr, int *id, int *ip, int nd, size_t size, void **v)
{
	/* enforce limit on size */
	BUG_ON(size % 64 != 0);

	/* enforce limit on number of failures */
	BUG_ON(nr > nd);
	BUG_ON(nr > RAID_PARITY_MAX);

	/* enforce order in index vector for data */
	BUG_ON(nr >= 2 && id[0] >= id[1]);
	BUG_ON(nr >= 3 && id[1] >= id[2]);
	BUG_ON(nr >= 4 && id[2] >= id[3]);
	BUG_ON(nr >= 5 && id[3] >= id[4]);
	BUG_ON(nr >= 6 && id[4] >= id[5]);

	/* enforce limit on index vector for data */
	BUG_ON(nr > 0 && id[nr-1] >= nd);

	/* enforce order in index vector for parity */
	BUG_ON(nr >= 2 && ip[0] >= ip[1]);
	BUG_ON(nr >= 3 && ip[1] >= ip[2]);
	BUG_ON(nr >= 4 && ip[2] >= ip[3]);
	BUG_ON(nr >= 5 && ip[3] >= ip[4]);
	BUG_ON(nr >= 6 && ip[4] >= ip[5]);

	/* if failed data is present */
	if (nr != 0)
		raid_rec_ptr[nr - 1](nr, id, ip, nd, size, v);
}

