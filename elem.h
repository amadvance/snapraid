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
	uint64_t device; /**< Device identifier. */
	tommy_node node; /**< Next node in the list. */
};

/**
 * Filter for paths.
 */
struct snapraid_filter {
	char pattern[PATH_MAX]; /**< Filter pattern. */
	int is_disk; /**< If the pattern is a disk one. */
	int is_path; /**< If the pattern is only for the complete path. */
	int is_dir; /**< If the pattern is only for dir. */
	int direction; /**< If it's an inclusion (=1) or an exclusion (=-1). */
	tommy_node node; /**< Next node in the list. */
};

struct snapraid_file;

/**
 * The block has both the hash and the parity computed.
 * This is the normal state of a saved block.
 * Note that if exists at least one BLOCK_STATE_BLK in a disk, all the other
 * blocks at the same address in other disks can be only BLOCK_EMPTY or BLOCK_STATE_BLK.
 *
 * The block hash field IS set.
 * The parity for this disk is updated.
 */
#define BLOCK_STATE_BLK 1

/**
 * The block is new and not yet hashed, and it's using a space previously empty.
 *
 * The block hash field IS NOT set.
 * The parity for this disk is not updated, but it contains 0.
 */
#define BLOCK_STATE_NEW 2

/**
 * The block has the hash computed, but the parity is invalid.
 * This happens when the parity is invalidated by a change in another disk.
 *
 * The block hash field IS set.
 * The parity for this disk is updated, but there is at least another disk not updated parity.
 */
#define BLOCK_STATE_INV 3

/**
 * The block is new and not yet hashed, and it's using the space of a block not existing anymore.
 * This happens when a new block overwrite a just removed block.
 *
 * The block hash field IS set, and it represents the hash of the previous data.
 * The parity for this disk is not updated, but it contains the old data referenced by the hash.
 *
 * If the hash is completely filled with 0, it means that its lost.
 */
#define BLOCK_STATE_CHG 4

/**
 * This block is a deleted one.
 * This happens when a file is deleted.
 *
 * The block hash field IS set, and it represents the hash of the previous data.
 * The parity for this disk is not updated, but it contains the old data referenced by the hash.
 *
 * If the hash is completely filled with 0, it means that its lost.
 */
#define BLOCK_STATE_DELETED 5

/**
 * Mask used to store the previous states of the block.
 * Used to mix them inside the file pointer.
 */
#define BLOCK_STATE_MASK 7

/**
 * This block is an empty one.
 * Note that this state cannot be stored inside the block,
 * and it's represented by the block ::BLOCK_EMPTY.
 */
#define BLOCK_STATE_EMPTY 8

/**
 * Block of a file.
 */
struct snapraid_block {
	uintptr_t file_mixed; /**< Back pointer to the file owning this block, mixed with some flags. */
	block_off_t parity_pos; /**< Position of the block in the parity. */
	unsigned char hash[HASH_SIZE]; /**< Hash of the block. */
};

/**
 * Block pointer used to mark unused blocks.
 */
#define BLOCK_EMPTY 0

#define FILE_IS_PRESENT 1 /**< If it's seen as present. */
#define FILE_IS_EXCLUDED 2 /**< If it's an excluded file from the processing. */
#define FILE_IS_LARGER 4 /**< If a larger file was already detected. Just to avoid to report it more times. */
#define FILE_IS_DAMAGED 8 /**< If a fix was attempted but it failed. */
#define FILE_IS_FIXED 16 /**< If a fix was done. */
#define FILE_IS_HARDLINK 32 /**< If it's an hardlink. */

/**
 * Value used to mark files without the nanoseconds value.
 * Note that this value is never reported by stat().
 */
#define FILE_MTIME_NSEC_INVALID -1

/**
 * File.
 */
