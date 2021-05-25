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
#include "support.h"
#include "tommyds/tommyhash.h"
#include "tommyds/tommylist.h"
#include "tommyds/tommytree.h"
#include "tommyds/tommyhashdyn.h"
#include "tommyds/tommyarray.h"
#include "tommyds/tommyarrayblkof.h"

/****************************************************************************/
/* snapraid */

/**
 * Number of measures of the operation progress.
 */
#define PROGRESS_MAX 100

/**
 * Max UUID length.
 */
#define UUID_MAX 128

/**
 * Invalid position.
 */
#define POS_NULL ((block_off_t)-1)

/**
 * Content file specification.
 */
struct snapraid_content {
	char content[PATH_MAX]; /**< Path of the content file. */
	uint64_t device; /**< Device identifier. */
	void* context; /**< Context used for multithread operations. */
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
 * Block pointer used to represent unused blocks.
 */
#define BLOCK_NULL 0

/**
 * This block is an empty one.
 * Note that an empty block is represent with ::BLOCK_NULL.
 */
#define BLOCK_STATE_EMPTY 0

/**
 * The block has both the hash and the parity computed.
 * This is the normal state of a saved block.
 *
 * The block hash field IS set.
 * The parity for this disk is updated.
 */
#define BLOCK_STATE_BLK 1

/**
 * The block is new and not yet hashed.
 * This happens when a new block overwrite a just removed block, or an empty space.
 *
 * The block hash field MAY be set and it represents the hash of the OLD data.
 * The hash may be also INVALID or ZERO.
 *
 * If the OLD block was empty, the hash is set to the special ZERO value.
 * If the OLD block was lost, the hash is set to the special INVALID value.
 *
 * The parity for this disk is not updated, but it contains the old data referenced by the hash.
 *
 * If the state is read from an incomplete sync, we don't really know if the hash is referring at the
 * data used to compute the parity, because the sync process was interrupted at an unknown point,
 * and the parity may or may not be updated.
 *
 * For this reason we clear all such hashes when reading the state from an incomplete sync before
 * starting a new sync, because sync is affected by such hashes, as sync updates the parity, only
 * if the new data read for CHG blocks has a mismatching hash.
 * Clearing is done setting the ::clear_past_hash flag before reading the state.
 * No clearing is done in other commands, as check and fix are instead able to work with unsynced
 * hashes, and scrub ignores CHG/DELETED blocks.
 */
#define BLOCK_STATE_CHG 2

/**
 * The block is new and hashed.
 * This happens when a new block overwrite a just removed block, or an empty space.
 *
 * Note that when the file copy heuristic is enabled, the REP blocks may be set
 * using this heuristic, meaning that the hash may be wrong.
 *
 * For this reason, when the ::force_nocopy flag is enabled in sync, we convert all the REP blocks
 * to CHG, invalidating the stored hash.
 * Clearing is done setting the ::clear_past_hash flag before reading the state.
 * No clearing is done in other commands, as they don't stop the process like in sync
 * when there is a false silent error.
 *
 * The block hash field IS set, and it represents the hash of the new data.
 * The parity for this disk is not updated.
 */
#define BLOCK_STATE_REP 3

/**
 * This block is a deleted one.
 * This happens when a file is deleted.
 *
 * The block hash field IS set, and it represents the hash of the previous data,
 * but only if it's different by all 0.
 * The parity for this disk is not updated, but it contains the old data referenced by the hash.
 *
 * If the state is read from an incomplete sync, we don't really know if the hash is referring at the
 * data used to compute the parity, because the sync process was interrupted at an unknown point,
 * and the parity may or may not be updated.
 *
 * A now the sync process is not affected by DELETED hash, so clearing won't be really needed,
 * but considering that we have to do it for CHG blocks, we do it also for DELETED ones,
 * clearing all the past hashes.
 * Clearing is done setting the ::clear_past_hash flag before reading the state.
 * No clearing is done in other commands, as check and fix are instead able to work with unsynced
 * hashes, and scrub ignores CHG/DELETED blocks.
 */
#define BLOCK_STATE_DELETED 4

/**
 * Block hash size.
 *
 * At max HASH_MAX.
 */
extern int BLOCK_HASH_SIZE;

/**
 * Block of a file.
 */
struct snapraid_block {
	unsigned char state; /**< State of the block. */

