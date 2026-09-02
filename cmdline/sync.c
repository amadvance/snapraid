// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2011 Andrea Mazzoleni

#include "os/portable.h"

#include "support.h"
#include "elem.h"
#include "state.h"
#include "parity.h"
#include "handle.h"
#include "io.h"
#include "raid/raid.h"

/*
 * SnapRAID sync model
 *
 * Sync brings the stored parity and content metadata in line with the data
 * files observed by SnapRAID.
 *
 * On the first sync there is no previously synchronized state. SnapRAID scans
 * the filesystem, allocates parity blocks, reads the data, computes hashes,
 * and generates parity. The resulting content file records the synchronized
 * file metadata, block allocation and hashes.
 *
 * On subsequent syncs the filesystem is scanned again and compared with the
 * previous content state. Added, removed, moved, restored or changed files
 * invalidate only the parity positions affected by those changes.
 *
 * Content and parity are related but distinct persistent states. The content
 * file records SnapRAID's logical knowledge of the array; the parity files
 * contain the physically generated redundancy.
 *
 * They do not advance atomically. If a sync is interrupted, some parity
 * positions may already have been updated while the content file still
 * describes an intermediate state. CHG, REP, BLK and DELETED preserve enough
 * information to handle these conditions safely on a later run.
 *
 * Consequently, content state must not be interpreted as an unconditional
 * description of the bytes currently present in parity. In particular,
 * CHG/REP/DELETED deliberately represent states for which physical parity
 * cannot be trusted to correspond to the current logical data.
 *
 * Block state model
 * -----------------
 *
 * A block state describes the relationship between:
 *
 * - the data currently associated with the parity position,
 * - the hash stored in the content state,
 * - the parity physically stored on disk.
 *
 * OLD and NEW below refer to generations of data, not necessarily to the hash
 * algorithm. During rehashing, per-position information determines whether a
 * hash belongs to the previous or current hash algorithm.
 *
 * The stable states are:
 *
 *   State      File     Stored hash                 Parity meaning
 *   ------------------------------------------------------------------------
 *   EMPTY      no       none                        logical input is zero
 *   CHG        yes      OLD / ZERO / INVALID        invalid for current data
 *   REP        yes      NEW                         invalid for current data
 *   BLK        yes      CURRENT                     synchronized contribution
 *   DELETED    no       OLD / INVALID               invalid for logical zero
 *   REBUILD    yes      CURRENT                     physical parity untrusted, rebuild in place
 *
 * "Invalid" is conservative. It does not necessarily mean that the bytes in
 * parity are wrong. After an interrupted sync parity may still represent OLD
 * data, may already represent NEW data, or may have been only partly updated.
 * CHG, REP and DELETED mean that no particular physical parity state may be
 * assumed. REBUILD means current data and hash are known, but physical parity
 * must be regenerated using the existing data-to-parity mapping.
 *
 * EMPTY contributes zero and does not by itself require an update. Therefore
 * a parity position containing only EMPTY and DELETED blocks may be skipped:
 * no live file depends on it. A DELETED block alone does not necessarily mean
 * that the array is considered unsynchronized.
 *
 * BLK is a per-data-block property. It means that this block does not itself
 * require a parity update. It does not imply that the whole parity stripe is
 * valid if another disk at the same position contains CHG, REP, DELETED or REBUILD.
 *
 * The main state transitions are:
 *
 *   Transition        Hash after       Physical parity
 *   ------------------------------------------------------------------------
 *   EMPTY   -> CHG    ZERO             represents the previous zero input
 *   DELETED -> CHG    OLD/INVALID      unknown and not trusted
 *   EMPTY   -> REP    NEW              represents the previous zero input
 *   DELETED -> REP    NEW              unknown and not trusted
 *
 *   BLK     -> REBUILD CURRENT         parity is deliberately no longer trusted
 *   REBUILD -> BLK    CURRENT          parity rebuilt and progress committed
 *
 *   BLK     -> DELETED OLD             initially still represents OLD data
 *   CHG     -> DELETED unchanged       already unknown
 *   REP     -> DELETED INVALID         already unknown
 *   REBUILD -> DELETED OLD             untrusted parity, CURRENT hash becomes OLD
 *
 *   REP     -> CHG    INVALID          unchanged and not trusted
 *   CHG     -> REP    NEW              unchanged and not trusted
 *
 *   CHG     -> BLK    CURRENT          valid when sync progress is committed
 *   REP     -> BLK    CURRENT          valid when sync progress is committed
 *   DELETED -> EMPTY  none             valid when sync progress is committed
 *
 * EMPTY/DELETED -> CHG/REP and BLK/CHG/REP -> DELETED are normally produced
 * by the filesystem scan before sync starts modifying parity. REP -> CHG is
 * also used when a previously stored copy-derived hash is invalidated.
 *
 * BLK -> REBUILD is produced when --force-full sync is requested to make full
 * parity reconstruction persistent across crashes before touching physical parity.
 * BLK -> REBUILD does not itself change physical parity; it changes what SnapRAID
 * is willing to trust.
 *
 * CHG -> REP is performed by the optional prehash pass. It means that NEW data
 * has been read and hashed, while parity remains explicitly invalid.
 *
 * CHG can become BLK in two ways. Normally parity is regenerated. However, if
 * the previous sync completed normally, the OLD hash is unambiguous and the
 * current data has the same hash, the existing parity contribution is already
 * correct and no parity write is needed. This optimization is disabled after
 * an interrupted sync because the stored OLD hash can no longer be reliably
 * associated with the physical parity state.
 *
 * The table describes stable state semantics. During processing, a CHG hash
 * may temporarily be replaced in memory by the hash just computed for current
 * data before the block is promoted to BLK. Such temporary values are
 * implementation state and do not redefine the persistent meaning of CHG.
 *
 * Crash consistency model
 * -----------------------
 *
 * State changes made by state_sync_process() are not immediate durability
 * guarantees. CHG/REP/REBUILD may become BLK, and DELETED may become EMPTY, in memory
 * before the corresponding asynchronous parity write has completed.
 *
 * This is safe because completed state is not published to the content file
 * until parity has crossed a durability barrier. The ordering is:
 *
 *   1. Scan creates CHG, REP and DELETED pending states.
 *      For --force-full, BLK blocks are marked REBUILD before parity resizing.
 *      For --force-realloc, the scan also constructs the new data-to-parity
 *      allocation before physical parity is resized.
 *
 *   2. Before a destructive parity resize, the pending logical state is made
 *      durable. This includes --force-full, --force-realloc, and normal syncs
 *      whose new logical parity size is smaller than the previously persisted
 *      one.
 *
 *      Persisting this state before a shrink guarantees that parity positions
 *      physically discarded by the resize are no longer referenced by an
 *      older durable mapping.
 *
 *      Non-destructive growth does not require this extra barrier. The normal
 *      post-resize content write persists the resulting physical split sizes
 *      before parity synchronization starts.
 *
 *   3. Sync reads current data and computes new parity. Blocks may already be
 *      changed to BLK/EMPTY in memory while parity writes are still pending.
 *
 *   4. Before an autosave publishes these completed states, state_barrier()
 *      drains pending parity writes and synchronizes the parity files.
 *
 *   5. Only after that barrier may BLK/EMPTY progress be written to content.
 *      Final content persistence follows the same ordering.
 *
 * The persistent crash invariant is therefore:
 *
 *   CHG / REP / DELETED in content
 *       => physical parity must be treated according to the existing interrupted
 *          sync semantics.
 *
 *   REBUILD in content
 *       => physical parity must not be trusted
 *       => parity must be recomputed
 *       => existing data-to-parity allocation must be preserved
 *
 *   BLK / EMPTY progress published by sync
 *       => the parity required by that progress was made durable before the
 *          content state was written.
 *
 * If a crash happens after parity was written but before completed content
 * state was saved, content simply retains CHG/REP/DELETED. A later sync may
 * repeat work, but it does not incorrectly trust the modified parity.
 *
 * DELETED has one additional persistence case. Trailing positions containing
 * no live data can disappear when parity is shortened, because
 * parity_allocated_size() excludes trailing positions without files. The
 * subsequent content state therefore no longer needs to serialize those
 * trailing DELETED entries.
 *
 * Parity stripe model
 * -------------------
 *
 * Sync operates on parity positions. At each position, the blocks from all
 * data disks form the data inputs of the same parity stripe and must therefore
 * be considered together.
 *
 * A stripe requires sync processing only when it contains both an invalid
 * parity contribution and at least one live data block. If no contribution is
 * invalid, parity already represents all current live inputs. If no live block
 * exists, the parity at that position is irrelevant to any file and may be
 * left untouched even when DELETED blocks remain.
 *
 * In simplified form, the decision is:
 *
 *   no invalid block
 *       => no parity update is required
 *
 *   invalid block, but no live block
 *       => the stripe may be skipped
 *
 *   invalid block and at least one live block
 *       => the stripe must be processed
 *
 * This is why a DELETED block alone does not necessarily make the array
 * unsynchronized. Its old contribution only matters when the same parity
 * position is still used by live data on another disk.
 *
 * Invalid blocks are also handled conservatively during silent-error
 * recovery. CHG, REP and DELETED cannot in general be used as known inputs to
 * the recovery equations because their current logical data may differ from
 * the data represented by physical parity, especially after an interrupted
 * sync.
 *
 * They are therefore treated as unknown inputs while reconstructing a
 * suspected BLK block. A CHG whose stored OLD value is known to be zero is a
 * special case: the old zero contribution can be supplied directly and does
 * not consume a recovery equation.
 *
 * Any reconstructed values for CHG, REP or DELETED are only intermediate
 * values needed to recover synchronized data and regenerate parity. Their
 * current data buffers remain the authoritative inputs for completing sync.
 *
 * Live filesystem model
 * ---------------------
 *
 * SnapRAID is not a real-time RAID system. A successful sync establishes a
 * synchronized point in filesystem history, but data files remain writable
 * before, during and after the operation.
 *
 * Sync does not require every file to remain unchanged for its whole duration
 * and does not provide a transactional or atomic filesystem view.
 *
 * A file may therefore change while it is being processed. Such a change has
 * essentially the same effect as changing the file immediately after it was
 * synchronized: the parity generated by the current sync may describe an
 * older or intermediate version while the live filesystem already contains a
 * newer one.
 *
 * This is expected. Parity that no longer matches a file modified during or
 * after sync is not by itself evidence of parity corruption. A later sync
 * brings the parity back in line with the then-current filesystem state.
 *
 * A concurrent modification does not therefore have to be detected by that
 * same sync to preserve the normal SnapRAID consistency model. If the change
 * affects metadata used by normal change detection, such as size or mtime, a
 * later scan will detect it and process the file again.
 *
 * The same applies to races around file access, including writes after open,
 * between block reads, or immediately before or after the final read. These
 * are equivalent, for consistency purposes, to modifying a file immediately
 * after synchronization.
 *
 * Metadata checks performed during sync are therefore opportunistic checks,
 * not a mechanism for freezing the filesystem or detecting every concurrent
 * modification.
 *
 * This model relies on later changes being visible to normal change detection.
 * An in-place modification that preserves all relevant metadata, for example
 * both file size and modification time, may not be detected by a later scan.
 * This is a general limitation of metadata-based change detection, not a
 * special property of modifications occurring during sync.
 */

