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

static int state_check_process(struct snapraid_state* state, int fix, int parity_f, block_off_t blockstart, block_off_t blockmax)
{
	struct snapraid_handle* handle;
	unsigned diskmax = tommy_array_size(&state->diskarr);
	block_off_t i;
	unsigned j;
	unsigned char* block_buffer;
	unsigned char* xor_buffer;
	int ret;
	data_off_t countsize;
	block_off_t countpos;
	block_off_t countmax;
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

	countmax = blockmax - blockstart;
	countsize = 0;
	countpos = 0;
	start = time(0);
	last = start;
	for(i=blockstart;i<blockmax;++i) {
		int failed;
		struct snapraid_block* failed_block;
		struct snapraid_handle* failed_handle;
		int one_hash;
		int all_parity;

		/* for each disk */
		one_hash = 0;
		for(j=0;j<diskmax;++j) {
			struct snapraid_block* block = disk_block_get(handle[j].disk, i);
			if (block && bit_has(block->flag, BLOCK_HAS_HASH)) {
				one_hash = 1;
				break;
			}
		}

		/* if no hashed block skip */
		if (!one_hash)
			continue;

		/* start with 0 */
		memset(xor_buffer, 0, state->block_size);

		all_parity = 1; /* if all hashed block have parity computed */
		failed = 0; /* number of failed block */
		failed_block = 0; /* last failed block */
		failed_handle = 0;

		/* for each disk, process the block */
		for(j=0;j<diskmax;++j) {
			int read_size;
			unsigned char hash[MD5_SIZE];
			struct snapraid_block* block;

			block = disk_block_get(handle[j].disk, i);
			if (!block)
				continue;

			/* we try to check and fix only if the block is hashed */
			/* it could be that a file was added, and not yet synched */
			/* so we should not include not hashed block in the parity computation */
			if (!bit_has(block->flag, BLOCK_HAS_HASH))
				continue;

			/* keep track if at least one hashed block has no parity */
			if (!bit_has(block->flag, BLOCK_HAS_PARITY))
				all_parity = 0;

			ret = handle_close_if_different(&handle[j], block->file);
			if (ret == -1) {
				fprintf(stderr, "WARNING! Without a working data disk, it isn't possible to fix errors on it.\n");
				fprintf(stderr, "Stopping at block %u\n", i);
				++unrecoverable_error;
				goto bail;
			}

			if (fix) {
				/* if fixing, create the file and open for writing */
				ret = handle_create(&handle[j],  block->file);
				if (ret == -1) {
					fprintf(stderr, "WARNING! Without a working data disk, it isn't possible to fix errors on it.\n");
					fprintf(stderr, "Stopping at block %u\n", i);
					++unrecoverable_error;
					goto bail;
				}
			} else {
				/* if checking, open the file for reading */
				ret = handle_open(&handle[j], block->file);
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

			read_size = handle_read(&handle[j], block, block_buffer, state->block_size);
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
			memmd5(hash, block_buffer, read_size);

			/* compare the hash */
			if (memcmp(hash, block->hash, MD5_SIZE) != 0) {
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

			countsize += read_size;
		}

		if (parity_f == -1 || !all_parity) {
			/* all the cases with no parity file */
			if (failed != 0) {
				fprintf(stderr, "%u: UNRECOVERABLE errors for this block\n", i);
				++unrecoverable_error;
			}
		} else if (failed == 0) {
			int ret;
			int fixable;

			fixable = 0;

			/* read the parity */
			ret = parity_read(state->parity, parity_f, i, block_buffer, state->block_size);
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
				ret = parity_write(state->parity, parity_f, i, xor_buffer, state->block_size);
				if (ret == -1) {
					fprintf(stderr, "WARNING! Without a working parity disk, it isn't possible to fix errors on it.\n");
					fprintf(stderr, "Stopping at block %u\n", i);
					++unrecoverable_error;
					goto bail;
				}

				fprintf(stderr, "%u: Fixed\n", i);
				++recovered_error;
			}
		} else if (failed == 1) {
			unsigned char hash[MD5_SIZE];
			unsigned failed_size;
			int ret;
			int fixable;

			fixable = 0;

			/* read the parity */
			ret = parity_read(state->parity, parity_f, i, block_buffer, state->block_size);
			if (ret == -1) {
				fprintf(stderr, "%u: Parity read error\n", i);
				fprintf(stderr, "%u: UNRECOVERABLE errors for this block\n", i);
				++unrecoverable_error;
			} else {
				/* compute the failed block */
				memxor(block_buffer, xor_buffer, state->block_size);

				failed_size = block_file_size(failed_block, state->block_size);

				/* now compute the hash */
				memmd5(hash, block_buffer, failed_size);

				/* compare the hash */
				if (memcmp(hash, failed_block->hash, MD5_SIZE) != 0) {
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
				ret = handle_write(failed_handle, failed_block, block_buffer, state->block_size);
				if (ret == -1) {
					fprintf(stderr, "WARNING! Without a working data disk, it isn't possible to fix errors on it.\n");
					fprintf(stderr, "Stopping at block %u\n", i);
					++unrecoverable_error;
					goto bail;
				}

				fprintf(stderr, "%u: Fixed\n", i);
				++recovered_error;
			}
		} else {
			fprintf(stderr, "%u: UNRECOVERABLE errors for this block\n", i);
			++unrecoverable_error;
		}

		/* count the number of processed block */
		++countpos;

		/* progress */
		if (state_progress(&start, &last, countpos, countmax, countsize)) {
			printf("Stopping for interruption at block %u\n", i);
			break;
		}
	}

	if (countmax)
		printf("%u%% completed, %u MiB processed\n", countpos * 100 / countmax, (unsigned)(countsize / (1024*1024)));
	else
		printf("Nothing to do\n");

bail:
	for(j=0;j<diskmax;++j) {
		ret = handle_close(&handle[j]);
		if (ret == -1) {
			if (fix) {
				fprintf(stderr, "WARNING! A 'snapraid check' command is highly suggested.\n");
				++unrecoverable_error;
				/* continue, as we are already exiting */
			}
		}
	}

	if (error || recovered_error || unrecoverable_error) {
		if (error)
			printf("%u read/data errors\n", error);
		else
			printf("No read/data errors\n");
		if (fix) {
			if (recovered_error)
				printf("%u recovered errors\n", recovered_error);
			else
				printf("No recovered errors\n");
		}
		if (unrecoverable_error)
			printf("%u UNRECOVERABLE errors\n", unrecoverable_error);
		else
			printf("No unrecoverable errors\n");
	} else {
		printf("No error\n");
	}

	free(handle);
	free(block_buffer);
	free(xor_buffer);

	if (unrecoverable_error != 0)
		return -1;
	return 0;
}

void state_check(struct snapraid_state* state, int fix, block_off_t blockstart, block_off_t blockcount)
{
	char path[PATH_MAX];
	block_off_t blockmax;
	data_off_t size;
	int ret;
	int f;
	unsigned unrecoverable_error;

	if (fix)
		printf("Checking and fixing...\n");
	else
		printf("Checking...\n");

	blockmax = parity_resize(state);
	size = blockmax * (data_off_t)state->block_size;

	if (blockstart > blockmax) {
		fprintf(stderr, "Error in the specified starting block %u. It's bigger than the parity size %u.\n", blockstart, blockmax);
		exit(EXIT_FAILURE);
	}

	/* adjust the number of block to process */
	if (blockcount != 0 && blockstart + blockcount < blockmax) {
		blockmax = blockstart + blockcount;
	}

	pathcpy(path, sizeof(path), state->parity);
	if (fix) {
		/* if fixing, create the file and open for writing */
		/* if it fails, we cannot continue */
		f = parity_create(path, size);
		if (f == -1) {
			fprintf(stderr, "WARNING! Without an accessible parity file, it isn't possible to fix any error.\n");
			exit(EXIT_FAILURE);
		}
	} else {
		/* if checking, open the file for reading */
		/* it may fail if the file doesn't exist, in this case we continue to check the files */
		f = parity_open(path);
		if (f == -1) {
			printf("No accessible parity file, all the errors are going to be UNRECOVERABLE.\n");
			/* continue anyway */
		}
	}

	unrecoverable_error = 0;

	/* skip degenerated cases of empty parity, or skipping all */
	if (blockstart < blockmax) {
		ret = state_check_process(state, fix, f, blockstart, blockmax);
		if (ret == -1) {
			++unrecoverable_error;
			/* continue, as we are already exiting */
		}
	}

	/* try to close only if opened */
	if (f != -1) {
		ret = parity_close(path, f);
		if (ret == -1) {
			fprintf(stderr, "WARNING! A 'snapraid check' command is highly suggested.\n");
			++unrecoverable_error;
			/* continue, as we are already exiting */
		}
	}

	/* abort if required */
	if (unrecoverable_error != 0)
		exit(EXIT_FAILURE);
}

