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

#include "support.h"
#include "util.h"
#include "elem.h"
#include "import.h"
#include "search.h"
#include "state.h"
#include "parity.h"
#include "handle.h"
#include "raid/raid.h"
#include "raid/combo.h"

/****************************************************************************/
/* check */

/**
 * A block that failed the hash check, or that was deleted.
 */
struct failed_struct {
	/**
	 * If we know for sure that the block is garbage or missing
	 * and it needs to be recovered and rewritten to the disk.
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
	 * Note that this could happen only for CHG blocks.
	 */
	int is_outofdate;

	unsigned index; /**< Index of the failed block. */
	struct snapraid_block* block; /**< The failed block */
	struct snapraid_disk* disk; /**< The failed disk. */
	struct snapraid_file* file; /**< The failed file. 0 for DELETED block. */
	block_off_t file_pos; /**< Offset inside the file */
	struct snapraid_handle* handle; /**< The handle containing the failed block, or 0 for a DELETED block */
};

/**
 * Check if a block hash matches the specified buffer.
 * Return ==0 if equal
 */
static int blockcmp(struct snapraid_state* state, int rehash, struct snapraid_block* block, unsigned pos_size, unsigned char* buffer, unsigned char* buffer_zero)
{
	unsigned char hash[HASH_MAX];

	/* now compute the hash of the valid part */
	if (rehash) {
		memhash(state->prevhash, state->prevhashseed, hash, buffer, pos_size);
	} else {
		memhash(state->hash, state->hashseed, hash, buffer, pos_size);
	}

	/* compare the hash */
	if (memcmp(hash, block->hash, BLOCK_HASH_SIZE) != 0) {
		return -1;
	}

	/* compare to the end of the block */
	if (pos_size < state->block_size) {
		if (memcmp(buffer + pos_size, buffer_zero + pos_size, state->block_size - pos_size) != 0) {
			return -1;
		}
	}

	return 0;
}

/**
 * Check if the hash of all the failed block we are expecting to recover are now matching.
 */
static int is_hash_matching(struct snapraid_state* state, int rehash, unsigned diskmax, struct failed_struct* failed, unsigned* failed_map, unsigned failed_count, void** buffer, void* buffer_zero)
{
	unsigned j;
	int hash_checked;

	hash_checked = 0; /* keep track if we check at least one block */

	/* check if the recovered blocks are OK */
	for (j = 0; j < failed_count; ++j) {
		/* if we are expected to recover this block */
		if (!failed[failed_map[j]].is_outofdate
		        /* if the block has a hash to check */
			&& block_has_updated_hash(failed[failed_map[j]].block)
		) {
			/* if a hash doesn't match, fail the check */
			unsigned pos_size = file_block_size(failed[failed_map[j]].file, failed[failed_map[j]].file_pos, state->block_size);
			if (blockcmp(state, rehash, failed[failed_map[j]].block, pos_size, buffer[failed[failed_map[j]].index], buffer_zero) != 0) {
				log_tag("hash_error: Hash mismatch on entry %u\n", failed_map[j]);
				return 0;
			}

			hash_checked = 1;
		}
	}

	/* if nothing checked, we reject it */
	/* note that we are excluding this case at upper level */
	/* but checking again doesn't hurt */
	if (!hash_checked) {
		/* LCOV_EXCL_START */
		return 0;
		/* LCOV_EXCL_STOP */
	}

	/* if we checked something, and no block failed the check */
	/* recompute all the redundancy information */
	raid_gen(diskmax, state->level, state->block_size, buffer);
	return 1;
}

/**
 * Check if specified parity is now matching with a recomputed one.
 */
static int is_parity_matching(struct snapraid_state* state, unsigned diskmax, unsigned i, void** buffer, void** buffer_recov)
{
	/* recompute parity, note that we don't need parity over i */
	raid_gen(diskmax, i + 1, state->block_size, buffer);

	/* if the recovered parity block matches */
	if (memcmp(buffer[diskmax + i], buffer_recov[i], state->block_size) == 0) {
		/* recompute all the redundancy information */
		raid_gen(diskmax, state->level, state->block_size, buffer);
		return 1;
	}

	return 0;
}

/**
 * Repair errors.
 * Return <0 if failure for missing strategy, >0 if data is wrong and we cannot rebuild correctly, 0 on success.
 * If success, the parity are computed in the buffer variable.
 */
static int repair_step(struct snapraid_state* state, int rehash, unsigned pos, unsigned diskmax, struct failed_struct* failed, unsigned* failed_map, unsigned failed_count, void** buffer, void** buffer_recov, void* buffer_zero)
{
	unsigned i, n;
	int error;
	int has_hash;
	int id[LEV_MAX];
	int ip[LEV_MAX];

	/* no fix required, already checked at higher level, but just to be sure */
	if (failed_count == 0) {
		/* LCOV_EXCL_START */
		/* recompute only the parity */
		raid_gen(diskmax, state->level, state->block_size, buffer);
		return 0;
		/* LCOV_EXCL_STOP */
	}

	n = state->level;
	error = 0;

	/* setup vector of failed disk indexes */
	for (i = 0; i < failed_count; ++i)
		id[i] = failed[failed_map[i]].index;

	/* check if there is at least a failed block that can be checked for correctness using the hash */
	/* if there isn't, we have to sacrifice a parity block to check that the result is correct */
	has_hash = 0;
	for (i = 0; i < failed_count; ++i) {
		/* if we are expected to recover this block */
		if (!failed[failed_map[i]].is_outofdate
		        /* if the block has a hash to check */
			&& block_has_updated_hash(failed[failed_map[i]].block)
		)
			has_hash = 1;
	}

	/* if we don't have a hash, but we have an extra parity */
	/* (strictly-less failures than number of parities) */
	if (!has_hash && failed_count < n) {
		/* number of parity to use, one more to check the recovering */
		unsigned r = failed_count + 1;

		/* all combinations (r of n) parities */
		combination_first(r, n, ip);
		do {
			/* if a parity is missing, do nothing */
			for (i = 0; i < r; ++i) {
				if (buffer_recov[ip[i]] == 0)
					break;
			}
			if (i != r)
				continue;

			/* copy the parities to use, one less because the last is used for checking */
			for (i = 0; i < r - 1; ++i)
				memcpy(buffer[diskmax + ip[i]], buffer_recov[ip[i]], state->block_size);

			/* recover using one less parity, the ip[r-1] one */
			raid_data(r - 1, id, ip, diskmax, state->block_size, buffer);

			/* use the remaining ip[r-1] parity to check the result */
			if (is_parity_matching(state, diskmax, ip[r - 1], buffer, buffer_recov))
				return 0;

			/* log */
			log_tag("parity_error:%u:", pos);
			for (i = 0; i < r; ++i) {
				if (i != 0)
					log_tag("/");
				log_tag("%s", lev_config_name(ip[i]));
			}
			log_tag(":parity: Parity mismatch\n");
			++error;
		} while (combination_next(r, n, ip));
	}

	/* if we have a hash, and enough parities */
	/* (less-or-equal failures than number of parities) */
	if (has_hash && failed_count <= n) {
		/* number of parities to use equal at the number of failures */
		unsigned r = failed_count;

		/* all combinations (r of n) parities */
		combination_first(r, n, ip);
		do {
			/* if a parity is missing, do nothing */
			for (i = 0; i < r; ++i) {
				if (buffer_recov[ip[i]] == 0)
					break;
			}
			if (i != r)
				continue;

			/* copy the parities to use */
			for (i = 0; i < r; ++i)
				memcpy(buffer[diskmax + ip[i]], buffer_recov[ip[i]], state->block_size);

			/* recover */
			raid_data(r, id, ip, diskmax, state->block_size, buffer);

			/* use the hash to check the result */
			if (is_hash_matching(state, rehash, diskmax, failed, failed_map, failed_count, buffer, buffer_zero))
				return 0;

			/* log */
			log_tag("parity_error:%u:", pos);
			for (i = 0; i < r; ++i) {
				if (i != 0)
					log_tag("/");
				log_tag("%s", lev_config_name(ip[i]));
			}
			log_tag(":hash: Hash mismatch\n");
			++error;
		} while (combination_next(r, n, ip));
	}

	/* return the number of failed attempts, or -1 if no strategy */
	if (error)
		return error;

	log_tag("strategy_error:%u: No strategy to recover from %u failures with %u parity %s hash\n",
		pos, failed_count, n, has_hash ? "with" : "without");
	return -1;
}

