/*
 * Copyright (C) 2011 Andrea Mazzoleni
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
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

#ifndef __CPU_H
#define __CPU_H

#if defined(__i386__) || defined(__x86_64__)

#define X86_FEATURE_MMX (0*32+23) /* Multimedia Extensions */
#define X86_FEATURE_FXSR (0*32+24) /* FXSAVE and FXRSTOR instructions (fast save and restore) */
#define X86_FEATURE_XMM (0*32+25) /* Streaming SIMD Extensions */
#define X86_FEATURE_XMM2 (0*32+26) /* Streaming SIMD Extensions-2 */
#define X86_FEATURE_MMXEXT (1*32+22) /* AMD MMX extensions */

static inline int cpu_has(uint32_t flag)
{
	uint32_t eax = (flag >> 5) ? 0x80000001 : 1;
	uint32_t edx;

	asm volatile(
#if defined(__i386__)
		/* allow compilation in PIC mode saving ebx */
		"push %%ebx\n"
		"cpuid\n"
		"pop %%ebx\n"
		: "+a" (eax), "=d" (edx)
		: : "ecx");
#else
		"cpuid\n"
		: "+a" (eax), "=d" (edx)
		: : "ecx", "ebx");
#endif

	return (edx >> (flag & 31)) & 1;
}

static inline int cpu_has_mmx(void)
{
	return cpu_has(X86_FEATURE_MMX);
}

static inline int cpu_has_sse2(void)
{
	return cpu_has(X86_FEATURE_MMX) &&
		cpu_has(X86_FEATURE_FXSR) &&
		cpu_has(X86_FEATURE_XMM) &&
		cpu_has(X86_FEATURE_XMM2);
}

#endif

#endif