	/**
	 * Hash of the block.
	 *
	 * The effective stored size is BLOCK_HASH_SIZE.
	 */
	unsigned char hash[HASH_MAX];
};

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
 * If a fix was attempted but it failed.
 * It's used only in fix to mark that some data is unrecoverable.
 */
#define FILE_IS_DAMAGED 0x04

/**
 * If a fix was done.
 * It's used only in fix to mark that some data was recovered.
 */
#define FILE_IS_FIXED 0x08

/**
 * If the file was originally missing, and it was created in the fix process.
 * It's used only in fix to mark files recovered from scratch,
 * meaning that they don't have any previous content.
 * This is important because it means that deleting them, you are not going
 * to lose something that cannot be recovered.
 * Note that excluded files won't ever get this flag.
 */
#define FILE_IS_CREATED 0x10

/**
 * If the file has completed its processing, meaning that it won't be opened anymore.
 * It's used only in fix to mark when we finish processing one file.
 * Note that excluded files won't ever get this flag.
 */
#define FILE_IS_FINISHED 0x20

/**
 * If the file hash was obtained from a file copy
 * identified by the same name, size and stamp.
 */
#define FILE_IS_COPY 0x40

/**
 * If the file was opened.
 * It's used in fix to detect if it's the first time a file is opened.
 */
#define FILE_IS_OPENED 0x80

/**
 * If the file is modified from the latest sync.
 * It's used in fix to store if the state of the file before being modified.
 */
#define FILE_IS_UNSYNCED 0x100

/**
 * If the file is without inode.
 * It could happen in file-system where inodes are not persistent,
 * or when restoring a full disk with "fix".
 * In such cases we have to clear any stored duplicate inode.
 * After the scan process completes, no file should have this flag set.
 */
#define FILE_IS_WITHOUT_INODE 0x200

/**
 * The file is deleted.
 * This happens when a file is deleted from the array,
 * but it's keep inside the parity until the next sync.
 *
 * During the file-system check we needs this information,
 * because deleted files may be present only partially.
 */
#define FILE_IS_DELETED 0x400

/**
 * The file is missing.
 * This happens in fix/check when a file is cannot be opened,
 * and marking it as such prevents to retry to open it again.
 */
#define FILE_IS_MISSING 0x800

#define FILE_IS_HARDLINK 0x1000 /**< If it's an hardlink. */
#define FILE_IS_SYMLINK 0x2000 /**< If it's a file symlink. */
#define FILE_IS_SYMDIR 0x4000 /**< If it's a dir symlink for Windows. Not yet supported. */
#define FILE_IS_JUNCTION 0x8000 /**< If it's a junction for Windows. Not yet supported. */
#define FILE_IS_LINK_MASK 0xF000 /**< Mask for link type. */

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
	tommy_hashdyn_node stampset;
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
 * Chunk.
 *
 * A extent represents a fragment of a file mapped into the parity.
 */
struct snapraid_extent {
	struct snapraid_file* file; /**< File containing this extent. */
	block_off_t parity_pos; /**< Parity position. */
	block_off_t file_pos; /**< Position in the file. */
	block_off_t count; /**< Number of sequential blocks in the file and parity. */
	tommy_tree_node parity_node; /**< Tree sorted by <parity_pos>. */
	tommy_tree_node file_node; /**< Tree sorter by <file,file_pos>. */
};

/**
 * Disk.
 */
struct snapraid_disk {
	char name[PATH_MAX]; /**< Name of the disk. */
	char dir[PATH_MAX]; /**< Mount point of the disk. It always terminates with /. */
	char smartctl[PATH_MAX]; /**< Custom command for smartctl. Empty means auto. */
	char uuid[UUID_MAX]; /**< UUID of the disk. */

