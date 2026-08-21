// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2013 Andrea Mazzoleni

#ifndef __RAID_TEST_H
#define __RAID_TEST_H

/**
 * Tests insertion function.
 *
 * Tests raid_insert() with all the possible combinations of elements to insert.
 *
 * Returns 0 on success.
 */
int raid_test_insert(void);

/**
 * Tests sorting function.
 *
 * Tests raid_sort() with all the possible combinations of elements to sort.
 *
 * Returns 0 on success.
 */
int raid_test_sort(void);

/**
 * Tests combination functions.
 *
 * Tests combination_first() and combination_next() for all the parity levels.
 *
 * Returns 0 on success.
 */
int raid_test_combo(void);

/**
 * Tests all RAID affine GFNI matrices against scalar GF multiplication.
 *
 * Returns 0 on success.
 */
int raid_test_gfaffine(void);

/**
 * Tests the Galois field and Cauchy matrix properties.
 *
 * Verifies the expected polynomial and matrix construction and basic
 * properties of the Extended Cauchy matrix for the specified RAID mode.
 *
 * Returns 0 on success.
 */
int raid_test_poly(unsigned mode);

/**
 * Tests recovering functions.
 *
 * All the recovering functions are tested with all the combinations
 * of failing disks and recovering parities.
 *
 * Take care that the test time grows exponentially with the number of disks.
 *
 * Returns 0 on success.
 */
int raid_test_rec(unsigned mode, int nd, size_t size);

/**
 * Tests P/Q double-disk recovery at selected G23 positions.
 *
 * Returns 0 on success.
 */
int raid_test_rec2_g23(size_t size);

/**
 * Tests recovering functions on the tail data disks.
 *
 * All the recovering functions are tested by recovering the last 1, 2, ..., np
 * data disks for each disk count from 1 to nd.
 *
 * Returns 0 on success.
 */
int raid_test_tail(unsigned mode, int nd, size_t size);

/**
 * Tests parity generation functions.
 *
 * All the parity generation functions are tested with the specified
 * number of disks.
 *
 * Returns 0 on success.
 */
int raid_test_par(unsigned mode, int nd, size_t size);

#endif
