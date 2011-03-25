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

/****************************************************************************/
/* sync */

static int state_sync_process(struct snapraid_state* state, int parity_f, block_off_t blockstart, block_off_t blockmax)
{
	struct snapraid_handle* handle;
	unsigned diskmax = tommy_array_size(&state->diskarr);
	block_off_t i;
	unsigned j;
	unsigned char* block_buffer;
	unsigned char* xor_buffer;
	data_off_t count_size;
	data_off_t count_block;
	time_t start;
	time_t last;
	int ret;
	unsigned unrecoverable_error;

	block_buffer = malloc_nofail(state->block_size);
	xor_buffer = malloc_nofail(state->block_size);

	handle = malloc_nofail(diskmax * sizeof(struct snapraid_handle));
	for(i=0;i<diskmax;++i) {
		handle[i].disk = tommy_array_get(&state->diskarr, i);
		handle[i].file = 0;
		handle[i].f = -1;
	}
	unrecoverable_error = 0;

	count_size = 0;
	count_block = 0;
	start = time(0);
	last = start;

	for(i=blockstart;i<blockmax;++i) {
		int one_invalid;
		int ret;

		/* for each disk */
		one_invalid = 0;
		for(j=0;j<diskmax;++j) {
			struct snapraid_block* block = disk_block_get(handle[j].disk, i);
			if (block && !bit_has(block->flag, BLOCK_HAS_HASH | BLOCK_HAS_PARITY)) {
				one_invalid = 1;
				break;
			}
		}

		/* if no invalid block skip */
		if (!one_invalid)
			continue;

		/* start with 0 */
		memset(xor_buffer, 0, state->block_size);

		/* for each disk, process the block */
		for(j=0;j<diskmax;++j) {
			int read_size;
			unsigned char hash[MD5_SIZE];
			struct snapraid_block* block;

			block = disk_block_get(handle[j].disk, i);
			if (!block)
				continue;

			ret = handle_close_if_different(&handle[j], block->file);
			if (ret == -1) {
				/* This one is really an unexpected error, because we are only reading */
				/* and closing a descriptor should never fail */
				fprintf(stderr, "DANGER! Unexpected close error in a data disk, it isn't possible to sync.\n");
				fprintf(stderr, "Stopping at block %u\n", i);
				++unrecoverable_error;
				goto bail;
			}

			ret = handle_open(&handle[j], block->file);
			if (ret == -1) {
				if (errno == ENOENT) {
					fprintf(stderr, "Missing file '%s'.\n", handle[j].path);
					fprintf(stderr, "WARNING! You cannot modify data disk during a sync. Rerun the sync command when finished.\n");
					fprintf(stderr, "Stopping at block %u\n", i);
				} else if (errno == EACCES) {
					fprintf(stderr, "No access at file '%s'.\n", handle[j].path);
					fprintf(stderr, "WARNING! Please fix the access permission in the data disk.\n");
					fprintf(stderr, "Stopping at block %u\n", i);
				} else {
					fprintf(stderr, "DANGER! Unexpected open error in a data disk, it isn't possible to sync.\n");
					fprintf(stderr, "Stopping to allow recovery. Try with 'snapraid check'\n");
				}
				++unrecoverable_error;
				goto bail;
			}

			/* check if the file is changed */
			if (handle[j].st.st_size != block->file->size
				|| handle[j].st.st_mtime != block->file->mtime
				|| handle[j].st.st_ino != block->file->inode
			) {
				fprintf(stderr, "Unexpected change at file '%s'.\n", handle[j].path);
				fprintf(stderr, "WARNING! You cannot modify data disk during a sync. Rerun the sync command when finished.\n");
				fprintf(stderr, "Stopping at block %u\n", i);
				++unrecoverable_error;
				goto bail;
			}

			read_size = handle_read(&handle[j], block, block_buffer, state->block_size);
			if (read_size == -1) {
				fprintf(stderr, "DANGER! Unexpected read error in a data disk, it isn't possible to sync.\n");
				fprintf(stderr, "Stopping to allow recovery. Try with 'snapraid check'\n");
				++unrecoverable_error;
				goto bail;
			}

			/* now compute the hash */
			memmd5(hash, block_buffer, read_size);

			if (bit_has(block->flag, BLOCK_HAS_HASH)) {
				/* compare the hash */
				if (memcmp(hash, block->hash, MD5_SIZE) != 0) {
					fprintf(stderr, "%u: Data error for file %s at position %u\n", i, block->file->sub, block_file_pos(block));
					fprintf(stderr, "DANGER! Unexpected data error in a data disk, it isn't possible to sync.\n");
					fprintf(stderr, "Stopping to allow recovery. Try with 'snapraid -s %u check'\n", i);
					++unrecoverable_error;
					goto bail;
				}
			} else {
				/* copy the hash, but doesn't mark the block as hashed */
				/* this allow on error to do not save the failed computation */
				memcpy(block->hash, hash, MD5_SIZE);
			}

			/* compute the parity */
			memxor(xor_buffer, block_buffer, read_size);

			count_size += read_size;
		}

		/* write the parity */
		ret = parity_write(state->parity, parity_f, i, xor_buffer, state->block_size);
		if (ret == -1) {
			fprintf(stderr, "DANGER! Write error in the parity disk, it isn't possible to sync.\n");
			fprintf(stderr, "Stopping at block %u\n", i);
			++unrecoverable_error;
			goto bail;
		}

		/* for each disk, mark the blocks as processed */
		for(j=0;j<diskmax;++j) {
			struct snapraid_block* block;

			block = disk_block_get(handle[j].disk, i);
			if (!block)
				continue;

			block->flag = bit_set(block->flag, BLOCK_HAS_HASH | BLOCK_HAS_PARITY);
		}

		/* mark the state as needing write */
		state->need_write = 1;

		/* count the number of processed block */
		++count_block;

		/* progress */
		if (state_progress(&start, &last, i, blockmax, count_block, count_size)) {
			printf("Stopping for interruption at block %u\n", i);
			break;
		}
	}

	printf("%u%% completed, %u MiB processed\n", i * 100 / blockmax, (unsigned)(count_size / (1024*1024)));

bail:
	for(j=0;j<diskmax;++j) {
		ret = handle_close(&handle[j]);
		if (ret == -1) {
			fprintf(stderr, "DANGER! Unexpected close error in a data disk.\n");
			++unrecoverable_error;
			/* continue, as we are already exiting */
		}
	}

	free(handle);
	free(block_buffer);
	free(xor_buffer);

	if (unrecoverable_error != 0)
		return -1;
	return 0;
}