	uint64_t device; /**< Device identifier. */
	block_off_t total_blocks; /**< Number of total blocks. */
	block_off_t free_blocks; /**< Number of free blocks at the last sync. */

	uint64_t tick; /**< Usage time. */
	uint64_t progress_tick[PROGRESS_MAX]; /**< Last ticks of progress. */
	unsigned cached_blocks; /**< Number of IO blocks cached. */
	struct snapraid_file* progress_file; /**< File in progress. */

	/**
	 * First free searching block.
	 * Note that it doesn't necessarily point at the first free block,
	 * but it just tell you that no free block is present before this position.
	 */
	block_off_t first_free_block;

	int has_volatile_inodes; /**< If the underline file-system has not persistent inodes. */
	int has_volatile_hardlinks; /**< If the underline file-system has not synchronized metadata for hardlink (NTFS). */
	int has_unreliable_physical; /**< If the physical offset of files has duplicates. */
	int has_different_uuid; /**< If the disk has a different UUID, meaning that it is not the same file-system. */
	int has_unsupported_uuid; /**< If the disk doesn't report UUID, meaning it's not supported. */
	int had_empty_uuid; /**< If the disk had an empty UUID, meaning that it's a new disk. */
	int mapping_idx; /**< Index in the mapping vector. Used only as buffer when writing the content file. */
	int skip_access; /**< If the disk is inaccessible and it should be skipped. */

#if HAVE_THREAD
	/**
	 * Mutex for protecting the filesystem structure.
	 *
	 * Specifically, this protects ::fs_parity, ::fs_file, and ::fs_last,
	 * meaning that it protects only extents.
	 *
	 * Files, links and dirs are not protected as they are not expected to
	 * change during multithread processing.
	 */
	thread_mutex_t fs_mutex;
	int fs_mutex_enabled; /*< If the lock has to be used. */
#endif

	/**
	 * Mapping of extents in the parity.
	 * Sorted by <parity_pos> and by <file,file_pos>
	 */
	tommy_tree fs_parity;
	tommy_tree fs_file;

	/**
	 * Last extent we accessed.
	 * It's used to optimize access of sequential blocks.
	 */
	struct snapraid_extent* fs_last;

	/**
	 * List of all the snapraid_file for the disk.
	 */
	tommy_list filelist;

	/**
	 * List of all the deleted file for the disk.
	 *
	 * These files are kept allocated, because the blocks are still referenced in
	 * the ::blockarr.
	 */
	tommy_list deletedlist;

	tommy_hashdyn inodeset; /**< Hashtable by inode of all the files. */
	tommy_hashdyn pathset; /**< Hashtable by path of all the files. */
	tommy_hashdyn stampset; /**< Hashtable by stamp (size and time) of all the files. */
	tommy_list linklist; /**< List of all the links. */
	tommy_hashdyn linkset; /**< Hashtable by name of all the links. */
	tommy_list dirlist; /**< List of all the empty dirs. */
	tommy_hashdyn dirset; /**< Hashtable by name of all the empty dirs. */

	/* nodes for data structures */
	tommy_node node;
};

/**
 * Disk mapping.
 */
struct snapraid_map {
	char name[PATH_MAX]; /**< Name of the disk. */
	char uuid[UUID_MAX]; /**< UUID of the disk. Empty if unknown. */
	block_off_t total_blocks; /**< Number of total blocks. */
	block_off_t free_blocks; /**< Number of free blocks at last 'sync'. */
	unsigned position; /**< Position of the disk in the parity. */

	/* nodes for data structures */
	tommy_node node;
};

/**
 * Max number of parity split.
 */
#define SPLIT_MAX 8

/**
 * Invalid parity size.
 *
 * This value is used to identify new parities,
 * like when you alter the configuration adding
 * a new parity level, creating it with 'fix'.
 * Given that 'fix' doesn't write the content file,
 * the new size will be written only at the next
 * 'sync'.
 */
#define PARITY_SIZE_INVALID -1LL

/**
 * Parity split.
 */
struct snapraid_split {
	char path[PATH_MAX]; /**< Path of the parity file. */
	char uuid[UUID_MAX]; /**< UUID of the disk. Empty if unknown. */

