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

#if defined(CONFIG_X86) && defined(CONFIG_SSE2)
static const struct gfzconst16 {
	uint8_t poly[16];
	uint8_t half[16];
	uint8_t low7[16];
} gfzconst16 __aligned(64) =
{
	{
		0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d,
		0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d
	},
	{
		0x8e, 0x8e, 0x8e, 0x8e, 0x8e, 0x8e, 0x8e, 0x8e,
		0x8e, 0x8e, 0x8e, 0x8e, 0x8e, 0x8e, 0x8e, 0x8e
	},
	{
		0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
		0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f
	}
};
#endif

#if defined(CONFIG_X86) && defined(CONFIG_SSE2)
/*
 * GENz (triple parity with powers of 2^-1) SSE2 implementation
 */
void raid_genz_sse2(int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t**)vv;
	uint8_t *p;
	uint8_t *q;
	uint8_t *r;
	int d, l;
	size_t i;

	l = nd - 1;
	p = v[nd];
	q = v[nd + 1];
	r = v[nd + 2];

	raid_sse_begin();

	asm volatile ("movdqa %0,%%xmm7" : : "m" (gfzconst16.poly[0]));
	asm volatile ("movdqa %0,%%xmm3" : : "m" (gfzconst16.half[0]));
	asm volatile ("movdqa %0,%%xmm6" : : "m" (gfzconst16.low7[0]));

	for (i = 0; i < size; i += 16) {
		asm volatile ("movdqa %0,%%xmm0" : : "m" (v[l][i]));
		asm volatile ("movdqa %xmm0,%xmm1");
		asm volatile ("movdqa %xmm0,%xmm2");
		for (d = l - 1; d >= 0; --d) {
			asm volatile ("pxor %xmm4,%xmm4");
			asm volatile ("pcmpgtb %xmm1,%xmm4");
			asm volatile ("paddb %xmm1,%xmm1");
			asm volatile ("pand %xmm7,%xmm4");
			asm volatile ("pxor %xmm4,%xmm1");

			asm volatile ("movdqa %xmm2,%xmm4");
			asm volatile ("pxor %xmm5,%xmm5");
			asm volatile ("psllw $7,%xmm4");
			asm volatile ("psrlw $1,%xmm2");
			asm volatile ("pcmpgtb %xmm4,%xmm5");
			asm volatile ("pand %xmm6,%xmm2");
			asm volatile ("pand %xmm3,%xmm5");
			asm volatile ("pxor %xmm5,%xmm2");

			asm volatile ("movdqa %0,%%xmm4" : : "m" (v[d][i]));
			asm volatile ("pxor %xmm4,%xmm0");
			asm volatile ("pxor %xmm4,%xmm1");
			asm volatile ("pxor %xmm4,%xmm2");
		}
		asm volatile ("movntdq %%xmm0,%0" : "=m" (p[i]));
		asm volatile ("movntdq %%xmm1,%0" : "=m" (q[i]));
		asm volatile ("movntdq %%xmm2,%0" : "=m" (r[i]));
	}

	raid_sse_end();
}
#endif

#if defined(CONFIG_X86_64) && defined(CONFIG_SSE2)
/*
 * GENz (triple parity with powers of 2^-1) SSE2 implementation
 *
 * Note that it uses 16 registers, meaning that x64 is required.
 */
void raid_genz_sse2ext(int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t**)vv;
	uint8_t *p;
	uint8_t *q;
	uint8_t *r;
	int d, l;
	size_t i;

	l = nd - 1;
	p = v[nd];
	q = v[nd + 1];
	r = v[nd + 2];

	raid_sse_begin();

	asm volatile ("movdqa %0,%%xmm7" : : "m" (gfzconst16.poly[0]));
	asm volatile ("movdqa %0,%%xmm3" : : "m" (gfzconst16.half[0]));
	asm volatile ("movdqa %0,%%xmm11" : : "m" (gfzconst16.low7[0]));

	for (i = 0; i < size; i += 32) {
		asm volatile ("movdqa %0,%%xmm0" : : "m" (v[l][i]));
		asm volatile ("movdqa %0,%%xmm8" : : "m" (v[l][i + 16]));
		asm volatile ("movdqa %xmm0,%xmm1");
		asm volatile ("movdqa %xmm8,%xmm9");
		asm volatile ("movdqa %xmm0,%xmm2");
		asm volatile ("movdqa %xmm8,%xmm10");
		for (d = l - 1; d >= 0; --d) {
			asm volatile ("movdqa %xmm2,%xmm6");
			asm volatile ("movdqa %xmm10,%xmm14");
			asm volatile ("pxor %xmm4,%xmm4");
			asm volatile ("pxor %xmm12,%xmm12");
			asm volatile ("pxor %xmm5,%xmm5");
			asm volatile ("pxor %xmm13,%xmm13");
			asm volatile ("psllw $7,%xmm6");
			asm volatile ("psllw $7,%xmm14");
			asm volatile ("psrlw $1,%xmm2");
			asm volatile ("psrlw $1,%xmm10");
			asm volatile ("pcmpgtb %xmm1,%xmm4");
			asm volatile ("pcmpgtb %xmm9,%xmm12");
			asm volatile ("pcmpgtb %xmm6,%xmm5");
			asm volatile ("pcmpgtb %xmm14,%xmm13");
			asm volatile ("paddb %xmm1,%xmm1");
			asm volatile ("paddb %xmm9,%xmm9");
			asm volatile ("pand %xmm11,%xmm2");
			asm volatile ("pand %xmm11,%xmm10");
			asm volatile ("pand %xmm7,%xmm4");
			asm volatile ("pand %xmm7,%xmm12");
			asm volatile ("pand %xmm3,%xmm5");
			asm volatile ("pand %xmm3,%xmm13");
			asm volatile ("pxor %xmm4,%xmm1");
			asm volatile ("pxor %xmm12,%xmm9");
			asm volatile ("pxor %xmm5,%xmm2");
			asm volatile ("pxor %xmm13,%xmm10");

			asm volatile ("movdqa %0,%%xmm4" : : "m" (v[d][i]));
			asm volatile ("movdqa %0,%%xmm12" : : "m" (v[d][i + 16]));
			asm volatile ("pxor %xmm4,%xmm0");
			asm volatile ("pxor %xmm4,%xmm1");
			asm volatile ("pxor %xmm4,%xmm2");
			asm volatile ("pxor %xmm12,%xmm8");
			asm volatile ("pxor %xmm12,%xmm9");
			asm volatile ("pxor %xmm12,%xmm10");
		}
		asm volatile ("movntdq %%xmm0,%0" : "=m" (p[i]));
		asm volatile ("movntdq %%xmm8,%0" : "=m" (p[i + 16]));
		asm volatile ("movntdq %%xmm1,%0" : "=m" (q[i]));
		asm volatile ("movntdq %%xmm9,%0" : "=m" (q[i + 16]));
		asm volatile ("movntdq %%xmm2,%0" : "=m" (r[i]));
		asm volatile ("movntdq %%xmm10,%0" : "=m" (r[i + 16]));
	}

	raid_sse_end();
}
#endif

