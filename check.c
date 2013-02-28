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
#include "import.h"
#include "state.h"
#include "parity.h"
#include "handle.h"
#include "raid.h"

/****************************************************************************/
/* check */

/**
 * A block that failed the hash check, or that was deleted.
 */
struct failed_struct {
	/**
	 * If the block needs to be recovered and rewritten to the disk.
	 */
	int is_bad;

	/**
	 * If that we have recovered may be not updated data,
	 * an old version, or just garbage.
	 *
	 * Essentially, it means that we are not sure what we have recovered
	 * is really correct. It's just our best guess.
	 *
	 * These "recovered" block are also written to the disk if the block is marked as ::is_bad.
	 * But these files are marked also as FILE_IS_DAMAGED, and then renamed to .unrecoverable.
	 *
	 * Note that this could happen only for NEW and CHG blocks.
	 */
	int is_outofdate;

	unsigned index; /**< Index of the failed block. */
	struct snapraid_block* block; /**< The failed block, or BLOCK_DELETED for a deleted block */
	struct snapraid_handle* handle; /**< The file containing the failed block, or 0 for a deleted block */
};

/**
 * Checks if a block hash matches the specified buffer.
 * Return ==0 if equal
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
 * Returns <0 if failure for missing strategy, >0 if data is wrong and we cannot rebuild correctly, 0 on success.
 * If success, the parity and qarity are computed in the buffer variable.
 */
static int repair_step(struct snapraid_state* state, unsigned pos, unsigned diskmax, struct failed_struct* failed, unsigned* failed_map, unsigned failed_count, unsigned char** buffer, unsigned char* buffer_parity, unsigned char* buffer_qarity, unsigned char* buffer_zero)
{
	unsigned j;
	int error = 0;
	int has_hash;
	int hash_checked;

	/* no fix required */
	if (failed_count == 0) {
		/* recompute only parity and qarity */
		raid_gen(state->level, buffer, diskmax, state->block_size);
		return 0;
	}

	/* check if there is at least a failed block that can be checked for correctness using the hash */
	/* if there isn't, we have to sacrifice a parity block to check that the result is correct */
	has_hash = 0;
	for(j=0;j<failed_count;++j) {
		if (block_has_hash(failed[failed_map[j]].block)) /* if the block can be checked */
			has_hash = 1;
	}

	/* RAID5 recovering from p */
	if (failed_count == 1 && buffer_parity != 0 && has_hash) {
		/* copy the redundancy to use */
		memcpy(buffer[diskmax], buffer_parity, state->block_size);

		/* recover data */
		raid5_recov_data(buffer, diskmax, state->block_size, failed[failed_map[0]].index);

		hash_checked = 0; /* keep track if we check at least one block */

		/* check if the recovered blocks are OK */
		for(j=0;j<1;++j) {
			if (block_has_hash(failed[failed_map[j]].block)) { /* if the block can be checked */
				hash_checked = 1;
				if (blockcmp(state, failed[failed_map[j]].block, buffer[failed[failed_map[j]].index], buffer_zero) != 0)
					break;
			}
		}

		/* if we checked something, and no block failed the check */
		if (hash_checked && j==1) {
			/* recompute all the redundancy information */
			raid_gen(state->level, buffer, diskmax, state->block_size);
			return 0;
		}

		fprintf(stderr, "error:%u:parity: Data error\n", pos);
		++error;
	}

	/* RAID6 recovering from q, ignoring p */
	if (failed_count == 1 && buffer_qarity != 0 && has_hash) {
		/* copy the redundancy to use */
		memcpy(buffer[diskmax+1], buffer_qarity, state->block_size);

		/* recover both data and p */
		raid6_recov_datap(buffer, diskmax, state->block_size, failed[failed_map[0]].index, buffer_zero);

		hash_checked = 0; /* keep track if we check at least one block */

		/* check if the recovered blocks are OK */
		for(j=0;j<1;++j) {
			if (block_has_hash(failed[failed_map[j]].block)) { /* if the block can be checked */
				hash_checked = 1;
				if (blockcmp(state, failed[failed_map[j]].block, buffer[failed[failed_map[j]].index], buffer_zero) != 0)
					break;
			}
		}

		/* if we checked something, and no block failed the check */
		if (hash_checked && j==1) {
			/* recompute all the redundancy information */
			raid_gen(state->level, buffer, diskmax, state->block_size);
			return 0;
		}

		fprintf(stderr, "error:%u:qarity: Data error\n", pos);
		++error;
	}

	/* RAID6 recovering from q, using p to check the result */
	if (failed_count == 1 && buffer_parity != 0 && buffer_qarity != 0 && !has_hash) {
		/* copy the redundancy to use */
		memcpy(buffer[diskmax+1], buffer_qarity, state->block_size);

		/* recover both data and p */
		raid6_recov_datap(buffer, diskmax, state->block_size, failed[failed_map[0]].index, buffer_zero);

		/* if the recovered parity block matches */
		if (memcmp(buffer[diskmax], buffer_parity, state->block_size) == 0) {
			/* recompute all the redundancy information */
			raid_gen(state->level, buffer, diskmax, state->block_size);
			return 0;
		}

		fprintf(stderr, "error:%u:parity/qarity: Data error\n", pos);
		++error;
	}

	/* RAID6 recovering from p+q */
	if (failed_count == 2 && buffer_parity != 0 && buffer_qarity != 0 && has_hash) {
		/* copy the redundancy to use */
		memcpy(buffer[diskmax], buffer_parity, state->block_size);
		memcpy(buffer[diskmax+1], buffer_qarity, state->block_size);

		/* recover 2 data */
		raid6_recov_2data(buffer, diskmax, state->block_size, failed[failed_map[0]].index, failed[failed_map[1]].index, buffer_zero);

		hash_checked = 0; /* keep track if we check at least one block */

		/* check if the recovered blocks are OK */
		for(j=0;j<2;++j) {
			if (block_has_hash(failed[failed_map[j]].block)) { /* if the block can be checked */
				hash_checked = 1;
				if (blockcmp(state, failed[failed_map[j]].block, buffer[failed[failed_map[j]].index], buffer_zero) != 0)
					break;
			}
		}

		/* if we checked something, and no block failed the check */
		if (hash_checked && j==2) {
			/* recompute all the redundancy information */
			raid_gen(state->level, buffer, diskmax, state->block_size);
			return 0;
		}

		fprintf(stderr, "error:%u:parity/qarity: Data error\n", pos);
		++error;
	}

	/* return the number of failed attempts, or -1 if no strategy */
	if (error)
		return error;
	else
		return -1;
}

