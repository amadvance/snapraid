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

#ifndef __RAID_TEST_H
#define __RAID_TEST_H

/**
 * Tests insertion function.
 *
 * Test raid_insert() with all the possible combinations of elements to insert.
 *
 * Returns 0 on success.
 */
int raid_test_insert(void);

/**
 * Tests sorting function.
 *
 * Test raid_sort() with all the possible combinations of elements to sort.
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
 * Tests parity generation functions.
 *
 * All the parity generation functions are tested with the specified
 * number of disks.
 *
 * Returns 0 on success.
 */
int raid_test_par(unsigned mode, int nd, size_t size);

#endif

