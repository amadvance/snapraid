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

#include "internal.h"
#include "gf.h"

#if defined(__i386__) || defined(__x86_64__)
/*
 * PAR1 (RAID5 with xor) SSE2 implementation
 *
 * Note that we don't have the corresponding x64 sse2ext function using more
 * registers because processing a block of 64 bytes already fills
 * the typical cache block, and processing 128 bytes doesn't increase
 * performance.
 */
void raid_par1_sse2(int nd, size_t size, void** vv)
{
	uint8_t** v = (uint8_t**)vv;
	uint8_t* p;
	int d, l;
	size_t i;

	l = nd - 1;
	p = v[nd];

	for(i=0;i<size;i+=64) {
		asm volatile("movdqa %0,%%xmm0" : : "m" (v[l][i]));
		asm volatile("movdqa %0,%%xmm1" : : "m" (v[l][i+16]));
		asm volatile("movdqa %0,%%xmm2" : : "m" (v[l][i+32]));
		asm volatile("movdqa %0,%%xmm3" : : "m" (v[l][i+48]));
		/* accessing disks in backward order because the buffers */
		/* are also in backward order */
		for(d=l-1;d>=0;--d) {
			asm volatile("movdqa %0,%%xmm4" : : "m" (v[d][i]));
			asm volatile("movdqa %0,%%xmm5" : : "m" (v[d][i+16]));
			asm volatile("movdqa %0,%%xmm6" : : "m" (v[d][i+32]));
			asm volatile("movdqa %0,%%xmm7" : : "m" (v[d][i+48]));
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

#if defined(__i386__) || defined(__x86_64__)
static const struct gfconst16 {
	uint8_t poly[16];
	uint8_t low4[16];
} gfconst16  __attribute__((aligned(64))) = {
	{ 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d },
	{ 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f },
};

/*
 * PAR2 (RAID6 with powers of 2) SSE2 implementation
 */
void raid_par2_sse2(int nd, size_t size, void** vv)
{
	uint8_t** v = (uint8_t**)vv;
	uint8_t* p;
	uint8_t* q;
	int d, l;
	size_t i;

	l = nd - 1;
	p = v[nd];
	q = v[nd+1];

	asm volatile("movdqa %0,%%xmm7" : : "m" (gfconst16.poly[0]));

	for(i=0;i<size;i+=32) {
		asm volatile("movdqa %0,%%xmm0" : : "m" (v[l][i]));
		asm volatile("movdqa %0,%%xmm1" : : "m" (v[l][i+16]));
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

			asm volatile("movdqa %0,%%xmm4" : : "m" (v[d][i]));
			asm volatile("movdqa %0,%%xmm5" : : "m" (v[d][i+16]));
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
void raid_par2_sse2ext(int nd, size_t size, void** vv)
{
	uint8_t** v = (uint8_t**)vv;
	uint8_t* p;
	uint8_t* q;
	int d, l;
	size_t i;

	l = nd - 1;
	p = v[nd];
	q = v[nd+1];

	asm volatile("movdqa %0,%%xmm15" : : "m" (gfconst16.poly[0]));

	for(i=0;i<size;i+=64) {
		asm volatile("movdqa %0,%%xmm0" : : "m" (v[l][i]));
		asm volatile("movdqa %0,%%xmm1" : : "m" (v[l][i+16]));
		asm volatile("movdqa %0,%%xmm2" : : "m" (v[l][i+32]));
		asm volatile("movdqa %0,%%xmm3" : : "m" (v[l][i+48]));
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

			asm volatile("movdqa %0,%%xmm8" : : "m" (v[d][i]));
			asm volatile("movdqa %0,%%xmm9" : : "m" (v[d][i+16]));
			asm volatile("movdqa %0,%%xmm10" : : "m" (v[d][i+32]));
			asm volatile("movdqa %0,%%xmm11" : : "m" (v[d][i+48]));
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

#if defined(__i386__) || defined(__x86_64__)
/*
 * PAR3 (triple parity with Cauchy matrix) SSSE3 implementation
 */
void raid_par3_ssse3(int nd, size_t size, void** vv)
{
	uint8_t** v = (uint8_t**)vv;
	uint8_t* p;
	uint8_t* q;
	uint8_t* r;
	int d, l;
	size_t i;

	l = nd - 1;
	p = v[nd];
	q = v[nd+1];
	r = v[nd+2];

	/* special case with only one data disk */
	if (l == 0) {
		for(i=0;i<3;++i)
			memcpy(v[1+i], v[0], size);
		return;
	}

	/* generic case with at least two data disks */
	asm volatile("movdqa %0,%%xmm3" : : "m" (gfconst16.poly[0]));
	asm volatile("movdqa %0,%%xmm7" : : "m" (gfconst16.low4[0]));

	for(i=0;i<size;i+=16) {
		/* last disk without the by two multiplication */
		asm volatile("movdqa %0,%%xmm4" : : "m" (v[l][i]));

		asm volatile("movdqa %xmm4,%xmm0");
		asm volatile("movdqa %xmm4,%xmm1");

		asm volatile("movdqa %xmm4,%xmm5");
		asm volatile("psrlw  $4,%xmm5");
		asm volatile("pand   %xmm7,%xmm4");
		asm volatile("pand   %xmm7,%xmm5");

		asm volatile("movdqa %0,%%xmm2" : : "m" (gfcauchypshufb[l][0][0][0]));
		asm volatile("movdqa %0,%%xmm6" : : "m" (gfcauchypshufb[l][0][1][0]));
		asm volatile("pshufb %xmm4,%xmm2");
		asm volatile("pshufb %xmm5,%xmm6");
		asm volatile("pxor   %xmm6,%xmm2");

		/* intermediate disks */
		for(d=l-1;d>0;--d) {
			asm volatile("movdqa %0,%%xmm4" : : "m" (v[d][i]));

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

			asm volatile("movdqa %0,%%xmm6" : : "m" (gfcauchypshufb[d][0][0][0]));
			asm volatile("pshufb %xmm4,%xmm6");
			asm volatile("pxor   %xmm6,%xmm2");
			asm volatile("movdqa %0,%%xmm6" : : "m" (gfcauchypshufb[d][0][1][0]));
			asm volatile("pshufb %xmm5,%xmm6");
			asm volatile("pxor   %xmm6,%xmm2");
		}

		/* first disk with all coefficients at 1 */
		asm volatile("movdqa %0,%%xmm4" : : "m" (v[0][i]));

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
void raid_par3_ssse3ext(int nd, size_t size, void** vv)
{
	uint8_t** v = (uint8_t**)vv;
	uint8_t* p;
	uint8_t* q;
	uint8_t* r;
	int d, l;
	size_t i;

	l = nd - 1;
	p = v[nd];
	q = v[nd+1];
	r = v[nd+2];

	/* special case with only one data disk */
	if (l == 0) {
		for(i=0;i<3;++i)
			memcpy(v[1+i], v[0], size);
		return;
	}

	/* generic case with at least two data disks */
	asm volatile("movdqa %0,%%xmm3" : : "m" (gfconst16.poly[0]));
	asm volatile("movdqa %0,%%xmm11" : : "m" (gfconst16.low4[0]));

	for(i=0;i<size;i+=32) {
		/* last disk without the by two multiplication */
		asm volatile("movdqa %0,%%xmm4" : : "m" (v[l][i]));
		asm volatile("movdqa %0,%%xmm12" : : "m" (v[l][i+16]));

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

		asm volatile("movdqa %0,%%xmm2" : : "m" (gfcauchypshufb[l][0][0][0]));
		asm volatile("movdqa %0,%%xmm7" : : "m" (gfcauchypshufb[l][0][1][0]));
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
			asm volatile("movdqa %0,%%xmm4" : : "m" (v[d][i]));
			asm volatile("movdqa %0,%%xmm12" : : "m" (v[d][i+16]));

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

			asm volatile("movdqa %0,%%xmm6" : : "m" (gfcauchypshufb[d][0][0][0]));
			asm volatile("movdqa %0,%%xmm7" : : "m" (gfcauchypshufb[d][0][1][0]));
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
		asm volatile("movdqa %0,%%xmm4" : : "m" (v[0][i]));
		asm volatile("movdqa %0,%%xmm12" : : "m" (v[0][i+16]));

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

#if defined(__i386__) || defined(__x86_64__)
/*
 * PAR4 (quad parity with Cauchy matrix) SSSE3 implementation
 */
void raid_par4_ssse3(int nd, size_t size, void** vv)
{
	uint8_t** v = (uint8_t**)vv;
	uint8_t* p;
	uint8_t* q;
	uint8_t* r;
	uint8_t* s;
	int d, l;
	size_t i;

	l = nd - 1;
	p = v[nd];
	q = v[nd+1];
	r = v[nd+2];
	s = v[nd+3];

	/* special case with only one data disk */
	if (l == 0) {
		for(i=0;i<4;++i)
			memcpy(v[1+i], v[0], size);
		return;
	}

	/* generic case with at least two data disks */
	for(i=0;i<size;i+=16) {
		/* last disk without the by two multiplication */
		asm volatile("movdqa %0,%%xmm7" : : "m" (gfconst16.low4[0]));
		asm volatile("movdqa %0,%%xmm4" : : "m" (v[l][i]));

		asm volatile("movdqa %xmm4,%xmm0");
		asm volatile("movdqa %xmm4,%xmm1");

		asm volatile("movdqa %xmm4,%xmm5");
		asm volatile("psrlw  $4,%xmm5");
		asm volatile("pand   %xmm7,%xmm4");
		asm volatile("pand   %xmm7,%xmm5");

		asm volatile("movdqa %0,%%xmm2" : : "m" (gfcauchypshufb[l][0][0][0]));
		asm volatile("movdqa %0,%%xmm7" : : "m" (gfcauchypshufb[l][0][1][0]));
		asm volatile("pshufb %xmm4,%xmm2");
		asm volatile("pshufb %xmm5,%xmm7");
		asm volatile("pxor   %xmm7,%xmm2");

		asm volatile("movdqa %0,%%xmm3" : : "m" (gfcauchypshufb[l][1][0][0]));
		asm volatile("movdqa %0,%%xmm7" : : "m" (gfcauchypshufb[l][1][1][0]));
		asm volatile("pshufb %xmm4,%xmm3");
		asm volatile("pshufb %xmm5,%xmm7");
		asm volatile("pxor   %xmm7,%xmm3");

		/* intermediate disks */
		for(d=l-1;d>0;--d) {
			asm volatile("movdqa %0,%%xmm7" : : "m" (gfconst16.poly[0]));
			asm volatile("movdqa %0,%%xmm4" : : "m" (v[d][i]));

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

			asm volatile("movdqa %0,%%xmm6" : : "m" (gfcauchypshufb[d][0][0][0]));
			asm volatile("movdqa %0,%%xmm7" : : "m" (gfcauchypshufb[d][0][1][0]));
			asm volatile("pshufb %xmm4,%xmm6");
			asm volatile("pshufb %xmm5,%xmm7");
			asm volatile("pxor   %xmm6,%xmm2");
			asm volatile("pxor   %xmm7,%xmm2");

			asm volatile("movdqa %0,%%xmm6" : : "m" (gfcauchypshufb[d][1][0][0]));
			asm volatile("movdqa %0,%%xmm7" : : "m" (gfcauchypshufb[d][1][1][0]));
			asm volatile("pshufb %xmm4,%xmm6");
			asm volatile("pshufb %xmm5,%xmm7");
			asm volatile("pxor   %xmm6,%xmm3");
			asm volatile("pxor   %xmm7,%xmm3");
		}

		/* first disk with all coefficients at 1 */
		asm volatile("movdqa %0,%%xmm7" : : "m" (gfconst16.poly[0]));
		asm volatile("movdqa %0,%%xmm4" : : "m" (v[0][i]));

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
void raid_par4_ssse3ext(int nd, size_t size, void** vv)
{
	uint8_t** v = (uint8_t**)vv;
	uint8_t* p;
	uint8_t* q;
	uint8_t* r;
	uint8_t* s;
	int d, l;
	size_t i;

	l = nd - 1;
	p = v[nd];
	q = v[nd+1];
	r = v[nd+2];
	s = v[nd+3];

	/* special case with only one data disk */
	if (l == 0) {
		for(i=0;i<4;++i)
			memcpy(v[1+i], v[0], size);
		return;
	}

	/* generic case with at least two data disks */
	for(i=0;i<size;i+=32) {
		/* last disk without the by two multiplication */
		asm volatile("movdqa %0,%%xmm15" : : "m" (gfconst16.low4[0]));
		asm volatile("movdqa %0,%%xmm4" : : "m" (v[l][i]));
		asm volatile("movdqa %0,%%xmm12" : : "m" (v[l][i+16]));

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

		asm volatile("movdqa %0,%%xmm2" : : "m" (gfcauchypshufb[l][0][0][0]));
		asm volatile("movdqa %0,%%xmm7" : : "m" (gfcauchypshufb[l][0][1][0]));
		asm volatile("movdqa %xmm2,%xmm10");
		asm volatile("movdqa %xmm7,%xmm15");
		asm volatile("pshufb %xmm4,%xmm2");
		asm volatile("pshufb %xmm12,%xmm10");
		asm volatile("pshufb %xmm5,%xmm7");
		asm volatile("pshufb %xmm13,%xmm15");
		asm volatile("pxor   %xmm7,%xmm2");
		asm volatile("pxor   %xmm15,%xmm10");

		asm volatile("movdqa %0,%%xmm3" : : "m" (gfcauchypshufb[l][1][0][0]));
		asm volatile("movdqa %0,%%xmm7" : : "m" (gfcauchypshufb[l][1][1][0]));
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
			asm volatile("movdqa %0,%%xmm4" : : "m" (v[d][i]));
			asm volatile("movdqa %0,%%xmm12" : : "m" (v[d][i+16]));

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

			asm volatile("movdqa %0,%%xmm6" : : "m" (gfcauchypshufb[d][0][0][0]));
			asm volatile("movdqa %0,%%xmm7" : : "m" (gfcauchypshufb[d][0][1][0]));
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

			asm volatile("movdqa %0,%%xmm6" : : "m" (gfcauchypshufb[d][1][0][0]));
			asm volatile("movdqa %0,%%xmm7" : : "m" (gfcauchypshufb[d][1][1][0]));
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
		asm volatile("movdqa %0,%%xmm4" : : "m" (v[0][i]));
		asm volatile("movdqa %0,%%xmm12" : : "m" (v[0][i+16]));

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

#if defined(__i386__) || defined(__x86_64__)
/*
 * PAR5 (penta parity with Cauchy matrix) SSSE3 implementation
 */
#ifdef _WIN32
/* ensures that stack is aligned at 16 bytes because we allocate SSE registers in it */
__attribute__((force_align_arg_pointer))
#endif
void raid_par5_ssse3(int nd, size_t size, void** vv)
/* ensures that stack is aligned at 16 bytes because we allocate SSE registers in it */
{
	uint8_t** v = (uint8_t**)vv;
	uint8_t* p;
	uint8_t* q;
	uint8_t* r;
	uint8_t* s;
	uint8_t* t;
	int d, l;
	size_t i;
	uint8_t p0[16] __attribute__((aligned(16)));

	l = nd - 1;
	p = v[nd];
	q = v[nd+1];
	r = v[nd+2];
	s = v[nd+3];
	t = v[nd+4];

	/* special case with only one data disk */
	if (l == 0) {
		for(i=0;i<5;++i)
			memcpy(v[1+i], v[0], size);
		return;
	}

	/* generic case with at least two data disks */
	for(i=0;i<size;i+=16) {
		/* last disk without the by two multiplication */
		asm volatile("movdqa %0,%%xmm4" : : "m" (v[l][i]));

		asm volatile("movdqa %xmm4,%xmm0");
		asm volatile("movdqa %%xmm4,%0" : "=m" (p0[0]));

		asm volatile("movdqa %0,%%xmm7" : : "m" (gfconst16.low4[0]));
		asm volatile("movdqa %xmm4,%xmm5");
		asm volatile("psrlw  $4,%xmm5");
		asm volatile("pand   %xmm7,%xmm4");
		asm volatile("pand   %xmm7,%xmm5");

		asm volatile("movdqa %0,%%xmm1" : : "m" (gfcauchypshufb[l][0][0][0]));
		asm volatile("movdqa %0,%%xmm7" : : "m" (gfcauchypshufb[l][0][1][0]));
		asm volatile("pshufb %xmm4,%xmm1");
		asm volatile("pshufb %xmm5,%xmm7");
		asm volatile("pxor   %xmm7,%xmm1");

		asm volatile("movdqa %0,%%xmm2" : : "m" (gfcauchypshufb[l][1][0][0]));
		asm volatile("movdqa %0,%%xmm7" : : "m" (gfcauchypshufb[l][1][1][0]));
		asm volatile("pshufb %xmm4,%xmm2");
		asm volatile("pshufb %xmm5,%xmm7");
		asm volatile("pxor   %xmm7,%xmm2");

		asm volatile("movdqa %0,%%xmm3" : : "m" (gfcauchypshufb[l][2][0][0]));
		asm volatile("movdqa %0,%%xmm7" : : "m" (gfcauchypshufb[l][2][1][0]));
		asm volatile("pshufb %xmm4,%xmm3");
		asm volatile("pshufb %xmm5,%xmm7");
		asm volatile("pxor   %xmm7,%xmm3");

		/* intermediate disks */
		for(d=l-1;d>0;--d) {
			asm volatile("movdqa %0,%%xmm4" : : "m" (v[d][i]));
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

			asm volatile("movdqa %0,%%xmm6" : : "m" (gfcauchypshufb[d][0][0][0]));
			asm volatile("movdqa %0,%%xmm7" : : "m" (gfcauchypshufb[d][0][1][0]));
			asm volatile("pshufb %xmm4,%xmm6");
			asm volatile("pshufb %xmm5,%xmm7");
			asm volatile("pxor   %xmm6,%xmm1");
			asm volatile("pxor   %xmm7,%xmm1");

			asm volatile("movdqa %0,%%xmm6" : : "m" (gfcauchypshufb[d][1][0][0]));
			asm volatile("movdqa %0,%%xmm7" : : "m" (gfcauchypshufb[d][1][1][0]));
			asm volatile("pshufb %xmm4,%xmm6");
			asm volatile("pshufb %xmm5,%xmm7");
			asm volatile("pxor   %xmm6,%xmm2");
			asm volatile("pxor   %xmm7,%xmm2");

			asm volatile("movdqa %0,%%xmm6" : : "m" (gfcauchypshufb[d][2][0][0]));
			asm volatile("movdqa %0,%%xmm7" : : "m" (gfcauchypshufb[d][2][1][0]));
			asm volatile("pshufb %xmm4,%xmm6");
			asm volatile("pshufb %xmm5,%xmm7");
			asm volatile("pxor   %xmm6,%xmm3");
			asm volatile("pxor   %xmm7,%xmm3");
		}

		/* first disk with all coefficients at 1 */
		asm volatile("movdqa %0,%%xmm4" : : "m" (v[0][i]));
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
void raid_par5_ssse3ext(int nd, size_t size, void** vv)
{
	uint8_t** v = (uint8_t**)vv;
	uint8_t* p;
	uint8_t* q;
	uint8_t* r;
	uint8_t* s;
	uint8_t* t;
	int d, l;
	size_t i;

	l = nd - 1;
	p = v[nd];
	q = v[nd+1];
	r = v[nd+2];
	s = v[nd+3];
	t = v[nd+4];

	/* special case with only one data disk */
	if (l == 0) {
		for(i=0;i<5;++i)
			memcpy(v[1+i], v[0], size);
		return;
	}

	/* generic case with at least two data disks */
	asm volatile("movdqa %0,%%xmm14" : : "m" (gfconst16.poly[0]));
	asm volatile("movdqa %0,%%xmm15" : : "m" (gfconst16.low4[0]));

	for(i=0;i<size;i+=16) {
		/* last disk without the by two multiplication */
		asm volatile("movdqa %0,%%xmm10" : : "m" (v[l][i]));

		asm volatile("movdqa %xmm10,%xmm0");
		asm volatile("movdqa %xmm10,%xmm1");

		asm volatile("movdqa %xmm10,%xmm11");
		asm volatile("psrlw  $4,%xmm11");
		asm volatile("pand   %xmm15,%xmm10");
		asm volatile("pand   %xmm15,%xmm11");

		asm volatile("movdqa %0,%%xmm2" : : "m" (gfcauchypshufb[l][0][0][0]));
		asm volatile("movdqa %0,%%xmm13" : : "m" (gfcauchypshufb[l][0][1][0]));
		asm volatile("pshufb %xmm10,%xmm2");
		asm volatile("pshufb %xmm11,%xmm13");
		asm volatile("pxor   %xmm13,%xmm2");

		asm volatile("movdqa %0,%%xmm3" : : "m" (gfcauchypshufb[l][1][0][0]));
		asm volatile("movdqa %0,%%xmm13" : : "m" (gfcauchypshufb[l][1][1][0]));
		asm volatile("pshufb %xmm10,%xmm3");
		asm volatile("pshufb %xmm11,%xmm13");
		asm volatile("pxor   %xmm13,%xmm3");

		asm volatile("movdqa %0,%%xmm4" : : "m" (gfcauchypshufb[l][2][0][0]));
		asm volatile("movdqa %0,%%xmm13" : : "m" (gfcauchypshufb[l][2][1][0]));
		asm volatile("pshufb %xmm10,%xmm4");
		asm volatile("pshufb %xmm11,%xmm13");
		asm volatile("pxor   %xmm13,%xmm4");

		/* intermediate disks */
		for(d=l-1;d>0;--d) {
			asm volatile("movdqa %0,%%xmm10" : : "m" (v[d][i]));

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

			asm volatile("movdqa %0,%%xmm12" : : "m" (gfcauchypshufb[d][0][0][0]));
			asm volatile("movdqa %0,%%xmm13" : : "m" (gfcauchypshufb[d][0][1][0]));
			asm volatile("pshufb %xmm10,%xmm12");
			asm volatile("pshufb %xmm11,%xmm13");
			asm volatile("pxor   %xmm12,%xmm2");
			asm volatile("pxor   %xmm13,%xmm2");

			asm volatile("movdqa %0,%%xmm12" : : "m" (gfcauchypshufb[d][1][0][0]));
			asm volatile("movdqa %0,%%xmm13" : : "m" (gfcauchypshufb[d][1][1][0]));
			asm volatile("pshufb %xmm10,%xmm12");
			asm volatile("pshufb %xmm11,%xmm13");
			asm volatile("pxor   %xmm12,%xmm3");
			asm volatile("pxor   %xmm13,%xmm3");

			asm volatile("movdqa %0,%%xmm12" : : "m" (gfcauchypshufb[d][2][0][0]));
			asm volatile("movdqa %0,%%xmm13" : : "m" (gfcauchypshufb[d][2][1][0]));
			asm volatile("pshufb %xmm10,%xmm12");
			asm volatile("pshufb %xmm11,%xmm13");
			asm volatile("pxor   %xmm12,%xmm4");
			asm volatile("pxor   %xmm13,%xmm4");
		}

		/* first disk with all coefficients at 1 */
		asm volatile("movdqa %0,%%xmm10" : : "m" (v[0][i]));

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

#if defined(__i386__) || defined(__x86_64__)
/*
 * PAR6 (hexa parity with Cauchy matrix) SSSE3 implementation
 */
#ifdef _WIN32
/* ensures that stack is aligned at 16 bytes because we allocate SSE registers in it */
__attribute__((force_align_arg_pointer))
#endif
void raid_par6_ssse3(int nd, size_t size, void** vv)
{
	uint8_t** v = (uint8_t**)vv;
	uint8_t* p;
	uint8_t* q;
	uint8_t* r;
	uint8_t* s;
	uint8_t* t;
	uint8_t* u;
	int d, l;
	size_t i;
	uint8_t p0[16] __attribute__((aligned(16)));
	uint8_t q0[16] __attribute__((aligned(16))); 

	l = nd - 1;
	p = v[nd];
	q = v[nd+1];
	r = v[nd+2];
	s = v[nd+3];
	t = v[nd+4];
	u = v[nd+5];

	/* special case with only one data disk */
	if (l == 0) {
		for(i=0;i<6;++i)
			memcpy(v[1+i], v[0], size);
		return;
	}

	/* generic case with at least two data disks */
	for(i=0;i<size;i+=16) {
		/* last disk without the by two multiplication */
		asm volatile("movdqa %0,%%xmm4" : : "m" (v[l][i]));

		asm volatile("movdqa %%xmm4,%0" : "=m" (p0[0]));
		asm volatile("movdqa %%xmm4,%0" : "=m" (q0[0]));

		asm volatile("movdqa %0,%%xmm7" : : "m" (gfconst16.low4[0]));
		asm volatile("movdqa %xmm4,%xmm5");
		asm volatile("psrlw  $4,%xmm5");
		asm volatile("pand   %xmm7,%xmm4");
		asm volatile("pand   %xmm7,%xmm5");

		asm volatile("movdqa %0,%%xmm0" : : "m" (gfcauchypshufb[l][0][0][0]));
		asm volatile("movdqa %0,%%xmm7" : : "m" (gfcauchypshufb[l][0][1][0]));
		asm volatile("pshufb %xmm4,%xmm0");
		asm volatile("pshufb %xmm5,%xmm7");
		asm volatile("pxor   %xmm7,%xmm0");

		asm volatile("movdqa %0,%%xmm1" : : "m" (gfcauchypshufb[l][1][0][0]));
		asm volatile("movdqa %0,%%xmm7" : : "m" (gfcauchypshufb[l][1][1][0]));
		asm volatile("pshufb %xmm4,%xmm1");
		asm volatile("pshufb %xmm5,%xmm7");
		asm volatile("pxor   %xmm7,%xmm1");

		asm volatile("movdqa %0,%%xmm2" : : "m" (gfcauchypshufb[l][2][0][0]));
		asm volatile("movdqa %0,%%xmm7" : : "m" (gfcauchypshufb[l][2][1][0]));
		asm volatile("pshufb %xmm4,%xmm2");
		asm volatile("pshufb %xmm5,%xmm7");
		asm volatile("pxor   %xmm7,%xmm2");

		asm volatile("movdqa %0,%%xmm3" : : "m" (gfcauchypshufb[l][3][0][0]));
		asm volatile("movdqa %0,%%xmm7" : : "m" (gfcauchypshufb[l][3][1][0]));
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

			asm volatile("movdqa %0,%%xmm4" : : "m" (v[d][i]));

			asm volatile("pxor %xmm4,%xmm5");
			asm volatile("pxor %xmm4,%xmm6");
			asm volatile("movdqa %%xmm5,%0" : "=m" (p0[0]));
			asm volatile("movdqa %%xmm6,%0" : "=m" (q0[0]));

			asm volatile("movdqa %0,%%xmm7" : : "m" (gfconst16.low4[0]));
			asm volatile("movdqa %xmm4,%xmm5");
			asm volatile("psrlw  $4,%xmm5");
			asm volatile("pand   %xmm7,%xmm4");
			asm volatile("pand   %xmm7,%xmm5");

			asm volatile("movdqa %0,%%xmm6" : : "m" (gfcauchypshufb[d][0][0][0]));
			asm volatile("movdqa %0,%%xmm7" : : "m" (gfcauchypshufb[d][0][1][0]));
			asm volatile("pshufb %xmm4,%xmm6");
			asm volatile("pshufb %xmm5,%xmm7");
			asm volatile("pxor   %xmm6,%xmm0");
			asm volatile("pxor   %xmm7,%xmm0");

			asm volatile("movdqa %0,%%xmm6" : : "m" (gfcauchypshufb[d][1][0][0]));
			asm volatile("movdqa %0,%%xmm7" : : "m" (gfcauchypshufb[d][1][1][0]));
			asm volatile("pshufb %xmm4,%xmm6");
			asm volatile("pshufb %xmm5,%xmm7");
			asm volatile("pxor   %xmm6,%xmm1");
			asm volatile("pxor   %xmm7,%xmm1");

			asm volatile("movdqa %0,%%xmm6" : : "m" (gfcauchypshufb[d][2][0][0]));
			asm volatile("movdqa %0,%%xmm7" : : "m" (gfcauchypshufb[d][2][1][0]));
			asm volatile("pshufb %xmm4,%xmm6");
			asm volatile("pshufb %xmm5,%xmm7");
			asm volatile("pxor   %xmm6,%xmm2");
			asm volatile("pxor   %xmm7,%xmm2");

			asm volatile("movdqa %0,%%xmm6" : : "m" (gfcauchypshufb[d][3][0][0]));
			asm volatile("movdqa %0,%%xmm7" : : "m" (gfcauchypshufb[d][3][1][0]));
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

		asm volatile("movdqa %0,%%xmm4" : : "m" (v[0][i]));
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
void raid_par6_ssse3ext(int nd, size_t size, void** vv)
{
	uint8_t** v = (uint8_t**)vv;
	uint8_t* p;
	uint8_t* q;
	uint8_t* r;
	uint8_t* s;
	uint8_t* t;
	uint8_t* u;
	int d, l;
	size_t i;

	l = nd - 1;
	p = v[nd];
	q = v[nd+1];
	r = v[nd+2];
	s = v[nd+3];
	t = v[nd+4];
	u = v[nd+5];

	/* special case with only one data disk */
	if (l == 0) {
		for(i=0;i<6;++i)
			memcpy(v[1+i], v[0], size);
		return;
	}

	/* generic case with at least two data disks */
	asm volatile("movdqa %0,%%xmm14" : : "m" (gfconst16.poly[0]));
	asm volatile("movdqa %0,%%xmm15" : : "m" (gfconst16.low4[0]));

	for(i=0;i<size;i+=16) {
		/* last disk without the by two multiplication */
		asm volatile("movdqa %0,%%xmm10" : : "m" (v[l][i]));

		asm volatile("movdqa %xmm10,%xmm0");
		asm volatile("movdqa %xmm10,%xmm1");

		asm volatile("movdqa %xmm10,%xmm11");
		asm volatile("psrlw  $4,%xmm11");
		asm volatile("pand   %xmm15,%xmm10");
		asm volatile("pand   %xmm15,%xmm11");

		asm volatile("movdqa %0,%%xmm2" : : "m" (gfcauchypshufb[l][0][0][0]));
		asm volatile("movdqa %0,%%xmm13" : : "m" (gfcauchypshufb[l][0][1][0]));
		asm volatile("pshufb %xmm10,%xmm2");
		asm volatile("pshufb %xmm11,%xmm13");
		asm volatile("pxor   %xmm13,%xmm2");

		asm volatile("movdqa %0,%%xmm3" : : "m" (gfcauchypshufb[l][1][0][0]));
		asm volatile("movdqa %0,%%xmm13" : : "m" (gfcauchypshufb[l][1][1][0]));
		asm volatile("pshufb %xmm10,%xmm3");
		asm volatile("pshufb %xmm11,%xmm13");
		asm volatile("pxor   %xmm13,%xmm3");

		asm volatile("movdqa %0,%%xmm4" : : "m" (gfcauchypshufb[l][2][0][0]));
		asm volatile("movdqa %0,%%xmm13" : : "m" (gfcauchypshufb[l][2][1][0]));
		asm volatile("pshufb %xmm10,%xmm4");
		asm volatile("pshufb %xmm11,%xmm13");
		asm volatile("pxor   %xmm13,%xmm4");

		asm volatile("movdqa %0,%%xmm5" : : "m" (gfcauchypshufb[l][3][0][0]));
		asm volatile("movdqa %0,%%xmm13" : : "m" (gfcauchypshufb[l][3][1][0]));
		asm volatile("pshufb %xmm10,%xmm5");
		asm volatile("pshufb %xmm11,%xmm13");
		asm volatile("pxor   %xmm13,%xmm5");

		/* intermediate disks */
		for(d=l-1;d>0;--d) {
			asm volatile("movdqa %0,%%xmm10" : : "m" (v[d][i]));

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

			asm volatile("movdqa %0,%%xmm12" : : "m" (gfcauchypshufb[d][0][0][0]));
			asm volatile("movdqa %0,%%xmm13" : : "m" (gfcauchypshufb[d][0][1][0]));
			asm volatile("pshufb %xmm10,%xmm12");
			asm volatile("pshufb %xmm11,%xmm13");
			asm volatile("pxor   %xmm12,%xmm2");
			asm volatile("pxor   %xmm13,%xmm2");

			asm volatile("movdqa %0,%%xmm12" : : "m" (gfcauchypshufb[d][1][0][0]));
			asm volatile("movdqa %0,%%xmm13" : : "m" (gfcauchypshufb[d][1][1][0]));
			asm volatile("pshufb %xmm10,%xmm12");
			asm volatile("pshufb %xmm11,%xmm13");
			asm volatile("pxor   %xmm12,%xmm3");
			asm volatile("pxor   %xmm13,%xmm3");

			asm volatile("movdqa %0,%%xmm12" : : "m" (gfcauchypshufb[d][2][0][0]));
			asm volatile("movdqa %0,%%xmm13" : : "m" (gfcauchypshufb[d][2][1][0]));
			asm volatile("pshufb %xmm10,%xmm12");
			asm volatile("pshufb %xmm11,%xmm13");
			asm volatile("pxor   %xmm12,%xmm4");
			asm volatile("pxor   %xmm13,%xmm4");

			asm volatile("movdqa %0,%%xmm12" : : "m" (gfcauchypshufb[d][3][0][0]));
			asm volatile("movdqa %0,%%xmm13" : : "m" (gfcauchypshufb[d][3][1][0]));
			asm volatile("pshufb %xmm10,%xmm12");
			asm volatile("pshufb %xmm11,%xmm13");
			asm volatile("pxor   %xmm12,%xmm5");
			asm volatile("pxor   %xmm13,%xmm5");
		}

		/* first disk with all coefficients at 1 */
		asm volatile("movdqa %0,%%xmm10" : : "m" (v[0][i]));

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

#if defined(__i386__) || defined(__x86_64__)
/*
 * RAID recovering for one disk SSSE3 implementation
 */
void raid_rec1_ssse3(int nr, const int* id, const int* ip, int nd, size_t size, void** vv)
{
	uint8_t** v = (uint8_t**)vv;
	uint8_t* p;
	uint8_t* pa;
	uint8_t G;
	uint8_t V;
	size_t i;

	(void)nr; /* unused, it's always 1 */

	/* if it's RAID5 uses the dedicated and faster function */
	if (ip[0] == 0) {
		raid_rec1_par1(id, nd, size, vv);
		return;
	}

	/* setup the coefficients matrix */
	G = A(ip[0],id[0]);

	/* invert it to solve the system of linear equations */
	V = inv(G);

	/* compute delta parity */
	raid_delta_gen(1, id, ip, nd, size, vv);

	p = v[nd+ip[0]];
	pa = v[id[0]];

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
void raid_rec2_ssse3(int nr, const int* id, const int* ip, int nd, size_t size, void** vv)
{
	uint8_t** v = (uint8_t**)vv;
	const int N = 2;
	uint8_t* p[N];
	uint8_t* pa[N];
	uint8_t G[N*N];
	uint8_t V[N*N];
	size_t i;
	int j, k;

	(void)nr; /* unused, it's always 2 */

	/* setup the coefficients matrix */
	for(j=0;j<N;++j) {
		for(k=0;k<N;++k) {
			G[j*N+k] = A(ip[j],id[k]);
		}
	}

	/* invert it to solve the system of linear equations */
	raid_invert(G, V, N);

	/* compute delta parity */
	raid_delta_gen(N, id, ip, nd, size, vv);

	for(j=0;j<N;++j) {
		p[j] = v[nd+ip[j]];
		pa[j] = v[id[j]];
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
void raid_recX_ssse3(int nr, const int* id, const int* ip, int nd, size_t size, void** vv)
{
	uint8_t** v = (uint8_t**)vv;
	int N = nr;
	uint8_t* p[RAID_PARITY_MAX];
	uint8_t* pa[RAID_PARITY_MAX];
	uint8_t G[RAID_PARITY_MAX*RAID_PARITY_MAX];
	uint8_t V[RAID_PARITY_MAX*RAID_PARITY_MAX];
	size_t i;
	int j, k;

	/* setup the coefficients matrix */
	for(j=0;j<N;++j) {
		for(k=0;k<N;++k) {
			G[j*N+k] = A(ip[j],id[k]);
		}
	}

	/* invert it to solve the system of linear equations */
	raid_invert(G, V, N);

	/* compute delta parity */
	raid_delta_gen(N, id, ip, nd, size, vv);

	for(j=0;j<N;++j) {
		p[j] = v[nd+ip[j]];
		pa[j] = v[id[j]];
	}

	asm volatile("movdqa %0,%%xmm7" : : "m" (gfconst16.low4[0]));

	for(i=0;i<size;i+=16) {
		uint8_t PD[RAID_PARITY_MAX][16] __attribute__((aligned(16)));

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
				uint8_t m = V[j*N+k];

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

