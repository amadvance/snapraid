// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2011 Andrea Mazzoleni

#include "os/portable.h"

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

/*
 * SnapRAID check/fix model
 *
 * Check verifies the data described by the content file against the files
 * currently present on the data disks. For blocks with a current hash, the
 * data is verified directly by hashing it. Unless running in audit-only mode,
 * parity is also read and can be used both to recover unreadable or corrupted
 * data and to verify the stored parity itself.
 *
 * Fix uses the same verification and recovery logic as check, but writes
 * validated recovered data and parity back to disk. Recovery is deliberately
 * conservative: obtaining a solution from the RAID equations is not enough;
 * the result must also be validated independently before it can be accepted.
 *
 * Block state model
 * -----------------
 *
 * Check/fix must interpret block states differently because the content file
 * may describe an incomplete sync. In that case, the logical block state and
 * the physical parity on disk do not necessarily describe the same point in
 * time.
 *
 *   State      Stored hash        Current file       Physical parity
 *   ------------------------------------------------------------------------
 *   EMPTY      none               none               zero contribution
 *   BLK        CURRENT            present            synchronized
 *   CHG        OLD/ZERO/INVALID   present            may be OLD or CURRENT
 *   REP        NEW                present            may be OLD or NEW
 *   DELETED    OLD/INVALID        none               may be OLD or zero
 *
 * BLK has a hash for the data that should currently be present and normally
 * represents a synchronized contribution.
 *
 * REP also has a hash for the current data, so its current contents can be
 * verified. However, parity is still invalid because an interrupted sync may
 * have left physical parity representing either the old or the new block.
 *
 * CHG is different: its stored hash describes the OLD data, not the current
 * file contents. A readable CHG block therefore cannot be validated against
 * block->hash. Its current contents are kept as the best available copy, but
 * the old hash is useful when considering parity that may still represent the
 * state before sync.
 *
 * DELETED has no current file data. Its stored hash, when available, describes
 * the previous contribution that may still be present in physical parity.
 *
 * Interrupted sync recovery model
 * -------------------------------
 *
 * When CHG, REP or DELETED blocks are present, check/fix cannot know whether
 * an interrupted sync updated the physical parity before stopping. Recovery
 * therefore tries two different interpretations of the same parity.
 *
 * The first strategy assumes that parity represents the CURRENT state:
 *
 *   BLK       -> current data is represented
 *   CHG       -> current data is represented
 *   REP       -> new/current data is represented
 *   DELETED   -> contributes zero
 *
 * Under this assumption, readable current data can be used directly and only
 * blocks known to be bad or missing have to be reconstructed. BLK and REP
 * recovery can be validated with their stored current hashes. CHG recovery is
 * more subtle because only its OLD hash is available; a recovered CHG may
 * therefore remain ambiguous and be marked out-of-date.
 *
 * If that strategy cannot produce a validated recovery, a second strategy
 * assumes that parity still represents the state BEFORE the interrupted sync:
 *
 *   BLK       -> synchronized data is represented
 *   CHG       -> OLD data is represented
 *   REP       -> OLD data is represented
 *   DELETED   -> OLD data is represented
 *
 * Under this assumption, the current contents of CHG and REP cannot be used
 * as parity inputs even when they are perfectly readable. CHG, REP and
 * DELETED must instead be treated as unknown old contributions, reconstructed
 * only as needed to make recovery of a bad BLK possible.
 *
 * These reconstructed old contributions are auxiliary recovery state, not
 * current user data. They are marked out-of-date so they cannot validate the
 * recovery and so an old CHG/REP result is never promoted as known-good
 * current data.
 *
 * A CHG whose OLD hash is ZERO is a useful special case: its old contribution
 * is known exactly and can be supplied as an all-zero block without consuming
 * a RAID recovery equation. Similarly, if an old CHG or DELETED block can be
 * obtained from the import set using its stored OLD hash, that known old
 * contribution reduces the number of unknowns. REP cannot use this shortcut
 * because its stored hash describes the NEW data, not the old contribution.
 *
 * Recovery validation model
 * -------------------------
 *
 * Solving the RAID equations does not by itself prove that the reconstructed
 * data is correct. One of the parity blocks used for reconstruction may itself
 * be damaged, and after an interrupted sync different parity levels may not
 * necessarily provide the expected state.
 *
 * For F unknown data blocks, recovery therefore requires independent evidence:
 *
 * - If at least one recovered block has a trustworthy current hash, F parity
 *   equations are sufficient. The candidate recovery is accepted only if all
 *   available trustworthy hashes of recovered blocks match.
 *
 * - If no recovered block has a trustworthy hash, F+1 parity blocks are
 *   required. F parity blocks are used to solve the data equations and the
 *   remaining parity block is reserved as an independent check of the result.
 *
 * When more parity levels are available, repair_step() tries their different
 * combinations. Missing parity levels are skipped, and a candidate obtained
 * from a bad parity combination is rejected when its hash or independent
 * parity check fails.
 *
 * This distinction is important: a successful RAID decode is only a candidate
 * recovery. It becomes an accepted recovery only after independent validation.
 *
 * Parity validation model
 * -----------------------
 *
 * Physical parity is checked or rewritten only when it is both meaningful and
 * expected to be valid.
 *
 * used_parity means that at least one live data block uses the parity position.
 * Parity for a position containing no live data is irrelevant and sync is not
 * required to keep such unused parity updated.
 *
 * valid_parity means that no block at the position is CHG, REP or DELETED.
 * When one of these states is present, parity may legitimately differ from
 * parity recomputed from the current data because sync has not completed.
 * Such a difference must not be reported or "fixed" by check/fix; the next
 * sync is responsible for bringing that parity position to the current state.
 *
 * Parity is therefore compared and repaired only when:
 *
 *   used_parity && valid_parity
 *
 * A parity level excluded from fix is not necessarily useless. When accessible
 * it is still opened for reading because it can provide an additional recovery
 * equation or independent validation, while remaining protected from writes.
 *
 * Fix and quarantine model
 * ------------------------
 *
 * failed_struct distinguishes two different properties of a recovery entry.
 *
 * is_bad means that the current file block is missing, unreadable or does not
 * match its expected current hash and therefore needs recovery.
 *
 * is_outofdate means that the bytes currently associated with the entry cannot
 * be proven to represent the required current data. This is used both for the
 * auxiliary old CHG/REP/DELETED values of the pre-sync recovery strategy and
 * for ambiguous recovered CHG blocks.
 *
 * An out-of-date candidate may still be the best data available. Fix may write
 * such data, but the containing file remains marked damaged and is kept under
 * the .unrecoverable name instead of being reported as successfully recovered.
 *
 * Missing files are created as .unrecoverable from the beginning, and an
 * existing .unrecoverable file is reused by later fix runs. This allows a
 * recovery to proceed over multiple runs without exposing a partially rebuilt
 * file under its final name.
 *
 * Promotion to the final file name is a file-level commit operation. It happens
 * only after the complete file has been processed successfully, its metadata
 * has been restored, and no block remains damaged. A partial -S/-B fix operates
 * only at block level and therefore never performs this file-level promotion
 * or other finalization.
 */

/****************************************************************************/
/* check */

static const char* es(int err)
{
	if (is_hw(err))
		return "error_io";
	else
		return "error";
}

/**
 * A block participating in recovery.
 *
 * Entries include blocks that are actually missing or corrupted, but also
 * readable CHG, REP and DELETED blocks whose old/current contribution may be
 * needed to interpret parity left by an interrupted sync.
 */
struct failed_struct {
	/**
	 * If the current file block is known to require recovery because it is
	 * missing, unreadable, or doesn't match its expected current hash.
	 *
	 * DELETED blocks and readable CHG/REP auxiliary entries are not bad even
	 * though they may participate in the recovery equations.
	 */
	int is_bad;

	/**
	 * If the data associated with this entry cannot be proven to represent the
	 * required CURRENT contents.
	 *
	 * This does not mean that the data is known to be wrong or actually old.
	 * In particular, for a CHG block the stored hash describes the OLD contents,
	 * but the block itself may not have changed even if the file was detected as
	 * modified. Therefore data matching the OLD state may also be perfectly valid
	 * CURRENT data.
	 *
	 * In the pre-sync parity strategy this is set on CHG, REP and DELETED entries
	 * because their OLD contribution is being used or reconstructed as auxiliary
	 * recovery state. Such data cannot be assumed to represent the required
	 * CURRENT contents, even though in some cases OLD and CURRENT may actually be
	 * identical.
	 *
	 * It is also set on a bad CHG when the current-parity strategy recovers a
	 * candidate that cannot be distinguished reliably from its OLD contents.
	 *
	 * Out-of-date entries are excluded from hash validation. If an out-of-date
	 * block is also is_bad, fix may still preserve the recovered bytes as the best
	 * available copy, but the containing file remains damaged and quarantined as
	 * .unrecoverable.
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
static int blockcmp(struct snapraid_state* state, int rehash, struct snapraid_block* block, size_t pos_size, unsigned char* buffer, unsigned char* buffer_zero)
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

	/*
	 * The hash covers only the logical bytes belonging to the file, while RAID
	 * operates on the complete zero-padded block. Verify the padding separately:
	 * otherwise corruption confined to the reconstructed tail could pass the hash
	 * check and later produce incorrect parity.
	 */
	if (pos_size < state->block_size) {
		if (memcmp(buffer + pos_size, buffer_zero + pos_size, state->block_size - pos_size) != 0) {
			return -1;
		}
	}

