// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2013 Andrea Mazzoleni

#include "os/portable.h"

#include "support.h"
#include "elem.h"
#include "state.h"
#include "parity.h"
#include "handle.h"
#include "io.h"
#include "raid/raid.h"

/*
 * SnapRAID scrub model
 *
 * Scrub periodically verifies data and parity without modifying either of
 * them. It reads selected parity positions, checks the data blocks against
 * their stored hashes when a current hash is available, recomputes all parity
 * levels from the data read, and compares the result with physical parity.
 *
 * The unit of scrub processing is a parity position, not an individual file
 * block. Selecting one position means considering all mapped data blocks and
 * all parity levels belonging to the same stripe.
 *
 * Scrub does modify the content state. For each processed position it records
 * when the position was last verified, maintains the persistent bad flag, and
 * can progressively migrate block hashes while a rehash is in progress.
 *
 * Verification model
 * ------------------
 *
 * The block states have different meanings for scrub:
 *
 *   State      File     Stored hash        Data verification    Parity
 *   ------------------------------------------------------------------------
 *   EMPTY      no       none               zero input           expected
 *   BLK        yes      CURRENT            hash checked         expected
 *   CHG        yes      OLD                not hash checked     unsynced
 *   REP        yes      NEW                hash checked         unsynced
 *   DELETED    no       OLD/INVALID        zero input           unsynced
 *
 * BLK and REP have a hash describing their current file data and can therefore
 * be checked directly.
 *
 * CHG is deliberately different. Its stored hash describes the OLD data, so
 * comparing the current file contents against block->hash would not validate
 * the current block. Scrub still reads the current data because it is needed
 * to recompute parity, but does not use the CHG hash as a current-data check.
 *
 * EMPTY and DELETED have no current file contribution and are supplied to the
 * RAID computation as zero. DELETED still makes the position unsynced because
 * physical parity may contain its previous contribution.
 *
 * A position is also considered unsynced when a file's current size or
 * modification time differs from the metadata recorded in the content state.
 * This does not by itself abort scrub. It changes how a later data or parity
 * mismatch is classified.
 *
 * Parity is recomputed only when every required data input was read without an
 * error and every applicable current hash matched. Without a complete and
 * trustworthy set of data inputs, a parity comparison would not identify
 * whether an error belongs to data or parity.
 *
 * Error classification model
 * --------------------------
 *
 * A hash or parity mismatch is a silent data error only when the corresponding
 * data and parity position are expected to be synchronized.
 *
 * If the file metadata differs from the content state, or any block at the
 * position is CHG, REP or DELETED, a mismatch can be the normal consequence
 * of an unsynchronized array. It is therefore reported as a soft error rather
 * than as silent corruption.
 *
 * The unsynced classification is conservative and affects only the meaning of
 * a mismatch. It does not make the position fail automatically. If the data
 * that can be checked is correct and physical parity already agrees with the
 * current logical inputs, the scrub of that position can still complete.
 *
 * I/O errors are tracked separately from logical or hash mismatches. A
 * recoverable per-position I/O failure marks the position bad; errors severe
 * enough to abort processing may terminate the command before normal
 * per-position bookkeeping is completed.
 *
 * Scrub plan model
 * ----------------
 *
 * Scrub selection is based on the per-position snapraid_info stored in the
 * content file. An info entry records:
 *
 * - the last time the position was known to be correct,
 * - whether the position is marked bad,
 * - whether its hashes still use the previous hash algorithm,
 * - whether it was just synchronized and has never been scrubbed.
 *
 * Positions without an info entry are not scrub candidates.
 *
 * Bad positions override every scrub plan and are always selected. This makes
 * the persistent bad flag self-checking: a later scrub rechecks the complete
 * position regardless of its age or the requested normal plan.
 *
 * The explicit plans otherwise select positions as follows:
 *
 *   full       all tracked positions
 *   even       even parity positions
 *   new        positions never scrubbed since synchronization
 *   bad        only positions marked bad
 *
 * The automatic plan selects the oldest positions first. The bucket list
 * groups positions by their last-known-good timestamp and is sorted from the
 * oldest timestamp to the newest.
 *
 * Selection is limited by both the requested amount and the optional age
 * limit. When the requested count ends inside a timestamp bucket, timelimit
 * identifies that bucket and lastlimit specifies how many positions from that
 * exact timestamp may be selected. block_is_enabled() is called in increasing
 * parity-position order, providing a deterministic tie break inside the
 * bucket.
 *
 * Bad positions do not consume this boundary quota: they are selected before
 * normal plan filtering and may therefore make the actual scrub count larger
 * than the requested automatic amount.
 *
 * The complete block selection is materialized before I/O starts. Autosaves
 * performed during the run may rebuild the scrub-time bucket list in the
 * content state, but they do not change the set of positions selected for the
 * current scrub.
 *
 * Bad state and persistence model
 * -------------------------------
 *
 * The bad flag belongs to the whole parity position. It does not identify a
 * specific data disk or parity level. A silent data/parity error or a
 * continuable I/O error at any contribution makes the complete position bad,
 * because scrub cannot safely reduce that observation to a single trusted
 * component.
 *
 * When a position fails with a silent or I/O error, info_set_bad() preserves
 * its previous timestamp and all other flags. In particular, a failed scrub
 * does not make the position appear recently verified and does not discard
 * pending rehash or just-synchronized state.
 *
 * A generic soft error also leaves the previous info unchanged. Such a
 * position has not completed a successful scrub and therefore retains its
 * previous scrub age.
 *
 * Only a completely successful position is replaced with:
 *
 *   info_make(now, 0, 0, 0)
 *
 * This records the new last-known-good time and clears bad, rehash and
 * just-synchronized state. Consequently, "scrub -p bad" clears a bad position
 * only after the whole position can again be verified successfully.
 *
 * Scrub writes only content metadata and hashes; it never writes user data or
 * parity. Autosave can therefore persist completed scrub positions
 * independently. A crash before the next content save may lose some scrub
 * progress, causing those positions to be checked again, but cannot leave a
 * partially repaired data/parity state because scrub performs no such repair.
 *
 * Rehash model
 * ------------
 *
 * Rehash state is also tracked per parity position. While rehash is pending,
 * hashes that can be validated are checked with the previous hash algorithm,
 * while hashes using the new algorithm are computed into temporary storage.
 *
 * The new hashes are not committed independently for each disk. They are
 * staged until the complete parity position has passed data reads, applicable
 * hash checks, and parity verification. Only then are the staged hashes copied
 * into the block state and the per-position rehash flag cleared.
 *
 * This keeps the hash generation and its info flag consistent across content
 * saves: a persisted rehash flag means the previous algorithm must still be
 * used for that position, while clearing the flag publishes the newly computed
 * hashes at the same successful scrub checkpoint.
 *
 * A CHG block has no current hash to validate. If such a position passes scrub,
 * physical parity has nevertheless been verified against the bytes just read.
 * The newly computed hash can then describe that verified parity baseline
 * using the new algorithm, while the block itself remains CHG and is still
 * conservatively treated as unsynchronized.
 *
 * Live filesystem model
 * ---------------------
 *
 * Scrub does not freeze the data filesystem. Files can be modified while the
 * command is running.
 *
 * A size or modification-time difference observed when opening a file marks
 * that file and parity position as unsynced for error classification. Scrub
 * continues reading so that it can still report the state actually observed.
 *
 * If the changed file later produces a hash or parity mismatch, the mismatch
 * is treated as a soft synchronization error rather than silent corruption.
 * If its bytes still agree with the stored hash and parity, scrub may complete
 * successfully even though the metadata difference remains for a later sync
 * to process.
 *
 * As with other metadata-based operations, a concurrent modification that
 * preserves all metadata used for change detection cannot be distinguished
 * reliably from silent corruption by metadata checks alone.
 */

