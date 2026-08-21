// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2013 Andrea Mazzoleni

#ifndef __RAID_H
#define __RAID_H

/*
 * RAID operating modes and GF(2^8) field selection.
 *
 * RAID parity computation operates in GF(2^8), a finite field where
 * arithmetic is performed modulo an irreducible polynomial of degree 8.
 * The choice of operating mode affects both correctness and performance:
 * all tables, coefficients, and parity data are tied to the selected
 * polynomial and generator matrix and cannot be mixed between
 * incompatible operating modes.
 *
 * Two polynomials are supported:
 *
 * 0x1d  (x^8 + x^4 + x^3 + x^2 + 1)  -- Standard RAID polynomial
 *
 *   The polynomial used by the original RAID-6 specification and the
 *   Linux kernel RAID implementation, used with primitive generator g=2.
 *   The P and Q rows preserve the standard RAID5/RAID6 coefficient layout,
 *   providing compatibility with the corresponding Linux RAID parity.
 *   Higher parity rows are SnapRAID's Extended Cauchy extension.
 *
 *   On CPUs supporting Intel GFNI (Galois Field New Instructions),
 *   generation and recovery are accelerated with vgf2p8affineqb.
 *   Multiplication by each GF(2^8) coefficient is represented as a
 *   precomputed GF(2) 8x8 affine matrix for the 0x11d field.
 *
 * 0x1b  (x^8 + x^4 + x^3 + x + 1)    -- AES polynomial
 *
 *   The polynomial used by the AES encryption standard (0x11b including
 *   the x^8 term). Parity uses a 6x251 Extended Cauchy matrix whose Q row
 *   follows the G23 sequence.
 *
 *   On CPUs supporting Intel GFNI, generation and recovery can use the
 *   vgf2p8mulb instruction, which performs GF(2^8) multiplication directly
 *   in hardware using this polynomial. This native multiplication is
 *   typically faster than the affine GFNI implementation required for the
 *   standard RAID polynomial.
 *
 *   The trade-off is that parity data is not compatible with arrays using
 *   the standard 0x11d polynomial (g=2), so all data must be generated and
 *   recovered using the same operating mode.
 *
 * Both Cauchy modes can therefore use GFNI acceleration. The standard RAID
 * mode preserves compatibility by using affine GFNI transformations, while
 * the AES mode trades compatibility for the typically faster native GFNI
 * multiplication instruction.
 */

/**
 * Special mode code used to query the active mode.
 */
#define RAID_MODE_GET -1

/**
 * Default RAID mode supporting up to 6 parities using standard polynomial 0x1d and generator g=2.
 *
 * Provides high performance on modern CPUs with SSSE3, AVX2, AVX-512, GFNI, or NEON support.
 * On GFNI CPUs, the standard RAID field is accelerated using affine transformations.
 * Ensures maximum compatibility with standard Linux RAID arrays.
 *
 * This is the default mode set after calling raid_init().
 */
#define RAID_MODE_CAUCHY_RAID 0

/**
 * Legacy RAID mode supporting up to 3 parities using standard polynomial 0x1d.
 *
 * Uses Vandermonde matrix coefficients for triple parity (Z parity), which allows
 * efficient 3-parity generation on older/low-end CPUs without SSSE3 or NEON.
 *
 * It cannot be used for arrays with more than 3 parities.
 */
#define RAID_MODE_VANDERMONDE_RAID 1

/**
 * RAID mode supporting up to 6 parities and 251 data disks using AES polynomial 0x1b (0x11b).
 *
 * It uses an Extended Cauchy matrix with a G23 Q sequence, allowing optimized
 * generators to use multiplication by 2 for almost every disk transition.
 * On CPUs supporting GFNI, it uses native VGF2P8MULB multiplication.
 *
 * This parity layout is incompatible with RAID_MODE_CAUCHY_RAID and with the
 * previous RAID_MODE_CAUCHY_AES layout.
 */
#define RAID_MODE_CAUCHY_AES 2

/**
 * Maximum number of parity disks supported.
 */
#define RAID_PARITY_MAX 6

/**
 * Maximum number of data disks supported.
 */
#define RAID_DATA_MAX 251

/**
 * Initializes the RAID system.
 *
 * You must call this function before any other RAID function.
 *
 * Initialization modifies global RAID state and must not be called
 * concurrently with any other RAID function.
 *
 * The RAID system is initialized in the RAID_MODE_CAUCHY_RAID mode.
 */
