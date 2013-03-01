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

/****************************************************************************/
/* parity */

struct snapraid_parity {
	char path[PATH_MAX]; /**< Path of the file. */
	int f; /**< Handle of the file. */
	struct stat st; /**< Stat info of the opened file. */
	data_off_t valid_size; /**< Size of the valid data. */
};

/**
 * Computes the size of the parity data in number of blocks.
 */
block_off_t parity_size(struct snapraid_state* state);

/**
 * Reports all the files outside the specified parity size.
 */
void parity_overflow(struct snapraid_state* state, data_off_t size);

/**
 * Creates the parity file.
 * \param out_size Return the size of the parity file.
 */
int parity_create(struct snapraid_parity* parity, const char* path, data_off_t* out_size);

/**
 * Changes the parity size.
 * \param out_size Return the size of the parity file. The out_size is set also on error to reflect a partial resize.
 */
int parity_chsize(struct snapraid_parity* parity, data_off_t size, data_off_t* out_size, int skip_fallocate);

/**
 * Opens an already existing parity file.
 */
int parity_open(struct snapraid_parity* parity, const char* path);

/**
 * Flushes the parity file in the disk.
 */
int parity_sync(struct snapraid_parity* parity);

/**
 * Closes the parity file.
 */
int parity_close(struct snapraid_parity* parity);

/**
 * Read a block from the parity file.
 */
int parity_read(struct snapraid_parity* parity, block_off_t pos, unsigned char* block_buffer, unsigned block_size, FILE* stdout);

/**
 * Writes a block in the parity file.
 */
int parity_write(struct snapraid_parity* parity, block_off_t pos, unsigned char* block_buffer, unsigned block_size);

#endif