/****************************************************************************/
/* scrub */

static const char* es(int err)
{
	if (is_hw(err))
		return "error_io";
	else
		return "error";
}

/**
 * Staged hashes for a rehash operation.
 *
 * New hashes are kept here until the complete parity position has passed
 * verification. They are copied into the block state only together with
 * clearing the per-position rehash flag.
 */
struct snapraid_rehash {
	unsigned char hash[HASH_MAX];
	struct snapraid_block* block;
};

/**
 * Scrub plan to use.
 *
 * AUTO selects complete buckets older than timelimit and, when the requested
 * count cuts through the boundary bucket, at most lastlimit positions whose
 * timestamp is exactly timelimit.
 */
struct snapraid_plan {
	struct snapraid_state* state;
	int plan; /**< One of the SCRUB_*. */
	time_t timelimit; /**< Time limit. Valid only with SCRUB_AUTO. */
	block_off_t lastlimit; /**< Maximum number of normal AUTO selections whose timestamp is exactly timelimit. */
};

/**
 * Check if we have to process the specified block index ::i.
 */
static int block_is_enabled(struct snapraid_plan* plan, block_off_t* countlast, block_off_t i)
{
	time_t blocktime;
	snapraid_info info;

	/* don't scrub unused blocks in all plans */
	info = info_get(&plan->state->infoarr, i);
	if (info == 0)
		return 0;

	/*
	 * Bad is an override, not a normal plan criterion.
	 *
	 * Always recheck bad positions so a successful scrub can clear the persistent
	 * bad flag even if the position would normally be excluded by age, parity,
	 * NEW, or any other plan.
	 */
	if (info_get_bad(info))
		return 1;

	switch (plan->plan) {
	case SCRUB_FULL :
		/* in 'full' plan everything is scrubbed */
		return 1;
	case SCRUB_EVEN :
		/* in 'even' plan, scrub only even blocks */
		return i % 2 == 0;
	case SCRUB_NEW :
		/* in 'new' plan, only positions never scrubbed after sync */
		return info_get_justsynced(info);
	case SCRUB_BAD :
		/* in 'bad' plan, only bad blocks (already reported) */
		return 0;
	}

	/* if it's too new */
	blocktime = info_get_time(info);
	if (blocktime > plan->timelimit) {
		/* skip it */
		return 0;
	}

	/*
	 * AUTO buckets have timestamp granularity, so the count limit may end inside
	 * a bucket shared by many positions.
	 *
	 * All positions older than timelimit are selected. At timelimit select only
	 * lastlimit normal positions; block_is_enabled() is called in ascending parity
	 * order, which provides the deterministic tie break.
	 */
	if (blocktime == plan->timelimit) {
		/* if we reached the count limit */
		if (*countlast >= plan->lastlimit) {
			/* skip it */
			return 0;
		}

		++*countlast;
	}

	return 1;
}

