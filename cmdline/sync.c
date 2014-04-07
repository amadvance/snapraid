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

#include "portable.h"

#include "util.h"
#include "elem.h"
#include "state.h"
#include "parity.h"
#include "handle.h"
#include "raid/raid.h"

/****************************************************************************/
/* hash */

static int state_hash_process(struct snapraid_state* state, block_off_t blockstart, block_off_t blockmax)
{
	struct snapraid_handle* handle;
	unsigned diskmax;
	block_off_t i;
	unsigned j;
	void* buffer;
	data_off_t countsize;
	block_off_t countpos;
	block_off_t countmax;
	int ret;
	unsigned error;
	int skip_sync = 0;

	/* maps the disks to handles */
	handle = handle_map(state, &diskmax);

	/* buffer for reading */
	buffer = malloc_nofail(state->block_size);
	if (!state->opt.skip_self)
		mtest_vector(1, state->block_size, &buffer);

	error = 0;

	/* first count the number of blocks to process */
	countmax = 0;
	for(j=0;j<diskmax;++j) {
		struct snapraid_disk* disk = handle[j].disk;

		/* if unused disk skip it */
		if (!disk)
			continue;

		for(i=blockstart;i<blockmax;++i) {
			struct snapraid_block* block;

			block = disk_block_get(disk, i);

			/* if no file in this block, skip it */
			if (!block_has_file(block))
				continue;

			/* if the block already has a hash, skip it */
			if (block_has_updated_hash(block))
				continue;

			++countmax;
		}
	}

	countsize = 0;
	countpos = 0;
	if (state_progress_begin(state, blockstart, blockmax, countmax))
	for(j=0;j<diskmax;++j) {
		struct snapraid_disk* disk = handle[j].disk;

		/* if unused disk skip it */
		if (!disk)
			continue;

		for(i=blockstart;i<blockmax;++i) {
			snapraid_info info;
			int rehash;
			struct snapraid_block* block;
			int read_size;
			unsigned char hash[HASH_SIZE];

			block = disk_block_get(disk, i);

			/* if no file in this block, skip it */
			if (!block_has_file(block))
				continue;

			/* if the block already has a hash, skip it */
			if (block_has_updated_hash(block))
				continue;

			/* here only CHG blocks are allowed */
			assert(block_state_get(block) == BLOCK_STATE_CHG);

			/* get block specific info */
			info = info_get(&state->infoarr, i);

			/* if we have to use the old hash */
			rehash = info_get_rehash(info);

			/* if the file is different than the current one, close it */
			if (handle[j].file != 0 && handle[j].file != block_file_get(block)) {
				/* file we are processing for error reporting */
				struct snapraid_file* file = handle[j].file;
				ret = handle_close(&handle[j]);
				if (ret == -1) {
					/* LCOV_EXCL_START */
					/* This one is really an unexpected error, because we are only reading */
					/* and closing a descriptor should never fail */
					fprintf(stdlog, "error:%u:%s:%s: Close error. %s\n", i, disk->name, file->sub, strerror(errno));
					fprintf(stderr, "DANGER! Unexpected close error in a data disk, it isn't possible to sync.\n");
					fprintf(stderr, "Ensure that disk '%s' is sane and that file '%s' can be accessed.\n", disk->dir, handle[j].path);
					printf("Stopping at block %u\n", i);
					++error;
					goto bail;
					/* LCOV_EXCL_STOP */
				}
			}

			ret = handle_open(&handle[j], block_file_get(block), state->opt.skip_sequential, stderr);
			if (ret == -1) {
				/* file we are processing for error reporting */
				struct snapraid_file* file = block_file_get(block);
				if (errno == ENOENT) {
					fprintf(stdlog, "error:%u:%s:%s: Open missing error\n", i, disk->name, file->sub);
					fprintf(stderr, "Missing file '%s'.\n", handle[j].path);
					fprintf(stderr, "WARNING! You cannot modify data disk during a sync.\n");
					fprintf(stderr, "Rerun the sync command when finished.\n");

					++error;

					/* if the file is missing, it means that it was removed during sync */
					/* this isn't a serious error, so we skip this block, and continue with others */
					continue;
				} else if (errno == EACCES) {
					fprintf(stdlog, "error:%u:%s:%s: Open access error\n", i, disk->name, file->sub);
					fprintf(stderr, "No access at file '%s'.\n", handle[j].path);
					fprintf(stderr, "WARNING! Please fix the access permission in the data disk.\n");
					fprintf(stderr, "Rerun the sync command when finished.\n");

					++error;

					/* this isn't a serious error, so we skip this block, and continue with others */
					continue;
				} else {
					fprintf(stdlog, "error:%u:%s:%s: Open error. %s\n", i, disk->name, file->sub, strerror(errno));
					fprintf(stderr, "DANGER! Unexpected open error in a data disk, it isn't possible to sync.\n");
					fprintf(stderr, "Ensure that disk '%s' is sane and that file '%s' can be accessed.\n", disk->dir, handle[j].path);
					printf("Stopping to allow recovery. Try with 'snapraid check -f %s'\n", file->sub);
				}

				/* LCOV_EXCL_START */
				++error;
				goto bail;
				/* LCOV_EXCL_STOP */
			}

			/* check if the file is changed */
			if (handle[j].st.st_size != block_file_get(block)->size
				|| handle[j].st.st_mtime != block_file_get(block)->mtime_sec
				|| STAT_NSEC(&handle[j].st) != block_file_get(block)->mtime_nsec
				|| handle[j].st.st_ino != block_file_get(block)->inode
			) {
				/* file we are processing for error reporting */
				struct snapraid_file* file = block_file_get(block);
				fprintf(stdlog, "error:%u:%s:%s: Unexpected attribute change\n", i, disk->name, file->sub);
				if (handle[j].st.st_size != file->size) {
					fprintf(stderr, "Unexpected size change at file '%s' from %"PRIu64" to %"PRIu64".\n", handle[j].path, file->size, handle[j].st.st_size);
				} else if (handle[j].st.st_mtime != file->mtime_sec
					|| STAT_NSEC(&handle[j].st) != file->mtime_nsec) {
					fprintf(stderr, "Unexpected time change at file '%s' from %"PRIu64".%d to %"PRIu64".%d.\n", handle[j].path, file->mtime_sec, file->mtime_nsec, (uint64_t)handle[j].st.st_mtime, (uint32_t)STAT_NSEC(&handle[j].st));
				} else {
					fprintf(stderr, "Unexpected inode change from %"PRIu64" to %"PRIu64" at file '%s'.\n", file->inode, (uint64_t)handle[j].st.st_ino, handle[j].path);
				}
				fprintf(stderr, "WARNING! You cannot modify files during a sync.\n");
				fprintf(stderr, "Rerun the sync command when finished.\n");

				++error;

				/* if the file is changed, it means that it was modified during sync */
				/* this isn't a serious error, so we skip this block, and continue with others */
				continue;
			}

			read_size = handle_read(&handle[j], block, buffer, state->block_size, stderr);
			if (read_size == -1) {
				/* LCOV_EXCL_START */
				/* file we are processing for error reporting */
				struct snapraid_file* file = block_file_get(block);
				fprintf(stdlog, "error:%u:%s:%s: Read error at position %u\n", i, disk->name, file->sub, block_file_pos(block));
				fprintf(stderr, "DANGER! Unexpected read error in a data disk, it isn't possible to sync.\n");
				fprintf(stderr, "Ensure that disk '%s' is sane and that file '%s' can be read.\n", disk->dir, handle[j].path);
				printf("Stopping to allow recovery. Try with 'snapraid check -f %s'\n", file->sub);
				++error;
				goto bail;
				/* LCOV_EXCL_STOP */
			}

			countsize += read_size;

			/* now compute the hash */
			if (rehash) {
				memhash(state->prevhash, state->prevhashseed, hash, buffer, read_size);
			} else {
				memhash(state->hash, state->hashseed, hash, buffer, read_size);
			}

			/* copy the hash in the block */
			memcpy(block->hash, hash, HASH_SIZE);

			/* and mark the block as hashed */
			block_state_set(block, BLOCK_STATE_REP);

			/* mark the state as needing write */
			state->need_write = 1;

			/* count the number of processed block */
			++countpos;

			/* progress */
			if (state_progress(state, i, countpos, countmax, countsize)) {
				/* LCOV_EXCL_START */
				skip_sync = 1;
				break;
				/* LCOV_EXCL_STOP */
			}
		}

		/* close the last file in the disk */
		if (handle[j].file != 0) {
			/* file we are processing for error reporting */
			struct snapraid_file* file = handle[j].file;
			ret = handle_close(&handle[j]);

			if (ret == -1) {
				/* LCOV_EXCL_START */
				/* This one is really an unexpected error, because we are only reading */
				/* and closing a descriptor should never fail */
				fprintf(stdlog, "error:%u:%s:%s: Close error. %s\n", blockmax, disk->name, file->sub, strerror(errno));
				fprintf(stderr, "DANGER! Unexpected close error in a data disk, it isn't possible to sync.\n");
				fprintf(stderr, "Ensure that disk '%s' is sane and that file '%s' can be accessed.\n", disk->dir, handle[j].path);
				printf("Stopping at block %u\n", blockmax);
				++error;
				goto bail;
				/* LCOV_EXCL_STOP */
			}
		}
	}

	state_progress_end(state, countpos, countmax, countsize);

	if (error) {
		printf("\n");
		printf("%8u read/write errors\n", error);
		printf("WARNING! There are errors!\n");
	} else {
		/* print the result only if processed something */
		if (countpos != 0)
			printf("Everything OK\n");
	}

	fprintf(stdlog, "hash_summary:error_readwrite:%u\n", error);

bail:
	for(j=0;j<diskmax;++j) {
		ret = handle_close(&handle[j]);
		if (ret == -1) {
			fprintf(stderr, "DANGER! Unexpected close error in a data disk.\n");
			++error;
			/* continue, as we are already exiting */
		}
	}

	free(handle);
	free(buffer);

	return skip_sync;
}