	return 0;
}

/**
 * Check if the hash of all the failed blocks we are expecting to recover are now matching.
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
			size_t pos_size = file_block_size(failed[failed_map[j]].file, failed[failed_map[j]].file_pos, state->block_size);
			if (blockcmp(state, rehash, failed[failed_map[j]].block, pos_size, buffer[failed[failed_map[j]].index], buffer_zero) != 0) {
				log_tag("repair_hash_error:%u: Hash mismatch\n", failed_map[j]);
				return 0;
			}

			hash_checked = 1;
		}
	}

	/*
	 * If nothing checked, we reject it
	 * note that we are excluding this case at upper level
	 * but checking again doesn't hurt
	 */
	if (!hash_checked) {
		/* LCOV_EXCL_START */
		return 0;
		/* LCOV_EXCL_STOP */
	}

	/*
	 * If we checked something, and no block failed the check
	 * recompute all the redundancy information
	 */
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
 * Try to recover the selected failed inputs and independently validate the
 * reconstructed result.
 *
 * Return:
 *   0  if a recovery was successfully validated;
 *  >0  if one or more recovery candidates were tried but all failed validation;
 *  <0  if no usable recovery/validation strategy exists.
 *
 * On success all parity levels are recomputed in buffer.
 */
static int repair_step(struct snapraid_state* state, int rehash, block_off_t pos, unsigned diskmax, struct failed_struct* failed, unsigned* failed_map, unsigned failed_count, void** buffer, void** buffer_recov, void* buffer_zero)
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

	/* if failures exceed parity level, recovery is impossible */
	if (failed_count > n) {
		log_tag("recover_strategy_error:%" PRIu64 ": Impossible to recover from %u failures with %u parity\n",
			pos, failed_count, n);
		return -1;
	}

	/* setup vector of failed disk indexes */
	for (i = 0; i < failed_count; ++i)
		id[i] = failed[failed_map[i]].index;

	/*
	 * RAID decoding alone cannot tell whether the parity equations used for the
	 * recovery were themselves correct.
	 *
	 * If at least one failed input has a trustworthy CURRENT hash, recovered data
	 * can provide the independent validation. Otherwise one additional parity
	 * equation must be reserved exclusively for checking the candidate result.
	 */
	has_hash = 0;
	for (i = 0; i < failed_count; ++i) {
		/* if we are expected to recover this block */
		if (!failed[failed_map[i]].is_outofdate
		        /* if the block has a hash to check */
			&& block_has_updated_hash(failed[failed_map[i]].block)
		)
			has_hash = 1;
	}

	/*
	 * If we don't have a hash, but we have an extra parity
	 * (strictly-less failures than number of parities)
	 */
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
			log_tag("recover_parity_error:%" PRIu64 ":", pos);
			for (i = 0; i < r; ++i) {
				if (i != 0)
					log_tag(",");
				log_tag("%s", lev_config_name(ip[i]));
			}
			log_tag(": Parity mismatch\n");
			++error;
		} while (combination_next(r, n, ip));
	}

	/*
	 * If we have a hash, and enough parities
	 * (less-or-equal failures than number of parities)
	 */
	if (has_hash && failed_count <= n) {
		/* number of parities to use equal to the number of failures */
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
			log_tag("recover_hash_error:%" PRIu64 ":", pos);
			for (i = 0; i < r; ++i) {
				if (i != 0)
					log_tag("/");
				log_tag("%s", lev_config_name(ip[i]));
			}
			log_tag(": Hash mismatch\n");
			++error;
		} while (combination_next(r, n, ip));
	}

	/* return the number of failed attempts, or -1 if no strategy */
	if (error)
		return error;

	log_tag("recover_strategy_error:%" PRIu64 ": No strategy to recover from %u failures with %u parity %s hash\n",
		pos, failed_count, n, has_hash ? "with" : "without");
	return -1;
}

