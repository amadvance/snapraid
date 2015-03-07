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
		0); /* SSE2 */
}

static inline int raid_cpu_has_avx2(void)
{
	/*
	 * Intel Architecture Instruction Set Extensions Programming Reference
	 * 319433-022 October 2014
	 *
	 * 14.7.1 Detection of AVX2
	 * Hardware support for AVX2 is indicated by CPUID.(EAX=07H, ECX=0H):EBX.AVX2[bit 5]=1.
	 * Application Software must identify that hardware supports AVX, after that it must
	 * also detect support for AVX2 by checking CPUID.(EAX=07H, ECX=0H):EBX.AVX2[bit 5].
	 */
	return raid_cpu_match_avx(
		(1 << 27) | (1 << 5), /* OSXSAVE and AVX */
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
	return raid_cpu_match_avx(
		(1 << 27) | (1 << 28), /* XSAVE/XGETBV and AVX */
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
		/* Intel(R) Atom(TM) CPU D525 @ 1.80GHz (info from user)
		 * Compiler gcc 4.7.2
		 * CPU GenuineIntel, family 6, model 28, flags mmx sse2 slowmult
		 * Memory is little-endian 64-bit
		 * Speed test with 8 disk and 262144 buffer size...
		 * memset0 1849 [MB/s]
		 * HASH Murmur3 378 [MB/s] (extremely slower than Spooky2)
		 * HASH Spooky2 3413 [MB/s]
		 * PAR1 int32x2 707 [MB/s]
		 * PAR1 mmxx2 1264 [MB/s]
		 * PAR1 mmxx4 1910 [MB/s]
		 * PAR1 sse2x2 2204 [MB/s]
		 * PAR1 sse2x4 2980 [MB/s]
		 * PAR2 int32x2 296 [MB/s]
		 * PAR2 mmxx2 543 [MB/s]
		 * PAR2 sse2x2 1068 [MB/s]
		 * PAR2 sse2x4 1601 [MB/s]
		 */
		if (family == 6 && model == 28)
			return 1;
	}

	return 0;
}

/**
 * Check if the processor has a slow PSHUFB implementation.
 * If yes, it's better to not use it.
 */
static inline int raid_cpu_has_slowpshufb(void)
{
	char vendor[CPU_VENDOR_MAX];
	unsigned family;
	unsigned model;

	raid_cpu_info(vendor, &family, &model);

	if (strcmp(vendor, "GenuineIntel") == 0) {
		/* Intel ATOM (info from x264 -> full family/model 6/28) */

		/* Intel(R) Atom(TM) CPU D525 @ 1.80GHz (info from user)
		 * Compiler gcc 4.7.2
		 * CPU GenuineIntel, family 6, model 28, flags mmx sse2 ssse3 slowmult
		 * Memory is little-endian 64-bit
		 * Support nanosecond timestamps with futimens()
		 *
		 * Speed test with 8 disk and 262144 buffer size, for a total of 2816 KiB...
		 * memset0 1850 [MB/s]
		 * CRC table 316 [MB/s]
		 * HASH Murmur3 378 [MB/s]
		 * HASH Spooky2 3389 [MB/s]
		 * PAR1 int32 1800 [MB/s]
		 * PAR1 int64 2938 [MB/s]
		 * PAR1 mmx 2946 [MB/s]
		 * PAR1 sse2 3429 [MB/s]
		 * PAR1 sse2ext 3447 [MB/s]
		 * PAR2 int32 519 [MB/s]
		 * PAR2 int64 944 [MB/s]
		 * PAR2 mmx 1160 [MB/s]
		 * PAR2 sse2 2082 [MB/s]
		 * PAR2 sse2ext 2481 [MB/s]
		 * PARz int32 236 [MB/s]
		 * PARz int64 456 [MB/s]
		 * PARz mmx 576 [MB/s]
		 * PARz sse2 1109 [MB/s]
		 * PARz sse2ext 1527 [MB/s]
		 * PARz ssse3 931 [MB/s] (slower than sse2ext)
		 * PARz ssse3ext 932 [MB/s] (slower than sse2ext)
		 * PAR4z int32 144 [MB/s]
		 * PAR4z int64 271 [MB/s]
		 * PAR4z mmx 431 [MB/s]
		 * PAR4z sse2 843 [MB/s]
		 * PAR4z sse2ext 1086 [MB/s]
		 * PAR4z ssse3 732 [MB/s] (slower than sse2ext)
		 * PAR4z ssse3ext 571 [MB/s] (slower than sse2ext)
		 */
		if (family == 6 && model == 28)
			return 1;
	}

	if (strcmp(vendor, "AuthenticAMD") == 0) {
		/* AMD Jaguar (info from x264 -> full family 0x16==21) */
		if (family == 21)
			return 1;
	}

	return 0;
}

/**
 * Check if the processor has a slow extended set of SSE registers.
 * If yes, it's better to unroll without using the second part of registers.
 */
static inline int raid_cpu_has_slowextendedreg(void)
{
	char vendor[CPU_VENDOR_MAX];
	unsigned family;
	unsigned model;

	raid_cpu_info(vendor, &family, &model);

	if (strcmp(vendor, "AuthenticAMD") == 0) {
		/* AMD Bulldozer (from user)
		 * CPU AuthenticAMD, family 21, model 19, flags mmx sse2
		 * Memory is little-endian 64-bit
		 * Speed test with 8 disk and 262144 buffer size, for a total of 2560 KiB...
		 * memset0 5721 [MB/s]
		 * CRC table 1080 [MB/s]
		 * CRC intel-crc32 2845 [MB/s]
		 * HASH Murmur3 2970 [MB/s]
		 * HASH Spooky2 7503 [MB/s]
		 * PAR1 int32x2 4595 [MB/s]
		 * PAR1 mmxx2 5856 [MB/s]
		 * PAR1 mmxx4 6157 [MB/s]
		 * PAR1 sse2x2 7151 [MB/s]
		 * PAR1 sse2x4 8447 [MB/s]
		 * PAR1 sse2x8 8155 [MB/s] (slower than sse2x4)
		 * PAR2 int32x2 1892 [MB/s]
		 * PAR2 mmxx2 3744 [MB/s]
		 * PAR2 sse2x2 4922 [MB/s]
		 * PAR2 sse2x4 4464 [MB/s] (slower than sse2x2)
		 */
		if (family == 21)
			return 1;
	}

	return 0;
}

/* other cases */

/* Intel(R) Core(TM) i7-3740QM CPU @ 2.70GHz
 * Compiler gcc 4.7.3
 * CPU GenuineIntel, family 6, model 58, flags mmx sse2
 * Memory is little-endian 32-bit
 * memset0 34685 [MB/s]
 * Murmur3 4170 [MB/s]
 * Spooky2 2599 [MB/s]
 * PAR1 int32x2 8919 [MB/s]
 * PAR1 mmxx2 17170 [MB/s]
 * PAR1 sse2x2 27478 [MB/s]
 * PAR2 int32x2 1953 [MB/s]
 * PAR2 mmxx2 7560 [MB/s]
 * PAR2 sse2x2 13930 [MB/s]
 */

/* Intel(R) Core(TM) i7-3740QM CPU @ 2.70GHz
 * Compiler gcc 4.7.3
 * CPU GenuineIntel, family 6, model 58, flags mmx sse2
 * Memory is little-endian 64-bit
 * memset0 26164 [MB/s]
 * Murmur3 4469 [MB/s]
 * Spooky2 12834 [MB/s]
 * PAR1 int32x2 10775 [MB/s]
 * PAR1 mmxx2 19644 [MB/s]
 * PAR1 sse2x2 29168 [MB/s]
 * PAR2 int32x2 3288 [MB/s]
 * PAR2 mmxx2 8374 [MB/s]
 * PAR2 sse2x2 15547 [MB/s]
 */

/* Intel(R) Core(TM) i5 CPU 650 @ 3.20GHz
 * Compiler gcc 4.7.1
 * CPU GenuineIntel, family 6, model 37, flags mmx sse2
 * Memory is little-endian 32-bit
 * memset0 25468 [MB/s]
 * Murmur3 3357 [MB/s]
 * Spooky2 1861 [MB/s]
 * PAR1 int32x2 5043 [MB/s]
 * PAR1 mmxx2 9864 [MB/s]
 * PAR1 sse2x2 16896 [MB/s]
 * PAR2 int32x2 1100 [MB/s]
 * PAR2 mmxx2 5056 [MB/s]
 * PAR2 sse2x2 8726 [MB/s]
 */

/* AMD Athlon(tm) 64 X2 Dual Core Processor 3600+ 1913.377 MHz
 * Compiler gcc 4.7.3
 * CPU AuthenticAMD, family 15, model 107, flags mmx sse2
 * Memory is little-endian 32-bit
 * memset 2414 [MB/s]
 * Murmur3 1213 [MB/s]
 * Spooky2 947 [MB/s]
 * PAR1 int32x2 532 [MB/s]
 * PAR1 mmxx2 1121 [MB/s]
 * PAR1 sse2x2 2061 [MB/s]
 * PAR2 int32x2 280 [MB/s]
 * PAR2 mmxx2 632 [MB/s]
 * PAR2 sse2x2 909 [MB/s]
 */

/* ARM Feroceon 88FR131 rev 1 (v5l)
 * Compiler gcc 4.7.2
 * CPU is not a x86/x64
 * Memory is little-endian 32-bit
 * memset 763 [MB/s]
 * Murmur3 237 [MB/s]
 * Spooky2 216 [MB/s]
 * PAR1 int32x2 233 [MB/s]
 * PAR2 int32x2 103 [MB/s]
 */

/* Intel(R) Xeon(R) CPU E3-1270 V2 @ 3.50GHz $
 * Compiler gcc 4.8.1
 * CPU GenuineIntel, family 6, model 58, flags mmx sse2
 * Memory is little-endian 64-bit
 * memset 27842 [MB/s]
 * Murmur3 4884 [MB/s]
 * Spooky2 14039 [MB/s]
 * PAR1 int32x2 11038 [MB/s]
 * PAR1 mmxx2 20055 [MB/s]
 * PAR1 sse2x2 30703 [MB/s]
 * PAR2 int32x2 3267 [MB/s]
 * PAR2 mmxx2 8883 [MB/s]
 * PAR2 sse2x2 16433 [MB/s]
 */

/* Bobcat/Zacade (info from x264 -> full family 20)
 * AMD E-350 Processor (info from user)
 * Compiler gcc 4.7.3
 * CPU AuthenticAMD, family 20, model 1, flags mmx sse2
 * Memory is little-endian 64-bit
 * memset 2137 [MB/s]
 * Murmur3 1140 [MB/s]
 * Spooky2 2326 [MB/s]
 * PAR1 int32x2 1853 [MB/s]
 * PAR1 mmxx2 2019 [MB/s]
 * PAR1 sse2x2 2908 [MB/s]
 * PAR2 int32x2 884 [MB/s]
 * PAR2 mmxx2 1502 [MB/s]
 * PAR2 sse2x2 1168 [MB/s] (slow version with prefetchnta)
 */

/* AMD Turion(tm) II Neo N40L Dual-Core Processor (info from user)
 * Compiler gcc 4.7.1
 * CPU AuthenticAMD, family 16, model 6, flags mmx sse2
 * Memory is little-endian 64-bit
 * memset 4910 [MB/s]
 * Murmur3 1160 [MB/s]
 * Spooky2 3994 [MB/s]
 * PAR1 int32x2 530 [MB/s]
 * PAR1 mmxx2 1114 [MB/s]
 * PAR1 sse2x1 2127 [MB/s]
 * PAR1 sse2x2 2804 [MB/s]
 * PAR2 int32x2 316 [MB/s]
 * PAR2 mmxx2 655 [MB/s]
 * PAR2 sse2x2 639 [MB/s] (slow version with prefetchnta)
 */

/* AMD Athlon(tm) II X4 620 (info from user)
 * Compiler gcc 4.7.3
 * CPU AuthenticAMD, family 16, model 5, flags mmx sse2
 * Memory is little-endian 64-bit
 * memset 2845 [MB/s]
 * Murmur3 2262 [MB/s]
 * Murmur3x64 3500 [MB/s]
 * Spooky2 4362 [MB/s]
 * Spooky2x86 4000 [MB/s]
 * PAR1 int32x2 1064 [MB/s]
 * PAR1 mmxx2 2064 [MB/s]
 * PAR1 sse2x2 3289 [MB/s]
 * PAR2 int32x2 639 [MB/s]
 * PAR2 mmxx2 1325 [MB/s]
 * PAR2 sse2x2 915 [MB/s] (slow version with prefetchnta)
 */

/* ARM @ 1.2GHz (info from user)
 * /proc/cpuinfo
 * model name      : Feroceon 88FR131 rev 1 (v5l)
 * BogoMIPS        : 1196.85
 * Features        : swp half thumb fastmult edsp
 * CPU implementer : 0x56
 * CPU architecture: 5TE
 * CPU variant     : 0x2
 * CPU part        : 0x131
 * CPU revision    : 1
 * Hardware        : QNAP TS-41x
 *
 * Compiler gcc 4.7.2
 * CPU is not a x86/x64
 * Memory is little-endian 32-bit
 * Speed test with 4 disk and 262144 buffer size...
 * memset 763 [MB/s]
 * Murmur3 237 [MB/s]
 * Spooky2 216 [MB/s]
 * Spooky2x86 418 [MB/s] (dropped experiment of a new hash)
 * PAR1 int32x2 233 [MB/s]
 * PAR2 int32x2 103 [MB/s]
 */
#endif

#endif