struct snapraid_file {
	int64_t mtime_sec; /**< Modification time. */
	uint64_t inode; /**< Inode. */
	data_off_t size; /**< Size of the file. */
	struct snapraid_block* blockvec; /**< All the blocks of the file. */
	int mtime_nsec; /**< Modification time nanoseconds. In the range 0 <= x < 1,000,000,000, or FILE_MTIME_NSEC_INVALID. */
	block_off_t blockmax; /**< Number of blocks. */
	unsigned flag; /**< FILE_IS_* flags. */
	char sub[PATH_MAX]; /**< Sub path of the file. Without the disk dir. The disk is implicit. */

	/* nodes for data structures */
	tommy_node nodelist;
	tommy_hashdyn_node nodeset;
	tommy_hashdyn_node pathset;
};

/**
 * Symbolic Link.
 */
struct snapraid_link {
	unsigned flag; /**< FILE_IS_* flags. */
	char sub[PATH_MAX]; /**< Sub path of the file. Without the disk dir. The disk is implicit. */
	char linkto[PATH_MAX]; /**< Link to. */

	/* nodes for data structures */
	tommy_node nodelist;
	tommy_hashdyn_node nodeset;
}; 

/**
 * Dir.
 */
struct snapraid_dir {
	unsigned flag; /**< FILE_IS_* flags. */
	char sub[PATH_MAX]; /**< Sub path of the file. Without the disk dir. The disk is implicit. */

	/* nodes for data structures */
	tommy_node nodelist;
	tommy_hashdyn_node nodeset;
}; 

/**
 * Deleted entry.
 */
struct snapraid_deleted {
	/**
	 * Deleted block.
	 * This block is always in state BLOCK_STATE_DELETED,
	 * and it's used to keep the old hash during a sync.
	 */
	struct snapraid_block block;

	/* nodes for data structures */
	tommy_node node;
};

/**
 * Disk.
 */
struct snapraid_disk {
	char name[PATH_MAX]; /**< Name of the disk. */
	char dir[PATH_MAX]; /**< Mount point of the disk. It always terminates with /. */
	uint64_t device; /**< Device identifier. */
	block_off_t first_free_block; /**< First free searching block. */

	/**<
	 * Block array of the disk.
	 *
	 * Each element points to a snapraid_block structure, or it's BLOCK_EMPTY.
	 */
	tommy_array blockarr;

	/**
	 * List of all the snapraid_file for the disk.
	 */
	tommy_list filelist;

	/**
	 * List of all the snapraid_deleted blocks for the disk.
	 *
	 * These files are kept allocated, because the blocks are still referenced in
	 * the ::blockarr.
	 */
	tommy_list deletedlist;

	tommy_hashdyn inodeset; /**< Hashtable by inode of all the files. */
	tommy_hashdyn pathset; /**< Hashtable by path of all the files. */
	tommy_list linklist; /**< List of all the links. */
	tommy_hashdyn linkset; /**< Hashtable by name of all the links. */
	tommy_list dirlist; /**< List of all the dirs. */
	tommy_hashdyn dirset; /**< Hashtable by name of all the dirs. */

	/* nodes for data structures */
	tommy_node node;
};

/**
 * Disk mapping.
 */
struct snapraid_map {
	char name[PATH_MAX]; /**< Name of the disk. */
	unsigned position; /**< Position of the disk in the parity. */

	/* nodes for data structures */
	tommy_node node;
};

/**
 * Allocates a content.
 */
struct snapraid_content* content_alloc(const char* path, uint64_t dev);

/**
 * Deallocates a content.
 */
void content_free(struct snapraid_content* content);

/**
 * Allocates a filter pattern for files and directories.
 */
struct snapraid_filter* filter_alloc_file(int is_include, const char* pattern);

/**
 * Allocates a filter pattern for disks.
 */
struct snapraid_filter* filter_alloc_disk(int is_include, const char* pattern);

/**
 * Deallocates an exclusion.
 */
void filter_free(struct snapraid_filter* filter);

/**
 * Filter hidden files.
 * Return !=0 if it matches and it should be excluded.
 */