static int repair(struct snapraid_state* state, unsigned pos, unsigned diskmax, struct failed_struct* failed, unsigned* failed_map, unsigned failed_count, unsigned char** buffer, unsigned char* buffer_parity, unsigned char* buffer_qarity, unsigned char* buffer_zero)
{
	int ret;
	int error;
	unsigned j;
	int n;
	int something_to_recover;

	error = 0;

	/* Here we have to try two different strategies to recover, because in case the 'sync' */
	/* process is aborted, we don't know if the parity data is really updated just like after 'sync', */
	/* or if it still represents the state before the 'sync'. */

	/* As first, we assume that the parity is already computed for the current state */
	/* and that we are going to recover the state after the last 'sync'. */
	/* This is the normal condition. */
	/* In this case, parity contains correct info for BLK, CHG (new version) and NEW blocks, */
	/* and not for DELETED ones. */
	/* We need to put in the recovering process only the bad blocks, because all the */
	/* others already contains the correct data read frin disk, and the parity is correctly computed for them. */
	/* We are interested to recover BLK, CHG and NEW blocks if they are marked as bad, */
	/* but we are not interested in DELETED ones. */

	n = 0;
	something_to_recover = 0; /* keep track if there is at least one block to fix */
	for(j=0;j<failed_count;++j) {
		if (failed[j].is_bad) {
			unsigned block_state = block_state_get(failed[j].block);

			assert(block_state != BLOCK_STATE_DELETED); /* we cannot have bad DELETED blocks */

			if (block_state == BLOCK_STATE_BLK) { /* if we have the hash for it */
				/* try to fetch the block using the hash */
				if (state_import_fetch(state, failed[j].block->hash, buffer[failed[j].index]) == 0) {
					/* we have corrected it! */
				} else {
					/* otherwise try to recover it */
					failed_map[n] = j;
					++n;
				
					/* we have something to try to recover */
					something_to_recover = 1;
				}
			} else {
				assert(block_state == BLOCK_STATE_CHG || block_state == BLOCK_STATE_NEW);
			
				/* otherwise it's CHG or NEW and we try to recover it */
				failed_map[n] = j;
				++n;
				
				/* we have something to try to recover */
				something_to_recover = 1;
			}
		}
	}

	/* if nothing to fix */
	if (!something_to_recover) {
		/* recompute only parity and qarity */
		raid_gen(state->level, buffer, diskmax, state->block_size);
		return 0;
	}

	ret = repair_step(state, pos, diskmax, failed, failed_map, n, buffer, buffer_parity, buffer_qarity, buffer_zero);
	if (ret == 0) {
		/* reprocess the blocks for NEW and CHG ones, for which we don't have a hash to check */
		/* if they were BAD we have to use some euristics to ensure that we have recovered  */
		/* the state after the sync. If unsure, we assume the worst case */

		for(j=0;j<failed_count;++j) {
			/* we take care only of BAD blocks we have to write back */
			if (failed[j].is_bad) {
				unsigned block_state = block_state_get(failed[j].block);

				if (block_state == BLOCK_STATE_NEW) {
					/* if the block is not filled with 0, we are sure to have */
					/* restored it to the state after the 'sync' */
					/* instead, if the block is filled with 0, it could be either that the */
					/* block after the sync is really filled by 0, or that */
					/* we restored the block before the 'sync'. */
					if (memcmp(buffer[failed[j].index], buffer_zero, state->block_size) == 0) {
						/* it may contain garbage */
						failed[j].is_outofdate = 1;
					}
				} else if (block_state == BLOCK_STATE_CHG) {
					/* if the hash is a bogus value we cannot check the result */
					/* this could happen if we have lost this information */
					/* after an aborted sync */
					if (memcmp(failed[j].block->hash, buffer_zero, HASH_SIZE) == 0) {
						/* it may contain garbage */
						failed[j].is_outofdate = 1;
					} else
					/* if the hash is different than the previous one, we are sure to have */
					/* restored it to the state after the 'sync' */
					/* instead, if the hash matches, it could be either that the */
					/* block after the sync has this hash, or that */
					/* we restored the block before the 'sync'. */
					if (blockcmp(state, failed[j].block, buffer[failed[j].index], buffer_zero) == 0) {
						/* it may contain garbage */
						failed[j].is_outofdate = 1;
					}
				}
			}
		}

		return 0;
	}
	if (ret > 0)
		error += ret;

	/* Now assume that the parity computation was not updated at the current state, */
	/* but still represent the state before the last 'sync' process. */
	/* This may happen only if a 'sync' is aborted by a crash or manually. */
	/* In this case, parity contains info for BLK, CHG (old version) and DELETED blocks, */
	/* but not for CHG (new version) and NEW ones. */
	/* We are interested to recover BLK ones marked as bad, */
	/* but we are not interested to recover NEW and CHG (new version) blocks, even if marked as bad, */
	/* because we don't have parity for them and it's just impossible, */
	/* and we are not interested to recover DELETED ones. */
	n = 0;
	something_to_recover = 0; /* keep track if there is at least one block to fix */
	for(j=0;j<failed_count;++j) {
		unsigned block_state = block_state_get(failed[j].block);

		if (block_state == BLOCK_STATE_DELETED
			|| block_state == BLOCK_STATE_CHG
		) {
			/* If the block is CHG or DELETED, we don't have the original content of block, */
			/* and we must try to recover it. */
			/* This apply to CHG blocks even if they are not marked bad, */
			/* because the parityis computed with old content, and not with the new one. */
			/* Note that this recovering is done just to make possible to recover any other BLK one, */
			/* we are not really interseted in DELETED and CHG (old version) ones. */

			/* try to fetch the old block using the old hash */
			if (state_import_fetch(state, failed[j].block->hash, buffer[failed[j].index]) == 0) {
				/* note that from now the buffer is definitively lost */
				/* we can do this only because it's the last retry of recovering */
			} else {
				/* otherwise try to recover it */
				failed_map[n] = j;
				++n;

				/* note that we don't set something_to_recover, because we are */
				/* not really interested to recover *only* old blocks. */
			}

			/* mark that we have restored or we will restore an old state */
			/* note that if the block is not marked as is_bad, */
			/* we are not going to write it in the disk */
			failed[j].is_outofdate = 1;
		} else if (block_state == BLOCK_STATE_NEW) {
			/* If the block is NEW, restore it to the original 0 as before the 'sync' */
			/* Also in this case, we do this to just allow recovering of other BLK ones */

			memset(buffer[failed[j].index], 0, state->block_size);
			/* note that from now the buffer is definitively lost */
			/* we can do this only because it's the last retry of recovering */

			/* mark that we have restored an old state */
			/* note that if the block is not marked as is_bad, */
			/* we are not going to write it in the disk */
			failed[j].is_outofdate = 1;
		} else if (failed[j].is_bad) {
			/* If the block is bad we don't know its content, and we try to recover it */
			/* At this point, we can have only BLK ones */

			assert(block_state == BLOCK_STATE_BLK);

			/* we have something we are interested to recover */
			something_to_recover = 1;
		
			/* we try to recover it */
			failed_map[n] = j;
			++n;
		}
	}

	/* if nothing to fix, we just don't try */
	if (something_to_recover) {
		ret = repair_step(state, pos, diskmax, failed, failed_map, n, buffer, buffer_parity, buffer_qarity, buffer_zero);
		if (ret == 0) {
			/* we alreay marked as outdated NEW and CHG blocks, we don't need to do it again */
			return 0;
		}
		if (ret > 0)
			error += ret;
	}

	/* return the number of failed attempts, or -1 if no strategy */
	if (error)
		return error;
	else
		return -1;
}

