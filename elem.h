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
#include "tommyarrayof.h"

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
 * The block is new and not yet hashed, and it's using the space of a block not existing anymore.
 * This happens when a new block overwrite a just removed block.
 *
 * The block hash field IS set, and it represents the hash of the previous data,
 * but only if it's different by all 0.
 * The parity for this disk is not updated, but it contains the old data referenced by the hash.
 *
 * If the hash is completely filled with 0, it means that the hash is lost.
 * This could happen if a NEW/CHG block is first DELETED,
 * and then reallocated in a CHG one after an interrupted sync.
  * See scan_file_remove() and scan_file_insert() for the exact location where this could happen.
 */
#define BLOCK_STATE_CHG 3

/**
 * This block is a deleted one.
 * This happens when a file is deleted.
 *
 * The block hash field IS set, and it represents the hash of the previous data,
 * but only if it's different by all 0.
 * The parity for this disk is not updated, but it contains the old data referenced by the hash.
 *
 * If the hash is completely filled with 0, it means that the hash is lost.
 * This could happen if a NEW/CHG block is DELETED after an interrupted sync.
 * See scan_file_remove() for the exact location where this could happen.
 */
#define BLOCK_STATE_DELETED 4

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

/**
 * If a file is present in the disk.
 * It's used only in scan to detect present and missing files.
 */
#define FILE_IS_PRESENT 0x01

/**
 * If it's an excluded file from the processing.
 * It's used in both check and fix to mark files to exclude from the processing.
 */
#define FILE_IS_EXCLUDED 0x02

/**
 * If the file was originally larger, and then truncated to the expected size.
 * It's used only in check to avoid to report this condition more than one time,
 * in case the file is opened multiple times.
 * In fix it's not used, because the size is fixed just after the first opening.
 */
#define FILE_IS_LARGER 0x04

/**
 * If a fix was attempted but it failed.
 * It's used only in fix to mark that some data is unrecoverable.
 */
#define FILE_IS_DAMAGED 0x08

/**
 * If a fix was done.
 * It's used only in fix to mark that some data was recovered.
 */
#define FILE_IS_FIXED 0x10

/**
 * If the file was originally missing, and it was created in the fix process.
 * It's used only in fix to mark files recovered from scratch,
 * meaning that they don't have any previous content.
 * This is important because it means that deleting them, you are not going
 * to lose something that cannot be recovered.
 * Note that excluded files won't ever get this flag. 
 */
#define FILE_IS_CREATED 0x20

/**
 * If the file has completed its processing, meaning that it won't be opened anymore.
 * It's used only in fix to mark when we finish processing one file.
 * Note that excluded files won't ever get this flag.
 */
#define FILE_IS_FINISHED 0x40

#define FILE_IS_HARDLINK 0x100 /**< If it's an hardlink. */
#define FILE_IS_SYMLINK 0x200 /**< If it's a file symlink. */
#define FILE_IS_SYMDIR 0x400 /**< If it's a dir symlink for Windows. Not yet supported. */
#define FILE_IS_JUNCTION 0x800 /**< If it's a junction for Windows. Not yet supported. */
#define FILE_IS_LINK_MASK 0xF00 /**< Mask for link type. */

/**
 * File.
 */
struct snapraid_file {
	int64_t mtime_sec; /**< Modification time. */
	uint64_t inode; /**< Inode. */
	uint64_t physical; /**< Physical offset of the file. */
	data_off_t size; /**< Size of the file. */
	struct snapraid_block* blockvec; /**< All the blocks of the file. */
	int mtime_nsec; /**< Modification time nanoseconds. In the range 0 <= x < 1,000,000,000, or STAT_NSEC_INVALID if not present. */
	block_off_t blockmax; /**< Number of blocks. */
	unsigned flag; /**< FILE_IS_* flags. */
	char* sub; /**< Sub path of the file. Without the disk dir. The disk is implicit. */

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
	char* sub; /**< Sub path of the file. Without the disk dir. The disk is implicit. */
	char* linkto; /**< Link to. */

	/* nodes for data structures */
	tommy_node nodelist;
	tommy_hashdyn_node nodeset;
}; 

/**
 * Dir.
 */
struct snapraid_dir {
	unsigned flag; /**< FILE_IS_* flags. */
	char* sub; /**< Sub path of the file. Without the disk dir. The disk is implicit. */

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
 * Max UUID length.
 */
#define UUID_MAX 128

/**
 * Disk mapping.
 */
struct snapraid_map {
	char name[PATH_MAX]; /**< Name of the disk. */
	unsigned position; /**< Position of the disk in the parity. */
	char uuid[UUID_MAX]; /**< UUID of the disk. Empty if unknown. */