/****************************************************************************/
/* hash */

static const char* es(int err)
{
	if (is_hw(err))
		return "error_io";
	else
		return "error";
}

static int state_hash_process(struct snapraid_state* state, block_off_t blockstart, block_off_t blockmax, int* skip_sync)
{
	struct snapraid_handle* handle;
	unsigned diskmax;
	block_off_t blockcur;
	unsigned j;
	void* buffer;
	void* buffer_alloc;
	data_off_t countsize;
	block_off_t countpos;
	block_off_t countmax;
	int ret;
	unsigned soft_error;
	unsigned silent_error;
	unsigned io_error;
	/* maps the disks to handles */
	handle = handle_mapping(state, &diskmax);

	/* buffer for reading */
	buffer = malloc_nofail_direct(state->block_size, &buffer_alloc);
	if (!state->opt.skip_self)
		mtest_vector(1, state->block_size, &buffer);

	soft_error = 0;
	silent_error = 0;
	io_error = 0;

	/* first count the number of blocks to process */
	countmax = 0;
	for (j = 0; j < diskmax; ++j) {
		struct snapraid_disk* disk = handle[j].disk;

		/* if no disk, nothing to check */
		if (!disk)
			continue;

		for (blockcur = blockstart; blockcur < blockmax; ++blockcur) {
			struct snapraid_block* block;
			unsigned block_state;

			block = fs_par2block_find(disk, blockcur);

			/* get the state of the block */
			block_state = block_state_get(block);

			/* process REP and CHG blocks */
			if (block_state != BLOCK_STATE_REP && block_state != BLOCK_STATE_CHG)
				continue;

			++countmax;
		}
	}

	/* drop until now */
	state_usage_waste(state);

	countsize = 0;
	countpos = 0;
	blockcur = blockstart;

	int alert = state_progress_begin(state, blockstart, blockmax, countmax);
	if (alert > 0)
		goto end;
	if (alert < 0)
		goto bail;

	for (j = 0; j < diskmax; ++j) {
		struct snapraid_disk* disk = handle[j].disk;

		/* if no disk, nothing to check */
		if (!disk)
			continue;

		for (blockcur = blockstart; blockcur < blockmax; ++blockcur) {
			snapraid_info info;
			int rehash;
			struct snapraid_block* block;
			ssize_t read_size;
			unsigned char hash[HASH_MAX];
			unsigned block_state;
			struct snapraid_file* file;
			block_off_t file_pos;

			block = fs_par2block_find(disk, blockcur);

			/* get the state of the block */
			block_state = block_state_get(block);

			/* process REP and CHG blocks */
			if (block_state != BLOCK_STATE_REP && block_state != BLOCK_STATE_CHG)
				continue;

			/* get the file of this block */
			file = fs_par2file_get(disk, blockcur, &file_pos);

			/* get block specific info */
			info = info_get(&state->infoarr, blockcur);

			/* if we have to use the old hash */
			rehash = info_get_rehash(info);

			/* until now is misc */
			state_usage_misc(state);

			/* if the file is different than the current one, close it */
			if (handle[j].file != 0 && handle[j].file != file) {
				/* keep a pointer at the file we are going to close for error reporting */
				struct snapraid_file* report = handle[j].file;
				ret = handle_close(&handle[j]);
				if (ret == -1) {
					/* LCOV_EXCL_START */
					/*
					 * This one is really an unexpected error, because we are only reading
					 * and closing a descriptor should never fail
					 */
					log_tag("%s:%" PRIu64 ":%s:%s: Close error. %s.\n", es(errno), blockcur, disk->name, esc_tag(report->sub), strerror(errno));
					log_fatal_errno(errno, disk->name);
					log_fatal(errno, "Stopping at block %" PRIu64 "\n", blockcur);

					if (is_hw(errno)) {
						++io_error;
					} else {
						++soft_error;
					}
					goto bail;
					/* LCOV_EXCL_STOP */
				}
			}

			ret = handle_open(&handle[j], file, state->file_mode, 0);
			if (ret == -1) {
				log_tag("%s:%" PRIu64 ":%s:%s: Open error. %s.\n", es(errno), blockcur, disk->name, esc_tag(file->sub), strerror(errno));
				if (errno == ENOENT) {
					log_error_errno(errno, disk->name);

					++soft_error;
					/*
					 * If the file is missing, it means that it was removed during sync
					 * this isn't a serious error, so we skip this block, and continue with others
					 */
					continue;
				}

				if (errno == EACCES) {
					log_error_errno(errno, disk->name);

					++soft_error;
					/* this isn't a serious error, so we skip this block, and continue with others */
					continue;
				}

				/* LCOV_EXCL_START */
				log_fatal_errno(errno, disk->name);

				if (is_hw(errno)) {
					log_fatal(errno, "Stopping at block %" PRIu64 "\n", blockcur);
					++io_error;
				} else {
					log_fatal(errno, "Stopping to allow recovery. Try with 'snapraid check -f /%s'\n", fmt_poll(disk, file->sub));
					++soft_error;
				}
				goto bail;
				/* LCOV_EXCL_STOP */
			}

			/*
			 * Check if the file changed between scan and open. Stat info is cached
			 * on open; further changes will be detected in the next sync by mtime.
			 */
			if (handle[j].st.st_size != file->size
				|| handle[j].st.st_mtime != file->mtime_sec
				|| STAT_NSEC(&handle[j].st) != file->mtime_nsec
				|| (handle[j].st.st_ino != INODE_INVALID && file->inode != INODE_INVALID && handle[j].st.st_ino != file->inode)
			) {
				if (handle[j].st.st_size != file->size) {
					log_tag("error:%" PRIu64 ":%s:%s: Unexpected size change\n", blockcur, disk->name, esc_tag(file->sub));
					log_error(ESOFT, "Unexpected size change at file '%s' from %" PRIu64 " to %" PRIu64 ".\n", handle[j].path, file->size, (uint64_t)handle[j].st.st_size);
				} else if (handle[j].st.st_mtime != file->mtime_sec
					|| STAT_NSEC(&handle[j].st) != file->mtime_nsec) {
					log_tag("error:%" PRIu64 ":%s:%s: Unexpected time change\n", blockcur, disk->name, esc_tag(file->sub));
					log_error(ESOFT, "Unexpected time change at file '%s' from %" PRIu64 ".%d to %" PRIu64 ".%d.\n", handle[j].path, file->mtime_sec, file->mtime_nsec, (uint64_t)handle[j].st.st_mtime, STAT_NSEC(&handle[j].st));
				} else {
					log_tag("error:%" PRIu64 ":%s:%s: Unexpected inode change\n", blockcur, disk->name, esc_tag(file->sub));
					log_error(ESOFT, "Unexpected inode change from %" PRIu64 " to %" PRIu64 " at file '%s'.\n", file->inode, (uint64_t)handle[j].st.st_ino, handle[j].path);
				}
				log_error_errno(ENOENT, disk->name); /* same message for ENOENT */

				++soft_error;

				/*
				 * File changed between scan and open; this isn't a serious error,
				 * so we skip this block, and continue with others
				 */
				continue;
			}

			read_size = handle_read(&handle[j], file_pos, buffer, state->block_size, 0);
			if (read_size == -1) {
				/* LCOV_EXCL_START */
				log_tag("%s:%" PRIu64 ":%s:%s: Read error at position %" PRIu64 ". %s.\n", es(errno), blockcur, disk->name, esc_tag(file->sub), file_pos, strerror(errno));
				log_fatal_errno(errno, disk->name);

				if (is_hw(errno)) {
					log_fatal(errno, "Stopping at block %" PRIu64 "\n", blockcur);
					++io_error;
				} else {
					log_fatal(errno, "Stopping to allow recovery. Try with 'snapraid check -f /%s'\n", fmt_poll(disk, file->sub));
					++soft_error;
				}
				goto bail;
				/* LCOV_EXCL_STOP */
			}

			/* until now is disk */
			state_usage_disk(state, handle, &j, 1);

			state_usage_file(state, disk, file);

			countsize += read_size;

			/* now compute the hash */
			if (rehash) {
				memhash_block(state->prevhash, state->prevhashseed, hash, buffer, read_size, state->block_size);
			} else {
				memhash_block(state->hash, state->hashseed, hash, buffer, read_size, state->block_size);
			}

			/* until now is hash */
			state_usage_hash(state);

			if (block_state == BLOCK_STATE_REP) {
				/* compare the hash */
				if (memcmp(hash, block->hash, BLOCK_HASH_SIZE) != 0) {
					log_tag("error_data:%" PRIu64 ":%s:%s: Unexpected data change\n", blockcur, disk->name, esc_tag(file->sub));
					log_error(EDATA, "Data change at file '%s' at position '%" PRIu64 "'\n", handle[j].path, file_pos);
					log_error(EDATA, "WARNING! Unexpected data modification of a file without parity!\n");

					if (file_flag_has(file, FILE_IS_COPY)) {
						log_error(EDATA, "This file was detected as a copy of another file with the same name, size,\n");
						log_error(EDATA, "and timestamp, but the file data isn't matching the assumed copy.\n");
						log_error(EDATA, "If this is a false positive, and the files are expected to be different,\n");
						log_error(EDATA, "you can 'sync' anyway using 'snapraid --force-nocopy sync'\n");
					} else {
						log_error(EDATA, "Try removing the file from the array and rerun the 'sync' command!\n");
					}

					++silent_error;
					continue;
				}
			} else {
				/* the only other case is BLOCK_STATE_CHG */
				assert(block_state == BLOCK_STATE_CHG);

				/*
				 * Prehash is a persistent state transition: CHG -> REP records that
				 * new data has been read and hashed while parity remains invalid.
				 *
				 * Persisting this state before parity writes preserves verified hashes
				 * across interruptions without claiming synchronization.
				 */

				/* copy the hash in the block */
				memcpy(block->hash, hash, BLOCK_HASH_SIZE);

				/* and mark the block as hashed */
				block_state_set(block, BLOCK_STATE_REP);

				/* mark the state as needing write */
				state->need_write = 1;
			}

			/* count the number of processed block */
			++countpos;

			/* progress */
			alert = state_progress(state, 0, blockcur, countpos, countmax, countsize);
			if (alert != 0) {
				/* LCOV_EXCL_START */
				*skip_sync = 1; /* avoid to run the next sync due user interruption */
				break;
				/* LCOV_EXCL_STOP */
			}
		}

		/* close the last file in the disk */
		if (handle[j].file != 0) {
			/* keep a pointer at the file we are going to close for error reporting */
			struct snapraid_file* report = handle[j].file;
			ret = handle_close(&handle[j]);
			if (ret == -1) {
				/* LCOV_EXCL_START */
				/*
				 * This one is really an unexpected error, because we are only reading
				 * and closing a descriptor should never fail
				 */
				log_tag("%s:%" PRIu64 ":%s:%s: Close error. %s.\n", es(errno), blockmax, disk->name, esc_tag(report->sub), strerror(errno));
				log_fatal_errno(errno, disk->name);
				log_fatal(errno, "Stopping at block %" PRIu64 "\n", blockmax);

				if (is_hw(errno)) {
					++io_error;
				} else {
					++soft_error;
				}
				goto bail;
				/* LCOV_EXCL_STOP */
			}
		}

		if (*skip_sync)
			break;
	}

end:
	state_progress_end(state, countpos, countmax, countsize, "Nothing to hash.\n");

	/*
	 * Note that at this point no io_error is possible
	 * because at the first one we bail out
	 */
	assert(io_error == 0);

	if (soft_error || io_error || silent_error) {
		msg_status("\n");
		msg_status("%8u soft errors\n", soft_error);
		msg_status("%8u io errors\n", io_error);
		msg_status("%8u data errors\n", silent_error);
	}

	if (soft_error)
		log_fatal(ESOFT, "WARNING! Unexpected soft errors!\n");

	log_tag("hash_summary:error_soft:%u\n", soft_error);

	/* proceed without bailing out */
	goto finish;

bail:
	/* close files left open */
	for (j = 0; j < diskmax; ++j) {
		struct snapraid_file* file = handle[j].file;
		struct snapraid_disk* disk = handle[j].disk;
		ret = handle_close(&handle[j]);
		if (ret == -1) {
			/* LCOV_EXCL_START */
			/*
			 * If handle_close fails, the handle was open (f != -1), which
			 * guarantees that both file and disk pointers are valid.
			 */
			log_tag("%s:%" PRIu64 ":%s:%s: Close error. %s.\n", es(errno), blockcur, disk->name, esc_tag(file->sub), strerror(errno));
			log_fatal_errno(errno, disk->name);

			if (is_hw(errno)) {
				++io_error;
			} else {
				++soft_error;
			}
			/* continue, as we are already exiting */
			/* LCOV_EXCL_STOP */
		}
	}

finish:
	free(handle);
	free(buffer_alloc);

	if (soft_error + io_error + silent_error != 0)
		return -1;

	if (alert != 0)
		return -1;

	return 0;
}