static int state_check_process(struct snapraid_state* state, int check, int fix, struct snapraid_parity* parity, struct snapraid_parity* qarity, block_off_t blockstart, block_off_t blockmax)
{
	struct snapraid_handle* handle;
	unsigned diskmax;
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
	unsigned error;
	unsigned unrecoverable_error;
	unsigned recovered_error;
	struct failed_struct* failed;
	unsigned* failed_map;

	handle = handle_map(state, &diskmax);

	/* we need disk + 2 for each parity level buffers + 1 zero buffer */
	buffermax = diskmax + state->level * 2 + 1;

	buffer_aligned = malloc_nofail_align(buffermax * state->block_size, &buffer_alloc);
	buffer = malloc_nofail(buffermax * sizeof(void*));
	for(i=0;i<buffermax;++i) {
		buffer[i] = buffer_aligned + i * state->block_size;
	}
	memset(buffer[buffermax-1], 0, state->block_size);

	failed = malloc_nofail(diskmax * sizeof(struct failed_struct));
	failed_map = malloc_nofail(diskmax * sizeof(unsigned));

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
			struct snapraid_block* block = BLOCK_EMPTY;
			if (handle[j].disk)
				block = disk_block_get(handle[j].disk, i);

			/* try to recover all files, even the ones without hash */
			/* because in some cases we can recover also them */
			if (block_has_file(block)
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

	/* check all the blocks in files */
	countsize = 0;
	countpos = 0;
	state_progress_begin(state, blockstart, blockmax, countmax);
	for(i=blockstart;i<blockmax;++i) {
		unsigned failed_count;
		int one_tocheck;
		int valid_parity;

		/* for each disk */
		one_tocheck = 0;
		for(j=0;j<diskmax;++j) {
			struct snapraid_block* block = BLOCK_EMPTY;
			if (handle[j].disk)
				block = disk_block_get(handle[j].disk, i);

			/* try to recover all files, even the ones without hash */
			/* because in some cases we can recover also them */
			if (block_has_file(block)
				&& !file_flag_has(block_file_get(block), FILE_IS_EXCLUDED) /* only if the file is not filtered out */
			) {
				one_tocheck = 1;
				break;
			}
		}

		/* if no block to check skip */
		if (!one_tocheck)
			continue;

		/* If we have valid parity, and it makes sense to check its content. */
		/* If we already know that the parity is invalid, we just read the file */
		/* but we don't report errors */
		/* Note that if check==0, we'll anyway skip the full parity check, */
		/* because we also don't read it at all */
		valid_parity = 1;

		/* keep track of the number of failed blocks */
		failed_count = 0;

		/* for each disk, process the block */
		for(j=0;j<diskmax;++j) {
			int read_size;
			unsigned char hash[HASH_SIZE];
			struct snapraid_block* block;
			unsigned block_state;

			/* if the disk position is not used */
			if (!handle[j].disk) {
				/* use an empty block */
				memset(buffer[j], 0, state->block_size);
				continue;
			}

			/* if the disk block is not used */
			block = disk_block_get(handle[j].disk, i);
			if (block == BLOCK_EMPTY) {
				/* use an empty block */
				memset(buffer[j], 0, state->block_size);
				continue;
			}

			/* get the state of the block */
			block_state = block_state_get(block);

			/* if the parity is not valid */
			if (block_has_invalid_parity(block)) {
				/* mark the parity as invalid, and don't try to check/fix it */
				/* because it will be recomputed at the next sync */
				valid_parity = 0;
			}

			/* if the block is deleted */
			if (block_state == BLOCK_STATE_DELETED) {
				/* use an empty block */
				memset(buffer[j], 0, state->block_size);

				/* store it in the failed set, because potentially */
				/* the parity may be still computed with the previous content */
				failed[failed_count].is_bad = 0;
				failed[failed_count].is_outofdate = 0;
				failed[failed_count].index = j;
				failed[failed_count].block = block;
				failed[failed_count].handle = 0;
				++failed_count;
				continue;
			}

			/* if we are only hashing, we can skip excluded files and don't event read them */
			if (!check && file_flag_has(block_file_get(block), FILE_IS_EXCLUDED)) {
				/* use an empty block */
				/* in true, this is unnecessary, becase we are not checking any parity */
				/* but we keep it for completeness */
				memset(buffer[j], 0, state->block_size);
				continue;
			}

			/* if the file is different than the current one, close it */
			if (handle[j].file != block_file_get(block)) {
				ret = handle_close(&handle[j]);
				if (ret == -1) {
					fprintf(stderr, "DANGER! Unexpected close error in a data disk, it isn't possible to sync.\n");
					printf("Stopping at block %u\n", i);
					++unrecoverable_error;
					goto bail;
				}
			}

			/* if fixing, and the file is not excluded, we must open for writing */
			if (fix && !file_flag_has(block_file_get(block), FILE_IS_EXCLUDED)) {
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
					fprintf(stderr, "error:%u:%s:%s: Size error\n", i, handle[j].disk->name, block_file_get(block)->sub);
					++error;

					/* this is already a recovered error */
					fprintf(stderr, "fixed:%u:%s:%s: Fixed size\n", i, handle[j].disk->name, block_file_get(block)->sub);
					++recovered_error;
				}
			} else {
				/* if checking or hashing, open the file only for reading */
				ret = handle_open(&handle[j], block_file_get(block));
				if (ret == -1) {
					/* save the failed block for the check/fix */
					failed[failed_count].is_bad = 1;
					failed[failed_count].is_outofdate = 0;
					failed[failed_count].index = j;
					failed[failed_count].block = block;
					failed[failed_count].handle = &handle[j];
					++failed_count;

					fprintf(stderr, "error:%u:%s:%s: Open error at position %u\n", i, handle[j].disk->name, block_file_get(block)->sub, block_file_pos(block));
					++error;
					continue;
				}

				/* check if it's a larger file, but not if already notified */
				if (!file_flag_has(block_file_get(block), FILE_IS_LARGER)
					&& handle[j].st.st_size > block_file_get(block)->size
				) {
					fprintf(stderr, "File '%s' is larger than expected.\n", handle[j].path);
					fprintf(stderr, "error:%u:%s:%s: Size error\n", i, handle[j].disk->name, block_file_get(block)->sub);
					++error;

					/* if fragmented, it may be reopened, so store the notification */
					/* to prevent to signal and count the error more than one time */
					file_flag_set(block_file_get(block), FILE_IS_LARGER);
				}
			}

			/* read from the file */
			read_size = handle_read(&handle[j], block, buffer[j], state->block_size);
			if (read_size == -1) {
				/* save the failed block for the check/fix */
				failed[failed_count].is_bad = 1;
				failed[failed_count].is_outofdate = 0;
				failed[failed_count].index = j;
				failed[failed_count].block = block;
				failed[failed_count].handle = &handle[j];
				++failed_count;

				fprintf(stderr, "error:%u:%s:%s: Read error at position %u\n", i, handle[j].disk->name, block_file_get(block)->sub, block_file_pos(block));
				++error;
				continue;
			}

			countsize += read_size;

			/* if the block is potentially not updated */
			if (block_state == BLOCK_STATE_NEW || block_state == BLOCK_STATE_CHG) {
				/* store it in the failed set, because potentially */
				/* the parity may be still computed with the previous content */
				failed[failed_count].is_bad = 0;
				failed[failed_count].is_outofdate = 0;
				failed[failed_count].index = j;
				failed[failed_count].block = block;
				failed[failed_count].handle = &handle[j];
				++failed_count;
				continue;
			}

			/* if the block has the hash */
			if (block_state == BLOCK_STATE_BLK) {
				/* compute the hash of the block just read */
				memhash(state->hash, hash, buffer[j], read_size);

				/* compare the hash */
				if (memcmp(hash, block->hash, HASH_SIZE) != 0) {
					/* save the failed block for the check/fix */
					failed[failed_count].is_bad = 1;
					failed[failed_count].is_outofdate = 0;
					failed[failed_count].index = j;
					failed[failed_count].block = block;
					failed[failed_count].handle = &handle[j];
					++failed_count;

					fprintf(stderr, "error:%u:%s:%s: Data error at position %u\n", i, handle[j].disk->name, block_file_get(block)->sub, block_file_pos(block));
					++error;
					continue;
				}
			}
		}

		/* now read and check the parity if requested */
		if (check) {
			unsigned char* buffer_parity;
			unsigned char* buffer_qarity;
			unsigned char* buffer_zero;

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
			if (parity) {
				ret = parity_read(parity, i, buffer_parity, state->block_size);
				if (ret == -1) {
					buffer_parity = 0; /* no parity to use */

					fprintf(stderr, "error:%u:parity: Read error\n", i);
					++error;
				}
			} else {
				buffer_parity = 0;
			}

			/* read the qarity */
			if (state->level >= 2) {
				if (qarity) {
					ret = parity_read(qarity, i, buffer_qarity, state->block_size);
					if (ret == -1) {
						buffer_qarity = 0; /* no qarity to use */

						fprintf(stderr, "error:%u:qarity: Read error\n", i);
						++error;
					}
				} else {
					buffer_qarity = 0;
				}
			}

			/* try all the recovering strategies */
			ret = repair(state, i, diskmax, failed, failed_map, failed_count, buffer, buffer_parity, buffer_qarity, buffer_zero);
			if (ret != 0) {
				/* increment the number of errors */
				if (ret > 0)
					error += ret;
				++unrecoverable_error;

				/* print a list of all the errors in files */
				for(j=0;j<failed_count;++j) {
					if (failed[j].is_bad)
						fprintf(stderr, "unrecoverable:%u:%s:%s: Unrecoverable error at position %u\n", i, failed[j].handle->disk->name, block_file_get(failed[j].block)->sub, block_file_pos(failed[j].block));
				}

				if (fix) {
					/* keep track of damaged files */
					for(j=0;j<failed_count;++j) {
						if (failed[j].is_bad)
							file_flag_set(block_file_get(failed[j].block), FILE_IS_DAMAGED);
					}
				}
			} else {
				/* now counts partial recovers */
				/* note that this could happen only when we have an incomplete 'sync' */
				/* and that we have recovered is the state before the 'sync' */
				int partial_recover_error = 0;

				/* print a list of all the errors in files */
				for(j=0;j<failed_count;++j) {
					if (failed[j].is_bad && failed[j].is_outofdate) {
						++partial_recover_error;
						fprintf(stderr, "unrecoverable:%u:%s:%s: Unrecoverable error at position %u\n", i, failed[j].handle->disk->name, block_file_get(failed[j].block)->sub, block_file_pos(failed[j].block));
					}
				}
				if (partial_recover_error != 0) {
					error += partial_recover_error;
					++unrecoverable_error;
				}

				/* now check parity and q-parity, but only if all the blocks have it computed */
				/* if you check/fix after a partial sync, it's OK to have parity errors on the blocks with invalid parity */
				/* and doesn't make sense to try to fix it */
				if (valid_parity) {
					/* check the parity */
					if (buffer_parity != 0 && memcmp(buffer_parity, buffer[diskmax], state->block_size) != 0) {
						buffer_parity = 0;

						fprintf(stderr, "error:%u:parity: Data error\n", i);
						++error;
					}

					/* check the qarity */
					if (state->level >= 2) {
						if (buffer_qarity != 0 && memcmp(buffer_qarity, buffer[diskmax + 1], state->block_size) != 0) {
							buffer_qarity = 0;

							fprintf(stderr, "error:%u:qarity: Data error\n", i);
							++error;
						}
					}
				}

				/* now writes recovered files */
				if (fix) {
					/* update the fixed files */
					for(j=0;j<failed_count;++j) {
						/* nothing to do if it doesn't need recovering */
						if (!failed[j].is_bad)
							continue;

						/* do not fix if the file filtered out */
						if (file_flag_has(block_file_get(failed[j].block), FILE_IS_EXCLUDED))
							continue;

						ret = handle_write(failed[j].handle, failed[j].block, buffer[failed[j].index], state->block_size);
						if (ret == -1) {
							/* mark the file as damaged */
							file_flag_set(block_file_get(failed[j].block), FILE_IS_DAMAGED);

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

						/* if we are not sure that the recovered content is uptodate */
						if (failed[j].is_outofdate) {
							/* mark the file as damaged */
							file_flag_set(block_file_get(failed[j].block), FILE_IS_DAMAGED);
							continue;
						}

						/* mark the file as fixed */
						file_flag_set(block_file_get(failed[j].block), FILE_IS_FIXED);

						fprintf(stderr, "fixed:%u:%s:%s: Fixed data error at position %u\n", i, failed[j].handle->disk->name, block_file_get(failed[j].block)->sub, block_file_pos(failed[j].block));
						++recovered_error;
					}

					/* update parity and q-parity only if all the blocks have it computed */
					/* if you check/fix after a partial sync, you do not want to fix parity */
					/* for blocks that are going to have it computed in the sync completion */
					if (valid_parity) {
						/* update the parity */
						if (buffer_parity == 0 && parity) {
							ret = parity_write(parity, i, buffer[diskmax], state->block_size);
							if (ret == -1) {
								/* we do not use DANGER because it could be ENOSPC which is not always correctly reported */
								fprintf(stderr, "WARNING! Without a working Parity disk, it isn't possible to fix errors on it.\n");
								printf("Stopping at block %u\n", i);
								++unrecoverable_error;
								goto bail;
							}

							fprintf(stderr, "fixed:%u:parity: Fixed data error\n", i);
							++recovered_error;
						}

						/* update the qarity */
						if (state->level >= 2) {
							if (buffer_qarity == 0 && qarity) {
								ret = parity_write(qarity, i, buffer[diskmax + 1], state->block_size);
								if (ret == -1) {
									/* we do not use DANGER because it could be ENOSPC which is not always correctly reported */
									fprintf(stderr, "WARNING! Without a working Q-Parity disk, it isn't possible to fix errors on it.\n");
									printf("Stopping at block %u\n", i);
									++unrecoverable_error;
									goto bail;
								}

								fprintf(stderr, "fixed:%u:qarity: Fixed data error\n", i);
								++recovered_error;
							}
						}
					}
				}
			}
		}

		/* finish the fix process if it's the last block of the files */
		if (fix) {
			/* for all the files of this block check if we need to fix the modification time */
			for(j=0;j<diskmax;++j) {
				struct snapraid_block* block = BLOCK_EMPTY;
				struct snapraid_file* collide_file;
				uint64_t inode;

				if (handle[j].disk)
					block = disk_block_get(handle[j].disk, i);

				if (!block_has_file(block)) {
					/* if no file, nothing to do */
					continue;
				}

				/* if the file is excluded, we have nothing to fix */
				/* note that this check is required, because if the file is excluded */
				/* it's also possible to have it not opened, */
				/* and have the handle[j].file pointing to NULL. */
				/* A typical case is if the file is missing, */
				/* and the read-only open failed before. */
				if (file_flag_has(block_file_get(block), FILE_IS_EXCLUDED)) {
					/* nothing to do */
					continue;
				}

				/* if it isn't the last block in the file */
				if (!block_is_last(block)) {
					/* nothing to do */
					continue;
				}

				/* if the file is damaged, meaning that a fix failed */
				if (file_flag_has(handle[j].file, FILE_IS_DAMAGED)) {
					/* rename it to .unrecoverable */
					char path_from[PATH_MAX];
					char path_to[PATH_MAX];

					pathprint(path_from, sizeof(path_from), "%s%s", handle[j].disk->dir, handle[j].file->sub);
					pathprint(path_to, sizeof(path_to), "%s%s.unrecoverable", handle[j].disk->dir, handle[j].file->sub);

					/* ensure to operate on a closed file */
					ret = handle_close(&handle[j]);
					if (ret != 0) {
						fprintf(stderr, "Error closing '%s'. %s.\n", path_from, strerror(errno));
						fprintf(stderr, "WARNING! Without a working data disk, it isn't possible to fix errors on it.\n");
						printf("Stopping at block %u\n", i);
						++unrecoverable_error;
						goto bail;
					}

					ret = rename(path_from, path_to);
					if (ret != 0) {
						fprintf(stderr, "Error renaming  '%s' to '%s'. %s.\n", path_from, path_to, strerror(errno));
						fprintf(stderr, "WARNING! Without a working data disk, it isn't possible to fix errors on it.\n");
						printf("Stopping at block %u\n", i);
						++unrecoverable_error;
						goto bail;
					}

					/* and do not set the time if damaged */
					continue;
				}

				/* if the file is not fixed, meaning that it is untouched */
				if (!file_flag_has(handle[j].file, FILE_IS_FIXED)) {
					/* nothing to do */
					continue;
				}
				
				inode = handle[j].st.st_ino;

				/* search for the corresponding inode */
				collide_file = tommy_hashdyn_search(&handle[j].disk->inodeset, file_inode_compare, &inode, file_inode_hash(inode));

				/* if the inode is already in the database and it refers at a different file name, */
				/* we can fix the file time ONLY if the time and size allow to differentiates */
				/* between the two files */

				/* for example, suppose we delete a bunch of files with all the same size and time, */
				/* when recreating them the inodes may be reused in a different order, */
				/* and at the next sync some files may have matching inode/size/time even if different name */
				/* not allowing sync to detect that the file is changed and not renamed */
				if (!collide_file /* if not in the database, there is no collision */
					|| strcmp(collide_file->sub, handle[j].file->sub) == 0 /* if the name is the same, it's the right collision */
					|| collide_file->size != handle[j].file->size /* if the size is different, the collision is identified */
					|| collide_file->mtime_sec != handle[j].file->mtime_sec /* if the mtime is different, the collision is identified */
					|| collide_file->mtime_nsec != handle[j].file->mtime_nsec
				) {
					/* set the original modification time */
					ret = handle_utime(&handle[j]);
					if (ret == -1) {
						/* mark the file as damaged */
						file_flag_set(handle[j].file, FILE_IS_DAMAGED);
						fprintf(stderr, "WARNING! Without a working data disk, it isn't possible to fix errors on it.\n");
						printf("Stopping at block %u\n", i);
						++unrecoverable_error;
						goto bail;
					}
				} else {
					fprintf(stderr, "collision:%s:%s:%s: Not setting modification time to avoid inode collision\n", handle[j].disk->name, block_file_get(block)->sub, collide_file->sub);
				}
			}
		}

		/* count the number of processed block */
		++countpos;

		/* progress */
		if (state_progress(state, i, countpos, countmax, countsize)) {
			break;
		}
	}

	/* for each disk, recover empty files, symlinks and empty dirs */
	for(i=0;i<diskmax;++i) {
		tommy_node* node;
		struct snapraid_disk* disk;

		if (!handle[i].disk)
			continue;

		/* for each file in the disk */
		disk = handle[i].disk;
		node = disk->filelist;
		while (node) {
			char path[PATH_MAX];
			struct stat st;
			struct snapraid_file* file;
			int failed = 0;

			file = node->data;
			node = node->next; /* next node */

			/* if not empty, it's already checked and continue to the next one */
			if (file->size != 0) {
				continue;
			}

			/* if excluded continue to the next one */
			if (file_flag_has(file, FILE_IS_EXCLUDED)) {
				continue;
			}

			/* stat the file */
			pathprint(path, sizeof(path), "%s%s", disk->dir, file->sub);
			ret = stat(path, &st);
			if (ret == -1) {
				failed = 1;

				fprintf(stderr, "Error stating empty file '%s'. %s.\n", path, strerror(errno));
				fprintf(stderr, "error:%s:%s: Empty file stat error\n", disk->name, file->sub);
				++error;
			} else if (!S_ISREG(st.st_mode)) {
				failed = 1;

				fprintf(stderr, "error:%s:%s: Empty file error for not regular file\n", disk->name, file->sub);
				++error;
			} else if (st.st_size != 0) {
				failed = 1;

				fprintf(stderr, "error:%s:%s: Empty file error for size '%"PRIu64"'\n", disk->name, file->sub, st.st_size);
				++error;
			}

			if (fix && failed) {
				int f;

				/* create the ancestor directories */
				ret = mkancestor(path);
				if (ret != 0) {
					fprintf(stderr, "WARNING! Without a working data disk, it isn't possible to fix errors on it.\n");
					printf("Stopping\n");
					++unrecoverable_error;
					goto bail;
				}

				/* create it */
				/* O_NOFOLLOW: do not follow links to ensure to open the real file */
				f = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY | O_NOFOLLOW, 0600);
				if (f == -1) {
					fprintf(stderr, "Error creating empty file '%s'. %s.\n", path, strerror(errno));
					if (errno == EACCES) {
						fprintf(stderr, "WARNING! Please give write permission to the file.\n");
					} else {
						/* we do not use DANGER because it could be ENOSPC which is not always correctly reported */
						fprintf(stderr, "WARNING! Without a working data disk, it isn't possible to fix errors on it.\n");
					}
					printf("Stopping\n");
					++unrecoverable_error;
					goto bail;
				}

				/* close it */
				close(f);

				fprintf(stderr, "fixed:%s:%s: Fixed empty file\n", disk->name, file->sub);
				++recovered_error;
			}
		}

		/* for each link in the disk */
		disk = handle[i].disk;
		node = disk->linklist;
		while (node) {
			char path[PATH_MAX];
			char pathto[PATH_MAX];
			char linkto[PATH_MAX];
			struct stat st;
			struct stat stto;
			struct snapraid_link* link;
			int failed = 0;

			link = node->data;
			node = node->next; /* next node */

			/* if excluded continue to the next one */
			if (link_flag_has(link, FILE_IS_EXCLUDED)) {
				continue;
			}

			if (link_flag_has(link, FILE_IS_HARDLINK)) {
				/* stat the link */
				pathprint(path, sizeof(path), "%s%s", disk->dir, link->sub);
				ret = stat(path, &st);
				if (ret == -1) {
					failed = 1;

					fprintf(stderr, "Error stating hardlink '%s'. %s.\n", path, strerror(errno));
					fprintf(stderr, "hardlinkerror:%s:%s:%s: Hardlink stat error\n", disk->name, link->sub, link->linkto);
					++error;
				} else if (!S_ISREG(st.st_mode)) {
					failed = 1;

					fprintf(stderr, "hardlinkerror:%s:%s:%s: Hardlink error for not regular file\n", disk->name, link->sub, link->linkto);
					++error;
				}

				/* stat the "to" file */
				pathprint(pathto, sizeof(pathto), "%s%s", disk->dir, link->linkto);
				ret = stat(pathto, &stto);
				if (ret == -1) {
					failed = 1;

					fprintf(stderr, "Error stating hardlink-to '%s'. %s.\n", pathto, strerror(errno));
					fprintf(stderr, "hardlinkerror:%s:%s:%s: Hardlink to stat error\n", disk->name, link->sub, link->linkto);
					++error;
				} else if (!S_ISREG(stto.st_mode)) {
					failed = 1;

					fprintf(stderr, "hardlinkerror:%s:%s:%s: Hardlink-to error for not regular file\n", disk->name, link->sub, link->linkto);
					++error;
				} else if (!failed && st.st_ino != stto.st_ino) {
					failed = 1;

					fprintf(stderr, "Mismatch hardlink '%s' and '%s'. Different inode.\n", path, pathto);
					fprintf(stderr, "hardlinkerror:%s:%s:%s: Hardlink mismatch for different inode\n", disk->name, link->sub, link->linkto);
					++error;
				}
			} else {
				/* read the symlink */
				pathprint(path, sizeof(path), "%s%s", disk->dir, link->sub);
				ret = readlink(path, linkto, sizeof(linkto));
				if (ret < 0) {
					failed = 1;

					fprintf(stderr, "Error reading symlink '%s'. %s.\n", path, strerror(errno));
					fprintf(stderr, "symlinkerror:%s:%s: Symlink read error\n", disk->name, link->sub);
					++error;
				} else if (ret >= PATH_MAX) {
					failed = 1;

					fprintf(stderr, "Error reading symlink '%s'. Symlink too long.\n", path);
					fprintf(stderr, "symlinkerror:%s:%s: Symlink read error\n", disk->name, link->sub);
					++error;
				} else {
					linkto[ret] = 0;

					if (strcmp(linkto, link->linkto) != 0) {
						failed = 1;

						fprintf(stderr, "symlinkerror:%s:%s: Symlink data error '%s' instead of '%s'\n", disk->name, link->sub, linkto, link->linkto);
						++error;
					}
				}
			}

			if (fix && failed) {
				/* create the ancestor directories */
				ret = mkancestor(path);
				if (ret != 0) {
					fprintf(stderr, "WARNING! Without a working data disk, it isn't possible to fix errors on it.\n");
					printf("Stopping\n");
					++unrecoverable_error;
					goto bail;
				}

				/* if it exists, it must be deleted before recreating */
				ret = remove(path);
				if (ret != 0 && errno != ENOENT) {
					fprintf(stderr, "Error removing '%s'. %s.\n", path, strerror(errno));
					fprintf(stderr, "WARNING! Without a working data disk, it isn't possible to fix errors on it.\n");
					printf("Stopping\n");
					++unrecoverable_error;
					goto bail;
				}

				/* create it */
				if (link_flag_has(link, FILE_IS_HARDLINK)) {
					ret = hardlink(pathto, path);
					if (ret != 0) {
						fprintf(stderr, "Error writing hardlink '%s'. %s.\n", path, strerror(errno));
						if (errno == EACCES) {
							fprintf(stderr, "WARNING! Please give write permission to the hardlink.\n");
						} else {
							/* we do not use DANGER because it could be ENOSPC which is not always correctly reported */
							fprintf(stderr, "WARNING! Without a working data disk, it isn't possible to fix errors on it.\n");
						}
						printf("Stopping\n");
						++unrecoverable_error;
						goto bail;
					}

					fprintf(stderr, "hardlinkfixed:%s:%s: Fixed hardlink error\n", disk->name, link->sub);
					++recovered_error;
				} else {
					ret = symlink(link->linkto, path);
					if (ret != 0) {
						fprintf(stderr, "Error writing symlink '%s'. %s.\n", path, strerror(errno));
						if (errno == EACCES) {
							fprintf(stderr, "WARNING! Please give write permission to the symlink.\n");
						} else {
							/* we do not use DANGER because it could be ENOSPC which is not always correctly reported */
							fprintf(stderr, "WARNING! Without a working data disk, it isn't possible to fix errors on it.\n");
						}
						printf("Stopping\n");
						++unrecoverable_error;
						goto bail;
					}

					fprintf(stderr, "symlinkfixed:%s:%s: Fixed symlink error\n", disk->name, link->sub);
					++recovered_error;
				}
			}
		}

		/* for each dir in the disk */
		disk = handle[i].disk;
		node = disk->dirlist;
		while (node) {
			char path[PATH_MAX];
			struct stat st;
			struct snapraid_dir* dir;
			int failed = 0;

			dir = node->data;
			node = node->next; /* next node */

			/* if excluded continue to the next one */
			if (dir_flag_has(dir, FILE_IS_EXCLUDED)) {
				continue;
			}

			/* stat the dir */
			pathprint(path, sizeof(path), "%s%s", disk->dir, dir->sub);
			ret = stat(path, &st);
			if (ret == -1) {
				failed = 1;

				fprintf(stderr, "Error stating dir '%s'. %s.\n", path, strerror(errno));
				fprintf(stderr, "dir_error:%s:%s: Dir stat error\n", disk->name, dir->sub);
				++error;
			} else if (!S_ISDIR(st.st_mode)) {
				failed = 1;

				fprintf(stderr, "dir_error:%s:%s: Dir error for not directory\n", disk->name, dir->sub);
				++error;
			}

			if (fix && failed) {
				/* create the ancestor directories */
				ret = mkancestor(path);
				if (ret != 0) {
					fprintf(stderr, "WARNING! Without a working data disk, it isn't possible to fix errors on it.\n");
					printf("Stopping\n");
					++unrecoverable_error;
					goto bail;
				}

				/* create it */
				ret = mkdir(path, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
				if (ret != 0) {
					fprintf(stderr, "Error creating dir '%s'. %s.\n", path, strerror(errno));
					if (errno == EACCES) {
						fprintf(stderr, "WARNING! Please give write permission to the dir.\n");
					} else {
						/* we do not use DANGER because it could be ENOSPC which is not always correctly reported */
						fprintf(stderr, "WARNING! Without a working data disk, it isn't possible to fix errors on it.\n");
					}
					printf("Stopping\n");
					++unrecoverable_error;
					goto bail;
				}

				fprintf(stderr, "dir_fixed:%s:%s: Fixed dir error\n", disk->name, dir->sub);
				++recovered_error;
			}
		}
	}

	state_progress_end(state, countpos, countmax, countsize);

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
		else {
			/* without checking, we don't know if they are really recoverable or not */
			if (check)
				printf("No unrecoverable errors\n");
		}
	} else {
		printf("No error\n");
	}

	free(failed);
	free(failed_map);
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

void state_check(struct snapraid_state* state, int check, int fix, block_off_t blockstart, block_off_t blockcount)
{
	block_off_t blockmax;
	data_off_t size;
	data_off_t out_size;
	int ret;
	struct snapraid_parity parity;
	struct snapraid_parity qarity;
	struct snapraid_parity* parity_ptr;
	struct snapraid_parity* qarity_ptr;
	unsigned error;

	printf("Initializing...\n");

	if (!check && fix) {
		fprintf(stderr, "Error in calling, you cannot fix without checking parity.\n");
		exit(EXIT_FAILURE);
	}

	blockmax = parity_size(state);
	size = blockmax * (data_off_t)state->block_size;

	if (blockstart > blockmax) {
		fprintf(stderr, "Error in the specified starting block %u. It's bigger than the parity size %u.\n", blockstart, blockmax);
		exit(EXIT_FAILURE);
	}

	/* adjust the number of block to process */
	if (blockcount != 0 && blockstart + blockcount < blockmax) {
		blockmax = blockstart + blockcount;
	}

	if (fix) {
		/* if fixing, create the file and open for writing */
		/* if it fails, we cannot continue */
		parity_ptr = &parity;
		ret = parity_create(parity_ptr, state->parity, &out_size);
		if (ret == -1) {
			fprintf(stderr, "WARNING! Without an accessible Parity file, it isn't possible to fix any error.\n");
			exit(EXIT_FAILURE);
		}

		ret = parity_chsize(parity_ptr, size, &out_size, state->skip_fallocate);
		if (ret == -1) {
			fprintf(stderr, "WARNING! Without an accessible Parity file, it isn't possible to sync.\n");
			exit(EXIT_FAILURE);
		}

		if (state->level >= 2) {
			qarity_ptr = &qarity;
			ret = parity_create(qarity_ptr, state->qarity, &out_size);
			if (ret == -1) {
				fprintf(stderr, "WARNING! Without an accessible Q-Parity file, it isn't possible to fix any error.\n");
				exit(EXIT_FAILURE);
			}

			ret = parity_chsize(qarity_ptr, size, &out_size, state->skip_fallocate);
			if (ret == -1) {
				fprintf(stderr, "WARNING! Without an accessible Q-Parity file, it isn't possible to sync.\n");
				exit(EXIT_FAILURE);
			}
		} else {
			qarity_ptr = 0;
		}
	} else if (check) {
		/* if checking, open the file for reading */
		/* it may fail if the file doesn't exist, in this case we continue to check the files */
		parity_ptr = &parity;
		ret = parity_open(parity_ptr, state->parity);
		if (ret == -1) {
			printf("No accessible Parity file, only files will be checked.\n");
			/* continue anyway */
			parity_ptr = 0;
		}

		if (state->level >= 2) {
			qarity_ptr = &qarity;
			ret = parity_open(qarity_ptr, state->qarity);
			if (ret == -1) {
				printf("No accessible Q-Parity file, only files will be checked.\n");
				/* continue anyway */
				qarity_ptr = 0;
			}
		} else {
			qarity_ptr = 0;
		}
	} else {
		/* otherwise don't use any parity */
		parity_ptr = 0;
		qarity_ptr = 0;
	}

	if (fix)
		printf("Fixing...\n");
	else if (check)
		printf("Checking...\n");
	else
		printf("Hashing...\n");

	error = 0;

	/* skip degenerated cases of empty parity, or skipping all */
	if (blockstart < blockmax) {
		ret = state_check_process(state, check, fix, parity_ptr, qarity_ptr, blockstart, blockmax);
		if (ret == -1) {
			++error;
			/* continue, as we are already exiting */
		}
	}

	/* try to close only if opened */
	if (parity_ptr) {
		ret = parity_close(parity_ptr);
		if (ret == -1) {
			fprintf(stderr, "DANGER! Unexpected close error in Parity disk.\n");
			++error;
			/* continue, as we are already exiting */
		}
	}

	if (state->level >= 2) {
		if (qarity_ptr) {
			ret = parity_close(qarity_ptr);
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

