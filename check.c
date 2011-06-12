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
#include "raid.h"

/****************************************************************************/
/* check */

struct failed_struct {
	unsigned index;
	struct snapraid_block* block;
	struct snapraid_handle* handle;
};

/**
 * Checks if a block hash matches the specified buffer.
 */
static int blockcmp(struct snapraid_state* state, struct snapraid_block* block, unsigned char* buffer, unsigned char* buffer_zero)
{
	unsigned char hash[HASH_SIZE];
	unsigned size;

	size = block_file_size(block, state->block_size);

	/* now compute the hash of the valid part */
	memhash(state->hash, hash, buffer, size);

	/* compare the hash */
	if (memcmp(hash, block->hash, HASH_SIZE) != 0) {
		return -1;
	}

	/* compare to the end of the block */
	if (size < state->block_size) {
		if (memcmp(buffer + size, buffer_zero + size, state->block_size - size) != 0) {
			return -1;
		}
	}

	return 0;
}

/**
 * Repair errors.
 * Returns <0 if failure for missing stratey, >0 if data is wrong and we cannot rebuild correctly, 0 on success.
 * If success, the parity and qarity are computed in the buffer variable.
 */
static int repair(struct snapraid_state* state, unsigned i, unsigned diskmax, struct failed_struct* failed, unsigned failed_count, unsigned char** buffer, unsigned char* buffer_parity, unsigned char* buffer_qarity, unsigned char* buffer_zero)
{
	unsigned j;
	int error = 0;

	/* no fix required */
	if (failed_count == 0) {
		/* compute parity and qarity to check it */
		raid_gen(state->level, buffer, diskmax, state->block_size);
		return 0;
	}

	if (failed_count == 1 && buffer_parity != 0) {
		/* copy the redundancy to use */
		memcpy(buffer[diskmax], buffer_parity, state->block_size);

		/* recover */
		raid5_recov_data(buffer, diskmax, state->block_size, failed[0].index);

		for(j=0;j<1;++j) {
			if (blockcmp(state, failed[j].block, buffer[failed[j].index], buffer_zero) != 0) 
				break; 
		}

		if (j==1) {
			/* compute parity and qarity to check it */
			/* we recompute everything because we may have used only a small part of the redundancy */
			raid_gen(state->level, buffer, diskmax, state->block_size);
			return 0;
		}

		fprintf(stderr, "%u: Parity data error\n", i);
		++error;
	}

	if (failed_count == 1 && buffer_qarity != 0) {
		/* copy the redundancy to use */
		memcpy(buffer[diskmax+1], buffer_qarity, state->block_size);

		raid6_recov_datap(buffer, diskmax, state->block_size, failed[0].index, buffer_zero);

		for(j=0;j<1;++j) {
			if (blockcmp(state, failed[j].block, buffer[failed[j].index], buffer_zero) != 0) 
				break; 
		}

		if (j==1) {
			/* compute parity and qarity to check it */
			/* we recompute everything because we may have used only a small part of the redundancy */
			raid_gen(state->level, buffer, diskmax, state->block_size);
			return 0;
		}

		fprintf(stderr, "%u: Q-Parity data error\n", i);
		++error;
	}

	if (failed_count == 2 && buffer_parity != 0 && buffer_qarity != 0) {
		/* copy the redundancy to use */
		memcpy(buffer[diskmax], buffer_parity, state->block_size);
		memcpy(buffer[diskmax+1], buffer_qarity, state->block_size);

		/* recover */
		raid6_recov_2data(buffer, diskmax, state->block_size, failed[0].index, failed[1].index, buffer_zero);

		for(j=0;j<2;++j) {
			if (blockcmp(state, failed[j].block, buffer[failed[j].index], buffer_zero) != 0) 
				break; 
		}

		if (j==2) {
			/* compute parity and qarity to check it */
			/* we recompute everything because we may have used only a small part of the redundancy */
			raid_gen(state->level, buffer, diskmax, state->block_size);
			return 0;
		}

		fprintf(stderr, "%u: Parity/Q-Parity data error\n", i);
		++error;
	}

	/* no more stragety to fix */
	if (error)
		return error;
	else
		return -1;
}

