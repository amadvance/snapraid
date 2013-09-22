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
#define RAID6_DATA_LIMIT 255

/**
 * Max number of data disks for RAIDQP.
 */
#define RAIDQP_DATA_LIMIT 21

/**
 * Initializes the RAID system.
 */
void raid_init(void);

/**
 * Computes the specified number of parities.
 * \param level Number of parities to compute.
 * \param vbuf Vector of pointers to the blocks for disks and parities.
 * It has (::data + ::level) elements. Each element points to a buffer of ::size bytes.
 * \param data Number of data disks.
 * \param size Size of the blocks pointed by vbuf.
 */
void raid_gen(unsigned level, unsigned char** vbuf, unsigned data, unsigned size);

/*
 * Recovers failure of one data block x using parity i.
 * Note that only the data block is recovered. The full parity is instead destroyed.
 * \param x Index of the data disk to recover. Starting from 0.
 * \param i Index of the parity to use for the recovering. Starting from 0.
 * \param vbuf Vector of pointers to the blocks for disks and parities.
 * It has (::data + ::i + 1) elements. Each element points to a buffer of ::size bytes.
 * \param data Number of data disks.
 * \param zero Buffer filled with 0 of ::size bytes.
 * This buffer is not modified.
 * \param size Size of the blocks pointed by vbuf.
 */
void raid_recov_1data(int x, int i, unsigned char** vbuf, unsigned data, unsigned char* zero, unsigned size);

/*
 * Recovers failure of two data blocks x,y using parity i,j.
 * Note that only the data blocks are recovered. The full parity is instead destroyed.
 */
void raid_recov_2data(int x, int y, int i, int j, unsigned char** vbuf, unsigned data, unsigned char* zero, unsigned size);

/*
 * Recovers failure of three data blocks x,y,z using parity i,j,k.
 * Note that only the data blocks are recovered. The full parity is instead destroyed.
 */
void raid_recov_3data(int x, int y, int z, int i, int j, int k, unsigned char** vbuf, unsigned data, unsigned char* zero, unsigned size);

/*
 * Recovers failure of four data blocks x,y,z,v using parity i,j,k,l.
 * Note that only the data blocks are recovered. The full parity is instead destroyed.
 */
void raid_recov_4data(int x, int y, int z, int v, int i, int j, int k, int l, unsigned char** vbuf, unsigned data, unsigned char* zero, unsigned size);

/**
 * Internal specialized parity computation.
 * Only for testing.
 */
void raid5_int32r2(unsigned char** vbuf, unsigned data, unsigned size);
void raid5_int64r2(unsigned char** vbuf, unsigned data, unsigned size);
void raid5_mmxr4(unsigned char** vbuf, unsigned data, unsigned size);
void raid5_sse2r4(unsigned char** vbuf, unsigned data, unsigned size);
void raid5_sse2r8(unsigned char** vbuf, unsigned data, unsigned size);
void raid6_int32r2(unsigned char** vbuf, unsigned data, unsigned size);
void raid6_int64r2(unsigned char** vbuf, unsigned data, unsigned size);
void raid6_mmxr2(unsigned char** vbuf, unsigned data, unsigned size);
void raid6_sse2r2(unsigned char** vbuf, unsigned data, unsigned size);
void raid6_sse2r4(unsigned char** vbuf, unsigned data, unsigned size);
void raidTP_int32r2(unsigned char** vbuf, unsigned data, unsigned size);
void raidTP_int64r2(unsigned char** vbuf, unsigned data, unsigned size);
void raidTP_mmxr1(unsigned char** vbuf, unsigned data, unsigned size);
void raidTP_sse2r1(unsigned char** vbuf, unsigned data, unsigned size);
void raidTP_sse2r2(unsigned char** vbuf, unsigned data, unsigned size);
void raidQP_int32r2(unsigned char** vbuf, unsigned data, unsigned size);
void raidQP_int64r2(unsigned char** vbuf, unsigned data, unsigned size);
void raidQP_mmxr1(unsigned char** vbuf, unsigned data, unsigned size);
void raidQP_sse2r1(unsigned char** vbuf, unsigned data, unsigned size);
void raidQP_sse2r2(unsigned char** vbuf, unsigned data, unsigned size);

#endif