static void scrub_data_reader(struct snapraid_worker* worker, struct snapraid_task* task)
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

	/* if the block is not used */
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
		if (is_hw(errno)) {
			/* LCOV_EXCL_START */
			log_fatal_errno(errno, disk->name);
			log_fatal(errno, "Stopping at block %" PRIu64 "\n", blockcur);
			task->state = TASK_STATE_IOERROR;
			return;
			/* LCOV_EXCL_STOP */
		}

		task->state = TASK_STATE_ERROR_CONTINUE;
		return;
	}

	/* check if the file is changed */
	if (handle->st.st_size != task->file->size
		|| handle->st.st_mtime != task->file->mtime_sec
		|| STAT_NSEC(&handle->st) != task->file->mtime_nsec
	        /* don't check the inode to support filesystem without persistent inodes */
	) {
		/* report that the block and the file are not synced */
		task->is_timestamp_different = 1;
		/* follow */
	}

	/*
	 * Note that we intentionally don't abort if the file has different attributes
	 * from the last sync, as we are expected to return errors if running
	 * in an unsynced array. This is just like the check command.
	 */

	task->read_size = handle_read(handle, task->file_pos, buffer, state->block_size, 0);
	if (task->read_size == -1) {
		log_tag("%s:%" PRIu64 ":%s:%s: Read error at position %" PRIu64 ". %s.\n", es(errno), blockcur, disk->name, esc_tag(task->file->sub), task->file_pos, strerror(errno));
		if (is_hw(errno)) {
			/* LCOV_EXCL_START */
			log_fatal_errno(errno, disk->name);
			task->state = TASK_STATE_IOERROR_CONTINUE;
			return;
			/* LCOV_EXCL_STOP */
		}

		task->state = TASK_STATE_ERROR_CONTINUE;
		return;
	}

	/* store the path of the opened file */
	pathcpy(task->path, sizeof(task->path), handle->path);

	task->state = TASK_STATE_DONE;
}