	/* nodes for data structures */
	tommy_node node;
};

/**
 * Info.
 */
typedef unsigned snapraid_info;

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
 * Return !=0 if it should be excluded.
 */
int filter_path(tommy_list* filterlist, const char* disk, const char* sub);

/**
 * Filter a file/link/dir if missing.
 * This call imply a disk check for the file presence.
 * Return !=0 if the file is present and it should be excluded.
 */
int filter_existence(int filter_missing, const char* dir, const char* sub);

/**
 * Filter a file if bad.
 * Return !=0 if the file is correct and it should be excluded.
 */
int filter_correctness(int filter_error, tommy_arrayof* infoarr, struct snapraid_file* file);

/**
 * Filters a dir using a list of filters.
 * For each element of the path all the filters are applied, until the first one that matches.
 * Return !=0 if should be excluded.
 */
int filter_dir(tommy_list* filterlist, const char* disk, const char* sub);

/**
 * Filters a path if it's a content file.
 * Return !=0 if should be excluded.
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

/**
 * Checks if the specified block is valid and has an updated hash.
 *
 * Note that EMPTY / CHG / NEW / DELETED return 0.
 */
static inline int block_has_updated_hash(const struct snapraid_block* block)
{
	unsigned state = block_state_get(block);

	return state == BLOCK_STATE_BLK;
}

/**
 * Checks if the specified block has any kind of hash.
 * Even a not updated one.
 *
 * Note that for DELETED / CHG with a NULL hash returns 0,
 * because in these cases the hash is lost.
 */
static inline int block_has_any_hash(const struct snapraid_block* block)
{
	unsigned block_state = block_state_get(block);
	unsigned i;

	switch (block_state) {
	case BLOCK_STATE_BLK :
		return 1;
	case BLOCK_STATE_CHG :
	case BLOCK_STATE_DELETED :
		/* the hash is valid only if different than 0 */
		for(i=0;i<HASH_SIZE;++i)
			if (block->hash[i] != 0)
				return 1;
		break;
	}

	return 0;
}

/**
 * Checks if the specified block is part of a file.
 *
 * Note that EMPTY / DELETED return 0.
 */
static inline int block_has_file(const struct snapraid_block* block)
{
	unsigned state = block_state_get(block);

	return state == BLOCK_STATE_BLK || state == BLOCK_STATE_NEW || state == BLOCK_STATE_CHG;
}

/**
 * Checks if the block has an invalid parity than needs to be updated.
 *
 * Note that EMPTY / BLK return 0.
 */
static inline int block_has_invalid_parity(const struct snapraid_block* block)
{
	unsigned state = block_state_get(block);

	return state == BLOCK_STATE_DELETED || state == BLOCK_STATE_NEW || state == BLOCK_STATE_CHG;
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
 * Frees a deleted block.
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
struct snapraid_file* file_alloc(unsigned block_size, const char* sub, uint64_t size, uint64_t mtime_sec, int mtime_nsec, uint64_t inode, uint64_t physical);

/**
 * Deallocates a file.
 */
void file_free(struct snapraid_file* file);

/**
 * Rename a file.
 */
void file_rename(struct snapraid_file* file, const char* sub);

/**
 * Returns the name of the file, without the dir.
 */
const char* file_name(struct snapraid_file* file);

/**
 * Compares a file with an inode.
 */
int file_inode_compare_to_arg(const void* void_arg, const void* void_data);

/**
 * Compares files by inode.
 */
int file_inode_compare(const void* void_a, const void* void_b);

/**
 * Compares files by path.
 */
int file_alpha_compare(const void* void_a, const void* void_b);

/**
 * Compares files by physical address.
 */
int file_physical_compare(const void* void_a, const void* void_b);

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

static inline void link_flag_let(struct snapraid_link* link, unsigned flag, unsigned mask)
{
	link->flag &= ~mask;
	link->flag |= flag & mask;
}

static inline unsigned link_flag_get(struct snapraid_link* link, unsigned mask)
{
	return link->flag & mask;
}

/**
 * Allocates a link.
 */
struct snapraid_link* link_alloc(const char* name, const char* link, unsigned link_flag);

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
 * Uses uuid="" if not available.
 */
struct snapraid_map* map_alloc(const char* name, unsigned position, const char* uuid);

/**
 * Deallocates a disk mapping.
 */
void map_free(struct snapraid_map* map);

/**
 * Makes an info.
 */
static inline snapraid_info info_make(time_t last_access, int error, int rehash)
{
	/* clear the lowest bits as reserved for other information */
	snapraid_info info = last_access & ~0x3;
	if (error != 0)
		info |= 0x1;
	if (rehash != 0)
		info |= 0x2;
	return info;
}

/**
 * Extracts the time information.
 * This is the last time when the block was know to be correct.
 */
static inline time_t info_get_time(snapraid_info info)
{
	return info & ~0x3;
}

/**
 * Extracts the error information.
 * Reports if the block address had some problem.
 */
static inline int info_get_bad(snapraid_info info)
{
	return (info & 0x1) != 0;
}

/**
 * Extracts the rehash information.
 * Reports if the block address is using the old hash and needs to be rehashed.
 */
static inline int info_get_rehash(snapraid_info info)
{
	return (info & 0x2) != 0;
}

/**
 * Marks the block address as with error.
 */
static inline snapraid_info info_set_bad(snapraid_info info)
{
	return info | 0x1;
}


/**
 * Marks the block address as with rehash.
 */
static inline snapraid_info info_set_rehash(snapraid_info info)
{
	return info | 0x2;
}

/**
 * Sets the info at the specified position.
 * The position is allocated if not yet done.
 */
static inline void info_set(tommy_arrayof* array, unsigned pos, snapraid_info info)
{
	tommy_arrayof_grow(array, pos + 1);

	memcpy(tommy_arrayof_ref(array, pos), &info, sizeof(snapraid_info));
}

/**
 * Gets the info at the specified position.
 * For not allocated position, 0 is returned.
 */
static inline snapraid_info info_get(tommy_arrayof* array, unsigned pos)
{
	snapraid_info info;
	
	if (pos >= tommy_arrayof_size(array))
		return 0;

	memcpy(&info, tommy_arrayof_ref(array, pos), sizeof(snapraid_info));

	return info;
}

/**
 * Compares info by time.
 */
int info_time_compare(const void* void_a, const void* void_b);

#endif

