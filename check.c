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
/* check */

static int state_check_process(int ret_on_error, struct snapraid_state* state, int fix, int parity_f, block_off_t blockstart, block_off_t blockmax)
{
	struct snapraid_handle* handle;
	unsigned diskmax = tommy_array_size(&state->diskarr);
	block_off_t i;
	unsigned j;
	unsigned char* block_buffer;
	unsigned char* xor_buffer;
	int ret;
	data_off_t count_size;
	data_off_t count_block;
	time_t start;
	time_t last;
	unsigned error;
	unsigned unrecoverable_error;
	unsigned recovered_error;

	block_buffer = malloc_nofail(state->block_size);
	xor_buffer = malloc_nofail(state->block_size);

	handle = malloc_nofail(diskmax * sizeof(struct snapraid_handle));
	for(i=0;i<diskmax;++i) {
		handle[i].disk = tommy_array_get(&state->diskarr, i);
		handle[i].file = 0;
		handle[i].f = -1;
	}
	error = 0;
	unrecoverable_error = 0;
	recovered_error = 0;

	count_size = 0;
	count_block = 0;
	start = time(0);
	last = start;

	for(i=blockstart;i<blockmax;++i) {
		int hashed;
		int failed;
		struct snapraid_block* failed_block;
		struct snapraid_handle* failed_handle;

		/* for each disk, search for a hashed block */
		hashed = 0;
		for(j=0;j<diskmax;++j) {
			struct snapraid_block* block = disk_block_get(handle[j].disk, i);
			if (block && block->is_hashed) {
				hashed = 1;
				break;
			}
		}

		/* if there is at least an hashed block we have to check */
		if (!hashed)
			continue;

		/* start with 0 */
		memset(xor_buffer, 0, state->block_size);

		/* for each disk, process the block */
		failed = 0;
		failed_block = 0;
		failed_handle = 0;
		for(j=0;j<diskmax;++j) {
			int read_size;
			struct md5_t md5;
			unsigned char hash[HASH_MAX];
			struct snapraid_block* block;

			block = disk_block_get(handle[j].disk, i);
			if (!block)
				continue;

			/* we can check and fix only if the block is hashed */
			if (!block->is_hashed)
				continue;

			if (fix) {
				/* if fixing, create the file and open for writing */
				handle_create(&handle[j],  block->file);
			} else {
				/* if checking, open the file for reading */
				ret = handle_open(-1, &handle[j], block->file);
				if (ret == -1) {
					/* save the failed block for the parity check */
					++failed;
					failed_block = block;
					failed_handle = &handle[j];

					fprintf(stderr, "%u: Open error for file %s at position %u\n", i, block->file->sub, block_file_pos(block));
					++error;
					continue;
				}
			}

			read_size = handle_read(-1, &handle[j], block, block_buffer, state->block_size);
			if (read_size == -1) {
				/* save the failed block for the parity check */
				++failed;
				failed_block = block;
				failed_handle = &handle[j];

				fprintf(stderr, "%u: Read error for file %s at position %u\n", i, block->file->sub, block_file_pos(block));
				++error;
				continue;
			}

			/* now compute the hash */
			md5_init(&md5);
			md5_update(&md5, block_buffer, read_size);
			md5_final(&md5, hash);

			/* compare the hash */
			if (memcmp(hash, block->hash, HASH_MAX) != 0) {
				/* save the failed block for the parity check */
				++failed;
				failed_block = block;
				failed_handle = &handle[j];

				fprintf(stderr, "%u: Data error for file %s at position %u\n", i, block->file->sub, block_file_pos(block));
				++error;
				continue;
			}

			/* compute the parity */
			memxor(xor_buffer, block_buffer, read_size);

			count_size += read_size;
		}

		if (failed == 0) {
			int ret;
			int fixable;

			fixable = 0;

			/* read the parity */
			ret = parity_read(-1, state->parity, parity_f, i, block_buffer, state->block_size);
			if (ret == -1) {
				fprintf(stderr, "%u: Parity read error\n", i);
				++error;

				fixable = 1;
			} else {
				/* compare it */
				if (memcmp(xor_buffer, block_buffer, state->block_size) != 0) {
					fprintf(stderr, "%u: Parity data error\n", i);
					++error;

					fixable = 1;
				}
			}

			if (fix && fixable) {
				/* fix the parity */
				/* if all the file hashes are matching, we can recompute the parity */
				parity_write(state->parity, parity_f, i, xor_buffer, state->block_size);

				fprintf(stderr, "%u: Fixed\n", i);
				++recovered_error;
			}
		} else if (failed == 1) {
			struct md5_t md5;
			unsigned char hash[HASH_MAX];
			unsigned failed_size;
			int ret;
			int fixable;

			fixable = 0;

			/* read the parity */
			ret = parity_read(-1, state->parity, parity_f, i, block_buffer, state->block_size);
			if (ret == -1) {
				fprintf(stderr, "%u: Parity read error\n", i);
				fprintf(stderr, "%u: UNRECOVERABLE errors for this block\n", i);
				++unrecoverable_error;
			} else {
				/* compute the failed block */
				memxor(block_buffer, xor_buffer, state->block_size);

				failed_size = block_file_size(failed_block, state->block_size);

				/* now compute the hash */
				md5_init(&md5);
				md5_update(&md5, block_buffer, failed_size);
				md5_final(&md5, hash);

				/* compare the hash */
				if (memcmp(hash, failed_block->hash, HASH_MAX) != 0) {
					fprintf(stderr, "%u: Parity data error\n", i);
					fprintf(stderr, "%u: UNRECOVERABLE erros for this block\n", i);
					++unrecoverable_error;
				} else {
					fixable = 1;
				}
			}

			if (fix && fixable) {
				/* fix the file */
				/* if only one file is wrong, we already have recomputed it */
				handle_write(failed_handle, failed_block, block_buffer, state->block_size);

				fprintf(stderr, "%u: Fixed\n", i);
				++recovered_error;
			}
		} else {
			fprintf(stderr, "%u: UNRECOVERABLE errors for this block\n", i);
			++unrecoverable_error;
		}

		/* count the number of processed block */
		++count_block;

		/* progress */
		if (state_progress(&start, &last, i, blockmax, count_block, count_size))
			break;
	}

	printf("%u%% completed, %u MB processed\n", i * 100 / blockmax, (unsigned)(count_size / (1024*1024)));

	if (error || unrecoverable_error) {
		if (error)
			printf("%u recoverable errors\n", error);
		else
			printf("No recoverable errors\n");
		if (fix)
			printf("%u fixed errors\n", recovered_error);
		if (unrecoverable_error)
			printf("%u UNRECOVERABLE errors\n", unrecoverable_error);
		else
			printf("No unrecoverable errors\n");
	} else {
		printf("No error\n");
	}

	for(j=0;j<diskmax;++j) {
		handle_close(&handle[j]);
	}

	free(handle);
	free(block_buffer);
	free(xor_buffer);

	return unrecoverable_error != 0 ? ret_on_error : 0;
}

void state_check(struct snapraid_state* state, int fix, block_off_t blockstart)
{
	char path[PATH_MAX];
	block_off_t blockmax;
	data_off_t size;
	int ret;
	int f;

	if (fix)
		printf("Checking and fixing...\n");
	else
		printf("Checking...\n");

	blockmax = parity_resize(state);
	size = blockmax * (data_off_t)state->block_size;

	if (blockstart >= blockmax) {
		fprintf(stderr, "The specified starting block %u is bigger than the parity size %u.\n", blockstart, blockmax);
		exit(EXIT_FAILURE);
	}

	pathcpy(path, sizeof(path), state->parity);
	if (fix) {
		/* if fixing, create the file and open for writing */
		/* it never fails */
		f = parity_create(path, size);
	} else {
		/* if checking, open the file for reading */
		/* it may fail if the file doesn't exist */
		f = parity_open(-1, path);
	}

	ret = state_check_process(-1, state, fix, f, blockstart, blockmax);
	if (ret == -1) {
		/* no extra message */
		exit(EXIT_FAILURE);
	}

	if (f != -1) {
		parity_close(path, f);
	}
}
