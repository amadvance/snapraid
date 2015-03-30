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

#ifndef __RAID_CPU_H
#define __RAID_CPU_H

#ifdef CONFIG_X86

static inline void raid_cpuid(uint32_t func_eax, uint32_t sub_ecx, uint32_t *reg)
{
	asm volatile (
#if defined(__i386__) && defined(__PIC__)
	        /* allow compilation in PIC mode saving ebx */
		"xchgl %%ebx, %1\n"
		"cpuid\n"
		"xchgl %%ebx, %1\n"
		: "=a" (reg[0]), "=r" (reg[1]), "=c" (reg[2]), "=d" (reg[3])
		: "0" (func_eax), "2" (sub_ecx)
#else
		"cpuid\n"
		: "=a" (reg[0]), "=b" (reg[1]), "=c" (reg[2]), "=d" (reg[3])
		: "0" (func_eax), "2" (sub_ecx)
#endif
	);
}

static inline void raid_xgetbv(uint32_t* reg)
{
	/* get the value of the Extended Control Register ecx=0 */
	asm volatile (
	        /* uses a direct encoding of the XGETBV instruction as only recent */
	        /* assemblers support it. */
	        /* the next line is equivalent at: "xgetbv\n" */
		".byte 0x0f, 0x01, 0xd0\n"
		: "=a" (reg[0]), "=d" (reg[3])
		: "c" (0)
	);
}

#define CPU_VENDOR_MAX 13

static inline void raid_cpu_info(char *vendor, unsigned *family, unsigned *model)
{
	uint32_t reg[4];
	unsigned f, ef, m, em;

	raid_cpuid(0, 0, reg);

	((uint32_t*)vendor)[0] = reg[1];
	((uint32_t*)vendor)[1] = reg[3];
	((uint32_t*)vendor)[2] = reg[2];
	vendor[12] = 0;

	raid_cpuid(1, 0, reg);

	f = (reg[0] >> 8) & 0xF;
	ef = (reg[0] >> 20) & 0xFF;
	m = (reg[0] >> 4) & 0xF;
	em = (reg[0] >> 16) & 0xF;

	if (strcmp(vendor, "AuthenticAMD") == 0) {
		if (f < 15) {
			*family = f;
			*model = m;
		} else {
			*family = f + ef;
			*model = m + (em << 4);
		}
	} else {
		*family = f + ef;
		*model = m + (em << 4);
	}
}

static inline int raid_cpu_match_sse(uint32_t cpuid_1_ecx, uint32_t cpuid_1_edx)
{
	uint32_t reg[4];

	raid_cpuid(1, 0, reg);
	if ((reg[2] & cpuid_1_ecx) != cpuid_1_ecx)
		return 0;
	if ((reg[3] & cpuid_1_edx) != cpuid_1_edx)
		return 0;

	return 1;
}

static inline int raid_cpu_match_avx(uint32_t cpuid_1_ecx, uint32_t cpuid_7_ebx, uint32_t xcr0)
{
	uint32_t reg[4];

	raid_cpuid(1, 0, reg);
	if ((reg[2] & cpuid_1_ecx) != cpuid_1_ecx)
		return 0;

	raid_xgetbv(reg);
	if ((reg[0] & xcr0) != xcr0)
		return 0;

	raid_cpuid(7, 0, reg);
	if ((reg[1] & cpuid_7_ebx) != cpuid_7_ebx)
		return 0;

	return 1;
}

static inline int raid_cpu_has_sse2(void)
{
	/*
	 * Intel® 64 and IA-32 Architectures Software Developer's Manual
	 * 325462-048US September 2013
	 *
	 * 11.6.2 Checking for SSE/SSE2 Support
	 * Before an application attempts to use the SSE and/or SSE2 extensions, it should check
	 * that they are present on the processor:
	 * 1. Check that the processor supports the CPUID instruction. Bit 21 of the EFLAGS
	 * register can be used to check processor's support the CPUID instruction.
	 * 2. Check that the processor supports the SSE and/or SSE2 extensions (true if
	 * CPUID.01H:EDX.SSE[bit 25] = 1 and/or CPUID.01H:EDX.SSE2[bit 26] = 1).
	 */
	return raid_cpu_match_sse(
		0,
		1 << 26); /* SSE2 */
}

static inline int raid_cpu_has_ssse3(void)
{
	/*
	 * Intel® 64 and IA-32 Architectures Software Developer's Manual
	 * 325462-048US September 2013
	 *
	 * 12.7.2 Checking for SSSE3 Support
	 * Before an application attempts to use the SSSE3 extensions, the application should
	 * follow the steps illustrated in Section 11.6.2, "Checking for SSE/SSE2 Support."
	 * Next, use the additional step provided below:
	 * Check that the processor supports SSSE3 (if CPUID.01H:ECX.SSSE3[bit 9] = 1).
	 */
	return raid_cpu_match_sse(
		1 << 9, /* SSSE3 */
		1 << 26); /* SSE2 */
}