static inline int filter_hidden(int enable, struct dirent* dd)
{
	if (enable && dirent_hidden(dd)) {
		return 1; /* filter out */
	}

	return 0;
}

/**
 * Filters a path using a list of filters.
 * For each element of the path all the filters are applied, until the first one that matches.
 * Return !=0 if it matches and it should be excluded.
 */
int filter_path(tommy_list* filterlist, const char* disk, const char* sub);

/**
 * Filter a file/link/dir if missing.
 * Return !=0 if it matches and it should be excluded.
 */
int filter_existence(int filter_missing, unsigned flag);

/**
 * Filters a dir using a list of filters.
 * For each element of the path all the filters are applied, until the first one that matches.
 * Return !=0 if it matches and it should be excluded.
 */
int filter_dir(tommy_list* filterlist, const char* disk, const char* sub);

/**
 * Filters a path if it's a content file.
 * Return !=0 if it matches and it should be excluded.
 */
int filter_content(tommy_list* contentlist, const char* path);

/**
 * Gets the file containing the block.
 */
static inline struct snapraid_file* block_file_get(struct snapraid_block* block)
{
	return (struct snapraid_file*)(block->file_mixed & ~(uintptr_t)BLOCK_STATE_MASK);
}

/**
 * Sets the file containing the block.
 */
static inline void block_file_set(struct snapraid_block* block, struct snapraid_file* file)
{
	uintptr_t ptr = (uintptr_t)file;

	/* ensure that the pointer doesn't use the flag space */
	if ((ptr & (uintptr_t)BLOCK_STATE_MASK) != 0) {
		fprintf(stderr, "Internal error for pointer not aligned\n");
		exit(EXIT_FAILURE);
	}

	block->file_mixed = (block->file_mixed & ~(uintptr_t)BLOCK_STATE_MASK) | ptr;
}

/**
 * Get the state of the block.
 *
 * For this function, it's allowed to pass a NULL block
 * pointer than results in the BLOCK_STATE_EMPTY state.
 */
static inline unsigned block_state_get(const struct snapraid_block* block)
{
	if (block == BLOCK_EMPTY)
		return BLOCK_STATE_EMPTY;

	return block->file_mixed & BLOCK_STATE_MASK;
}

static inline void block_state_set(struct snapraid_block* block, unsigned state)
{
	/* ensure that the state can be stored inside the file pointer */
	if ((state & BLOCK_STATE_MASK) != state) {
		fprintf(stderr, "Internal error when setting the block state %u\n", state);
		exit(EXIT_FAILURE);
	}

	block->file_mixed &= ~(uintptr_t)BLOCK_STATE_MASK;
	block->file_mixed |= state & BLOCK_STATE_MASK;
}

static inline void block_clear_parity(struct snapraid_block* block)
{
	unsigned state = block_state_get(block);

	if (state == BLOCK_STATE_BLK) {
		block_state_set(block, BLOCK_STATE_INV);
	}
}

/**
 * Check if the specified block has a valid and updated hash.
 *
 * Note that blocks with hash of old data, like CHG and DELETED ones, are not considered.
 */
static inline int block_has_hash(const struct snapraid_block* block)
{
	unsigned state = block_state_get(block);

	return state == BLOCK_STATE_BLK || state == BLOCK_STATE_INV;
}

/**
 * Check if the specified block has a valid file.
 *
 * It includes BLK/INV/NEW/CHG and excludes EMPTY/DELETED.
 */
static inline int block_has_file(const struct snapraid_block* block)
{
	unsigned state = block_state_get(block);

	return state == BLOCK_STATE_BLK || state == BLOCK_STATE_INV || state == BLOCK_STATE_NEW || state == BLOCK_STATE_CHG;
}

/**
 * Gets the relative position of a block inside the file.
 */
block_off_t block_file_pos(struct snapraid_block* block);

/**
 * Checks if the block is the last in the file.
 */
int block_is_last(struct snapraid_block* block);

/**
 * Gets the size in bytes of the block.
 * If it's the last block of a file it could be less than block_size.
 */