/****************************************************************************/
/* sync */

/**
 * Sync plan to use.
 */
struct snapraid_plan {
	unsigned handle_max;
	struct snapraid_handle* handle_map;
	int force_full;
};

/**
 * A block that failed the hash check, or that was deleted.
 */
struct failed_struct {
	unsigned index; /**< Index of the failed block. */
	size_t size; /**< Size of the block. */

	struct snapraid_block* block; /**< The failed block, or BLOCK_DELETED for a deleted block */
};

/**
 * Comparison function for sorting by index.
 */
int failed_compare_by_index(const void* void_a, const void* void_b)
{
	const struct failed_struct* a = void_a;
	const struct failed_struct* b = void_b;

	if (a->index < b->index)
		return -1;
	if (a->index > b->index)
		return 1;
	return 0;
}

/**
 * Buffer for storing the new hashes.
 */
struct snapraid_rehash {
	unsigned char hash[HASH_MAX];
	struct snapraid_block* block;
};

/**
 * Check if we have to process the specified block index ::i.
 */
static int block_is_enabled(struct snapraid_plan* plan, block_off_t i)
{
	unsigned j;
	int one_invalid;
	int one_valid;

	/* for each disk */
	one_invalid = 0;
	one_valid = 0;
	for (j = 0; j < plan->handle_max; ++j) {
		struct snapraid_block* block;
		struct snapraid_disk* disk = plan->handle_map[j].disk;

		/* if no disk, nothing to check */
		if (!disk)
			continue;

		block = fs_par2block_find(disk, i);

		if (block_has_file(block))
			one_valid = 1;

		if (block_has_invalid_parity(block) || plan->force_full)
			one_invalid = 1;
	}

	/*
	 * A parity position needs processing only if it contains both:
	 * - at least one live data block, and
	 * - at least one block with invalid parity.
	 *
	 * Positions with only EMPTY/DELETED blocks are skipped even if parity
	 * contains old non-zero data: no live file depends on it. Reused positions
	 * become CHG/REP, forcing parity regeneration before becoming BLK.
	 *
	 * Requiring one_valid also avoids rewriting unused parity that will be
	 * removed by parity truncation.
	 */
	if (!one_invalid || !one_valid)
		return 0;

	return 1;
}