/****************************************************************************/
/* sync */

/**
 * A block that failed the hash check, or that was deleted.
 */
struct failed_struct {
	unsigned index; /**< Index of the failed block. */
	unsigned size; /**< Size of the block. */

	struct snapraid_block* block; /**< The failed block, or BLOCK_DELETED for a deleted block */
};

/**
 * Buffer for storing the new hashes.
 */
struct snapraid_rehash {
	unsigned char hash[HASH_SIZE];
	struct snapraid_block* block;
};

static int state_sync_process(struct snapraid_state* state, struct snapraid_parity** parity, block_off_t blockstart, block_off_t blockmax)
{
	struct snapraid_handle* handle;
	void* rehandle_alloc;
	struct snapraid_rehash* rehandle;
	unsigned diskmax;
	block_off_t i;
	unsigned j;
	void* buffer_alloc;
	void** buffer;
	unsigned buffermax;
	data_off_t countsize;
	block_off_t countpos;
	block_off_t countmax;
	block_off_t autosavedone;
	block_off_t autosavelimit;
	block_off_t autosavemissing;
	int ret;
	unsigned error;
	unsigned silent_error;
	time_t now;
	struct failed_struct* failed;
	int* failed_map;
	unsigned l;

	/* the sync process assumes that all the hashes are correct */
	/* including the ones from CHG and DELETED blocks */
	assert(state->clear_undeterminate_hash != 0);

	/* get the present time */
	now = time(0);

	/* maps the disks to handles */
	handle = handle_map(state, &diskmax);

	/* rehash buffers */
	rehandle = malloc_nofail_align(diskmax * sizeof(struct snapraid_rehash), &rehandle_alloc);

	/* we need 2 * data + 1 * parity + 1 * zero */
	buffermax = 2 * diskmax + state->level + 1;

	buffer = malloc_nofail_vector_align(diskmax, buffermax, state->block_size, &buffer_alloc);
	if (!state->opt.skip_self)
		mtest_vector(buffermax, state->block_size, buffer);

	/* fill up the zero buffer */
	memset(buffer[buffermax-1], 0, state->block_size);
	raid_zero(buffer[buffermax-1]);

	failed = malloc_nofail(diskmax * sizeof(struct failed_struct));
	failed_map = malloc_nofail(diskmax * sizeof(unsigned));

	error = 0;
	silent_error = 0;

	/* first count the number of blocks to process */
	countmax = 0;
	for(i=blockstart;i<blockmax;++i) {
		int one_invalid;
		int one_valid;

		/* for each disk */
		one_invalid = 0;
		one_valid = 0;
		for(j=0;j<diskmax;++j) {
			struct snapraid_block* block = BLOCK_EMPTY;
			if (handle[j].disk)
				block = disk_block_get(handle[j].disk, i);

			if (block_has_file(block)) {
				one_valid = 1;
			}
			if (block_has_invalid_parity(block)) {
				one_invalid = 1;
			}
		}

		/* if none valid or none invalid, we don't need to update */
		if (!one_invalid || !one_valid) {
			continue;
		}

		++countmax;
	}

	/* compute the autosave size for all disk, even if not read */
	/* this makes sense because the speed should be almost the same */
	/* if the disks are read in parallel */
	autosavelimit = state->autosave / (diskmax * state->block_size);
	autosavemissing = countmax; /* blocks to do */
	autosavedone = 0; /* blocks done */

	countsize = 0;
	countpos = 0;
	if (state_progress_begin(state, blockstart, blockmax, countmax))
	for(i=blockstart;i<blockmax;++i) {
		unsigned failed_count;
		int one_invalid;
		int one_valid;
		int error_on_this_block;
		int silent_error_on_this_block;
		int fixed_error_on_this_block;
		int parity_needs_to_be_updated;
		snapraid_info info;
		int rehash;

		/* for each disk */
		one_invalid = 0;
		one_valid = 0;
		for(j=0;j<diskmax;++j) {
			struct snapraid_block* block = BLOCK_EMPTY;
			if (handle[j].disk)
				block = disk_block_get(handle[j].disk, i);

			if (block_has_file(block)) {
				one_valid = 1;
			}
			if (block_has_invalid_parity(block)) {
				one_invalid = 1;
			}
		}

		/* if none valid or none invalid, we don't need to update */
		if (!one_invalid || !one_valid) {
			continue;
		}

		/* one more block processed for autosave */
		++autosavedone;
		--autosavemissing;

		/* by default process the block, and skip it if something go wrong */
		error_on_this_block = 0;
		silent_error_on_this_block = 0;
		fixed_error_on_this_block = 0;

		/* keep track of the number of failed blocks */
		failed_count = 0;

		/* get block specific info */
		info = info_get(&state->infoarr, i);

		/* if we have to use the old hash */
		rehash = info_get_rehash(info);

		/* it could happens that all the blocks are EMPTY/BLK and CHG but with the hash */
		/* still matching because the specific CHG block was not modified. */
		/* Note that CHG/DELETED blocks already present in the content file loaded */
		/* have the hash cleared, and then they won't never match the hash. */
		/* We are treating only CHG blocks created at runtime. */
		/* In such case, we can avoid to update parity, because it would be the same as before */
		parity_needs_to_be_updated = 0;

		/* if the block is marked as bad, we force the parity update */
		/* because the bad block may be the result of a wrong parity */
		if (info_get_bad(info))
			parity_needs_to_be_updated = 1;

		/* for each disk, process the block */
		for(j=0;j<diskmax;++j) {
			int read_size;
			unsigned char hash[HASH_SIZE];
			struct snapraid_block* block;
			unsigned block_state;
			struct snapraid_disk* disk = handle[j].disk;

			/* by default no rehash in case of "continue" */
			rehandle[j].block = 0;

			/* if the disk position is not used */
			if (!disk) {
				/* use an empty block */
				memset(buffer[j], 0, state->block_size);
				continue;
			}

			/* get the block */
			block = disk_block_get(disk, i);

			/* get the state of the block */
			block_state = block_state_get(block);

			/* if the block has invalid parity, */
			/* we have to take care of it in case of recover */
			if (block_has_invalid_parity(block)) {
				/* store it in the failed set, because */
				/* the parity may be still computed with the previous content */
				failed[failed_count].index = j;
				failed[failed_count].size = state->block_size;
				failed[failed_count].block = block;
				++failed_count;

				/* if the block has invalid parity, we have to update the parity */
				/* to include this block change */
				/* This also apply to CHG blocks, but we are going to handle */
				/* later this case to do the updates only if really needed */
				if (block_state != BLOCK_STATE_CHG)
					parity_needs_to_be_updated = 1;

				/* note that DELETE blocks are skipped in the next check */
				/* and we have to store them in the failed blocks */
				/* before skipping */

				/* follow */
			}

			/* if the block has no file, meanining that it's EMPTY or DELETED, */
			/* it doesn't partecipate in the new parity computation */
			if (!block_has_file(block)) {
				/* use an empty block */
				memset(buffer[j], 0, state->block_size);
				continue;
			}

			/* if the file is different than the current one, close it */
			if (handle[j].file != 0 && handle[j].file != block_file_get(block)) {
				/* file we are processing for error reporting */
				struct snapraid_file* file = handle[j].file;
				ret = handle_close(&handle[j]);
				if (ret == -1) {
					/* LCOV_EXCL_START */
					/* This one is really an unexpected error, because we are only reading */
					/* and closing a descriptor should never fail */
					fprintf(stdlog, "error:%u:%s:%s: Close error. %s\n", i, disk->name, file->sub, strerror(errno));
					fprintf(stderr, "DANGER! Unexpected close error in a data disk, it isn't possible to sync.\n");
					fprintf(stderr, "Ensure that disk '%s' is sane and that file '%s' can be accessed.\n", disk->dir, handle[j].path);
					printf("Stopping at block %u\n", i);
					++error;
					goto bail;
					/* LCOV_EXCL_STOP */
				}
			}

			ret = handle_open(&handle[j], block_file_get(block), state->opt.skip_sequential, stderr);
			if (ret == -1) {
				/* file we are processing for error reporting */
				struct snapraid_file* file = block_file_get(block);
				if (errno == ENOENT) {
					fprintf(stdlog, "error:%u:%s:%s: Open missing error\n", i, disk->name, file->sub);
					fprintf(stderr, "Missing file '%s'.\n", handle[j].path);
					fprintf(stderr, "WARNING! You cannot modify data disk during a sync.\n");
					fprintf(stderr, "Rerun the sync command when finished.\n");

					++error;

					/* if the file is missing, it means that it was removed during sync */
					/* this isn't a serious error, so we skip this block, and continue with others */
					error_on_this_block = 1;
					continue;
				}
				if (errno == EACCES) {
					fprintf(stdlog, "error:%u:%s:%s: Open access error\n", i, disk->name, file->sub);
					fprintf(stderr, "No access at file '%s'.\n", handle[j].path);
					fprintf(stderr, "WARNING! Please fix the access permission in the data disk.\n");
					fprintf(stderr, "Rerun the sync command when finished.\n");

					++error;

					/* this isn't a serious error, so we skip this block, and continue with others */
					error_on_this_block = 1;
					continue;
				}
				
				/* LCOV_EXCL_START */
				fprintf(stdlog, "error:%u:%s:%s: Open error. %s\n", i, disk->name, file->sub, strerror(errno));
				fprintf(stderr, "DANGER! Unexpected open error in a data disk, it isn't possible to sync.\n");
				fprintf(stderr, "Ensure that disk '%s' is sane and that file '%s' can be accessed.\n", disk->dir, handle[j].path);
				printf("Stopping to allow recovery. Try with 'snapraid check -f %s'\n", file->sub);

				++error;
				goto bail;
				/* LCOV_EXCL_STOP */
			}

			/* check if the file is changed */
			if (handle[j].st.st_size != block_file_get(block)->size
				|| handle[j].st.st_mtime != block_file_get(block)->mtime_sec
				|| STAT_NSEC(&handle[j].st) != block_file_get(block)->mtime_nsec
				|| handle[j].st.st_ino != block_file_get(block)->inode
			) {
				/* file we are processing for error reporting */
				struct snapraid_file* file = block_file_get(block);
				fprintf(stdlog, "error:%u:%s:%s: Unexpected attribute change\n", i, disk->name, file->sub);
				if (handle[j].st.st_size != file->size) {
					fprintf(stderr, "Unexpected size change at file '%s' from %"PRIu64" to %"PRIu64".\n", handle[j].path, file->size, handle[j].st.st_size);
				} else if (handle[j].st.st_mtime != file->mtime_sec
					|| STAT_NSEC(&handle[j].st) != file->mtime_nsec) {
					fprintf(stderr, "Unexpected time change at file '%s' from %"PRIu64".%d to %"PRIu64".%d.\n", handle[j].path, file->mtime_sec, file->mtime_nsec, (uint64_t)handle[j].st.st_mtime, (uint32_t)STAT_NSEC(&handle[j].st));
				} else {
					fprintf(stderr, "Unexpected inode change from %"PRIu64" to %"PRIu64" at file '%s'.\n", file->inode, (uint64_t)handle[j].st.st_ino, handle[j].path);
				}
				fprintf(stderr, "WARNING! You cannot modify files during a sync.\n");
				fprintf(stderr, "Rerun the sync command when finished.\n");

				++error;

				/* if the file is changed, it means that it was modified during sync */
				/* this isn't a serious error, so we skip this block, and continue with others */
				error_on_this_block = 1;
				continue;
			}

			read_size = handle_read(&handle[j], block, buffer[j], state->block_size, stderr);
			if (read_size == -1) {
				/* LCOV_EXCL_START */
				/* file we are processing for error reporting */
				struct snapraid_file* file = block_file_get(block);
				fprintf(stdlog, "error:%u:%s:%s: Read error at position %u\n", i, disk->name, file->sub, block_file_pos(block));
				fprintf(stderr, "DANGER! Unexpected read error in a data disk, it isn't possible to sync.\n");
				fprintf(stderr, "Ensure that disk '%s' is sane and that file '%s' can be read.\n", disk->dir, handle[j].path);
				printf("Stopping to allow recovery. Try with 'snapraid check -f %s'\n", file->sub);
				++error;
				goto bail;
				/* LCOV_EXCL_STOP */
			}

			countsize += read_size;

			/* now compute the hash */
			if (rehash) {
				memhash(state->prevhash, state->prevhashseed, hash, buffer[j], read_size);

				/* compute the new hash, and store it */
				rehandle[j].block = block;
				memhash(state->hash, state->hashseed, rehandle[j].hash, buffer[j], read_size);
			} else {
				memhash(state->hash, state->hashseed, hash, buffer[j], read_size);
			}

			if (block_has_updated_hash(block)) {
				/* compare the hash */
				if (memcmp(hash, block->hash, HASH_SIZE) != 0) {
					/* file we are processing for error reporting */
					struct snapraid_file* file = block_file_get(block);

					/* if the file has invalid parity, it's a REP changed during the sync */
					if (block_has_invalid_parity(block)) {
						fprintf(stdlog, "error:%u:%s:%s: Unexpected data change\n", i, disk->name, file->sub);
						fprintf(stderr, "Data change at file '%s' at position '%u'\n", handle[j].path, block_file_pos(block));
						fprintf(stderr, "WARNING! Unexpected data modification of a file without parity!\n");
						fprintf(stderr, "Try removing the file from the array and rerun the 'sync' command!\n");

						++error;

						/* if the file is changed, it means that it was modified during sync */
						/* this isn't a serious error, so we skip this block, and continue with others */
						error_on_this_block = 1;
						continue;
					} else { /* otherwise it's a BLK with silent error */
						fprintf(stdlog, "error:%u:%s:%s: Data error at position %u\n", i, disk->name, file->sub, block_file_pos(block));
						fprintf(stderr, "Data error at file '%s' at position '%u'\n", handle[j].path, block_file_pos(block));
						fprintf(stderr, "WARNING! Unexpected data error in a data disk! The block is now marked as bad!\n");
						fprintf(stderr, "Try with 'snapraid -e fix' to recover!\n");

						/* save the failed block for the fix */
						failed[failed_count].index = j;
						failed[failed_count].size = read_size;
						failed[failed_count].block = block;
						++failed_count;

						/* silent errors are very rare, and are not a signal that a disk */
						/* is going to fail. So, we just continue marking the block as bad */
						/* just like in scrub */
						++silent_error;
						silent_error_on_this_block = 1;
						continue;
					}
				}
			} else {
				/* if until now the parity doesn't need to be updated */
				if (!parity_needs_to_be_updated) {
					/* for sure it's a CHG block, because EMPTY are processed before with "continue" */
					/* and BLK and REP have "block_has_updated_hash()" as 1, and all the others */
					/* have "parity_needs_to_be_updated" already at 1 */
					assert(block_state_get(block) == BLOCK_STATE_CHG);

					/* if there is an hash */
					if (hash_is_real(block->hash)) {
						/* check if the hash is changed */
						if (memcmp(hash, block->hash, HASH_SIZE) != 0) {
							/* the block is different, and we must update parity */
							parity_needs_to_be_updated = 1;
						}
					} else {
						/* if the hash is already invalid, we update parity */
						parity_needs_to_be_updated = 1;
					}
				}
			
				/* copy the hash in the block, but doesn't mark the block as hashed */
				/* this allow in case of skipped block to do not save the failed computation */
				memcpy(block->hash, hash, HASH_SIZE);

				/* note that in case of rehash, this is the wrong hash, */
				/* but it will be overwritten later */
			}
		}

		/* if we have only silent errors we can try to fix them on-the-fly */
		if (!error_on_this_block && silent_error_on_this_block) {
			unsigned failed_mac;
			int something_to_recover = 0;

			/* setup the blocks to recover */
			failed_mac = 0;
			for(j=0;j<failed_count;++j) {
				unsigned char* block_buffer = buffer[failed[j].index];
				unsigned char* block_copy = buffer[diskmax + state->level + failed[j].index];
				unsigned block_state = block_state_get(failed[j].block);

				/* we try to recover only if at least one BLK is present */
				if (block_state == BLOCK_STATE_BLK)
					something_to_recover = 1;

				/* save a copy of the content just read */
				/* that it's going to be overwritten by the recovering function */
				memcpy(block_copy, block_buffer, state->block_size);

				if (block_state == BLOCK_STATE_CHG
					&& hash_is_zero(failed[j].block->hash)
				) {
					/* if the block was filled with 0, restore this state */
					/* and avoid to recover it */
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
				/* read the parity */
				/* we are sure that parity exists because */
				/* we have at least one BLK block */
				for(l=0;l<state->level;++l) {
					ret = parity_read(parity[l], i, buffer[diskmax+l], state->block_size, stdlog);
					if (ret == -1) {
						/* LCOV_EXCL_START */
						fprintf(stdlog, "parity_error:%u:%s: Read error\n", i, lev_config_name(l));
						fprintf(stderr, "DANGER! Read error in the %s disk, it isn't possible to sync.\n", lev_name(l));
						fprintf(stderr, "Ensure that disk '%s' is sane.\n", lev_config_name(l));
						printf("Stopping at block %u\n", i);
						++error;
						goto bail;
						/* LCOV_EXCL_STOP */
					}
				}

				/* try to fix the data */
				raid_rec(failed_mac, failed_map, diskmax, state->level, state->block_size, buffer);

				/* check the result and prepare the data */
				for(j=0;j<failed_count;++j) {
					unsigned char hash[HASH_SIZE];
					unsigned char* block_buffer = buffer[failed[j].index];
					unsigned char* block_copy = buffer[diskmax + state->level + failed[j].index];
					unsigned block_state = block_state_get(failed[j].block);

					if (block_state == BLOCK_STATE_BLK) {
						unsigned size = failed[j].size;

						/* compute the hash of the recovered block */
						if (rehash) {
							memhash(state->prevhash, state->prevhashseed, hash, block_buffer, size);
						} else {
							memhash(state->hash, state->hashseed, hash, block_buffer, size);
						}

						/* if the hash doesn't match */
						if (memcmp(hash, failed[j].block->hash, HASH_SIZE) != 0) {
							/* we have not recovered */
							break;
						}

						/* pad with 0 if needed */
						if (size < state->block_size)
							memset(block_buffer + size, 0, state->block_size - size);
					} else {
						/* otherwise restore the content */
						/* because we are not interested in the old state */
						/* that it's recovered for CHG, REP and DELETED blocks */
						memcpy(block_buffer, block_copy, state->block_size);
					}
				}

				/* if all is processed, we have fixed it */
				if (j == failed_count)
					fixed_error_on_this_block = 1;
			}
		}

		/* if we have read all the data required and it's correct, proceed with the parity */
		if (!error_on_this_block
			&& (!silent_error_on_this_block || fixed_error_on_this_block)
		) {
			/* updates the parity only if really needed */
			if (parity_needs_to_be_updated) {
				/* compute the parity */
				raid_gen(diskmax, state->level, state->block_size, buffer);

				/* write the parity */
				for(l=0;l<state->level;++l) {
					ret = parity_write(parity[l], i, buffer[diskmax+l], state->block_size);
					if (ret == -1) {
						/* LCOV_EXCL_START */
						fprintf(stdlog, "parity_error:%u:%s: Write error\n", i, lev_config_name(l));
						fprintf(stderr, "DANGER! Write error in the %s disk, it isn't possible to sync.\n", lev_name(l));
						fprintf(stderr, "Ensure that disk '%s' is sane.\n", lev_config_name(l));
						printf("Stopping at block %u\n", i);
						++error;
						goto bail;
						/* LCOV_EXCL_STOP */
					}
				}
			}

			/* for each disk, mark the blocks as processed */
			for(j=0;j<diskmax;++j) {
				struct snapraid_block* block = BLOCK_EMPTY;
				if (handle[j].disk)
					block = disk_block_get(handle[j].disk, i);

				if (block == BLOCK_EMPTY) {
					/* nothing to do */
					continue;
				}

				/* if it's a deleted block */
				if (block_state_get(block) == BLOCK_STATE_DELETED) {
					/* the parity is now updated without this block, so it's now empty */
					tommy_arrayblk_set(&handle[j].disk->blockarr, i, BLOCK_EMPTY);
					continue;
				}

				/* now all the blocks have the hash and the parity computed */
				block_state_set(block, BLOCK_STATE_BLK);
			}

			/* we update the info block only if we really have updated the parity */
			/* because otherwise the time info would be misleading as we didn't */
			/* wrote the parity at this time */
			/* we also update the info block only if no silent error was found */
			/* because has no sense to refresh the time for data that we know bad */
			if (parity_needs_to_be_updated
				&& !silent_error_on_this_block
			) {
				/* if rehash is neeed */
				if (rehash) {
					/* store all the new hash already computed */
					for(j=0;j<diskmax;++j) {
						if (rehandle[j].block)
							memcpy(rehandle[j].block->hash, rehandle[j].hash, HASH_SIZE);
					}
				}

				/* update the time info of the block */
				/* we are also clearing any previous bad and rehash flag */
				info_set(&state->infoarr, i, info_make(now, 0, 0));
			}
		}

		/* if a silent error was found, even if corrected */
		/* mark the block as bad to have check/fix to handle it */
		/* because our correction is in memory only and not yet written */
		if (silent_error_on_this_block) {
			/* set the error status keeping the other info */
			info_set(&state->infoarr, i, info_set_bad(info));
		}

		/* mark the state as needing write */
		state->need_write = 1;

		/* count the number of processed block */
		++countpos;

		/* progress */
		if (state_progress(state, i, countpos, countmax, countsize)) {
			/* LCOV_EXCL_START */
			break;
			/* LCOV_EXCL_STOP */
		}

		/* autosave */
		if (state->autosave != 0
			&& autosavedone >= autosavelimit /* if we have reached the limit */
			&& autosavemissing >= autosavelimit /* if we have at least a full step to do */
		) {
			autosavedone = 0; /* restart the counter */

			state_progress_stop(state);

			printf("Autosaving...\n");
			state_write(state);

			state_progress_restart(state);
		}
	}

	state_progress_end(state, countpos, countmax, countsize);

	if (error || silent_error) {
		printf("\n");
		printf("%8u read/write errors\n", error);
		printf("%8u data errors\n", silent_error);
		printf("WARNING! There are errors!\n");
	} else {
		/* print the result only if processed something */
		if (countpos != 0)
			printf("Everything OK\n");
	}

	fprintf(stdlog, "summary:error_readwrite:%u\n", error);
	fprintf(stdlog, "summary:error_data:%u\n", silent_error);
	if (error + silent_error == 0)
		fprintf(stdlog, "summary:exit:ok\n");
	else
		fprintf(stdlog, "summary:exit:error\n");
	fflush(stdlog);

bail:
	for(j=0;j<diskmax;++j) {
		ret = handle_close(&handle[j]);
		if (ret == -1) {
			fprintf(stderr, "DANGER! Unexpected close error in a data disk.\n");
			++error;
			/* continue, as we are already exiting */
		}
	}

	free(handle);
	free(buffer_alloc);
	free(buffer);
	free(rehandle_alloc);
	free(failed);
	free(failed_map);

	if (state->opt.expect_recoverable) {
		if (error + silent_error == 0)
			return -1;
	} else {
		if (error + silent_error != 0)
			return -1;
	}
	return 0;
}

int state_sync(struct snapraid_state* state, block_off_t blockstart, block_off_t blockcount)
{
	block_off_t blockmax;
	data_off_t loaded_size;
	data_off_t size;
	data_off_t out_size;
	int ret;
	struct snapraid_parity parity[LEV_MAX];
	struct snapraid_parity* parity_ptr[LEV_MAX];
	unsigned unrecoverable_error;
	unsigned l;
	unsigned level;
	int skip_sync = 0;

	printf("Initializing...\n");

	level = state->level;
	blockmax = parity_size(state);
	size = blockmax * (data_off_t)state->block_size;
	loaded_size = state->loaded_paritymax * (data_off_t)state->block_size;

	if (blockstart > blockmax) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Error in the starting block %u. It's bigger than the parity size %u.\n", blockstart, blockmax);
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* adjust the number of block to process */
	if (blockcount != 0 && blockstart + blockcount < blockmax) {
		blockmax = blockstart + blockcount;
	}

	for(l=0;l<level;++l) {
		/* create the file and open for writing */
		parity_ptr[l] = &parity[l];
		ret = parity_create(parity_ptr[l], state->parity_path[l], &out_size, state->opt.skip_sequential);
		if (ret == -1) {
			/* LCOV_EXCL_START */
			fprintf(stderr, "WARNING! Without an accessible %s file, it isn't possible to sync.\n", lev_name(l));
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		/* if the file is too small */
		if (out_size < loaded_size) {
			/* LCOV_EXCL_START */
			fprintf(stderr, "DANGER! The %s file %s is smaller than the expected %" PRId64 ".\n", lev_name(l), state->parity_path[l], loaded_size);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		/* change the size of the parity file, truncating or extending it */
		/* from this point all the DELETED blocks after the end of the parity are invalid */
		/* and they are automatically removed when we save the new content file */
		ret = parity_chsize(parity_ptr[l], size, &out_size, state->opt.skip_fallocate);
		if (ret == -1) {
			/* LCOV_EXCL_START */
			parity_overflow(state, out_size);
			fprintf(stderr, "WARNING! Without an accessible %s file, it isn't possible to sync.\n", lev_name(l));
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}

	unrecoverable_error = 0;

	if (state->opt.prehash) {
		printf("Hashing...\n");

		skip_sync = state_hash_process(state, blockstart, blockmax);

		if (state->need_write)
			state_write(state);
	}

	if (!skip_sync) {
		printf("Syncing...\n");

		/* skip degenerated cases of empty parity, or skipping all */
		if (blockstart < blockmax) {
			ret = state_sync_process(state, parity_ptr, blockstart, blockmax);
			if (ret == -1) {
				/* LCOV_EXCL_START */
				++unrecoverable_error;
				/* continue, as we are already exiting */
				/* LCOV_EXCL_STOP */
			}
		} else {
			printf("Nothing to do\n");
		}
	}

	for(l=0;l<level;++l) {
		ret = parity_sync(parity_ptr[l]);
		if (ret == -1) {
			/* LCOV_EXCL_START */
			fprintf(stderr, "DANGER! Unexpected sync error in %s disk.\n", lev_name(l));
			++unrecoverable_error;
			/* continue, as we are already exiting */
			/* LCOV_EXCL_STOP */
		}

		ret = parity_close(parity_ptr[l]);
		if (ret == -1) {
			/* LCOV_EXCL_START */
			fprintf(stderr, "DANGER! Unexpected close error in %s disk.\n", lev_name(l));
			++unrecoverable_error;
			/* continue, as we are already exiting */
			/* LCOV_EXCL_STOP */
		}
	}

	/* abort if required */
	if (unrecoverable_error != 0)
		return -1;
	return 0;
}

