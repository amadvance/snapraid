// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2013 Andrea Mazzoleni

#ifndef __RAID_HELPER_H
#define __RAID_HELPER_H

/**
 * Inserts an integer in a sorted vector.
 *
 * This function can be used to insert indexes in order, ready to be used for
 * calling raid_rec().
 *
 * @n Number of integers currently in the vector.
 * @v Vector of integers already sorted.
 *   It must have extra space for the new element at the end.
 * @i Value to insert.
 */
void raid_insert(int n, int *v, int i);

/**
 * Sorts a small vector of integers.
 *
 * If you have indexes not in order, you can use this function to sort them
 * before calling raid_rec().
 *
 * @n Number of integers. No more than RAID_PARITY_MAX.
 * @v Vector of integers.
 */
void raid_sort(int n, int *v);

#endif

