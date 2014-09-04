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
 * Buffer for storing data contex.
 */
struct snapraid_data_contex {
	int skipped; /**< If this block is skipped. */
	struct snapraid_block* block; /**< Pointer at the assigned block. 0 if none. */

	/**
	 * If the block is rehashed.
	 * if ::has_rehash is 1, ::rehash contains the new hash.
	 */
	int has_rehash;
	unsigned char rehash[HASH_SIZE];

	/**
	 * If the file on this disk is synced
	 * If not synched, silent errors are assumed as expected error.
	 */
	int file_is_unsynced;

#if HAVE_LIBAIO
	/**
	 * Result of libaio operation.
	 */
	struct iocb aio_res;
#endif
	/**
	 * Result of syncronous operation.
	 */
	int sio_res;
};

/**
 * Buffer for storing parity contex.
 */
struct snapraid_parity_context {
#if HAVE_LIBAIO
	/**
	 * Result of libaio operation.
	 */
	struct iocb aio_res;
#endif
	/**
	 * Result of syncronous operation.
	 */
	int sio_res;
};

static int state_scrub_process(struct snapraid_state* state, struct snapraid_parity** parity, block_off_t blockstart, block_off_t blockmax, time_t timelimit, block_off_t lastlimit, time_t now)
{
	struct snapraid_handle* handle;
	void* data_handle_alloc;
	struct snapraid_data_contex* data_handle;
	void* parity_handle_alloc;
	struct snapraid_parity_context* parity_handle;
	unsigned diskmax;
	block_off_t i;
	unsigned j;
	void* buffer_alloc;
	void** buffer;
	unsigned buffermax;
	data_off_t countsize;
	block_off_t countpos;
	block_off_t countmax;
	block_off_t countlast;
	block_off_t autosavedone;
	block_off_t autosavelimit;
	block_off_t autosavemissing;
#ifdef HAVE_LIBAIO
	io_context_t aio_data; /**< AIO context for data. */
	io_context_t aio_parity; /**< AIO context for parity. */
#endif
	int ret;
	unsigned error;
	unsigned silent_error;
	unsigned l;

	/* maps the disks to handles */
	handle = handle_map(state, &diskmax);

	/* data contex */
	data_handle = malloc_nofail_align(diskmax * sizeof(struct snapraid_data_contex), &data_handle_alloc);

	/* parity contex */
	parity_handle = malloc_nofail_align(state->level * sizeof(struct snapraid_parity_context), &parity_handle_alloc);

	/* we need disk + 2 for each parity level buffers */
	buffermax = diskmax + state->level * 2;

	buffer = malloc_nofail_vector_align(diskmax, buffermax, state->block_size, &buffer_alloc);
	if (!state->opt.skip_self)
		mtest_vector(buffermax, state->block_size, buffer);

	error = 0;
	silent_error = 0;

#ifdef HAVE_LIBAIO
	/* setup the libaio context */
	ret = io_setup(diskmax, &aio_data);
	if (ret < 0) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Libaio io_setup() failed: %s.\n", strerror(-ret));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}
	ret = io_setup(state->level, &aio_parity);
	if (ret < 0) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Libaio io_setup() failed: %s.\n", strerror(-ret));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}
#endif

	/* first count the number of blocks to process */
	countmax = 0;
	countlast = 0;
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
			/* otherwise, check if we reached the last limit count */
			if (blocktime == timelimit) {
				/* if we reached the count limit */
				if (countlast >= lastlimit) {
					/* skip it */
					continue;
				}

				++countlast;
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
	countlast = 0;
	state_progress_begin(state, blockstart, blockmax, countmax);
	for(i=blockstart;i<blockmax;++i) {
		time_t blocktime;
		snapraid_info info;
		int data_error_on_this_block;
		int parity_error_on_this_block;
		int silent_error_on_this_block;
		int block_is_unsynced;
		int rehash;
		int sio_i;
		int sio_data_submit = 0;
		int sio_data_index[diskmax];
		int sio_parity_submit = 0;
		int sio_parity_index[state->level];
		unsigned char* buffer_recov[LEV_MAX];
#ifdef HAVE_LIBAIO
		int aio_i;
		int aio_data_submit = 0;
		int aio_parity_submit = 0;
		struct io_event aio_events[1];
#endif

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
			/* otherwise, check if we reached the last limit count */
			if (blocktime == timelimit) {
				/* if we reached the count limit */
				if (countlast >= lastlimit) {
					/* skip it */
					continue;
				}

				++countlast;
			}
		}

		/* one more block processed for autosave */
		++autosavedone;
		--autosavemissing;

		/* by default process the block, and skip it if something goes wrong */
		data_error_on_this_block = 0;
		parity_error_on_this_block = 0;
		silent_error_on_this_block = 0;

		/* if all the blocks at this address are synced */
		/* if not, parity is not even checked */
		block_is_unsynced = 0;

		/* if we have to use the old hash */
		rehash = info_get_rehash(info);

		/* buffers to store the parity read from disk */
		for(l=0;l<state->level;++l)
			buffer_recov[l] = buffer[diskmax + state->level + l];
		for(;l<LEV_MAX;++l)
			buffer_recov[l] = 0;

		/* schedule data block */
		for(j=0;j<diskmax;++j) {
			struct snapraid_block* block;
			struct snapraid_data_contex* data_ctx = &data_handle[j];
#if HAVE_LIBAIO
			unsigned read_size;
#endif

			/* setup data handle, in case of "continue" */
			data_ctx->skipped = 1;
			data_ctx->block = 0;
			data_ctx->has_rehash = 0;
			data_ctx->file_is_unsynced = 0;

			/* if the disk position is not used */
			if (!handle[j].disk) {
				/* use an empty block */
				memset(buffer[j], 0, state->block_size);
				continue;
			}

			/* get the block */
			block = disk_block_get(handle[j].disk, i);
			data_ctx->block = block;

			/* if the block is not used */
			if (!block_has_file(block)) {
				/* use an empty block */
				memset(buffer[j], 0, state->block_size);
				continue;
			}

			/* if the block is unsynced, errors are expected */
			if (block_has_invalid_parity(block)) {
				/* report that the block and the file are not synced */
				block_is_unsynced = 1;
				data_ctx->file_is_unsynced = 1;

				/* follow */
			}

			/* if the file is different than the current one, close it */
			if (handle[j].file != 0 && handle[j].file != block_file_get(block)) {
				/* keep a pointer at the file we are going to close for error reporting */
				struct snapraid_file* file = handle[j].file;
				ret = handle_close(&handle[j]);
				if (ret == -1) {
					/* LCOV_EXCL_START */
					/* This one is really an unexpected error, because we are only reading */
					/* and closing a descriptor should never fail */
					fprintf(stdlog, "error:%u:%s:%s: Close error. %s\n", i, handle[j].disk->name, file->sub, strerror(errno));
					fprintf(stderr, "DANGER! Unexpected close error in a data disk, it isn't possible to scrub.\n");
					printf("Stopping at block %u\n", i);
					++error;
					goto bail;
					/* LCOV_EXCL_STOP */
				}
			}

			ret = handle_open(&handle[j], block_file_get(block), state->file_mode, stderr);
			if (ret == -1) {
				/* file we have tried to open for error reporting */
				struct snapraid_file* file = block_file_get(block);
				fprintf(stdlog, "error:%u:%s:%s: Open error. %s\n", i, handle[j].disk->name, file->sub, strerror(errno));
				++error;
				data_error_on_this_block = 1;
				continue;
			}

			/* check if the file is changed */
			if (handle[j].st.st_size != block_file_get(block)->size
				|| handle[j].st.st_mtime != block_file_get(block)->mtime_sec
				|| STAT_NSEC(&handle[j].st) != block_file_get(block)->mtime_nsec
				|| handle[j].st.st_ino != block_file_get(block)->inode
			) {
				/* report that the block and the file are not synced */
				block_is_unsynced = 1;
				data_ctx->file_is_unsynced = 1;

				/* follow */
			}

			/* block is processed */
			data_ctx->skipped = 0;

			/* note that we intentionally don't abort if the file has different attributes */
			/* from the last sync, as we are expected to return errors if running */
			/* in an unsynced array. This is just like the check command. */
#if HAVE_LIBAIO
			read_size = block_file_size(block, state->block_size);

			/* schedule asyncronous read if it's full block read */
			if (read_size == state->block_size) {
				struct iocb* aio_cbs[1];
				data_off_t offset = block_file_pos(block) * (data_off_t)state->block_size;

				/* setup the read request */
				io_prep_pread(&data_ctx->aio_res, handle[j].f, buffer[j], state->block_size, offset);

				/* set the request context */
				data_ctx->aio_res.data = data_ctx;

				/* submit the request */
				aio_cbs[0] = &data_ctx->aio_res;
				ret = io_submit(aio_data, 1, aio_cbs);
				if (ret < 0) {
					/* LCOV_EXCL_START */
					fprintf(stderr, "Libaio io_submit() failed: %s.\n", strerror(-ret));
					exit(EXIT_FAILURE);
					/* LCOV_EXCL_STOP */
				}

				++aio_data_submit;
			} else {
				/* submit the syncronous operation */
				sio_data_index[sio_data_submit] = j;

				++sio_data_submit;
			}
#else
			/* submit the syncronous operation */
			sio_data_index[sio_data_submit] = j;

			++sio_data_submit;
#endif
		}

		/* schedule parity blocks */
		for(l=0;l<state->level;++l) {
#if HAVE_LIBAIO
			struct snapraid_parity_context* parity_ctx = &parity_handle[l];
			data_off_t offset = i * (data_off_t)state->block_size;
			struct iocb* aio_cbs[1];

			/* setup the read request */
			io_prep_pread(&parity_ctx->aio_res, parity[l]->f, buffer_recov[l], state->block_size, offset);

			/* set the request context */
			parity_ctx->aio_res.data = parity_ctx;

			/* submit the request */
			aio_cbs[0] = &parity_ctx->aio_res;
			ret = io_submit(aio_parity, 1, aio_cbs);
			if (ret < 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Libaio io_submit() failed: %s.\n", strerror(-ret));
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			++aio_parity_submit;
#else
			/* submit the syncronous operation */
			sio_parity_index[sio_parity_submit] = l;

			++sio_parity_submit;
#endif
		}

		/* process pending data syncronous operations */
		for(sio_i=0;sio_i<sio_data_submit;++sio_i) {
			struct snapraid_data_contex* data_ctx;
			struct snapraid_block* block;

			j = sio_data_index[sio_i];
			data_ctx = &data_handle[j];
			block = data_ctx->block;

#if HAVE_LIBAIO
			/* disable O_DIRECT if enabled */
			ret = fcntl(handle[j].f, F_GETFL);
			if (ret < 0) {
				data_ctx->sio_res = ret;
				continue;
			}
			if ((ret & O_DIRECT) != 0) {
				ret &= ~O_DIRECT;
				ret = fcntl(handle[j].f, F_SETFL, (int)ret);
				if (ret < 0) {
					data_ctx->sio_res = ret;
					continue;
				}
			}
#endif
			data_ctx->sio_res = handle_read(&handle[j], block, buffer[j], state->block_size, stderr);
		}

		/* process data blocks */
		sio_i = 0;
#if HAVE_LIBAIO
		aio_i = 0;
#endif
		while (1) {
			unsigned char hash[HASH_SIZE];
			struct snapraid_data_contex* data_ctx;
			struct snapraid_block* block;
			int read_size;

			/* first process syncronous operations */
			if (sio_i < sio_data_submit) {
				j = sio_data_index[sio_i];
				data_ctx = &data_handle[j];
				block = data_ctx->block;
				read_size = data_ctx->sio_res;

				/* next event */
				++sio_i;
			} else {
#if HAVE_LIBAIO
				/* if no more event, it's done */
				if (aio_i >= aio_data_submit)
					break;

				/* wait for an asyncronous event */
				ret = io_getevents(aio_data, 1, 1, aio_events, 0);
				if (ret < 0) {
					/* LCOV_EXCL_START */
					fprintf(stderr, "Libaio io_getevents() failed: %s.\n", strerror(-ret));
					exit(EXIT_FAILURE);
					/* LCOV_EXCL_STOP */
				}

				/* get the disk processed */
				data_ctx = aio_events[0].data;
				read_size = aio_events[0].res;
				block = data_ctx->block;
				j = data_ctx - data_handle;

				/* next event */
				++aio_i;
#else
				/* no more event, it's done */
				break;
#endif
			}

			/* if the block is skipped, ignore it */
			if (data_ctx->skipped)
				continue;

			if (read_size < 0) {
				fprintf(stdlog, "error:%u:%s:%s: Read error at position %u\n", i, handle[j].disk->name, handle[j].file->sub, block_file_pos(block));
				++error;
				data_error_on_this_block = 1;
				continue;
			}

			countsize += read_size;

			/* now compute the hash */
			if (rehash) {
				memhash(state->prevhash, state->prevhashseed, hash, buffer[j], read_size);

				/* mark the block as rehashed */
				data_ctx->has_rehash = 1;

				/* compute the new hash, and store it */
				memhash(state->hash, state->hashseed, data_ctx->rehash, buffer[j], read_size);
			} else {
				memhash(state->hash, state->hashseed, hash, buffer[j], read_size);
			}

			if (block_has_updated_hash(block)) {
				/* compare the hash */
				if (memcmp(hash, block->hash, HASH_SIZE) != 0) {
					fprintf(stdlog, "error:%u:%s:%s: Data error at position %u\n", i, handle[j].disk->name, handle[j].file->sub, block_file_pos(block));

					/* it's a silent error only if we are dealing with synced files */
					if (data_ctx->file_is_unsynced) {
						++error;
						data_error_on_this_block = 1;
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

		/* process pending parity syncronous operations */
		for(sio_i=0;sio_i<sio_parity_submit;++sio_i) {
			struct snapraid_parity_context* parity_ctx;

			l = sio_parity_index[sio_i];
			parity_ctx = &parity_handle[l];

			parity_ctx->sio_res = parity_read(parity[l], i, buffer_recov[l], state->block_size, stdlog);
		}

		/* process parity blocks */
		sio_i = 0;
#if HAVE_LIBAIO
		aio_i = 0;
#endif
		while (1) {
			struct snapraid_parity_context* parity_ctx;
			int read_size;

			if (sio_i < sio_parity_submit) {
				l = sio_parity_index[sio_i];
				parity_ctx = &parity_handle[l];
				read_size = parity_ctx->sio_res;

				/* next event */
				++sio_i;
			} else {
#if HAVE_LIBAIO
				/* if no more event, it's done */
				if (aio_i >= aio_parity_submit)
					break;

				/* wait for an asyncronous event */
				ret = io_getevents(aio_parity, 1, 1, aio_events, 0);
				if (ret < 0) {
					/* LCOV_EXCL_START */
					fprintf(stderr, "Libaio io_getevents() failed: %s.\n", strerror(-ret));
					exit(EXIT_FAILURE);
					/* LCOV_EXCL_STOP */
				}

				/* get the disk processed */
				parity_ctx = aio_events[0].data;
				read_size = aio_events[0].res;
				l = parity_ctx - parity_handle;

				/* next event */
				++aio_i;
#else
				/* no more event, it's done */
				break;
#endif
			}

			if (read_size == -1) {
				buffer_recov[l] = 0;
				fprintf(stdlog, "parity_error:%u:%s: Read error\n", i, lev_config_name(l));
				++error;
				parity_error_on_this_block = 1;

				/* follow */
			}
		}

		/* if we have read all the data required and it's correct, proceed with the parity check */
		if (!data_error_on_this_block && !silent_error_on_this_block) {
			/* compute the parity */
			raid_gen(diskmax, state->level, state->block_size, buffer);

			/* compare the parity */
			for(l=0;l<state->level;++l) {
				if (buffer_recov[l] && memcmp(buffer[diskmax + l], buffer_recov[l], state->block_size) != 0) {
					fprintf(stdlog, "parity_error:%u:%s: Data error\n", i, lev_config_name(l));

					/* it's a silent error only if we are dealing with synced blocks */
					if (block_is_unsynced) {
						++error;
						parity_error_on_this_block = 1;
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
		} else if (data_error_on_this_block || parity_error_on_this_block) {
			/* do nothing, as this is a generic error */
			/* likely caused by a not synced array */
		} else {
			/* if rehash is needed */
			if (rehash) {
				/* store all the new hash already computed */
				for(j=0;j<diskmax;++j) {
					struct snapraid_data_contex* data_ctx = &data_handle[j];
					if (data_ctx->has_rehash)
						memcpy(data_ctx->block->hash, data_ctx->rehash, HASH_SIZE);
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

#ifdef HAVE_LIBAIO
	ret = io_destroy(aio_data);
	if (ret < 0) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Libaio io_destroy() failed: %s.\n", strerror(-ret));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}
	ret = io_destroy(aio_parity);
	if (ret < 0) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Libaio io_destroy() failed: %s.\n", strerror(-ret));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}
#endif

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
			/* LCOV_EXCL_START */
			fprintf(stderr, "DANGER! Unexpected close error in a data disk.\n");
			++error;
			/* continue, as we are already exiting */
			/* LCOV_EXCL_STOP */
		}
	}

	free(handle);
	free(buffer_alloc);
	free(buffer);
	free(data_handle_alloc);
	free(parity_handle_alloc);

	if (state->opt.expect_recoverable) {
		if (error + silent_error == 0)
			return -1;
	} else {
		if (error + silent_error != 0)
			return -1;
	}
	return 0;
}

/**
 * Returns a * b / c approximated to the upper value.
 */
static uint32_t md(uint32_t a, uint32_t b, uint32_t c)
{
	uint64_t v = a;

	v *= b;
	v += c - 1;
	v /= c;

	return v;
}

int state_scrub(struct snapraid_state* state, int percentage, int olderthan)
{
	block_off_t blockmax;
	block_off_t countlimit;
	block_off_t lastlimit;
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

#ifdef HAVE_LIBAIO
	/* O_DIRECT is required by libaio */
	state->file_mode |= MODE_DIRECT;
#endif

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
	} else {
		/* by default scrub 1/12 of the array */
		countlimit = md(blockmax, 1, 12);

		if (percentage != -1)
			countlimit = md(blockmax, percentage, 100);

		/* by default use a 10 day time limit */
		recentlimit = now - 10 * 24 * 3600;

		if (olderthan != -1)
			recentlimit = now - olderthan * 24 * 3600;
	}

	/* identify the time limit */
	/* we sort all the block times, and we identify the time limit for which we reach the quota */
	/* this allow to process first the oldest blocks */
	infomap = malloc_nofail(blockmax * sizeof(snapraid_info));

	/* copy the info in the temp vector */
	count = 0;
	fprintf(stdlog, "block_count:%u\n", blockmax);
	for(i=0;i<blockmax;++i) {
		snapraid_info info = info_get(&state->infoarr, i);

		/* skip unused blocks */
		if (info == 0)
			continue;

		infomap[count++] = info;
	}

	if (!count) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "The array appears to be empty.\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* sort it */
	qsort(infomap, count, sizeof(snapraid_info), info_time_compare);

	/* output the info map */
	i = 0;
	fprintf(stdlog, "info_count:%u\n", count);
	while (i < count) {
		unsigned j = i + 1;
		while (j < count && info_get_time(infomap[i]) == info_get_time(infomap[j]))
			++j;
		fprintf(stdlog, "info_time:%"PRIu64":%u\n", (uint64_t)info_get_time(infomap[i]), j - i);
		i = j;
	}

	/* no more than the full count */
	if (countlimit > count)
		countlimit = count;

	/* decrease until we reach the specific recentlimit */
	while (countlimit > 0 && info_get_time(infomap[countlimit - 1]) > recentlimit)
		--countlimit;

	/* if there is something to scrub */
	if (countlimit > 0) {
		/* get the most recent time we want to scrub */
		timelimit = info_get_time(infomap[countlimit - 1]);

		/* count how many entries for this exact time we have to scrub */
		/* if the blocks have all the same time, we end with countlimit == lastlimit */
		lastlimit = 1;
		while (countlimit > lastlimit && info_get_time(infomap[countlimit - lastlimit - 1]) == timelimit)
			++lastlimit;
	} else {
		/* if nothing to scrub, disable also other limits */
		timelimit = 0;
		lastlimit = 0;
	}

	fprintf(stdlog, "count_limit:%u\n", countlimit);
	fprintf(stdlog, "time_limit:%"PRIu64"\n", (uint64_t)timelimit);
	fprintf(stdlog, "last_limit:%u\n", lastlimit);

	/* free the temp vector */
	free(infomap);

	/* open the file for reading */
	for(l=0;l<state->level;++l) {
		parity_ptr[l] = &parity[l];
		ret = parity_open(parity_ptr[l], state->parity_path[l], state->file_mode);
		if (ret == -1) {
			/* LCOV_EXCL_START */
			fprintf(stderr, "WARNING! Without an accessible %s file, it isn't possible to scrub.\n", lev_name(l));
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}

	printf("Scrubbing...\n");

	error = 0;

	ret = state_scrub_process(state, parity_ptr, 0, blockmax, timelimit, lastlimit, now);
	if (ret == -1) {
		++error;
		/* continue, as we are already exiting */
	}

	for(l=0;l<state->level;++l) {
		ret = parity_close(parity_ptr[l]);
		if (ret == -1) {
			/* LCOV_EXCL_START */
			fprintf(stderr, "DANGER! Unexpected close error in %s disk.\n", lev_name(l));
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