#if defined(CONFIG_X86_64) && defined(CONFIG_AVX2)
/*
 * GENz (triple parity with powers of 2^-1) AVX2 implementation
 *
 * Note that it uses 16 registers, meaning that x64 is required.
 */
void raid_genz_avx2ext(int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t**)vv;
	uint8_t *p;
	uint8_t *q;
	uint8_t *r;
	int d, l;
	size_t i;

	l = nd - 1;
	p = v[nd];
	q = v[nd + 1];
	r = v[nd + 2];

	raid_avx_begin();

	asm volatile ("vbroadcasti128 %0,%%ymm7" : : "m" (gfzconst16.poly[0]));
	asm volatile ("vbroadcasti128 %0,%%ymm3" : : "m" (gfzconst16.half[0]));
	asm volatile ("vbroadcasti128 %0,%%ymm11" : : "m" (gfzconst16.low7[0]));
	asm volatile ("vpxor %ymm15,%ymm15,%ymm15");

	for (i = 0; i < size; i += 64) {
		asm volatile ("vmovdqa %0,%%ymm0" : : "m" (v[l][i]));
		asm volatile ("vmovdqa %0,%%ymm8" : : "m" (v[l][i + 32]));
		asm volatile ("vmovdqa %ymm0,%ymm1");
		asm volatile ("vmovdqa %ymm8,%ymm9");
		asm volatile ("vmovdqa %ymm0,%ymm2");
		asm volatile ("vmovdqa %ymm8,%ymm10");
		for (d = l - 1; d >= 0; --d) {
			asm volatile ("vpsllw $7,%ymm2,%ymm6");
			asm volatile ("vpsllw $7,%ymm10,%ymm14");
			asm volatile ("vpsrlw $1,%ymm2,%ymm2");
			asm volatile ("vpsrlw $1,%ymm10,%ymm10");
			asm volatile ("vpcmpgtb %ymm1,%ymm15,%ymm4");
			asm volatile ("vpcmpgtb %ymm9,%ymm15,%ymm12");
			asm volatile ("vpcmpgtb %ymm6,%ymm15,%ymm5");
			asm volatile ("vpcmpgtb %ymm14,%ymm15,%ymm13");
			asm volatile ("vpaddb %ymm1,%ymm1,%ymm1");
			asm volatile ("vpaddb %ymm9,%ymm9,%ymm9");
			asm volatile ("vpand %ymm11,%ymm2,%ymm2");
			asm volatile ("vpand %ymm11,%ymm10,%ymm10");
			asm volatile ("vpand %ymm7,%ymm4,%ymm4");
			asm volatile ("vpand %ymm7,%ymm12,%ymm12");
			asm volatile ("vpand %ymm3,%ymm5,%ymm5");
			asm volatile ("vpand %ymm3,%ymm13,%ymm13");
			asm volatile ("vpxor %ymm4,%ymm1,%ymm1");
			asm volatile ("vpxor %ymm12,%ymm9,%ymm9");
			asm volatile ("vpxor %ymm5,%ymm2,%ymm2");
			asm volatile ("vpxor %ymm13,%ymm10,%ymm10");

			asm volatile ("vmovdqa %0,%%ymm4" : : "m" (v[d][i]));
			asm volatile ("vmovdqa %0,%%ymm12" : : "m" (v[d][i + 32]));
			asm volatile ("vpxor %ymm4,%ymm0,%ymm0");
			asm volatile ("vpxor %ymm4,%ymm1,%ymm1");
			asm volatile ("vpxor %ymm4,%ymm2,%ymm2");
			asm volatile ("vpxor %ymm12,%ymm8,%ymm8");
			asm volatile ("vpxor %ymm12,%ymm9,%ymm9");
			asm volatile ("vpxor %ymm12,%ymm10,%ymm10");
		}
		asm volatile ("vmovntdq %%ymm0,%0" : "=m" (p[i]));
		asm volatile ("vmovntdq %%ymm8,%0" : "=m" (p[i + 32]));
		asm volatile ("vmovntdq %%ymm1,%0" : "=m" (q[i]));
		asm volatile ("vmovntdq %%ymm9,%0" : "=m" (q[i + 32]));
		asm volatile ("vmovntdq %%ymm2,%0" : "=m" (r[i]));
		asm volatile ("vmovntdq %%ymm10,%0" : "=m" (r[i + 32]));
	}

	raid_avx_end();
}
#endif