	/**
	 * Size of the parity split.
	 * Only the latest not zero size is allowed to grow.
	 * If the value is unset, it's PARITY_SIZE_INVALID.
	 */
	data_off_t size;

	uint64_t device; /**< Device identifier of the parity. */
};

/**
 * Parity.
 */
struct snapraid_parity {
	struct snapraid_split split_map[SPLIT_MAX]; /**< Parity splits. */
	unsigned split_mac; /**< Number of parity splits. */
	char smartctl[PATH_MAX]; /**< Custom command for smartctl. Empty means auto. */
	block_off_t total_blocks; /**< Number of total blocks. */
	block_off_t free_blocks; /**< Number of free blocks at the last sync. */
	int is_excluded_by_filter; /**< If the parity is excluded by filters. */
	int skip_access; /**< If at least one of the parity disk is inaccessible and it should be skipped. */
	uint64_t tick; /**< Usage time. */
	uint64_t progress_tick[PROGRESS_MAX]; /**< Last cpu ticks of progress. */
	unsigned cached_blocks; /**< Number of IO blocks cached. */
};

/**
 * Info.
 */
typedef uint32_t snapraid_info;

/**
 * Allocate a content.
 */
struct snapraid_content* content_alloc(const char* path, uint64_t dev);

/**
 * Deallocate a content.
 */
void content_free(struct snapraid_content* content);

/**
 * Allocate a filter pattern for files and directories.
 */
struct snapraid_filter* filter_alloc_file(int is_include, const char* pattern);

/**
 * Allocate a filter pattern for disks.
 */
struct snapraid_filter* filter_alloc_disk(int is_include, const char* pattern);

/**
 * Deallocate an exclusion.
 */
void filter_free(struct snapraid_filter* filter);

/**
 * Filter type description.
 */
const char* filter_type(struct snapraid_filter* filter, char* out, size_t out_size);

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
 * Filter a path using a list of filters.
 * For each element of the path all the filters are applied, until the first one that matches.
 * Return !=0 if it should be excluded.
 */
int filter_path(tommy_list* filterlist, struct snapraid_filter** reason, const char* disk, const char* sub);

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
int filter_correctness(int filter_error, tommy_arrayblkof* infoarr, struct snapraid_disk* disk, struct snapraid_file* file);

/**
 * Filter a dir using a list of filters.
 * For each element of the path all the filters are applied, until the first one that matches.
 * Thesesdir are always by included by default, to allow to apply rules at the contained files.
 * Return !=0 if should be excluded.
 */
int filter_subdir(tommy_list* filterlist, struct snapraid_filter** reason, const char* disk, const char* sub);

/**
 * Filter a dir using a list of filters.
 * For each element of the path all the filters are applied, until the first one that matches.
 * Return !=0 if should be excluded.
 */
int filter_emptydir(tommy_list* filterlist, struct snapraid_filter** reason, const char* disk, const char* sub);

/**
 * Filter a path if it's a content file.
 * Return !=0 if should be excluded.
 */
int filter_content(tommy_list* contentlist, const char* path);

/**
 * Check if the specified hash is invalid.
 *
 * An invalid hash is represented with all bytes at 0x00.
 *
 * If working with reduced hash lengths, this function always return 0.
 */
static inline int hash_is_invalid(const unsigned char* hash)
{
	int i;

	/* if the hash is reduced, we cannot grant that it's a specific kind of hash */
	if (BLOCK_HASH_SIZE != HASH_MAX)
		return 0;

	for (i = 0; i < BLOCK_HASH_SIZE; ++i)
		if (hash[i] != 0x00)
			return 0;

	return 1;
}

/**
 * Check if the specified hash represent the zero block.
 *
 * A zero hash is represented with all bytes at 0xFF.
 *
 * If working with reduced hash lengths, this function always return 0.
 */
static inline int hash_is_zero(const unsigned char* hash)
{
	int i;

	/* if the hash is reduced, we cannot grant that it's a specific kind of hash */
	if (BLOCK_HASH_SIZE != HASH_MAX)
		return 0;

	for (i = 0; i < BLOCK_HASH_SIZE; ++i)
		if (hash[i] != 0xFF)
			return 0;

	return 1;
}

/**
 * Check if the specified hash is unequivocally representing the data.
 *
 * If working with reduced hash lengths, this function always return 0.
 */
static inline int hash_is_unique(const unsigned char* hash)
{
	/* if the hash is reduced, we cannot grant that it's a specific kind of hash */
	if (BLOCK_HASH_SIZE != HASH_MAX)
		return 0;

	return !hash_is_zero(hash) && !hash_is_invalid(hash);
}

/**
 * Set the hash to the special INVALID value.
 */
static inline void hash_invalid_set(unsigned char* hash)
{
	memset(hash, 0x00, BLOCK_HASH_SIZE);
}

/**
 * Set the hash to the special ZERO value.
 */
static inline void hash_zero_set(unsigned char* hash)
{
	memset(hash, 0xFF, BLOCK_HASH_SIZE);
}

/**
 * Allocated space for block.
 */
static inline size_t block_sizeof(void)
{
	return 1 + BLOCK_HASH_SIZE;
}

/**
 * Get the state of the block.
 *
 * For this function, it's allowed to pass a NULL block
 * pointer than results in the BLOCK_STATE_EMPTY state.
 */
static inline unsigned block_state_get(const struct snapraid_block* block)
{
	if (block == BLOCK_NULL)
		return BLOCK_STATE_EMPTY;

	return block->state;
}

/**
 * Set the state of the block.
 */
static inline void block_state_set(struct snapraid_block* block, unsigned state)
{
	block->state = state;
}

/**
 * Check if the specified block has an updated hash.
 *
 * Note that EMPTY / CHG / DELETED return 0.
 */
static inline int block_has_updated_hash(const struct snapraid_block* block)
{
	unsigned state = block_state_get(block);

	return state == BLOCK_STATE_BLK || state == BLOCK_STATE_REP;
}

/**
 * Check if the specified block has a past hash,
 * i.e. the hash of the data that it's now overwritten or lost.
 *
 * Note that EMPTY / BLK / REP return 0.
 */
static inline int block_has_past_hash(const struct snapraid_block* block)
{
	unsigned state = block_state_get(block);

	return state == BLOCK_STATE_CHG || state == BLOCK_STATE_DELETED;
}

/**
 * Check if the specified block is part of a file.
 *
 * Note that EMPTY / DELETED return 0.
 */
static inline int block_has_file(const struct snapraid_block* block)
{
	unsigned state = block_state_get(block);

	return state == BLOCK_STATE_BLK
	       || state == BLOCK_STATE_CHG || state == BLOCK_STATE_REP;
}

/**
 * Check if the block has an invalid parity than needs to be updated.
 *
 * Note that EMPTY / BLK return 0.
 */
static inline int block_has_invalid_parity(const struct snapraid_block* block)
{
	unsigned state = block_state_get(block);

	return state == BLOCK_STATE_DELETED
	       || state == BLOCK_STATE_CHG || state == BLOCK_STATE_REP;
}

/**
 * Check if the block is part of a file with valid parity.
 *
 * Note that anything different than BLK return 0.
 */
static inline int block_has_file_and_valid_parity(const struct snapraid_block* block)
{
	unsigned state = block_state_get(block);

	return state == BLOCK_STATE_BLK;
}

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
 * Allocate a file.
 */
struct snapraid_file* file_alloc(unsigned block_size, const char* sub, data_off_t size, uint64_t mtime_sec, int mtime_nsec, uint64_t inode, uint64_t physical);

/**
 * Duplicate a file.
 */
struct snapraid_file* file_dup(struct snapraid_file* copy);

/**
 * Deallocate a file.
 */
void file_free(struct snapraid_file* file);

/**
 * Rename a file.
 */
void file_rename(struct snapraid_file* file, const char* sub);

/**
 * Copy a file.
 */
void file_copy(struct snapraid_file* src_file, struct snapraid_file* dest_file);

/**
 * Return the block at the specified position.
 *
 * Note that the block size if a runtime value.
 */
static inline struct snapraid_block* file_block(struct snapraid_file* file, size_t pos)
{
	unsigned char* ptr = (unsigned char*)file->blockvec;

	return (struct snapraid_block*)(ptr + pos * block_sizeof());
}

/**
 * Return the name of the file, without the dir.
 */
const char* file_name(const struct snapraid_file* file);

/**
 * Check if the block is the last in the file.
 */
int file_block_is_last(struct snapraid_file* file, block_off_t file_pos);

/**
 * Get the size in bytes of the block.
 * If it's the last block of a file it could be less than block_size.
 */
unsigned file_block_size(struct snapraid_file* file, block_off_t file_pos, unsigned block_size);

/**
 * Compare a file with an inode.
 */
int file_inode_compare_to_arg(const void* void_arg, const void* void_data);

/**
 * Compare files by inode.
 */
int file_inode_compare(const void* void_a, const void* void_b);

/**
 * Compare files by path.
 */
int file_path_compare(const void* void_a, const void* void_b);

/**
 * Compare files by physical address.
 */
int file_physical_compare(const void* void_a, const void* void_b);

/**
 * Compute the hash of a file inode.
 */
static inline tommy_uint32_t file_inode_hash(uint64_t inode)
{
	return (tommy_uint32_t)tommy_inthash_u64(inode);
}

/**
 * Compare a file with a path.
 */
int file_path_compare_to_arg(const void* void_arg, const void* void_data);

/**
 * Compare a file with another file for name, stamp, and both.
 */
int file_name_compare(const void* void_a, const void* void_b);
int file_stamp_compare(const void* void_a, const void* void_b);
int file_namestamp_compare(const void* void_a, const void* void_b);
int file_pathstamp_compare(const void* void_a, const void* void_b);

/**
 * Compute the hash of a file path.
 */
static inline tommy_uint32_t file_path_hash(const char* sub)
{
	return tommy_hash_u32(0, sub, strlen(sub));
}

/**
 * Compute the hash of a file stamp.
 */
static inline tommy_uint32_t file_stamp_hash(data_off_t size, int64_t mtime_sec, int mtime_nsec)
{
	return tommy_inthash_u32((tommy_uint32_t)size ^ tommy_inthash_u32(mtime_sec ^ tommy_inthash_u32(mtime_nsec)));
}

/**
 * Allocate a extent.
 */
struct snapraid_extent* extent_alloc(block_off_t parity_pos, struct snapraid_file* file, block_off_t file_pos, block_off_t count);

/**
 * Deallocate a extent.
 */
void extent_free(struct snapraid_extent* extent);

/**
 * Compare extent by parity position.
 */
int extent_parity_compare(const void* void_a, const void* void_b);

/**
 * Compare extent by file and file position.
 */
int extent_file_compare(const void* void_a, const void* void_b);

static inline int link_flag_has(const struct snapraid_link* slink, unsigned mask)
{
	return (slink->flag & mask) == mask;
}

static inline void link_flag_set(struct snapraid_link* slink, unsigned mask)
{
	slink->flag |= mask;
}

static inline void link_flag_clear(struct snapraid_link* slink, unsigned mask)
{
	slink->flag &= ~mask;
}

static inline void link_flag_let(struct snapraid_link* slink, unsigned flag, unsigned mask)
{
	slink->flag &= ~mask;
	slink->flag |= flag & mask;
}

static inline unsigned link_flag_get(struct snapraid_link* slink, unsigned mask)
{
	return slink->flag & mask;
}

/**
 * Allocate a link.
 */
struct snapraid_link* link_alloc(const char* name, const char* slink, unsigned link_flag);

/**
 * Deallocate a link.
 */
void link_free(struct snapraid_link* slink);

/**
 * Compare a link with a name.
 */
int link_name_compare_to_arg(const void* void_arg, const void* void_data);

/**
 * Compare links by path.
 */
int link_alpha_compare(const void* void_a, const void* void_b);

/**
 * Compute the hash of a link name.
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
 * Allocate a dir.
 */
struct snapraid_dir* dir_alloc(const char* name);

/**
 * Deallocate a dir.
 */
void dir_free(struct snapraid_dir* dir);

/**
 * Compare a dir with a name.
 */
int dir_name_compare(const void* void_arg, const void* void_data);

/**
 * Compute the hash of a dir name.
 */
static inline tommy_uint32_t dir_name_hash(const char* name)
{
	return tommy_hash_u32(0, name, strlen(name));
}

/**
 * Allocate a disk.
 */
struct snapraid_disk* disk_alloc(const char* name, const char* dir, uint64_t dev, const char* uuid, int skip_access);

/**
 * Deallocate a disk.
 */
void disk_free(struct snapraid_disk* disk);

/**
 * Enable multithread support for the disk.
 */
void disk_start_thread(struct snapraid_disk* disk);

/**
 * Get the size of the disk in blocks.
 */
block_off_t fs_size(struct snapraid_disk* disk);

/**
 * Check if a disk is totally empty and can be discarded from the content file.
 * A disk is empty if it doesn't contain any file, symlink, hardlink or dir
 * and without any DELETED block.
 * The blockmax is used to limit the search of DELETED block up to blockmax.
 */
int fs_is_empty(struct snapraid_disk* disk, block_off_t blockmax);

/**
 * Check the file-system for errors.
 * Return 0 if it's OK.
 */
int fs_check(struct snapraid_disk* disk);

/**
 * Allocate a parity position for the specified file position.
 *
 * After this call you can use the par2file/par2block operations
 * to query the relation.
 *
 * \note This function is NOT thread-safe as it uses the disk cache. +
 */
void fs_allocate(struct snapraid_disk* disk, block_off_t parity_pos, struct snapraid_file* file, block_off_t file_pos);

/**
 * Deallocate the parity position.
 *
 * After this call the par2file/par2block operations
 * won't find anymore the parity association.
 *
 * \note This function is NOT thread-safe as it uses the disk cache.
 */
void fs_deallocate(struct snapraid_disk* disk, block_off_t pos);

/**
 * Get the block from the file position.
 */
struct snapraid_block* fs_file2block_get(struct snapraid_file* file, block_off_t file_pos);

/**
 * Get the file position from the parity position.
 * Return 0 if no file is using it.
 */
struct snapraid_file* fs_par2file_find(struct snapraid_disk* disk, block_off_t parity_pos, block_off_t* file_pos);

/**
 * Get the file position from the parity position.
 */
static inline struct snapraid_file* fs_par2file_get(struct snapraid_disk* disk, block_off_t parity_pos, block_off_t* file_pos)
{
	struct snapraid_file* ret;

	ret = fs_par2file_find(disk, parity_pos, file_pos);
	if (ret == 0) {
		/* LCOV_EXCL_START */
		log_fatal("Internal inconsistency when deresolving parity to file at position '%u' in disk '%s'\n", parity_pos, disk->name);
		os_abort();
		/* LCOV_EXCL_STOP */
	}

	return ret;
}

/**
 * Get the parity position from the file position.
 * Return POS_NULL if no parity is allocated.
 */
block_off_t fs_file2par_find(struct snapraid_disk* disk, struct snapraid_file* file, block_off_t file_pos);

/**
 * Get the parity position from the file position.
 */
static inline block_off_t fs_file2par_get(struct snapraid_disk* disk, struct snapraid_file* file, block_off_t file_pos)
{
	block_off_t ret;

	ret = fs_file2par_find(disk, file, file_pos);
	if (ret == POS_NULL) {
		/* LCOV_EXCL_START */
		log_fatal("Internal inconsistency when resolving file '%s' at position '%u/%u' in disk '%s'\n", file->sub, file_pos, file->blockmax, disk->name);
		os_abort();
		/* LCOV_EXCL_STOP */
	}

	return ret;
}

/**
 * Get the block from the parity position.
 * Return BLOCK_NULL==0 if the block is over the end of the disk or not used.
 */
struct snapraid_block* fs_par2block_find(struct snapraid_disk* disk, block_off_t parity_pos);

/**
 * Get the block from the parity position.
 */
static inline struct snapraid_block* fs_par2block_get(struct snapraid_disk* disk, block_off_t parity_pos)
{
	struct snapraid_block* ret;

	ret = fs_par2block_find(disk, parity_pos);
	if (ret == BLOCK_NULL) {
		/* LCOV_EXCL_START */
		log_fatal("Internal inconsistency when deresolving parity to block at position '%u' in disk '%s'\n", parity_pos, disk->name);
		os_abort();
		/* LCOV_EXCL_STOP */
	}

	return ret;
}

/**
 * Allocate a disk mapping.
 * Uses uuid="" if not available.
 */
struct snapraid_map* map_alloc(const char* name, unsigned position, block_off_t total_blocks, block_off_t free_blocks, const char* uuid);

/**
 * Deallocate a disk mapping.
 */
void map_free(struct snapraid_map* map);

/**
 * Mask used to store additional information in the info bits.
 *
 * These bits reduce the granularity of the time in the memory representation.
 */
#define INFO_MASK 0x7

/**
 * Make an info.
 */
static inline snapraid_info info_make(time_t last_access, int error, int rehash, int justsynced)
{
	/* clear the lowest bits as reserved for other information */
	snapraid_info info = last_access & ~INFO_MASK;

	if (error != 0)
		info |= 0x1;
	if (rehash != 0)
		info |= 0x2;
	if (justsynced != 0)
		info |= 0x4;
	return info;
}

/**
 * Extract the time information.
 * This is the last time when the block was know to be correct.
 * The "scrubbed" info tells if the time is referring at the latest sync or scrub.
 */
static inline time_t info_get_time(snapraid_info info)
{
	return info & ~INFO_MASK;
}

/**
 * Extract the error information.
 * Report if the block address had some problem.
 */
static inline int info_get_bad(snapraid_info info)
{
	return (info & 0x1) != 0;
}

/**
 * Extract the rehash information.
 * Report if the block address is using the old hash and needs to be rehashed.
 */
static inline int info_get_rehash(snapraid_info info)
{
	return (info & 0x2) != 0;
}

/**
 * Extract the scrubbed information.
 * Report if the block address was never scrubbed.
 */
static inline int info_get_justsynced(snapraid_info info)
{
	return (info & 0x4) != 0;
}

/**
 * Mark the block address as with error.
 */
static inline snapraid_info info_set_bad(snapraid_info info)
{
	return info | 0x1;
}

/**
 * Mark the block address as with rehash.
 */
static inline snapraid_info info_set_rehash(snapraid_info info)
{
	return info | 0x2;
}

/**
 * Set the info at the specified position.
 * The position is allocated if not yet done.
 */
static inline void info_set(tommy_arrayblkof* array, block_off_t pos, snapraid_info info)
{
	tommy_arrayblkof_grow(array, pos + 1);

	memcpy(tommy_arrayblkof_ref(array, pos), &info, sizeof(snapraid_info));
}

/**
 * Get the info at the specified position.
 * For not allocated position, 0 is returned.
 */
static inline snapraid_info info_get(tommy_arrayblkof* array, block_off_t pos)
{
	snapraid_info info;

	if (pos >= tommy_arrayblkof_size(array))
		return 0;

	memcpy(&info, tommy_arrayblkof_ref(array, pos), sizeof(snapraid_info));

	return info;
}

/**
 * Compare times
 */
int time_compare(const void* void_a, const void* void_b);

/****************************************************************************/
/* format */

#define FMT_FILE 0 /**< Print only the file. */
#define FMT_DISK 1 /**< Print the disk name and the file. */
#define FMT_PATH 2 /**< Print the full path. */

extern int FMT_MODE;

/**
 * Format a file path for poll reference
 */
const char* fmt_poll(const struct snapraid_disk* disk, const char* str, char* buffer);

/**
 * Format a path name for terminal reference
 */
const char* fmt_term(const struct snapraid_disk* disk, const char* str, char* buffer);

#endif

