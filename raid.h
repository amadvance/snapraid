/*
 * Copyright (C) 2011 Andrea Mazzoleni
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

#ifndef __RAID_H
#define __RAID_H

/****************************************************************************/
/* raid */

/**
 * Max number of data disks for RAID6.
 */
#define RAID6_DATA_LIMIT 21

/**
 * Max number of data disks for RAIDQP.
 */
#define RAIDQP_DATA_LIMIT 21

/**
 * Syndrome computation.
 */
void raid_gen(unsigned level, unsigned char** buffer, unsigned diskmax, unsigned size);

/*
 * Recover failure of one data block x using parity i.
 */
void raid_recov_1data(unsigned char** dptrs, unsigned diskmax, unsigned size, int x, int i, unsigned char* zero);

/*
 * Recover failure of two data blocks x,y using parity i,j.
 */
void raid_recov_2data(unsigned char** dptrs, unsigned diskmax, unsigned size, int x, int y, int i, int j, unsigned char* zero);

/*
 * Recover failure of three data blocks x,y,z using parity i,j,k.
 */
void raid_recov_3data(unsigned char** dptrs, unsigned diskmax, unsigned size, int x, int y, int z, int i, int j, int k, unsigned char* zero);

/*
 * Recover failure of four data blocks x,y,z,v using parity i,j,k,l.
 */
void raid_recov_4data(unsigned char** dptrs, unsigned diskmax, unsigned size, int x, int y, int z, int v, int i, int j, int k, int l, unsigned char* zero);

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
void raidTP_int32r2(unsigned char** dptr, unsigned diskmax, unsigned size);
void raidTP_mmxr1(unsigned char** buffer, unsigned diskmax, unsigned size);
void raidTP_sse2r1(unsigned char** dptr, unsigned diskmax, unsigned size);
void raidTP_sse2r2(unsigned char** dptr, unsigned diskmax, unsigned size);
void raidQP_int32r2(unsigned char** dptr, unsigned diskmax, unsigned size);
void raidQP_mmxr1(unsigned char** buffer, unsigned diskmax, unsigned size);
void raidQP_sse2r1(unsigned char** dptr, unsigned diskmax, unsigned size);
void raidQP_sse2r2(unsigned char** buffer, unsigned diskmax, unsigned size);

#endif

