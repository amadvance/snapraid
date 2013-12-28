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

#define RAID_SWAP(a, b) \
	do { \
		if (v[a] > v[b]) { \
			int t = v[a]; \
			v[a] = v[b]; \
			v[b] = t; \
		} \
	} while (0)

void raid_sort(int n, int* v)
{
	/* sorting networks generated with the Batcher's Merge-Exchange algorithm */
	switch (n) {
	case 2 :
		RAID_SWAP(0, 1);
		break;
	case 3 :
		RAID_SWAP(0, 2);
		RAID_SWAP(0, 1);
		RAID_SWAP(1, 2);
		break;
	case 4 :
		RAID_SWAP(0, 2);
		RAID_SWAP(1, 3);
		RAID_SWAP(0, 1);
		RAID_SWAP(2, 3);
		RAID_SWAP(1, 2);
		break;
	case 5 :
		RAID_SWAP(0, 4);
		RAID_SWAP(0, 2);
		RAID_SWAP(1, 3);
		RAID_SWAP(2, 4);
		RAID_SWAP(0, 1);
		RAID_SWAP(2, 3);
		RAID_SWAP(1, 4);
		RAID_SWAP(1, 2);
		RAID_SWAP(3, 4);
		break;
	case 6 :
		RAID_SWAP(0, 4);
		RAID_SWAP(1, 5);
		RAID_SWAP(0, 2);
		RAID_SWAP(1, 3);
		RAID_SWAP(2, 4);
		RAID_SWAP(3, 5);
		RAID_SWAP(0, 1);
		RAID_SWAP(2, 3);
		RAID_SWAP(4, 5);
		RAID_SWAP(1, 4);
		RAID_SWAP(1, 2);
		RAID_SWAP(3, 4);
		break;
	}
}

