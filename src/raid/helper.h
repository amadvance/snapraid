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
 *   It must have extra space for the new elemet at the end.
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

