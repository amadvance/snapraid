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

/**
 * Computes the size of the parity data in number of blocks.
 */
block_off_t parity_resize(struct snapraid_state* state);

/**
 * Creates the parity file.
 */
int parity_create(const char* path, data_off_t size);

/**
 * Opens an already existing parity file.
 */
int parity_open(int ret_on_error, const char* path);

/**
 * Flushes the parity file in the disk.
 */
void parity_sync(const char* path, int f);

/**
 * Closes the parity file.
 */
void parity_close(const char* path, int f);

/**
 * Read a block from the parity file.
 */
int parity_read(int ret_on_error, const char* parity_path, int parity_f, block_off_t pos, unsigned char* block_buffer, unsigned block_size);

/**
 * Writes a block in the parity file.
 */
void parity_write(const char* parity_path, int parity_f, block_off_t pos, unsigned char* block_buffer, unsigned block_size);

#endif