int state_sync(struct snapraid_state* state, block_off_t blockstart, block_off_t blockcount)
{
	char path[PATH_MAX];
	block_off_t blockmax;
	data_off_t size;
	int ret;
	int f;
	unsigned unrecoverable_error;

	printf("Syncing...\n");

	blockmax = parity_resize(state);

	size = blockmax * (data_off_t)state->block_size;

	if (blockstart > blockmax) {
		fprintf(stderr, "Error in the starting block %u. It's bigger than the parity size %u.\n", blockstart, blockmax);
		exit(EXIT_FAILURE);
	}
	
	/* adjust the number of block to process */
	if (blockcount != 0 && blockstart + blockcount < blockmax) {
		blockmax = blockstart + blockcount;
	}

	pathcpy(path, sizeof(path), state->parity);
	f = parity_create(path, size);
	if (f == -1) {
		fprintf(stderr, "WARNING! Without an accessible parity file, it isn't possible to sync.\n");
		exit(EXIT_FAILURE);
	}

	unrecoverable_error = 0;

	/* skip degenerated cases of empty parity, or skipping all */
	if (blockstart < blockmax) {
		ret = state_sync_process(state, f, blockstart, blockmax);
		if (ret == -1) {
			++unrecoverable_error;
			/* continue, as we are already exiting */
		}
	}

	ret = parity_sync(path, f);
	if (ret == -1) {
		fprintf(stderr, "DANGER! Unexpected sync error in parity disk.\n");
		++unrecoverable_error;
		/* continue, as we are already exiting */
	}

	ret = parity_close(path, f);
	if (ret == -1) {
		fprintf(stderr, "DANGER! Unexpected close error in parity disk.\n");
		++unrecoverable_error;
		/* continue, as we are already exiting */
	}

	/* abort if required */
	if (unrecoverable_error != 0)
		return -1;
	return 0;
}

