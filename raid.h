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

#ifndef __RAID_H
#define __RAID_H

/****************************************************************************/
/* raid */

/**
 * Syndrome computation.
 */
void raid_gen(unsigned level, unsigned char** buffer, unsigned diskmax, unsigned size);

/*
 * Recover failure of one data block.
 */
void raid5_recov_data(unsigned char** dptrs, unsigned diskmax, unsigned size, int faila);

/*
 * Recover two failed data blocks.
 */
void raid6_recov_2data(unsigned char** dptrs, unsigned diskmax, unsigned size, int faila, int failb, unsigned char* zero);

/*
 * Recover failure of one data block plus the P block.
 */
void raid6_recov_datap(unsigned char** dptrs, unsigned diskmax, unsigned size, int faila, unsigned char* zero);

/**
 * Initializes the RAID system.
 */
void raid_init(void);

/**
 * Internal specialized syndrome computation.
 */
void raid5_int32r2(unsigned char** dptr, unsigned diskmax, unsigned size);
void raid5_mmxr2(unsigned char** dptr, unsigned diskmax, unsigned size);
void raid5_mmxr4(unsigned char** dptr, unsigned diskmax, unsigned size);
void raid5_sse2r2(unsigned char** dptr, unsigned diskmax, unsigned size);
void raid5_sse2r4(unsigned char** dptr, unsigned diskmax, unsigned size);
void raid5_sse2r8(unsigned char** dptr, unsigned diskmax, unsigned size);
void raid6_int32r2(unsigned char** dptr, unsigned diskmax, unsigned size);
void raid6_mmxr2(unsigned char** dptr, unsigned diskmax, unsigned size);
void raid6_sse2r2(unsigned char** dptr, unsigned diskmax, unsigned size);
void raid6_sse2r4(unsigned char** buffer, unsigned diskmax, unsigned size);

#endif

