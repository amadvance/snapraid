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
#include "raid/raid.h"

/****************************************************************************/
/* scrub */

/**
 * Buffer for storing the new hashes.
 */
struct snapraid_rehash {
	unsigned char hash[HASH_SIZE];
	struct snapraid_block* block;
};

/**
 * Scrub plan to use.
 */
struct snapraid_plan {
	int plan; /**< One of the SCRUB_*. */
	time_t timelimit; /**< Time limit. Valid only with SCRUB_AUTO. */
	block_off_t lastlimit; /**< Number of blocks allowed with time exactly at ::timelimit. */
	block_off_t countlast; /**< Counter of blocks with time exactly at ::timelimit. */
};

/**
 * Check if we have to process the specified block index ::i.
 */
static int block_is_enabled(struct snapraid_state* state, block_off_t i, struct snapraid_plan* plan)
{
	time_t blocktime;
	snapraid_info info;

	/* don't scrub unused blocks in all plans */
	info = info_get(&state->infoarr, i);
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
		if (plan->countlast >= plan->lastlimit) {
			/* skip it */
			return 0;
		}

		++plan->countlast;
	}

	return 1;
}

static int state_scrub_process(struct snapraid_state* state, struct snapraid_parity_handle** parity, block_off_t blockstart, block_off_t blockmax, struct snapraid_plan* plan, time_t now)
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
	unsigned io_error;
	unsigned l;

	/* maps the disks to handles */
	handle = handle_map(state, &diskmax);

	/* rehash buffers */
	rehandle = malloc_nofail_align(diskmax * sizeof(struct snapraid_rehash), &rehandle_alloc);

	/* we need disk + 2 for each parity level buffers */
	buffermax = diskmax + state->level * 2;

	buffer = malloc_nofail_vector_align(diskmax, buffermax, state->block_size, &buffer_alloc);
	if (!state->opt.skip_self)
		mtest_vector(buffermax, state->block_size, buffer);

	error = 0;
	silent_error = 0;
	io_error = 0;

	/* first count the number of blocks to process */
	countmax = 0;
	plan->countlast = 0;
	for (i = blockstart; i < blockmax; ++i) {
		if (!block_is_enabled(state, i, plan))
			continue;

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
	plan->countlast = 0;
	state_progress_begin(state, blockstart, blockmax, countmax);
	for (i = blockstart; i < blockmax; ++i) {
		snapraid_info info;
		int error_on_this_block;
		int silent_error_on_this_block;
		int io_error_on_this_block;
		int block_is_unsynced;
		int rehash;

		if (!block_is_enabled(state, i, plan))
			continue;

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
		info = info_get(&state->infoarr, i);

		/* if we have to use the old hash */
		rehash = info_get_rehash(info);

		/* for each disk, process the block */
		for (j = 0; j < diskmax; ++j) {
			int read_size;
			unsigned char hash[HASH_SIZE];
			struct snapraid_block* block;
			int file_is_unsynced;
			struct snapraid_disk* disk = handle[j].disk;
			struct snapraid_file* file;
			block_off_t file_pos;

			/* if the file on this disk is synced */
			/* if not, silent errors are assumed as expected error */
			file_is_unsynced = 0;

			/* by default no rehash in case of "continue" */
			rehandle[j].block = 0;

			/* if the disk position is not used */
			if (!disk) {
				/* use an empty block */
				memset(buffer[j], 0, state->block_size);
				continue;
			}

			/* if the block is not used */
			block = fs_par2block_get(disk, i);
			if (!block_has_file(block)) {
				/* use an empty block */
				memset(buffer[j], 0, state->block_size);
				continue;
			}

			/* get the file of this block */
			file = fs_par2file_get(disk, i, &file_pos);

			/* if the block is unsynced, errors are expected */
			if (block_has_invalid_parity(block)) {
				/* report that the block and the file are not synced */
				block_is_unsynced = 1;
				file_is_unsynced = 1;
				/* follow */
			}

			/* until now is CPU */
			state_usage_cpu(state);

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
						log_tag("error:%u:%s:%s: Close EIO error. %s\n", i, disk->name, esc(report->sub), strerror(errno));
						log_fatal("DANGER! Unexpected input/output close error in a data disk, it isn't possible to scrub.\n");
						log_fatal("Ensure that disk '%s' is sane and that file '%s' can be accessed.\n", disk->dir, handle[j].path);
						log_fatal("Stopping at block %u\n", i);
						++io_error;
						goto bail;
					}

					log_tag("error:%u:%s:%s: Close error. %s\n", i, disk->name, esc(report->sub), strerror(errno));
					log_fatal("WARNING! Unexpected close error in a data disk, it isn't possible to scrub.\n");
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
					log_tag("error:%u:%s:%s: Open EIO error. %s\n", i, disk->name, esc(file->sub), strerror(errno));
					log_fatal("DANGER! Unexpected input/output open error in a data disk, it isn't possible to scrub.\n");
					log_fatal("Ensure that disk '%s' is sane and that file '%s' can be accessed.\n", disk->dir, handle[j].path);
					log_fatal("Stopping at block %u\n", i);
					++io_error;
					goto bail;
					/* LCOV_EXCL_STOP */
				}

				log_tag("error:%u:%s:%s: Open error. %s\n", i, disk->name, esc(file->sub), strerror(errno));
				++error;
				error_on_this_block = 1;
				continue;
			}

			/* check if the file is changed */
			if (handle[j].st.st_size != file->size
				|| handle[j].st.st_mtime != file->mtime_sec
				|| STAT_NSEC(&handle[j].st) != file->mtime_nsec
				/* don't check the inode to support file-system without persistent inodes */
			) {
				/* report that the block and the file are not synced */
				block_is_unsynced = 1;
				file_is_unsynced = 1;
				/* follow */
			}

			/* note that we intentionally don't abort if the file has different attributes */
			/* from the last sync, as we are expected to return errors if running */
			/* in an unsynced array. This is just like the check command. */

			read_size = handle_read(&handle[j], file_pos, buffer[j], state->block_size, log_error, 0);
			if (read_size == -1) {
				if (errno == EIO) {
					log_tag("error:%u:%s:%s: Read EIO error at position %u. %s\n", i, disk->name, esc(file->sub), file_pos, strerror(errno));
					if (io_error >= state->opt.io_error_limit) {
						/* LCOV_EXCL_START */
						log_fatal("DANGER! Too many input/output read error in a data disk, it isn't possible to scrub.\n");
						log_fatal("Ensure that disk '%s' is sane and that file '%s' can be accessed.\n", disk->dir, handle[j].path);
						log_fatal("Stopping at block %u\n", i);
						++io_error;
						goto bail;
						/* LCOV_EXCL_STOP */
					}

					log_error("Input/Output error in file '%s' at position '%u'\n", handle[j].path, file_pos);
					++io_error;
					io_error_on_this_block = 1;
					continue;
				}

				log_tag("error:%u:%s:%s: Read error at position %u. %s\n", i, disk->name, esc(file->sub), file_pos, strerror(errno));
				++error;
				error_on_this_block = 1;
				continue;
			}

			/* until now is disk */
			state_usage_disk(state, disk);

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
					unsigned diff = memdiff(hash, block->hash, HASH_SIZE);

					log_tag("error:%u:%s:%s: Data error at position %u, diff bits %u\n", i, disk->name, esc(file->sub), file_pos, diff);

					/* it's a silent error only if we are dealing with synced files */
					if (file_is_unsynced) {
						++error;
						error_on_this_block = 1;
					} else {
						log_error("Data error in file '%s' at position '%u', diff bits %u\n", handle[j].path, file_pos, diff);
						++silent_error;
						silent_error_on_this_block = 1;
					}
					continue;
				}
			}
		}

		/* if we have read all the data required and it's correct, proceed with the parity check */
		if (!error_on_this_block && !silent_error_on_this_block && !io_error_on_this_block) {
			unsigned char* buffer_recov[LEV_MAX];

			/* until now is CPU */
			state_usage_cpu(state);

			/* buffers for parity read and not computed */
			for (l = 0; l < state->level; ++l)
				buffer_recov[l] = buffer[diskmax + state->level + l];
			for (; l < LEV_MAX; ++l)
				buffer_recov[l] = 0;

			/* read the parity */
			for (l = 0; l < state->level; ++l) {
				ret = parity_read(parity[l], i, buffer_recov[l], state->block_size, log_error);
				if (ret == -1) {
					buffer_recov[l] = 0;

					if (errno == EIO) {
						log_tag("parity_error:%u:%s: Read EIO error. %s\n", i, lev_config_name(l), strerror(errno));
						if (io_error >= state->opt.io_error_limit) {
							/* LCOV_EXCL_START */
							log_fatal("DANGER! Too many input/output read error in the %s disk, it isn't possible to scrub.\n", lev_name(l));
							log_fatal("Ensure that disk '%s' is sane and can be read.\n", lev_config_name(l));
							log_fatal("Stopping at block %u\n", i);
							++io_error;
							goto bail;
							/* LCOV_EXCL_STOP */
						}

						log_error("Input/Output error in parity '%s' at position '%u'\n", lev_config_name(l), i);
						++io_error;
						io_error_on_this_block = 1;
						continue;
					}

					log_tag("parity_error:%u:%s: Read error. %s\n", i, lev_config_name(l), strerror(errno));
					++error;
					error_on_this_block = 1;
					continue;
				}

				/* until now is parity */
				state_usage_parity(state, l);
			}

			/* compute the parity */
			raid_gen(diskmax, state->level, state->block_size, buffer);

			/* compare the parity */
			for (l = 0; l < state->level; ++l) {
				if (buffer_recov[l] && memcmp(buffer[diskmax + l], buffer_recov[l], state->block_size) != 0) {
					unsigned diff = memdiff(buffer[diskmax + l], buffer_recov[l], state->block_size);

					log_tag("parity_error:%u:%s: Data error, diff bits %u\n", i, lev_config_name(l), diff);

					/* it's a silent error only if we are dealing with synced blocks */
					if (block_is_unsynced) {
						++error;
						error_on_this_block = 1;
					} else {
						log_fatal("Data error in parity '%s' at position '%u', diff bits %u\n", lev_config_name(l), i, diff);
						++silent_error;
						silent_error_on_this_block = 1;
					}
				}
			}
		}

		if (silent_error_on_this_block || io_error_on_this_block) {
			/* set the error status keeping other info */
			info_set(&state->infoarr, i, info_set_bad(info));
		} else if (error_on_this_block) {
			/* do nothing, as this is a generic error */
			/* likely caused by a not synced array */
		} else {
			/* if rehash is needed */
			if (rehash) {
				/* store all the new hash already computed */
				for (j = 0; j < diskmax; ++j) {
					if (rehandle[j].block)
						memcpy(rehandle[j].block->hash, rehandle[j].hash, HASH_SIZE);
				}
			}

			/* update the time info of the block */
			/* and clear any other flag */
			info_set(&state->infoarr, i, info_make(now, 0, 0, 0));
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

			/* until now is CPU */
			state_usage_cpu(state);

			state_progress_stop(state);

			msg_progress("Autosaving...\n");
			state_write(state);

			state_progress_restart(state);

			/* drop until now */
			state_usage_waste(state);
		}
	}

	state_progress_end(state, countpos, countmax, countsize);

	state_usage_print(state);

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
	for (j = 0; j < diskmax; ++j) {
		struct snapraid_file* file = handle[j].file;
		struct snapraid_disk* disk = handle[j].disk;
		ret = handle_close(&handle[j]);
		if (ret == -1) {
			/* LCOV_EXCL_START */
			log_tag("error:%u:%s:%s: Close error. %s\n", i, disk->name, esc(file->sub), strerror(errno));
			log_fatal("DANGER! Unexpected close error in a data disk.\n");
			++error;
			/* continue, as we are already exiting */
			/* LCOV_EXCL_STOP */
		}
	}

	free(handle);
	free(buffer_alloc);
	free(buffer);
	free(rehandle_alloc);

	if (state->opt.expect_recoverable) {
		if (error + silent_error + io_error == 0)
			return -1;
	} else {
		if (error + silent_error + io_error != 0)
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
	block_off_t i;
	block_off_t count;
	time_t recentlimit;
	int ret;
	struct snapraid_parity_handle parity[LEV_MAX];
	struct snapraid_parity_handle* parity_ptr[LEV_MAX];
	struct snapraid_plan ps;
	time_t* timemap;
	unsigned error;
	time_t now;
	unsigned l;

	/* get the present time */
	now = time(0);

	msg_progress("Initializing...\n");

	if ((plan == SCRUB_BAD || plan == SCRUB_NEW || plan == SCRUB_FULL)
		&& olderthan >= 0) {
		/* LCOV_EXCL_START */
		log_fatal("You can specify -o, --older-than only with a numeric percentage.\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	blockmax = parity_allocated_size(state);

	/* preinitialize to avoid warnings */
	countlimit = 0;
	recentlimit = 0;

	if (state->opt.force_scrub_even) {
		ps.plan = SCRUB_EVEN;
	} else if (plan == SCRUB_FULL) {
		ps.plan = SCRUB_FULL;
	} else if (plan == SCRUB_NEW) {
		ps.plan = SCRUB_NEW;
	} else if (plan == SCRUB_BAD) {
		ps.plan = SCRUB_BAD;
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
	}

	/* identify the time limit */
	/* we sort all the block times, and we identify the time limit for which we reach the quota */
	/* this allow to process first the oldest blocks */
	timemap = malloc_nofail(blockmax * sizeof(time_t));

	/* copy the info in the temp vector */
	count = 0;
	log_tag("block_count:%u\n", blockmax);
	for (i = 0; i < blockmax; ++i) {
		snapraid_info info = info_get(&state->infoarr, i);

		/* skip unused blocks */
		if (info == 0)
			continue;

		timemap[count++] = info_get_time(info);
	}

	if (!count) {
		/* LCOV_EXCL_START */
		log_fatal("The array appears to be empty.\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* sort it */
	qsort(timemap, count, sizeof(time_t), time_compare);

	/* output the info map */
	i = 0;
	log_tag("info_count:%u\n", count);
	while (i < count) {
		unsigned j = i + 1;
		while (j < count && timemap[i] == timemap[j])
			++j;
		log_tag("info_time:%" PRIu64 ":%u\n", (uint64_t)timemap[i], j - i);
		i = j;
	}

	/* compute the limits from count/recentlimit */
	if (ps.plan == SCRUB_AUTO) {
		/* no more than the full count */
		if (countlimit > count)
			countlimit = count;

		/* decrease until we reach the specific recentlimit */
		while (countlimit > 0 && timemap[countlimit - 1] > recentlimit)
			--countlimit;

		/* if there is something to scrub */
		if (countlimit > 0) {
			/* get the most recent time we want to scrub */
			ps.timelimit = timemap[countlimit - 1];

			/* count how many entries for this exact time we have to scrub */
			/* if the blocks have all the same time, we end with countlimit == lastlimit */
			ps.lastlimit = 1;
			while (countlimit > ps.lastlimit && timemap[countlimit - ps.lastlimit - 1] == ps.timelimit)
				++ps.lastlimit;
		} else {
			/* if nothing to scrub, disable also other limits */
			ps.timelimit = 0;
			ps.lastlimit = 0;
		}

		log_tag("count_limit:%u\n", countlimit);
		log_tag("time_limit:%" PRIu64 "\n", (uint64_t)ps.timelimit);
		log_tag("last_limit:%u\n", ps.lastlimit);
	}

	/* free the temp vector */
	free(timemap);

	/* open the file for reading */
	for (l = 0; l < state->level; ++l) {
		parity_ptr[l] = &parity[l];
		ret = parity_open(parity_ptr[l], state->parity[l].path, state->file_mode);
		if (ret == -1) {
			/* LCOV_EXCL_START */
			log_fatal("WARNING! Without an accessible %s file, it isn't possible to scrub.\n", lev_name(l));
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}

	msg_progress("Scrubbing...\n");

	error = 0;

	ret = state_scrub_process(state, parity_ptr, 0, blockmax, &ps, now);
	if (ret == -1) {
		++error;
		/* continue, as we are already exiting */
	}

	for (l = 0; l < state->level; ++l) {
		ret = parity_close(parity_ptr[l]);
		if (ret == -1) {
			/* LCOV_EXCL_START */
			log_fatal("DANGER! Unexpected close error in %s disk.\n", lev_name(l));
			++error;
			/* continue, as we are already exiting */
			/* LCOV_EXCL_STOP */
		}
	}

	/* abort if required */
	if (error != 0)
		return -1;
	return 0;
}

