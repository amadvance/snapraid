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

#ifndef __ELEM_H
#define __ELEM_H

#include "md5.h"
#include "tommylist.h"
#include "tommyhash.h"
#include "tommyhashdyn.h"
#include "tommyarray.h"

/****************************************************************************/
/* snapraid */

/**
 * Size of the hash used as a checksum.
 */
#define HASH_MAX MD5_SIZE

/**
 * Invalid position.
 */
#define POS_INVALID -1

/**
 * Basic position type.
 * With 32 bits and 128k blocks you can address 256 TB.
 */
typedef uint32_t pos_t;

struct snapraid_file;

/**
 * Block of a file.
 */
struct snapraid_block {
	pos_t parity_pos; /**< Position of the block in the parity. */
	int is_hashed; /**< If the hash of the block is valid. */
	struct snapraid_file* file; /**< Back pointer to the file owning this block. */
	unsigned char hash[HASH_MAX]; /**< Hash of the block. */
};

/**
 * File.
 */
struct snapraid_file {
	char sub[PATH_MAX]; /**< Sub path of the file. Without the disk dir. The disk is implicit. */
	uint64_t size; /**< Size of the file. */
	struct snapraid_block* blockvec; /**< All the blocks of the file. */
	pos_t blockmax; /**< Number of blocks. */
	time_t mtime; /**< Modification time. */
	int is_present; /**< If it's seen as present. */

	/* nodes for data structures */
	tommy_node nodelist;
	tommy_hashdyn_node nodeset;
};

/**
 * Disk.
 */
struct snapraid_disk {
	char name[PATH_MAX]; /**< Name of the disk. */
	char dir[PATH_MAX]; /**< Mount point of the disk. */
	pos_t first_free_block; /**< First free searching block. */
	tommy_list filelist; /**< List of all the files. */
	tommy_hashdyn fileset; /**< Hashtable by sub of all the files. */
	tommy_array blockarr; /**< Block array of the disk. */
};

/**
 * Get the relative position of a block inside the file.
 */
pos_t block_file_pos(struct snapraid_block* block);

/**
 * Get the size in bytes of the block.
 * If it's the last block of a file it could less than block_size.
 */
unsigned block_file_size(struct snapraid_block* block, unsigned block_size);

struct snapraid_file* file_alloc(unsigned block_size, const char* sub, uint64_t size, time_t mtime);

void file_free(struct snapraid_file* file);

int file_compare(const void* void_arg, const void* void_data);

static inline tommy_uint32_t file_hash(const char* sub)
{
	return tommy_hash_u32(0, sub, strlen(sub));
}

struct snapraid_disk* disk_alloc(const char* name, const char* dir);

void disk_free(struct snapraid_disk* disk);

static inline struct snapraid_block* disk_block_get(struct snapraid_disk* disk, pos_t pos)
{
	if (pos < tommy_array_size(&disk->blockarr))
		return tommy_array_get(&disk->blockarr, pos);
	else
		return 0;
}

#endif