static void scrub_parity_reader(struct snapraid_worker* worker, struct snapraid_task* task)
{
	struct snapraid_io* io = worker->io;
	struct snapraid_state* state = io->state;
	struct snapraid_parity_handle* parity_handle = worker->parity_handle;
	unsigned level = parity_handle->level;
	block_off_t blockcur = task->position;
	unsigned char* buffer = task->buffer;
	int ret;

	/* read the parity */
	ret = parity_read(parity_handle, blockcur, buffer, state->block_size);
	if (ret == -1) {
		log_tag("parity_%s:%" PRIu64 ":%s: Read error. %s.\n", es(errno), blockcur, lev_config_name(level), strerror(errno));
		if (is_hw(errno)) {
			/* LCOV_EXCL_START */
			log_fatal_errno(errno, lev_config_name(level));
			task->state = TASK_STATE_IOERROR_CONTINUE;
			return;
			/* LCOV_EXCL_STOP */
		}

		task->state = TASK_STATE_ERROR_CONTINUE;
		return;
	}

	task->state = TASK_STATE_DONE;
}

static int state_scrub_process(struct snapraid_state* state, struct snapraid_parity_handle* parity_handle, block_off_t blockstart, block_off_t blockmax, struct snapraid_plan* plan, time_t now)
{
	struct snapraid_io io;
	struct snapraid_handle* handle;
	void* rehandle_alloc;
	struct snapraid_rehash* rehandle;
	unsigned diskmax;
	block_off_t blockcur;
	unsigned j;
	unsigned buffermax;
	data_off_t countsize;
	block_off_t countpos;
	block_off_t countmax;
	block_off_t countlast;
	block_off_t autosavedone;
	block_off_t autosavelimit;
	block_off_t autosavemissing;
	int ret;
	unsigned soft_error;
	unsigned silent_error;
	unsigned io_error;
	unsigned l;
	unsigned* waiting_map;
	unsigned waiting_mac;
	bit_vect_t* block_enabled;

	/* maps the disks to handles */
	handle = handle_mapping(state, &diskmax);

	/* rehash buffers */
	rehandle = malloc_nofail_align(diskmax * sizeof(struct snapraid_rehash), &rehandle_alloc);

	/* we need 1 * data + 2 * parity */
	buffermax = diskmax + 2 * state->level;

	/* initialize the io threads */
	io_init(&io, state, state->opt.io_cache, buffermax, handle, diskmax, 0, scrub_data_reader, 0, parity_handle, state->level, 0, scrub_parity_reader, 0);

	/* possibly waiting disks */
	waiting_mac = diskmax > RAID_PARITY_MAX ? diskmax : RAID_PARITY_MAX;
	waiting_map = nalloc_nofail(waiting_mac, sizeof(unsigned));

	soft_error = 0;
	silent_error = 0;
	io_error = 0;

	/*
	 * Materialize the complete selection before starting I/O.
	 *
	 * Autosave may later rebuild state->bucketlist as scrub timestamps change, but
	 * the current run must continue using the plan computed from the state that
	 * existed at startup.
	 */
	/* first count the number of blocks to process */
	countmax = 0;
	countlast = 0;
	block_enabled = calloc_nofail(1, bit_vect_size(blockmax)); /* preinitialize to 0 */
	for (blockcur = blockstart; blockcur < blockmax; ++blockcur) {
		if (!block_is_enabled(plan, &countlast, blockcur))
			continue;
		bit_vect_set(block_enabled, blockcur);
		++countmax;
	}

	/*
	 * Convert the byte autosave interval to parity positions.
	 *
	 * A selected position normally reads all data columns in parallel, so use the
	 * aggregate data width rather than the actual serialized number of bytes read.
	 * Autosave only persists scrub metadata/hashes; data and parity are read-only.
	 */
	autosavelimit = state->autosave / (diskmax * state->block_size);
	autosavemissing = countmax; /* blocks to do */
	autosavedone = 0; /* blocks done */

	/* drop until now */
	state_usage_waste(state);

	countsize = 0;
	countpos = 0;
	blockcur = blockstart;

	msg_progress("Scrubbing...\n");

	/* start all the worker threads */
	io_start(&io, blockstart, blockmax, block_enabled);

	int alert = state_progress_begin(state, blockstart, blockmax, countmax);
	if (alert > 0)
		goto end;
	if (alert < 0)
		goto bail;

	while (1) {
		unsigned char* buffer_recov[LEV_MAX];
		snapraid_info info;
		int error_on_this_block;
		int silent_error_on_this_block;
		int io_error_on_this_block;
		int block_is_unsynced;
		int rehash;
		void** buffer;

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

		/*
		 * If all the blocks at this address are synced
		 * if not, parity is not even checked
		 */
		block_is_unsynced = 0;

		/* get block specific info */
		info = info_get(&state->infoarr, blockcur);

		/* if we have to use the old hash */
		rehash = info_get_rehash(info);

		/* for each disk, process the block */
		for (j = 0; j < diskmax; ++j) {
			struct snapraid_task* task;
			ssize_t read_size;
			unsigned char hash[HASH_MAX];
			struct snapraid_block* block;
			int file_is_unsynced;
			struct snapraid_disk* disk;
			struct snapraid_file* file;
			block_off_t file_pos;
			unsigned diskcur;

			/*
			 * If the file on this disk is synced
			 * if not, silent errors are assumed as expected error
			 */
			file_is_unsynced = 0;

			/* until now is misc */
			state_usage_misc(state);

			/* get the next task */
			task = io_data_read(&io, &diskcur, waiting_map, &waiting_mac);

			/* until now is disk */
			state_usage_disk(state, handle, waiting_map, waiting_mac);

			/* get the task results */
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

			/*
			 * Do this before skipping blocks without a current file.
			 *
			 * DELETED has no file contribution but still has invalid parity: physical
			 * parity may contain its OLD data. It must therefore make the whole position
			 * unsynced so a later parity mismatch is not reported as silent corruption.
			 */
			if (block_has_invalid_parity(block)) {
				/* report that the block and the file are not synced */
				block_is_unsynced = 1;
				file_is_unsynced = 1;
				/* follow */
			}

			/* if the block is not used */
			if (!block_has_file(block))
				continue;

			/*
			 * Metadata mismatch does not itself make this scrub position bad.
			 *
			 * It only tells us that the current file is not the version described by the
			 * content metadata. Any later data/parity mismatch is therefore an expected
			 * synchronization error, not evidence of silent corruption.
			 */
			if (task->is_timestamp_different) {
				/* report that the block and the file are not synced */
				block_is_unsynced = 1;
				file_is_unsynced = 1;
				/* follow */
			}

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

			/*
			 * During rehash, validate the existing content state with the previous hash
			 * algorithm and stage the digest for the new algorithm separately.
			 *
			 * Do not publish the new digest yet: another data block or parity level at the
			 * same position may still fail verification.
			 */
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

			/*
			 * Only BLK and REP contain a hash for the current file data.
			 *
			 * CHG deliberately skips this comparison because block->hash describes its
			 * OLD parity contribution. A CHG can still participate in the parity check
			 * using the current bytes read from disk.
			 */
			if (block_has_updated_hash(block)) {
				/* compare the hash */
				if (memcmp(hash, block->hash, BLOCK_HASH_SIZE) != 0) {
					unsigned diff = memdiff(hash, block->hash, BLOCK_HASH_SIZE);

					/*
					 * A mismatch is evidence of silent corruption only when this file is expected
					 * to match the synchronized content state.
					 *
					 * For an already unsynced file the difference may simply be the legitimate
					 * file modification that requires the next sync, so report it as soft and do
					 * not mark the parity position bad.
					 */
					if (file_is_unsynced) {
						log_tag("error:%" PRIu64 ":%s:%s: Data error at position %" PRIu64 ", diff hash bits %u/%zu\n", blockcur, disk->name, esc_tag(file->sub), file_pos, diff, BLOCK_HASH_SIZE * 8);
						++soft_error;
						error_on_this_block = 1;
					} else {
						log_tag("error_data:%" PRIu64 ":%s:%s: Data error at position %" PRIu64 ", diff hash bits %u/%zu\n", blockcur, disk->name, esc_tag(file->sub), file_pos, diff, BLOCK_HASH_SIZE * 8);
						log_error(EDATA, "Data error in file '%s' at position '%" PRIu64 "', diff hash bits %u/%zu\n", task->path, file_pos, diff, BLOCK_HASH_SIZE * 8);
						++silent_error;
						silent_error_on_this_block = 1;
					}
					continue;
				}
			}
		}

		/* buffers for parity read and not computed */
		for (l = 0; l < state->level; ++l)
			buffer_recov[l] = buffer[diskmax + state->level + l];
		for (; l < LEV_MAX; ++l)
			buffer_recov[l] = 0;

		/* until now is misc */
		state_usage_misc(state);

		/* read the parity */
		for (l = 0; l < state->level; ++l) {
			struct snapraid_task* task;
			unsigned levcur;

			task = io_parity_read(&io, &levcur, waiting_map, &waiting_mac);

			/* until now is parity */
			state_usage_parity(state, waiting_map, waiting_mac);

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

				/* if continuing on error, clear the missing buffer */
				buffer_recov[levcur] = 0;
				continue;
			}
			if (task->state == TASK_STATE_IOERROR_CONTINUE) {
				++io_error;
				if (io_error >= state->opt.io_error_limit) {
					/* LCOV_EXCL_START */
					log_fatal(EIO, "DANGER! Too many input/output errors in the %s disk. It isn't possible to continue.\n", lev_name(levcur));
					log_fatal(EIO, "Stopping at block %" PRIu64 "\n", blockcur);
					goto bail;
					/* LCOV_EXCL_STOP */
				}

				/* otherwise continue */
				io_error_on_this_block = 1;

				/* if continuing on error, clear the missing buffer */
				buffer_recov[levcur] = 0;
				continue;
			}
			if (task->state != TASK_STATE_DONE) {
				/* LCOV_EXCL_START */
				log_fatal(EINTERNAL, "Internal inconsistency in task state\n");
				os_abort();
				/* LCOV_EXCL_STOP */
			}
		}

		/*
		 * Recomputed parity is meaningful only with a complete trusted set of current
		 * data inputs.
		 *
		 * If any data read or applicable hash check failed, skip parity comparison:
		 * a mismatch could be caused entirely by the missing/bad data input and would
		 * not identify a parity error.
		 */
		if (!error_on_this_block && !silent_error_on_this_block && !io_error_on_this_block) {

			/* compute the parity */
			raid_gen(diskmax, state->level, state->block_size, buffer, 0);

			/* compare the parity */
			for (l = 0; l < state->level; ++l) {
				if (buffer_recov[l] && memcmp(buffer[diskmax + l], buffer_recov[l], state->block_size) != 0) {
					unsigned diff = memdiff(buffer[diskmax + l], buffer_recov[l], state->block_size);

					/*
					 * Invalid block states or changed file metadata mean that physical parity is
					 * not required to match parity recomputed from the current filesystem.
					 *
					 * Report such a mismatch as a synchronization error. Only a mismatch on a
					 * position expected to be fully synchronized is a silent parity error.
					 */
					if (block_is_unsynced) {
						log_tag("parity_error:%" PRIu64 ":%s: Data error, diff parity bits %u/%u\n", blockcur, lev_config_name(l), diff, state->block_size * 8);
						++soft_error;
						error_on_this_block = 1;
					} else {
						log_tag("parity_error_data:%" PRIu64 ":%s: Data error, diff parity bits %u/%u\n", blockcur, lev_config_name(l), diff, state->block_size * 8);
						log_error(EDATA, "Data error in parity '%s' at position '%" PRIu64 "', diff parity bits %u/%u\n", lev_config_name(l), blockcur, diff, state->block_size * 8);
						++silent_error;
						silent_error_on_this_block = 1;
					}
				}
			}

			/* until now is raid */
			state_usage_raid(state);
		}

		if (silent_error_on_this_block || io_error_on_this_block) {
			/*
			 * Mark the complete parity position bad, preserving its previous time,
			 * rehash state and just-synced state.
			 *
			 * A failed scrub must not refresh the last-known-good time or make pending
			 * metadata work appear completed.
			 */
			info_set(&state->infoarr, blockcur, info_set_bad(info));
		} else if (error_on_this_block) {
			/*
			 * A soft error does not establish either success or corruption.
			 *
			 * Keep the previous info unchanged: do not mark the position bad, but also do
			 * not refresh its scrub time, clear justsynced, or complete a pending rehash.
			 */
		} else {
			/*
			 * Publish staged hashes before clearing the rehash flag. Both changes are then
			 * persisted by the same content write, so a saved position is interpreted
			 * consistently with either the previous or the current hash algorithm.
			 *
			 * For a CHG block there was no current hash to validate directly. Reaching
			 * this point nevertheless means parity agrees with the current bytes just
			 * read, so these bytes form the verified parity baseline represented by the
			 * newly stored digest. The block remains CHG and therefore still conservatively
			 * requires sync.
			 */
			if (rehash) {
				/* store all the new hash already computed */
				for (j = 0; j < diskmax; ++j) {
					if (rehandle[j].block)
						memcpy(rehandle[j].block->hash, rehandle[j].hash, BLOCK_HASH_SIZE);
				}
			}

			/*
			 * A completely clean scrub establishes a new last-known-good checkpoint for
			 * the whole parity position.
			 *
			 * Refresh the time and clear bad, rehash and justsynced together. In
			 * particular this is the only normal scrub path that clears a previous bad
			 * mark.
			 */
			info_set(&state->infoarr, blockcur, info_make(now, 0, 0, 0));
		}

		/* mark the state as needing write */
		state->need_write = 1;

		/* count the number of processed block */
		++countpos;

		/* progress */
		if (state_progress(state, &io, blockcur, countpos, countmax, countsize)) {
			/* LCOV_EXCL_START */
			break;
			/* LCOV_EXCL_STOP */
		}

		/* thermal control */
		if (state_thermal_alarm(state)) {
			/* until now is misc */
			state_usage_misc(state);

			state_progress_stop(state);

			/* complete all read-ahead without consuming the results */
			io_quiesce(&io);

			state_thermal_cooldown(state);

			state_progress_restart(state);

			/* drop until now */
			state_usage_waste(state);
		}

		/* autosave */
		if (state->autosave != 0
			&& autosavedone >= autosavelimit /* if we have reached the limit */
			&& autosavemissing >= autosavelimit /* if we have at least a full step to do */
		) {
			autosavedone = 0; /* restart the counter */

			/* until now is misc */
			state_usage_misc(state);

			state_progress_stop(state);

			msg_progress("Autosaving...\n");
			state_write(state);

			state_progress_restart(state);

			/* drop until now */
			state_usage_waste(state);
		}
	}