static int repair(struct snapraid_state* state, int rehash, block_off_t pos, unsigned diskmax, struct failed_struct* failed, unsigned* failed_map, unsigned failed_count, void** buffer, void** buffer_recov, void* buffer_zero)
{
	int ret;
	int error;
	unsigned j;
	int n;
	int something_to_recover;
	int something_unsynced;
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

		if (block == BLOCK_NULL) {
			hash = "none";
		} else if (hash_is_invalid(block->hash)) {
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

			log_tag("repair_entry:%u:%s:%s:%s:%s:%s:%" PRIu64 ":\n", j, desc, hash, data, disk->name, esc_tag(file->sub), file_pos);
		} else {
			log_tag("repair_entry:%u:%s:%s:%s:\n", j, desc, hash, data);
		}
	}

	/*
	 * Here we have to try two different strategies to recover, because in case the 'sync'
	 * process is aborted, we don't know if the parity data is really updated just like after 'sync',
	 * or if it still represents the state before the 'sync'.
	 */

	/*
	 * Note that if the 'sync' ends normally, we don't have any DELETED, REP and CHG blocks
	 * and the two strategies are identical
	 */

	/*
	 * As first, we assume that the parity IS updated for the current state
	 * and that we are going to recover the state after the last 'sync'.
	 * In this case, parity contains info from BLK, REP and CHG blocks,
	 * but not for DELETED.
	 * We need to put in the recovering process only the bad blocks, because all the
	 * others already contains the correct data read from disk, and the parity is correctly computed for them.
	 * We are interested to recover BLK, REP and CHG blocks if they are marked as bad,
	 * but we are not interested in DELETED ones.
	 */

	n = 0;
	something_to_recover = 0; /* keep track if there is at least one block to fix */
	for (j = 0; j < failed_count; ++j) {
		if (failed[j].is_bad) {
			unsigned block_state = block_state_get(failed[j].block);

			assert(block_state != BLOCK_STATE_DELETED); /* we cannot have bad DELETED blocks */

			/*
			 * Import/search can directly repair only states whose stored hash identifies
			 * the desired CURRENT contents. This is true for BLK and REP.
			 *
			 * A CHG hash identifies the OLD contents, so fetching data by that hash would
			 * not prove that we recovered the current version required by this strategy.
			 */
			if ((block_state == BLOCK_STATE_BLK || block_state == BLOCK_STATE_REP)
			        /* try to fetch the block using the known hash */
				&& (state_import_fetch(state, rehash, failed[j].block, buffer[failed[j].index]) == 0
				|| state_search_fetch(state, rehash, failed[j].file, failed[j].file_pos, failed[j].block, buffer[failed[j].index]) == 0)
			) {
				/* we already have corrected it! */
				log_tag("repair_hash_import:%u: Fixed by import\n", j);
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
		log_tag("recover_sync:%" PRIu64 ":%u: Skipped for already recovered\n", pos, n);

		/* recompute only the parity */
		raid_gen(diskmax, state->level, state->block_size, buffer);
		return 0;
	}

	ret = repair_step(state, rehash, pos, diskmax, failed, failed_map, n, buffer, buffer_recov, buffer_zero);
	if (ret == 0) {
		/*
		 * Reprocess the CHG blocks, for which we don't have a hash to check
		 * if they were BAD we have to use some heuristics to ensure that we have recovered
		 * the state after the sync. If unsure, we assume the worst case
		 */

		for (j = 0; j < failed_count; ++j) {
			/* we take care only of BAD blocks we have to write back */
			if (failed[j].is_bad) {
				unsigned block_state = block_state_get(failed[j].block);

				/*
				 * BLK and REP blocks are always OK, because at this point
				 * we have already checked their hash
				 */
				if (block_state != BLOCK_STATE_CHG) {
					assert(block_state == BLOCK_STATE_BLK || block_state == BLOCK_STATE_REP);
					continue;
				}

				/* for CHG blocks we have to 'guess' if they are correct or not */

				/*
				 * If the hash is invalid we cannot check the result
				 * this could happen if we have lost this information
				 * after an aborted sync
				 */
				if (hash_is_invalid(failed[j].block->hash)) {
					/* it may contain garbage */
					failed[j].is_outofdate = 1;

					log_tag("repair:hash_unknown:%u: Unknown hash\n", j);
				} else if (hash_is_zero(failed[j].block->hash)) {
					/*
					 * If the block is not filled with 0, we are sure to have
					 * restored it to the state after the 'sync'
					 * instead, if the block is filled with 0, it could be either that the
					 * block after the sync is really filled by 0, or that
					 * we restored the block before the 'sync'.
					 */
					if (memcmp(buffer[failed[j].index], buffer_zero, state->block_size) == 0) {
						/* it may contain garbage */
						failed[j].is_outofdate = 1;

						log_tag("repair_hash_unknown:%u: Maybe old zero\n", j);
					}
				} else {
					/*
					 * If the hash is different than the previous one, we are sure to have
					 * restored it to the state after the 'sync'
					 * instead, if the hash matches, it could be either that the
					 * block after the sync has this hash, or that
					 * we restored the block before the 'sync'.
					 */
					size_t pos_size = file_block_size(failed[j].file, failed[j].file_pos, state->block_size);
					if (blockcmp(state, rehash, failed[j].block, pos_size, buffer[failed[j].index], buffer_zero) == 0) {
						/* it may contain garbage */
						failed[j].is_outofdate = 1;

						log_tag("repair_hash_unknown:%u: Maybe old data\n", j);
					}
				}
			}
		}

		return 0;
	}
	if (ret > 0)
		error += ret;

	if (ret < 0)
		log_tag("recover_sync:%" PRIu64 ":%u: Failed with no attempts\n", pos, n);
	else
		log_tag("recover_sync:%" PRIu64 ":%u: Failed with %d attempts\n", pos, n, ret);

	/*
	 * Now assume that the parity IS NOT updated at the current state,
	 * but still represent the state before the last 'sync' process.
	 * In this case, parity contains info from BLK, REP (old version), CHG (old version) and DELETED blocks,
	 * but not for REP (new version) and CHG (new version).
	 * We are interested to recover BLK ones marked as bad,
	 * but we are not interested to recover CHG (new version) and REP (new version) blocks,
	 * even if marked as bad, because we don't have parity for them and it's just impossible,
	 * and we are not interested to recover DELETED ones.
	 */
	n = 0;
	something_to_recover = 0; /* keep track if there is at least one block to fix */
	something_unsynced = 0; /* keep track if we have some unsynced info to process */
	for (j = 0; j < failed_count; ++j) {
		unsigned block_state = block_state_get(failed[j].block);

		if (block_state == BLOCK_STATE_DELETED
			|| block_state == BLOCK_STATE_CHG
			|| block_state == BLOCK_STATE_REP
		) {
			/*
			 * Under the pre-sync hypothesis the readable buffer of a CHG or REP contains
			 * CURRENT data, while physical parity was generated from its OLD data.
			 *
			 * Therefore every CHG, REP and DELETED contribution must be replaced by its
			 * OLD value or treated as an unknown RAID input, even when the current file
			 * block is perfectly readable and is_bad is false.
			 *
			 * These old values are reconstructed only to make recovery of bad BLK blocks
			 * possible; recovering them is not itself a user-visible recovery goal.
			 */
			something_unsynced = 1;

			if (block_state == BLOCK_STATE_CHG
				&& hash_is_zero(failed[j].block->hash)
			) {
				/*
				 * If the block was a ZERO block, restore it to the original 0 as before the 'sync'
				 * We do this to just allow recovering of other BLK ones
				 */

				memset(buffer[failed[j].index], 0, state->block_size);
				/*
				 * Note that from now the buffer is definitely lost
				 * we can do this only because it's the last retry of recovering
				 */

				/*
				 * CHG and DELETED retain an OLD hash, so a matching imported block can supply
				 * their exact pre-sync contribution and remove one unknown from the RAID
				 * equations.
				 *
				 * REP cannot use this shortcut: its stored hash describes NEW data and gives
				 * no direct way to identify the OLD contribution required by this strategy.
				 */
			} else if ((block_state == BLOCK_STATE_CHG || block_state == BLOCK_STATE_DELETED)
				&& hash_is_unique(failed[j].block->hash)
				&& state_import_fetch(state, rehash, failed[j].block, buffer[failed[j].index]) == 0) {

				/*
				 * Note that from now the buffer is definitely lost
				 * we can do this only because it's the last retry of recovering
				 */
			} else {
				/* otherwise try to recover it */
				failed_map[n] = j;
				++n;

				/*
				 * Note that we don't set something_to_recover, because we are
				 * not really interested to recover *only* old blocks.
				 */
			}

			/*
			 * Avoid using the hash of this block to verify the recovering
			 * this applies to REP blocks because we are going to recover the old state
			 * and the REP hash represents the new one
			 * it also applies to CHG and DELETE blocks because we want to have
			 * a successful recovering only if a BLK one is matching
			 */
			failed[j].is_outofdate = 1;
		} else if (failed[j].is_bad) {
			/*
			 * If the block is bad we don't know its content, and we try to recover it
			 * At this point, we can have only BLK ones
			 */

			assert(block_state == BLOCK_STATE_BLK);

			/* we have something we are interested to recover */
			something_to_recover = 1;

			/* we try to recover it */
			failed_map[n] = j;
			++n;
		}
	}

	/*
	 * Retry only when both conditions hold.
	 *
	 * Without a bad BLK there is no useful current data to recover: reconstructing
	 * only OLD CHG/REP/DELETED values would have no user-visible purpose.
	 *
	 * Without any unsynced state this hypothesis is identical to the current-state
	 * attempt already performed above, so repeating it cannot provide a different
	 * solution.
	 */
	if (something_to_recover && something_unsynced) {
		ret = repair_step(state, rehash, pos, diskmax, failed, failed_map, n, buffer, buffer_recov, buffer_zero);
		if (ret == 0) {
			/*
			 * Reprocess the REP and CHG blocks, for which we have recovered an old state
			 * that we don't want to save into disk
			 * we have already marked them, but we redo it for logging
			 */

			for (j = 0; j < failed_count; ++j) {
				/* we take care only of BAD blocks we have to write back */
				if (failed[j].is_bad) {
					unsigned block_state = block_state_get(failed[j].block);

					if (block_state == BLOCK_STATE_CHG
						|| block_state == BLOCK_STATE_REP
					) {
						/*
						 * Mark that we have restored an old state
						 * and we don't want to write it to the disk
						 */
						failed[j].is_outofdate = 1;

						log_tag("repair_hash_unknown:%u: Surely old data\n", j);
					}
				}
			}

			return 0;
		}
		if (ret > 0)
			error += ret;

		if (ret < 0)
			log_tag("recover_unsync:%" PRIu64 ":%u: Failed with no attempts\n", pos, n);
		else
			log_tag("recover_unsync:%" PRIu64 ":%u: Failed with %d attempts\n", pos, n, ret);
	} else {
		log_tag("recover_unsync:%" PRIu64 ":%u: Skipped for%s%s\n", pos, n,
			!something_to_recover ? " nothing to recover" : "",
			!something_unsynced ? " nothing unsynced" : ""
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
 * For each file, if we are at the last block, close it,
 * adjust the timestamp, and print the result.
 *
 * This works only if the whole file is processed, including its last block.
 * This doesn't always happen, like with an explicit end block.
 *
 * In such case, the check/fix command won't report any information of the
 * files partially checked.
 */
static int file_post(struct snapraid_state* state, int fix, int partial, block_off_t i, struct snapraid_handle* handle, unsigned diskmax)
{
	unsigned j;
	int ret;

	/*
	 * For all the files print the final status, and do the final time fix
	 * we also ensure to close files after processing the last block
	 */
	for (j = 0; j < diskmax; ++j) {
		struct snapraid_block* block;
		struct snapraid_disk* disk;
		struct snapraid_file* collide_file;
		struct snapraid_file* file;
		block_off_t file_pos;

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
			int was_unrecoverable;

			if (handle[j].file != file) {
				/* LCOV_EXCL_START */
				log_fatal(EINTERNAL, "Internal inconsistency in file handle '%s' instead of '%s'\n",
					handle[j].file != 0 ? handle[j].file->sub : "<none>", file->sub);
				os_abort();
				/* LCOV_EXCL_STOP */
			}
			was_unrecoverable = handle[j].is_unrecoverable;

			/*
			 * A partial fix operates only at block level. Reaching the last block of a
			 * file doesn't imply that the whole file was processed, so don't perform any
			 * file-level finalization, including marking it finished, promoting an
			 * .unrecoverable file, reporting it recovered, or restoring its timestamp.
			 * This is intentional also when an explicit block range happens to cover all
			 * the parity blocks, to keep -S/-B behavior independent of the parity size.
			 */
			if (partial)
				goto close_and_continue;

			/*
			 * Mark that we finished with this file
			 * to identify later any NOT finished ones
			 */
			file_flag_set(file, FILE_IS_FINISHED);

			/* if the file is damaged, meaning that a fix failed */
			if (file_flag_has(file, FILE_IS_DAMAGED)) {
				char path[PATH_MAX];
				char path_to[PATH_MAX];

				pathprint(path, sizeof(path), "%s%s", disk->dir, file->sub);
				pathprint(path_to, sizeof(path_to), "%s%s.unrecoverable", disk->dir, file->sub);

				/* ensure to close the file before renaming */
				ret = handle_close(&handle[j]);
				if (ret != 0) {
					/* LCOV_EXCL_START */
					log_tag("%s:%" PRIu64 ":%s:%s: Close error. %s.\n", es(errno), i, disk->name, esc_tag(file->sub), strerror(errno));
					log_fatal_errno(errno, disk->name);
					return -1;
					/* LCOV_EXCL_STOP */
				}

				/* a previous recovery is already quarantined */
				if (!was_unrecoverable) {
					ret = rename(path, path_to);
					if (ret != 0) {
						/* LCOV_EXCL_START */
						log_fatal(errno, "Error renaming '%s%s'. %s.\n", disk->dir, file->sub, strerror(errno));
						log_tag("%s:%" PRIu64 ":%s:%s: Rename error. %s.\n", es(errno), i, disk->name, esc_tag(file->sub), strerror(errno));
						log_fatal_errno(errno, disk->name);
						return -1;
						/* LCOV_EXCL_STOP */
					}
				}

				log_tag("status:unrecoverable:%s:%s\n", disk->name, esc_tag(file->sub));
				msg_info("unrecoverable %s\n", fmt_term(disk, file->sub));

				/* and do not set the time if damaged */
				goto close_and_continue;
			}

			/* if the file is not fixed, meaning that it is untouched */
			if (!file_flag_has(file, FILE_IS_FIXED) && !was_unrecoverable) {
				/* nothing to do, but close the file */
				goto close_and_continue;
			}

			/*
			 * If the file is closed or different than the one expected, reopen it
			 * a different open file could happen when filtering for bad blocks
			 */
			if (handle[j].file != file) {
				/* keep a pointer at the file we are going to close for error reporting */
				struct snapraid_file* report = handle[j].file;
				ret = handle_close(&handle[j]);
				if (ret != 0) {
					/* LCOV_EXCL_START */
					log_tag("%s:%" PRIu64 ":%s:%s: Close error. %s.\n", es(errno), i, disk->name, esc_tag(report->sub), strerror(errno));
					log_fatal_errno(errno, disk->name);
					return -1;
					/* LCOV_EXCL_STOP */
				}

				/* reopen the file for writing, as required to set the mtime on Windows */
				ret = handle_create(&handle[j], file, state->file_mode);
				if (ret != 0) {
					/* LCOV_EXCL_START */
					log_tag("%s:%" PRIu64 ":%s:%s: Open error. %s.\n", es(errno), i, disk->name, esc_tag(file->sub), strerror(errno));
					log_fatal_errno(errno, disk->name);
					return -1;
					/* LCOV_EXCL_STOP */
				}
			}

			/* search for the corresponding inode */
			uint64_t inode = handle[j].st.st_ino; /* don't know the exact type of st_ino and we cannot pass it by pointer in the search */
			if (inode != INODE_INVALID)
				collide_file = tommy_hashdyn_search(&disk->inodeset, file_inode_compare_to_arg, &inode, file_inode_hash(inode));
			else
				collide_file = 0;

			/*
			 * If the inode is already in the database and it refers to a different file name,
			 * we can fix the file time ONLY if the time and size allow to differentiate
			 * between the two files
			 *
			 * For example, suppose we delete a bunch of files with all the same size and time,
			 * when recreating them the inodes may be reused in a different order,
			 * and at the next sync some files may have matching inode/size/time even if different name
			 * not allowing sync to detect that the file is changed and not renamed
			 */
			if (!collide_file /* if not in the database, there is no collision */
				|| strcmp(collide_file->sub, file->sub) == 0 /* if the name is the same, it's the right collision */
				|| collide_file->size != file->size /* if the size is different, the collision is identified */
				|| collide_file->mtime_sec != file->mtime_sec /* if the mtime is different, the collision is identified */
				|| collide_file->mtime_nsec != file->mtime_nsec /* same for mtime_nsec */
			) {
				/* set the original modification time before promoting/renaming */
				ret = handle_utime(&handle[j]);
				if (ret == -1) {
					/* LCOV_EXCL_START */
					log_tag("%s:%" PRIu64 ":%s:%s: Time error. %s.\n", es(errno), i, disk->name, esc_tag(file->sub), strerror(errno));
					log_fatal_errno(errno, disk->name);

					/* mark the file as damaged */
					file_flag_set(file, FILE_IS_DAMAGED);
					return -1;
					/* LCOV_EXCL_STOP */
				}
			} else {
				log_tag("collision:%s:%s:%s: Not setting modification time to avoid inode collision\n", disk->name, esc_tag(file->sub), esc_tag(collide_file->sub));
			}

			/*
			 * This rename is the file-level recovery commit point. Keep the file
			 * quarantined until every block and metadata was processed without error;
			 * promoting only after setting the timestamp ensures the final file is
			 * never exposed with incomplete data or provisional timestamp.
			 */
			if (was_unrecoverable) {
				char path[PATH_MAX];
				char path_from[PATH_MAX];

				pathprint(path, sizeof(path), "%s%s", disk->dir, file->sub);
				pathprint(path_from, sizeof(path_from), "%s%s.unrecoverable", disk->dir, file->sub);

				ret = handle_close(&handle[j]);
				if (ret != 0) {
					/* LCOV_EXCL_START */
					log_tag("%s:%" PRIu64 ":%s:%s: Close error. %s.\n", es(errno), i, disk->name, esc_tag(file->sub), strerror(errno));
					log_fatal_errno(errno, disk->name);
					return -1;
					/* LCOV_EXCL_STOP */
				}

				ret = rename(path_from, path);
				if (ret != 0) {
					/* LCOV_EXCL_START */
					log_fatal(errno, "Error renaming '%s'. %s.\n", path_from, strerror(errno));
					log_tag("%s:%" PRIu64 ":%s:%s: Rename error. %s.\n", es(errno), i, disk->name, esc_tag(file->sub), strerror(errno));
					log_fatal_errno(errno, disk->name);
					return -1;
					/* LCOV_EXCL_STOP */
				}
			}

			log_tag("status:recovered:%s:%s\n", disk->name, esc_tag(file->sub));
			msg_info("recovered %s\n", fmt_term(disk, file->sub));
		} else {
			/*
			 * We are not fixing, but only checking
			 * print just the final status
			 */
			if (file_flag_has(file, FILE_IS_DAMAGED)) {
				log_tag("status:unrecoverable:%s:%s\n", disk->name, esc_tag(file->sub));
				msg_info("unrecoverable %s\n", fmt_term(disk, file->sub));
			} else if (file_flag_has(file, FILE_IS_FIXED)) {
				log_tag("status:recoverable:%s:%s\n", disk->name, esc_tag(file->sub));
				msg_info("recoverable %s\n", fmt_term(disk, file->sub));
			} else {
				/* we don't use msg_verbose() because it also goes into the log */
				if (msg_level >= MSG_VERBOSE) {
					log_tag("status:correct:%s:%s\n", disk->name, esc_tag(file->sub));
					msg_info("correct %s\n", fmt_term(disk, file->sub));
				}
			}
		}

close_and_continue:
		/*
		 * If the opened file is the correct one, close it
		 * in case of excluded and fragmented files it's possible
		 * that the opened file is not the current one
		 */
		if (handle[j].file == file) {
			/*
			 * Ensure to close the file just after finishing with it
			 * to avoid keeping it open without any possible use
			 */
			ret = handle_close(&handle[j]);
			if (ret != 0) {
				/* LCOV_EXCL_START */
				log_tag("%s:%" PRIu64 ":%s:%s: Close error. %s.\n", es(errno), i, disk->name, esc_tag(file->sub), strerror(errno));
				log_fatal_errno(errno, disk->name);
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
	unsigned j;
	unsigned l;

	/* filter for bad blocks */
	if (state->opt.badblockonly) {
		snapraid_info info;

		/* get block specific info */
		info = info_get(&state->infoarr, i);

		/*
		 * Filter specifically only for bad blocks
		 */
		return info_get_bad(info);
	}

	/* filter for the parity */
	if (state->opt.badfileonly) {
		snapraid_info info;

		/* get block specific info */
		info = info_get(&state->infoarr, i);

		/*
		 * If the block is bad, it has to be processed
		 *
		 * This is not necessary in normal cases because if a block is bad,
		 * it necessary needs to have a file related to it, and files with
		 * bad blocks are fully included.
		 *
		 * But some files may be excluded by additional filter options,
		 * so it's not always true, and this ensures to always check all
		 * the bad blocks.
		 */
		if (info_get_bad(info))
			return 1;
	} else {
		/* if a parity is not excluded, include all blocks, even unused ones */
		for (l = 0; l < state->level; ++l) {
			if (!state->parity[l].is_excluded_by_filter) {
				return 1;
			}
		}
	}

	/* filter for the files */
	for (j = 0; j < diskmax; ++j) {
		struct snapraid_block* block;

		/* if no disk, nothing to check */
		if (!handle[j].disk)
			continue;

		block = fs_par2block_find(handle[j].disk, i);

		/*
		 * Try to recover all files, even the ones without hash
		 * because in some cases we can recover also them
		 */
		if (block_has_file(block)) {
			struct snapraid_file* file = fs_par2file_get(handle[j].disk, i, 0);
			if (!file_flag_has(file, FILE_IS_EXCLUDED)) { /* only if the file is not filtered out */
				return 1;
			}
		}
	}

	return 0;
}

static int state_check_process(struct snapraid_state* state, int fix, struct snapraid_parity_handle** parity, block_off_t blockstart, block_off_t blockmax, int partial)
{
	struct snapraid_handle* handle;
	unsigned diskmax;
	block_off_t i;
	unsigned j;
	void* buffer_alloc;
	void** buffer;
	unsigned buffermax;
	ssize_t ret;
	data_off_t countsize;
	block_off_t countpos;
	block_off_t countmax;
	unsigned soft_error;
	unsigned io_error;
	unsigned silent_error;
	unsigned unrecoverable_error;
	unsigned recovered_error;
	struct failed_struct* failed;
	unsigned* failed_map;
	unsigned l;
	bit_vect_t* block_enabled;
	struct snapraid_bw bw;

	handle = handle_mapping(state, &diskmax);

	/* initialize the bandwidth context */
	bw_init(&bw, state->opt.bwlimit);

	/* share the bandwidth context with all handles */
	for (j = 0; j < diskmax; ++j)
		handle[j].bw = &bw;
	for (j = 0; j < state->level; ++j)
		if (parity[j])
			parity[j]->bw = &bw;

	/* we need 1 * data + 2 * parity + 1 * zero */
	buffermax = diskmax + 2 * state->level + 1;

	buffer = malloc_nofail_vector_align(buffermax, state->block_size, &buffer_alloc);
	if (!state->opt.skip_self)
		mtest_vector(buffermax, state->block_size, buffer);

	/* fill up the zero buffer */
	memset(buffer[buffermax - 1], 0, state->block_size);
	raid_zero(buffer[buffermax - 1]);

	failed = nalloc_nofail(diskmax, sizeof(struct failed_struct));
	failed_map = nalloc_nofail(diskmax, sizeof(unsigned));

	soft_error = 0;
	io_error = 0;
	silent_error = 0;
	unrecoverable_error = 0;
	recovered_error = 0;

	msg_progress("Selecting...\n");

	/* first count the number of blocks to process */
	countmax = 0;
	block_enabled = calloc_nofail(1, bit_vect_size(blockmax)); /* preinitialize to 0 */
	for (i = blockstart; i < blockmax; ++i) {
		if (!block_is_enabled(state, i, handle, diskmax))
			continue;
		bit_vect_set(block_enabled, i);
		++countmax;
	}

	if (fix)
		msg_progress("Fixing...\n");
	else if (!state->opt.auditonly)
		msg_progress("Checking...\n");
	else
		msg_progress("Hashing...\n");

	/* check all the blocks in files */
	countsize = 0;
	countpos = 0;

	int alert = state_progress_begin(state, blockstart, blockmax, countmax);
	if (alert > 0)
		goto end;
	if (alert < 0)
		goto bail;

	for (i = blockstart; i < blockmax; ++i) {
		unsigned failed_count;
		int valid_parity;
		int used_parity;
		snapraid_info info;
		int rehash;

		if (!bit_vect_test(block_enabled, i)) {
			/* continue with the next block */
			continue;
		}

		/*
		 * If we have valid parity, and it makes sense to check its content.
		 * If we already know that the parity is invalid, we just read the file
		 * but we don't report parity errors
		 * Note that with auditonly, we anyway skip the full parity check,
		 * because we also don't read it at all
		 */
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
			ssize_t read_size;
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
				/*
				 * Mark the parity as invalid, and don't try to check/fix it
				 * because it will be recomputed at the next sync
				 */
				valid_parity = 0;
				/* follow */
			}

			/* if the block is DELETED */
			if (block_state == BLOCK_STATE_DELETED) {
				/* use an empty block */
				memset(buffer[j], 0, state->block_size);

				/*
				 * DELETED is not a file repair target. It is kept only because its OLD
				 * contribution may still be present in parity and may be required by the
				 * pre-sync recovery hypothesis.
				 */
				failed[failed_count].is_bad = 0;
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
				/*
				 * Use an empty block
				 * in true, this is unnecessary, because we are not checking any parity
				 * but we keep it for completeness
				 */
				memset(buffer[j], 0, state->block_size);
				continue;
			}

			/* if the file is closed or different than the current one */
			if (handle[j].file == 0 || handle[j].file != file) {
				/* keep a pointer at the file we are going to close for error reporting */
				struct snapraid_file* report = handle[j].file;
				ret = handle_close(&handle[j]);
				if (ret == -1) {
					/* LCOV_EXCL_START */
					log_tag("%s:%" PRIu64 ":%s:%s: Close error. %s.\n", es(errno), i, disk->name, esc_tag(report->sub), strerror(errno));
					log_fatal_errno(errno, disk->name);
					log_fatal(errno, "Stopping at block %" PRIu64 "\n", i);

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
						log_tag("%s:%" PRIu64 ":%s:%s: Create error. %s.\n", es(errno), i, disk->name, esc_tag(file->sub), strerror(errno));
						log_fatal_errno(errno, disk->name);
						log_fatal(errno, "Stopping at block %" PRIu64 "\n", i);

						++unrecoverable_error;
						goto bail;
						/* LCOV_EXCL_STOP */
					}

					/* check if the file was just created */
					if (handle[j].created != 0) {
						/*
						 * If fragmented, it may be reopened, so remember that the file
						 * was originally missing
						 */
						file_flag_set(file, FILE_IS_CREATED);
					}
				} else {
					/* open the file only for reading */
					if (!file_flag_has(file, FILE_IS_MISSING)) {
						ret = handle_open(&handle[j], file, state->file_mode, state->opt.expected_missing ? log_expected : 0);
					} else {
						errno = ENOENT;
						ret = -1; /* if the file is missing, we cannot open it */
					}
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

						log_tag("%s:%" PRIu64 ":%s:%s: Open error at position %" PRIu64 ". %s.\n", es(errno), i, disk->name, esc_tag(file->sub), file_pos, strerror(errno));

						if (is_hw(errno)) {
							++io_error;
						} else {
							++soft_error;
						}

						/*
						 * Mark the file as missing, to avoid to retry to open it again
						 * note that this can be done only if we are not fixing it
						 * otherwise, it could be recreated
						 */
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

				/*
				 * Missing data inside a short file is handled block by block by normal
				 * recovery writes, which extend the file as needed.
				 *
				 * Extra data past the expected end is different: it has no content block and
				 * therefore no later block recovery can remove it. Fix must truncate that
				 * untracked tail explicitly.
				 */
				if (!file_flag_has(file, FILE_IS_OPENED)
					&& !file_flag_has(file, FILE_IS_EXCLUDED)
					&& !(state->opt.syncedonly && file_flag_has(file, FILE_IS_UNSYNCED))
					&& handle[j].st.st_size > file->size
				) {
					log_error(ESOFT, "File '%s' is larger than expected.\n", handle[j].path);
					log_tag("error:%" PRIu64 ":%s:%s: Size error\n", i, disk->name, esc_tag(file->sub));
					++soft_error;

					if (fix) {
						ret = handle_truncate(&handle[j], file);
						if (ret == -1) {
							/* LCOV_EXCL_START */
							log_tag("%s:%" PRIu64 ":%s:%s: Truncate error. %s.\n", es(errno), i, disk->name, esc_tag(file->sub), strerror(errno));
							log_fatal_errno(errno, disk->name);
							log_fatal(errno, "Stopping at block %" PRIu64 "\n", i);

							++unrecoverable_error;
							goto bail;
							/* LCOV_EXCL_STOP */
						}

						log_tag("fixed:%" PRIu64 ":%s:%s: Fixed size\n", i, disk->name, esc_tag(file->sub));
						++recovered_error;
					}
				}

				/*
				 * Mark the file as opened at least one time
				 * this is used to avoid to check the unsynced and size
				 * more than one time, in case the file is reopened later
				 */
				file_flag_set(file, FILE_IS_OPENED);
			}

			/* read from the file */
			if (file_flag_has(file, FILE_IS_MISSING)) {
				/* if the file is reported missing, don't even try to read it */
				errno = ENOENT;
				read_size = -1;
			} else {
				read_size = handle_read(&handle[j], file_pos, buffer[j], state->block_size, state->opt.expected_missing ? log_expected : 0);
			}
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

				log_tag("%s:%" PRIu64 ":%s:%s: Read error at position %" PRIu64 ". %s.\n", es(errno), i, disk->name, esc_tag(file->sub), file_pos, strerror(errno));

				if (is_hw(errno)) {
					++io_error;
				} else {
					++soft_error;
				}

				/* if we are reading at the end, mark the file as missing to avoid to try to read it again at the next block */
				if (errno == ENOENT) {
					file_flag_set(file, FILE_IS_MISSING);
				}
				continue;
			}

			countsize += read_size;

			/*
			 * Always insert CHG blocks, the repair functions needs all of them
			 * because the parity may be still referring at the old state
			 * and the repair must be aware of it
			 */
			if (block_state == BLOCK_STATE_CHG) {
				/*
				 * A CHG block has no hash for the current data. The stored hash
				 * refers to the previous content, and after an interrupted sync
				 * parity may refer either to the old or to the new content.
				 *
				 * If the block is readable, preserve it and assume it is the best
				 * available copy of the current data. We must not try to validate it
				 * against block->hash, as a match would only identify the old data
				 * and a mismatch would not prove that the current data is correct.
				 *
				 * This also intentionally applies to data reused from a previous
				 * multistep fix through an .unrecoverable file: already readable CHG
				 * data is preserved, while fix attempts to recover only missing or
				 * unreadable blocks.
				 */
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

				log_tag("error:%" PRIu64 ":%s:%s: Data error at position %" PRIu64 ", diff hash bits %u/%zu\n", i, disk->name, esc_tag(file->sub), file_pos, diff, BLOCK_HASH_SIZE * 8);
				++silent_error;
				continue;
			}

			/*
			 * A readable REP has already passed its NEW/current hash check, so its current
			 * data is known good. It must nevertheless participate in repair because an
			 * interrupted sync may have left physical parity containing its OLD
			 * contribution.
			 *
			 * Unlike CHG, the REP stored hash cannot identify that OLD contribution
			 * because it describes the NEW data.
			 */
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
					ret = parity_read(parity[l], i, buffer_recov[l], state->block_size);
					if (ret == -1) {
						log_tag("parity_%s:%" PRIu64 ":%s: Read error. %s.\n", es(errno), i, lev_config_name(l), strerror(errno));

						/*
						 * NULL means that this parity level cannot be trusted as an input to recovery.
						 * The same marker is later used for a parity block that was read successfully
						 * but failed comparison with recomputed parity.
						 *
						 * During fix, an accessible non-excluded parity marked this way is also the
						 * one that needs to be rewritten from the recomputed value.
						 */
						buffer_recov[l] = 0;

						if (is_hw(errno)) {
							++io_error;
						} else {
							++soft_error;
						}
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
					silent_error += ret;
				++unrecoverable_error;

				/* print a list of all the errors in files */
				for (j = 0; j < failed_count; ++j) {
					if (failed[j].is_bad)
						log_tag("unrecoverable:%" PRIu64 ":%s:%s: Unrecoverable error at position %" PRIu64 "\n", i, failed[j].disk->name, esc_tag(failed[j].file->sub), failed[j].file_pos);
				}

				/* keep track of damaged files */
				for (j = 0; j < failed_count; ++j) {
					if (failed[j].is_bad)
						file_flag_set(failed[j].file, FILE_IS_DAMAGED);
				}
			} else {
				/*
				 * repair() returning success means that a RAID solution was validated under
				 * one of the parity-history hypotheses. It does not necessarily mean that
				 * every bad unsynced block was recovered to its CURRENT version.
				 *
				 * A bad block marked is_outofdate is therefore still an unrecoverable current
				 * data error even though the stripe-level RAID recovery succeeded.
				 */
				int partial_recover_error = 0;

				/* print a list of all the errors in files */
				for (j = 0; j < failed_count; ++j) {
					if (failed[j].is_bad && failed[j].is_outofdate) {
						++partial_recover_error;
						log_tag("unrecoverable:%" PRIu64 ":%s:%s: Unrecoverable unsynced error at position %" PRIu64 "\n", i, failed[j].disk->name, esc_tag(failed[j].file->sub), failed[j].file_pos);
					}
				}
				if (partial_recover_error != 0) {
					silent_error += partial_recover_error;
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

							log_tag("parity_error:%" PRIu64 ":%s: Data error, diff parity bits %u/%u\n", i, lev_config_name(l), diff, state->block_size * 8);
							++silent_error;
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

						/*
						 * Write the best recovered candidate even when it is out-of-date. Such data
						 * may still be useful to the user or to a later multistep recovery.
						 *
						 * An out-of-date write is not considered a successful fix: the file is marked
						 * damaged below and file_post() keeps or moves it to .unrecoverable instead
						 * of promoting it as recovered.
						 */
						ret = handle_write(failed[j].handle, failed[j].file_pos, buffer[failed[j].index], state->block_size);
						if (ret == -1) {
							/* LCOV_EXCL_START */
							log_tag("%s:%" PRIu64 ":%s:%s: Write error. %s.\n", es(errno), i, failed[j].disk->name, esc_tag(failed[j].file->sub), strerror(errno));
							log_fatal_errno(errno, failed[j].disk->name);
							log_fatal(errno, "Stopping at block %" PRIu64 "\n", i);

							/* mark the file as damaged */
							file_flag_set(failed[j].file, FILE_IS_DAMAGED);

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

						/*
						 * Mark the file as containing some fixes
						 * note that it could be also marked as damaged in other iterations
						 */
						file_flag_set(failed[j].file, FILE_IS_FIXED);

						log_tag("fixed:%" PRIu64 ":%s:%s: Fixed data error at position %" PRIu64 "\n", i, failed[j].disk->name, esc_tag(failed[j].file->sub), failed[j].file_pos);
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
									log_tag("%s:%" PRIu64 ":%s: Write error. %s.\n", es(errno), i, lev_config_name(l), strerror(errno));
									log_fatal_errno(errno, lev_config_name(l));
									log_fatal(errno, "Stopping at block %" PRIu64 "\n", i);

									++unrecoverable_error;
									goto bail;
									/* LCOV_EXCL_STOP */
								}

								log_tag("parity_fixed:%" PRIu64 ":%s: Fixed data error\n", i, lev_config_name(l));
								++recovered_error;
							}
						}
					}
				} else {
					/*
					 * If we are not fixing, we just set the FIXED flag
					 * meaning that we could fix this file if we try
					 */
					for (j = 0; j < failed_count; ++j) {
						if (failed[j].is_bad) {
							file_flag_set(failed[j].file, FILE_IS_FIXED);
						}
					}
				}
			}
		} else {
			/*
			 * If we are not checking, we just set the DAMAGED flag
			 * to report that the file is damaged, and we don't know if we can fix it
			 */
			for (j = 0; j < failed_count; ++j) {
				if (failed[j].is_bad) {
					file_flag_set(failed[j].file, FILE_IS_DAMAGED);
				}
			}
		}

		/* post process the files */
		ret = file_post(state, fix, partial, i, handle, diskmax);
		if (ret == -1) {
			/* LCOV_EXCL_START */
			log_fatal(errno, "Stopping at block %" PRIu64 "\n", i);

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

	/*
	 * These objects have no parity block position, so an explicit -S/-B block
	 * range cannot select them. Check/fix them separately after block processing,
	 * including during an otherwise partial block-range operation.
	 */
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
			int unsuccessful = 0;

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
				unsuccessful = 1;

				log_error(errno, "Error stating empty file '%s'. %s.\n", path, strerror(errno));
				log_tag("empty_%s:%s:%s: Empty file stat error\n", es(errno), disk->name, esc_tag(file->sub));

				if (is_hw(errno)) {
					++io_error;
				} else {
					++soft_error;
				}
			} else if (!S_ISREG(st.st_mode)) {
				unsuccessful = 1;

				log_error(ESOFT, "Error stating empty file '%s' for not regular file.\n", path);
				log_tag("empty_error:%s:%s: Empty file error for not regular file\n", disk->name, esc_tag(file->sub));
				++soft_error;
			} else if (st.st_size != 0) {
				unsuccessful = 1;

				log_error(ESOFT, "Error stating empty file '%s' for not empty file.\n", path);
				log_tag("empty_error:%s:%s: Empty file error for size '%" PRIu64 "'\n", disk->name, esc_tag(file->sub), (uint64_t)st.st_size);
				++soft_error;
			}

			if (fix && unsuccessful) {
				int f;

				/* create the ancestor directories */
				ret = mkancestor(path);
				if (ret != 0) {
					/* LCOV_EXCL_START */
					log_fatal(errno, "Error creating ancestor '%s%s'. %s.\n", disk->dir, file->sub, strerror(errno));
					log_tag("empty_%s:%" PRIu64 ":%s:%s: Create ancestor error. %s.\n", es(errno), i, disk->name, esc_tag(file->sub), strerror(errno));
					log_fatal_errno(errno, disk->name);
					log_fatal(errno, "Stopping\n");

					++unrecoverable_error;
					goto bail;
					/* LCOV_EXCL_STOP */
				}

				/*
				 * Create it
				 * O_NOFOLLOW: do not follow links to ensure to open the real file
				 */
				f = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY | O_NOFOLLOW, 0600);
				if (f == -1) {
					/* LCOV_EXCL_START */
					log_fatal(errno, "Error creating '%s%s'. %s.\n", disk->dir, file->sub, strerror(errno));
					log_tag("empty_%s:%" PRIu64 ":%s:%s: Create error. %s.\n", es(errno), i, disk->name, esc_tag(file->sub), strerror(errno));
					log_fatal_errno(errno, disk->name);
					log_fatal(errno, "Stopping\n");

					++unrecoverable_error;
					goto bail;
					/* LCOV_EXCL_STOP */
				}

				/* set the original modification time */
				ret = fmtime(f, file->mtime_sec, file->mtime_nsec);
				if (ret != 0) {
					/* LCOV_EXCL_START */
					log_fatal(errno, "Error timing '%s%s'. %s.\n", disk->dir, file->sub, strerror(errno));
					log_tag("empty_%s:%" PRIu64 ":%s:%s: Time error. %s.\n", es(errno), i, disk->name, esc_tag(file->sub), strerror(errno));
					log_fatal_errno(errno, disk->name);
					log_fatal(errno, "Stopping\n");

					close(f);

					++unrecoverable_error;
					goto bail;
					/* LCOV_EXCL_STOP */
				}

				/* close it */
				ret = close(f);
				if (ret != 0) {
					/* LCOV_EXCL_START */
					log_fatal(errno, "Error closing '%s%s'. %s.\n", disk->dir, file->sub, strerror(errno));
					log_tag("empty_%s:%" PRIu64 ":%s:%s: Close error. %s.\n", es(errno), i, disk->name, esc_tag(file->sub), strerror(errno));
					log_fatal_errno(errno, disk->name);
					log_fatal(errno, "Stopping\n");

					++unrecoverable_error;
					goto bail;
					/* LCOV_EXCL_STOP */
				}

				log_tag("empty_fixed:%s:%s: Fixed empty file\n", disk->name, esc_tag(file->sub));
				++recovered_error;

				log_tag("status:recovered:%s:%s\n", disk->name, esc_tag(file->sub));
				msg_info("recovered %s\n", fmt_term(disk, file->sub));
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
			int unsuccessful = 0;
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
					unsuccessful = 1;

					log_error(errno, "Error stating hardlink '%s'. %s.\n", path, strerror(errno));
					log_tag("hardlink_%s:%s:%s:%s: Hardlink stat error. %s.\n", es(errno), disk->name, esc_tag(slink->sub), esc_tag(slink->linkto), strerror(errno));

					if (is_hw(errno)) {
						++io_error;
					} else {
						++soft_error;
					}
				} else if (!S_ISREG(st.st_mode)) {
					unsuccessful = 1;

					log_error(ESOFT, "Error stating hardlink '%s' for not regular file.\n", path);
					log_tag("hardlink_error:%s:%s:%s: Hardlink error for not regular file\n", disk->name, esc_tag(slink->sub), esc_tag(slink->linkto));
					++soft_error;
				}

				/* stat the "to" file */
				pathprint(pathto, sizeof(pathto), "%s%s", disk->dir, slink->linkto);
				ret = stat(pathto, &stto);
				if (ret == -1) {
					unsuccessful = 1;

					if (errno == ENOENT) {
						unrecoverable = 1;
						if (fix) {
							/*
							 * If the target doesn't exist, it's unrecoverable
							 * because we cannot create an hardlink of a file that
							 * doesn't exists
							 * but in check, we can assume that fixing will recover
							 * such missing file, so we assume a less drastic error
							 */
							++unrecoverable_error;
						}
					}

					log_error(errno, "Error stating hardlink-to '%s'. %s.\n", pathto, strerror(errno));
					log_tag("hardlink_%s:%s:%s:%s: Hardlink to stat error. %s.\n", es(errno), disk->name, esc_tag(slink->sub), esc_tag(slink->linkto), strerror(errno));

					if (is_hw(errno)) {
						++io_error;
					} else {
						++soft_error;
					}
				} else if (!S_ISREG(stto.st_mode)) {
					unsuccessful = 1;

					log_error(ESOFT, "Error stating hardlink-to '%s' for not regular file.\n", path);
					log_tag("hardlink_error:%s:%s:%s: Hardlink-to error for not regular file\n", disk->name, esc_tag(slink->sub), esc_tag(slink->linkto));
					++soft_error;
				} else if (!unsuccessful && st.st_ino != INODE_INVALID && stto.st_ino != INODE_INVALID && st.st_ino != stto.st_ino) {
					unsuccessful = 1;

					log_error(ESOFT, "Mismatch hardlink '%s' and '%s'. Different inode.\n", path, pathto);
					log_tag("hardlink_error:%s:%s:%s: Hardlink mismatch for different inode\n", disk->name, esc_tag(slink->sub), esc_tag(slink->linkto));
					++soft_error;
				}
			} else {
				/* read the symlink */
				pathprint(path, sizeof(path), "%s%s", disk->dir, slink->sub);
				ret = readlink(path, linkto, sizeof(linkto));
				if (ret < 0) {
					unsuccessful = 1;

					log_error(errno, "Error reading symlink '%s'. %s.\n", path, strerror(errno));
					log_tag("symlink_%s:%s:%s: Symlink read error. %s.\n", es(errno), disk->name, esc_tag(slink->sub), strerror(errno));

					if (is_hw(errno)) {
						++io_error;
					} else {
						++soft_error;
					}
				} else if (ret >= PATH_MAX) {
					unsuccessful = 1;

					log_error(ESOFT, "Error reading symlink '%s'. Symlink too long.\n", path);
					log_tag("symlink_error:%s:%s: Symlink too long\n", disk->name, esc_tag(slink->sub));
					++soft_error;
				} else {
					linkto[ret] = 0;

					if (strcmp(linkto, slink->linkto) != 0) {
						unsuccessful = 1;

						log_tag("symlink_error:%s:%s: Symlink data error '%s' instead of '%s'\n", disk->name, esc_tag(slink->sub), linkto, slink->linkto);
						++soft_error;
					}
				}
			}

			if (fix && unsuccessful && !unrecoverable) {
				const char* link = link_flag_has(slink, FILE_IS_HARDLINK) ? "hard" : "sym";

				/* create the ancestor directories */
				ret = mkancestor(path);
				if (ret != 0) {
					/* LCOV_EXCL_START */
					log_fatal(errno, "Error creating ancestor '%s%s'. %s.\n", disk->dir, slink->sub, strerror(errno));
					log_tag("%slink_%s:%" PRIu64 ":%s:%s: Create ancestor error. %s.\n", link, es(errno), i, disk->name, esc_tag(slink->sub), strerror(errno));
					log_fatal_errno(errno, disk->name);
					log_fatal(errno, "Stopping\n");

					++unrecoverable_error;
					goto bail;
					/* LCOV_EXCL_STOP */
				}

				/* if it exists, it must be deleted before recreating */
				ret = remove(path);
				if (ret != 0 && errno != ENOENT) {
					/* LCOV_EXCL_START */
					log_fatal(errno, "Error removing '%s%s'. %s.\n", disk->dir, slink->sub, strerror(errno));
					log_tag("%slink_%s:%" PRIu64 ":%s:%s: Remove error. %s.\n", link, es(errno), i, disk->name, esc_tag(slink->sub), strerror(errno));
					log_fatal_errno(errno, disk->name);
					log_fatal(errno, "Stopping\n");

					++unrecoverable_error;
					goto bail;
					/* LCOV_EXCL_STOP */
				}

				/* create it */
				if (link_flag_has(slink, FILE_IS_HARDLINK)) {
					ret = hardlink(pathto, path);
					if (ret != 0) {
						/* LCOV_EXCL_START */
						log_fatal(errno, "Error writing hardlink '%s' to '%s'. %s.\n", path, pathto, strerror(errno));
						log_tag("hardlink_%s:%" PRIu64 ":%s:%s: Hardlink error. %s.\n", es(errno), i, disk->name, esc_tag(slink->sub), strerror(errno));
						log_fatal_errno(errno, disk->name);
						log_fatal(errno, "Stopping\n");

						++unrecoverable_error;
						goto bail;
						/* LCOV_EXCL_STOP */
					}

					log_tag("hardlink_fixed:%s:%s: Fixed hardlink error\n", disk->name, esc_tag(slink->sub));
					++recovered_error;
				} else {
					ret = symlink(slink->linkto, path);
					if (ret != 0) {
						/* LCOV_EXCL_START */
						log_fatal(errno, "Error writing symlink '%s' to '%s'. %s.\n", path, slink->linkto, strerror(errno));
						log_tag("symlink_%s:%" PRIu64 ":%s:%s: Hardlink error. %s.\n", es(errno), i, disk->name, esc_tag(slink->sub), strerror(errno));
						log_fatal_errno(errno, disk->name);
						log_fatal(errno, "Stopping\n");

						++unrecoverable_error;
						goto bail;
						/* LCOV_EXCL_STOP */
					}

					log_tag("symlink_fixed:%s:%s: Fixed symlink error\n", disk->name, esc_tag(slink->sub));
					++recovered_error;
				}

				log_tag("status:recovered:%s:%s\n", disk->name, esc_tag(slink->sub));
				msg_info("recovered %s\n", fmt_term(disk, slink->sub));
			}
		}

		/* for each dir in the disk */
		disk = handle[i].disk;
		node = disk->dirlist;
		while (node) {
			char path[PATH_MAX];
			struct stat st;
			struct snapraid_dir* dir;
			int unsuccessful = 0;

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
				unsuccessful = 1;

				log_error(errno, "Error stating dir '%s'. %s.\n", path, strerror(errno));
				log_tag("dir_%s:%s:%s: Dir stat error. %s.\n", es(errno), disk->name, esc_tag(dir->sub), strerror(errno));

				if (is_hw(errno)) {
					++io_error;
				} else {
					++soft_error;
				}
			} else if (!S_ISDIR(st.st_mode)) {
				unsuccessful = 1;

				log_tag("dir_error:%s:%s: Dir error for not directory\n", disk->name, esc_tag(dir->sub));
				++soft_error;
			}

			if (fix && unsuccessful) {
				/* create the ancestor directories */
				ret = mkancestor(path);
				if (ret != 0) {
					/* LCOV_EXCL_START */
					log_fatal(errno, "Error creating ancestor '%s%s'. %s.\n", disk->dir, dir->sub, strerror(errno));
					log_tag("dir_%s:%" PRIu64 ":%s:%s: Create ancestor error. %s.\n", es(errno), i, disk->name, esc_tag(dir->sub), strerror(errno));
					log_fatal_errno(errno, disk->name);
					log_fatal(errno, "Stopping\n");

					++unrecoverable_error;
					goto bail;
					/* LCOV_EXCL_STOP */
				}

				/* create it */
				ret = mkdir(path, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
				if (ret != 0 && errno != EEXIST) {
					/* LCOV_EXCL_START */
					log_fatal(errno, "Error creating directory '%s%s'. %s.\n", disk->dir, dir->sub, strerror(errno));
					log_tag("dir_%s:%" PRIu64 ":%s:%s: Create directory error. %s.\n", es(errno), i, disk->name, esc_tag(dir->sub), strerror(errno));
					log_fatal_errno(errno, disk->name);
					log_fatal(errno, "Stopping\n");

					++unrecoverable_error;
					goto bail;
					/* LCOV_EXCL_STOP */
				}

				log_tag("dir_fixed:%s:%s: Fixed dir error\n", disk->name, esc_tag(dir->sub));
				++recovered_error;

				log_tag("status:recovered:%s:%s\n", disk->name, esc_tag(dir->sub));
				msg_info("recovered %s\n", fmt_term(disk, dir->sub));
			}
		}
	}

