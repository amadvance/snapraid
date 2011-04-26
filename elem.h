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

#include "util.h"
#include "tommylist.h"
#include "tommyhash.h"
#include "tommyhashdyn.h"
#include "tommyarray.h"

/****************************************************************************/
/* snapraid */

/**
 * Invalid position.
 */
#define POS_INVALID ((block_off_t)-1)

/**
 * Basic block position type.
 * With 32 bits and 128k blocks you can address 256 TB.
 */
typedef uint32_t block_off_t;

/**
 * Basic data position type.
 * It's signed as file size and offset are usually signed.
 */
typedef int64_t data_off_t;

/**
 * Content file specification.
 */
struct snapraid_content {
	char content[PATH_MAX]; /**< Path of the content file. */
	tommy_node node; /**< Next node in the list. */
};

/**
 * Filter for paths.
 */
struct snapraid_filter {
	char pattern[PATH_MAX]; /**< Filter pattern. */
	int is_path; /**< If the pattern is only for the complete path. */
	int is_dir; /**< If the pattern is only for dir. */
	int direction; /**< If it's an inclusion (=1) or an exclusion (=-1). */
	tommy_node node; /**< Next node in the list. */
};

struct snapraid_file;

#define BLOCK_HAS_HASH 1 /**< If the hash value is valid. */
#define BLOCK_HAS_PARITY 2 /**< If the parity block is valid. */

/**
 * Block of a file.
 */
struct snapraid_block {
	block_off_t parity_pos; /**< Position of the block in the parity. */
	unsigned flag; /**< If the hash of the block is valid. */
	struct snapraid_file* file; /**< Back pointer to the file owning this block. */
	unsigned char hash[HASH_SIZE]; /**< Hash of the block. */
};

/**
 * File.
 */
struct snapraid_file {
	char sub[PATH_MAX]; /**< Sub path of the file. Without the disk dir. The disk is implicit. */
	data_off_t size; /**< Size of the file. */
	struct snapraid_block* blockvec; /**< All the blocks of the file. */
	block_off_t blockmax; /**< Number of blocks. */
	int64_t mtime; /**< Modification time. */
	uint64_t inode; /**< Inode. */
	int is_present; /**< If it's seen as present. */
	int is_excluded; /**< If it's an excluded file from the processing. */
	/* nodes for data structures */
	tommy_node nodelist;
	tommy_hashdyn_node nodeset;
};

/**
 * Disk.
 */
struct snapraid_disk {
	char name[PATH_MAX]; /**< Name of the disk. */
	char dir[PATH_MAX]; /**< Mount point of the disk. It always terminates with /. */
	block_off_t first_free_block; /**< First free searching block. */
	tommy_list filelist; /**< List of all the files. */
	tommy_hashdyn inodeset; /**< Hashtable by inode of all the files. */
	tommy_array blockarr; /**< Block array of the disk. */
};

/**
 * Allocates a content.
 */
struct snapraid_content* content_alloc(const char* path);

/**
 * Deallocates a content.
 */
void content_free(struct snapraid_content* content);

/**
 * Allocates an exclusion.
 */
struct snapraid_filter* filter_alloc(int is_include, const char* pattern);

/**
 * Deallocates an exclusion.
 */
void filter_free(struct snapraid_filter* filter);


/**
 * Filters a path.
 * For each element of the path all the filters are applied, until the first one that matches.
 * Returns 0 if the files has to be processed.
 */
int filter_path(tommy_list* filterlist, const char* path, int is_dir);

/**
 * Gets the relative position of a block inside the file.
 */
block_off_t block_file_pos(struct snapraid_block* block);

/**
 * Gets the size in bytes of the block.
 * If it's the last block of a file it could be less than block_size.
 */
unsigned block_file_size(struct snapraid_block* block, unsigned block_size);

/**
 * Allocates a file.
 */
struct snapraid_file* file_alloc(unsigned block_size, const char* sub, uint64_t size, uint64_t mtime, uint64_t inode);

/**
 * Deallocates a file.
 */
void file_free(struct snapraid_file* file);

/**
 * Returns the name of the file, without the dir.
 */
const char* file_name(struct snapraid_file* file);

/**
 * Compares two files by inode.
 */
int file_inode_compare(const void* void_arg, const void* void_data);

/**
 * Computes the hash of a file inode.
 */
static inline tommy_uint32_t file_inode_hash(uint64_t inode)
{
	return (tommy_uint32_t)tommy_inthash_u64(inode);
}

/**
 * Allocates a disk.
 */
struct snapraid_disk* disk_alloc(const char* name, const char* dir);

/**
 * Deallocates a disk.
 */
void disk_free(struct snapraid_disk* disk);

/**
 * Gets a specific block of a disk.
 * Returns 0 if the block is over the end of the disk or not used.
 */
static inline struct snapraid_block* disk_block_get(struct snapraid_disk* disk, block_off_t pos)
{
	if (pos < tommy_array_size(&disk->blockarr))
		return tommy_array_get(&disk->blockarr, pos);
	else
		return 0;
}

#endif