static void sync_data_reader(struct snapraid_worker* worker, struct snapraid_task* task)
{
	struct snapraid_io* io = worker->io;
	struct snapraid_state* state = io->state;
	struct snapraid_handle* handle = worker->handle;
	struct snapraid_disk* disk = handle->disk;
	block_off_t blockcur = task->position;
	unsigned char* buffer = task->buffer;
	int ret;
	/* if the disk position is not used */
	if (!disk) {
		/* use an empty block */
		memset(buffer, 0, state->block_size);
		task->state = TASK_STATE_DONE;
		return;
	}

	/* get the block */
	task->block = fs_par2block_find(disk, blockcur);

	/*
	 * If the block has no file, meaning that it's EMPTY or DELETED,
	 * it doesn't participate in the new parity computation
	 */
	if (!block_has_file(task->block)) {
		/* use an empty block */
		memset(buffer, 0, state->block_size);
		task->state = TASK_STATE_DONE;
		return;
	}

	/* get the file of this block */
	task->file = fs_par2file_get(disk, blockcur, &task->file_pos);

	/* if the file is different than the current one, close it */
	if (handle->file != 0 && handle->file != task->file) {
		/* keep a pointer at the file we are going to close for error reporting */
		struct snapraid_file* report = handle->file;
		ret = handle_close(handle);
		if (ret == -1) {
			/* LCOV_EXCL_START */
			/*
			 * This one is really an unexpected error, because we are only reading
			 * and closing a descriptor should never fail
			 */
			log_tag("%s:%" PRIu64 ":%s:%s: Close error. %s.\n", es(errno), blockcur, disk->name, esc_tag(report->sub), strerror(errno));
			log_fatal_errno(errno, disk->name);
			log_fatal(errno, "Stopping at block %" PRIu64 "\n", blockcur);

			if (is_hw(errno)) {
				task->state = TASK_STATE_IOERROR;
			} else {
				task->state = TASK_STATE_ERROR;
			}
			return;
			/* LCOV_EXCL_STOP */
		}
	}

	ret = handle_open(handle, task->file, state->file_mode, 0);
	if (ret == -1) {
		log_tag("%s:%" PRIu64 ":%s:%s: Open error. %s.\n", es(errno), blockcur, disk->name, esc_tag(task->file->sub), strerror(errno));
		if (errno == ENOENT) {
			log_error_errno(errno, disk->name);

			/*
			 * If the file is missing, it means that it was removed during sync
			 * this isn't a serious error, so we skip this block, and continue with others
			 */
			task->state = TASK_STATE_ERROR_CONTINUE;
			return;
		}

		if (errno == EACCES) {
			log_error_errno(errno, disk->name);

			/* this isn't a serious error, so we skip this block, and continue with others */
			task->state = TASK_STATE_ERROR_CONTINUE;
			return;
		}

		/* LCOV_EXCL_START */
		log_fatal_errno(errno, disk->name);

		if (is_hw(errno)) {
			log_fatal(errno, "Stopping at block %" PRIu64 "\n", blockcur);
			task->state = TASK_STATE_IOERROR;
		} else {
			log_fatal(errno, "Stopping to allow recovery. Try with 'snapraid check -f /%s'\n", fmt_poll(disk, task->file->sub));
			task->state = TASK_STATE_ERROR;
		}
		return;
		/* LCOV_EXCL_STOP */
	}

	/*
	 * Check if the file changed between scan and open. Stat info is cached
	 * on open; further changes will be detected in the next sync by mtime.
	 */
	if (handle->st.st_size != task->file->size
		|| handle->st.st_mtime != task->file->mtime_sec
		|| STAT_NSEC(&handle->st) != task->file->mtime_nsec
		|| (handle->st.st_ino != INODE_INVALID && task->file->inode != INODE_INVALID && handle->st.st_ino != task->file->inode)
	) {
		log_tag("error:%" PRIu64 ":%s:%s: Unexpected attribute change\n", blockcur, disk->name, esc_tag(task->file->sub));
		if (handle->st.st_size != task->file->size) {
			log_error(ESOFT, "Unexpected size change at file '%s' from %" PRIu64 " to %" PRIu64 ".\n", handle->path, task->file->size, (uint64_t)handle->st.st_size);
		} else if (handle->st.st_mtime != task->file->mtime_sec
			|| STAT_NSEC(&handle->st) != task->file->mtime_nsec) {
			log_error(ESOFT, "Unexpected time change at file '%s' from %" PRIu64 ".%d to %" PRIu64 ".%d.\n", handle->path, task->file->mtime_sec, task->file->mtime_nsec, (uint64_t)handle->st.st_mtime, STAT_NSEC(&handle->st));
		} else {
			log_error(ESOFT, "Unexpected inode change from %" PRIu64 " to %" PRIu64 " at file '%s'.\n", task->file->inode, (uint64_t)handle->st.st_ino, handle->path);
		}
		log_error_errno(ENOENT, disk->name); /* same message for ENOENT */

		/*
		 * File changed between scan and open; this isn't a serious error,
		 * so we skip this block, and continue with others
		 */
		task->state = TASK_STATE_ERROR_CONTINUE;
		return;
	}

	task->read_size = handle_read(handle, task->file_pos, buffer, state->block_size, 0);
	if (task->read_size == -1) {
		/* LCOV_EXCL_START */
		log_tag("%s:%" PRIu64 ":%s:%s: Read error at position %" PRIu64 ". %s.\n", es(errno), blockcur, disk->name, esc_tag(task->file->sub), task->file_pos, strerror(errno));

		if (is_hw(errno)) {
			log_fatal_errno(errno, disk->name);
			/* continue until the error limit is reached */
			task->state = TASK_STATE_IOERROR_CONTINUE;
		} else {
			log_fatal_errno(errno, disk->name);
			log_fatal(errno, "Stopping to allow recovery. Try with 'snapraid check -f /%s'\n", fmt_poll(disk, task->file->sub));
			task->state = TASK_STATE_ERROR;
		}
		return;
		/* LCOV_EXCL_STOP */
	}

	/* store the path of the opened file */
	pathcpy(task->path, sizeof(task->path), handle->path);

	task->state = TASK_STATE_DONE;
}

static void sync_parity_writer(struct snapraid_worker* worker, struct snapraid_task* task)
{
	struct snapraid_io* io = worker->io;
	struct snapraid_state* state = io->state;
	struct snapraid_parity_handle* parity_handle = worker->parity_handle;
	unsigned level = parity_handle->level;
	block_off_t blockcur = task->position;
	unsigned char* buffer = task->buffer;
	int ret;

	/* write parity */
	ret = parity_write(parity_handle, blockcur, buffer, state->block_size);
	if (ret == -1) {
		/* LCOV_EXCL_START */
		log_tag("parity_%s:%" PRIu64 ":%s: Write error. %s.\n", es(errno), blockcur, lev_config_name(level), strerror(errno));

		if (is_hw(errno)) {
			log_fatal_errno(errno, lev_config_name(level));
			task->state = TASK_STATE_IOERROR;
		} else {
			log_fatal_errno(errno, lev_config_name(level));
			log_fatal(errno, "Stopping at block %" PRIu64 "\n", blockcur);
			task->state = TASK_STATE_ERROR;
		}
		return;
		/* LCOV_EXCL_STOP */
	}

	task->state = TASK_STATE_DONE;
}

