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
#include "io.h"
#include "raid/raid.h"

/****************************************************************************/
/* dry */

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

static void dry_data_reader(struct snapraid_worker* worker, struct snapraid_task* task)
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

static void dry_parity_reader(struct snapraid_worker* worker, struct snapraid_task* task)
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

static int state_dry_process(struct snapraid_state* state, struct snapraid_parity_handle* parity_handle, block_off_t blockstart, block_off_t blockmax)
{
	struct snapraid_io io;
	struct snapraid_handle* handle;
	unsigned diskmax;
	block_off_t blockcur;
	unsigned j;
	unsigned buffermax;
	int ret;
	data_off_t countsize;
	block_off_t countpos;
	block_off_t countmax;
	unsigned soft_error;
	unsigned io_error;
	unsigned l;
	unsigned* waiting_map;
	unsigned waiting_mac;
	char esc_buffer[ESC_MAX];

	handle = handle_mapping(state, &diskmax);

	/* we need 1 * data + 2 * parity */
	buffermax = diskmax + 2 * state->level;

	/* initialize the io threads */
	io_init(&io, state, state->opt.io_cache, buffermax, dry_data_reader, handle, diskmax, dry_parity_reader, 0, parity_handle, state->level);

	/* possibly waiting disks */
	waiting_mac = diskmax > RAID_PARITY_MAX ? diskmax : RAID_PARITY_MAX;
	waiting_map = malloc_nofail(waiting_mac * sizeof(unsigned));

	soft_error = 0;
	io_error = 0;

	/* drop until now */
	state_usage_waste(state);

	countmax = blockmax - blockstart;
	countsize = 0;
	countpos = 0;

	/* start all the worker threads */
	io_start(&io, blockstart, blockmax, 0);

	blockcur = blockstart;

	int alert = state_progress_begin(state, blockstart, blockmax, countmax);
	if (alert > 0)
		goto end;
	if (alert < 0)
		goto bail;

	while (1) {
		void** buffer;

		/* go to the next block */
		blockcur = io_read_next(&io, &buffer);
		if (blockcur >= blockmax)
			break;

		/* until now is scheduling */
		state_usage_sched(state);

		/* for each disk, process the block */
		for (j = 0; j < diskmax; ++j) {
			struct snapraid_task* task;
			int read_size;
			struct snapraid_block* block;
			struct snapraid_disk* disk;
			unsigned diskcur;

			/* until now is misc */
			state_usage_misc(state);

			/* get the next task */
			task = io_data_read(&io, &diskcur, waiting_map, &waiting_mac);

			/* until now is disk */
			state_usage_disk(state, handle, waiting_map, waiting_mac);

			/* get the task results */
			disk = task->disk;
			block = task->block;
			read_size = task->read_size;

			/* if the disk position is not used */
			if (!disk)
				continue;

			state_usage_file(state, disk, task->file);

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
				continue;
			}
			if (task->state != TASK_STATE_DONE) {
				/* LCOV_EXCL_START */
				log_fatal(EINTERNAL, "Internal inconsistency in task state\n");
				os_abort();
				/* LCOV_EXCL_STOP */
			}

			countsize += read_size;
		}

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
				continue;
			}
			if (task->state != TASK_STATE_DONE) {
				/* LCOV_EXCL_START */
				log_fatal(EINTERNAL, "Internal inconsistency in task state\n");
				os_abort();
				/* LCOV_EXCL_STOP */
			}
		}

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
	}

end:
	state_progress_end(state, countpos, countmax, countsize, "Nothing to dry.\n");

	state_usage_print(state);

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

	if (soft_error || io_error) {
		msg_status("\n");
		msg_status("%8u soft errors\n", soft_error);
		msg_status("%8u io errors\n", io_error);
	} else {
		msg_status("Everything OK\n");
	}

	if (soft_error)
		log_fatal(ESOFT, "DANGER! Unexpected errors!\n");
	if (io_error)
		log_fatal(EIO, "DANGER! Unexpected input/output errors!\n");

	log_tag("summary:error_soft:%u\n", soft_error);
	log_tag("summary:error_io:%u\n", io_error);
	if (soft_error + io_error == 0)
		log_tag("summary:exit:ok\n");
	else
		log_tag("summary:exit:error\n");
	log_flush();

	free(handle);
	free(waiting_map);
	io_done(&io);

	if (soft_error + io_error != 0)
		return -1;

	if (alert < 0)
		return -1;

	return 0;
}

int state_dry(struct snapraid_state* state, block_off_t blockstart, block_off_t blockcount)
{
	block_off_t blockmax;
	int ret;
	struct snapraid_parity_handle parity_handle[LEV_MAX];
	unsigned process_error;
	unsigned l;

	msg_progress("Drying...\n");

	blockmax = parity_allocated_size(state);

	if (blockstart > blockmax) {
		/* LCOV_EXCL_START */
		log_fatal(EUSER, "Error in the specified starting block %u. It's larger than the parity size %u.\n", blockstart, blockmax);
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* adjust the number of block to process */
	if (blockcount != 0 && blockstart + blockcount < blockmax) {
		blockmax = blockstart + blockcount;
	}

	/* open the file for reading */
	/* it may fail if the file doesn't exist, in this case we continue to dry the files */
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

	/* skip degenerated cases of empty parity, or skipping all */
	if (blockstart < blockmax) {
		ret = state_dry_process(state, parity_handle, blockstart, blockmax);
		if (ret == -1) {
			/* LCOV_EXCL_START */
			++process_error;
			/* continue, as we are already exiting */
			/* LCOV_EXCL_STOP */
		}
	}

	/* try to close only if opened */
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

