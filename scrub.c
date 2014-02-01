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

#include "util.h"
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

static int state_scrub_process(struct snapraid_state* state, struct snapraid_parity** parity, block_off_t blockstart, block_off_t blockmax, time_t timelimit, block_off_t countlimit, time_t now)
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
	block_off_t recountmax;
	block_off_t autosavedone;
	block_off_t autosavelimit;
	block_off_t autosavemissing;
	int ret;
	unsigned error;
	unsigned silent_error;
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

	/* first count the number of blocks to process */
	countmax = 0;
	for(i=blockstart;i<blockmax;++i) {
		time_t blocktime;
		snapraid_info info;

		/* if it's unused */
		info = info_get(&state->infoarr, i);
		if (info == 0) {
			/* skip it */
			continue;
		}

		/* blocks marked as bad are always checked */
		if (!info_get_bad(info)) {

			/* if it's too new */
			blocktime = info_get_time(info);
			if (blocktime > timelimit) {
				/* skip it */
				continue;
			}

			/* skip odd blocks, used only for testing */
			if (state->opt.force_scrub_even && (i % 2) != 0) {
				/* skip it */
				continue;
			}

			/* if the time is less than the limit, always include */
			/* otherwise, check if we reaced the count max */
			if (blocktime == timelimit) {
				/* if we reached the count limit */
				if (countmax >= countlimit) {
					/* skip it */
					continue;
				}
			}
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
	state_progress_begin(state, blockstart, blockmax, countmax);
	recountmax = 0;
	for(i=blockstart;i<blockmax;++i) {
		time_t blocktime;
		snapraid_info info;
		int error_on_this_block;
		int silent_error_on_this_block;
		int block_is_unsynched;
		int ret;
		int rehash;

		/* if it's unused */
		info = info_get(&state->infoarr, i);
		if (info == 0) {
			/* skip it */
			continue;
		}

		/* blocks marked as bad are always checked */
		if (!info_get_bad(info)) {

			/* if it's too new */
			blocktime = info_get_time(info);
			if (blocktime > timelimit) {
				/* skip it */
				continue;
			}

			/* skip odd blocks, used only for testing */
			if (state->opt.force_scrub_even && (i % 2) != 0) {
				/* skip it */
				continue;
			}

			/* if the time is less than the limit, always include */
			/* otherwise, check if we reaced the count max */
			if (blocktime == timelimit) {
				/* if we reached the count limit */
				if (recountmax >= countlimit) {
					/* skip it */
					continue;
				}
			}
		}

		++recountmax;

		/* one more block processed for autosave */
		++autosavedone;
		--autosavemissing;

		/* by default process the block, and skip it if something goes wrong */
		error_on_this_block = 0;
		silent_error_on_this_block = 0;

		/* if all the blocks at this address are synched */
		/* if not, parity is not even checked */
		block_is_unsynched = 0;

		/* if we have to use the old hash */
		rehash = info_get_rehash(info);

		/* for each disk, process the block */
		for(j=0;j<diskmax;++j) {
			int read_size;
			unsigned char hash[HASH_SIZE];
			struct snapraid_block* block;
			int file_is_unsynched;

			/* if the file on this disk is synched */
			/* if not, silent errors are assumed as expected error */
			file_is_unsynched = 0;

			/* by default no rehash in case of "continue" */
			rehandle[j].block = 0;

			/* if the disk position is not used */
			if (!handle[j].disk) {
				/* use an empty block */
				memset(buffer[j], 0, state->block_size);
				continue;
			}

			/* if the block is not used */
			block = disk_block_get(handle[j].disk, i);
			if (!block_has_file(block)) {
				/* use an empty block */
				memset(buffer[j], 0, state->block_size);
				continue;
			}

			/* if the block is unsynched, errors are expected */
			if (block_has_invalid_parity(block)) {
				/* report that the block and the file are not synched */
				block_is_unsynched = 1;
				file_is_unsynched = 1;

				/* follow */
			}

			/* if the file is different than the current one, close it */
			if (handle[j].file != 0 && handle[j].file != block_file_get(block)) {
				/* keep a pointer at the file we are going to close for error reporting */
				struct snapraid_file* file = handle[j].file;
				ret = handle_close(&handle[j]);
				if (ret == -1) {
					/* This one is really an unexpected error, because we are only reading */
					/* and closing a descriptor should never fail */
					fprintf(stdlog, "error:%u:%s:%s: Close error. %s\n", i, handle[j].disk->name, file->sub, strerror(errno));
					fprintf(stderr, "DANGER! Unexpected close error in a data disk, it isn't possible to scrub.\n");
					printf("Stopping at block %u\n", i);
					++error;
					goto bail;
				}
			}

			ret = handle_open(&handle[j], block_file_get(block), state->opt.skip_sequential, stderr);
			if (ret == -1) {
				/* file we have tried to open for error reporting */
				struct snapraid_file* file = block_file_get(block);
				fprintf(stdlog, "error:%u:%s:%s: Open error. %s\n", i, handle[j].disk->name, file->sub, strerror(errno));
				++error;
				error_on_this_block = 1;
				continue;
			}

			/* check if the file is changed */
			if (handle[j].st.st_size != block_file_get(block)->size
				|| handle[j].st.st_mtime != block_file_get(block)->mtime_sec
				|| STAT_NSEC(&handle[j].st) != block_file_get(block)->mtime_nsec
				|| handle[j].st.st_ino != block_file_get(block)->inode
			) {
				/* report that the block and the file are not synched */
				block_is_unsynched = 1;
				file_is_unsynched = 1;
			}

			/* note that we intentionally don't abort if the file has different attributes */
			/* from the last sync, as we are expected to return errors if running */
			/* in an unsynched array. This is just like the check command. */

			read_size = handle_read(&handle[j], block, buffer[j], state->block_size, stderr);
			if (read_size == -1) {
				fprintf(stdlog, "error:%u:%s:%s: Read error at position %u\n", i, handle[j].disk->name, handle[j].file->sub, block_file_pos(block));
				++error;
				error_on_this_block = 1;
				continue;
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
					fprintf(stdlog, "error:%u:%s:%s: Data error at position %u\n", i, handle[j].disk->name, handle[j].file->sub, block_file_pos(block));

					/* it's a silent error only if we are dealing with synched files */
					if (file_is_unsynched) {
						++error;
						error_on_this_block = 1;
					} else {
						fprintf(stderr, "Data error in file '%s' at position '%u'\n", handle[j].path, block_file_pos(block));
						fprintf(stderr, "WARNING! Unexpected data error in a data disk! The block is now marked as bad!\n");
						fprintf(stderr, "Try with 'snapraid -e fix' to recover!\n");

						++silent_error;
						silent_error_on_this_block = 1;
					}
					continue;
				}
			}
		}

		/* if we have read all the data required and it's correct, proceed with the parity check */
		if (!error_on_this_block && !silent_error_on_this_block) {
			unsigned char* buffer_recov[LEV_MAX];

			/* buffers for parity read and not computed */
			for(l=0;l<state->level;++l)
				buffer_recov[l] = buffer[diskmax + state->level + l];
			for(;l<LEV_MAX;++l)
				buffer_recov[l] = 0;

			/* read the parity */
			for(l=0;l<state->level;++l) {
				ret = parity_read(parity[l], i, buffer_recov[l], state->block_size, stdlog);
				if (ret == -1) {
					buffer_recov[l] = 0;
					fprintf(stdlog, "parity_error:%u:%s: Read error\n", i, lev_config_name(l));
					++error;
					error_on_this_block = 1;
				}
			}

			/* compute the parity */
			raid_gen(diskmax, state->level, state->block_size, buffer);

			/* compare the parity */
			for(l=0;l<state->level;++l) {
				if (buffer_recov[l] && memcmp(buffer[diskmax + l], buffer_recov[l], state->block_size) != 0) {
					fprintf(stdlog, "parity_error:%u:%s: Data error\n", i, lev_config_name(l));

					/* it's a silent error only if we are dealing with synched blocks */
					if (block_is_unsynched) {
						++error;
						error_on_this_block = 1;
					} else {
						fprintf(stderr, "Data error in parity '%s' at position '%u'\n", lev_config_name(l), i);
						fprintf(stderr, "WARNING! Unexpected data error in a parity disk! The block is now marked as bad!\n");
						fprintf(stderr, "Try with 'snapraid -e fix' to recover!\n");

						++silent_error;
						silent_error_on_this_block = 1;
					}
				}
			}
		}

		if (silent_error_on_this_block) {
			/* set the error status keeping the existing time and hash */
			info_set(&state->infoarr, i, info_set_bad(info));
		} else if (error_on_this_block) {
			/* do nothing, as this is a generic error */
			/* likely caused by a not synched array */
		} else {
			/* if rehash is needed */
			if (rehash) {
				/* store all the new hash already computed */
				for(j=0;j<diskmax;++j) {
					if (rehandle[j].block)
						memcpy(rehandle[j].block->hash, rehandle[j].hash, HASH_SIZE);
				}
			}

			/* update the time info of the block */
			/* and clear any other flag */
			info_set(&state->infoarr, i, info_make(now, 0, 0));
		}

		/* mark the state as needing write */
		state->need_write = 1;

		/* count the number of processed block */
		++countpos;

		/* progress */
		if (state_progress(state, i, countpos, countmax, countsize)) {
			break;
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
		printf("%8u read errors\n", error);
		printf("%8u data errors\n", silent_error);
		printf("WARNING! There are errors!\n");
	} else {
		/* print the result only if processed something */
		if (countpos != 0)
			printf("Everything OK\n");
	}

	fprintf(stdlog, "summary:error_read:%u\n", error);
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

	if (state->opt.expect_recoverable) {
		if (error + silent_error == 0)
			return -1;
	} else {
		if (error + silent_error != 0)
			return -1;
	}
	return 0;
}

int state_scrub(struct snapraid_state* state, int percentage, int olderthan)
{
	block_off_t blockmax;
	block_off_t countlimit;
	block_off_t i;
	time_t timelimit;
	time_t recentlimit;
	unsigned count;
	int ret;
	struct snapraid_parity parity[LEV_MAX];
	struct snapraid_parity* parity_ptr[LEV_MAX];
	snapraid_info* infomap;
	unsigned error;
	time_t now;
	unsigned l;

	/* get the present time */
	now = time(0);

	printf("Initializing...\n");

	blockmax = parity_size(state);

	if (state->opt.force_scrub_even) {
		/* no limit */
		countlimit = blockmax;
		recentlimit = now;
	} else if (state->opt.force_scrub) {
		/* scrub the specified amount of blocks */
		countlimit = state->opt.force_scrub;
		recentlimit = now;
	} else if (percentage != -1 || olderthan != -1) {
		if (percentage != -1)
			countlimit = (blockmax * percentage + 99) / 100;
		else
			countlimit = blockmax;
		if (olderthan != -1)
			recentlimit = now - olderthan * 24 * 3600;
		else
			recentlimit = now;
	} else {
		/* by default scrub 1/12 of the array */
		countlimit = blockmax / 12;

		/* by default use a 10 day time limit */
		recentlimit = now - 10 * 24 * 3600;
	}

	/* identify the time limit */
	/* we sort all the block times, and we identify the time limit for which we reach the quota */
	/* this allow to process first the oldest blocks */
	infomap = malloc_nofail(blockmax * sizeof(snapraid_info));

	/* copy the info in the temp vector */
	count = 0;
	for(i=0;i<blockmax;++i) {
		snapraid_info info = info_get(&state->infoarr, i);

		/* skip unused blocks */
		if (info == 0)
			continue;

		infomap[count++] = info;
	}

	if (!count) {
		fprintf(stderr, "The array appears to be empty.\n");
		exit(EXIT_FAILURE);
	}

	/* sort it */
	qsort(infomap, count, sizeof(snapraid_info), info_time_compare);

	/* don't check more block than the available ones */
	if (countlimit > count)
		countlimit = count;

	if (countlimit > 0) {
		/* get the time limit */
		timelimit = info_get_time(infomap[countlimit - 1]);

		/* don't scrub too recent blocks */
		if (timelimit > recentlimit) {
			timelimit = recentlimit;
		}
	} else {
		/* if we select a 0 percentage, disable also the time limit */
		timelimit = 0;
	}

	/* free the temp vector */
	free(infomap);

	/* open the file for reading */
	for(l=0;l<state->level;++l) {
		parity_ptr[l] = &parity[l];
		ret = parity_open(parity_ptr[l], state->parity_path[l], state->opt.skip_sequential);
		if (ret == -1) {
			fprintf(stderr, "WARNING! Without an accessible %s file, it isn't possible to scrub.\n", lev_name(l));
			exit(EXIT_FAILURE);
		}
	}

	printf("Scrubbing...\n");

	error = 0;

	ret = state_scrub_process(state, parity_ptr, 0, blockmax, timelimit, countlimit, now);
	if (ret == -1) {
		++error;
		/* continue, as we are already exiting */
	}

	for(l=0;l<state->level;++l) {
		ret = parity_close(parity_ptr[l]);
		if (ret == -1) {
			fprintf(stderr, "DANGER! Unexpected close error in %s disk.\n", lev_name(l));
			++error;
			/* continue, as we are already exiting */
		}
	}

	/* abort if required */
	if (error != 0)
		return -1;
	return 0;
}

