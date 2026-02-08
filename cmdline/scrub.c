/*
 * Copyright (C) 2013 Andrea Mazzoleni
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
/* scrub */

static const char* es(int err)
{
	if (err == EIO)
		return "error_io";
	else
		return "error";
}

static void log_fatal_errno(int err, const char* name)
{
	if (err == EIO) {
		log_fatal(err, "DANGER! Unexpected input/output error in disk %s. It isn't possible to continue.\n", name);
	} else if (err == EACCES) {
		log_fatal(err, "WARNING! Grant permission in the disk %s. It isn't possible to continue.\n", name);
	} else if (err == ENOSPC) {
		log_fatal(err, "WARNING! Ensure there is free space on the disk %s. It isn't possible to continue.\n", name);
	} else {
		log_fatal(err, "WARNING! Without a working %s disk, it isn't possible to continue.\n", name);
	}
}

static void log_error_errno(int err, const char* name)
{
	if (err == EIO) {
		log_fatal(err, "DANGER! Unexpected input/output error in disk %s.\n", name);
	}
}

/**
 * Buffer for storing the new hashes.
 */
struct snapraid_rehash {
	unsigned char hash[HASH_MAX];
	struct snapraid_block* block;
};

/**
 * Scrub plan to use.
 */
struct snapraid_plan {
	struct snapraid_state* state;
	int plan; /**< One of the SCRUB_*. */
	time_t timelimit; /**< Time limit. Valid only with SCRUB_AUTO. */
	block_off_t lastlimit; /**< Number of blocks allowed with time exactly at ::timelimit. Valid only with SCRUB_AUTO. */
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

	/* bad blocks are always scrubbed in all plans */
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
		/* in 'sync' plan, only blocks never scrubbed */
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

	/* if the time is less than the limit, always include */
	/* otherwise, check if we reached the last limit count */
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
			/* This one is really an unexpected error, because we are only reading */
			/* and closing a descriptor should never fail */
			log_tag("%s:%u:%s:%s: Close error. %s.\n", es(errno), blockcur, disk->name, esc_tag(report->sub, esc_buffer), strerror(errno));
			log_fatal_errno(errno, disk->name);
			log_fatal(errno, "Stopping at block %u\n", blockcur);

