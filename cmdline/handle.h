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

#ifndef __HANDLE_H
#define __HANDLE_H

#include "state.h"
#include "support.h"

/****************************************************************************/
/* handle */

struct snapraid_handle {
	char path[PATH_MAX]; /**< Path of the file. */
	struct snapraid_disk* disk; /**< Disk of the file. */
	struct snapraid_file* file; /**< File opened. When the file is closed, it's set to 0. */
	int f; /**< Handle of the file. */
	struct stat st; /**< Stat info of the opened file. */
	struct advise_struct advise; /**< Advise information. */
	data_off_t valid_size; /**< Size of the valid data. */
	int created; /**< If the file was created, otherwise it was already existing. */
};

/**
 * Create a file.
 * The file is created if missing, and opened with write access.
 * If the file is created, the handle->created is set.
 * The initial size of the file is stored in the file->st struct.
 * If the file cannot be opened for write access, it's opened with read-only access.
 * The read-only access works only if the file has already the correct size and doesn't need to be modified.
 */
int handle_create(struct snapraid_handle* handle, struct snapraid_file* file, int mode);

/**
 * Truncate a file if required.
 */
int handle_truncate(struct snapraid_handle* handle, struct snapraid_file* file);

/**
 * Open a file.
 * The file is opened for reading.
 */
int handle_open(struct snapraid_handle* handle, struct snapraid_file* file, int mode, fptr* out, fptr* out_missing);

/**
 * Close a file.
 */
int handle_close(struct snapraid_handle* handle);

/**
 * Read a block from a file.
 * If the read block is shorter, it's padded with 0.
 */
int handle_read(struct snapraid_handle* handle, block_off_t file_pos, unsigned char* block_buffer, unsigned block_size, fptr* out, fptr* out_missing);

/**
 * Write a block to a file.
 */
int handle_write(struct snapraid_handle* handle, block_off_t file_pos, unsigned char* block_buffer, unsigned block_size);

/**
 * Change the modification time of the file to the saved value.
 */
int handle_utime(struct snapraid_handle* handle);

/**
 * Map the unsorted list of disk to an ordered vector.
 * \param diskmax The size of the vector.
 * \return The allocated vector of pointers.
 */
struct snapraid_handle* handle_mapping(struct snapraid_state* state, unsigned* diskmax);

#endif