unsigned block_file_size(struct snapraid_block* block, unsigned block_size);

/**
 * Allocates a deleted block.
 */
struct snapraid_deleted* deleted_alloc(void);

/**
 * Allocates a deleted block from a real one.
 */
struct snapraid_deleted* deleted_dup(struct snapraid_block* block);

/**
 * Frees  a deleted block.
 */
void deleted_free(struct snapraid_deleted* deleted);

static inline int file_flag_has(const struct snapraid_file* file, unsigned mask)
{
	return (file->flag & mask) == mask;
}

static inline void file_flag_set(struct snapraid_file* file, unsigned mask)
{
	file->flag |= mask;
}

static inline void file_flag_clear(struct snapraid_file* file, unsigned mask)
{
	file->flag &= ~mask;
}

/**
 * Allocates a file.
 */
struct snapraid_file* file_alloc(unsigned block_size, const char* sub, uint64_t size, uint64_t mtime_sec, int mtime_nsec, uint64_t inode);

/**
 * Deallocates a file.
 */
void file_free(struct snapraid_file* file);

/**
 * Returns the name of the file, without the dir.
 */
const char* file_name(struct snapraid_file* file);

/**
 * Compares a file with an inode.
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
 * Compares a file with a path.
 */
int file_path_compare(const void* void_arg, const void* void_data);

/**
 * Computes the hash of a file path.
 */
static inline tommy_uint32_t file_path_hash(const char* sub)
{
	return tommy_hash_u32(0, sub, strlen(sub));
}

static inline int link_flag_has(const struct snapraid_link* link, unsigned mask)
{
	return (link->flag & mask) == mask;
}

static inline void link_flag_set(struct snapraid_link* link, unsigned mask)
{
	link->flag |= mask;
}

static inline void link_flag_clear(struct snapraid_link* link, unsigned mask)
{
	link->flag &= ~mask;
}

/**
 * Checks if the link is a hardlink. Otherwise it's a symbolic link.
 */
static inline int link_is_hardlink(struct snapraid_link* link)
{
	return link_flag_has(link, FILE_IS_HARDLINK);
}

/**
 * Allocates a link.
 */
struct snapraid_link* link_alloc(const char* name, const char* link, int is_hardlink);

/**
 * Deallocates a link.
 */
void link_free(struct snapraid_link* link);

/**
 * Compare a link with a name.
 */
int link_name_compare(const void* void_arg, const void* void_data);

/**
 * Computes the hash of a link name.
 */
static inline tommy_uint32_t link_name_hash(const char* name)
{
	return tommy_hash_u32(0, name, strlen(name));
}

static inline int dir_flag_has(const struct snapraid_dir* dir, unsigned mask)
{
	return (dir->flag & mask) == mask;
}

static inline void dir_flag_set(struct snapraid_dir* dir, unsigned mask)
{
	dir->flag |= mask;
}

static inline void dir_flag_clear(struct snapraid_dir* dir, unsigned mask)
{
	dir->flag &= ~mask;
}

/**
 * Allocates a dir.
 */
struct snapraid_dir* dir_alloc(const char* name);

/**
 * Deallocates a dir.
 */
void dir_free(struct snapraid_dir* dir);

/**
 * Compare a dir with a name.
 */
int dir_name_compare(const void* void_arg, const void* void_data);

/**
 * Computes the hash of a dir name.
 */
static inline tommy_uint32_t dir_name_hash(const char* name)
{
	return tommy_hash_u32(0, name, strlen(name));
}

/**
 * Allocates a disk.
 */
struct snapraid_disk* disk_alloc(const char* name, const char* dir, uint64_t dev);

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
		return BLOCK_EMPTY;
}

/**
 * Allocates a disk mapping.
 */
struct snapraid_map* map_alloc(const char* name, unsigned position);

/**
 * Deallocates a disk mapping.
 */
void map_free(struct snapraid_map* map);

#endif