static inline int raid_cpu_has_crc32(void)
{
	/*
	 * Intel® 64 and IA-32 Architectures Software Developer's Manual
	 * 325462-048US September 2013
	 *
	 * 12.12.3 Checking for SSE4.2 Support
	 * ...
	 * Before an application attempts to use the CRC32 instruction, it must check
	 * that the processor supports SSE4.2 (if CPUID.01H:ECX.SSE4_2[bit 20] = 1).
	 */
	return raid_cpu_match_sse(
		1 << 20, /* CRC32 */
		0);
}

static inline int raid_cpu_has_avx2(void)
{
	/*
	 * Intel Architecture Instruction Set Extensions Programming Reference
	 * 319433-022 October 2014
	 *
	 * 14.3 Detection of AVX instructions
	 * 1) Detect CPUID.1:ECX.OSXSAVE[bit 27] = 1 (XGETBV enabled for application use1)
	 * 2) Issue XGETBV and verify that XCR0[2:1] = `11b' (XMM state and YMM state are enabled by OS).
	 * 3) detect CPUID.1:ECX.AVX[bit 28] = 1 (AVX instructions supported).
	 * (Step 3 can be done in any order relative to 1 and 2)
	 *
	 * 14.7.1 Detection of AVX2
	 * Hardware support for AVX2 is indicated by CPUID.(EAX=07H, ECX=0H):EBX.AVX2[bit 5]=1.
	 * Application Software must identify that hardware supports AVX, after that it must
	 * also detect support for AVX2 by checking CPUID.(EAX=07H, ECX=0H):EBX.AVX2[bit 5].
	 */
	return raid_cpu_match_avx(
		(1 << 27) | (1 << 28), /* OSXSAVE and AVX */
		1 << 5, /* AVX2 */
		3 << 1); /* OS saves XMM and YMM registers */
}

static inline int raid_cpu_has_avx512bw(void)
{
	/*
	 * Intel Architecture Instruction Set Extensions Programming Reference
	 * 319433-022 October 2014
	 *
	 * 2.2 Detection of 512-bit Instruction Groups of Intel AVX-512 Family
	 * 1) Detect CPUID.1:ECX.OSXSAVE[bit 27] = 1 (XGETBV enabled for application use)
	 * 2) Execute XGETBV and verify that XCR0[7:5] = `111b' (OPMASK state, upper 256-bit of
	 * ZMM0-ZMM15 and ZMM16-ZMM31 state are enabled by OS) and that XCR0[2:1] = `11b'
	 * (XMM state and YMM state are enabled by OS).
	 * 3) Verify both CPUID.0x7.0:EBX.AVX512F[bit 16] = 1, CPUID.0x7.0:EBX.AVX512BW[bit 30] = 1.
	 */

	/* note that intentionally we don't check for AVX and AVX2 */
	/* because the documentation doesn't require that */
	return raid_cpu_match_avx(
		1 << 27, /* XSAVE/XGETBV */
		(1 << 16) | (1 << 30), /* AVX512F and AVX512BW */
		(3 << 1) | (7 << 5)); /* OS saves XMM, YMM and ZMM registers */
}

/**
 * Check if the processor has a slow MULT implementation.
 * If yes, it's better to use a hash not based on multiplication.
 */
static inline int raid_cpu_has_slowmult(void)
{
	char vendor[CPU_VENDOR_MAX];
	unsigned family;
	unsigned model;

	raid_cpu_info(vendor, &family, &model);

	if (strcmp(vendor, "GenuineIntel") == 0) {
		/*
		 * Intel Atom
		 * Murmur3 based on MUL instruction, is a lot slower
		 * than Spooky2 based on SHIFTs.
		 * Like: 378 MB/s vs 3413 MB/s
		 */
		if (family == 6 && model == 28)
			return 1;
	}

	return 0;
}

/**
 * Check if the processor has a slow extended set of SSE registers.
 * If yes, it's better to limit the unroll to the firsrt 8 registers.
 */
static inline int raid_cpu_has_slowextendedreg(void)
{
	char vendor[CPU_VENDOR_MAX];
	unsigned family;
	unsigned model;

	raid_cpu_info(vendor, &family, &model);

	if (strcmp(vendor, "AuthenticAMD") == 0) {
		/*
		 * AMD Bulldozer
		 * PAR1/PAR2 implementations using 16 SSE registers are slower
		 * than ones using only the first 8 registers.
		 * Like: 4465 MB/s vs 4922 MB/s.
		 */
		if (family == 21)
			return 1;
	}

	return 0;
}
#endif

#endif