end:
	state_progress_end(state, countpos, countmax, countsize, "Nothing to check.\n");

bail:
	/* close all the files left open */
	for (j = 0; j < diskmax; ++j) {
		struct snapraid_file* file = handle[j].file;
		struct snapraid_disk* disk = handle[j].disk;
		ret = handle_close(&handle[j]);
		if (ret == -1) {
			/* LCOV_EXCL_START */
			/*
			 * If handle_close fails, the handle was open (f != -1), which
			 * guarantees that both file and disk pointers are valid.
			 */
			log_tag("%s:%" PRIu64 ":%s:%s: Close error. %s.\n", es(errno), blockmax, disk->name, esc_tag(file->sub), strerror(errno));
			log_fatal_errno(errno, disk->name);

			++unrecoverable_error;
			/* continue, as we are already exiting */
			/* LCOV_EXCL_STOP */
		}
	}

	if (soft_error || io_error || silent_error || recovered_error || unrecoverable_error) {
		msg_status("\n");
		msg_status("%8u soft errors\n", soft_error);
		msg_status("%8u io errors\n", io_error);
		msg_status("%8u data errors\n", silent_error);
		if (fix) {
			msg_status("%8u recovered errors\n", recovered_error);
		}
		if (unrecoverable_error) {
			msg_status("%8u UNRECOVERABLE errors\n", unrecoverable_error);
		} else {
			/* without checking, we don't know if they are really recoverable or not */
			if (!state->opt.auditonly)
				msg_status("%8u unrecoverable errors\n", unrecoverable_error);
		}
	}

	if ((soft_error || io_error || silent_error) && !fix) {
		if (soft_error)
			log_fatal(ESOFT, "WARNING! There are soft errors!\n");
		if (io_error)
			log_fatal(EIO, "DANGER! Unexpected input/output errors!\n");
		if (silent_error)
			log_fatal(EDATA, "DANGER! Unexpected silent data errors!\n");
	}
	if (unrecoverable_error)
		log_fatal(ESOFT, "DANGER! Unrecoverable errors detected!\n");

	log_tag("summary:error_soft:%u\n", soft_error);
	log_tag("summary:error_io:%u\n", io_error);
	log_tag("summary:error_data:%u\n", silent_error);
	if (fix)
		log_tag("summary:error_recovered:%u\n", recovered_error);
	if (!state->opt.auditonly)
		log_tag("summary:error_unrecoverable:%u\n", unrecoverable_error);
	if (fix) {
		if (soft_error + io_error + silent_error + recovered_error + unrecoverable_error == 0)
			log_tag("summary:exit:ok\n");
		else if (unrecoverable_error == 0)
			log_tag("summary:exit:recovered\n");
		else
			log_tag("summary:exit:unrecoverable\n");
	} else if (!state->opt.auditonly) {
		if (soft_error + io_error + silent_error + unrecoverable_error == 0)
			log_tag("summary:exit:ok\n");
		else if (unrecoverable_error == 0)
			log_tag("summary:exit:recoverable\n");
		else
			log_tag("summary:exit:unrecoverable\n");
	} else { /* audit only */
		if (soft_error + silent_error + io_error == 0)
			log_tag("summary:exit:ok\n");
		else if (silent_error + io_error == 0)
			log_tag("summary:exit:warning\n");
		else
			log_tag("summary:exit:error\n");
	}
	log_flush();

	free(failed);
	free(failed_map);
	free(block_enabled);
	free(handle);
	free(buffer_alloc);
	free(buffer);

	bw_done(&bw);

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
			if (unrecoverable_error != 0)
				return -1;
			if (soft_error + silent_error + io_error == 0)
				return -1;
		} else {
			if (unrecoverable_error != 0)
				return -1;
			if (soft_error + silent_error + io_error != 0)
				return -1;
		}
	}

	if (alert < 0)
		return -1;

	return 0;
}

