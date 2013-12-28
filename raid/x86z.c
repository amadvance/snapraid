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

#if defined(__i386__) || defined(__x86_64__)
static const struct gfzconst16 {
	uint8_t poly[16];
	uint8_t half[16];
	uint8_t low7[16];
} gfzconst16  __aligned(64) = {
	{ 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d, 0x1d },
	{ 0x8e, 0x8e, 0x8e, 0x8e, 0x8e, 0x8e, 0x8e, 0x8e, 0x8e, 0x8e, 0x8e, 0x8e, 0x8e, 0x8e, 0x8e, 0x8e },
	{ 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f }
};

/*
 * PARz (triple parity with powers of 2^-1) SSE2 implementation
 */
void raid_parz_sse2(int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *q;
	uint8_t *r;
	int d, l;
	size_t i;

	l = nd - 1;
	p = v[nd];
	q = v[nd+1];
	r = v[nd+2];

	asm_begin();

	asm volatile("movdqa %0,%%xmm7" : : "m" (gfzconst16.poly[0]));
	asm volatile("movdqa %0,%%xmm3" : : "m" (gfzconst16.half[0]));
	asm volatile("movdqa %0,%%xmm6" : : "m" (gfzconst16.low7[0]));

	for (i = 0; i < size; i += 16) {
		asm volatile("movdqa %0,%%xmm0" : : "m" (v[l][i]));
		asm volatile("movdqa %xmm0,%xmm1");
		asm volatile("movdqa %xmm0,%xmm2");
		for (d = l-1; d >= 0; --d) {
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

			asm volatile("movdqa %0,%%xmm4" : : "m" (v[d][i]));
			asm volatile("pxor %xmm4,%xmm0");
			asm volatile("pxor %xmm4,%xmm1");
			asm volatile("pxor %xmm4,%xmm2");
		}
		asm volatile("movntdq %%xmm0,%0" : "=m" (p[i]));
		asm volatile("movntdq %%xmm1,%0" : "=m" (q[i]));
		asm volatile("movntdq %%xmm2,%0" : "=m" (r[i]));
	}

	asm_end();
}
#endif

#if defined(__x86_64__)
/*
 * PARz (triple parity with powers of 2^-1) SSE2 implementation
 *
 * Note that it uses 16 registers, meaning that x64 is required.
 */
void raid_parz_sse2ext(int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	uint8_t *p;
	uint8_t *q;
	uint8_t *r;
	int d, l;
	size_t i;

	l = nd - 1;
	p = v[nd];
	q = v[nd+1];
	r = v[nd+2];

	asm_begin();

	asm volatile("movdqa %0,%%xmm7" : : "m" (gfzconst16.poly[0]));
	asm volatile("movdqa %0,%%xmm3" : : "m" (gfzconst16.half[0]));
	asm volatile("movdqa %0,%%xmm11" : : "m" (gfzconst16.low7[0]));

	for (i = 0; i < size; i += 32) {
		asm volatile("movdqa %0,%%xmm0" : : "m" (v[l][i]));
		asm volatile("movdqa %0,%%xmm8" : : "m" (v[l][i+16]));
		asm volatile("movdqa %xmm0,%xmm1");
		asm volatile("movdqa %xmm8,%xmm9");
		asm volatile("movdqa %xmm0,%xmm2");
		asm volatile("movdqa %xmm8,%xmm10");
		for (d = l-1; d >= 0; --d) {
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

			asm volatile("movdqa %0,%%xmm4" : : "m" (v[d][i]));
			asm volatile("movdqa %0,%%xmm12" : : "m" (v[d][i+16]));
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

	asm_end();
}
#endif