static int state_sync_process(struct snapraid_state* state, struct snapraid_parity_handle* parity_handle, block_off_t blockstart, block_off_t blockmax)
{
	struct snapraid_io io;
	struct snapraid_plan plan;
	struct snapraid_handle* handle;
	void* rehandle_alloc;
	struct snapraid_rehash* rehandle;
	unsigned diskmax;
	block_off_t blockcur;
	unsigned j;
	void* zero_alloc;
	void** zero;
	void* copy_alloc;
	void** copy;
	unsigned buffermax;
	data_off_t countsize;
	block_off_t countpos;
	block_off_t countmax;
	block_off_t autosavedone;
	block_off_t autosavelimit;
	block_off_t autosavemissing;
	int ret;
	unsigned soft_error;
	unsigned silent_error;
	unsigned io_error;
	time_t now;
	struct failed_struct* failed;
	int* failed_map;
	unsigned l;
	unsigned* waiting_map;
	unsigned waiting_mac;
	bit_vect_t* block_enabled;

	/* get the present time */
	now = time(0);

	/* maps the disks to handles */
	handle = handle_mapping(state, &diskmax);

	/* rehash buffers */
	rehandle = malloc_nofail_align(diskmax * sizeof(struct snapraid_rehash), &rehandle_alloc);

	/* we need 1 * data + 1 * parity */
	buffermax = diskmax + state->level;

	/* initialize the io threads */
	io_init(&io, state, state->opt.io_cache, buffermax, handle, diskmax, 0, sync_data_reader, 0, parity_handle, state->level, 0, 0, sync_parity_writer);

	/* allocate the copy buffer */
	copy = malloc_nofail_vector_align(diskmax, state->block_size, &copy_alloc);

	/* allocate and fill the zero buffer */
	zero = malloc_nofail_align(state->block_size, &zero_alloc);
	memset(zero, 0, state->block_size);
	raid_zero(zero);

	failed = nalloc_nofail(diskmax, sizeof(struct failed_struct));
	failed_map = nalloc_nofail(diskmax, sizeof(unsigned));

	/* possibly waiting disks */
	waiting_mac = diskmax > RAID_PARITY_MAX ? diskmax : RAID_PARITY_MAX;
	waiting_map = nalloc_nofail(waiting_mac, sizeof(unsigned));

	soft_error = 0;
	silent_error = 0;
	io_error = 0;

	msg_progress("Selecting...\n");

	/* first count the number of blocks to process */
	countmax = 0;
	plan.handle_max = diskmax;
	plan.handle_map = handle;
	plan.force_full = state->opt.force_full;
	block_enabled = calloc_nofail(1, bit_vect_size(blockmax)); /* preinitialize to 0 */
	for (blockcur = blockstart; blockcur < blockmax; ++blockcur) {
		if (!block_is_enabled(&plan, blockcur))
			continue;
		bit_vect_set(block_enabled, blockcur);
		++countmax;
	}

	/*
	 * Compute the autosave size for all disk, even if not read
	 * this makes sense because the speed should be almost the same
	 * if the disks are read in parallel
	 */
	autosavelimit = state->autosave / (diskmax * state->block_size);
	autosavemissing = countmax; /* blocks to do */
	autosavedone = 0; /* blocks done */

	/* drop until now */
	state_usage_waste(state);

	countsize = 0;
	countpos = 0;
	blockcur = blockstart;

	msg_progress("Syncing...\n");

	/* start all the worker threads */
	io_start(&io, blockstart, blockmax, block_enabled);

	int alert = state_progress_begin(state, blockstart, blockmax, countmax);
	if (alert > 0)
		goto end;
	if (alert < 0)
		goto bail;

	while (1) {
		unsigned failed_count;
		int error_on_this_block;
		int silent_error_on_this_block;
		int io_error_on_this_block;
		int fixed_error_on_this_block;
		int parity_needs_to_be_updated;
		int parity_going_to_be_updated;
		snapraid_info info;
		int rehash;
		void** buffer;
		int writer_error[IO_WRITER_ERROR_MAX];

		/* go to the next block */
		blockcur = io_read_next(&io, &buffer);
		if (blockcur >= blockmax)
			break;

		/* until now is scheduling */
		state_usage_sched(state);

		/* one more block processed for autosave */
		++autosavedone;
		--autosavemissing;

		/* by default process the block, and skip it if something goes wrong */
		error_on_this_block = 0;
		silent_error_on_this_block = 0;
		io_error_on_this_block = 0;
		fixed_error_on_this_block = 0;

		/* keep track of the number of failed blocks */
		failed_count = 0;

		/* get block specific info */
		info = info_get(&state->infoarr, blockcur);

		/* if we have to use the old hash */
		rehash = info_get_rehash(info);

		/*
		 * If the parity requires to be updated
		 *
		 * It could happens that all the blocks are EMPTY/BLK and CHG but with the hash
		 * still matching because the specific CHG block was not modified.
		 * In such case, we can avoid to update parity, because it would be the same as before
		 *
		 * Note that if there is any CHG/DELETED blocks already present in the content
		 * file loaded, meaning that there are unsynced_blocks, this optimization is disabled
		 */
		parity_needs_to_be_updated = state->opt.force_full || state->opt.force_parity_update;

		/* if the parity is going to be updated */
		parity_going_to_be_updated = 0;

		/*
		 * If the block is marked as bad, we force the parity update
		 * because the bad block may be the result of a wrong parity
		 */
		if (info_get_bad(info))
			parity_needs_to_be_updated = 1;

		/* for each disk, process the block */
		for (j = 0; j < diskmax; ++j) {
			struct snapraid_task* task;
			ssize_t read_size;
			unsigned char hash[HASH_MAX];
			struct snapraid_block* block;
			unsigned block_state;
			struct snapraid_disk* disk;
			struct snapraid_file* file;
			block_off_t file_pos;
			unsigned diskcur;

			/* until now is misc */
			state_usage_misc(state);

			task = io_data_read(&io, &diskcur, waiting_map, &waiting_mac);

			/* until now is disk */
			state_usage_disk(state, handle, waiting_map, waiting_mac);

			/* get the results */
			disk = task->disk;
			block = task->block;
			file = task->file;
			file_pos = task->file_pos;
			read_size = task->read_size;

			/* by default no rehash in case of "continue" */
			rehandle[diskcur].block = 0;

			/* if the disk position is not used */
			if (!disk)
				continue;

			state_usage_file(state, disk, file);

			/* get the state of the block */
			block_state = block_state_get(block);

			/*
			 * If the block has invalid parity,
			 * we have to take care of it in case of recover
			 */
			if (block_has_invalid_parity(block)) {
				/*
				 * Store it in the failed set, because
				 * the parity may be still computed with the previous content
				 */
				failed[failed_count].index = diskcur;
				failed[failed_count].size = state->block_size;
				failed[failed_count].block = block;
				++failed_count;

				/*
				 * If the block has invalid parity, we have to update the parity
				 * to include this block change
				 * This also apply to CHG blocks, but we are going to handle
				 * later this case to do the updates only if really needed
				 */
				if (block_state != BLOCK_STATE_CHG)
					parity_needs_to_be_updated = 1;

				/*
				 * Note that DELETE blocks are skipped in the next check
				 * and we have to store them in the failed blocks
				 * before skipping
				 */

				/* follow */
			}

			/* if the block is not used */
			if (!block_has_file(block))
				continue;

			/* handle error conditions */
			if (task->state == TASK_STATE_IOERROR) {
				/* LCOV_EXCL_START */
				++io_error;
				goto bail;
				/* LCOV_EXCL_STOP */
			}
			if (task->state == TASK_STATE_ERROR) {
				/* LCOV_EXCL_START */
				++soft_error;
				goto bail;
				/* LCOV_EXCL_STOP */
			}
			if (task->state == TASK_STATE_ERROR_CONTINUE) {
				++soft_error;
				error_on_this_block = 1;
				continue;
			}
			if (task->state == TASK_STATE_IOERROR_CONTINUE) {
				++io_error;
				if (io_error >= state->opt.io_error_limit) {
					/* LCOV_EXCL_START */
					log_fatal(EIO, "DANGER! Too many input/output errors in the %s disk. It isn't possible to continue.\n", disk->dir);
					log_fatal(EIO, "Stopping at block %" PRIu64 "\n", blockcur);
					goto bail;
					/* LCOV_EXCL_STOP */
				}

				/* otherwise continue */
				io_error_on_this_block = 1;
				continue;
			}
			if (task->state != TASK_STATE_DONE) {
				/* LCOV_EXCL_START */
				log_fatal(EINTERNAL, "Internal inconsistency in task state\n");
				os_abort();
				/* LCOV_EXCL_STOP */
			}

			countsize += read_size;

			/* now compute the hash */
			if (rehash) {
				memhash_block(state->prevhash, state->prevhashseed, hash, buffer[diskcur], read_size, state->block_size);

				/* compute the new hash, and store it */
				rehandle[diskcur].block = block;
				memhash_block(state->hash, state->hashseed, rehandle[diskcur].hash, buffer[diskcur], read_size, state->block_size);
			} else {
				memhash_block(state->hash, state->hashseed, hash, buffer[diskcur], read_size, state->block_size);
			}

			/* until now is hash */
			state_usage_hash(state);

			if (block_has_updated_hash(block)) {
				/* compare the hash */
				if (memcmp(hash, block->hash, BLOCK_HASH_SIZE) != 0) {
					/* if the file has invalid parity, it's a REP changed during the sync */
					if (block_has_invalid_parity(block)) {
						log_tag("error:%" PRIu64 ":%s:%s: Unexpected data change\n", blockcur, disk->name, esc_tag(file->sub));
						log_error(ESOFT, "Data change at file '%s' at position '%" PRIu64 "'\n", task->path, file_pos);
						log_error(ESOFT, "WARNING! Unexpected data modification of a file without parity!\n");

						if (file_flag_has(file, FILE_IS_COPY)) {
							log_error(ESOFT, "This file was detected as a copy of another file with the same name, size,\n");
							log_error(ESOFT, "and timestamp, but the file data isn't matching the assumed copy.\n");
							log_error(ESOFT, "If this is a false positive, and the files are expected to be different,\n");
							log_error(ESOFT, "you can 'sync' anyway using 'snapraid --force-nocopy sync'\n");
						} else {
							log_error(ESOFT, "Try removing the file from the array and rerun the 'sync' command!\n");
						}

						++soft_error;

						/*
						 * If the file is changed, it means that it was modified during sync
						 * this isn't a serious error, so we skip this block, and continue with others
						 */
						error_on_this_block = 1;
						continue;
					} else { /* otherwise it's a BLK with silent error */
						unsigned diff = memdiff(hash, block->hash, BLOCK_HASH_SIZE);
						log_tag("error_data:%" PRIu64 ":%s:%s: Data error at position %" PRIu64 ", diff hash bits %u/%zu\n", blockcur, disk->name, esc_tag(file->sub), file_pos, diff, BLOCK_HASH_SIZE * 8);
						log_error(EDATA, "Data error in file '%s' at position '%" PRIu64 "', diff hash bits %u/%zu\n", task->path, file_pos, diff, BLOCK_HASH_SIZE * 8);

						/* save the failed block for the fix */
						failed[failed_count].index = diskcur;
						failed[failed_count].size = read_size;
						failed[failed_count].block = block;
						++failed_count;

						/*
						 * Silent errors are very rare, and are not a signal that a disk
						 * is going to fail. So, we just continue marking the block as bad
						 * just like in scrub
						 */
						++silent_error;
						silent_error_on_this_block = 1;
						continue;
					}
				}
			} else {
				/* if until now the parity doesn't need to be updated */
				if (!parity_needs_to_be_updated) {
					/*
					 * For sure it's a CHG block, because EMPTY are processed before with "continue"
					 * and BLK and REP have "block_has_updated_hash()" as 1, and all the others
					 * have "parity_needs_to_be_updated" already at 1
					 */
					assert(block_state_get(block) == BLOCK_STATE_CHG);

					/*
					 * When a sync is interrupted, the state of the parity is unknown becase
					 * we don't know exactly where the process stopped.
					 *
					 * This means that the hash information of the OLD blocks stored in the
					 * content file for CHG/DELETED blocks may be correct or not.
					 *
					 * The sync process uses the hash of CHG blocks to decide if the parity has to be
					 * recomputed, avoiding the recomputation if the input data is the same as before.
					 * But in case of an interrupted sync we cannot trust this data, so we
					 * disable this optimization if there are unsynced blocks.
					 *
					 * Note that CHG blocks may be from reading the content file, or from
					 * scanning the disk for really changed file, like with a different timestamp.
					 *
					 * An example for CHG blocks is:
					 * - One file is added creating a CHG block with ZERO state
					 * - Sync aborted after updating the parity to the new state,
					 *   but without saving the content file representing this new BLK state.
					 * - File is now deleted after the aborted sync
					 * - Sync again, deleting the blocks over the CHG ones
					 *   with the hash of CHG blocks not representing the real parity state
					 *
					 * An example for DELETED blocks is:
					 * - One file is deleted creating DELETED blocks
					 * - Sync aborted after, updating the parity to the new state,
					 *   but without saving the content file representing this new EMPTY state.
					 * - Another file is added again over the DELETE ones
					 *   with the hash of DELETED blocks not representing the real parity state
					 */

					/* if the previous sync was completed and the hash represents the data unequivocally */
					if (state->unsynced_blocks == 0 && hash_is_unique(block->hash)) {
						/* check if the hash is changed */
						if (memcmp(hash, block->hash, BLOCK_HASH_SIZE) != 0) {
							/* the block is different, and we must update parity */
							parity_needs_to_be_updated = 1;
						}
					} else {
						/* if we don't know the hash, always update parity */
						parity_needs_to_be_updated = 1;
					}
				}

				/*
				 * Copy the hash in the block, but doesn't mark the block as hashed
				 * this allow in case of skipped block to do not save the failed computation
				 */
				memcpy(block->hash, hash, BLOCK_HASH_SIZE);

				/*
				 * Note that in case of rehash, this is the wrong hash,
				 * but it will be overwritten later
				 */
			}
		}

		/*
		 * On silent errors, attempt on-the-fly recovery to avoid contaminating
		 * new parity with bad data. Repaired data is kept in memory only.
		 *
		 * Recovery reconstructs the state represented by on-disk parity. For
		 * CHG/REP/DELETED blocks, current buffers may differ from on-disk parity,
		 * especially after an interrupted sync. Treating them as erasures prevents
		 * raid_rec() from trusting their current contents to recover a bad BLK.
		 *
		 * CHG blocks with known ZERO old values supply zero directly without
		 * consuming a parity equation. If an interrupted sync had already moved
		 * parity to the new CHG value, hash validation fails and recovery is
		 * discarded.
		 *
		 * After recovery, current buffers are restored for CHG/REP/DELETED:
		 * reconstructed old values must not roll back pending filesystem changes.
		 */
		if (!error_on_this_block && !io_error_on_this_block && silent_error_on_this_block) {
			unsigned failed_mac;
			int something_to_recover = 0;

			/*
			 * Sort the failed vector
			 * because with threads it may be in any order
			 * but RAID requires the indexes to be sorted
			 */
			qsort(failed, failed_count, sizeof(failed[0]), failed_compare_by_index);

			/* setup the blocks to recover */
			failed_mac = 0;
			for (j = 0; j < failed_count; ++j) {
				unsigned char* block_buffer = buffer[failed[j].index];
				unsigned char* block_copy = copy[failed[j].index];
				unsigned block_state = block_state_get(failed[j].block);

				/* we try to recover only if at least one BLK is present */
				if (block_state == BLOCK_STATE_BLK)
					something_to_recover = 1;

				/*
				 * Save a copy of the content just read
				 * that it's going to be overwritten by the recovering function
				 */
				memcpy(block_copy, block_buffer, state->block_size);

				if (block_state == BLOCK_STATE_CHG
					&& hash_is_zero(failed[j].block->hash)
				) {
					/*
					 * If the block was filled with 0, restore this state
					 * and avoid to recover it
					 */
					memset(block_buffer, 0, state->block_size);
				} else {
					/* if we have too many failures, we cannot recover */
					if (failed_mac >= state->level)
						break;

					/* otherwise it has to be recovered */
					failed_map[failed_mac++] = failed[j].index;
				}
			}

			/* if we have something to recover and enough parity */
			if (something_to_recover && j == failed_count) {
				/* until now is misc */
				state_usage_misc(state);

				/*
				 * Read the parity
				 * we are sure that parity exists because
				 * we have at least one BLK block
				 */
				for (l = 0; l < state->level; ++l) {
					ret = parity_read(&parity_handle[l], blockcur, buffer[diskmax + l], state->block_size);
					if (ret == -1) {
						/* LCOV_EXCL_START */
						log_tag("parity_%s:%" PRIu64 ":%s: Read error. %s.\n", es(errno), blockcur, lev_config_name(l), strerror(errno));
						if (is_hw(errno)) {
							log_fatal_errno(errno, lev_config_name(l));
							if (io_error >= state->opt.io_error_limit) {
								log_fatal(errno, "DANGER! Too many input/output errors in the %s disk. It isn't possible to continue.\n", lev_config_name(l));
								log_fatal(errno, "Stopping at block %" PRIu64 "\n", blockcur);
								++io_error;
								goto bail;
							}

							++io_error;
							io_error_on_this_block = 1;
							continue;
						}

						log_fatal_errno(errno, lev_config_name(l));
						log_fatal(errno, "Stopping at block %" PRIu64 "\n", blockcur);
						++soft_error;
						goto bail;
						/* LCOV_EXCL_STOP */
					}

					/* until now is parity */
					state_usage_parity(state, &l, 1);
				}

				/* if no error in parity read */
				if (!io_error_on_this_block) {
					/*
					 * Reconstruct data using on-disk parity. Unlike 'fix', this simple
					 * recovery does not handle corrupt parity.
					 *
					 * Recovered BLK data is repaired in memory only to compute clean
					 * parity without writing back to disk. The block remains bad on
					 * disk for check/fix to repair later.
					 */
					raid_rec(failed_mac, failed_map, diskmax, state->level, state->block_size, buffer);

					/* until now is raid */
					state_usage_raid(state);

					/* check the result and prepare the data */
					for (j = 0; j < failed_count; ++j) {
						unsigned char hash[HASH_MAX];
						unsigned char* block_buffer = buffer[failed[j].index];
						unsigned char* block_copy = copy[failed[j].index];
						unsigned block_state = block_state_get(failed[j].block);

						if (block_state == BLOCK_STATE_BLK) {
							size_t size = failed[j].size;

							/* compute the hash of the recovered block */
							if (rehash) {
								memhash_block(state->prevhash, state->prevhashseed, hash, block_buffer, size, state->block_size);
							} else {
								memhash_block(state->hash, state->hashseed, hash, block_buffer, size, state->block_size);
							}

							/* until now is hash */
							state_usage_hash(state);

							/* if the hash doesn't match */
							if (memcmp(hash, failed[j].block->hash, BLOCK_HASH_SIZE) != 0) {
								/* we have not recovered */
								break;
							}

							/* pad with 0 if needed */
							if (size < state->block_size)
								memset(block_buffer + size, 0, state->block_size - size);
						} else {
							/*
							 * Otherwise restore the content
							 * because we are not interested in the old state
							 * that it's recovered for CHG, REP and DELETED blocks
							 */
							memcpy(block_buffer, block_copy, state->block_size);
						}
					}

					/* if all is processed, we have fixed it */
					if (j == failed_count)
						fixed_error_on_this_block = 1;
				}
			}
		}

		/* if we have read all the data required and it's correct, proceed with the parity */
		if (!error_on_this_block && !io_error_on_this_block
			&& (!silent_error_on_this_block || fixed_error_on_this_block)
		) {
			/* update the parity only if really needed */
			if (parity_needs_to_be_updated) {
				/* compute the parity */
				raid_gen(diskmax, state->level, state->block_size, buffer, 1);

				/* until now is raid */
				state_usage_raid(state);

				/* mark that the parity is going to be written */
				parity_going_to_be_updated = 1;
			}

			/* for each disk, mark the blocks as processed */
			for (j = 0; j < diskmax; ++j) {
				struct snapraid_block* block;

				if (!handle[j].disk)
					continue;

				block = fs_par2block_find(handle[j].disk, blockcur);

				if (block == BLOCK_NULL) {
					/* nothing to do */
					continue;
				}

				/* if it's a deleted block */
				if (block_state_get(block) == BLOCK_STATE_DELETED) {
					/* the parity is now updated without this block, so it's now empty */
					fs_deallocate(handle[j].disk, blockcur);
					continue;
				}

				/* now all the blocks have the hash and the parity computed */
				block_state_set(block, BLOCK_STATE_BLK);
			}

			/*
			 * We update the info block only if we really have updated the parity
			 * because otherwise the time/justsynced info would be misleading as we didn't
			 * wrote the parity at this time
			 * we also update the info block only if no silent error was found
			 * because has no sense to refresh the time for data that we know bad
			 */
			if (parity_needs_to_be_updated
				&& !silent_error_on_this_block
			) {
				/* if rehash is needed */
				if (rehash) {
					/* store all the new hash already computed */
					for (j = 0; j < diskmax; ++j) {
						if (rehandle[j].block)
							memcpy(rehandle[j].block->hash, rehandle[j].hash, BLOCK_HASH_SIZE);
					}
				}

				/*
				 * Update the time info of the block
				 * we are also clearing any previous bad and rehash flag
				 */
				info_set(&state->infoarr, blockcur, info_make(now, 0, 0, 1));
			}
		}

		/*
		 * If a silent (even if corrected) or input/output error was found
		 * mark the block as bad to have check/fix to handle it
		 * because our correction is in memory only and not yet written
		 */
		if (silent_error_on_this_block || io_error_on_this_block) {
			/* set the error status keeping the other info */
			info_set(&state->infoarr, blockcur, info_set_bad(info));
		}

		/*
		 * Finally schedule parity write
		 * Note that the calls to io_parity_write() are mandatory
		 * even if the parity doesn't need to be updated
		 * This because we want to keep track of the time usage
		 */
		state_usage_misc(state);

		/* write start */
		io_write_preset(&io, blockcur, !parity_going_to_be_updated);

		/* write the parity */
		for (l = 0; l < state->level; ++l) {
			unsigned levcur;

			io_parity_write(&io, &levcur, waiting_map, &waiting_mac);

			/* until now is parity */
			state_usage_parity(state, waiting_map, waiting_mac);
		}

		/* write finished */
		io_write_next(&io, blockcur, !parity_going_to_be_updated, writer_error);

		/* handle errors reported */
		for (j = 0; j < IO_WRITER_ERROR_MAX; ++j) {
			if (writer_error[j]) {
				switch (j + IO_WRITER_ERROR_BASE) {
				case TASK_STATE_IOERROR_CONTINUE :
					++io_error;
					if (io_error >= state->opt.io_error_limit) {
						/* LCOV_EXCL_START */
						log_fatal(EIO, "DANGER! Too many input/output errors in a parity disk. It isn't possible to continue.\n");
						log_fatal(EIO, "Stopping at block %" PRIu64 "\n", blockcur);
						goto bail;
						/* LCOV_EXCL_STOP */
					}
					break;
				case TASK_STATE_ERROR_CONTINUE :
					++soft_error;
					break;
				case TASK_STATE_IOERROR :
					/* LCOV_EXCL_START */
					++io_error;
					goto bail;
				/* LCOV_EXCL_STOP */
				case TASK_STATE_ERROR :
					/* LCOV_EXCL_START */
					++soft_error;
					goto bail;
					/* LCOV_EXCL_STOP */
				}
			}
		}

		/* mark the state as needing write */
		state->need_write = 1;

		/* count the number of processed block */
		++countpos;

		/* progress */
		alert = state_progress(state, &io, blockcur, countpos, countmax, countsize);
		if (alert != 0) {
			/* LCOV_EXCL_START */
			break;
			/* LCOV_EXCL_STOP */
		}

		/* thermal control */
		if (state_thermal_alarm(state)) {
			/* until now is misc */
			state_usage_misc(state);

			state_progress_stop(state);

			/* before spinning down flush all the caches */
			ret = state_barrier(state, &io, parity_handle, blockcur);
			if (ret == -1) {
				/* LCOV_EXCL_START */
				log_fatal(errno, "Stopping at block %" PRIu64 "\n", blockcur);
				++io_error;
				goto bail;
				/* LCOV_EXCL_STOP */
			}

			state_thermal_cooldown(state);

			state_progress_restart(state);

			/* drop until now */
			state_usage_waste(state);
		}

		/* autosave */
		if ((state->autosave != 0
			&& autosavedone >= autosavelimit /* if we have reached the limit */
			&& autosavemissing >= autosavelimit) /* if we have at least a full step to do */
		        /* or if we have a forced autosave at the specified block */
			|| (state->opt.force_autosave_at != 0 && state->opt.force_autosave_at == blockcur)
		) {
			autosavedone = 0; /* restart the counter */

			/* until now is misc */
			state_usage_misc(state);

			state_progress_stop(state);

			msg_progress("Autosaving...\n");

			/*
			 * Before writing the new content file we ensure that
			 * the parity is really written flushing the disk cache
			 */
			ret = state_barrier(state, &io, parity_handle, blockcur);
			if (ret == -1) {
				/* LCOV_EXCL_START */
				log_fatal(EIO, "Stopping at block %" PRIu64 "\n", blockcur);
				++io_error;
				goto bail;
				/* LCOV_EXCL_STOP */
			}

			/* now we can safely write the content file */
			state_write(state);

			state_progress_restart(state);

			/* drop until now */
			state_usage_waste(state);
		}
	}

end:
	state_progress_end(state, countpos, countmax, countsize, "Nothing to sync.\n");

	/*
	 * Before returning we ensure that
	 * the parity is really written flushing the disk cache
	 */
	ret = state_barrier(state, &io, parity_handle, blockcur);
	if (ret == -1) {
		/* LCOV_EXCL_START */
		log_fatal(errno, "Stopping at block %" PRIu64 "\n", blockcur);
		++io_error;
		goto bail;
		/* LCOV_EXCL_STOP */
	}
	if (state->opt.kill_after_sync) {
		log_fatal(EUSER, "WARNING! Killing due --test-kill-after-sync option.\n");
		exit(EXIT_SUCCESS);
	}

	state_usage_print(state);

	if (soft_error || silent_error || io_error) {
		msg_status("\n");
		msg_status("%8u soft errors\n", soft_error);
		msg_status("%8u io errors\n", io_error);
		msg_status("%8u data errors\n", silent_error);
	}

	if (soft_error)
		log_fatal(ESOFT, "WARNING! Unexpected soft errors!\n");
	if (io_error)
		log_fatal(EIO, "DANGER! Unexpected input/output errors! The failing blocks are now marked as bad!\n");
	if (silent_error)
		log_fatal(EDATA, "DANGER! Unexpected silent data errors! The failing blocks are now marked as bad!\n");
	if (io_error || silent_error) {
		log_fatal(ESOFT, "Use 'snapraid status' to list the bad blocks.\n");
		log_fatal(ESOFT, "Use 'snapraid -e fix' to recover.\n");
	}

	log_tag("summary:error_soft:%u\n", soft_error);
	log_tag("summary:error_io:%u\n", io_error);
	log_tag("summary:error_data:%u\n", silent_error);
	if (soft_error + silent_error + io_error == 0)
		log_tag("summary:exit:ok\n");
	else if (silent_error + io_error == 0)
		log_tag("summary:exit:warning\n");
	else
		log_tag("summary:exit:error\n");
	log_flush();

bail:
	/* stop all the worker threads */
	io_stop(&io);

	for (j = 0; j < diskmax; ++j) {
		struct snapraid_file* file = handle[j].file;
		struct snapraid_disk* disk = handle[j].disk;
		ret = handle_close(&handle[j]);
		if (ret == -1) {
			/* LCOV_EXCL_START */
			/*
			 * If handle_close fails, the handle was open (f != -1), which
			 * guarantees that both file and disk pointers are valid.
			 */
			log_tag("%s:%" PRIu64 ":%s:%s: Close error. %s.\n", es(errno), blockcur, disk->name, esc_tag(file->sub), strerror(errno));
			log_fatal_errno(errno, disk->name);

			if (is_hw(errno)) {
				++io_error;
			} else {
				++soft_error;
			}
			/* continue, as we are already exiting */
			/* LCOV_EXCL_STOP */
		}
	}

	free(handle);
	free(zero_alloc);
	free(copy_alloc);
	free(copy);
	free(rehandle_alloc);
	free(failed);
	free(failed_map);
	free(waiting_map);
	io_done(&io);
	free(block_enabled);

	if (state->opt.expect_recoverable) {
		if (soft_error + silent_error + io_error == 0)
			return -1;
	} else {
		if (soft_error + silent_error + io_error != 0)
			return -1;
	}

	if (alert != 0)
		return -1;

	return 0;
}