int state_check(struct snapraid_state* state, int fix, block_off_t blockstart, block_off_t blockcount)
{
	block_off_t blockmax;
	data_off_t size;
	int ret;
	int partial;
	struct snapraid_parity_handle parity[LEV_MAX];
	struct snapraid_parity_handle* parity_ptr[LEV_MAX];
	unsigned process_error;
	unsigned l;

	msg_progress("Initializing...\n");

	/*
	 * -S/--start and -B/--count allow check/fix to operate on an arbitrary
	 * parity range instead of processing from the beginning. In particular,
	 * recovery of a later section can be retried while an earlier section is
	 * missing or damaged.
	 *
	 * A partial fix therefore does not establish that preceding or following
	 * parity is valid; it covers only the requested range.
	 *
	 * Keep partial based on the explicit -S/-B request rather than the clipped
	 * range: a nonzero -B remains partial even if it reaches the end of parity.
	 */
	partial = blockstart != 0 || blockcount != 0;

	blockmax = parity_allocated_size(state);
	size = blockmax * (data_off_t)state->block_size;

	if (blockstart > blockmax) {
		/* LCOV_EXCL_START */
		log_fatal(EUSER, "Error in the specified starting block %" PRIu64 ". It's larger than the parity size %" PRIu64 ".\n", blockstart, blockmax);
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* adjust the number of block to process */
	if (blockcount != 0 && blockcount < blockmax - blockstart) {
		blockmax = blockstart + blockcount;
	}

	if (fix) {
		/*
		 * If fixing, create the file and open for writing
		 * if it fails, we cannot continue
		 */
		for (l = 0; l < state->level; ++l) {
			/* skip parity disks that are not accessible */
			if (state->parity[l].skip_access) {
				parity_ptr[l] = 0;
				continue;
			}

			parity_ptr[l] = &parity[l];

			/* if the parity is excluded */
			if (state->parity[l].is_excluded_by_filter) {
				/*
				 * Excluded means "do not modify", not "do not use".
				 *
				 * An excluded parity can still provide a recovery equation or the independent
				 * parity check required when recovered data has no trustworthy hash, so keep
				 * it available read-only whenever possible.
				 */
				ret = parity_open(parity_ptr[l], &state->parity[l], l, state->file_mode, state->block_size, state->opt.parity_limit_size);
				if (ret == -1) {
					log_tag("parity_%s:%" PRIu64 ":%s: Open error. %s.\n", es(errno), blockmax, lev_config_name(l), strerror(errno));
					if (is_hw(errno)) {
						log_fatal_errno(errno, lev_config_name(l));
						exit(EXIT_FAILURE);
					}

					/* continue anyway */
					parity_ptr[l] = 0;
				}
			} else {
				/* open for writing */
				ret = parity_create(parity_ptr[l], &state->parity[l], l, state->file_mode, state->block_size, state->opt.parity_limit_size);
				if (ret == -1) {
					/* LCOV_EXCL_START */
					log_tag("parity_%s:%u:%s: Create error. %s.\n", es(errno), 0, lev_config_name(l), strerror(errno));
					log_fatal_errno(errno, lev_config_name(l));
					exit(EXIT_FAILURE);
					/* LCOV_EXCL_STOP */
				}

				ret = parity_chsize(parity_ptr[l], &state->parity[l], 0, size, state->block_size, state->opt.skip_fallocate, state->opt.skip_space_holder);
				if (ret == -1) {
					/* LCOV_EXCL_START */
					log_tag("parity_%s:%u:%s: Create error. %s.\n", es(errno), 0, lev_config_name(l), strerror(errno));
					log_fatal_errno(errno, lev_config_name(l));
					exit(EXIT_FAILURE);
					/* LCOV_EXCL_STOP */
				}
			}
		}
	} else if (!state->opt.auditonly) {
		/*
		 * If checking, open the file for reading
		 * it may fail if the file doesn't exist, in this case we continue to check the files
		 */
		for (l = 0; l < state->level; ++l) {
			parity_ptr[l] = &parity[l];
			ret = parity_open(parity_ptr[l], &state->parity[l], l, state->file_mode, state->block_size, state->opt.parity_limit_size);
			if (ret == -1) {
				log_tag("parity_%s:%" PRIu64 ":%s: Open error. %s.\n", es(errno), blockmax, lev_config_name(l), strerror(errno));
				if (is_hw(errno)) {
					log_fatal_errno(errno, lev_config_name(l));
					exit(EXIT_FAILURE);
				}

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

	process_error = 0;

	/* skip degenerated cases of empty parity, or skipping all */
	if (blockstart < blockmax) {
		ret = state_check_process(state, fix, parity_ptr, blockstart, blockmax, partial);
		if (ret == -1) {
			/* LCOV_EXCL_START */
			++process_error;
			/* continue, as we are already exiting */
			/* LCOV_EXCL_STOP */
		}
	}

	/* try to close only if opened */
	for (l = 0; l < state->level; ++l) {
		if (parity_ptr[l]) {
			/* if fixing and not excluded, truncate parity to physical_reach_size */
			if (fix && !state->parity[l].is_excluded_by_filter) {
				ret = parity_truncate(parity_ptr[l]);
				if (ret == -1) {
					/* LCOV_EXCL_START */
					log_tag("parity_%s:%" PRIu64 ":%s: Truncate error. %s.\n", es(errno), blockmax, lev_config_name(l), strerror(errno));
					log_fatal_errno(errno, lev_config_name(l));

					++process_error;
					/* continue, as we are already exiting */
					/* LCOV_EXCL_STOP */
				}
			}

			ret = parity_close(parity_ptr[l]);
			if (ret == -1) {
				/* LCOV_EXCL_START */
				log_tag("parity_%s:%" PRIu64 ":%s: Close error. %s.\n", es(errno), blockmax, lev_config_name(l), strerror(errno));
				log_fatal_errno(errno, lev_config_name(l));

				++process_error;
				/* continue, as we are already exiting */
				/* LCOV_EXCL_STOP */
			}
		}
	}

	if (process_error != 0)
		return -1;

	msg_status("Everything OK\n");

	return 0;
}

