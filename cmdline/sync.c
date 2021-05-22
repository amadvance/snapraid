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

#include "support.h"
#include "elem.h"
#include "state.h"
#include "parity.h"
#include "handle.h"
#include "io.h"
#include "raid/raid.h"

/****************************************************************************/
/* hash */

static int state_hash_process(struct snapraid_state* state, block_off_t blockstart, block_off_t blockmax, int* skip_sync)
{
	struct snapraid_handle* handle;
	unsigned diskmax;
	block_off_t i;
	unsigned j;
	void* buffer;
	void* buffer_alloc;
	data_off_t countsize;
	block_off_t countpos;
	block_off_t countmax;
	int ret;
	unsigned error;
	unsigned silent_error;
	unsigned io_error;
	char esc_buffer[ESC_MAX];

	/* maps the disks to handles */
	handle = handle_mapping(state, &diskmax);

	/* buffer for reading */
	buffer = malloc_nofail_direct(state->block_size, &buffer_alloc);
	if (!state->opt.skip_self)
		mtest_vector(1, state->block_size, &buffer);

	error = 0;
	silent_error = 0;
	io_error = 0;

	/* first count the number of blocks to process */
	countmax = 0;
	for (j = 0; j < diskmax; ++j) {
		struct snapraid_disk* disk = handle[j].disk;

		/* if no disk, nothing to check */
		if (!disk)
			continue;

		for (i = blockstart; i < blockmax; ++i) {
			struct snapraid_block* block;
			unsigned block_state;

			block = fs_par2block_find(disk, i);

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
	if (!state_progress_begin(state, blockstart, blockmax, countmax))
		goto end;

	for (j = 0; j < diskmax; ++j) {
		struct snapraid_disk* disk = handle[j].disk;

		/* if no disk, nothing to check */
		if (!disk)
			continue;

		for (i = blockstart; i < blockmax; ++i) {
			snapraid_info info;
			int rehash;
			struct snapraid_block* block;
			int read_size;
			unsigned char hash[HASH_MAX];
			unsigned block_state;
			struct snapraid_file* file;
			block_off_t file_pos;

			block = fs_par2block_find(disk, i);

			/* get the state of the block */
			block_state = block_state_get(block);

			/* process REP and CHG blocks */
			if (block_state != BLOCK_STATE_REP && block_state != BLOCK_STATE_CHG)
				continue;

			/* get the file of this block */
			file = fs_par2file_get(disk, i, &file_pos);

			/* get block specific info */
			info = info_get(&state->infoarr, i);

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
					/* This one is really an unexpected error, because we are only reading */
					/* and closing a descriptor should never fail */
					if (errno == EIO) {
						log_tag("error:%u:%s:%s: Close EIO error. %s\n", i, disk->name, esc_tag(report->sub, esc_buffer), strerror(errno));
						log_fatal("DANGER! Unexpected input/output close error in a data disk, it isn't possible to sync.\n");
						log_fatal("Ensure that disk '%s' is sane and that file '%s' can be accessed.\n", disk->dir, handle[j].path);
						log_fatal("Stopping at block %u\n", i);
						++io_error;
						goto bail;
					}

					log_tag("error:%u:%s:%s: Close error. %s\n", i, disk->name, esc_tag(report->sub, esc_buffer), strerror(errno));
					log_fatal("WARNING! Unexpected close error in a data disk, it isn't possible to sync.\n");
					log_fatal("Ensure that file '%s' can be accessed.\n", handle[j].path);
					log_fatal("Stopping at block %u\n", i);
					++error;
					goto bail;
					/* LCOV_EXCL_STOP */
				}
			}

			ret = handle_open(&handle[j], file, state->file_mode, log_error, 0);
			if (ret == -1) {
				if (errno == EIO) {
					/* LCOV_EXCL_START */
					log_tag("error:%u:%s:%s: Open EIO error. %s\n", i, disk->name, esc_tag(file->sub, esc_buffer), strerror(errno));
					log_fatal("DANGER! Unexpected input/output open error in a data disk, it isn't possible to sync.\n");
					log_fatal("Ensure that disk '%s' is sane and that file '%s' can be accessed.\n", disk->dir, handle[j].path);
					log_fatal("Stopping at block %u\n", i);
					++io_error;
					goto bail;
					/* LCOV_EXCL_STOP */
				}

				if (errno == ENOENT) {
					log_tag("error:%u:%s:%s: Open ENOENT error. %s\n", i, disk->name, esc_tag(file->sub, esc_buffer), strerror(errno));
					log_error("Missing file '%s'.\n", handle[j].path);
					log_error("WARNING! You cannot modify data disk during a sync.\n");
					log_error("Rerun the sync command when finished.\n");
					++error;
					/* if the file is missing, it means that it was removed during sync */
					/* this isn't a serious error, so we skip this block, and continue with others */
					continue;
				}

				if (errno == EACCES) {
					log_tag("error:%u:%s:%s: Open EACCES error. %s\n", i, disk->name, esc_tag(file->sub, esc_buffer), strerror(errno));
					log_error("No access at file '%s'.\n", handle[j].path);
					log_error("WARNING! Please fix the access permission in the data disk.\n");
					log_error("Rerun the sync command when finished.\n");
					++error;
					/* this isn't a serious error, so we skip this block, and continue with others */
					continue;
				}

				/* LCOV_EXCL_START */
				log_tag("error:%u:%s:%s: Open error. %s\n", i, disk->name, esc_tag(file->sub, esc_buffer), strerror(errno));
				log_fatal("WARNING! Unexpected open error in a data disk, it isn't possible to sync.\n");
				log_fatal("Ensure that file '%s' can be accessed.\n", handle[j].path);
				log_fatal("Stopping to allow recovery. Try with 'snapraid check -f /%s'\n", fmt_poll(disk, file->sub, esc_buffer));
				++error;
				goto bail;
				/* LCOV_EXCL_STOP */
			}

			/* check if the file is changed */
			if (handle[j].st.st_size != file->size
				|| handle[j].st.st_mtime != file->mtime_sec
				|| STAT_NSEC(&handle[j].st) != file->mtime_nsec
				|| handle[j].st.st_ino != file->inode
			) {
				log_tag("error:%u:%s:%s: Unexpected attribute change\n", i, disk->name, esc_tag(file->sub, esc_buffer));
				if (handle[j].st.st_size != file->size) {
					log_error("Unexpected size change at file '%s' from %" PRIu64 " to %" PRIu64 ".\n", handle[j].path, file->size, (uint64_t)handle[j].st.st_size);
				} else if (handle[j].st.st_mtime != file->mtime_sec
					|| STAT_NSEC(&handle[j].st) != file->mtime_nsec) {
					log_error("Unexpected time change at file '%s' from %" PRIu64 ".%d to %" PRIu64 ".%d.\n", handle[j].path, file->mtime_sec, file->mtime_nsec, (uint64_t)handle[j].st.st_mtime, STAT_NSEC(&handle[j].st));
				} else {
					log_error("Unexpected inode change from %" PRIu64 " to %" PRIu64 " at file '%s'.\n", file->inode, (uint64_t)handle[j].st.st_ino, handle[j].path);
				}
				log_error("WARNING! You cannot modify files during a sync.\n");
				log_error("Rerun the sync command when finished.\n");
				++error;
				/* if the file is changed, it means that it was modified during sync */
				/* this isn't a serious error, so we skip this block, and continue with others */
				continue;
			}

			read_size = handle_read(&handle[j], file_pos, buffer, state->block_size, log_fatal, 0);
			if (read_size == -1) {
				/* LCOV_EXCL_START */
				if (errno == EIO) {
					log_tag("error:%u:%s:%s: Read EIO error at position %u. %s\n", i, disk->name, esc_tag(file->sub, esc_buffer), file_pos, strerror(errno));
					log_fatal("DANGER! Unexpected input/output read error in a data disk, it isn't possible to sync.\n");
					log_fatal("Ensure that disk '%s' is sane and that file '%s' can be read.\n", disk->dir, handle[j].path);
					log_fatal("Stopping at block %u\n", i);
					++io_error;
					goto bail;
				}

				log_tag("error:%u:%s:%s: Read error at position %u. %s\n", i, disk->name, esc_tag(file->sub, esc_buffer), file_pos, strerror(errno));
				log_fatal("WARNING! Unexpected read error in a data disk, it isn't possible to sync.\n");
				log_fatal("Ensure that file '%s' can be read.\n", handle[j].path);
				log_fatal("Stopping to allow recovery. Try with 'snapraid check -f /%s'\n", fmt_poll(disk, file->sub, esc_buffer));
				++error;
				goto bail;
				/* LCOV_EXCL_STOP */
			}

			/* until now is disk */
			state_usage_disk(state, handle, &j, 1);

			state_usage_file(state, disk, file);

			countsize += read_size;

			/* now compute the hash */
			if (rehash) {
				memhash(state->prevhash, state->prevhashseed, hash, buffer, read_size);
			} else {
				memhash(state->hash, state->hashseed, hash, buffer, read_size);
			}

			/* until now is hash */
			state_usage_hash(state);

			if (block_state == BLOCK_STATE_REP) {
				/* compare the hash */
				if (memcmp(hash, block->hash, BLOCK_HASH_SIZE) != 0) {
					log_tag("error:%u:%s:%s: Unexpected data change\n", i, disk->name, esc_tag(file->sub, esc_buffer));
					log_error("Data change at file '%s' at position '%u'\n", handle[j].path, file_pos);
					log_error("WARNING! Unexpected data modification of a file without parity!\n");

					if (file_flag_has(file, FILE_IS_COPY)) {
						log_error("This file was detected as a copy of another file with the same name, size,\n");
						log_error("and timestamp, but the file data isn't matching the assumed copy.\n");
						log_error("If this is a false positive, and the files are expected to be different,\n");
						log_error("you can 'sync' anyway using 'snapraid --force-nocopy sync'\n");
					} else {
						log_error("Try removing the file from the array and rerun the 'sync' command!\n");
					}

					/* block sync to allow a recovery before overwriting */
					/* the parity needed to make such recovery */
					*skip_sync = 1; /* avoid to run the next sync */

					++silent_error;
					continue;
				}
			} else {
				/* the only other case is BLOCK_STATE_CHG */
				assert(block_state == BLOCK_STATE_CHG);

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
			if (state_progress(state, 0, i, countpos, countmax, countsize)) {
				/* LCOV_EXCL_START */
				*skip_sync = 1; /* avoid to run the next sync */
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
				/* This one is really an unexpected error, because we are only reading */
				/* and closing a descriptor should never fail */
				if (errno == EIO) {
					log_tag("error:%u:%s:%s: Close EIO error. %s\n", blockmax, disk->name, esc_tag(report->sub, esc_buffer), strerror(errno));
					log_fatal("DANGER! Unexpected input/output close error in a data disk, it isn't possible to sync.\n");
					log_fatal("Ensure that disk '%s' is sane and that file '%s' can be accessed.\n", disk->dir, handle[j].path);
					log_fatal("Stopping at block %u\n", blockmax);
					++io_error;
					goto bail;
				}

				log_tag("error:%u:%s:%s: Close error. %s\n", blockmax, disk->name, esc_tag(report->sub, esc_buffer), strerror(errno));
				log_fatal("WARNING! Unexpected close error in a data disk, it isn't possible to sync.\n");
				log_fatal("Ensure that file '%s' can be accessed.\n", handle[j].path);
				log_fatal("Stopping at block %u\n", blockmax);
				++error;
				goto bail;
				/* LCOV_EXCL_STOP */
			}
		}
	}

end:
	state_progress_end(state, countpos, countmax, countsize);

	/* note that at this point no io_error is possible */
	/* because at the first one we bail out */
	assert(io_error == 0);

	if (error || io_error || silent_error) {
		msg_status("\n");
		msg_status("%8u file errors\n", error);
		msg_status("%8u io errors\n", io_error);
		msg_status("%8u data errors\n", silent_error);
	} else {
		/* print the result only if processed something */
		if (countpos != 0)
			msg_status("Everything OK\n");
	}

	if (error)
		log_fatal("WARNING! Unexpected file errors!\n");

	log_tag("hash_summary:error_file:%u\n", error);

	/* proceed without bailing out */
	goto finish;

bail:
	/* on bail, don't run the next sync */
	*skip_sync = 1;

	/* close files left open */
	for (j = 0; j < diskmax; ++j) {
		struct snapraid_file* file = handle[j].file;
		struct snapraid_disk* disk = handle[j].disk;
		ret = handle_close(&handle[j]);
		if (ret == -1) {
			log_tag("error:%u:%s:%s: Close error. %s\n", i, disk->name, esc_tag(file->sub, esc_buffer), strerror(errno));
			log_fatal("DANGER! Unexpected close error in a data disk.\n");
			++error;
			/* continue, as we are already exiting */
		}
	}

finish:
	free(handle);
	free(buffer_alloc);

	if (error + io_error + silent_error != 0)
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
	unsigned size; /**< Size of the block. */

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

	/* if none valid or none invalid, we don't need to update */
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
	char esc_buffer[ESC_MAX];

	/* if the disk position is not used */
	if (!disk) {
		/* use an empty block */
		memset(buffer, 0, state->block_size);
		task->state = TASK_STATE_DONE;
		return;
	}

	/* get the block */
	task->block = fs_par2block_find(disk, blockcur);

	/* if the block has no file, meaning that it's EMPTY or DELETED, */
	/* it doesn't participate in the new parity computation */
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
			/* This one is really an unexpected error, because we are only reading */
			/* and closing a descriptor should never fail */
			if (errno == EIO) {
				log_tag("error:%u:%s:%s: Close EIO error. %s\n", blockcur, disk->name, esc_tag(report->sub, esc_buffer), strerror(errno));
				log_fatal("DANGER! Unexpected input/output close error in a data disk, it isn't possible to sync.\n");
				log_fatal("Ensure that disk '%s' is sane and that file '%s' can be accessed.\n", disk->dir, handle->path);
				log_fatal("Stopping at block %u\n", blockcur);
				task->state = TASK_STATE_IOERROR;
				return;
			}

			log_tag("error:%u:%s:%s: Close error. %s\n", blockcur, disk->name, esc_tag(report->sub, esc_buffer), strerror(errno));
			log_fatal("WARNING! Unexpected close error in a data disk, it isn't possible to sync.\n");
			log_fatal("Ensure that file '%s' can be accessed.\n", handle->path);
			log_fatal("Stopping at block %u\n", blockcur);
			task->state = TASK_STATE_ERROR;
			return;
			/* LCOV_EXCL_STOP */
		}
	}

	ret = handle_open(handle, task->file, state->file_mode, log_error, 0);
	if (ret == -1) {
		if (errno == EIO) {
			/* LCOV_EXCL_START */
			log_tag("error:%u:%s:%s: Open EIO error. %s\n", blockcur, disk->name, esc_tag(task->file->sub, esc_buffer), strerror(errno));
			log_fatal("DANGER! Unexpected input/output open error in a data disk, it isn't possible to sync.\n");
			log_fatal("Ensure that disk '%s' is sane and that file '%s' can be accessed.\n", disk->dir, handle->path);
			log_fatal("Stopping at block %u\n", blockcur);
			task->state = TASK_STATE_IOERROR;
			return;
			/* LCOV_EXCL_STOP */
		}

		if (errno == ENOENT) {
			log_tag("error:%u:%s:%s: Open ENOENT error. %s\n", blockcur, disk->name, esc_tag(task->file->sub, esc_buffer), strerror(errno));
			log_error("Missing file '%s'.\n", handle->path);
			log_error("WARNING! You cannot modify data disk during a sync.\n");
			log_error("Rerun the sync command when finished.\n");
			/* if the file is missing, it means that it was removed during sync */
			/* this isn't a serious error, so we skip this block, and continue with others */
			task->state = TASK_STATE_ERROR_CONTINUE;
			return;
		}

		if (errno == EACCES) {
			log_tag("error:%u:%s:%s: Open EACCES error. %s\n", blockcur, disk->name, esc_tag(task->file->sub, esc_buffer), strerror(errno));
			log_error("No access at file '%s'.\n", handle->path);
			log_error("WARNING! Please fix the access permission in the data disk.\n");
			log_error("Rerun the sync command when finished.\n");
			/* this isn't a serious error, so we skip this block, and continue with others */
			task->state = TASK_STATE_ERROR_CONTINUE;
			return;
		}

		/* LCOV_EXCL_START */
		log_tag("error:%u:%s:%s: Open error. %s\n", blockcur, disk->name, esc_tag(task->file->sub, esc_buffer), strerror(errno));
		log_fatal("WARNING! Unexpected open error in a data disk, it isn't possible to sync.\n");
		log_fatal("Ensure that file '%s' can be accessed.\n", handle->path);
		log_fatal("Stopping to allow recovery. Try with 'snapraid check -f /%s'\n", fmt_poll(disk, task->file->sub, esc_buffer));
		task->state = TASK_STATE_ERROR;
		return;
		/* LCOV_EXCL_STOP */
	}

	/* check if the file is changed */
	if (handle->st.st_size != task->file->size
		|| handle->st.st_mtime != task->file->mtime_sec
		|| STAT_NSEC(&handle->st) != task->file->mtime_nsec
		|| handle->st.st_ino != task->file->inode
	) {
		log_tag("error:%u:%s:%s: Unexpected attribute change\n", blockcur, disk->name, esc_tag(task->file->sub, esc_buffer));
		if (handle->st.st_size != task->file->size) {
			log_error("Unexpected size change at file '%s' from %" PRIu64 " to %" PRIu64 ".\n", handle->path, task->file->size, (uint64_t)handle->st.st_size);
		} else if (handle->st.st_mtime != task->file->mtime_sec
			|| STAT_NSEC(&handle->st) != task->file->mtime_nsec) {
			log_error("Unexpected time change at file '%s' from %" PRIu64 ".%d to %" PRIu64 ".%d.\n", handle->path, task->file->mtime_sec, task->file->mtime_nsec, (uint64_t)handle->st.st_mtime, STAT_NSEC(&handle->st));
		} else {
			log_error("Unexpected inode change from %" PRIu64 " to %" PRIu64 " at file '%s'.\n", task->file->inode, (uint64_t)handle->st.st_ino, handle->path);
		}
		log_error("WARNING! You cannot modify files during a sync.\n");
		log_error("Rerun the sync command when finished.\n");
		/* if the file is changed, it means that it was modified during sync */
		/* this isn't a serious error, so we skip this block, and continue with others */
		task->state = TASK_STATE_ERROR_CONTINUE;
		return;
	}

	task->read_size = handle_read(handle, task->file_pos, buffer, state->block_size, log_error, 0);
	if (task->read_size == -1) {
		/* LCOV_EXCL_START */
		if (errno == EIO) {
			log_tag("error:%u:%s:%s: Read EIO error at position %u. %s\n", blockcur, disk->name, esc_tag(task->file->sub, esc_buffer), task->file_pos, strerror(errno));
			log_error("Input/Output error in file '%s' at position '%u'\n", handle->path, task->file_pos);
			task->state = TASK_STATE_IOERROR_CONTINUE;
			return;
		}

		log_tag("error:%u:%s:%s: Read error at position %u. %s\n", blockcur, disk->name, esc_tag(task->file->sub, esc_buffer), task->file_pos, strerror(errno));
		log_fatal("WARNING! Unexpected read error in a data disk, it isn't possible to sync.\n");
		log_fatal("Ensure that file '%s' can be read.\n", handle->path);
		log_fatal("Stopping to allow recovery. Try with 'snapraid check -f /%s'\n", fmt_poll(disk, task->file->sub, esc_buffer));
		task->state = TASK_STATE_ERROR;
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
		if (errno == EIO) {
			log_tag("parity_error:%u:%s: Write EIO error. %s\n", blockcur, lev_config_name(level), strerror(errno));
			log_error("Input/Output error in parity '%s' at position '%u'\n", lev_config_name(level), blockcur);
			task->state = TASK_STATE_IOERROR_CONTINUE;
			return;
		}

		log_tag("parity_error:%u:%s: Write error. %s\n", blockcur, lev_config_name(level), strerror(errno));
		log_fatal("WARNING! Unexpected write error in the %s disk, it isn't possible to sync.\n", lev_name(level));
		log_fatal("Ensure that disk '%s' has some free space available.\n", lev_config_name(level));
		log_fatal("Stopping at block %u\n", blockcur);
		task->state = TASK_STATE_ERROR;
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
	unsigned error;
	unsigned silent_error;
	unsigned io_error;
	time_t now;
	struct failed_struct* failed;
	int* failed_map;
	unsigned l;
	unsigned* waiting_map;
	unsigned waiting_mac;
	char esc_buffer[ESC_MAX];
	bit_vect_t* block_enabled;

	/* the sync process assumes that all the hashes are correct */
	/* including the ones from CHG and DELETED blocks */
	assert(state->clear_past_hash != 0);

	/* get the present time */
	now = time(0);

	/* maps the disks to handles */
	handle = handle_mapping(state, &diskmax);

	/* rehash buffers */
	rehandle = malloc_nofail_align(diskmax * sizeof(struct snapraid_rehash), &rehandle_alloc);

	/* we need 1 * data + 1 * parity */
	buffermax = diskmax + state->level;

	/* initialize the io threads */
	io_init(&io, state, state->opt.io_cache, buffermax, sync_data_reader, handle, diskmax, 0, sync_parity_writer, parity_handle, state->level);

	/* allocate the copy buffer */
	copy = malloc_nofail_vector_align(diskmax, diskmax, state->block_size, &copy_alloc);

	/* allocate and fill the zero buffer */
	zero = malloc_nofail_align(state->block_size, &zero_alloc);
	memset(zero, 0, state->block_size);
	raid_zero(zero);

	failed = malloc_nofail(diskmax * sizeof(struct failed_struct));
	failed_map = malloc_nofail(diskmax * sizeof(unsigned));

	/* possibly waiting disks */
	waiting_mac = diskmax > RAID_PARITY_MAX ? diskmax : RAID_PARITY_MAX;
	waiting_map = malloc_nofail(waiting_mac * sizeof(unsigned));

	error = 0;
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

	/* compute the autosave size for all disk, even if not read */
	/* this makes sense because the speed should be almost the same */
	/* if the disks are read in parallel */
	autosavelimit = state->autosave / (diskmax * state->block_size);
	autosavemissing = countmax; /* blocks to do */
	autosavedone = 0; /* blocks done */

	/* drop until now */
	state_usage_waste(state);

	countsize = 0;
	countpos = 0;

	msg_progress("Syncing...\n");

	/* start all the worker threads */
	io_start(&io, blockstart, blockmax, block_enabled);

	if (!state_progress_begin(state, blockstart, blockmax, countmax))
		goto end;

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

		/* if the parity requires to be updated */
		/* It could happens that all the blocks are EMPTY/BLK and CHG but with the hash */
		/* still matching because the specific CHG block was not modified. */
		/* In such case, we can avoid to update parity, because it would be the same as before */
		/* Note that CHG/DELETED blocks already present in the content file loaded */
		/* have the hash cleared (::clear_past_hash flag), and then they won't never match the hash. */
		/* We are treating only CHG blocks created at runtime. */
		parity_needs_to_be_updated = state->opt.force_full || state->opt.force_parity_update;

		/* if the parity is going to be updated */
		parity_going_to_be_updated = 0;

		/* if the block is marked as bad, we force the parity update */
		/* because the bad block may be the result of a wrong parity */
		if (info_get_bad(info))
			parity_needs_to_be_updated = 1;

		/* for each disk, process the block */
		for (j = 0; j < diskmax; ++j) {
			struct snapraid_task* task;
			int read_size;
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

			/* if the block has invalid parity, */
			/* we have to take care of it in case of recover */
			if (block_has_invalid_parity(block)) {
				/* store it in the failed set, because */
				/* the parity may be still computed with the previous content */
				failed[failed_count].index = diskcur;
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
				++error;
				goto bail;
				/* LCOV_EXCL_STOP */
			}
			if (task->state == TASK_STATE_ERROR_CONTINUE) {
				++error;
				error_on_this_block = 1;
				continue;
			}
			if (task->state == TASK_STATE_IOERROR_CONTINUE) {
				++io_error;
				if (io_error >= state->opt.io_error_limit) {
					/* LCOV_EXCL_START */
					log_fatal("DANGER! Unexpected input/output read error in a data disk, it isn't possible to sync.\n");
					log_fatal("Ensure that disk '%s' is sane and that file '%s' can be read.\n", disk->dir, task->path);
					log_fatal("Stopping at block %u\n", blockcur);
					goto bail;
					/* LCOV_EXCL_STOP */
				}

				/* otherwise continue */
				io_error_on_this_block = 1;
				continue;
			}
			if (task->state != TASK_STATE_DONE) {
				/* LCOV_EXCL_START */
				log_fatal("Internal inconsistency in task state\n");
				os_abort();
				/* LCOV_EXCL_STOP */
			}

			countsize += read_size;

			/* now compute the hash */
			if (rehash) {
				memhash(state->prevhash, state->prevhashseed, hash, buffer[diskcur], read_size);

				/* compute the new hash, and store it */
				rehandle[diskcur].block = block;
				memhash(state->hash, state->hashseed, rehandle[diskcur].hash, buffer[diskcur], read_size);
			} else {
				memhash(state->hash, state->hashseed, hash, buffer[diskcur], read_size);
			}

			/* until now is hash */
			state_usage_hash(state);

			if (block_has_updated_hash(block)) {
				/* compare the hash */
				if (memcmp(hash, block->hash, BLOCK_HASH_SIZE) != 0) {
					/* if the file has invalid parity, it's a REP changed during the sync */
					if (block_has_invalid_parity(block)) {
						log_tag("error:%u:%s:%s: Unexpected data change\n", blockcur, disk->name, esc_tag(file->sub, esc_buffer));
						log_error("Data change at file '%s' at position '%u'\n", task->path, file_pos);
						log_error("WARNING! Unexpected data modification of a file without parity!\n");

						if (file_flag_has(file, FILE_IS_COPY)) {
							log_error("This file was detected as a copy of another file with the same name, size,\n");
							log_error("and timestamp, but the file data isn't matching the assumed copy.\n");
							log_error("If this is a false positive, and the files are expected to be different,\n");
							log_error("you can 'sync' anyway using 'snapraid --force-nocopy sync'\n");
						} else {
							log_error("Try removing the file from the array and rerun the 'sync' command!\n");
						}

						++error;

						/* if the file is changed, it means that it was modified during sync */
						/* this isn't a serious error, so we skip this block, and continue with others */
						error_on_this_block = 1;
						continue;
					} else { /* otherwise it's a BLK with silent error */
						unsigned diff = memdiff(hash, block->hash, BLOCK_HASH_SIZE);
						log_tag("error:%u:%s:%s: Data error at position %u, diff bits %u/%u\n", blockcur, disk->name, esc_tag(file->sub, esc_buffer), file_pos, diff, BLOCK_HASH_SIZE * 8);
						log_error("Data error in file '%s' at position '%u', diff bits %u/%u\n", task->path, file_pos, diff, BLOCK_HASH_SIZE * 8);

						/* save the failed block for the fix */
						failed[failed_count].index = diskcur;
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

					/* if the hash represents the data unequivocally */
					if (hash_is_unique(block->hash)) {
						/* check if the hash is changed */
						if (memcmp(hash, block->hash, BLOCK_HASH_SIZE) != 0) {
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
				memcpy(block->hash, hash, BLOCK_HASH_SIZE);

				/* note that in case of rehash, this is the wrong hash, */
				/* but it will be overwritten later */
			}
		}

		/* if we have only silent errors we can try to fix them on-the-fly */
		/* note the fix is not written to disk, but used only to */
		/* compute the new parity */
		if (!error_on_this_block && !io_error_on_this_block && silent_error_on_this_block) {
			unsigned failed_mac;
			int something_to_recover = 0;

			/* sort the failed vector */
			/* because with threads it may be in any order */
			/* but RAID requires the indexes to be sorted */
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
				/* until now is misc */
				state_usage_misc(state);

				/* read the parity */
				/* we are sure that parity exists because */
				/* we have at least one BLK block */
				for (l = 0; l < state->level; ++l) {
					ret = parity_read(&parity_handle[l], blockcur, buffer[diskmax + l], state->block_size, log_error);
					if (ret == -1) {
						/* LCOV_EXCL_START */
						if (errno == EIO) {
							log_tag("parity_error:%u:%s: Read EIO error. %s\n", blockcur, lev_config_name(l), strerror(errno));
							if (io_error >= state->opt.io_error_limit) {
								log_fatal("DANGER! Unexpected input/output read error in the %s disk, it isn't possible to sync.\n", lev_name(l));
								log_fatal("Ensure that disk '%s' is sane and can be read.\n", lev_config_name(l));
								log_fatal("Stopping at block %u\n", blockcur);
								++io_error;
								goto bail;
							}

							log_error("Input/Output error in parity '%s' at position '%u'\n", lev_config_name(l), blockcur);
							++io_error;
							io_error_on_this_block = 1;
							continue;
						}

						log_tag("parity_error:%u:%s: Read error. %s\n", blockcur, lev_config_name(l), strerror(errno));
						log_fatal("WARNING! Unexpected read error in the %s disk, it isn't possible to sync.\n", lev_name(l));
						log_fatal("Ensure that disk '%s' can be read.\n", lev_config_name(l));
						log_fatal("Stopping at block %u\n", blockcur);
						++error;
						goto bail;
						/* LCOV_EXCL_STOP */
					}

					/* until now is parity */
					state_usage_parity(state, &l, 1);
				}

				/* if no error in parity read */
				if (!io_error_on_this_block) {
					/* try to fix the data */
					/* note that this is a simple fix algorithm, that doesn't take into */
					/* account the case of a wrong parity */
					/* only 'fix' supports the most advanced fixing */
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
							unsigned size = failed[j].size;

							/* compute the hash of the recovered block */
							if (rehash) {
								memhash(state->prevhash, state->prevhashseed, hash, block_buffer, size);
							} else {
								memhash(state->hash, state->hashseed, hash, block_buffer, size);
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
		}

		/* if we have read all the data required and it's correct, proceed with the parity */
		if (!error_on_this_block && !io_error_on_this_block
			&& (!silent_error_on_this_block || fixed_error_on_this_block)
		) {
			/* update the parity only if really needed */
			if (parity_needs_to_be_updated) {
				/* compute the parity */
				raid_gen(diskmax, state->level, state->block_size, buffer);

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

			/* we update the info block only if we really have updated the parity */
			/* because otherwise the time/justsynced info would be misleading as we didn't */
			/* wrote the parity at this time */
			/* we also update the info block only if no silent error was found */
			/* because has no sense to refresh the time for data that we know bad */
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

				/* update the time info of the block */
				/* we are also clearing any previous bad and rehash flag */
				info_set(&state->infoarr, blockcur, info_make(now, 0, 0, 1));
			}
		}

		/* if a silent (even if corrected) or input/output error was found */
		/* mark the block as bad to have check/fix to handle it */
		/* because our correction is in memory only and not yet written */
		if (silent_error_on_this_block || io_error_on_this_block) {
			/* set the error status keeping the other info */
			info_set(&state->infoarr, blockcur, info_set_bad(info));
		}

		/* finally schedule parity write */
		/* Note that the calls to io_parity_write() are mandatory */
		/* even if the parity doesn't need to be updated */
		/* This because we want to keep track of the time usage */
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
						log_fatal("DANGER! Unexpected input/output write error in a parity disk, it isn't possible to sync.\n");
						log_fatal("Stopping at block %u\n", blockcur);
						goto bail;
						/* LCOV_EXCL_STOP */
					}
					break;
				case TASK_STATE_ERROR_CONTINUE :
					++error;
					break;
				case TASK_STATE_IOERROR :
					/* LCOV_EXCL_START */
					++io_error;
					goto bail;
					/* LCOV_EXCL_STOP */
				case TASK_STATE_ERROR :
					/* LCOV_EXCL_START */
					++error;
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
		if (state_progress(state, &io, blockcur, countpos, countmax, countsize)) {
			/* LCOV_EXCL_START */
			break;
			/* LCOV_EXCL_STOP */
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

			/* before writing the new content file we ensure that */
			/* the parity is really written flushing the disk cache */
			for (l = 0; l < state->level; ++l) {
				ret = parity_sync(&parity_handle[l]);
				if (ret == -1) {
					/* LCOV_EXCL_START */
					log_tag("parity_error:%u:%s: Sync error\n", blockcur, lev_config_name(l));
					log_fatal("DANGER! Unexpected sync error in %s disk.\n", lev_name(l));
					log_fatal("Ensure that disk '%s' is sane.\n", lev_config_name(l));
					log_fatal("Stopping at block %u\n", blockcur);
					++error;
					goto bail;
					/* LCOV_EXCL_STOP */
				}
			}

			/* now we can safely write the content file */
			state_write(state);

			state_progress_restart(state);

			/* drop until now */
			state_usage_waste(state);
		}
	}

end:
	state_progress_end(state, countpos, countmax, countsize);

	state_usage_print(state);

	/* before returning we ensure that */
	/* the parity is really written flushing the disk cache */
	for (l = 0; l < state->level; ++l) {
		ret = parity_sync(&parity_handle[l]);
		if (ret == -1) {
			/* LCOV_EXCL_START */
			log_tag("parity_error:%u:%s: Sync error\n", blockcur, lev_config_name(l));
			log_fatal("DANGER! Unexpected sync error in %s disk.\n", lev_name(l));
			log_fatal("Ensure that disk '%s' is sane.\n", lev_config_name(l));
			log_fatal("Stopping at block %u\n", blockcur);
			++error;
			goto bail;
			/* LCOV_EXCL_STOP */
		}
	}

	if (error || silent_error || io_error) {
		msg_status("\n");
		msg_status("%8u file errors\n", error);
		msg_status("%8u io errors\n", io_error);
		msg_status("%8u data errors\n", silent_error);
	} else {
		/* print the result only if processed something */
		if (countpos != 0)
			msg_status("Everything OK\n");
	}

	if (error)
		log_fatal("WARNING! Unexpected file errors!\n");
	if (io_error)
		log_fatal("DANGER! Unexpected input/output errors! The failing blocks are now marked as bad!\n");
	if (silent_error)
		log_fatal("DANGER! Unexpected data errors! The failing blocks are now marked as bad!\n");
	if (io_error || silent_error) {
		log_fatal("Use 'snapraid status' to list the bad blocks.\n");
		log_fatal("Use 'snapraid -e fix' to recover.\n");
	}

	log_tag("summary:error_file:%u\n", error);
	log_tag("summary:error_io:%u\n", io_error);
	log_tag("summary:error_data:%u\n", silent_error);
	if (error + silent_error + io_error == 0)
		log_tag("summary:exit:ok\n");
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
			log_tag("error:%u:%s:%s: Close error. %s\n", blockcur, disk->name, esc_tag(file->sub, esc_buffer), strerror(errno));
			log_fatal("DANGER! Unexpected close error in a data disk.\n");
			++error;
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
		if (error + silent_error + io_error == 0)
			return -1;
	} else {
		if (error + silent_error + io_error != 0)
			return -1;
	}
	return 0;
}

int state_sync(struct snapraid_state* state, block_off_t blockstart, block_off_t blockcount)
{
	block_off_t blockmax;
	block_off_t used_paritymax;
	block_off_t file_paritymax;
	data_off_t size;
	int ret;
	struct snapraid_parity_handle parity_handle[LEV_MAX];
	unsigned unrecoverable_error;
	unsigned l;
	int skip_sync = 0;

	msg_progress("Initializing...\n");

	blockmax = parity_allocated_size(state);
	size = blockmax * (data_off_t)state->block_size;

	/* minimum size of the parity files we expect */
	used_paritymax = parity_used_size(state);

	/* effective size of the parity files */
	file_paritymax = 0;

	if (blockstart > blockmax) {
		/* LCOV_EXCL_START */
		log_fatal("Error in the starting block %u. It's bigger than the parity size %u.\n", blockstart, blockmax);
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* adjust the number of block to process */
	if (blockcount != 0 && blockstart + blockcount < blockmax) {
		blockmax = blockstart + blockcount;
	}

	for (l = 0; l < state->level; ++l) {
		data_off_t out_size;
		block_off_t parityblocks;

		/* create the file and open for writing */
		ret = parity_create(&parity_handle[l], &state->parity[l], l, state->file_mode, state->block_size, state->opt.parity_limit_size);
		if (ret == -1) {
			/* LCOV_EXCL_START */
			log_fatal("WARNING! Without an accessible %s file, it isn't possible to sync.\n", lev_name(l));
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		/* number of block in the parity file */
		parity_size(&parity_handle[l], &out_size);
		parityblocks = out_size / state->block_size;

		/* if the file is too small */
		if (parityblocks < used_paritymax) {
			log_fatal("WARNING! The %s parity has data only %u blocks instead of %u.\n", lev_name(l), parityblocks, used_paritymax);
		}

		/* keep the smallest parity number of blocks */
		if (l == 0 || file_paritymax > parityblocks)
			file_paritymax = parityblocks;
	}

	/* if we do a full parity realloc or computation, having a wrong parity size is expected */
	if (!state->opt.force_realloc && !state->opt.force_full) {
		/* if the parities are too small */
		if (file_paritymax < used_paritymax) {
			/* LCOV_EXCL_START */
			log_fatal("DANGER! One or more the parity files are smaller than expected!\n");
			if (file_paritymax != 0) {
				log_fatal("If this happens because you are using an old content file,\n");
				log_fatal("you can 'sync' anyway using 'snapraid --force-full sync'\n");
				log_fatal("to force a full rebuild of the parity.\n");
			} else {
				log_fatal("It's possible that the parity disks are not mounted.\n");
				log_fatal("If instead you are adding a new parity level, you can 'sync' using\n");
				log_fatal("'snapraid --force-full sync' to force a full rebuild of the parity.\n");
			}
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}

	unrecoverable_error = 0;

	if (state->opt.prehash) {
		msg_progress("Hashing...\n");

		ret = state_hash_process(state, blockstart, blockmax, &skip_sync);
		if (ret == -1) {
			/* LCOV_EXCL_START */
			++unrecoverable_error;
			/* continue, in case also doing the sync if ::skip_sync is not set */
			/* LCOV_EXCL_STOP */
		}
	}

	if (!skip_sync) {
		msg_progress("Resizing...\n");

		/* now change the size of all parities */
		for (l = 0; l < state->level; ++l) {
			int is_modified;

			/* change the size of the parity file, truncating or extending it */
			/* from this point all the DELETED blocks after the end of the parity are invalid */
			/* and they are automatically removed when we save the new content file */
			ret = parity_chsize(&parity_handle[l], &state->parity[l], &is_modified, size, state->block_size, state->opt.skip_fallocate, state->opt.skip_space_holder);
			if (ret == -1) {
				/* LCOV_EXCL_START */
				data_off_t out_size;
				parity_size(&parity_handle[l], &out_size);
				parity_overflow(state, out_size);
				log_fatal("WARNING! Without a usable %s file, it isn't possible to sync.\n", lev_name(l));
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			if (is_modified)
				state->need_write = 1;
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
			log_fatal("WARNING! Skipped state write for --test-skip-content-write option.\n");
		}

		/* skip degenerated cases of empty parity, or skipping all */
		if (blockstart < blockmax) {
			ret = state_sync_process(state, parity_handle, blockstart, blockmax);
			if (ret == -1) {
				/* LCOV_EXCL_START */
				++unrecoverable_error;
				/* continue, as we are already exiting */
				/* LCOV_EXCL_STOP */
			}
		} else {
			msg_status("Nothing to do\n");
		}
	}

	for (l = 0; l < state->level; ++l) {
		ret = parity_close(&parity_handle[l]);
		if (ret == -1) {
			/* LCOV_EXCL_START */
			log_fatal("DANGER! Unexpected close error in %s disk.\n", lev_name(l));
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