/*
 * Make a forced full parity rebuild persistent.
 *
 * REBUILD means that the current data and hash are known, but physical parity
 * must be regenerated using the existing data-to-parity mapping.
 *
 * Only BLK blocks are converted. CHG, REP and DELETED already carry their own
 * unsynchronized semantics and must not be rewritten merely because
 * --force-full was requested.
 */
static void state_sync_mark_rebuild(struct snapraid_state* state)
{
	tommy_node* i;
	int modified = 0;

	for (i = state->disklist; i != 0; i = i->next) {
		struct snapraid_disk* disk = i->data;
		tommy_node* j;

		for (j = disk->filelist; j != 0; j = j->next) {
			struct snapraid_file* file = j->data;
			block_off_t f;

			for (f = 0; f < file->blockmax; ++f) {
				struct snapraid_block* block = fs_file2block_get(file, f);

				if (block_state_get(block) == BLOCK_STATE_BLK) {
					block_state_set(block, BLOCK_STATE_REBUILD);
					modified = 1;
				}
			}
		}
	}

	if (modified)
		state->need_write = 1;
}

/*
 * Initialize persistent parity split sizes resolved while opening parity.
 *
 * parity_create() resolves PARITY_SIZE_INVALID in the parity handles using
 * the current physical file size, but intentionally doesn't modify the
 * persistent parity state.
 *
 * Before writing a pre-resize recovery checkpoint, copy only these resolved
 * initial sizes into the persistent state. Existing valid sizes must be
 * preserved because they describe the previously persisted logical split
 * layout.
 */