end:
	state_progress_end(state, countpos, countmax, countsize, "Nothing to scrub. Use the -p PLAN option to select a different plan, like -p full.\n");

	/* save the new state if required */
	if (state->need_write || state->opt.force_content_write)
		state_write(state);

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
		log_fatal(EDATA, "DANGER! Unexpected data errors! The failing blocks are now marked as bad!\n");
	if (io_error || silent_error) {
		log_fatal(ESOFT, "Use 'snapraid status' to list the bad blocks.\n");
		log_fatal(ESOFT, "Use 'snapraid -e fix' to recover them.\n");
		log_fatal(ESOFT, "Use 'snapraid -p bad scrub' to recheck after fixing to clear the bad state.\n");
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
	free(rehandle_alloc);
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

	if (alert < 0)
		return -1;

	return 0;
}

/**
 * Return a * b / c approximated to the upper value.
 */
static uint64_t md(uint64_t a, uint32_t b, uint32_t c)
{
	uint64_t v = a;

	v *= b;
	v += c - 1;
	v /= c;

	return v;
}

int state_scrub(struct snapraid_state* state, int plan100, int olderthan)
{
	block_off_t blockmax;
	block_off_t countlimit;
	block_off_t count;
	time_t recentlimit;
	int ret;
	struct snapraid_parity_handle parity_handle[LEV_MAX];
	struct snapraid_plan ps;
	unsigned process_error;
	time_t now;
	unsigned l;

	/* get the present time */
	now = time(0);

	msg_progress("Initializing...\n");

	if ((plan100 == SCRUB_BAD || plan100 == SCRUB_NEW || plan100 == SCRUB_FULL)
		&& olderthan >= 0) {
		/* LCOV_EXCL_START */
		log_fatal(EUSER, "You can specify -o, --older-than only with a numeric percentage.\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	blockmax = parity_allocated_size(state);

	/* preinitialize to avoid warnings */
	countlimit = 0;
	recentlimit = 0;

	ps.state = state;
	if (state->opt.force_scrub_even) {
		ps.plan = SCRUB_EVEN;
	} else if (plan100 == SCRUB_FULL) {
		ps.plan = SCRUB_FULL;
		msg_progress("Scrub plan: full. All data blocks will be checked.\n");
	} else if (plan100 == SCRUB_NEW) {
		ps.plan = SCRUB_NEW;
		msg_progress("Scrub plan: new. Only blocks that have never been scrubbed will be checked.\n");
	} else if (plan100 == SCRUB_BAD) {
		ps.plan = SCRUB_BAD;
		msg_progress("Scrub plan: bad. Only blocks previously marked as bad will be checked.\n");
	} else if (state->opt.force_scrub_at) {
		/* scrub the specified amount of blocks */
		ps.plan = SCRUB_AUTO;
		countlimit = state->opt.force_scrub_at;
		recentlimit = now;
	} else {
		ps.plan = SCRUB_AUTO;
		if (plan100 >= 0) {
			countlimit = md(blockmax, plan100, 10000);
		} else {
			/* by default scrub 8.33% of the array (100/12=8.(3)) */
			countlimit = md(blockmax, 1, 12);
		}

		if (olderthan >= 0) {
			recentlimit = now - olderthan * 24 * 3600;
		} else {
			/* by default use a 10 day time limit */
			recentlimit = now - 10 * 24 * 3600;
		}

		if (plan100 >= 0) {
			if (olderthan >= 0)
				msg_progress("Scrub plan: auto. %.1f%% of the array, older than %d days, will be checked.\n", plan100 / 100.0, olderthan);
			else
				msg_progress("Scrub plan: auto. %.1f%% of the array, older than 10 days, will be checked.\n", plan100 / 100.0);
		} else {
			if (olderthan >= 0)
				msg_progress("Scrub plan: auto. 8.3%% of the array, older than %d days, will be checked.\n", olderthan);
			else
				msg_progress("Scrub plan: auto. 8.3%% of the array, older than 10 days, will be checked.\n");
		}
	}

	count = 0;
	for (tommy_node* j = tommy_list_head(&state->bucketlist); j != 0; j = j->next) {
		struct snapraid_bucket* bucket = j->data;
		count += bucket->count_scrubbed + bucket->count_justsynced;
	}

	if (!count) {
		/* LCOV_EXCL_START */
		log_fatal(EUSER, "The array is empty.\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* compute the limits from count/recentlimit */
	if (ps.plan == SCRUB_AUTO) {
		/* no more than the full count */
		if (countlimit > count)
			countlimit = count;

		/* by default process everything */
		ps.timelimit = now;
		ps.lastlimit = 0;

		tommy_node* j = tommy_list_head(&state->bucketlist);
		block_off_t processed_count = 0;

		/*
		 * Buckets are sorted oldest first. Walk them until either the age cutoff or
		 * requested count is reached.
		 *
		 * If the count ends inside a bucket, timelimit identifies that bucket and
		 * lastlimit records how many positions from it may be selected.
		 */
		while (j) {
			struct snapraid_bucket* bucket = j->data;
			block_off_t bucket_count = bucket->count_justsynced + bucket->count_scrubbed;

			if (bucket->time_at > recentlimit) {
				ps.timelimit = recentlimit;
				ps.lastlimit = 0;
				break;
			}

			if (processed_count + bucket_count > countlimit) {
				ps.timelimit = bucket->time_at;
				ps.lastlimit = countlimit - processed_count;
				processed_count = countlimit;
				break;
			}

			processed_count += bucket_count;
			j = j->next;
		}

		/*
		 * Disable selection completely when no normal AUTO position is eligible.
		 * Leaving timelimit initialized to 'now' would otherwise make the subsequent
		 * per-position filter appear permissive.
		 *
		 * Bad positions are unaffected because they bypass AUTO limits.
		 */
		if (processed_count == 0) {
			ps.timelimit = 0;
			ps.lastlimit = 0;
		}

		log_tag("count_limit:%" PRIu64 "\n", countlimit);
		log_tag("time_limit:%" PRIu64 "\n", (uint64_t)ps.timelimit);
		log_tag("last_limit:%" PRIu64 "\n", ps.lastlimit);
	} else {
		/* avoid compiler warnings */
		ps.timelimit = 0;
		ps.lastlimit = 0;
	}

	/* open the file for reading */
	for (l = 0; l < state->level; ++l) {
		ret = parity_open(&parity_handle[l], &state->parity[l], l, state->file_mode, state->block_size, state->opt.parity_limit_size);
		if (ret == -1) {
			/* LCOV_EXCL_START */
			log_tag("parity_%s:%" PRIu64 ":%s: Open error. %s.\n", es(errno), blockmax, lev_config_name(l), strerror(errno));
			log_fatal_errno(errno, lev_config_name(l));
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}

	process_error = 0;

	ret = state_scrub_process(state, parity_handle, 0, blockmax, &ps, now);
	if (ret == -1) {
		++process_error;
		/* continue, as we are already exiting */
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

	if (process_error != 0)
		return -1;

	msg_status("Everything OK\n");

	return 0;
}