void raid_init(void);

/**
 * Sets or queries the mode to use. One of RAID_MODE_*.
 *
 * The selected mode is global to the RAID library and affects all subsequent
 * generation, recovery, validation, and scanning operations.
 *
 * Passing RAID_MODE_GET (-1) queries the current active mode without changing it.
 * Passing a valid mode (RAID_MODE_CAUCHY_RAID, RAID_MODE_VANDERMONDE_RAID,
 * RAID_MODE_CAUCHY_AES) sets the mode.
 *
 * Changing the mode modifies global RAID state. raid_mode() must not be called
 * concurrently with any other RAID function.
 *
 * Returns the mode that was active prior to this call.
 */
int raid_mode(int mode);

/**
 * Runs a basic functionality self test.
 *
 * raid_init() must be called before this function.
 *
 * If a mode different from the default is required, raid_mode() must be
 * called before this function. The self test verifies the currently selected
 * RAID mode.
 *
 * The test is immediate, and it's intended to be run at application
 * startup to check the integrity of the RAID system.
 *
 * The test modifies global RAID state and must not be called concurrently
 * with any other RAID function.
 *
 * It returns 0 on success.
 */
int raid_selftest(void);

/**
 * Sets the zero buffer to use during recovery.
 *
 * Before calling raid_rec() and raid_data() you must provide a memory
 * buffer filled with zeros of the same size as the blocks to recover.
 *
 * The buffer pointed to by @zero must be aligned to a 64-byte boundary.
 *
 * This buffer is only read and never written.
 *
 * The selected zero buffer is global to the RAID library. raid_zero() must
 * not be called concurrently with any other RAID function.
 *
 * Once set, the buffer must remain valid and unchanged while it is being
 * used by recovery operations.
 */
void raid_zero(void *zero);

/**
 * Block aliasing
 *
 * In raid_gen(), block pointers passed through @v may alias the same block
 * buffer. Blocks are processed from lower to higher offsets. For each
 * processed byte or vector chunk, all required inputs are read before
 * outputs are written. Parity outputs are written in increasing parity
 * order.
 *
 * If multiple parity outputs alias the same block, the value of the last
 * parity written remains in that block.
 *
 * In raid_rec() and raid_data(), each block being recovered must have a
 * distinct destination buffer that does not alias any other block in @v.
 *
 * Arbitrary partial overlap between different block ranges is not supported.
 */

/**
 * Computes parity blocks.
 *
 * This function computes the specified number of parity blocks of the
 * provided set of data blocks.
 *
 * Each parity block allows to recover one data block.
 *
 * @nd Number of data blocks.
 * @np Number of parity blocks to compute.
 * @size Size of the blocks pointed to by @v. It must be a multiple of 64.
 * @v Vector of pointers to the blocks of data and parity.
 *   It has (@nd + @np) elements. The starting elements are the blocks for
 *   data, following with the parity blocks.
 *   Data entries are used as inputs and parity entries as outputs.
 *   Entries may point to the same block buffer.
 *   Each block has @size bytes and must be aligned to a 64-byte boundary.
 */
void raid_gen(int nd, int np, size_t size, void **v);

/**
 * Recovers failures in data and parity blocks.
 *
 * This function recovers all the data and parity blocks marked as bad
 * in the @ir vector.
 *
 * Ensure @nr <= @np, otherwise recovery is not possible.
 *
 * The parity blocks used for recovery are automatically selected from
 * the ones NOT present in the @ir vector.
 *
 * In case there are more parity blocks than needed, the parities at lower
 * indexes are used for recovery, and the others are ignored.
 *
 * Note that no internal integrity check is done when recovering. If the
 * provided parities are correct, the resulting data will be correct.
 * If parities are wrong, the resulting recovered data will be wrong.
 * This happens even in the case you have more parity blocks than needed,
 * and some form of integrity verification would be possible.
 *
 * @nr Number of failed data and parity blocks to recover.
 * @ir[] Vector of @nr indexes of the failed data and parity blocks.
 *   The indexes start from 0. They must be in order.
 *   The first parity is represented with value @nd, the second with value
 *   @nd + 1, just like positions in the @v vector.
 * @nd Number of data blocks.
 * @np Number of parity blocks.
 * @size Size of the blocks pointed by @v. It must be a multiple of 64.
 * @v Vector of pointers to the blocks of data and parity.
 *   It has (@nd + @np) elements. The starting elements are the blocks
 *   for data, following with the parity blocks.
 *   Each block being recovered must have a distinct destination buffer
 *   that does not alias any other block in this vector.
 *   Each block has @size bytes and must be aligned to a 64-byte boundary.
 */