static void state_sync_parity_size(struct snapraid_state* state, struct snapraid_parity_handle* parity_handle)
{
	unsigned l;
	unsigned s;
	int modified = 0;

	for (l = 0; l < state->level; ++l) {
		for (s = 0; s < state->parity[l].split_mac; ++s) {
			if (state->parity[l].split_map[s].size == PARITY_SIZE_INVALID) {
				state->parity[l].split_map[s].size = parity_handle[l].split_map[s].size;
				modified = 1;
			}
		}
	}

	if (modified)
		state->need_write = 1;
}

int state_sync(struct snapraid_state* state, block_off_t blockstart, block_off_t blockcount)
{
	block_off_t blockmax;
	block_off_t used_paritymax;
	data_off_t used_parity_size;
	data_off_t file_parity_size;
	data_off_t size;
	int ret;
	struct snapraid_parity_handle parity_handle[LEV_MAX];
	unsigned process_error;
	unsigned l;
	int skip_sync = 0;
	int parity_shrink = 0;

	msg_progress("Initializing...\n");

	/*
	 * Track two distinct parity extents:
	 *
	 * - allocated_size: extent required by the new layout, including live
	 *   CHG/REP blocks whose parity is not yet generated.
	 * - used_size: extent claimed valid by current content state (last BLK).
	 *
	 * Physical parity may be shorter than allocated_size (sync will extend it),
	 * but must not be shorter than used_size (which indicates missing parity).
	 */
	blockmax = parity_allocated_size(state);
	size = blockmax * (data_off_t)state->block_size;

	/* minimum size of the parity files we expect */
	used_paritymax = parity_used_size(state);
	used_parity_size = used_paritymax * (data_off_t)state->block_size;

	if (blockstart > blockmax) {
		/* LCOV_EXCL_START */
		log_fatal(EUSER, "Error in the starting block %" PRIu64 ". It is larger than the parity size %" PRIu64 ".\n", blockstart, blockmax);
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* adjust the number of block to process */
	if (blockcount != 0 && blockcount < blockmax - blockstart) {
		blockmax = blockstart + blockcount;
	}

	/* effective size of the parity files */
	file_parity_size = 0;

	for (l = 0; l < state->level; ++l) {
		data_off_t out_size;
		data_off_t current_size;
		block_off_t parityblocks;

		/* create the file and open for writing */
		ret = parity_create(&parity_handle[l], &state->parity[l], l, state->file_mode, state->block_size, state->opt.parity_limit_size);
		if (ret == -1) {
			/* LCOV_EXCL_START */
			log_tag("parity_%s:%u:%s: Create error. %s.\n", es(errno), 0, lev_config_name(l), strerror(errno));
			log_fatal_errno(errno, lev_config_name(l));
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		/*
		 * Detect a destructive logical parity shrink before changing the physical
		 * parity files.
		 *
		 * parity_size() describes the logical split layout loaded from the current
		 * content state. If the new allocated size is smaller, parity_chsize() may
		 * discard parity positions that are still referenced by the previously
		 * persisted content file.
		 *
		 * This comparison deliberately does not use physical_reach_size or physical EOF.
		 * Those describe physical I/O extent, not the logical parity layout whose
		 * crash-consistency relationship with the content file must be preserved.
		 */
		parity_size(&parity_handle[l], &current_size);
		if (size < current_size)
			parity_shrink = 1;

		/* number of block in the parity file */
		parity_physical_reach_size(&parity_handle[l], &out_size);
		parityblocks = out_size / state->block_size;

		/* if the file is too small */
		if (out_size < used_parity_size) {
			log_fatal(ESOFT, "WARNING! The %s parity has only %" PRIu64 " blocks instead of %" PRIu64 ".\n", lev_name(l), parityblocks, used_paritymax);
		}

		/* keep the smallest physical parity extent */
		if (l == 0 || file_parity_size > out_size)
			file_parity_size = out_size;
	}

	/*
	 * A full parity rebuild or reallocation from zero reconstructs truncated
	 * parity. Tail-only reallocation still relies on the existing prefix, so a
	 * truncation before parity_tail remains fatal.
	 */
	if (!state->opt.force_full && !(state->opt.force_realloc && state->opt.parity_tail == 0)) {
		/* if the parities are too small */
		if (file_parity_size < used_parity_size) {
			/* LCOV_EXCL_START */
			log_fatal(ESOFT, "DANGER! One or more the parity files are smaller than expected!\n");
			if (file_parity_size != 0) {
				log_fatal(ESOFT, "If this happens because you are using an old content file,\n");
				log_fatal(ESOFT, "you can 'sync' anyway using 'snapraid --force-full sync'\n");
				log_fatal(ESOFT, "to force a full rebuild of the parity.\n");
			} else {
				log_fatal(ESOFT, "It's possible that the parity disks are not mounted.\n");
				log_fatal(ESOFT, "If instead you are adding a new parity level, you can 'sync' using\n");
				log_fatal(ESOFT, "'snapraid --force-full sync' to force a full rebuild of the parity.\n");
			}
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}

	process_error = 0;

	if (state->opt.prehash) {
		msg_progress("Hashing...\n");

		ret = state_hash_process(state, blockstart, blockmax, &skip_sync);
		if (ret == -1) {
			/* LCOV_EXCL_START */
			++process_error;
			/* on any error do NOT proceeed with the sync */
			skip_sync = 1;
			/* LCOV_EXCL_STOP */
		}
	}

	if (!skip_sync) {
		/*
		 * Persist the logical state required to recover from a crash before a
		 * destructive physical parity change.
		 *
		 * For --force-full, BLK -> REBUILD records that the existing allocation must
		 * be preserved but its physical parity must be regenerated.
		 *
		 * For --force-realloc, state_scan() has already constructed the new
		 * data-to-parity allocation. That new mapping and its REP/CHG/DELETED states
		 * must be durable before parity_chsize() can alter the old physical layout.
		 *
		 * A normal sync needs the same ordering when the new logical parity size is
		 * smaller than the old one. A shrink may discard parity positions still
		 * referenced by the previously persisted content state. Persisting the
		 * post-scan state first guarantees that, after the physical truncation, no
		 * durable mapping still depends on the discarded tail.
		 *
		 * Non-destructive parity growth does not require this additional barrier.
		 * Newly allocated positions are represented by pending block states, and the
		 * existing post-resize content write persists the resulting split sizes before
		 * parity synchronization begins.
		 *
		 * This decision uses the logical parity size, not physical_reach_size or physical
		 * EOF. physical_reach_size is only a physical I/O extent and does not represent
		 * parity validity or the persisted logical mapping.
		 */
		if (state->opt.force_full)
			state_sync_mark_rebuild(state);

		if (state->opt.force_full || state->opt.force_realloc || parity_shrink)
			state_sync_parity_size(state, parity_handle);

		if ((state->opt.force_full
			|| state->opt.force_realloc
			|| parity_shrink)
			&& !state->opt.skip_content_write
			&& state->need_write
		) {
			state_write(state);
		}

		msg_progress("Resizing...\n");

		/* now change the size of all parities */
		for (l = 0; l < state->level; ++l) {
			int is_modified;

			/*
			 * Sync may reallocate an elastic split into following splits. Any resulting
			 * logical split mapping is persisted by the sync state-update protocol.
			 */
			ret = parity_chsize(&parity_handle[l], &state->parity[l], &is_modified, size, state->block_size, state->opt.skip_fallocate, state->opt.skip_space_holder, 1);
			if (ret == -1) {
				/* LCOV_EXCL_START */
				data_off_t out_size;
				parity_size(&parity_handle[l], &out_size);
				parity_overflow(state, out_size);
				log_fatal(errno, "WARNING! Without a usable %s file, it isn't possible to sync.\n", lev_name(l));
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			if (is_modified)
				state->need_write = 1;
		}

		if (state->opt.kill_after_resize) {
			log_fatal(EUSER, "WARNING! Killing due --test-kill-after-resize option.\n");
			exit(EXIT_SUCCESS);
		}

		/* after resizing parity files, refresh again the free info */
		state_refresh(state);

		/**
		 * Save the new state before the sync but after the hashing phase
		 *
		 * This allows to recover after an aborted sync, and at the same time
		 * it allows to recover broken copied/moved files identified in the
		 * hashing phase.
		 *
		 * For example, think at this case:
		 * - Add some files at the array
		 * - Run a sync command, it will recompute the parity adding the new files
		 * - Abort the sync command before it stores the new content file
		 * - Delete the not yet synced files from the array
		 * - Run a new sync command
		 *
		 * The sync command has no way to know that the parity file was modified
		 * because the files triggering these changes are now deleted and they aren't
		 * listed in the content file.
		 * Instead, saving the new content file in advance, keeps track of all the parity
		 * that may be modified.
		 */
		if (!state->opt.skip_content_write) {
			if (state->need_write)
				state_write(state);
		} else {
			log_fatal(EUSER, "WARNING! Skipped state write for --test-skip-content-write option.\n");
		}

		/* make the scanned state available for recovery before changing parity */
		if (state_snapshot_pending(state) != 0) {
			/* LCOV_EXCL_START */
			++process_error;
			/* continue, as we are already exiting */
			/* LCOV_EXCL_STOP */
		} else if (state->opt.kill_before_sync) {
			log_fatal(EUSER, "WARNING! Killing due --test-kill-before-sync option.\n");
			exit(EXIT_SUCCESS);
		} else if (blockstart < blockmax) {
			ret = state_sync_process(state, parity_handle, blockstart, blockmax);
			if (ret == -1) {
				/* LCOV_EXCL_START */
				++process_error;
				/* continue, as we are already exiting */
				/* LCOV_EXCL_STOP */
			}
		} else {
			msg_status("Nothing to sync.\n");
		}
	}

	for (l = 0; l < state->level; ++l) {
		ret = parity_close(&parity_handle[l]);
		if (ret == -1) {
			/* LCOV_EXCL_START */
			log_tag("parity_%s:%" PRIu64 ":%s: Close error. %s.\n", es(errno), blockmax, lev_config_name(l), strerror(errno));
			log_fatal_errno(errno, lev_config_name(l));

			++process_error;
			/* continue, as we are already exiting */
			/* LCOV_EXCL_STOP */
		}
	}

	/*
	 * On close errors, abort without committing or persisting state,
	 * keeping the previous content file intact for recovery on the next run.
	 */
	if (process_error != 0)
		return -1;

	msg_status("Everything OK\n");

	/* only a clean full sync makes all deallocation records obsolete */
	state_commit(state);

	/* persist progress and error markings only after all parity handles are closed */
	if (state->need_write || state->opt.force_content_write)
		state_write(state);

	return 0;
}

