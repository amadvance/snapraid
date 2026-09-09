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
 * An elastic split that cannot reach the requested size may keep the size
 * actually allocated and leave the remaining space to following splits.
 * The resulting split layout is returned in parity.
 */
int parity_chsize(struct snapraid_parity_handle* handle, struct snapraid_parity* parity, int* is_modified, data_off_t size, uint32_t block_size, int skip_fallocate, int skip_space_holder);

/**
 * Restore the parity split layout already stored in the handle, limited to size.
 *
 * Persisted split boundaries are preserved. If size ends inside the persisted
 * layout, only its logical tail is removed: the containing split is shortened
 * and all following splits are truncated to zero. If a split is physically
 * non-empty, all preceding splits are restored to their exact logical size.
 * The remaining physical tail is grown lazily by parity_write().
 */
int parity_restore(struct snapraid_parity_handle* handle, data_off_t size, uint32_t block_size, int skip_fallocate);

/**
 * Get the size of the parity.
 *
 * This returns the cached/expected version of the split sizes, and not the real file size.
 */
void parity_size(struct snapraid_parity_handle* handle, data_off_t* out_size);

/**
 * Get the physical size of the logical parity file across splits.
 *
 * This composes the physical EOF of consecutive splits within their logical
 * boundaries. If an earlier split is physically shorter than its logical size,
 * later splits cannot extend the logical offset past that missing region.
 *
 * Physical presence does not imply parity validity. Parity may be invalid,
 * stale, unwritten, sparse, or contain holes independently of its physical EOF.
 */
void parity_physical_size(struct snapraid_parity_handle* handle, data_off_t* out_size);

/**
 * Open an already existing parity file.
 */
int parity_open(struct snapraid_parity_handle* handle, const struct snapraid_parity* parity, unsigned level, int mode, uint32_t block_size, data_off_t limit_size);

/**
 * Flush the parity file in the disk.
 */
int parity_sync(struct snapraid_parity_handle* handle);

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
int parity_write(struct snapraid_parity_handle* handle, block_off_t pos, unsigned char* block_buffer, unsigned block_size, int skip_fallocate);

/**
 * Complete all pending I/O and sync the parity files.
 *
 * This waits for all parity writes, reports their errors, and flushes the
 * parity data to disk. It also waits for all scheduled read-ahead to complete
 * without consuming the results, which remain available to the caller.
 */
int state_barrier(struct snapraid_state* state, struct snapraid_io* io, struct snapraid_parity_handle* parity_handle, block_off_t blockcur);


#endif