void raid_rec(int nr, int *ir, int nd, int np, size_t size, void **v);

/**
 * Recovers failures in data blocks only.
 *
 * This function recovers all the data blocks marked as bad in the @id vector.
 * The parity blocks are not modified.
 *
 * @nr Number of failed data blocks to recover.
 * @id[] Vector of @nr indexes of the data blocks to recover.
 *   The indexes start from 0. They must be in order.
 * @ip[] Vector of @nr indexes of the parity blocks to use for recovery.
 *   The indexes start from 0. They must be in order.
 * @nd Number of data blocks.
 * @size Size of the blocks pointed to by @v. It must be a multiple of 64.
 * @v Vector of pointers to the blocks of data and parity.
 *   It has (@nd + @ip[@nr - 1] + 1) elements. The starting elements are the
 *   blocks for data, following with the parity blocks.
 *   Each data block being recovered must have a distinct destination buffer
 *   that does not alias any other block in this vector.
 *   Each block has @size bytes and must be aligned to a 64-byte boundary.
 */
void raid_data(int nr, int *id, int *ip, int nd, size_t size, void **v);

/**
 * Check the provided failed blocks combination.
 *
 * This function checks if the specified failed blocks combination satisfies
 * the redundancy information. A combination is assumed matching, if the
 * remaining valid parity matches the expected value after recovery.
 *
 * The number of failed blocks @nr must be strictly less than the number of
 * parities @np, because you need one more parity to validate recovery.
 *
 * No data or parity blocks are modified.
 *
 * @nr Number of failed data and parity blocks.
 * @ir[] Vector of @nr indexes of the failed data and parity blocks.
 *   The indexes start from 0. They must be in order.
 *   The first parity is represented with value @nd, the second with value
 *   @nd + 1, just like positions in the @v vector.
 * @nd Number of data blocks.
 * @np Number of parity blocks.
 * @size Size of the blocks pointed by @v. It must be a multiple of 64.
 * @v Vector of pointers to the blocks of data and parity.
 *   It has (@nd + @np) elements. The starting elements are the blocks
 *   for data, following with the parity blocks.
 *   Each block has @size bytes and must be aligned to a 64-byte boundary.
 * @return 0 if the check is satisfied. -1 otherwise.
 */
int raid_check(int nr, int *ir, int nd, int np, size_t size, void **v);

/**
 * Scan for failed blocks.
 *
 * This function searches for a set of failed data and parity blocks
 * compatible with the available redundancy.
 *
 * It first checks for no failures, and then tries candidate combinations
 * with an increasing number of failures, from 1 up to @np - 1. The first
 * combination satisfying the redundancy information is returned.
 *
 * If multiple combinations satisfy the redundancy information, only the
 * first one encountered is returned. This function does not check that the
 * returned combination is unique.
 *
 * It uses a brute force method, and then the call can be expensive.
 * The expected execution time is proportional to the binomial coefficient
 * @np + @nd choose @np - 1, usually written as:
 *
 * ( @np + @nd )
 * (           )
 * (  @np - 1  )
 *
 * No data or parity blocks are modified.
 *
 * The failed block indexes are returned in the @ir vector.
 * It must have space for at least @np - 1 values.
 *
 * The returned @ir vector can then be used in a raid_rec() call to recover
 * the failed data and parity blocks.
 *
 * @ir[] Vector filled with the indexes of the failed data and parity blocks.
 *   The indexes start from 0 and they are in order.
 *   The first parity is represented with value @nd, the second with value
 *   @nd + 1, just like positions in the @v vector.
 * @nd Number of data blocks.
 * @np Number of parity blocks.
 * @size Size of the blocks pointed by @v. It must be a multiple of 64.
 * @v Vector of pointers to the blocks of data and parity.
 *   It has (@nd + @np) elements. The starting elements are the blocks
 *   for data, following with the parity blocks.
 *   Each block has @size bytes and must be aligned to a 64-byte boundary.
 * @return Number of block indexes returned in the @ir vector.
 *   0 if no error is detected.
 *   -1 if no compatible combination with fewer than @np failures is found.
 */
int raid_scan(int *ir, int nd, int np, size_t size, void **v);

#endif
