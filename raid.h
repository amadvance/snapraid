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
 * It requires SSSE3 to get good performance with triple or more 
 * parities.
 *
 * This is the default mode used by SnapRAID.
 */
#define RAID_MODE_CAUCHY 0

/**
 * RAID mode supporting up to 3 parities,
 *
 * It has a fast triple parity implementation even without SSSE3, but it
 * cannot go beyond it.
 * This is mostly intended for low end CPUs like ARM and AMD Athlon.
 */
#define RAID_MODE_VANDERMONDE 1

/**
 * Max level of parity supported for RAID_MODE_CAUCHY.
 */
#define RAID_PARITY_CAUCHY_MAX 6

/**
 * Max level of parity supported for RAID_MODE_VANDERMONDE.
 */
#define RAID_PARITY_VANDERMONDE_MAX 3

/**
 * Maximum number of data disks.
 * Limit of the parity generator matrix used.
 */
#define RAID_DATA_MAX 251

/**
 * Initializes the RAID system.
 */
void raid_init(void);

/**
 * Set the RAID mode to use. One of RAID_MODE_*.
 *
 * You can change mode at any time, and it will affect next calls to raid_par() and raid_rec().
 *
 * The two modes are compatible for the first two levels of parity. The third one is different.
 */
void raid_set(unsigned mode);

/**
 * Computes the parity.
 *
 * \param np Number of parities to compute.
 * \param nd Number of data disks.
 * \param size Size of the blocks pointed by vbuf. It must be a power of 2, and at least 64.
 * \param vbuf Vector of pointers to the blocks for disks and parities.
 * It has (::nd + ::np) elements. Each element points to a buffer of ::size bytes. 
 */
void raid_par(unsigned np, unsigned nd, unsigned size, unsigned char** vbuf);

/**
 * Recovers failures of data disks using the specified parities.
 *
 * Note that only the data blocks are recovered. The parity is destroyed.
 * \param nr Number of failed disks to recover.
 * \param id[] Vector of ::nr indexes of the data disks to recover. Starting from 0.
 * \param ip[] Vector of ::nr indexes of the parity to use for the recovering. Starting from 0.
 * \param nd Number of data disks.
 * \param size Size of the blocks pointed by vbuf. It must be a power of 2, and at least 64.
 * \param vbuf Vector of pointers to the blocks for disks and parities.
 * It has (::nd + max(::ip[] + 1)) elements. Each element points to a buffer of ::size bytes.
 * \param zero Buffer filled with 0 of ::size bytes. This buffer is not modified. 
 */
void raid_rec(unsigned nr, const int* id, const int* ip, unsigned nd, unsigned size, unsigned char** vbuf, unsigned char* zero);

/**
 * Gets the name of the selected function to compute the parity.
 * Only for testing.
 */
const char* raid_par1_tag(void);
const char* raid_par2_tag(void);
const char* raid_parz_tag(void);
const char* raid_par3_tag(void);
const char* raid_par4_tag(void);
const char* raid_par5_tag(void);
const char* raid_par6_tag(void);
const char* raid_rec1_tag(void);
const char* raid_rec2_tag(void);
const char* raid_recX_tag(void);

/**
 * Specialized parity computation.
 * Only for testing.
 */
void raid_par1_int32(unsigned nd, unsigned size, unsigned char** vbuf);
void raid_par1_int64(unsigned nd, unsigned size, unsigned char** vbuf);
void raid_par1_sse2(unsigned nd, unsigned size, unsigned char** vbuf);
void raid_par2_int32(unsigned nd, unsigned size, unsigned char** vbuf);
void raid_par2_int64(unsigned nd, unsigned size, unsigned char** vbuf);
void raid_par2_sse2(unsigned nd, unsigned size, unsigned char** vbuf);
void raid_par2_sse2ext(unsigned nd, unsigned size, unsigned char** vbuf);
void raid_parz_int32(unsigned nd, unsigned size, unsigned char** vbuf);
void raid_parz_int64(unsigned nd, unsigned size, unsigned char** vbuf);
void raid_parz_sse2(unsigned nd, unsigned size, unsigned char** vbuf);
void raid_parz_sse2ext(unsigned nd, unsigned size, unsigned char** vbuf);
void raid_par3_int8(unsigned nd, unsigned size, unsigned char** vbuf);
void raid_par3_ssse3(unsigned nd, unsigned size, unsigned char** vbuf);
void raid_par3_ssse3ext(unsigned nd, unsigned size, unsigned char** vbuf);
void raid_par4_int8(unsigned nd, unsigned size, unsigned char** vbuf);
void raid_par4_ssse3(unsigned nd, unsigned size, unsigned char** vbuf);
void raid_par4_ssse3ext(unsigned nd, unsigned size, unsigned char** vbuf);
void raid_par5_int8(unsigned nd, unsigned size, unsigned char** vbuf);
void raid_par5_ssse3(unsigned nd, unsigned size, unsigned char** vbuf);
void raid_par5_ssse3ext(unsigned nd, unsigned size, unsigned char** vbuf);
void raid_par6_int8(unsigned nd, unsigned size, unsigned char** vbuf);
void raid_par6_ssse3(unsigned nd, unsigned size, unsigned char** vbuf);
void raid_par6_ssse3ext(unsigned nd, unsigned size, unsigned char** vbuf);
void raid_rec1_int8(unsigned nr, const int* id, const int* ip, unsigned nd, unsigned size, unsigned char** vbuf, unsigned char* zero);
void raid_rec2_int8(unsigned nr, const int* id, const int* ip, unsigned nd, unsigned size, unsigned char** vbuf, unsigned char* zero);
void raid_recX_int8(unsigned nr, const int* id, const int* ip, unsigned nd, unsigned size, unsigned char** vbuf, unsigned char* zero);
void raid_rec1_ssse3(unsigned nr, const int* id, const int* ip, unsigned nd, unsigned size, unsigned char** vbuf, unsigned char* zero);
void raid_rec2_ssse3(unsigned nr, const int* id, const int* ip, unsigned nd, unsigned size, unsigned char** vbuf, unsigned char* zero);
void raid_recX_ssse3(unsigned nr, const int* id, const int* ip, unsigned nd, unsigned size, unsigned char** vbuf, unsigned char* zero);

#endif
