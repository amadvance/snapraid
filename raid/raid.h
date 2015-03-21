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

#ifndef __RAID_H
#define __RAID_H

/**
 * RAID mode supporting up to 6 parities.
 *
 * It requires SSSE3 to get good performance with triple or more parities.
 *
 * This is the default mode set after calling raid_init().
 */
#define RAID_MODE_CAUCHY 0

/**
 * RAID mode supporting up to 3 parities,
 *
 * It has a fast triple parity implementation without SSSE3, but it cannot
 * go beyond triple parity.
 *
 * This is mostly intended for low end CPUs like ARM and AMD Athlon.
 */
#define RAID_MODE_VANDERMONDE 1

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
 * You must call this function before any other.
 *
 * The RAID system is initialized in the RAID_MODE_CAUCHY mode.
 */
void raid_init(void);

/**
 * Runs a basic functionality self test.
 *
 * The test is immediate, and it's intended to be run at application
 * startup to check the integrity of the RAID system.
 *
 * It returns 0 on success.
 */
int raid_selftest(void);

/**
 * Sets the mode to use. One of RAID_MODE_*.
 *
 * You can change mode at any time, and it will affect next calls to raid_gen(),
 * raid_rec() and raid_data().
 *
 * The two modes are compatible for the first two levels of parity.
 * The third one is different.
 */
void raid_mode(int mode);

/**
 * Sets the zero buffer to use in recovering.
 *
 * Before calling raid_rec() and raid_data() you must provide a memory
 * buffer filled with zero with the same size of the blocks to recover.
 *
 * This buffer is only read and never written.
 */
void raid_zero(void *zero);

/**
 * Computes parity blocks.
 *
 * This function computes the specified number of parity blocks of the
 * provided set of data blocks.
 *
 * Each parity block allows to recover one data block.
 *
 * @nd Number of data blocks.
 * @np Number of parities blocks to compute.
 * @size Size of the blocks pointed by @v. It must be a multiplier of 64.
 * @v Vector of pointers to the blocks of data and parity.
 *   It has (@nd + @np) elements. The starting elements are the blocks for
 *   data, following with the parity blocks.
 *   Data blocks are only read and not modified. Parity blocks are written.
 *   Each block has @size bytes.
 */
void raid_gen(int nd, int np, size_t size, void **v);

/**
 * Recovers failures in data and parity blocks.
 *
 * This function recovers all the data and parity blocks marked as bad
 * in the @ir vector.
 *
 * Ensure to have @nr <= @np, otherwise recovering is not possible.
 *
 * The parities blocks used for recovering are automatically selected from
 * the ones NOT present in the @ir vector.
 *
 * In case there are more parity blocks than needed, the parities at lower
 * indexes are used in the recovering, and the others are ignored.
 *
 * Note that no internal integrity check is done when recovering. If the
 * provided parities are correct, the resulting data will be correct.
 * If parities are wrong, the resulting recovered data will be wrong.
 * This happens even in the case you have more parities blocks than needed,
 * and some form of integrity verification would be possible.
 *
 * @nr Number of failed data and parity blocks to recover.
 * @ir[] Vector of @nr indexes of the failed data and parity blocks.
 *   The indexes start from 0. They must be in order.
 *   The first parity is represented with value @nd, the second with value
 *   @nd + 1, just like positions in the @v vector.
 * @nd Number of data blocks.
 * @np Number of parity blocks.
 * @size Size of the blocks pointed by @v. It must be a multiplier of 64.
 * @v Vector of pointers to the blocks of data and parity.
 *   It has (@nd + @np) elements. The starting elements are the blocks
 *   for data, following with the parity blocks.
 *   Each block has @size bytes.
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
 * @ip[] Vector of @nr indexes of the parity blocks to use for recovering.
 *   The indexes start from 0. They must be in order.
 * @nd Number of data blocks.
 * @size Size of the blocks pointed by @v. It must be a multiplier of 64.
 * @v Vector of pointers to the blocks of data and parity.
 *   It has (@nd + @ip[@nr - 1] + 1) elements. The starting elements are the
 *   blocks for data, following with the parity blocks.
 *   Each blocks has @size bytes.
 */
void raid_data(int nr, int *id, int *ip, int nd, size_t size, void **v);

/**
 * Check the provided failed blocks combination.
 *
 * This function checks if the specified failed blocks combination satisfies
 * the redundancy information. A combination is assumed matching, if the
 * remaining valid parity is matching the expected value after recovering.
 *
 * The number of failed blocks @nr must be strictly less than the number of
 * parities @np, because you need one more parity to validate the recovering.
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
 * @size Size of the blocks pointed by @v. It must be a multiplier of 64.
 * @v Vector of pointers to the blocks of data and parity.
 *   It has (@nd + @np) elements. The starting elements are the blocks
 *   for data, following with the parity blocks.
 *   Each block has @size bytes.
 * @return 0 if the check is satisfied. -1 otherwise.
 */
int raid_check(int nr, int *ir, int nd, int np, size_t size, void **v);

/**
 * Scan for failed blocks.
 *
 * This function identifies the failed data and parity blocks using the
 * available redundancy.
 *
 * It uses a brute force method, and then the call can be expansive.
 * The expected execution time is proportional at the binomial coefficient
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
 * @size Size of the blocks pointed by @v. It must be a multiplier of 64.
 * @v Vector of pointers to the blocks of data and parity.
 *   It has (@nd + @np) elements. The starting elements are the blocks
 *   for data, following with the parity blocks.
 *   Each block has @size bytes.
 * @return Number of block indexes returned in the @ir vector.
 *   0 if no error is detected.
 *   -1 if it's not possible to identify the failed disks.
 */
int raid_scan(int *ir, int nd, int np, size_t size, void **v);

#endif

