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
 * RAID mode supporting up to 6 parities.
 *
 * It requires SSSE3 to get good performance with triple or more parities.
 *
 * This is the default mode used by SnapRAID.
 */
#define RAID_MODE_S 0

/**
 * RAID mode supporting up to 3 parities,
 *
 * It has a fast implementation not requiring SSSE3 for triple parity.
 * This is mostly intended for low end CPUs like ARM and AMD Athlon II.
 *
 * This is the mode used by ZFS.
 */
#define RAID_MODE_Z 1

/**
 * Max level of parity supported for mode S.
 */
#define RAID_PARITY_S_MAX 6

/**
 * Max level of parity supported for mode Z.
 */
#define RAID_PARITY_Z_MAX 3

/**
 * Maximum number of data disks.
 * Limit of the parity generator matrix used.
 */
#define RAID_DATA_MAX 251

/**
 * Minimum number of data disks.
 */
#define RAID_DATA_MIN 2

/**
 * Initializes the RAID system.
 */
void raid_init(void);

/**
 * Set the RAID mode to use. One of RAID_MODE_*.
 *
 * You can change mode at any time, and it will affect next calls to raid_gen() and raid_recov().
 *
 * The two modes are compatible for the first two levels of parity. The third one is different.
 */
void raid_set(unsigned mode);

/**
 * Computes the parity.
 *
 * \param level Number of parities to compute.
 * \param vbuf Vector of pointers to the blocks for disks and parities.
 * It has (::data + ::level) elements. Each element points to a buffer of ::size bytes.
 * \param data Number of data disks.
 * \param size Size of the blocks pointed by vbuf.
 */
void raid_gen(unsigned level, unsigned char** vbuf, unsigned data, unsigned size);

/**
 * Recovers failures of data disks using the specified parities.
 *
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
const char* raidZ3_tag(void);
const char* raidS3_tag(void);
const char* raidS4_tag(void);
const char* raidS5_tag(void);
const char* raidS6_tag(void);
const char* raid_recov1_tag(void);
const char* raid_recov2_tag(void);
const char* raid_recovX_tag(void);

/**
 * Specialized parity computation.
 *
 * Only for testing.
 */
void raid5_int32(unsigned char** vbuf, unsigned data, unsigned size);
void raid5_int64(unsigned char** vbuf, unsigned data, unsigned size);
void raid5_sse2(unsigned char** vbuf, unsigned data, unsigned size);
void raid6_int32(unsigned char** vbuf, unsigned data, unsigned size);
void raid6_int64(unsigned char** vbuf, unsigned data, unsigned size);
void raid6_sse2(unsigned char** vbuf, unsigned data, unsigned size);
void raid6_sse2ext(unsigned char** vbuf, unsigned data, unsigned size);
void raidZ3_int32(unsigned char** vbuf, unsigned data, unsigned size);
void raidZ3_int64(unsigned char** vbuf, unsigned data, unsigned size);
void raidZ3_sse2(unsigned char** vbuf, unsigned data, unsigned size);
void raidZ3_sse2ext(unsigned char** vbuf, unsigned data, unsigned size);
void raidS3_int8(unsigned char** vbuf, unsigned data, unsigned size);
void raidS3_ssse3(unsigned char** vbuf, unsigned data, unsigned size);
void raidS3_ssse3ext(unsigned char** vbuf, unsigned data, unsigned size);
void raidS4_int8(unsigned char** vbuf, unsigned data, unsigned size);
void raidS4_ssse3(unsigned char** vbuf, unsigned data, unsigned size);
void raidS4_ssse3ext(unsigned char** vbuf, unsigned data, unsigned size);
void raidS5_int8(unsigned char** vbuf, unsigned data, unsigned size);
void raidS5_ssse3(unsigned char** vbuf, unsigned data, unsigned size);
void raidS5_ssse3ext(unsigned char** vbuf, unsigned data, unsigned size);
void raidS6_int8(unsigned char** vbuf, unsigned data, unsigned size);
void raidS6_ssse3(unsigned char** vbuf, unsigned data, unsigned size);
void raidS6_ssse3ext(unsigned char** vbuf, unsigned data, unsigned size);
void raid_recov1_int8(unsigned level, const int* d, const int* c, unsigned char** vbuf, unsigned data, unsigned char* zero, unsigned size);
void raid_recov2_int8(unsigned level, const int* d, const int* c, unsigned char** vbuf, unsigned data, unsigned char* zero, unsigned size);
void raid_recovX_int8(unsigned level, const int* d, const int* c, unsigned char** vbuf, unsigned data, unsigned char* zero, unsigned size);
void raid_recov1_ssse3(unsigned level, const int* d, const int* c, unsigned char** vbuf, unsigned data, unsigned char* zero, unsigned size);
void raid_recov2_ssse3(unsigned level, const int* d, const int* c, unsigned char** vbuf, unsigned data, unsigned char* zero, unsigned size);
void raid_recovX_ssse3(unsigned level, const int* d, const int* c, unsigned char** vbuf, unsigned data, unsigned char* zero, unsigned size);

#endif