static int repair(struct snapraid_state* state, int rehash, unsigned pos, unsigned diskmax, struct failed_struct* failed, unsigned* failed_map, unsigned failed_count, void** buffer, void** buffer_recov, void* buffer_zero)
{
	int ret;
	int error;
	unsigned j;
	int n;
	int something_to_recover;
	int something_unsynced;
	char esc_buffer[ESC_MAX];

	error = 0;

	/* if nothing failed, just recompute the parity */
	if (failed_count == 0) {
		raid_gen(diskmax, state->level, state->block_size, buffer);
		return 0;
	}

	/* logs the status */
	for (j = 0; j < failed_count; ++j) {
		const char* desc;
		const char* hash;
		const char* data;
		struct snapraid_block* block = failed[j].block;
		unsigned block_state = block_state_get(block);

		switch (block_state) {
		case BLOCK_STATE_DELETED : desc = "delete"; break;
		case BLOCK_STATE_CHG : desc = "change"; break;
		case BLOCK_STATE_REP : desc = "replace"; break;
		case BLOCK_STATE_BLK : desc = "block"; break;
		/* LCOV_EXCL_START */
		default : desc = "unknown"; break;
			/* LCOV_EXCL_STOP */
		}

		if (hash_is_invalid(block->hash)) {
			hash = "lost";
		} else if (hash_is_zero(block->hash)) {
			hash = "zero";
		} else {
			hash = "known";
		}

		if (failed[j].is_bad)
			data = "bad";
		else
			data = "good";

		if (failed[j].file) {
			struct snapraid_disk* disk = failed[j].disk;
			struct snapraid_file* file = failed[j].file;
			block_off_t file_pos = failed[j].file_pos;

			log_tag("entry:%u:%s:%s:%s:%s:%s:%u:\n", j, desc, hash, data, disk->name, esc_tag(file->sub, esc_buffer), file_pos);
		} else {
			log_tag("entry:%u:%s:%s:%s:\n", j, desc, hash, data);
		}
	}

	/* Here we have to try two different strategies to recover, because in case the 'sync' */
	/* process is aborted, we don't know if the parity data is really updated just like after 'sync', */
	/* or if it still represents the state before the 'sync'. */

	/* Note that if the 'sync' ends normally, we don't have any DELETED, REP and CHG blocks */
	/* and the two strategies are identical */

	/* As first, we assume that the parity IS updated for the current state */
	/* and that we are going to recover the state after the last 'sync'. */
	/* In this case, parity contains info from BLK, REP and CHG blocks, */
	/* but not for DELETED. */
	/* We need to put in the recovering process only the bad blocks, because all the */
	/* others already contains the correct data read from disk, and the parity is correctly computed for them. */
	/* We are interested to recover BLK, REP and CHG blocks if they are marked as bad, */
	/* but we are not interested in DELETED ones. */

	n = 0;
	something_to_recover = 0; /* keep track if there is at least one block to fix */
	for (j = 0; j < failed_count; ++j) {
		if (failed[j].is_bad) {
			unsigned block_state = block_state_get(failed[j].block);

			assert(block_state != BLOCK_STATE_DELETED); /* we cannot have bad DELETED blocks */

			/* if we have the hash for it */
			if ((block_state == BLOCK_STATE_BLK || block_state == BLOCK_STATE_REP)
			        /* try to fetch the block using the known hash */
				&& (state_import_fetch(state, rehash, failed[j].block, buffer[failed[j].index]) == 0
					|| state_search_fetch(state, rehash, failed[j].file, failed[j].file_pos, failed[j].block, buffer[failed[j].index]) == 0)
			) {
				/* we already have corrected it! */
				log_tag("hash_import: Fixed entry %u\n", j);
			} else {
				/* otherwise try to recover it */
				failed_map[n] = j;
				++n;

				/* we have something to try to recover */
				something_to_recover = 1;
			}
		}
	}

	/* if nothing to fix */
	if (!something_to_recover) {
		log_tag("recover_sync:%u:%u: Skipped for already recovered\n", pos, n);

		/* recompute only the parity */
		raid_gen(diskmax, state->level, state->block_size, buffer);
		return 0;
	}

	ret = repair_step(state, rehash, pos, diskmax, failed, failed_map, n, buffer, buffer_recov, buffer_zero);
	if (ret == 0) {
		/* reprocess the CHG blocks, for which we don't have a hash to check */
		/* if they were BAD we have to use some heuristics to ensure that we have recovered  */
		/* the state after the sync. If unsure, we assume the worst case */

		for (j = 0; j < failed_count; ++j) {
			/* we take care only of BAD blocks we have to write back */
			if (failed[j].is_bad) {
				unsigned block_state = block_state_get(failed[j].block);

				/* BLK and REP blocks are always OK, because at this point */
				/* we have already checked their hash */
				if (block_state != BLOCK_STATE_CHG) {
					assert(block_state == BLOCK_STATE_BLK || block_state == BLOCK_STATE_REP);
					continue;
				}

				/* for CHG blocks we have to 'guess' if they are correct or not */

				/* if the hash is invalid we cannot check the result */
				/* this could happen if we have lost this information */
				/* after an aborted sync */
				if (hash_is_invalid(failed[j].block->hash)) {
					/* it may contain garbage */
					failed[j].is_outofdate = 1;

					log_tag("hash_unknown: Unknown hash on entry %u\n", j);
				} else if (hash_is_zero(failed[j].block->hash)) {
					/* if the block is not filled with 0, we are sure to have */
					/* restored it to the state after the 'sync' */
					/* instead, if the block is filled with 0, it could be either that the */
					/* block after the sync is really filled by 0, or that */
					/* we restored the block before the 'sync'. */
					if (memcmp(buffer[failed[j].index], buffer_zero, state->block_size) == 0) {
						/* it may contain garbage */
						failed[j].is_outofdate = 1;

						log_tag("hash_unknown: Maybe old zero on entry %u\n", j);
					}
				} else {
					/* if the hash is different than the previous one, we are sure to have */
					/* restored it to the state after the 'sync' */
					/* instead, if the hash matches, it could be either that the */
					/* block after the sync has this hash, or that */
					/* we restored the block before the 'sync'. */
					unsigned pos_size = file_block_size(failed[j].file, failed[j].file_pos, state->block_size);
					if (blockcmp(state, rehash, failed[j].block, pos_size, buffer[failed[j].index], buffer_zero) == 0) {
						/* it may contain garbage */
						failed[j].is_outofdate = 1;

						log_tag("hash_unknown: Maybe old data on entry %u\n", j);
					}
				}
			}
		}

		return 0;
	}
	if (ret > 0)
		error += ret;

	if (ret < 0)
		log_tag("recover_sync:%u:%u: Failed with no attempts\n", pos, n);
	else
		log_tag("recover_sync:%u:%u: Failed with %d attempts\n", pos, n, ret);

	/* Now assume that the parity IS NOT updated at the current state, */
	/* but still represent the state before the last 'sync' process. */
	/* In this case, parity contains info from BLK, REP (old version), CHG (old version) and DELETED blocks, */
	/* but not for REP (new version) and CHG (new version). */
	/* We are interested to recover BLK ones marked as bad, */
	/* but we are not interested to recover CHG (new version) and REP (new version) blocks, */
	/* even if marked as bad, because we don't have parity for them and it's just impossible, */
	/* and we are not interested to recover DELETED ones. */
	n = 0;
	something_to_recover = 0; /* keep track if there is at least one block to fix */
	something_unsynced = 0; /* keep track if we have some unsynced info to process */
	for (j = 0; j < failed_count; ++j) {
		unsigned block_state = block_state_get(failed[j].block);

		if (block_state == BLOCK_STATE_DELETED
			|| block_state == BLOCK_STATE_CHG
			|| block_state == BLOCK_STATE_REP
		) {
			/* If the block is CHG, REP or DELETED, we don't have the original content of block, */
			/* and we must try to recover it. */
			/* This apply to CHG and REP blocks even if they are not marked bad, */
			/* because the parity is computed with old content, and not with the new one. */
			/* Note that this recovering is done just to make possible to recover any other BLK one, */
			/* we are not really interested in DELETED, CHG (old version) and REP (old version). */
			something_unsynced = 1;

			if (block_state == BLOCK_STATE_CHG
				&& hash_is_zero(failed[j].block->hash)
			) {
				/* If the block was a ZERO block, restore it to the original 0 as before the 'sync' */
				/* We do this to just allow recovering of other BLK ones */

				memset(buffer[failed[j].index], 0, state->block_size);
				/* note that from now the buffer is definitively lost */
				/* we can do this only because it's the last retry of recovering */

				/* try to fetch the old block using the old hash for CHG and DELETED blocks */
			} else if ((block_state == BLOCK_STATE_CHG || block_state == BLOCK_STATE_DELETED)
				&& hash_is_unique(failed[j].block->hash)
				&& state_import_fetch(state, rehash, failed[j].block, buffer[failed[j].index]) == 0) {

				/* note that from now the buffer is definitively lost */
				/* we can do this only because it's the last retry of recovering */
			} else {
				/* otherwise try to recover it */
				failed_map[n] = j;
				++n;

				/* note that we don't set something_to_recover, because we are */
				/* not really interested to recover *only* old blocks. */
			}

			/* avoid to use the hash of this block to verify the recovering */
			/* this applies to REP blocks because we are going to recover the old state */
			/* and the REP hash represent the new one */
			/* it also applies to CHG and DELETE blocks because we want to have */
			/* a successful recovering only if a BLK one is matching */
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
	/* if nothing unsynced we also don't retry, because it's the same try as before */
	if (something_to_recover && something_unsynced) {
		ret = repair_step(state, rehash, pos, diskmax, failed, failed_map, n, buffer, buffer_recov, buffer_zero);
		if (ret == 0) {
			/* reprocess the REP and CHG blocks, for which we have recovered and old state */
			/* that we don't want to save into disk */
			/* we have already marked them, but we redo it for logging */

			for (j = 0; j < failed_count; ++j) {
				/* we take care only of BAD blocks we have to write back */
				if (failed[j].is_bad) {
					unsigned block_state = block_state_get(failed[j].block);

					if (block_state == BLOCK_STATE_CHG
						|| block_state == BLOCK_STATE_REP
					) {
						/* mark that we have restored an old state */
						/* and we don't want to write it to the disk */
						failed[j].is_outofdate = 1;

						log_tag("hash_unknown: Surely old data on entry %u\n", j);
					}
				}
			}

			return 0;
		}
		if (ret > 0)
			error += ret;

		if (ret < 0)
			log_tag("recover_unsync:%u:%u: Failed with no attempts\n", pos, n);
		else
			log_tag("recover_unsync:%u:%u: Failed with %d attempts\n", pos, n, ret);
	} else {
		log_tag("recover_unsync:%u:%u: Skipped for%s%s\n", pos, n,
			!something_to_recover ? " nothing to recover" : "",
			!something_unsynced ? " nothing unsynched" : ""
		);
	}

	/* return the number of failed attempts, or -1 if no strategy */
	if (error)
		return error;
	else
		return -1;
}

/**
 * Post process all the files at the specified block index ::i.
 * For each file, if we are at the last block, closes it,
 * adjust the timestamp, and print the result.
 *
 * This works with the assumption to always process the whole files to
 * fix. This assumption is not always correct, and in such case we have to
 * skip the whole postprocessing. And example, is when fixing only bad blocks.
 */
static int file_post(struct snapraid_state* state, int fix, unsigned i, struct snapraid_handle* handle, unsigned diskmax)
{
	unsigned j;
	int ret;
	char esc_buffer[ESC_MAX];
	char esc_buffer_alt[ESC_MAX];

	/* if we are processing only bad blocks, we don't have to do any post-processing */
	/* as we don't have any guarantee to process the last block of the fixed files */
	if (state->opt.badonly)
		return 0;

	/* for all the files print the final status, and does the final time fix */
	/* we also ensure to close files after processing the last block */
	for (j = 0; j < diskmax; ++j) {
		struct snapraid_block* block;
		struct snapraid_disk* disk;
		struct snapraid_file* collide_file;
		struct snapraid_file* file;
		block_off_t file_pos;
		char path[PATH_MAX];
		uint64_t inode;

		disk = handle[j].disk;
		if (!disk) {
			/* if no disk, nothing to do */
			continue;
		}

		block = fs_par2block_find(disk, i);
		if (!block_has_file(block)) {
			/* if no file, nothing to do */
			continue;
		}

		file = fs_par2file_get(disk, i, &file_pos);
		pathprint(path, sizeof(path), "%s%s", disk->dir, file->sub);

		/* if it isn't the last block in the file */
		if (!file_block_is_last(file, file_pos)) {
			/* nothing to do */
			continue;
		}

		/* if the file is excluded, we have nothing to adjust as the file is never written */
		if (file_flag_has(file, FILE_IS_EXCLUDED)
			|| (state->opt.syncedonly && file_flag_has(file, FILE_IS_UNSYNCED))) {
			/* nothing to do, but close the file */
			goto close_and_continue;
		}

		/* finish the fix process if it's the last block of the files */
		if (fix) {
			/* mark that we finished with this file */
			/* to identify later any NOT finished ones */
			file_flag_set(file, FILE_IS_FINISHED);

			/* if the file is damaged, meaning that a fix failed */
			if (file_flag_has(file, FILE_IS_DAMAGED)) {
				/* rename it to .unrecoverable */
				char path_to[PATH_MAX];

				pathprint(path_to, sizeof(path_to), "%s%s.unrecoverable", disk->dir, file->sub);

				/* ensure to close the file before renaming */
				if (handle[j].file == file) {
					ret = handle_close(&handle[j]);
					if (ret != 0) {
						/* LCOV_EXCL_START */
						log_tag("error:%u:%s:%s: Close error. %s\n", i, disk->name, esc_tag(file->sub, esc_buffer), strerror(errno));
						log_fatal("DANGER! Unexpected close error in a data disk.\n");
						return -1;
						/* LCOV_EXCL_STOP */
					}
				}

				ret = rename(path, path_to);
				if (ret != 0) {
					/* LCOV_EXCL_START */
					log_fatal("Error renaming '%s' to '%s'. %s.\n", path, path_to, strerror(errno));
					log_fatal("WARNING! Without a working data disk, it isn't possible to fix errors on it.\n");
					return -1;
					/* LCOV_EXCL_STOP */
				}

				log_tag("status:unrecoverable:%s:%s\n", disk->name, esc_tag(file->sub, esc_buffer));
				msg_info("unrecoverable %s\n", fmt_term(disk, file->sub, esc_buffer));

				/* and do not set the time if damaged */
				goto close_and_continue;
			}

			/* if the file is not fixed, meaning that it is untouched */
			if (!file_flag_has(file, FILE_IS_FIXED)) {
				/* nothing to do, but close the file */
				goto close_and_continue;
			}

			/* if the file is closed or different than the one expected, reopen it */
			/* a different open file could happen when filtering for bad blocks */
			if (handle[j].file != file) {
				/* close a potential different file */
				ret = handle_close(&handle[j]);
				if (ret != 0) {
					/* LCOV_EXCL_START */
					log_tag("error:%u:%s:%s: Close error. %s\n", i, disk->name, esc_tag(handle[j].file->sub, esc_buffer), strerror(errno));
					log_fatal("DANGER! Unexpected close error in a data disk.\n");
					return -1;
					/* LCOV_EXCL_STOP */
				}

				/* reopen it as readonly, as to set the mtime readonly access it's enough */
				/* we know that the file exists because it has the FILE_IS_FIXED tag */
				ret = handle_open(&handle[j], file, state->file_mode, log_error, 0);
				if (ret != 0) {
					/* LCOV_EXCL_START */
					log_tag("error:%u:%s:%s: Open error. %s\n", i, disk->name, esc_tag(file->sub, esc_buffer), strerror(errno));
					log_fatal("WARNING! Without a working data disk, it isn't possible to fix errors on it.\n");
					return -1;
					/* LCOV_EXCL_STOP */
				}
			}

			log_tag("status:recovered:%s:%s\n", disk->name, esc_tag(file->sub, esc_buffer));
			msg_info("recovered %s\n", fmt_term(disk, file->sub, esc_buffer));

			inode = handle[j].st.st_ino;

			/* search for the corresponding inode */
			collide_file = tommy_hashdyn_search(&disk->inodeset, file_inode_compare_to_arg, &inode, file_inode_hash(inode));

			/* if the inode is already in the database and it refers at a different file name, */
			/* we can fix the file time ONLY if the time and size allow to differentiate */
			/* between the two files */

			/* for example, suppose we delete a bunch of files with all the same size and time, */
			/* when recreating them the inodes may be reused in a different order, */
			/* and at the next sync some files may have matching inode/size/time even if different name */
			/* not allowing sync to detect that the file is changed and not renamed */
			if (!collide_file /* if not in the database, there is no collision */
				|| strcmp(collide_file->sub, file->sub) == 0 /* if the name is the same, it's the right collision */
				|| collide_file->size != file->size /* if the size is different, the collision is identified */
				|| collide_file->mtime_sec != file->mtime_sec /* if the mtime is different, the collision is identified */
				|| collide_file->mtime_nsec != file->mtime_nsec /* same for mtime_nsec */
			) {
				/* set the original modification time */
				ret = handle_utime(&handle[j]);
				if (ret == -1) {
					/* LCOV_EXCL_START */
					/* mark the file as damaged */
					file_flag_set(file, FILE_IS_DAMAGED);
					log_fatal("WARNING! Without a working data disk, it isn't possible to fix errors on it.\n");
					return -1;
					/* LCOV_EXCL_STOP */
				}
			} else {
				log_tag("collision:%s:%s:%s: Not setting modification time to avoid inode collision\n", disk->name, esc_tag(file->sub, esc_buffer), esc_tag(collide_file->sub, esc_buffer_alt));
			}
		} else {
			/* we are not fixing, but only checking */
			/* print just the final status */
			if (file_flag_has(file, FILE_IS_DAMAGED)) {
				if (state->opt.auditonly) {
					log_tag("status:damaged:%s:%s\n", disk->name, esc_tag(file->sub, esc_buffer));
					msg_info("damaged %s\n", fmt_term(disk, file->sub, esc_buffer));
				} else {
					log_tag("status:unrecoverable:%s:%s\n", disk->name, esc_tag(file->sub, esc_buffer));
					msg_info("unrecoverable %s\n", fmt_term(disk, file->sub, esc_buffer));
				}
			} else if (file_flag_has(file, FILE_IS_FIXED)) {
				log_tag("status:recoverable:%s:%s\n", disk->name, esc_tag(file->sub, esc_buffer));
				msg_info("recoverable %s\n", fmt_term(disk, file->sub, esc_buffer));
			} else {
				/* we don't use msg_verbose() because it also goes into the log */
				if (msg_level >= MSG_VERBOSE) {
					log_tag("status:correct:%s:%s\n", disk->name, esc_tag(file->sub, esc_buffer));
					msg_info("correct %s\n", fmt_term(disk, file->sub, esc_buffer));
				}
			}
		}

close_and_continue:
		/* if the opened file is the correct one, close it */
		/* in case of excluded and fragmented files it's possible */
		/* that the opened file is not the current one */
		if (handle[j].file == file) {
			/* ensure to close the file just after finishing with it */
			/* to avoid to keep it open without any possible use */
			ret = handle_close(&handle[j]);
			if (ret != 0) {
				/* LCOV_EXCL_START */
				log_tag("error:%u:%s:%s: Close error. %s\n", i, disk->name, esc_tag(file->sub, esc_buffer), strerror(errno));
				log_fatal("DANGER! Unexpected close error in a data disk.\n");
				return -1;
				/* LCOV_EXCL_STOP */
			}
		}
	}

	return 0;
}

/**
 * Check if we have to process the specified block index ::i.
 */
static int block_is_enabled(struct snapraid_state* state, block_off_t i, struct snapraid_handle* handle, unsigned diskmax)
{
	snapraid_info info;
	unsigned j;
	unsigned l;

	/* get block specific info */
	info = info_get(&state->infoarr, i);

	/* if we filter for only bad blocks */
	if (state->opt.badonly) {
		/* skip if this is not bad */
		if (!info_get_bad(info))
			return 0;
	}

	/* now apply the filters */

	/* if a parity is not excluded, include all blocks, even unused ones */
	for (l = 0; l < state->level; ++l) {
		if (!state->parity[l].is_excluded_by_filter) {
			return 1;
		}
	}

	/* otherwise include only used blocks */
	for (j = 0; j < diskmax; ++j) {
		struct snapraid_block* block;

		/* if no disk, nothing to check */
		if (!handle[j].disk)
			continue;

		block = fs_par2block_find(handle[j].disk, i);

		/* try to recover all files, even the ones without hash */
		/* because in some cases we can recover also them */
		if (block_has_file(block)) {
			struct snapraid_file* file = fs_par2file_get(handle[j].disk, i, 0);
			if (!file_flag_has(file, FILE_IS_EXCLUDED)) { /* only if the file is not filtered out */
				return 1;
			}
		}
	}

	return 0;
}

static int state_check_process(struct snapraid_state* state, int fix, struct snapraid_parity_handle** parity, block_off_t blockstart, block_off_t blockmax)
{
	struct snapraid_handle* handle;
	unsigned diskmax;
	block_off_t i;
	unsigned j;
	void* buffer_alloc;
	void** buffer;
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
	unsigned l;
	char esc_buffer[ESC_MAX];
	char esc_buffer_alt[ESC_MAX];

	handle = handle_mapping(state, &diskmax);

	/* we need 1 * data + 2 * parity + 1 * zero */
	buffermax = diskmax + 2 * state->level + 1;

	buffer = malloc_nofail_vector_align(diskmax, buffermax, state->block_size, &buffer_alloc);
	if (!state->opt.skip_self)
		mtest_vector(buffermax, state->block_size, buffer);

	/* fill up the zero buffer */
	memset(buffer[buffermax - 1], 0, state->block_size);
	raid_zero(buffer[buffermax - 1]);

	failed = malloc_nofail(diskmax * sizeof(struct failed_struct));
	failed_map = malloc_nofail(diskmax * sizeof(unsigned));

	error = 0;
	unrecoverable_error = 0;
	recovered_error = 0;

	/* first count the number of blocks to process */
	countmax = 0;
	for (i = blockstart; i < blockmax; ++i) {
		if (!block_is_enabled(state, i, handle, diskmax))
			continue;
		++countmax;
	}

	/* check all the blocks in files */
	countsize = 0;
	countpos = 0;
	state_progress_begin(state, blockstart, blockmax, countmax);
	for (i = blockstart; i < blockmax; ++i) {
		unsigned failed_count;
		int valid_parity;
		int used_parity;
		snapraid_info info;
		int rehash;

		if (!block_is_enabled(state, i, handle, diskmax)) {
			/* post process the files */
			ret = file_post(state, fix, i, handle, diskmax);
			if (ret == -1) {
				/* LCOV_EXCL_START */
				log_fatal("Stopping at block %u\n", i);
				++unrecoverable_error;
				goto bail;
				/* LCOV_EXCL_STOP */
			}

			/* and now continue with the next block */
			continue;
		}

		/* If we have valid parity, and it makes sense to check its content. */
		/* If we already know that the parity is invalid, we just read the file */
		/* but we don't report parity errors */
		/* Note that with auditonly, we anyway skip the full parity check, */
		/* because we also don't read it at all */
		valid_parity = 1;

		/* If the parity is used by at least one file */
		used_parity = 0;

		/* keep track of the number of failed blocks */
		failed_count = 0;

		/* get block specific info */
		info = info_get(&state->infoarr, i);

		/* if we have to use the old hash */
		rehash = info_get_rehash(info);

		/* for each disk, process the block */
		for (j = 0; j < diskmax; ++j) {
			int read_size;
			unsigned char hash[HASH_MAX];
			struct snapraid_disk* disk;
			struct snapraid_block* block;
			struct snapraid_file* file;
			block_off_t file_pos;
			unsigned block_state;

			/* if the disk position is not used */
			disk = handle[j].disk;
			if (!disk) {
				/* use an empty block */
				memset(buffer[j], 0, state->block_size);
				continue;
			}

			/* if the disk block is not used */
			block = fs_par2block_find(disk, i);
			if (block == BLOCK_NULL) {
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
				/* follow */
			}

			/* if the block is DELETED */
			if (block_state == BLOCK_STATE_DELETED) {
				/* use an empty block */
				memset(buffer[j], 0, state->block_size);

				/* store it in the failed set, because potentially */
				/* the parity may be still computed with the previous content */
				failed[failed_count].is_bad = 0; /* note that is_bad==0 <=> file==0 */
				failed[failed_count].is_outofdate = 0;
				failed[failed_count].index = j;
				failed[failed_count].block = block;
				failed[failed_count].disk = disk;
				failed[failed_count].file = 0;
				failed[failed_count].file_pos = 0;
				failed[failed_count].handle = 0;
				++failed_count;
				continue;
			}

			/* here we are sure that the parity is used by a file */
			used_parity = 1;

			/* get the file of this block */
			file = fs_par2file_get(disk, i, &file_pos);

			/* if we are only hashing, we can skip excluded files and don't even read them */
			if (state->opt.auditonly && file_flag_has(file, FILE_IS_EXCLUDED)) {
				/* use an empty block */
				/* in true, this is unnecessary, because we are not checking any parity */
				/* but we keep it for completeness */
				memset(buffer[j], 0, state->block_size);
				continue;
			}

			/* if the file is closed or different than the current one */
			if (handle[j].file == 0 || handle[j].file != file) {
				/* close the old one, if any */
				ret = handle_close(&handle[j]);
				if (ret == -1) {
					/* LCOV_EXCL_START */
					log_tag("error:%u:%s:%s: Close error. %s\n", i, disk->name, esc_tag(handle[j].file->sub, esc_buffer), strerror(errno));
					log_fatal("DANGER! Unexpected close error in a data disk.\n");
					log_fatal("Stopping at block %u\n", i);
					++unrecoverable_error;
					goto bail;
					/* LCOV_EXCL_STOP */
				}

				/* if fixing, and the file is not excluded, we must open for writing */
				if (fix && !file_flag_has(file, FILE_IS_EXCLUDED)) {
					/* if fixing, create the file, open for writing and resize if required */
					ret = handle_create(&handle[j], file, state->file_mode);
					if (ret == -1) {
						/* LCOV_EXCL_START */
						if (errno == EACCES) {
							log_fatal("WARNING! Please give write permission to the file.\n");
						} else {
							log_fatal("DANGER! Without a working data disk, it isn't possible to fix errors on it.\n");
						}
						log_fatal("Stopping at block %u\n", i);
						++unrecoverable_error;
						goto bail;
						/* LCOV_EXCL_STOP */
					}

					/* check if the file was just created */
					if (handle[j].created != 0) {
						/* if fragmented, it may be reopened, so remember that the file */
						/* was originally missing */
						file_flag_set(file, FILE_IS_CREATED);
					}
				} else {
					/* open the file only for reading */
					if (!file_flag_has(file, FILE_IS_MISSING))
						ret = handle_open(&handle[j], file, state->file_mode,
							log_error, state->opt.expected_missing ? log_expected : 0);
					else
						ret = -1; /* if the file is missing, we cannot open it */
					if (ret == -1) {
						/* save the failed block for the check/fix */
						failed[failed_count].is_bad = 1;
						failed[failed_count].is_outofdate = 0;
						failed[failed_count].index = j;
						failed[failed_count].block = block;
						failed[failed_count].disk = disk;
						failed[failed_count].file = file;
						failed[failed_count].file_pos = file_pos;
						failed[failed_count].handle = &handle[j];
						++failed_count;

						log_tag("error:%u:%s:%s: Open error at position %u\n", i, disk->name, esc_tag(file->sub, esc_buffer), file_pos);
						++error;

						/* mark the file as missing, to avoid to retry to open it again */
						/* note that this can be done only if we are not fixing it */
						/* otherwise, it could be recreated */
						file_flag_set(file, FILE_IS_MISSING);
						continue;
					}
				}

				/* if it's the first open, and not excluded */
				if (!file_flag_has(file, FILE_IS_OPENED)
					&& !file_flag_has(file, FILE_IS_EXCLUDED)) {

					/* check if the file is changed */
					if (handle[j].st.st_size != file->size
						|| handle[j].st.st_mtime != file->mtime_sec
						|| STAT_NSEC(&handle[j].st) != file->mtime_nsec
					        /* don't check the inode to support file-system without persistent inodes */
					) {
						/* report that the file is not synced */
						file_flag_set(file, FILE_IS_UNSYNCED);
					}
				}

				/* if it's the first open, and not excluded and larger */
				if (!file_flag_has(file, FILE_IS_OPENED)
					&& !file_flag_has(file, FILE_IS_EXCLUDED)
					&& !(state->opt.syncedonly && file_flag_has(file, FILE_IS_UNSYNCED))
					&& handle[j].st.st_size > file->size
				) {
					log_error("File '%s' is larger than expected.\n", handle[j].path);
					log_tag("error:%u:%s:%s: Size error\n", i, disk->name, esc_tag(file->sub, esc_buffer));
					++error;

					if (fix) {
						ret = handle_truncate(&handle[j], file);
						if (ret == -1) {
							/* LCOV_EXCL_START */
							log_fatal("DANGER! Unexpected truncate error in a data disk, it isn't possible to fix.\n");
							log_fatal("Stopping at block %u\n", i);
							++unrecoverable_error;
							goto bail;
							/* LCOV_EXCL_STOP */
						}

						log_tag("fixed:%u:%s:%s: Fixed size\n", i, disk->name, esc_tag(file->sub, esc_buffer));
						++recovered_error;
					}
				}

				/* mark the file as opened at least one time */
				/* this is used to avoid to check the unsynced and size */
				/* more than one time, in case the file is reopened later */
				file_flag_set(file, FILE_IS_OPENED);
			}

			/* read from the file */
			read_size = handle_read(&handle[j], file_pos, buffer[j], state->block_size,
				log_error, state->opt.expected_missing ? log_expected : 0);
			if (read_size == -1) {
				/* save the failed block for the check/fix */
				failed[failed_count].is_bad = 1; /* it's bad because we cannot read it */
				failed[failed_count].is_outofdate = 0;
				failed[failed_count].index = j;
				failed[failed_count].block = block;
				failed[failed_count].disk = disk;
				failed[failed_count].file = file;
				failed[failed_count].file_pos = file_pos;
				failed[failed_count].handle = &handle[j];
				++failed_count;

				log_tag("error:%u:%s:%s: Read error at position %u\n", i, disk->name, esc_tag(file->sub, esc_buffer), file_pos);
				++error;
				continue;
			}

			countsize += read_size;

			/* always insert CHG blocks, the repair functions needs all of them */
			/* because the parity may be still referring at the old state */
			/* and the repair must be aware of it */
			if (block_state == BLOCK_STATE_CHG) {
				/* we DO NOT mark them as bad to avoid to overwrite them with wrong data. */
				/* if we don't have a hash, we always assume the first read of the block correct. */
				failed[failed_count].is_bad = 0; /* we assume the CHG block correct */
				failed[failed_count].is_outofdate = 0;
				failed[failed_count].index = j;
				failed[failed_count].block = block;
				failed[failed_count].disk = disk;
				failed[failed_count].file = file;
				failed[failed_count].file_pos = file_pos;
				failed[failed_count].handle = &handle[j];
				++failed_count;
				continue;
			}

			assert(block_state == BLOCK_STATE_BLK || block_state == BLOCK_STATE_REP);

			/* compute the hash of the block just read */
			if (rehash) {
				memhash(state->prevhash, state->prevhashseed, hash, buffer[j], read_size);
			} else {
				memhash(state->hash, state->hashseed, hash, buffer[j], read_size);
			}

			/* compare the hash */
			if (memcmp(hash, block->hash, BLOCK_HASH_SIZE) != 0) {
				unsigned diff = memdiff(hash, block->hash, BLOCK_HASH_SIZE);

				/* save the failed block for the check/fix */
				failed[failed_count].is_bad = 1; /* it's bad because the hash doesn't match */
				failed[failed_count].is_outofdate = 0;
				failed[failed_count].index = j;
				failed[failed_count].block = block;
				failed[failed_count].disk = disk;
				failed[failed_count].file = file;
				failed[failed_count].file_pos = file_pos;
				failed[failed_count].handle = &handle[j];
				++failed_count;

				log_tag("error:%u:%s:%s: Data error at position %u, diff bits %u/%u\n", i, disk->name, esc_tag(file->sub, esc_buffer), file_pos, diff, BLOCK_HASH_SIZE * 8);
				++error;
				continue;
			}

			/* always insert REP blocks, the repair functions needs all of them */
			/* because the parity may be still referring at the old state */
			/* and the repair must be aware of it */
			if (block_state == BLOCK_STATE_REP) {
				failed[failed_count].is_bad = 0; /* it's not bad */
				failed[failed_count].is_outofdate = 0;
				failed[failed_count].index = j;
				failed[failed_count].block = block;
				failed[failed_count].disk = disk;
				failed[failed_count].file = file;
				failed[failed_count].file_pos = file_pos;
				failed[failed_count].handle = &handle[j];
				++failed_count;
				continue;
			}
		}

		/* now read and check the parity if requested */
		if (!state->opt.auditonly) {
			void* buffer_recov[LEV_MAX];
			void* buffer_zero;

			/* buffers for parity read and not computed */
			for (l = 0; l < state->level; ++l)
				buffer_recov[l] = buffer[diskmax + state->level + l];
			for (; l < LEV_MAX; ++l)
				buffer_recov[l] = 0;

			/* the zero buffer is the last one */
			buffer_zero = buffer[buffermax - 1];

			/* read the parity */
			for (l = 0; l < state->level; ++l) {
				if (parity[l]) {
					ret = parity_read(parity[l], i, buffer_recov[l], state->block_size, log_error);
					if (ret == -1) {
						buffer_recov[l] = 0; /* no parity to use */

						log_tag("parity_error:%u:%s: Read error\n", i, lev_config_name(l));
						++error;
					}
				} else {
					buffer_recov[l] = 0;
				}
			}

			/* try all the recovering strategies */
			ret = repair(state, rehash, i, diskmax, failed, failed_map, failed_count, buffer, buffer_recov, buffer_zero);
			if (ret != 0) {
				/* increment the number of errors */
				if (ret > 0)
					error += ret;
				++unrecoverable_error;

				/* print a list of all the errors in files */
				for (j = 0; j < failed_count; ++j) {
					if (failed[j].is_bad)
						log_tag("unrecoverable:%u:%s:%s: Unrecoverable error at position %u\n", i, failed[j].disk->name, esc_tag(failed[j].file->sub, esc_buffer), failed[j].file_pos);
				}

				/* keep track of damaged files */
				for (j = 0; j < failed_count; ++j) {
					if (failed[j].is_bad)
						file_flag_set(failed[j].file, FILE_IS_DAMAGED);
				}
			} else {
				/* now counts partial recovers */
				/* note that this could happen only when we have an incomplete 'sync' */
				/* and that we have recovered is the state before the 'sync' */
				int partial_recover_error = 0;

				/* print a list of all the errors in files */
				for (j = 0; j < failed_count; ++j) {
					if (failed[j].is_bad && failed[j].is_outofdate) {
						++partial_recover_error;
						log_tag("unrecoverable:%u:%s:%s: Unrecoverable unsynced error at position %u\n", i, failed[j].disk->name, esc_tag(failed[j].file->sub, esc_buffer), failed[j].file_pos);
					}
				}
				if (partial_recover_error != 0) {
					error += partial_recover_error;
					++unrecoverable_error;
				}

				/*
				 * Check parities, but only if all the blocks have it computed and it's used.
				 *
				 * If you check/fix after a partial sync, it's OK to have parity errors
				 * on the blocks with invalid parity and doesn't make sense to try to fix it.
				 *
				 * It's also OK to have data errors on unused parity, because sync doesn't
				 * update it.
				 */
				if (used_parity && valid_parity) {
					/* check the parity */
					for (l = 0; l < state->level; ++l) {
						if (buffer_recov[l] != 0 && memcmp(buffer_recov[l], buffer[diskmax + l], state->block_size) != 0) {
							unsigned diff = memdiff(buffer_recov[l], buffer[diskmax + l], state->block_size);

							/* mark that the read parity is wrong, setting ptr to 0 */
							buffer_recov[l] = 0;

							log_tag("parity_error:%u:%s: Data error, diff bits %u/%u\n", i, lev_config_name(l), diff, state->block_size * 8);
							++error;
						}
					}
				}

				/* now write recovered files */
				if (fix) {
					/* update the fixed files */
					for (j = 0; j < failed_count; ++j) {
						/* nothing to do if it doesn't need recovering */
						if (!failed[j].is_bad)
							continue;

						/* do not fix if the file is excluded */
						if (file_flag_has(failed[j].file, FILE_IS_EXCLUDED)
							|| (state->opt.syncedonly && file_flag_has(failed[j].file, FILE_IS_UNSYNCED)))
							continue;

						ret = handle_write(failed[j].handle, failed[j].file_pos, buffer[failed[j].index], state->block_size);
						if (ret == -1) {
							/* LCOV_EXCL_START */
							/* mark the file as damaged */
							file_flag_set(failed[j].file, FILE_IS_DAMAGED);

							if (errno == EACCES) {
								log_fatal("WARNING! Please give write permission to the file.\n");
							} else {
								/* we do not use DANGER because it could be ENOSPC which is not always correctly reported */
								log_fatal("WARNING! Without a working data disk, it isn't possible to fix errors on it.\n");
							}
							log_fatal("Stopping at block %u\n", i);
							++unrecoverable_error;
							goto bail;
							/* LCOV_EXCL_STOP */
						}

						/* if we are not sure that the recovered content is uptodate */
						if (failed[j].is_outofdate) {
							/* mark the file as damaged */
							file_flag_set(failed[j].file, FILE_IS_DAMAGED);
							continue;
						}

						/* mark the file as containing some fixes */
						/* note that it could be also marked as damaged in other iterations */
						file_flag_set(failed[j].file, FILE_IS_FIXED);

						log_tag("fixed:%u:%s:%s: Fixed data error at position %u\n", i, failed[j].disk->name, esc_tag(failed[j].file->sub, esc_buffer), failed[j].file_pos);
						++recovered_error;
					}

					/*
					 * Update parity only if all the blocks have it computed and it's used.
					 *
					 * If you check/fix after a partial sync, you do not want to fix parity
					 * for blocks that are going to have it computed in the sync completion.
					 *
					 * For unused parity there is no need to write it, because when fixing
					 * we already have allocated space for it on parity file creation,
					 * and its content doesn't matter.
					 */
					if (used_parity && valid_parity) {
						/* update the parity */
						for (l = 0; l < state->level; ++l) {
							/* if the parity on disk is wrong */
							if (buffer_recov[l] == 0
							        /* and we have access at the parity */
								&& parity[l] != 0
							        /* and the parity is not excluded */
								&& !state->parity[l].is_excluded_by_filter
							) {
								ret = parity_write(parity[l], i, buffer[diskmax + l], state->block_size);
								if (ret == -1) {
									/* LCOV_EXCL_START */
									/* we do not use DANGER because it could be ENOSPC which is not always correctly reported */
									log_fatal("WARNING! Without a working %s disk, it isn't possible to fix errors on it.\n", lev_name(l));
									log_fatal("Stopping at block %u\n", i);
									++unrecoverable_error;
									goto bail;
									/* LCOV_EXCL_STOP */
								}

								log_tag("parity_fixed:%u:%s: Fixed data error\n", i, lev_config_name(l));
								++recovered_error;
							}
						}
					}
				} else {
					/* if we are not fixing, we just set the FIXED flag */
					/* meaning that we could fix this file if we try */
					for (j = 0; j < failed_count; ++j) {
						if (failed[j].is_bad) {
							file_flag_set(failed[j].file, FILE_IS_FIXED);
						}
					}
				}
			}
		} else {
			/* if we are not checking, we just set the DAMAGED flag */
			/* to report that the file is damaged, and we don't know if we can fix it */
			for (j = 0; j < failed_count; ++j) {
				if (failed[j].is_bad) {
					file_flag_set(failed[j].file, FILE_IS_DAMAGED);
				}
			}
		}

		/* post process the files */
		ret = file_post(state, fix, i, handle, diskmax);
		if (ret == -1) {
			/* LCOV_EXCL_START */
			log_fatal("Stopping at block %u\n", i);
			++unrecoverable_error;
			goto bail;
			/* LCOV_EXCL_STOP */
		}

		/* count the number of processed block */
		++countpos;

		/* progress */
		if (state_progress(state, 0, i, countpos, countmax, countsize)) {
			/* LCOV_EXCL_START */
			break;
			/* LCOV_EXCL_STOP */
		}
	}

	/* for each disk, recover empty files, symlinks and empty dirs */
	for (i = 0; i < diskmax; ++i) {
		tommy_node* node;
		struct snapraid_disk* disk;

		if (!handle[i].disk)
			continue;

		/* for each empty file in the disk */
		disk = handle[i].disk;
		node = disk->filelist;
		while (node) {
			char path[PATH_MAX];
			struct stat st;
			struct snapraid_file* file;
			int unsuccesful = 0;

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
				unsuccesful = 1;

				log_error("Error stating empty file '%s'. %s.\n", path, strerror(errno));
				log_tag("error:%s:%s: Empty file stat error\n", disk->name, esc_tag(file->sub, esc_buffer));
				++error;
			} else if (!S_ISREG(st.st_mode)) {
				unsuccesful = 1;

				log_tag("error:%s:%s: Empty file error for not regular file\n", disk->name, esc_tag(file->sub, esc_buffer));
				++error;
			} else if (st.st_size != 0) {
				unsuccesful = 1;

				log_tag("error:%s:%s: Empty file error for size '%" PRIu64 "'\n", disk->name, esc_tag(file->sub, esc_buffer), (uint64_t)st.st_size);
				++error;
			}

			if (fix && unsuccesful) {
				int f;

				/* create the ancestor directories */
				ret = mkancestor(path);
				if (ret != 0) {
					/* LCOV_EXCL_START */
					log_fatal("WARNING! Without a working data disk, it isn't possible to fix errors on it.\n");
					log_fatal("Stopping\n");
					++unrecoverable_error;
					goto bail;
					/* LCOV_EXCL_STOP */
				}

				/* create it */
				/* O_NOFOLLOW: do not follow links to ensure to open the real file */
				f = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY | O_NOFOLLOW, 0600);
				if (f == -1) {
					/* LCOV_EXCL_START */
					log_fatal("Error creating empty file '%s'. %s.\n", path, strerror(errno));
					if (errno == EACCES) {
						log_fatal("WARNING! Please give write permission to the file.\n");
					} else {
						/* we do not use DANGER because it could be ENOSPC which is not always correctly reported */
						log_fatal("WARNING! Without a working data disk, it isn't possible to fix errors on it.\n");
					}
					log_fatal("Stopping\n");
					++unrecoverable_error;
					goto bail;
					/* LCOV_EXCL_STOP */
				}

				/* set the original modification time */
				ret = fmtime(f, file->mtime_sec, file->mtime_nsec);
				if (ret != 0) {
					/* LCOV_EXCL_START */
					close(f);

					log_fatal("Error timing file '%s'. %s.\n", file->sub, strerror(errno));
					log_fatal("WARNING! Without a working data disk, it isn't possible to fix errors on it.\n");
					log_fatal("Stopping\n");
					++unrecoverable_error;
					goto bail;
					/* LCOV_EXCL_STOP */
				}

				/* close it */
				ret = close(f);
				if (ret != 0) {
					/* LCOV_EXCL_START */
					log_fatal("WARNING! Without a working data disk, it isn't possible to fix errors on it.\n");
					log_fatal("Stopping\n");
					++unrecoverable_error;
					goto bail;
					/* LCOV_EXCL_STOP */
				}

				log_tag("fixed:%s:%s: Fixed empty file\n", disk->name, esc_tag(file->sub, esc_buffer));
				++recovered_error;

				log_tag("status:recovered:%s:%s\n", disk->name, esc_tag(file->sub, esc_buffer));
				msg_info("recovered %s\n", fmt_term(disk, file->sub, esc_buffer));
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
			struct snapraid_link* slink;
			int unsuccesful = 0;
			int unrecoverable = 0;

			slink = node->data;
			node = node->next; /* next node */

			/* if excluded continue to the next one */
			if (link_flag_has(slink, FILE_IS_EXCLUDED)) {
				continue;
			}

			if (link_flag_has(slink, FILE_IS_HARDLINK)) {
				/* stat the link */
				pathprint(path, sizeof(path), "%s%s", disk->dir, slink->sub);
				ret = stat(path, &st);
				if (ret == -1) {
					unsuccesful = 1;

					log_error("Error stating hardlink '%s'. %s.\n", path, strerror(errno));
					log_tag("hardlink_error:%s:%s:%s: Hardlink stat error\n", disk->name, esc_tag(slink->sub, esc_buffer), esc_tag(slink->linkto, esc_buffer_alt));
					++error;
				} else if (!S_ISREG(st.st_mode)) {
					unsuccesful = 1;

					log_tag("hardlink_error:%s:%s:%s: Hardlink error for not regular file\n", disk->name, esc_tag(slink->sub, esc_buffer), esc_tag(slink->linkto, esc_buffer_alt));
					++error;
				}

				/* stat the "to" file */
				pathprint(pathto, sizeof(pathto), "%s%s", disk->dir, slink->linkto);
				ret = stat(pathto, &stto);
				if (ret == -1) {
					unsuccesful = 1;

					if (errno == ENOENT) {
						unrecoverable = 1;
						if (fix) {
							/* if the target doesn't exist, it's unrecoverable */
							/* because we cannot create an hardlink of a file that */
							/* doesn't exists */
							++unrecoverable_error;
						} else {
							/* but in check, we can assume that fixing will recover */
							/* such missing file, so we assume a less drastic error */
							++error;
						}
					}

					log_error("Error stating hardlink-to '%s'. %s.\n", pathto, strerror(errno));
					log_tag("hardlink_error:%s:%s:%s: Hardlink to stat error\n", disk->name, esc_tag(slink->sub, esc_buffer), esc_tag(slink->linkto, esc_buffer_alt));
					++error;
				} else if (!S_ISREG(stto.st_mode)) {
					unsuccesful = 1;

					log_tag("hardlink_error:%s:%s:%s: Hardlink-to error for not regular file\n", disk->name, esc_tag(slink->sub, esc_buffer), esc_tag(slink->linkto, esc_buffer_alt));
					++error;
				} else if (!unsuccesful && st.st_ino != stto.st_ino) {
					unsuccesful = 1;

					log_error("Mismatch hardlink '%s' and '%s'. Different inode.\n", path, pathto);
					log_tag("hardlink_error:%s:%s:%s: Hardlink mismatch for different inode\n", disk->name, esc_tag(slink->sub, esc_buffer), esc_tag(slink->linkto, esc_buffer_alt));
					++error;
				}
			} else {
				/* read the symlink */
				pathprint(path, sizeof(path), "%s%s", disk->dir, slink->sub);
				ret = readlink(path, linkto, sizeof(linkto));
				if (ret < 0) {
					unsuccesful = 1;

					log_error("Error reading symlink '%s'. %s.\n", path, strerror(errno));
					log_tag("symlink_error:%s:%s: Symlink read error\n", disk->name, esc_tag(slink->sub, esc_buffer));
					++error;
				} else if (ret >= PATH_MAX) {
					unsuccesful = 1;

					log_error("Error reading symlink '%s'. Symlink too long.\n", path);
					log_tag("symlink_error:%s:%s: Symlink read error\n", disk->name, esc_tag(slink->sub, esc_buffer));
					++error;
				} else {
					linkto[ret] = 0;

					if (strcmp(linkto, slink->linkto) != 0) {
						unsuccesful = 1;

						log_tag("symlink_error:%s:%s: Symlink data error '%s' instead of '%s'\n", disk->name, esc_tag(slink->sub, esc_buffer), linkto, slink->linkto);
						++error;
					}
				}
			}

			if (fix && unsuccesful && !unrecoverable) {
				/* create the ancestor directories */
				ret = mkancestor(path);
				if (ret != 0) {
					/* LCOV_EXCL_START */
					log_fatal("WARNING! Without a working data disk, it isn't possible to fix errors on it.\n");
					log_fatal("Stopping\n");
					++unrecoverable_error;
					goto bail;
					/* LCOV_EXCL_STOP */
				}

				/* if it exists, it must be deleted before recreating */
				ret = remove(path);
				if (ret != 0 && errno != ENOENT) {
					/* LCOV_EXCL_START */
					log_fatal("Error removing '%s'. %s.\n", path, strerror(errno));
					log_fatal("WARNING! Without a working data disk, it isn't possible to fix errors on it.\n");
					log_fatal("Stopping\n");
					++unrecoverable_error;
					goto bail;
					/* LCOV_EXCL_STOP */
				}

				/* create it */
				if (link_flag_has(slink, FILE_IS_HARDLINK)) {
					ret = hardlink(pathto, path);
					if (ret != 0) {
						/* LCOV_EXCL_START */
						log_fatal("Error writing hardlink '%s' to '%s'. %s.\n", path, pathto, strerror(errno));
						if (errno == EACCES) {
							log_fatal("WARNING! Please give write permission to the hardlink.\n");
						} else {
							/* we do not use DANGER because it could be ENOSPC which is not always correctly reported */
							log_fatal("WARNING! Without a working data disk, it isn't possible to fix errors on it.\n");
						}
						log_fatal("Stopping\n");
						++unrecoverable_error;
						goto bail;
						/* LCOV_EXCL_STOP */
					}

					log_tag("hardlink_fixed:%s:%s: Fixed hardlink error\n", disk->name, esc_tag(slink->sub, esc_buffer));
					++recovered_error;
				} else {
					ret = symlink(slink->linkto, path);
					if (ret != 0) {
						/* LCOV_EXCL_START */
						log_fatal("Error writing symlink '%s' to '%s'. %s.\n", path, slink->linkto, strerror(errno));
						if (errno == EACCES) {
							log_fatal("WARNING! Please give write permission to the symlink.\n");
						} else {
							/* we do not use DANGER because it could be ENOSPC which is not always correctly reported */
							log_fatal("WARNING! Without a working data disk, it isn't possible to fix errors on it.\n");
						}
						log_fatal("Stopping\n");
						++unrecoverable_error;
						goto bail;
						/* LCOV_EXCL_STOP */
					}

					log_tag("symlink_fixed:%s:%s: Fixed symlink error\n", disk->name, esc_tag(slink->sub, esc_buffer));
					++recovered_error;
				}

				log_tag("status:recovered:%s:%s\n", disk->name, esc_tag(slink->sub, esc_buffer));
				msg_info("recovered %s\n", fmt_term(disk, slink->sub, esc_buffer));
			}
		}

		/* for each dir in the disk */
		disk = handle[i].disk;
		node = disk->dirlist;
		while (node) {
			char path[PATH_MAX];
			struct stat st;
			struct snapraid_dir* dir;
			int unsuccesful = 0;

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
				unsuccesful = 1;

				log_error("Error stating dir '%s'. %s.\n", path, strerror(errno));
				log_tag("dir_error:%s:%s: Dir stat error\n", disk->name, esc_tag(dir->sub, esc_buffer));
				++error;
			} else if (!S_ISDIR(st.st_mode)) {
				unsuccesful = 1;

				log_tag("dir_error:%s:%s: Dir error for not directory\n", disk->name, esc_tag(dir->sub, esc_buffer));
				++error;
			}

			if (fix && unsuccesful) {
				/* create the ancestor directories */
				ret = mkancestor(path);
				if (ret != 0) {
					/* LCOV_EXCL_START */
					log_fatal("WARNING! Without a working data disk, it isn't possible to fix errors on it.\n");
					log_fatal("Stopping\n");
					++unrecoverable_error;
					goto bail;
					/* LCOV_EXCL_STOP */
				}

				/* create it */
				ret = mkdir(path, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
				if (ret != 0) {
					/* LCOV_EXCL_START */
					log_fatal("Error creating dir '%s'. %s.\n", path, strerror(errno));
					if (errno == EACCES) {
						log_fatal("WARNING! Please give write permission to the dir.\n");
					} else {
						/* we do not use DANGER because it could be ENOSPC which is not always correctly reported */
						log_fatal("WARNING! Without a working data disk, it isn't possible to fix errors on it.\n");
					}
					log_fatal("Stopping\n");
					++unrecoverable_error;
					goto bail;
					/* LCOV_EXCL_STOP */
				}

				log_tag("dir_fixed:%s:%s: Fixed dir error\n", disk->name, esc_tag(dir->sub, esc_buffer));
				++recovered_error;

				log_tag("status:recovered:%s:%s\n", disk->name, esc_tag(dir->sub, esc_buffer));
				msg_info("recovered %s\n", fmt_term(disk, dir->sub, esc_buffer));
			}
		}
	}

	state_progress_end(state, countpos, countmax, countsize);

bail:
	/* close all the files left open */
	for (j = 0; j < diskmax; ++j) {
		struct snapraid_file* file = handle[j].file;
		struct snapraid_disk* disk = handle[j].disk;
		ret = handle_close(&handle[j]);
		if (ret == -1) {
			/* LCOV_EXCL_START */
			log_tag("error:%u:%s:%s: Close error. %s\n", blockmax, disk->name, esc_tag(file->sub, esc_buffer), strerror(errno));
			log_fatal("DANGER! Unexpected close error in a data disk.\n");
			++unrecoverable_error;
			/* continue, as we are already exiting */
			/* LCOV_EXCL_STOP */
		}
	}

	/* remove all the files created from scratch that have not finished the processing */
	/* it happens only when aborting pressing Ctrl+C or other reason. */
	if (fix) {
		/* for each disk */
		for (i = 0; i < diskmax; ++i) {
			tommy_node* node;
			struct snapraid_disk* disk;

			if (!handle[i].disk)
				continue;

			/* for each file in the disk */
			disk = handle[i].disk;
			node = disk->filelist;
			while (node) {
				char path[PATH_MAX];
				struct snapraid_file* file;

				file = node->data;
				node = node->next; /* next node */

				/* if the file was not created, meaning that it was already existing */
				if (!file_flag_has(file, FILE_IS_CREATED)) {
					/* nothing to do */
					continue;
				}

				/* if processing was finished */
				if (file_flag_has(file, FILE_IS_FINISHED)) {
					/* nothing to do */
					continue;
				}

				/* if the file was originally missing, and processing not yet finished */
				/* we have to throw it away  to ensure that at the next run we will retry */
				/* to fix it, in case we select to undelete missing files */
				pathprint(path, sizeof(path), "%s%s", disk->dir, file->sub);

				ret = remove(path);
				if (ret != 0) {
					/* LCOV_EXCL_START */
					log_fatal("Error removing '%s'. %s.\n", path, strerror(errno));
					log_fatal("WARNING! Without a working data disk, it isn't possible to fix errors on it.\n");
					++unrecoverable_error;
					/* continue, as we are already exiting */
					/* LCOV_EXCL_STOP */
				}
			}
		}
	}

	if (error || recovered_error || unrecoverable_error) {
		msg_status("\n");
		msg_status("%8u errors\n", error);
		if (fix) {
			msg_status("%8u recovered errors\n", recovered_error);
		}
		if (unrecoverable_error) {
			msg_status("%8u UNRECOVERABLE errors\n", unrecoverable_error);
		} else {
			/* without checking, we don't know if they are really recoverable or not */
			if (!state->opt.auditonly)
				msg_status("%8u unrecoverable errors\n", unrecoverable_error);
			if (fix)
				msg_status("Everything OK\n");
		}
	} else {
		msg_status("Everything OK\n");
	}

	if (error && !fix)
		log_fatal("WARNING! There are errors!\n");
	if (unrecoverable_error)
		log_fatal("DANGER! There are unrecoverable errors!\n");

	log_tag("summary:error:%u\n", error);
	if (fix)
		log_tag("summary:error_recovered:%u\n", recovered_error);
	if (!state->opt.auditonly)
		log_tag("summary:error_unrecoverable:%u\n", unrecoverable_error);
	if (fix) {
		if (error + recovered_error + unrecoverable_error == 0)
			log_tag("summary:exit:ok\n");
		else if (unrecoverable_error == 0)
			log_tag("summary:exit:recovered\n");
		else
			log_tag("summary:exit:unrecoverable\n");
	} else if (!state->opt.auditonly) {
		if (error + unrecoverable_error == 0)
			log_tag("summary:exit:ok\n");
		else if (unrecoverable_error == 0)
			log_tag("summary:exit:recoverable\n");
		else
			log_tag("summary:exit:unrecoverable\n");
	} else { /* audit only */
		if (error == 0)
			log_tag("summary:exit:ok\n");
		else
			log_tag("summary:exit:error\n");
	}
	log_flush();

	free(failed);
	free(failed_map);
	free(handle);
	free(buffer_alloc);
	free(buffer);

	/* fail if some error are present after the run */
	if (fix) {
		if (state->opt.expect_unrecoverable) {
			if (unrecoverable_error == 0)
				return -1;
		} else {
			if (unrecoverable_error != 0)
				return -1;
		}
	} else {
		if (state->opt.expect_unrecoverable) {
			if (unrecoverable_error == 0)
				return -1;
		} else if (state->opt.expect_recoverable) {
			if (unrecoverable_error != 0 || error == 0)
				return -1;
		} else {
			if (error != 0 || unrecoverable_error != 0)
				return -1;
		}
	}

	return 0;
}

int state_check(struct snapraid_state* state, int fix, block_off_t blockstart, block_off_t blockcount)
{
	block_off_t blockmax;
	data_off_t size;
	int ret;
	struct snapraid_parity_handle parity[LEV_MAX];
	struct snapraid_parity_handle* parity_ptr[LEV_MAX];
	unsigned error;
	unsigned l;

	msg_progress("Initializing...\n");

	blockmax = parity_allocated_size(state);
	size = blockmax * (data_off_t)state->block_size;

	if (blockstart > blockmax) {
		/* LCOV_EXCL_START */
		log_fatal("Error in the specified starting block %u. It's bigger than the parity size %u.\n", blockstart, blockmax);
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* adjust the number of block to process */
	if (blockcount != 0 && blockstart + blockcount < blockmax) {
		blockmax = blockstart + blockcount;
	}

	if (fix) {
		/* if fixing, create the file and open for writing */
		/* if it fails, we cannot continue */
		for (l = 0; l < state->level; ++l) {
			/* skip parity disks that are not accessible */
			if (state->parity[l].skip_access) {
				parity_ptr[l] = 0;
				continue;
			}

			parity_ptr[l] = &parity[l];

			/* if the parity is excluded */
			if (state->parity[l].is_excluded_by_filter) {
				/* open for reading, and ignore error */
				ret = parity_open(parity_ptr[l], &state->parity[l], l, state->file_mode, state->block_size, state->opt.parity_limit_size);
				if (ret == -1) {
					/* continue anyway */
					parity_ptr[l] = 0;
				}
			} else {
				/* open for writing */
				ret = parity_create(parity_ptr[l], &state->parity[l], l, state->file_mode, state->block_size, state->opt.parity_limit_size);
				if (ret == -1) {
					/* LCOV_EXCL_START */
					log_fatal("WARNING! Without an accessible %s file, it isn't possible to fix any error.\n", lev_name(l));
					exit(EXIT_FAILURE);
					/* LCOV_EXCL_STOP */
				}

				ret = parity_chsize(parity_ptr[l], &state->parity[l], 0, size, state->block_size, state->opt.skip_fallocate, state->opt.skip_space_holder);
				if (ret == -1) {
					/* LCOV_EXCL_START */
					log_fatal("WARNING! Without an accessible %s file, it isn't possible to sync.\n", lev_name(l));
					exit(EXIT_FAILURE);
					/* LCOV_EXCL_STOP */
				}
			}
		}
	} else if (!state->opt.auditonly) {
		/* if checking, open the file for reading */
		/* it may fail if the file doesn't exist, in this case we continue to check the files */
		for (l = 0; l < state->level; ++l) {
			parity_ptr[l] = &parity[l];
			ret = parity_open(parity_ptr[l], &state->parity[l], l, state->file_mode, state->block_size, state->opt.parity_limit_size);
			if (ret == -1) {
				msg_status("No accessible %s file, only files will be checked.\n", lev_name(l));
				/* continue anyway */
				parity_ptr[l] = 0;
			}
		}
	} else {
		/* otherwise don't use any parity */
		for (l = 0; l < state->level; ++l)
			parity_ptr[l] = 0;
	}

	if (fix)
		msg_progress("Fixing...\n");
	else if (!state->opt.auditonly)
		msg_progress("Checking...\n");
	else
		msg_progress("Hashing...\n");

	error = 0;

	/* skip degenerated cases of empty parity, or skipping all */
	if (blockstart < blockmax) {
		ret = state_check_process(state, fix, parity_ptr, blockstart, blockmax);
		if (ret == -1) {
			/* LCOV_EXCL_START */
			++error;
			/* continue, as we are already exiting */
			/* LCOV_EXCL_STOP */
		}
	}

	/* try to close only if opened */
	for (l = 0; l < state->level; ++l) {
		if (parity_ptr[l]) {
			/* if fixing and not excluded, truncate parity not valid */
			if (fix && !state->parity[l].is_excluded_by_filter) {
				ret = parity_truncate(parity_ptr[l]);
				if (ret == -1) {
					/* LCOV_EXCL_START */
					log_fatal("DANGER! Unexpected truncate error in %s disk.\n", lev_name(l));
					++error;
					/* continue, as we are already exiting */
					/* LCOV_EXCL_STOP */
				}
			}

			ret = parity_close(parity_ptr[l]);
			if (ret == -1) {
				/* LCOV_EXCL_START */
				log_fatal("DANGER! Unexpected close error in %s disk.\n", lev_name(l));
				++error;
				/* continue, as we are already exiting */
				/* LCOV_EXCL_STOP */
			}
		}
	}

	/* abort if error are present */
	if (error != 0)
		return -1;
	return 0;
}