			if (errno == EIO) {
				task->state = TASK_STATE_IOERROR;
			} else {
				task->state = TASK_STATE_ERROR;
			}
			return;
			/* LCOV_EXCL_STOP */
		}
	}

	ret = handle_open(handle, task->file, state->file_mode, log_error, 0); /* for missing file don't output a message */
	if (ret == -1) {
		log_tag("%s:%u:%s:%s: Open error. %s.\n", es(errno), blockcur, disk->name, esc_tag(task->file->sub, esc_buffer), strerror(errno));
		if (errno == EIO) {
			/* LCOV_EXCL_START */
			log_fatal_errno(errno, disk->name);
			log_fatal(errno, "Stopping at block %u\n", blockcur);
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

	/* note that we intentionally don't abort if the file has different attributes */
	/* from the last sync, as we are expected to return errors if running */
	/* in an unsynced array. This is just like the check command. */

	task->read_size = handle_read(handle, task->file_pos, buffer, state->block_size, log_error, 0);
	if (task->read_size == -1) {
		log_tag("%s:%u:%s:%s: Read error at position %u. %s.\n", es(errno), blockcur, disk->name, esc_tag(task->file->sub, esc_buffer), task->file_pos, strerror(errno));
		if (errno == EIO) {
			/* LCOV_EXCL_START */
			log_error_errno(errno, disk->name);
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
	ret = parity_read(parity_handle, blockcur, buffer, state->block_size, log_error);
	if (ret == -1) {
		log_tag("parity_%s:%u:%s: Read error. %s.\n", es(errno), blockcur, lev_config_name(level), strerror(errno));
		if (errno == EIO) {
			/* LCOV_EXCL_START */
			log_error_errno(errno, lev_config_name(level));
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
	char esc_buffer[ESC_MAX];
	bit_vect_t* block_enabled;

	/* maps the disks to handles */
	handle = handle_mapping(state, &diskmax);

	/* rehash buffers */
	rehandle = malloc_nofail_align(diskmax * sizeof(struct snapraid_rehash), &rehandle_alloc);

	/* we need 1 * data + 2 * parity */
	buffermax = diskmax + 2 * state->level;

	/* initialize the io threads */
	io_init(&io, state, state->opt.io_cache, buffermax, scrub_data_reader, handle, diskmax, scrub_parity_reader, 0, parity_handle, state->level);

	/* possibly waiting disks */
	waiting_mac = diskmax > RAID_PARITY_MAX ? diskmax : RAID_PARITY_MAX;
	waiting_map = malloc_nofail(waiting_mac * sizeof(unsigned));

	soft_error = 0;
	silent_error = 0;
	io_error = 0;

	msg_progress("Selecting...\n");

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

	msg_progress("Scrubbing...\n");

	/* start all the worker threads */
	io_start(&io, blockstart, blockmax, block_enabled);

	if (!state_progress_begin(state, blockstart, blockmax, countmax))
		goto end;

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

		/* if all the blocks at this address are synced */
		/* if not, parity is not even checked */
		block_is_unsynced = 0;

		/* get block specific info */
		info = info_get(&state->infoarr, blockcur);

		/* if we have to use the old hash */
		rehash = info_get_rehash(info);

		/* for each disk, process the block */
		for (j = 0; j < diskmax; ++j) {
			struct snapraid_task* task;
			int read_size;
			unsigned char hash[HASH_MAX];
			struct snapraid_block* block;
			int file_is_unsynced;
			struct snapraid_disk* disk;
			struct snapraid_file* file;
			block_off_t file_pos;
			unsigned diskcur;

			/* if the file on this disk is synced */
			/* if not, silent errors are assumed as expected error */
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

			/* if the block is unsynced, errors are expected */
			if (block_has_invalid_parity(block)) {
				/* report that the block and the file are not synced */
				block_is_unsynced = 1;
				file_is_unsynced = 1;
				/* follow */
			}

			/* if the block is not used */
			if (!block_has_file(block))
				continue;

			/* if the block is unsynced, errors are expected */
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
					log_fatal(EIO, "Stopping at block %u\n", blockcur);
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
					unsigned diff = memdiff(hash, block->hash, BLOCK_HASH_SIZE);

					/* it's a silent error only if we are dealing with synced files */
					if (file_is_unsynced) {
						log_tag("error:%u:%s:%s: Data error at position %u, diff hash bits %u/%u\n", blockcur, disk->name, esc_tag(file->sub, esc_buffer), file_pos, diff, BLOCK_HASH_SIZE * 8);
						++soft_error;
						error_on_this_block = 1;
					} else {
						log_tag("error_data:%u:%s:%s: Data error at position %u, diff hash bits %u/%u\n", blockcur, disk->name, esc_tag(file->sub, esc_buffer), file_pos, diff, BLOCK_HASH_SIZE * 8);
						log_error(EDATA, "Data error in file '%s' at position '%u', diff hash bits %u/%u\n", task->path, file_pos, diff, BLOCK_HASH_SIZE * 8);
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
					log_fatal(EIO, "Stopping at block %u\n", blockcur);
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

		/* if we have read all the data required and it's correct, proceed with the parity check */
		if (!error_on_this_block && !silent_error_on_this_block && !io_error_on_this_block) {

			/* compute the parity */
			raid_gen(diskmax, state->level, state->block_size, buffer);

			/* compare the parity */
			for (l = 0; l < state->level; ++l) {
				if (buffer_recov[l] && memcmp(buffer[diskmax + l], buffer_recov[l], state->block_size) != 0) {
					unsigned diff = memdiff(buffer[diskmax + l], buffer_recov[l], state->block_size);

					/* it's a silent error only if we are dealing with synced blocks */
					if (block_is_unsynced) {
						log_tag("parity_error:%u:%s: Data error, diff parity bits %u/%u\n", blockcur, lev_config_name(l), diff, state->block_size * 8);
						++soft_error;
						error_on_this_block = 1;
					} else {
						log_tag("parity_error_data:%u:%s: Data error, diff parity bits %u/%u\n", blockcur, lev_config_name(l), diff, state->block_size * 8);
						log_error(EDATA, "Data error in parity '%s' at position '%u', diff parity bits %u/%u\n", lev_config_name(l), blockcur, diff, state->block_size * 8);
						++silent_error;
						silent_error_on_this_block = 1;
					}
				}
			}

			/* until now is raid */
			state_usage_raid(state);
		}

		if (silent_error_on_this_block || io_error_on_this_block) {
			/* set the error status keeping other info */
			info_set(&state->infoarr, blockcur, info_set_bad(info));
		} else if (error_on_this_block) {
			/* do nothing, as this is a generic error */
			/* likely caused by a not synced array */
		} else {
			/* if rehash is needed */
			if (rehash) {
				/* store all the new hash already computed */
				for (j = 0; j < diskmax; ++j) {
					if (rehandle[j].block)
						memcpy(rehandle[j].block->hash, rehandle[j].hash, BLOCK_HASH_SIZE);
				}
			}

			/* update the time info of the block */
			/* and clear any other flag */
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
	} else {
		msg_status("Everything OK\n");
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
			log_tag("%s:%u:%s:%s: Close error. %s.\n", es(errno), blockcur, disk->name, esc_tag(file->sub, esc_buffer), strerror(errno));
			log_fatal_errno(errno, disk->name);

			if (errno == EIO) {
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
	return 0;
}

/**
 * Return a * b / c approximated to the upper value.
 */
static uint32_t md(uint32_t a, uint32_t b, uint32_t c)
{
	uint64_t v = a;

	v *= b;
	v += c - 1;
	v /= c;

	return v;
}

int state_scrub(struct snapraid_state* state, int plan, int olderthan)
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

	if ((plan == SCRUB_BAD || plan == SCRUB_NEW || plan == SCRUB_FULL)
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
	} else if (plan == SCRUB_FULL) {
		ps.plan = SCRUB_FULL;
		msg_progress("Scrub plan: full. All data blocks will be checked.\n");
	} else if (plan == SCRUB_NEW) {
		ps.plan = SCRUB_NEW;
		msg_progress("Scrub plan: new. Only blocks that have never been scrubbed will be checked.\n");
	} else if (plan == SCRUB_BAD) {
		ps.plan = SCRUB_BAD;
		msg_progress("Scrub plan: bad. Only blocks previously marked as bad will be checked.\n");
	} else if (state->opt.force_scrub_at) {
		/* scrub the specified amount of blocks */
		ps.plan = SCRUB_AUTO;
		countlimit = state->opt.force_scrub_at;
		recentlimit = now;
	} else {
		ps.plan = SCRUB_AUTO;
		if (plan >= 0) {
			countlimit = md(blockmax, plan, 100);
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

		if (plan >= 0) {
			if (olderthan >= 0)
				msg_progress("Scrub plan: auto. %d%% of the array, older than %d days, will be checked.\n", plan, olderthan);
			else
				msg_progress("Scrub plan: auto. %d%% of the array, older than 10 days, will be checked.\n", plan);
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

		/* if nothing to scrub, disable also other limits */
		if (processed_count == 0) {
			ps.timelimit = 0;
			ps.lastlimit = 0;
		}

		log_tag("count_limit:%u\n", countlimit);
		log_tag("time_limit:%" PRIu64 "\n", (uint64_t)ps.timelimit);
		log_tag("last_limit:%u\n", ps.lastlimit);
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
			log_tag("parity_%s:%u:%s: Open error. %s.\n", es(errno), blockmax, lev_config_name(l), strerror(errno));
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
			log_tag("parity_%s:%u:%s: Close error. %s.\n", es(errno), blockmax, lev_config_name(l), strerror(errno));
			log_fatal_errno(errno, lev_config_name(l));

			++process_error;
			/* continue, as we are already exiting */
			/* LCOV_EXCL_STOP */
		}
	}

	if (process_error != 0)
		return -1;
	return 0;
}

