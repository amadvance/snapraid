// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2011 Andrea Mazzoleni

#ifndef __PARITY_H
#define __PARITY_H

#include "support.h"
#include "bw.h"
#include "state.h"
#include "io.h"

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
	 * High-water mark size of the parity split.
	 *
	 * This is the physical truncation and read limit of the split.
	 *
	 * physical_reach_size does NOT indicate that data below this offset is valid parity.
	 * Parity may be invalid, stale, unwritten, or sparse at any point below this
	 * boundary. Correctness is tracked independently per block in the content state
	 * and verified through data hashes and recomputation.
	 *
	 * Data beyond physical_reach_size is disposable/preallocated and discarded by
	 * parity_truncate(). A completed write advances physical_reach_size to the end of
	 * the written block so it is not truncated away.
	 *
	 * Before closing writable parity handles during fix operations, parity_truncate()
	 * persists this boundary into the physical EOF. On the next open, physical_reach_size can
	 * therefore be initialized from st_size.
	 */
	data_off_t physical_reach_size;

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
	struct snapraid_bw* bw; /**< Context for bandwidth limiting. */
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
 *
 * If allow_split_realloc is nonzero, an elastic split that cannot reach the
 * requested size may keep the size actually allocated and leave the remaining
 * space to following splits.
 *
 * If allow_split_realloc is zero, a partial allocation must fail instead of
 * moving a logical split boundary. This is required by callers that cannot
 * persist a changed split layout.
 */
int parity_chsize(struct snapraid_parity_handle* handle, struct snapraid_parity* parity, int* is_modified, data_off_t size, uint32_t block_size, int skip_fallocate, int skip_space_holder, int allow_split_realloc);

/**
 * Get the size of the parity.
 *
 * This returns the cached/expected version of the split sizes, and not the real file size.
 */
void parity_size(struct snapraid_parity_handle* handle, data_off_t* out_size);

/**
 * Get the physical reach of the logical parity file across splits.
 *
 * This composes the physical_reach_size boundaries of consecutive splits.
 * If the physical_reach_size of an earlier split is smaller than its logical size,
 * later splits cannot extend the logical offset past that missing region.
 *
 * Parity within this range is not necessarily valid or synchronized;
 * per-block validity is tracked independently in the content state.
 *
 * For example, with two 100 GiB splits, if the first has physical_reach_size 90 GiB
 * and the second has physical_reach_size 100 GiB, the result is 90 GiB, not 190 GiB.
 */
void parity_physical_reach_size(struct snapraid_parity_handle* handle, data_off_t* out_size);

/**
 * Open an already existing parity file.
 */
int parity_open(struct snapraid_parity_handle* handle, const struct snapraid_parity* parity, unsigned level, int mode, uint32_t block_size, data_off_t limit_size);

/**
 * Flush the parity file in the disk.
 */
int parity_sync(struct snapraid_parity_handle* handle);

/**
 * Truncate each split to its physical physical_reach_size boundary.
 */
int parity_truncate(struct snapraid_parity_handle* handle);

/**
 * Close the parity file.
 */
int parity_close(struct snapraid_parity_handle* handle);

/**
 * Read a block from the parity file.
 */
int parity_read(struct snapraid_parity_handle* handle, block_off_t pos, unsigned char* block_buffer, unsigned block_size);

/**
 * Write a block in the parity file.
 */
int parity_write(struct snapraid_parity_handle* handle, block_off_t pos, unsigned char* block_buffer, unsigned block_size);

/**
 * Complete all pending I/O and sync the parity files.
 *
 * This waits for all parity writes, reports their errors, and flushes the
 * parity data to disk. It also waits for all scheduled read-ahead to complete
 * without consuming the results, which remain available to the caller.
 */
int state_barrier(struct snapraid_state* state, struct snapraid_io* io, struct snapraid_parity_handle* parity_handle, block_off_t blockcur);


#endif