static int state_check_process(struct snapraid_state* state, int fix, int parity_f, int qarity_f, block_off_t blockstart, block_off_t blockmax)
{
	struct snapraid_handle* handle;
	unsigned diskmax = tommy_array_size(&state->diskarr);
	block_off_t i;
	unsigned j;
	void* buffer_alloc;
	unsigned char* buffer_aligned;
	unsigned char** buffer;
	unsigned buffermax;
	int ret;
	data_off_t countsize;
	block_off_t countpos;
	block_off_t countmax;
	time_t start;
	time_t last;
	unsigned error;
	unsigned unrecoverable_error;
	unsigned recovered_error;
	struct failed_struct* failed;

	/* we need disk + 2 for each parity level buffers + 1 zero buffer */
	buffermax = diskmax + state->level * 2 + 1;

	buffer_aligned = malloc_nofail_align(buffermax * state->block_size, &buffer_alloc);
	buffer = malloc_nofail(buffermax * sizeof(void*));
	for(i=0;i<buffermax;++i) {
		buffer[i] = buffer_aligned + i * state->block_size;
	}
	memset(buffer[buffermax-1], 0, state->block_size);

	failed = malloc_nofail(diskmax * sizeof(struct failed_struct));

	handle = malloc_nofail(diskmax * sizeof(struct snapraid_handle));
	for(i=0;i<diskmax;++i) {
		handle[i].disk = tommy_array_get(&state->diskarr, i);
		handle[i].file = 0;
		handle[i].f = -1;
	}
	error = 0;
	unrecoverable_error = 0;
	recovered_error = 0;

	/* first count the number of blocks to process */
	countmax = 0;
	for(i=blockstart;i<blockmax;++i) {
		int one_tocheck;

		/* for each disk */
		one_tocheck = 0;
		for(j=0;j<diskmax;++j) {
			struct snapraid_block* block = disk_block_get(handle[j].disk, i);
			if (block
				&& block_flag_has(block, BLOCK_HAS_HASH) /* only if the block is hashed */
				&& !file_flag_has(block_file_get(block), FILE_IS_EXCLUDED) /* only if the file is not filtered out */
			) {
				one_tocheck = 1;
				break;
			}
		}

		/* if no block to check skip */
		if (!one_tocheck)
			continue;

		++countmax;
	}

	countsize = 0;
	countpos = 0;
	start = time(0);
	last = start;
	for(i=blockstart;i<blockmax;++i) {
		unsigned failed_count;
		int one_tocheck;
		int all_parity;
		unsigned char* buffer_parity;
		unsigned char* buffer_qarity;
		unsigned char* buffer_zero;

		/* for each disk */
		one_tocheck = 0;
		for(j=0;j<diskmax;++j) {
			struct snapraid_block* block = disk_block_get(handle[j].disk, i);
			if (block
				&& block_flag_has(block, BLOCK_HAS_HASH) /* only if the block is hashed */
				&& !file_flag_has(block_file_get(block), FILE_IS_EXCLUDED) /* only if the file is not filtered out */
			) {
				one_tocheck = 1;
				break;
			}
		}

		/* if no block to check skip */
		if (!one_tocheck)
			continue;

		all_parity = 1; /* if all hashed block have parity computed */
		failed_count = 0; /* number of failed blocks */

		/* for each disk, process the block */
		for(j=0;j<diskmax;++j) {
			int read_size;
			unsigned char hash[HASH_SIZE];
			struct snapraid_block* block;

			block = disk_block_get(handle[j].disk, i);
			if (!block) {
				/* use an empty block */
				memset(buffer[j], 0, state->block_size);
				continue;
			}

			/* we try to check and fix only if the block is hashed */
			/* if no hash is present also no parity is present, */
			/* but we expect to had it excluded from the parity computation, */
			/* so it's correct to assume it filled with 0 and DO NOT reset all_parity */
			if (!block_flag_has(block, BLOCK_HAS_HASH)) {
				/* use an empty block */
				memset(buffer[j], 0, state->block_size);
				continue;
			}

			/* keep track if at least one hashed block has no parity */
			/* if at least one block has no parity, doesn't make sense to check/fix the parity */
			/* as errors are the normal condition */
			if (!block_flag_has(block, BLOCK_HAS_PARITY))
				all_parity = 0;

			ret = handle_close_if_different(&handle[j], block_file_get(block));
			if (ret == -1) {
				fprintf(stderr, "DANGER! Unexpected close error in a data disk, it isn't possible to sync.\n");
				printf("Stopping at block %u\n", i);
				++unrecoverable_error;
				goto bail;
			}

			if (fix) {
				/* if fixing, create the file, open for writing and resize if required */
				ret = handle_create(&handle[j], block_file_get(block));
				if (ret == -1) {
					if (errno == EACCES) {
						fprintf(stderr, "WARNING! Please give write permission to the file.\n");
					} else {
						fprintf(stderr, "DANGER! Without a working data disk, it isn't possible to fix errors on it.\n");
					}
					printf("Stopping at block %u\n", i);
					++unrecoverable_error;
					goto bail;
				}

				/* check if the file was larger and now truncated */
				if (ret == 1) {
					fprintf(stderr, "File '%s' is larger than expected.\n", handle[j].path);
					fprintf(stderr, "%u: Size error for file %s\n", i, block_file_get(block)->sub);
					++error;

					/* this is already a recovered error */
					fprintf(stderr, "%u: Fixed size for file %s\n", i, block_file_get(block)->sub);
					++recovered_error;
				}
			} else {
				/* if checking, open the file for reading */
				ret = handle_open(&handle[j], block_file_get(block));
				if (ret == -1) {
					/* save the failed block for the check/fix */
					failed[failed_count].index = j;
					failed[failed_count].block = block;
					failed[failed_count].handle = &handle[j];
					++failed_count;

					fprintf(stderr, "%u: Open error for file %s at position %u\n", i, block_file_get(block)->sub, block_file_pos(block));
					++error;
					continue;
				}

				/* check if it's a larger file, but not if already notified */
				if (!file_flag_has(block_file_get(block), FILE_IS_LARGER)
					&& handle[j].st.st_size > block_file_get(block)->size
				) {
					fprintf(stderr, "File '%s' is larger than expected.\n", handle[j].path);
					fprintf(stderr, "%u: Size error for file %s\n", i, block_file_get(block)->sub);
					++error;

					/* if fragmented, it may be reopened, so store the notification */
					/* to prevent to signal and count the error more than one time */
					file_flag_set(block_file_get(block), FILE_IS_LARGER);
				}
			}

			read_size = handle_read(&handle[j], block, buffer[j], state->block_size);
			if (read_size == -1) {
				/* save the failed block for the check/fix */
				failed[failed_count].index = j;
				failed[failed_count].block = block;
				failed[failed_count].handle = &handle[j];
				++failed_count;

				fprintf(stderr, "%u: Read error for file %s at position %u\n", i, block_file_get(block)->sub, block_file_pos(block));
				++error;
				continue;
			}

			/* now compute the hash */
			memhash(state->hash, hash, buffer[j], read_size);

			/* compare the hash */
			if (memcmp(hash, block->hash, HASH_SIZE) != 0) {
				/* save the failed block for the check/fix */
				failed[failed_count].index = j;
				failed[failed_count].block = block;
				failed[failed_count].handle = &handle[j];
				++failed_count;

				fprintf(stderr, "%u: Data error for file %s at position %u\n", i, block_file_get(block)->sub, block_file_pos(block));
				++error;
				continue;
			}

			countsize += read_size;
		}

		/* buffers for parity read and not computed */
		if (state->level == 1) {
			buffer_parity = buffer[diskmax + 1];
			buffer_qarity = 0;
		} else {
			buffer_parity = buffer[diskmax + 2];
			buffer_qarity = buffer[diskmax + 3];
		}
		buffer_zero = buffer[buffermax-1];

		/* read the parity */
		if (parity_f != -1) {
			ret = parity_read(state->parity, parity_f, i, buffer_parity, state->block_size);
			if (ret == -1) {
				buffer_parity = 0; /* no parity to use */

				fprintf(stderr, "%u: Parity read error\n", i);
				++error;
			}
		} else {
			buffer_parity = 0;
		}

		/* read the qarity */
		if (state->level >= 2) {
			if (qarity_f != -1) {
				ret = parity_read(state->qarity, qarity_f, i, buffer_qarity, state->block_size);
				if (ret == -1) {
					buffer_qarity = 0; /* no qarity to use */

					fprintf(stderr, "%u: Q-Parity read error\n", i);
					++error;
				}
			} else {
				buffer_qarity = 0;
			}
		}

		ret = repair(state, i, diskmax, failed, failed_count, buffer, buffer_parity, buffer_qarity, buffer_zero);
		if (ret != 0) {
			/* increment the number of errors */
			if (ret > 0)
				error += ret;

			++unrecoverable_error;

			/* print a list of all the errors in files */
			for(j=0;j<failed_count;++j) {
				fprintf(stderr, "%u: Unrecoverable error for file %s at position %u\n", i, block_file_get(failed[j].block)->sub, block_file_pos(failed[j].block));
			}
		} else {
			/* check parity and q-parity only if all the blocks have it computed */
			/* if you check/fix after a partial sync, it's OK to have parity errors on the blocks with invalid parity */
			if (all_parity) {
				/* check the parity */
				if (buffer_parity != 0 && memcmp(buffer_parity, buffer[diskmax], state->block_size) != 0) {
					buffer_parity = 0;

					fprintf(stderr, "%u: Parity data error\n", i);
					++error;
				}

				/* check the qarity */
				if (state->level >= 2) {
					if (buffer_qarity != 0 && memcmp(buffer_qarity, buffer[diskmax + 1], state->block_size) != 0) {
						buffer_qarity = 0;

						fprintf(stderr, "%u: Q-Parity data error\n", i);
						++error;
					}
				}
			}

			if (fix) {
				/* update the fixed files */
				for(j=0;j<failed_count;++j) {
					/* do not fix if the file filtered out */
					if (file_flag_has(block_file_get(failed[j].block), FILE_IS_EXCLUDED))
						continue;

					ret = handle_write(failed[j].handle, failed[j].block, buffer[failed[j].index], state->block_size);
					if (ret == -1) {
						if (errno == EACCES) {
							fprintf(stderr, "WARNING! Please give write permission to the file.\n");
						} else {
							/* we do not use DANGER because it could be ENOSPC which is not always correctly reported */
							fprintf(stderr, "WARNING! Without a working data disk, it isn't possible to fix errors on it.\n");
						}
						printf("Stopping at block %u\n", i);
						++unrecoverable_error;
						goto bail;
					}

					fprintf(stderr, "%u: Fixed data error for file %s at position %u\n", i, block_file_get(failed[j].block)->sub, block_file_pos(failed[j].block));
					++recovered_error;
				}

				/* update parity and q-parity only if all the blocks have it computed */
				/* if you check/fix after a partial sync, you do not want to fix parity */
				/* for blocks that are going to have it computed in the sync completion */
				if (all_parity) {
					/* update the parity */
					if (buffer_parity == 0 && parity_f != -1) {
						ret = parity_write(state->parity, parity_f, i, buffer[diskmax], state->block_size);
						if (ret == -1) {
							/* we do not use DANGER because it could be ENOSPC which is not always correctly reported */
							fprintf(stderr, "WARNING! Without a working Parity disk, it isn't possible to fix errors on it.\n");
							printf("Stopping at block %u\n", i);
							++unrecoverable_error;
							goto bail;
						}

						fprintf(stderr, "%u: Fixed Parity error\n", i);
						++recovered_error;
					}

					/* update the qarity */
					if (state->level >= 2) {
						if (buffer_qarity == 0 && qarity_f != -1) {
							ret = parity_write(state->qarity, qarity_f, i, buffer[diskmax + 1], state->block_size);
							if (ret == -1) {
								/* we do not use DANGER because it could be ENOSPC which is not always correctly reported */
								fprintf(stderr, "WARNING! Without a working Q-Parity disk, it isn't possible to fix errors on it.\n");
								printf("Stopping at block %u\n", i);
								++unrecoverable_error;
								goto bail;
							}

							fprintf(stderr, "%u: Fixed Q-Parity error\n", i);
							++recovered_error;
						}
					}
				}
			}
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
			fprintf(stderr, "DANGER! Unexpected close error in a data disk.\n");
			++unrecoverable_error;
			/* continue, as we are already exiting */
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

	free(failed);
	free(handle);
	free(buffer_alloc);
	free(buffer);

	/* fails if some error are present after the run */
	if (fix) {
		if (state->expect_unrecoverable) {
			if (unrecoverable_error == 0)
				return -1;
		} else {
			if (unrecoverable_error != 0)
				return -1;
		}
	} else {
		if (state->expect_unrecoverable) {
			if (unrecoverable_error == 0)
				return -1;
		} else if (state->expect_recoverable) {
			if (error == 0)
				return -1;
		} else {
			if (error != 0 || unrecoverable_error != 0)
				return -1;
		}
	}

	return 0;
}

void state_check(struct snapraid_state* state, int fix, block_off_t blockstart, block_off_t blockcount)
{
	char parity_path[PATH_MAX];
	char qarity_path[PATH_MAX];
	block_off_t blockmax;
	data_off_t size;
	int ret;
	int parity_f;
	int qarity_f;
	unsigned error;

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

	pathcpy(parity_path, sizeof(parity_path), state->parity);
	pathcpy(qarity_path, sizeof(qarity_path), state->qarity);

	if (fix) {
		/* if fixing, create the file and open for writing */
		/* if it fails, we cannot continue */
		parity_f = parity_create(parity_path, size);
		if (parity_f == -1) {
			fprintf(stderr, "WARNING! Without an accessible Parity file, it isn't possible to fix any error.\n");
			exit(EXIT_FAILURE);
		}

		if (state->level >= 2) {
			qarity_f = parity_create(qarity_path, size);
			if (qarity_f == -1) {
				fprintf(stderr, "WARNING! Without an accessible Q-Parity file, it isn't possible to fix any error.\n");
				exit(EXIT_FAILURE);
			}
		} else {
			qarity_f = -1;
		}
	} else {
		/* if checking, open the file for reading */
		/* it may fail if the file doesn't exist, in this case we continue to check the files */
		parity_f = parity_open(parity_path);
		if (parity_f == -1) {
			printf("No accessible Parity file, only files will be checked.\n");
			/* continue anyway */
		}

		if (state->level >= 2) {
			qarity_f = parity_open(qarity_path);
			if (qarity_f == -1) {
				printf("No accessible Q-Parity file, only files will be checked.\n");
				/* continue anyway */
			}
		} else {
			qarity_f = -1;
		}
	}

	error = 0;

	/* skip degenerated cases of empty parity, or skipping all */
	if (blockstart < blockmax) {
		ret = state_check_process(state, fix, parity_f, qarity_f, blockstart, blockmax);
		if (ret == -1) {
			++error;
			/* continue, as we are already exiting */
		}
	}

	/* try to close only if opened */
	if (parity_f != -1) {
		ret = parity_close(parity_path, parity_f);
		if (ret == -1) {
			fprintf(stderr, "DANGER! Unexpected close error in Parity disk.\n");
			++error;
			/* continue, as we are already exiting */
		}
	}

	if (state->level >= 2) {
		if (qarity_f != -1) {
			ret = parity_close(qarity_path, qarity_f);
			if (ret == -1) {
				fprintf(stderr, "DANGER! Unexpected close error in Q-Parity disk.\n");
				++error;
				/* continue, as we are already exiting */
			}
		}
	}

	/* abort if error are present */
	if (error != 0)
		exit(EXIT_FAILURE);
}

