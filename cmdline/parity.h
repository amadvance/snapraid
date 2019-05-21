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

#ifndef __PARITY_H
#define __PARITY_H

#include "support.h"

/****************************************************************************/
/* parity */

struct snapraid_split_handle {
	char path[PATH_MAX]; /**< Path of the file. */
	int f; /**< Handle of the files. */
	struct stat st; /**< Stat info of the opened file. */
	struct advise_struct advise; /**< Advise information. */

	/**
	 * Size of the parity split.
	 * Only the latest not zero size is allowed to grow.
	 * Note that this value CANNOT be PARITY_SIZE_INVALID.
	 */
	data_off_t size;

	/**
	 * Valid size of the parity split.
	 * This is the size effectively written, and not the result of a chsize operation.
	 * It's used to make read operations failing if read over that size.
	 *
	 * Parity is also truncated to that size when fixing it, in case of a Break (Ctrl+C)
	 * of the program.
	 */
	data_off_t valid_size;

	/**
	 * Artificial size limit for testing.
	 * 0 means unlimited.
	 */
	data_off_t limit_size;
};

struct snapraid_parity_handle {
	struct snapraid_split_handle split_map[SPLIT_MAX];
	unsigned split_mac; /**< Number of parity splits. */
	unsigned level; /**< Level of the parity. */
};

/**
 * Compute the size of the allocated parity data in number of blocks.
 *
 * This includes parity blocks not yet written and still invalid.
 */
block_off_t parity_allocated_size(struct snapraid_state* state);

/**
 * Compute the size of the used parity data in number of blocks.
 *
 * This includes only parity blocks used for files, not counting
 * potential invalid parity at the end.
 *
 * If the array is fully synced there is no difference between
 * parity_allocate_size() and parity_used_size().
 * But if the sync is interrupted, the parity_used_size() returns
 * the position of the latest BLK block, ignoring CHG, REL and DELETED ones,
 * because their parity may be still not even written in the parity file.
 */
block_off_t parity_used_size(struct snapraid_state* state);

/**
 * Check if the parity needs to be updated with a "sync".
 *
 * This is the same logic used in "status" to detect an incomplete "sync",
 * that ignores invalid block, if they are not used by a file in any disk.
 * This means that DELETED blocks won't necessarily imply an invalid parity.
 */
int parity_is_invalid(struct snapraid_state* state);

/**
 * Report all the files outside the specified parity size.
 */
void parity_overflow(struct snapraid_state* state, data_off_t size);

/**
 * Create the parity file.
 * \param out_size Return the size of the parity file.
 */
int parity_create(struct snapraid_parity_handle* handle, const struct snapraid_parity* parity, unsigned level, int mode, uint32_t block_size, data_off_t limit_size);

/**
 * Change the parity size.
 * \param out_size Return the size of the parity file. The out_size is set also on error to reflect a partial resize.
 */
int parity_chsize(struct snapraid_parity_handle* handle, struct snapraid_parity* parity, int* is_modified, data_off_t size, uint32_t block_size, int skip_fallocate, int skip_space_holder);

/**
 * Get the size of the parity.
 *
 * This returns the cached/expected version of the split sizes, and not the real file size.
 */
void parity_size(struct snapraid_parity_handle* handle, data_off_t* out_size);

/**
 * Open an already existing parity file.
 */
int parity_open(struct snapraid_parity_handle* handle, const struct snapraid_parity* parity, unsigned level, int mode, uint32_t block_size, data_off_t limit_size);

/**
 * Flush the parity file in the disk.
 */
int parity_sync(struct snapraid_parity_handle* handle);

/**
 * Truncate the parity file to the valid size.
 */
int parity_truncate(struct snapraid_parity_handle* handle);

/**
 * Close the parity file.
 */
int parity_close(struct snapraid_parity_handle* handle);

/**
 * Read a block from the parity file.
 */
int parity_read(struct snapraid_parity_handle* handle, block_off_t pos, unsigned char* block_buffer, unsigned block_size, fptr* out);

/**
 * Write a block in the parity file.
 */
int parity_write(struct snapraid_parity_handle* handle, block_off_t pos, unsigned char* block_buffer, unsigned block_size);

#endif

