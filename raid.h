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

#ifndef __RAID_H
#define __RAID_H

/****************************************************************************/
/* raid */

/**
 * Maximum number of data disks.
 * Limit of the parity generator matrix used.
 */
#define RAID_DATA_MAX 251

/**
 * Minimum number of data disks.
 * Limit of some optimized parity generator functions.
 */
#define RAID_DATA_MIN 2

/**
 * Initializes the RAID system.
 */
void raid_init(void);

/**
 * Computes the parity.
 * \param level Number of parities to compute.
 * \param vbuf Vector of pointers to the blocks for disks and parities.
 * It has (::data + ::level) elements. Each element points to a buffer of ::size bytes.
 * \param data Number of data disks.
 * \param size Size of the blocks pointed by vbuf.
 */
void raid_gen(unsigned level, unsigned char** vbuf, unsigned data, unsigned size);

/**
 * Recovers failures of data disks using the specified parities.
 * Note that only the data blocks are recovered. The parity is destroyed.
 * \param level Number of failed disks.
 * \param d[] Vector of indexes of the data disks to recover. Starting from 0.
 * \param c[] Vector of indexes of the checksum/parity to use for the recovering. Starting from 0.
 * \param vbuf Vector of pointers to the blocks for disks and parities.
 * It has (::data + max(::c[] + 1)) elements. Each element points to a buffer of ::size bytes.
 * \param data Number of data disks.
 * \param zero Buffer filled with 0 of ::size bytes. This buffer is not modified.
 * \param size Size of the blocks pointed by vbuf.
 */
void raid_recov(unsigned level, const int* d, const int* c, unsigned char** vbuf, unsigned data, unsigned char* zero, unsigned size);

/**
 * Gets the name of the selected function to compute parity.
 * Only for testing.
 */
const char* raid5_tag(void);
const char* raid6_tag(void);
const char* raidTP_tag(void);
const char* raidQP_tag(void);
const char* raidPP_tag(void);
const char* raidHP_tag(void);

/**
 * Specialized parity computation.
 * Only for testing.
 */
void raid5_int32(unsigned char** vbuf, unsigned data, unsigned size);
void raid5_int64(unsigned char** vbuf, unsigned data, unsigned size);
void raid5_sse2(unsigned char** vbuf, unsigned data, unsigned size);
void raid6_int32(unsigned char** vbuf, unsigned data, unsigned size);
void raid6_int64(unsigned char** vbuf, unsigned data, unsigned size);
void raid6_sse2(unsigned char** vbuf, unsigned data, unsigned size);
void raid6_sse2ext(unsigned char** vbuf, unsigned data, unsigned size);
void raidTP_int8(unsigned char** vbuf, unsigned data, unsigned size);
void raidTP_ssse3(unsigned char** vbuf, unsigned data, unsigned size);
void raidTP_ssse3ext(unsigned char** vbuf, unsigned data, unsigned size);
void raidQP_int8(unsigned char** vbuf, unsigned data, unsigned size);
void raidQP_ssse3(unsigned char** vbuf, unsigned data, unsigned size);
void raidQP_ssse3ext(unsigned char** vbuf, unsigned data, unsigned size);
void raidPP_int8(unsigned char** vbuf, unsigned data, unsigned size);
void raidPP_ssse3(unsigned char** vbuf, unsigned data, unsigned size);
void raidPP_ssse3ext(unsigned char** vbuf, unsigned data, unsigned size);
void raidHP_int8(unsigned char** vbuf, unsigned data, unsigned size);
void raidHP_ssse3(unsigned char** vbuf, unsigned data, unsigned size);
void raidHP_ssse3ext(unsigned char** vbuf, unsigned data, unsigned size);

#endif

