// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2011 Andrea Mazzoleni

#include "os/portable.h"

#include "support.h"
#include "elem.h"
#include "state.h"
#include "parity.h"

static const char* es(int err)
{
	if (is_hw(err))
		return "error_io";
	else
		return "error";
}

/**
 * If the file metadata (size and timestamp) is known to be synchronized with disk.
 *
 * Always set on POSIX where stat()/lstat() returns coherent metadata. On Windows NTFS,
 * directory enumeration may return stale size/mtime for hardlinks until synchronized
 * via lstat_sync() (GetFileInformationByHandle).
 */
#define DISCOVERY_IS_SCAN_SYNCED 0x1

/**
 * If the file is the canonical representative among hardlinks sharing the same inode.
 *
 * During Phase 1, when multiple paths share an inode, one is deterministically chosen
 * as canonical (via discovery_canonical_better). In Phase 2, canonical files are applied
 * first to create or preserve the primary snapraid_file, while non-canonical entries
 * are subsequently applied as hardlink references (snapraid_link).
 */
#define DISCOVERY_IS_CANONICAL 0x2

/**
 * Temporary metadata collected during filesystem discovery.
 */
struct snapraid_discovery {
	data_off_t size; /**< Size discovered on disk. */
	int64_t mtime_sec; /**< mtime sec discovered on disk. */
	uint64_t inode; /**< Inode discovered on disk. */
	tommy_node nodelist; /**< Node for scan->file_discovery_list. */
	tommy_node nodeset; /**< Node for scan->file_discovery_inodeset. */
	int mtime_nsec; /**< mtime nsec discovered on disk. */
	unsigned flag; /**< Transient discovery flags (DISCOVERY_IS_*). */
	char sub[1]; /**< Sub path of the file. Without the disk dir. The disk is implicit. */
};

struct snapraid_scan {
	struct snapraid_state* state; /**< State used. */
	struct snapraid_disk* disk; /**< Disk used. */
	thread_id_t thread; /**< Thread used for scanning the disk */

	int is_diff; /**< If it's a diff command or a scanning */
	int need_write; /**< If a state write is required */

	/**
	 * Counters of changes.
	 */
	unsigned count_equal; /**< Files equal. */
	unsigned count_move; /**< Files with a different name, but equal inode, size and timestamp in the same disk. */
	unsigned count_restore; /**< Files with equal name, size and timestamp, but different inode. */
	unsigned count_change; /**< Files with same name, but different size and/or timestamp. */
	unsigned count_copy; /**< Files new, with same name size and timestamp of a file in a different disk. */
	unsigned count_relocate; /**< Like copy, but with the original disappeared. */
	unsigned count_insert; /**< Files new. */
	unsigned count_remove; /**< Files removed. */

	tommy_list file_discovery_list; /**< Discovered regular files for Apply phase. */
	tommy_hashdyn file_discovery_inodeset; /**< Inode set for hardlink collision detection during Discovery. */
	tommy_list file_insert_list; /**< Files to insert. */
	tommy_list link_insert_list; /**< Links to insert. */
	tommy_list dir_insert_list; /**< Dirs to insert. */
	tommy_list local_filter_list; /**< Filter list specific for the disk. */
	struct fsidentity dir_identity; /**< Effective scan root identity. */

	/* nodes for data structures */
	tommy_node node;
};

static struct snapraid_discovery* discovery_alloc(const char* sub, struct stat* st)
{
	struct snapraid_discovery* disc;
	size_t sub_len = strlen(sub);

	disc = malloc_nofail(sizeof(struct snapraid_discovery) + sub_len);
	disc->size = st->st_size;
	disc->mtime_sec = st->st_mtime;
	disc->mtime_nsec = STAT_NSEC(st);
	disc->inode = st->st_ino;
	disc->flag = 0;
	memcpy(disc->sub, sub, sub_len + 1);

#if HAVE_LSTAT_SYNC
	if (st->st_sync != 0) {
		disc->flag |= DISCOVERY_IS_SCAN_SYNCED;
	}
#else
	disc->flag |= DISCOVERY_IS_SCAN_SYNCED;
#endif

	return disc;
}

static void discovery_free(void* void_disc)
{
	struct snapraid_discovery* disc = void_disc;

	free(disc);
}

static struct snapraid_scan* scan_alloc(struct snapraid_state* state, struct snapraid_disk* disk, int is_diff)
{
	struct snapraid_scan* scan;

	scan = malloc_nofail(sizeof(struct snapraid_scan));
	scan->state = state;
	scan->disk = disk;
	scan->count_equal = 0;
	scan->count_move = 0;
	scan->count_copy = 0;
	scan->count_relocate = 0;
	scan->count_restore = 0;
	scan->count_change = 0;
	scan->count_remove = 0;
	scan->count_insert = 0;
	tommy_list_init(&scan->file_discovery_list);
	tommy_hashdyn_init(&scan->file_discovery_inodeset);
	tommy_list_init(&scan->file_insert_list);
	tommy_list_init(&scan->link_insert_list);
	tommy_list_init(&scan->dir_insert_list);
	tommy_list_init(&scan->local_filter_list);
	scan->dir_identity.device = 0;
	scan->dir_identity.mnt_id = 0;
	scan->dir_identity.subvol = 0;
	scan->is_diff = is_diff;
	scan->need_write = 0;

	return scan;
}

static void scan_free(void* void_scan)
{
	struct snapraid_scan* scan = void_scan;
	tommy_hashdyn_done(&scan->file_discovery_inodeset);
	tommy_list_foreach(&scan->file_discovery_list, discovery_free);
	tommy_list_foreach(&scan->local_filter_list, filter_free);
	free(scan);
}

/**
 * Remove the specified link from the data set.
 */
static void scan_link_remove(struct snapraid_scan* scan, struct snapraid_link* slink)
{
	struct snapraid_disk* disk = scan->disk;

	/* state changed */
	scan->need_write = 1;

	/* remove the file from the link containers */
	tommy_hashdyn_remove_existing(&disk->linkset, &slink->nodeset);
	tommy_list_remove_existing(&disk->linklist, &slink->nodelist);

	/* deallocate */
	link_free(slink);
}

/**
 * Insert the specified link in the data set.
 */
static void scan_link_insert(struct snapraid_scan* scan, struct snapraid_link* slink)
{
	struct snapraid_disk* disk = scan->disk;

	/* state changed */
	scan->need_write = 1;

	/* insert the link in the link containers */
	tommy_hashdyn_insert(&disk->linkset, &slink->nodeset, slink, link_name_hash(slink->sub));
	tommy_list_insert_tail(&disk->linklist, &slink->nodelist, slink);
}

/**
 * Process a symbolic link.
 */
static void scan_link(struct snapraid_scan* scan, int is_diff, const char* sub, const char* linkto, unsigned link_flag)
{
	struct snapraid_state* state = scan->state;
	struct snapraid_disk* disk = scan->disk;
	struct snapraid_link* slink;
	/* check if the link already exists */
	slink = tommy_hashdyn_search(&disk->linkset, link_name_compare_to_arg, sub, link_name_hash(sub));
	if (slink) {
		/* check if multiple files have the same name */
		if (link_flag_has(slink, FILE_IS_PRESENT)) {
			/* LCOV_EXCL_START */
			log_fatal(EINTERNAL, "Internal inconsistency for link '%s%s'\n", disk->dir, sub);
			os_abort();
			/* LCOV_EXCL_STOP */
		}

		/* mark as present */
		link_flag_set(slink, FILE_IS_PRESENT);

		/* check if the link is not changed and it's of the same kind */
		if (strcmp(slink->linkto, linkto) == 0 && link_flag == link_flag_get(slink, FILE_IS_LINK_MASK)) {
			/* it's equal */
			++scan->count_equal;

			if (state->opt.gui_verbose) {
				log_tag("scan:equal:%s:%s\n", disk->name, esc_tag(slink->sub));
			}
		} else {
			/* it's an update */

			/* we have to save the linkto/type */
			scan->need_write = 1;

			++scan->count_change;

			log_tag("scan:update:%s:%s\n", disk->name, esc_tag(slink->sub));
			if (is_diff) {
				msg_info("update %s\n", fmt_term(disk, slink->sub));
			}

			/* update it */
			free(slink->linkto);
			slink->linkto = strdup_nofail(linkto);
			link_flag_let(slink, link_flag, FILE_IS_LINK_MASK);
		}

		/* nothing more to do */
		return;
	} else {
		/* create the new link */
		++scan->count_insert;

		log_tag("scan:add:%s:%s\n", disk->name, esc_tag(sub));
		if (is_diff) {
			msg_info("add %s\n", fmt_term(disk, sub));
		}

		/* and continue to insert it */
	}

	/* insert it */
	slink = link_alloc(sub, linkto, link_flag);

	/* mark it as present */
	link_flag_set(slink, FILE_IS_PRESENT);

	/* insert it in the delayed insert list */
	tommy_list_insert_tail(&scan->link_insert_list, &slink->nodelist, slink);
}

/**
 * Insert the specified file in the parity.
 */
static void scan_file_allocate(struct snapraid_scan* scan, struct snapraid_file* file)
{
	struct snapraid_state* state = scan->state;
	struct snapraid_disk* disk = scan->disk;
	block_off_t i;
	block_off_t parity_pos;

	/* state changed */
	scan->need_write = 1;

	/* allocate the blocks of the file */
	parity_pos = disk->first_free_block;
	for (i = 0; i < file->blockmax; ++i) {
		struct snapraid_block* block;
		struct snapraid_block* over_block;
		snapraid_info info;

		/* increment the position until the first really free block */
		while (block_has_file(fs_par2block_find(disk, parity_pos)))
			++parity_pos;

		/* get block we are going to overwrite, if any */
		over_block = fs_par2block_find(disk, parity_pos);

		/* deallocate it */
		if (over_block != BLOCK_NULL)
			fs_deallocate(disk, parity_pos);

		/* get block specific info */
		info = info_get(&state->infoarr, parity_pos);

		/* get the new block we are going to write */
		block = fs_file2block_get(file, i);

		/* if the file block already has an updated hash without rehash */
		if (block_has_updated_hash(block) && !info_get_rehash(info)) {
			/* the only possible case is for REP blocks */
			assert(block_state_get(block) == BLOCK_STATE_REP);

			/* convert to a REP block */
			block_state_set(block, BLOCK_STATE_REP);

			/* and keep the hash as it's */
		} else {
			unsigned over_state;

			/* convert to a CHG block */
			block_state_set(block, BLOCK_STATE_CHG);

			/* state of the block we are going to overwrite */
			over_state = block_state_get(over_block);

			/* if the block is an empty one */
			if (over_state == BLOCK_STATE_EMPTY) {
				/*
				 * The block was empty and filled with zeros
				 * set the hash to the special ZERO value
				 */
				hash_zero_set(block->hash);
			} else {
				/* otherwise it's a DELETED one */
				assert(over_state == BLOCK_STATE_DELETED);

				/*
				 * In this cases we don't know if the old state is still the one
				 * stored inside the parity, because after an aborted sync, the parity
				 * may be or may be not have been updated with the data that it's now
				 * deleted.
				 *
				 * We anyway keep the hash as the new sync will not assume it correct,
				 * but the check/fix may be using it.
				 *
				 * For example:
				 * - One file is deleted
				 * - Sync aborted after updating the parity to the new state,
				 *   but without saving the content file representing this new state.
				 * - Another file is added again (exactly here)
				 *   with the hash of DELETED block not representing the real parity state
				 */

				/* copy the past hash of the block */
				hash_copy(block->hash, over_block->hash);
			}
		}

		/* store in the disk map, after invalidating all the other blocks */
		fs_allocate(disk, parity_pos, file, i, 1);

		/* set the new free position */
		disk->first_free_block = parity_pos + 1;
	}

	/* insert in the list of contained files */
	tommy_list_insert_tail(&disk->filelist, &file->nodelist, file);
}

/**
 * Delete the specified file from the parity.
 *
 * Note that the parity remains allocated, but the blocks and the file are marked as DELETED.
 *
 * The file pointer remain in memory and it's inserted in the deleted set.
 *
 * It should not be deallocated, as the parity still references it, and other threads
 * in the scanning process may reference it when searching for a copy in the "stampset".
 */
static void scan_file_deallocate(struct snapraid_scan* scan, struct snapraid_file* file)
{
	struct snapraid_disk* disk = scan->disk;
	block_off_t i;

	/* remove from the list of contained files */
	tommy_list_remove_existing(&disk->filelist, &file->nodelist);

	/* state changed */
	scan->need_write = 1;

	/*
	 * Here we are supposed to adjust the ::first_free_block position
	 * with the parity position we are deleting
	 * but we also know that we do only delayed insert, after all the deletion,
	 * so at this point ::first_free_block is always at 0, and we don't need to update it
	 */
	if (disk->first_free_block != 0) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "Internal inconsistency for first free position at '%" PRIu64 "' deallocating file '%s'\n", disk->first_free_block, file->sub);
		os_abort();
		/* LCOV_EXCL_STOP */
	}

	/* free all the blocks of the file */
	for (i = 0; i < file->blockmax; ++i) {
		struct snapraid_block* block = fs_file2block_get(file, i);
		unsigned block_state;

		/*
		 * In case we scan after an aborted sync,
		 * we could get also intermediate states
		 */
		block_state = block_state_get(block);
		switch (block_state) {
		case BLOCK_STATE_BLK :
			/* we keep the hash making it an "old" hash, because the parity is still containing data for it */
			break;
		case BLOCK_STATE_REBUILD :
			/*
			 * The stored hash describes the current file contents and therefore
			 * becomes a valid OLD hash after deletion.
			 *
			 * Physical parity was already untrusted, but DELETED already represents
			 * that condition conservatively.
			 */
			break;
		case BLOCK_STATE_CHG :
			/*
			 * In this cases we don't know if the old state is still the one
			 * stored inside the parity, because after an aborted sync, the parity
			 * may be or may be not have been updated with the data that it's now
			 * deleted.
			 *
			 * We anyway keep the hash as the new sync will not assume it correct,
			 * but the check/fix may be using it.
			 *
			 * For example:
			 * - One file is added
			 * - Sync aborted after updating the parity to the new state,
			 *   but without saving the content file representing this new state.
			 * - File is now deleted after the aborted sync
			 * - Sync again, deleting the blocks (exactly here)
			 *   with the hash of CHG block not representing the real parity state
			 */
			break;
		case BLOCK_STATE_REP :
			/* we just don't know the old hash, and then we set it to invalid */
			hash_invalid_set(block->hash);
			break;
		default :
			/* LCOV_EXCL_START */
			log_fatal(EINTERNAL, "Internal inconsistency in file '%s' deallocating block '%" PRIu64 ":%" PRIu64 "' state %u\n", file->sub, i, file->blockmax, block_state);
			os_abort();
			/* LCOV_EXCL_STOP */
		}

		/* set the block as deleted */
		block_state_set(block, BLOCK_STATE_DELETED);
	}

	/* mark the file as deleted */
	file_flag_set(file, FILE_IS_DELETED);

	/* insert it in the list of deleted blocks */
	tommy_list_insert_tail(&disk->deletedlist, &file->nodelist, file);
}

static void scan_file_delayed_allocate(struct snapraid_scan* scan, struct snapraid_file* file)
{
	/* insert in the delayed allocation list */
	tommy_list_insert_tail(&scan->file_insert_list, &file->nodelist, file);
}

/**
 * Check if a file is completely formed of blocks with reallocatable parity,
 * and no rehash is tagged, and if it has at least one block.
 */
static int file_is_full_reallocatable_and_stable(struct snapraid_state* state, struct snapraid_disk* disk, struct snapraid_file* file)
{
	block_off_t i;

	/* with no block, it never has reallocatable parity */
	if (file->blockmax == 0)
		return 0;

	/* check all blocks */
	for (i = 0; i < file->blockmax; ++i) {
		snapraid_info info;
		struct snapraid_block* block = fs_file2block_get(file, i);
		block_off_t parity_pos;

		/* exclude blocks without reallocatable parity */
		if (!block_has_reallocatable_parity(block))
			return 0;

		/*
		 * Get the parity position.
		 *
		 * Note that here we expect to always have mapped
		 * parity, because kept files always have it.
		 *
		 * Anyway, checking for POS_NULL doesn't hurt.
		 */
		parity_pos = fs_file2par_find(disk, file, i);

		/* if it's not mapped, it cannot have rehash */
		if (parity_pos != POS_NULL) {
			/* get block specific info */
			info = info_get(&state->infoarr, parity_pos);

			/* if rehash fails */
			if (info_get_rehash(info))
				return 0;
		}
	}

	return 1;
}

/**
 * Check if a file is completely formed of blocks with an updated hash,
 * and no rehash is tagged, and if it has at least one block.
 */
static int file_is_full_hashed_and_stable(struct snapraid_state* state, struct snapraid_disk* disk, struct snapraid_file* file)
{
	block_off_t i;

	/* with no block, it never has a hash */
	if (file->blockmax == 0)
		return 0;

	/* check all blocks */
	for (i = 0; i < file->blockmax; ++i) {
		snapraid_info info;
		struct snapraid_block* block = fs_file2block_get(file, i);
		block_off_t parity_pos;

		/* exclude blocks without hash */
		if (!block_has_updated_hash(block))
			return 0;

		/*
		 * Get the parity position.
		 *
		 * Note that it's possible to have files
		 * not mapped into the parity, even if they
		 * have a valid hash.
		 *
		 * This happens for example, for 'copied' files
		 * that have REP blocks, but not yet mapped.
		 *
		 * If there are multiple copies, it's also possible
		 * that such files are used as 'source' to copy
		 * hashes, and then to get them inside this function.
		 */
		parity_pos = fs_file2par_find(disk, file, i);

		/* if it's not mapped, it cannot have rehash */
		if (parity_pos != POS_NULL) {
			/* get block specific info */
			info = info_get(&state->infoarr, parity_pos);

			/* exclude blocks needing a rehash */
			if (info_get_rehash(info))
				return 0;
		}
	}

	return 1;
}

static int discovery_inode_compare_to_arg(const void* void_arg, const void* void_data)
{
	const uint64_t* arg = void_arg;
	const struct snapraid_discovery* disc = void_data;

	if (*arg < disc->inode)
		return -1;
	if (*arg > disc->inode)
		return 1;
	return 0;
}


/**
 * Insert the file in the inode set.
 */
static void scan_file_inode_insert(struct snapraid_scan* scan, struct snapraid_file* file)
{
	struct snapraid_disk* disk = scan->disk;

	if (file->inode != INODE_INVALID)
		tommy_hashdyn_insert(&disk->inodeset, &file->nodeset, file, file_inode_hash(file->inode));
}

/**
 * Insert the file in the path and stamp sets.
 */
static void scan_file_stamp_insert(struct snapraid_scan* scan, struct snapraid_file* file)
{
	struct snapraid_disk* disk = scan->disk;

	tommy_hashdyn_insert(&disk->pathset, &file->pathset, file, file_path_hash(file->sub));
	tommy_hashdyn_insert(&disk->stampset, &file->stampset, file, file_stamp_hash(file->size, file->mtime_sec, file->mtime_nsec));
}

/**
 * Remove the file from the data set.
 *
 * File is then deleted.
 */
static void scan_file_remove(struct snapraid_scan* scan, struct snapraid_file* file, int keep_track)
{
	struct snapraid_disk* disk = scan->disk;

	/* remove the file from the containers */
	if (file->inode != INODE_INVALID)
		tommy_hashdyn_remove_existing(&disk->inodeset, &file->nodeset);

	tommy_hashdyn_remove_existing(&disk->pathset, &file->pathset);
	tommy_hashdyn_remove_existing(&disk->stampset, &file->stampset);

	/*
	 * Keep track of the removed file (that won't be added later)
	 *
	 * This allows to record from where the parity was computed.
	 */
	if (keep_track) {
		struct snapraid_dealloc* dealloc = dealloc_alloc(scan->state->block_size, file->sub, file->size, file->mtime_sec, file->mtime_nsec);

		dealloc_import(dealloc, file);

		/* insert the dealloc in the dealloc containers */
		tommy_list_insert_tail(&disk->dealloclist, &dealloc->nodelist, dealloc);
	}

	/*
	 * Deallocate the file from the parity.
	 *
	 * This is safe to run unlocked because:
	 * 1. Modified files deallocations are deferred to Phase 3 (mono-threaded phase).
	 * 2. Invalid parity files deallocated in Phase 2 are never selected by
	 *    file_is_full_hashed_and_stable() during copy-detection (since they lack
	 *    valid parity blocks).
	 */
	scan_file_deallocate(scan, file);
}

/**
 * Keep the file as it's (or with only a name/inode modification).
 *
 * If the file is kept, nothing has to be done.
 *
 * But if a file is fully reallocatable, it's reallocated to ensure
 * to always minimize the space used in the parity.
 *
 * This could happen after a failed sync, when some other files are deleted,
 * and then new ones can be moved backward to fill the hole created.
 */
static void scan_file_keep(struct snapraid_scan* scan, struct snapraid_file* file)
{
	struct snapraid_disk* disk = scan->disk;

	/* if the file is fully reallocatable, schedule a reinsert at later stage */
	if (file_is_full_reallocatable_and_stable(scan->state, disk, file)) {

		struct snapraid_file* copy = file_dup(file);
		file_flag_set(copy, FILE_IS_REALLOC_NEW);

		/* insert in the delayed allocation list */
		scan_file_delayed_allocate(scan, copy);

		/* mark the file to be removed on Phase 3 */
		file_flag_set(file, FILE_IS_REALLOC_OLD);
	}
}

/**
 * Process and classify a discovered file in Phase 2.
 *
 * Invariants maintained during application:
 * - Equal, moved, or restored files are marked FILE_IS_PRESENT and keep their allocations.
 * - For every valid inode encountered, disk->inodeset points to the current
 *   FILE_IS_PRESENT file. Later paths with the same inode are represented as hardlinks.
 * - Files with INODE_INVALID are never inserted into inodeset. Inode validity
 *   does not change when their path/stamp insertion is scheduled.
 * - A new normal file is immediately inserted into all applicable
 *   inode/path/stamp sets and queued for delayed parity allocation in Phase 4.
 * - For a modified file, FILE_IS_MODIFIED_OLD has INODE_INVALID and is no
 *   longer in inodeset, but remains in pathset/stampset and filelist.
 *   FILE_IS_MODIFIED_NEW is in inodeset only if its inode is valid, has no
 *   path/stamp entry, and is queued for Phase 4.
 * - For a reallocated unchanged file, FILE_IS_REALLOC_OLD remains in all its
 *   original containers. FILE_IS_REALLOC_NEW is only queued for Phase 4 and
 *   is not inserted into any file container yet.
 */
static void scan_file_apply(void* void_scan, void* void_disc)
{
	struct snapraid_scan* scan = void_scan;
	struct snapraid_discovery* disc = void_disc;
	struct snapraid_state* state = scan->state;
	struct snapraid_disk* disk = scan->disk;
	int is_diff = scan->is_diff;
	struct snapraid_file* file;
	block_off_t block_pos;
	tommy_node* i;
	int is_original_file_size_different_than_zero;
	int is_file_already_present;
	data_off_t file_already_present_size;
	int64_t file_already_present_mtime_sec;
	int file_already_present_mtime_nsec;
	int is_file_reported;
	int is_file_modified;
	const char* sub = disc->sub;
	uint64_t inode = disc->inode;
	data_off_t size = disc->size;
	int64_t mtime_sec = disc->mtime_sec;
	int mtime_nsec = disc->mtime_nsec;
	/*
	 * If the disk has persistent inodes and UUID, try a search on the past inodes,
	 * to detect moved files.
	 *
	 * For persistent inodes we mean inodes that keep their values when the file-system
	 * is unmounted and remounted. This don't always happen.
	 *
	 * Cases found are:
	 * - Linux FUSE with exFAT driver from https://code.google.com/p/exfat/.
	 *   Inodes are reassigned at every mount restarting from 1 and incrementing.
	 *   As worse, the exFAT support in FUSE doesn't use sub-second precision in timestamps
	 *   making inode collision more easy (exFAT by design supports 10ms precision).
	 * - Linux VFAT kernel (3.2) driver. Inodes are fully reassigned at every mount.
	 *
	 * In such cases, to avoid possible random collisions, it's better to disable the moved
	 * file recognition.
	 *
	 * For persistent UUID we mean that it has the same UUID as before.
	 * Otherwise, if the UUID is changed, likely it's a new recreated file-system,
	 * and then the inode have no meaning.
	 *
	 * Note that to disable the search by past inode, we do this implicitly
	 * removing all the past inode before searching for files.
	 * This ensures that no file is found with a past inode, but at the same time,
	 * it allows to find new files with the same inode, to identify them as hardlinks.
	 */
	int has_past_inodes = !disk->has_volatile_inodes && !disk->has_different_uuid && !disk->has_unsupported_uuid;

	/*
	 * Always search with the new inode, in the all new inodes found until now,
	 * with the eventual presence of also the past inodes
	 */
	if (inode != INODE_INVALID)
		file = tommy_hashdyn_search(&disk->inodeset, file_inode_compare_to_arg, &inode, file_inode_hash(inode));
	else
		file = 0;

	/* identify moved files with past inodes and hardlinks with the new inodes */
	if (file) {
		/* check if the file is not changed */
		if (file->size == size
			&& file->mtime_sec == mtime_sec
			&& file->mtime_nsec == mtime_nsec
		) {
			/* check if multiple files have the same inode */
			if (file_flag_has(file, FILE_IS_PRESENT)) {
				/* it's a hardlink */
				scan_link(scan, is_diff, sub, file->sub, FILE_IS_HARDLINK);
				return;
			}

			/* mark as present */
			file_flag_set(file, FILE_IS_PRESENT);

			/* this old file was physically found during Discovery, possibly under a new pathname */
			file_flag_set(file, FILE_IS_DISCOVERED);

			if (strcmp(file->sub, sub) != 0) {
				/* if the path is different, it means a moved file with the same inode */
				++scan->count_move;

				log_tag("scan:move:%s:%s:%s\n", disk->name, esc_tag(file->sub), esc_tag(sub));
				if (is_diff) {
					msg_info("move %s -> %s\n", fmt_term(disk, file->sub), fmt_term(disk, sub));
				}

				/* remove from the name set */
				tommy_hashdyn_remove_existing(&disk->pathset, &file->pathset);

				/* rename the file (safe without locks in serialized apply) */
				file_rename(file, sub);

				/* reinsert in the name set */
				tommy_hashdyn_insert(&disk->pathset, &file->pathset, file, file_path_hash(file->sub));

				/* we have to save the new name */
				scan->need_write = 1;
			} else {
				/* otherwise it's equal */
				++scan->count_equal;

				if (state->opt.gui_verbose) {
					log_tag("scan:equal:%s:%s\n", disk->name, esc_tag(file->sub));
				}
			}

			/* mark the file as kept */
			scan_file_keep(scan, file);

			/* nothing more to do */
			return;
		}

		/*
		 * Here the file matches the inode, but not the other info
		 *
		 * It could be a modified file with the same name,
		 * or a restored/copied file that get assigned a previously used inode,
		 * or a file-system with not persistent inodes.
		 *
		 * In NTFS it could be also a hardlink, because in NTFS
		 * hardlink don't share the same directory information,
		 * like attribute and time.
		 *
		 * For example:
		 *   C:> echo A > A
		 *   C:> mklink /H B A
		 *   ...wait one minute
		 *   C:> echo AAAAAAAAAAAAAA > A
		 *   C:> dir
		 *   ...both time and size of A and B don't match!
		 */
		if (file_flag_has(file, FILE_IS_PRESENT)) {
			/* LCOV_EXCL_START */
			/* suppose it's hardlink with not synced metadata */
			scan_link(scan, is_diff, sub, file->sub, FILE_IS_HARDLINK);
			return;
			/* LCOV_EXCL_STOP */
		}

		/*
		 * Assume a previously used inode, it's the worst case
		 * and we handle it removing the duplicate stored inode.
		 * If the file is found by name later, it will have the inode restored,
		 * otherwise, it will get removed
		 */

		/* here the inode must be present */
		if (file->inode == INODE_INVALID) {
			/* LCOV_EXCL_START */
			log_fatal(EINTERNAL, "Internal inconsistency in inode for file '%s%s' as unexpected missing\n", disk->dir, sub);
			os_abort();
			/* LCOV_EXCL_STOP */
		}

		/* remove from the inode set */
		tommy_hashdyn_remove_existing(&disk->inodeset, &file->nodeset);

		/* clear the inode */
		file->inode = INODE_INVALID;

		/* go further to find it by name */
	}

	/* initialize for later overwrite */
	is_file_reported = 0;
	is_file_modified = 0;
	is_original_file_size_different_than_zero = 0;

	/* then try finding it by name */
	file = tommy_hashdyn_search(&disk->pathset, file_path_compare_to_arg, sub, file_path_hash(sub));

	/* keep track if the file already exists */
	is_file_already_present = file != 0;

	if (is_file_already_present) {
		/* if the file is without an inode */
		if (file->inode == INODE_INVALID) {
			/* set it now */
			file->inode = inode;

			/* insert in the set if valid */
			if (file->inode != INODE_INVALID)
				tommy_hashdyn_insert(&disk->inodeset, &file->nodeset, file, file_inode_hash(file->inode));
		} else {
			/* here the inode has to be different, otherwise we would have found it before */
			if (file->inode == inode) {
				/* LCOV_EXCL_START */
				log_fatal(EINTERNAL, "Internal inconsistency in inode '%" PRIu64 "' for file '%s%s' as unexpected matching\n", file->inode, disk->dir, sub);
				os_abort();
				/* LCOV_EXCL_STOP */
			}
		}

		/* for sure it cannot be already present */
		if (file_flag_has(file, FILE_IS_PRESENT)) {
			/* LCOV_EXCL_START */
			log_fatal(EINTERNAL, "Internal inconsistency in path for file '%s%s' matching and already present\n", disk->dir, sub);
			os_abort();
			/* LCOV_EXCL_STOP */
		}

		/* check if the file is not changed */
		if (file->size == size
			&& file->mtime_sec == mtime_sec
			&& file->mtime_nsec == mtime_nsec
		) {
			/* mark as present */
			file_flag_set(file, FILE_IS_PRESENT);

			/* if when processing the disk we used the past inodes values */
			if (has_past_inodes && file->inode != INODE_INVALID && inode != INODE_INVALID) {
				/*
				 * If persistent inodes are supported, we are sure that the inode number
				 * is now different, because otherwise the file would have been found
				 * when searching by inode.
				 * if the inode is different, it means a rewritten file with the same path
				 * like when restoring a backup that restores also the timestamp
				 */
				++scan->count_restore;

				log_tag("scan:restore:%s:%s\n", disk->name, esc_tag(sub));
				if (is_diff) {
					msg_info("restore %s\n", fmt_term(disk, sub));
				}

				/* remove from the inode set */
				tommy_hashdyn_remove_existing(&disk->inodeset, &file->nodeset);

				/* save the new inode */
				file->inode = inode;

				/* reinsert in the inode set */
				tommy_hashdyn_insert(&disk->inodeset, &file->nodeset, file, file_inode_hash(file->inode));

				/* we have to save the new inode */
				scan->need_write = 1;
			} else {
				/*
				 * Otherwise it's the case of not persistent inode, where doesn't
				 * matter if the inode is different or equal, because they have no
				 * meaning, and then we don't even save them
				 */
				++scan->count_equal;

				if (state->opt.gui_verbose) {
					log_tag("scan:equal:%s:%s\n", disk->name, esc_tag(file->sub));
				}
			}

			/* mark the file as kept */
			scan_file_keep(scan, file);

			/* nothing more to do */
			return;
		}

		/* here if the file is changed but with the correct name */

		/* save the info for later printout */
		file_already_present_size = file->size;
		file_already_present_mtime_sec = file->mtime_sec;
		file_already_present_mtime_nsec = file->mtime_nsec;

		/* keep track if the original file was not of zero size */
		is_original_file_size_different_than_zero = file->size != 0;

		/* the old version doesn't represent the current inode anymore */
		if (file->inode != INODE_INVALID) {
			tommy_hashdyn_remove_existing(&disk->inodeset, &file->nodeset);
			file->inode = INODE_INVALID;
		}

		/* mark it as to be removed in Phase 3 */
		file_flag_set(file, FILE_IS_MODIFIED_OLD);

		/* flag the modified version to defer path/stamp insertion to Phase 4 */
		is_file_modified = 1;

		/* and continue to insert it again */
	} else {
		file_already_present_size = 0;
		file_already_present_mtime_sec = 0;
		file_already_present_mtime_nsec = 0;
	}

#ifndef _WIN32
	/*
	 * Do a safety check to ensure that the common ext4 case of zeroing
	 * the size of a file after a crash doesn't propagate to the backup
	 * this check is specific for Linux, so we disable it on Windows
	 */
	if (is_original_file_size_different_than_zero && disc->size == 0) {
		if (!state->opt.force_zero) {
			/* LCOV_EXCL_START */
			log_error(ESOFT, "The file '%s%s' has unexpected zero size!\n", disk->mount_point, sub);
			log_error(ESOFT, "It's possible that after a kernel crash this file was lost,\n");
			log_error(ESOFT, "and you can use 'snapraid fix -f /%s' to recover it.\n", fmt_poll(disk, sub));
			if (!is_diff) {
				log_fatal(ESOFT, "If this an expected condition you can '%s' anyway using 'snapraid --force-zero %s'\n", state->command, state->command);
				exit(EXIT_FAILURE);
			}
			/* LCOV_EXCL_STOP */
		}
	}
#else
	/* avoid the unused warning in Windows */
	(void)is_original_file_size_different_than_zero;
#endif

	/* insert it */
	file = file_alloc(state->block_size, sub, disc->size, disc->mtime_sec, disc->mtime_nsec, disc->inode);

	/*
	 * Copy detection recognizes reused hashes from REP blocks before parity
	 * allocation. Initialize new scan blocks to CHG so uninitialized state can
	 * never be mistaken for a reusable hash.
	 */
	for (block_pos = 0; block_pos < file->blockmax; ++block_pos) {
		struct snapraid_block* block = fs_file2block_get(file, block_pos);

		block_state_set(block, BLOCK_STATE_CHG);
		hash_invalid_set(block->hash);
	}

	/* mark it as present and physically discovered during Phase 1 */
	file_flag_set(file, FILE_IS_PRESENT);
	file_flag_set(file, FILE_IS_DISCOVERED);

	/*
	 * If copy detection is enabled
	 * note that the copy detection is tried also for updated files
	 * this makes sense because it may happen to have two different copies
	 * of the same file, and we move the right one over the wrong one
	 * in such case we have a "copy" over an "update"
	 */
	if (!state->opt.force_nocopy) {
		tommy_uint32_t hash = file_stamp_hash(file->size, file->mtime_sec, file->mtime_nsec);

		/* search for a file with the same name and stamp in all the disks */
		for (i = state->disklist; i != 0; i = i->next) {
			struct snapraid_disk* other_disk = i->data;
			struct snapraid_file* other_file;

			/*
			 * If the nanosecond part of the time stamp is valid, search
			 * for name and stamp, otherwise for path and stamp
			 */
			if (file->mtime_nsec != 0 && file->mtime_nsec != STAT_NSEC_INVALID)
				other_file = tommy_hashdyn_search(&other_disk->stampset, file_namestamp_compare, file, hash);
			else
				other_file = tommy_hashdyn_search(&other_disk->stampset, file_pathstamp_compare, file, hash);

			/*
			 * If found, check stability and copy the hash.
			 *
			 * This is safe to execute without locks because:
			 * 1. Modified files deallocations are deferred to Phase 3.
			 * 2. file_is_full_reallocatable_and_stable() only accepts files with complete
			 *    reallocatable parity, preventing them from being selected by file_is_full_hashed_and_stable().
			 */
			if (other_file && file_is_full_hashed_and_stable(scan->state, other_disk, other_file)) {
				file_flag_set(other_file, FILE_IS_RELOCATED);

				/* assume that the file is a copy, and reuse the hash */
				file_copy(other_file, file);

				/* check if other file still exists */
				if (file_flag_has(other_file, FILE_IS_DISCOVERED)) {
					++scan->count_copy;

					log_tag("scan:copy:%s:%s:%s:%s\n", other_disk->name, esc_tag(other_file->sub), disk->name, esc_tag(file->sub));
					if (is_diff) {
						msg_info("copy %s -> %s\n", fmt_term(other_disk, other_file->sub), fmt_term(disk, file->sub));
					}
				} else {
					++scan->count_relocate;

					log_tag("scan:relocate:%s:%s:%s:%s\n", other_disk->name, esc_tag(other_file->sub), disk->name, esc_tag(file->sub));
					if (is_diff) {
						msg_info("relocate %s -> %s\n", fmt_term(other_disk, other_file->sub), fmt_term(disk, file->sub));
					}
				}

				/* mark it as reported */
				is_file_reported = 1;

				/* no need to continue the search */
				break;
			}
		}
	}

	/*
	 * If not yet reported, do it now
	 * we postpone this to avoid to print two times the copied files
	 */
	if (!is_file_reported) {
		if (is_file_already_present) {
			++scan->count_change;

			log_tag("scan:update:%s:%s: %" PRIu64 " %" PRIu64 ".%d -> %" PRIu64 " %" PRIu64 ".%d\n", disk->name, esc_tag(sub),
				file_already_present_size, file_already_present_mtime_sec, file_already_present_mtime_nsec,
				file->size, file->mtime_sec, file->mtime_nsec
			);

			if (is_diff) {
				msg_info("update %s\n", fmt_term(disk, sub));
			}
		} else {
			++scan->count_insert;

			log_tag("scan:add:%s:%s\n", disk->name, esc_tag(sub));
			if (is_diff) {
				msg_info("add %s\n", fmt_term(disk, sub));
			}
		}
	}

	if (!is_file_modified) {
		scan_file_inode_insert(scan, file);
		scan_file_stamp_insert(scan, file);
	} else {
		/*
		 * Insert the new inode before applying the next file, so paths with the
		 * same inode are recognized as hardlinks to this file. Keep the old
		 * path/stamp entries until Phase 3; the new entries are inserted in Phase 4.
		 */
		scan_file_inode_insert(scan, file);
		file_flag_set(file, FILE_IS_MODIFIED_NEW);
	}

	/* insert the file in the delayed allocation list */
	scan_file_delayed_allocate(scan, file);
}

/**
 * Check if candidate discovery is better suited as canonical representative than current.
 *
 * Preserves existing canonical files on persistent-inode filesystems,
 * falling back to deterministic pathcmp() ordering.
 */
static int discovery_canonical_better(struct snapraid_scan* scan, struct snapraid_discovery* candidate, struct snapraid_discovery* current)
{
	struct snapraid_disk* disk = scan->disk;

	/* on filesystems where previous inodes are reliable, preserve the previous canonical file if still present */
	if (!disk->has_volatile_inodes && !disk->has_different_uuid && !disk->has_unsupported_uuid) {
		uint64_t inode = candidate->inode;
		struct snapraid_file* old_file = tommy_hashdyn_search(&disk->inodeset, file_inode_compare_to_arg, &inode, file_inode_hash(inode));
		if (old_file) {
			if (pathcmp(candidate->sub, old_file->sub) == 0)
				return 1;
			if (pathcmp(current->sub, old_file->sub) == 0)
				return 0;
		}
	}

	/* deterministic fallback: minimum pathcmp() ordering */
	return pathcmp(candidate->sub, current->sub) < 0;
}

static void scan_file_discover(struct snapraid_scan* scan, const char* sub, struct stat* st)
{
	struct snapraid_disk* disk = scan->disk;
	struct snapraid_discovery* disc;

	disc = discovery_alloc(sub, st);

	/* Hardlink collision detection */
	if (disc->inode != INODE_INVALID) {
		struct snapraid_discovery* rep = tommy_hashdyn_search(&scan->file_discovery_inodeset, discovery_inode_compare_to_arg, &disc->inode, file_inode_hash(disc->inode));
		if (!rep) {
			/* first file seen with this inode: becomes representative */
			disc->flag |= DISCOVERY_IS_CANONICAL;
			tommy_hashdyn_insert(&scan->file_discovery_inodeset, &disc->nodeset, disc, file_inode_hash(disc->inode));
		} else {
			/* Inode collision detected! */
#if HAVE_LSTAT_SYNC
			if (disk->has_volatile_hardlinks && !(rep->flag & DISCOVERY_IS_SCAN_SYNCED)) {
				char path_next[PATH_MAX];
				struct stat synced_st;

				pathprint(path_next, sizeof(path_next), "%s%s", disk->dir, sub);
				if (lstat_sync(path_next, &synced_st, 0) != 0) {
					/* LCOV_EXCL_START */
					log_tag("%s:%u:%s:%s: Stat error. %s.\n", es(errno), 0, disk->name, esc_tag(path_next), strerror(errno));
					log_fatal(errno, "Error in stat file '%s'. %s.\n", path_next, strerror(errno));
					exit(EXIT_FAILURE);
					/* LCOV_EXCL_STOP */
				}

				if (disc->inode != INODE_INVALID && synced_st.st_ino != INODE_INVALID && disc->inode != synced_st.st_ino) {
					log_tag("%s:%u:%s:%s: Uncached inode change error.\n", es(ESOFT), 0, disk->name, esc_tag(sub));
					log_fatal(ESOFT, "DANGER! Detected uncached inode change from %" PRIu64 " to %" PRIu64 " for file '%s'\n",
						(uint64_t)disc->inode, (uint64_t)synced_st.st_ino, sub);
					log_fatal(ESOFT, "It's better if you run SnapRAID without other processes running.\n");
					exit(EXIT_FAILURE);
				}

				rep->size = synced_st.st_size;
				rep->mtime_sec = synced_st.st_mtime;
				rep->mtime_nsec = STAT_NSEC(&synced_st);
				rep->flag |= DISCOVERY_IS_SCAN_SYNCED;

				disc->size = rep->size;
				disc->mtime_sec = rep->mtime_sec;
				disc->mtime_nsec = rep->mtime_nsec;
				disc->flag |= DISCOVERY_IS_SCAN_SYNCED;
			} else
#endif
			if (rep->flag & DISCOVERY_IS_SCAN_SYNCED) {
				disc->size = rep->size;
				disc->mtime_sec = rep->mtime_sec;
				disc->mtime_nsec = rep->mtime_nsec;
				disc->flag |= DISCOVERY_IS_SCAN_SYNCED;
			}

			/* select deterministic canonical representative */
			if (discovery_canonical_better(scan, disc, rep)) {
				rep->flag &= ~DISCOVERY_IS_CANONICAL;
				disc->flag |= DISCOVERY_IS_CANONICAL;
				tommy_hashdyn_remove_existing(&scan->file_discovery_inodeset, &rep->nodeset);
				tommy_hashdyn_insert(&scan->file_discovery_inodeset, &disc->nodeset, disc, file_inode_hash(disc->inode));
			}
		}
	}

	/*
	 * If this file corresponds to an existing file moved under a new pathname,
	 * match it by inode and normalized metadata on filesystems with persistent inodes.
	 * Marking it FILE_IS_DISCOVERED in Phase 1 ensures all moved files across all
	 * disks are flagged before Phase 2 copy detection runs, avoiding any dependency
	 * on disk processing order in scanlist.
	 */
	if (!disk->has_volatile_inodes && !disk->has_different_uuid && !disk->has_unsupported_uuid && disc->inode != INODE_INVALID) {
		struct snapraid_file* inode_file;
		uint64_t inode = disc->inode;

		inode_file = tommy_hashdyn_search(&disk->inodeset, file_inode_compare_to_arg, &inode, file_inode_hash(inode));
		if (inode_file
			&& inode_file->size == disc->size
			&& inode_file->mtime_sec == disc->mtime_sec
			&& inode_file->mtime_nsec == disc->mtime_nsec
		) {
			file_flag_set(inode_file, FILE_IS_DISCOVERED);
		}
	}

	tommy_list_insert_tail(&scan->file_discovery_list, &disc->nodelist, disc);
}

static void scan_file_apply_canonical(void* void_scan, void* void_disc)
{
	struct snapraid_discovery* disc = void_disc;

	if (disc->inode == INODE_INVALID || (disc->flag & DISCOVERY_IS_CANONICAL) != 0)
		scan_file_apply(void_scan, void_disc);
}

static void scan_file_apply_hardlink(void* void_scan, void* void_disc)
{
	struct snapraid_discovery* disc = void_disc;

	if (disc->inode != INODE_INVALID && (disc->flag & DISCOVERY_IS_CANONICAL) == 0)
		scan_file_apply(void_scan, void_disc);
}

static void scan_apply(struct snapraid_scan* scan)
{
	tommy_list_foreach_arg(&scan->file_discovery_list, scan_file_apply_canonical, scan);
	tommy_list_foreach_arg(&scan->file_discovery_list, scan_file_apply_hardlink, scan);
	tommy_list_foreach(&scan->file_discovery_list, discovery_free);
	tommy_list_init(&scan->file_discovery_list);
}

/**
 * Remove the specified dir from the data set.
 */
static void scan_emptydir_remove(struct snapraid_scan* scan, struct snapraid_dir* dir)
{
	struct snapraid_disk* disk = scan->disk;

	/* state changed */
	scan->need_write = 1;

	/* remove the file from the dir containers */
	tommy_hashdyn_remove_existing(&disk->dirset, &dir->nodeset);
	tommy_list_remove_existing(&disk->dirlist, &dir->nodelist);

	/* deallocate */
	dir_free(dir);
}

/**
 * Insert the specified dir in the data set.
 */
static void scan_emptydir_insert(struct snapraid_scan* scan, struct snapraid_dir* dir)
{
	struct snapraid_disk* disk = scan->disk;

	/* state changed */
	scan->need_write = 1;

	/* insert the dir in the dir containers */
	tommy_hashdyn_insert(&disk->dirset, &dir->nodeset, dir, dir_name_hash(dir->sub));
	tommy_list_insert_tail(&disk->dirlist, &dir->nodelist, dir);
}

/**
 * Process a dir.
 */
static void scan_emptydir(struct snapraid_scan* scan, const char* sub)
{
	struct snapraid_disk* disk = scan->disk;
	struct snapraid_dir* dir;

	/* check if the dir already exists */
	dir = tommy_hashdyn_search(&disk->dirset, dir_name_compare, sub, dir_name_hash(sub));
	if (dir) {
		/* check if multiple files have the same name */
		if (dir_flag_has(dir, FILE_IS_PRESENT)) {
			/* LCOV_EXCL_START */
			log_fatal(EINTERNAL, "Internal inconsistency for dir '%s%s'\n", disk->dir, sub);
			os_abort();
			/* LCOV_EXCL_STOP */
		}

		/* mark as present */
		dir_flag_set(dir, FILE_IS_PRESENT);

		/* nothing more to do */
		return;
	} else {
		/* and continue to insert it */
	}

	/* insert it */
	dir = dir_alloc(sub);

	/* mark it as present */
	dir_flag_set(dir, FILE_IS_PRESENT);

	/* insert it in the delayed insert list */
	tommy_list_insert_tail(&scan->dir_insert_list, &dir->nodelist, dir);
}

struct dirent_sorted {
	/* node for data structures */
	tommy_node node;

#if HAVE_STRUCT_DIRENT_D_INO
	uint64_t d_ino; /**< Inode number. */
#endif
#if HAVE_STRUCT_DIRENT_D_TYPE
	uint32_t d_type; /**< File type. */
#endif
#if HAVE_STRUCT_DIRENT_D_STAT
	struct stat d_stat; /**< Stat result. */
#endif
	char d_name[]; /**< Variable length name. It must be the last field. */
};

#if HAVE_STRUCT_DIRENT_D_INO
static int dd_ino_compare(const void* void_a, const void* void_b)
{
	const struct dirent_sorted* a = void_a;
	const struct dirent_sorted* b = void_b;

	if (a->d_ino < b->d_ino)
		return -1;
	if (a->d_ino > b->d_ino)
		return 1;

	return 0;
}
#endif

static int dd_name_compare(const void* void_a, const void* void_b)
{
	const struct dirent_sorted* a = void_a;
	const struct dirent_sorted* b = void_b;

	return strcmp(a->d_name, b->d_name);
}

/**
 * Return the stat info of a dir entry.
 */
#if HAVE_STRUCT_DIRENT_D_STAT
#define DSTAT(file, dd, buf) dstat(dd)
struct stat* dstat(struct dirent_sorted* dd)
{
	return &dd->d_stat;
}
#else
#define DSTAT(file, dd, buf) dstat(disk, file, buf)
struct stat* dstat(struct snapraid_disk* disk, const char* file, struct stat* st)
{
	if (lstat(file, st) != 0) {
		/* LCOV_EXCL_START */
		log_tag("%s:%u:%s:%s: Stat error. %s.\n", es(errno), 0, disk->name, esc_tag(file), strerror(errno));
		log_fatal(errno, "Error in stat file/directory '%s'. %s.\n", file, strerror(errno));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}
	return st;
}
#endif

/**
 * Process a directory.
 * Return != 0 if at least one file or link is processed.
 */
static int scan_sub(struct snapraid_scan* scan, int level, int is_diff, char* path_next, char* sub_next, char* tmp)
{
	struct snapraid_state* state = scan->state;
	struct snapraid_disk* disk = scan->disk;
	int processed = 0;
	DIR* d;
	tommy_list list;
	tommy_node* node;
	size_t mount_point_len;
	size_t path_len;
	size_t sub_len;

	mount_point_len = strlen(disk->mount_point);
	path_len = strlen(path_next);
	sub_len = strlen(sub_next);

	tommy_list_init(&list);

	d = opendir(path_next);
	if (!d) {
		/* LCOV_EXCL_START */
		log_tag("%s:%u:%s:%s: Open dir error. %s.\n", es(errno), 0, disk->name, esc_tag(path_next), strerror(errno));
		log_fatal(errno, "Error opening directory '%s'. %s.\n", path_next, strerror(errno));
		if (level == 0)
			log_fatal(errno, "If this is the disk mount point, remember to create it manually\n");
		else
			log_fatal(errno, "If it's a permission problem, you can exclude it in the config file with: exclude /%s\n", sub_next);
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* read the full directory */
	while (1) {
		struct dirent_sorted* entry;
		const char* name;
		struct dirent* dd;
		size_t name_len;

		/*
		 * Clear errno to differentiate the end of the stream and an error condition
		 *
		 * From the Linux readdir() manpage:
		 * "If the end of the directory stream is reached, NULL is returned and errno is not changed.
		 * If an error occurs, NULL is returned and errno is set appropriately."
		 */
		errno = 0;
		dd = readdir(d);
		if (dd == 0 && errno != 0) {
			/* LCOV_EXCL_START */
			/* restore removing additions */
			path_next[path_len] = 0;
			sub_next[sub_len] = 0;
			log_tag("%s:%u:%s:%s: Read dir error. %s.\n", es(errno), 0, disk->name, esc_tag(path_next), strerror(errno));
			log_fatal(errno, "Error reading directory '%s'. %s.\n", path_next, strerror(errno));
			log_fatal(errno, "You can exclude it in the config file with: exclude /%s\n", sub_next);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
		if (dd == 0) {
			break; /* finished */
		}

		/* skip "." and ".." files */
		name = dd->d_name;
		if (name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0)))
			continue;

		pathcatl(path_next, path_len, PATH_MAX, name);

		/* check for not supported file names */
		if (name[0] == 0) {
			/* LCOV_EXCL_START */
			log_tag("%s:%u:%s:%s: Unsupported name error.\n", es(ESOFT), 0, disk->name, esc_tag(path_next));
			log_fatal(ESOFT, "Unsupported name '%s' in file '%s'.\n", name, path_next);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		/*
		 * Exclude hidden files even before calling lstat().
		 * Note that .snapraidignore is an exception because it is a control file
		 * that defines local exclusion rules, and must also be preserved as array
		 * data so that exclusion rules survive disaster recovery.
		 */
		if (filter_hidden(state->filter_hidden, dd) != 0) {
			if (pathcmp(".snapraidignore", dd->d_name) != 0) {
				msg_verbose("Excluding hidden '%s'\n", path_next);
				continue;
			}
		}

		/* exclude content files even before calling lstat() */
		if (filter_content(&state->contentlist, disk->mount_point, mount_point_len, sub_next, sub_len, name) != 0) {
			msg_verbose("Excluding content '%s'\n", path_next);
			continue;
		}

		/* exclude snapshot container even before calling lstat() */
		if (filter_snapshot(state->snapshot, sub_next, name) != 0) {
			msg_verbose("Excluding snapshots directory '%s'\n", path_next);
			continue;
		}

		name_len = strlen(dd->d_name);
		entry = malloc_nofail(sizeof(struct dirent_sorted) + name_len + 1);

		/* copy the dir entry */
#if HAVE_STRUCT_DIRENT_D_INO
		entry->d_ino = dd->d_ino;
#endif
#if HAVE_STRUCT_DIRENT_D_TYPE
		entry->d_type = dd->d_type;
#endif
#if HAVE_STRUCT_DIRENT_D_STAT
		/* convert dirent to lstat result */
		dirent_lstat(dd, &entry->d_stat);

		/* note that at this point the st_mode may be 0 */
#endif
		memcpy(entry->d_name, dd->d_name, name_len + 1);

		/* insert in the list */
		tommy_list_insert_tail(&list, &entry->node, entry);

		/* process ignore files */
		if (pathcmp(".snapraidignore", dd->d_name) == 0)
			state_load_ignore_file(&scan->local_filter_list, path_next, sub_next);
	}

	if (closedir(d) != 0) {
		/* LCOV_EXCL_START */
		/* restore removing additions */
		path_next[path_len] = 0;
		log_tag("%s:%u:%s:%s: Close dir error. %s.\n", es(errno), 0, disk->name, esc_tag(path_next), strerror(errno));
		log_fatal(errno, "Error closing directory '%s'. %s.\n", path_next, strerror(errno));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	if (state->opt.force_order == SORT_ALPHA) {
		/*
		 * If requested sort alphabetically
		 * this is mainly done for testing to ensure to always
		 * process in the same way in different platforms
		 */
		tommy_list_sort(&list, dd_name_compare);
	}
#if HAVE_STRUCT_DIRENT_D_INO
	else if (!disk->has_volatile_inodes) {
		/*
		 * If inodes are persistent
		 * sort the list of dir entries by inodes
		 */
		tommy_list_sort(&list, dd_ino_compare);
	}
	/* otherwise just keep the insertion order */
#endif

	/* process the sorted dir entries */
	node = list;
	while (node != 0) {
		struct snapraid_filter* reason = 0;
		struct dirent_sorted* dd = node->data;
		const char* name = dd->d_name;
		struct stat* st;
		int type;
#if !HAVE_STRUCT_DIRENT_D_STAT
		struct stat st_buf;
#endif

		pathcatl(path_next, path_len, PATH_MAX, name);
		pathcatl(sub_next, sub_len, PATH_MAX, name);

		/* start with an unknown type */
		type = -1;
		st = 0;

		/* if dirent has the type, use it */
#if HAVE_STRUCT_DIRENT_D_TYPE
		switch (dd->d_type) {
		case DT_UNKNOWN : break;
		case DT_REG : type = 0; break;
		case DT_LNK : type = 1; break;
		case DT_DIR : type = 2; break;
		default : type = 3; break;
		}
#endif

		/* if type is still unknown */
		if (type < 0) {
			/* get the type from stat */
			st = DSTAT(path_next, dd, &st_buf);

#if HAVE_STRUCT_DIRENT_D_STAT
			/*
			 * If the st_mode field is missing, takes care to fill it using normal lstat()
			 * at now this can happen only in Windows (with HAVE_STRUCT_DIRENT_D_STAT defined),
			 * because we use a directory reading method that doesn't read info about ReparsePoint.
			 * Note that here we cannot call here lstat_sync(), because we don't know what kind
			 * of file is it, and lstat_sync() doesn't always work
			 */
			if (st->st_mode == 0) {
				if (lstat(path_next, st) != 0) {
					/* LCOV_EXCL_START */
					log_tag("%s:%u:%s:%s: Stat error. %s.\n", es(errno), 0, disk->name, esc_tag(path_next), strerror(errno));
					log_fatal(errno, "Error in stat file/directory '%s'. %s.\n", path_next, strerror(errno));
					exit(EXIT_FAILURE);
					/* LCOV_EXCL_STOP */
				}
			}
#endif

			if (S_ISREG(st->st_mode))
				type = 0;
			else if (S_ISLNK(st->st_mode))
				type = 1;
			else if (S_ISDIR(st->st_mode))
				type = 2;
			else
				type = 3;
		}

		if (type == 0) { /* REG */
			/*
			 * Note that .snapraidignore is an exception from filtering because it
			 * is a control file that must be preserved as array data.
			 */
			if (pathcmp(".snapraidignore", name) == 0
				|| (filter_path(&state->filterlist, &reason, disk->name, sub_next) == 0
				&& filter_path(&scan->local_filter_list, &reason, disk->name, sub_next) == 0)) {

				struct snapraid_file* existing;

				/* late stat, if not yet called */
				if (!st)
					st = DSTAT(path_next, dd, &st_buf);

#if HAVE_LSTAT_SYNC
				/*
				 * In Windows fast directory enumeration, st_ino may be missing (0).
				 * Only st_ino is required for file identity and hardlink tracking;
				 * we call lstat_sync() only when st_ino is missing to avoid expensive file opens.
				 */
				if (st->st_ino == INODE_INVALID) {
					if (lstat_sync(path_next, st, 0) != 0) {
						/* LCOV_EXCL_START */
						log_tag("%s:%u:%s:%s: Stat error. %s.\n", es(errno), 0, disk->name, esc_tag(path_next), strerror(errno));
						log_fatal(errno, "Error in stat file '%s'. %s.\n", path_next, strerror(errno));
						exit(EXIT_FAILURE);
						/* LCOV_EXCL_STOP */
					}
				}
#endif

				existing = tommy_hashdyn_search(&disk->pathset, file_path_compare_to_arg, sub_next, file_path_hash(sub_next));
				if (existing)
					file_flag_set(existing, FILE_IS_DISCOVERED);

				scan_file_discover(scan, sub_next, st);
				processed = 1;
			} else {
				msg_verbose("Excluding file '%s' for rule '%s'\n", path_next, filter_type(reason, tmp, PATH_MAX));
			}
		} else if (type == 1) { /* LNK */
			/*
			 * Note that .snapraidignore is an exception from filtering because it
			 * is a control file that must be preserved as array data.
			 */
			if (pathcmp(".snapraidignore", name) == 0
				|| (filter_path(&state->filterlist, &reason, disk->name, sub_next) == 0
				&& filter_path(&scan->local_filter_list, &reason, disk->name, sub_next) == 0)) {
				ssize_t ret;

				ret = readlink(path_next, tmp, PATH_MAX);
				if (ret >= PATH_MAX) {
					/* LCOV_EXCL_START */
					log_tag("%s:%u:%s:%s: Readlink error. %s.\n", es(errno), 0, disk->name, esc_tag(path_next), strerror(errno));
					log_fatal(EINTERNAL, "Error in readlink file '%s'. Symlink too long.\n", path_next);
					exit(EXIT_FAILURE);
					/* LCOV_EXCL_STOP */
				}
				if (ret < 0) {
					/* LCOV_EXCL_START */
					log_tag("%s:%u:%s:%s: Readlink error. %s.\n", es(errno), 0, disk->name, esc_tag(path_next), strerror(errno));
					log_fatal(errno, "Error in readlink file '%s'. %s.\n", path_next, strerror(errno));
					exit(EXIT_FAILURE);
					/* LCOV_EXCL_STOP */
				}
				if (ret == 0)
					log_error(ESOFT, "WARNING! Empty symbolic link '%s'.\n", path_next);

				/* readlink doesn't put the final 0 */
				tmp[ret] = 0;

				/* process as a symbolic link */
				scan_link(scan, is_diff, sub_next, tmp, FILE_IS_SYMLINK);
				processed = 1;
			} else {
				msg_verbose("Excluding link '%s' for rule '%s'\n", path_next, filter_type(reason, tmp, PATH_MAX));
			}
		} else if (type == 2) { /* DIR */
			if (filter_subdir(&state->filterlist, &reason, disk->name, sub_next) == 0
				&& filter_subdir(&scan->local_filter_list, &reason, disk->name, sub_next) == 0) {
#ifndef _WIN32
				/* late stat, if not yet called */
				if (!st)
					st = DSTAT(path_next, dd, &st_buf);

				struct fsidentity identity;
				if (fsidentity(path_next, st, &identity) != 0) {
					/* LCOV_EXCL_START */
					/* restore removing additions */
					path_next[path_len] = 0;
					sub_next[sub_len] = 0;
					log_tag("%s:%u:%s:%s: Statx error. %s.\n", es(errno), 0, disk->name, esc_tag(path_next), strerror(errno));
					log_fatal(errno, "Error accessing directory '%s'. %s.\n", path_next, strerror(errno));
					exit(EXIT_FAILURE);
					/* LCOV_EXCL_STOP */
				}

				/*
				 * In Unix don't follow mount points in different devices or subvolumes
				 * in Windows we are already skipping them reporting them as special files
				 */
				if (identity.device != scan->dir_identity.device) {
					log_tag("%s:%u:%s:%s: Ignoring mount point.\n", es(ESOFT), 0, disk->name, esc_tag(path_next));
					log_error(ESOFT, "WARNING! Ignoring mount point '%s' because it appears to be in a different device\n", path_next);
				} else if (identity.mnt_id != scan->dir_identity.mnt_id) {
					log_tag("%s:%u:%s:%s: Ignoring mount point.\n", es(ESOFT), 0, disk->name, esc_tag(path_next));
					log_error(ESOFT, "WARNING! Ignoring mount point '%s' because it belongs to a different mount\n", path_next);
				} else if (identity.subvol != scan->dir_identity.subvol) {
					log_tag("%s:%u:%s:%s: Ignoring nested subvolume.\n", es(ESOFT), 0, disk->name, esc_tag(path_next));
					log_error(ESOFT, "WARNING! Ignoring nested subvolume '%s' because it belongs to a different subvolume\n", path_next);
				} else
#endif
				{
					/* recurse */
					pathslash(path_next, PATH_MAX);
					pathslash(sub_next, PATH_MAX);
					if (scan_sub(scan, level + 1, is_diff, path_next, sub_next, tmp) == 0) {
						/* restore removing additions */
						pathcatl(sub_next, sub_len, PATH_MAX, name);
						/* scan the directory as empty dir */
						scan_emptydir(scan, sub_next);
					}
					/* or we processed something internally, or we have added the empty dir */
					processed = 1;
				}
			} else {
				msg_verbose("Excluding directory '%s' for rule '%s'\n", path_next, filter_type(reason, tmp, PATH_MAX));
			}
		} else {
			if (filter_path(&state->filterlist, &reason, disk->name, sub_next) == 0
				&& filter_path(&scan->local_filter_list, &reason, disk->name, sub_next) == 0) {
				/* late stat, if not yet called */
				if (!st)
					st = DSTAT(path_next, dd, &st_buf);

				log_tag("%s:%u:%s:%s: Ignoring special file.\n", es(ESOFT), 0, disk->name, esc_tag(path_next));
				log_error(ESOFT, "WARNING! Ignoring special '%s' file '%s'\n", stat_desc(st), path_next);
			} else {
				msg_verbose("Excluding special file '%s' for rule '%s'\n", path_next, filter_type(reason, tmp, PATH_MAX));
			}
		}

		/* next entry */
		node = node->next;

		/* free the present one */
		free(dd);
	}

	return processed;
}

/**
 * Process a directory.
 * Return != 0 if at least one file or link is processed.
 */
static int scan_dir(struct snapraid_scan* scan, int level, int is_diff, const char* dir, const char* sub)
{
	/* working buffers used by scan_sub() */
	char path_next[PATH_MAX];
	char sub_next[PATH_MAX];
	char tmp[PATH_MAX];

	pathcpy(path_next, sizeof(path_next), dir);
	pathcpy(sub_next, sizeof(sub_next), sub);

	return scan_sub(scan, level, is_diff, path_next, sub_next, tmp);
}

static void* scan_disk(void* arg)
{
	struct snapraid_scan* scan = arg;
	struct snapraid_disk* disk = scan->disk;
	int ret;
	int has_persistent_inodes;
	int has_syncronized_hardlinks;
	uint64_t start;

	/* check if the disk supports persistent inodes */
	ret = fsinfo(disk->dir, &has_persistent_inodes, &has_syncronized_hardlinks, 0, 0, 0, 0, 0, 0);
	if (ret < 0) {
		/* LCOV_EXCL_START */
		log_tag("%s:%u:%s:%s: Filesystem info error. %s.\n", es(errno), 0, disk->name, esc_tag(disk->dir), strerror(errno));
		log_fatal(errno, "Error accessing disk '%s' to get file-system info. %s.\n", disk->dir, strerror(errno));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}
	if (!has_persistent_inodes) {
		disk->has_volatile_inodes = 1;
	}
	if (!has_syncronized_hardlinks) {
		disk->has_volatile_hardlinks = 1;
	}

	/* if inodes or UUID are not persistent/changed/unsupported */
	if (disk->has_volatile_inodes || disk->has_different_uuid || disk->has_unsupported_uuid) {
		/*
		 * Remove all the inodes from the inode collection
		 * if they are not persistent, all of them could be changed now
		 * and we don't want to find false matching ones
		 * See scan_file_apply() for more details
		 */
		tommy_node* node = disk->filelist;
		while (node) {
			struct snapraid_file* file = node->data;

			node = node->next;

			/* remove from the inode set */
			if (file->inode != INODE_INVALID)
				tommy_hashdyn_remove_existing(&disk->inodeset, &file->nodeset);

			/* clear the inode */
			file->inode = INODE_INVALID;
		}
	}

	start = os_tick_ms();

	struct stat st;
	if (lstat(disk->dir, &st) != 0) {
		/* LCOV_EXCL_START */
		log_tag("%s:%u:%s:%s: Stat error. %s.\n", es(errno), 0, disk->name, esc_tag(disk->dir), strerror(errno));
		log_fatal(errno, "Error accessing directory '%s'. %s.\n", disk->dir, strerror(errno));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	if (fsidentity(disk->dir, &st, &scan->dir_identity) != 0) {
		/* LCOV_EXCL_START */
		log_tag("%s:%u:%s:%s: Statx error. %s.\n", es(errno), 0, disk->name, esc_tag(disk->dir), strerror(errno));
		log_fatal(errno, "Error accessing directory '%s'. %s.\n", disk->dir, strerror(errno));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	scan_dir(scan, 0, scan->is_diff, disk->dir, "");

	if (!scan->is_diff)
		msg_progress("Scanned %s in %" PRIu64 " seconds\n", disk->name, (os_tick_ms() - start) / 1000);

	return 0;
}

static int state_diffscan(struct snapraid_state* state, int is_diff)
{
	tommy_node* i;
	tommy_node* j;
	tommy_list scanlist;
	int done;
	msg_ptr* msg;
	struct snapraid_scan total;
	int no_difference;
	tommy_list_init(&scanlist);

	if (is_diff)
		msg_progress("Comparing...\n");
	else
		msg_progress("Scanning...\n");

	log_tag("list:scan_begin\n");

	/* allocate all the scan data */
	for (i = state->disklist; i != 0; i = i->next) {
		struct snapraid_disk* disk = i->data;
		struct snapraid_scan* scan;

		scan = scan_alloc(state, disk, is_diff);

		tommy_list_insert_tail(&scanlist, &scan->node, scan);
	}

	/*
	 * We split the search in four phases:
	 * Phase 1: Parallel directory Discovery, collecting metadata and normalizing volatile hardlinks.
	 * Phase 2: Serialized Apply, classifying discovered files, updating counters/logs, and detecting copies.
	 * Phase 3: Serialized removals (deleted files and old versions of modified files) to free up parity space.
	 * Phase 4: Serialized insertions and allocations of new files.
	 *
	 * We must start Phase 3 (deletions) only when all disks have finished Phase 2 (Apply),
	 * to ensure that copy/relocation detection on any disk can search the stampset of other disks
	 * before their old files are deleted/deallocated.
	 */

	/*
	 * Phase 1: Parallel directory Discovery
	 *
	 * Invariants during and after this phase:
	 * - Each disk is read only by its scan thread, enumerating files and populating
	 *   file_discovery_list without observable SnapRAID classification side effects.
	 * - No old file, link, directory, or parity allocation is removed.
	 * - Inode collisions on volatile-hardlink filesystems trigger selective lstat_sync()
	 *   to normalize authoritative metadata across hardlink groups before Apply.
	 */
	for (i = scanlist; i != 0; i = i->next) {
		struct snapraid_scan* scan = i->data;
#if HAVE_THREAD
		if (state->opt.skip_multi_scan)
			scan_disk(scan);
		else
			thread_create(&scan->thread, scan_disk, scan);
#else
		scan_disk(scan);
#endif
	}

#if HAVE_THREAD
	/* wait for all threads to terminate */
	for (i = scanlist; i != 0; i = i->next) {
		struct snapraid_scan* scan = i->data;
		void* retval;

		/* wait for thread termination */
		if (!state->opt.skip_multi_scan)
			thread_join(scan->thread, &retval);
	}
#endif

	msg_progress("Applying...\n");

	/*
	 * Phase 2: Serialized Apply of discovered regular files
	 *
	 * Invariants during and after this phase:
	 * - All Phase 1 Discovery threads have terminated, so file enumeration,
	 *   authoritative metadata synchronization, and filesystem I/O are complete across all disks.
	 * - Apply performs purely in-memory operations with zero filesystem calls.
	 * - Each disk's discovered files are classified against past state (equal,
	 *   move, restore, update, add, hardlink) and state mutations (counters,
	 *   log tags, FILE_IS_PRESENT, copy detection) occur here.
	 * - Serialized execution ensures copy/relocate detection against stampsets
	 *   of other disks is completely deterministic and free of race conditions.
	 * - No old file, link, directory, or parity allocation is removed. This keeps
	 *   every old stamp available to all disks for copy/relocate detection.
	 * - For every valid inode already encountered in the current scan, inodeset
	 *   points to the current FILE_IS_PRESENT file. Later paths with the same inode
	 *   are therefore represented as hardlinks to that single file.
	 * - Files with INODE_INVALID are never inserted into inodeset. Inode validity
	 *   does not change when their path/stamp insertion is scheduled.
	 * - A new normal file is immediately inserted into all applicable
	 *   inode/path/stamp sets and queued for delayed parity allocation in Phase 4.
	 * - For a modified file, FILE_IS_MODIFIED_OLD has INODE_INVALID and is no
	 *   longer in inodeset, but remains in pathset/stampset and filelist.
	 *   FILE_IS_MODIFIED_NEW is in inodeset only if its inode is valid, has no
	 *   path/stamp entry, and is queued for Phase 4.
	 * - For a reallocated unchanged file, FILE_IS_REALLOC_OLD remains in all its
	 *   original containers. FILE_IS_REALLOC_NEW is only queued for Phase 4 and
	 *   is not inserted into any file container yet.
	 */
	for (i = scanlist; i != 0; i = i->next) {
		struct snapraid_scan* scan = i->data;
		scan_apply(scan);
	}

	for (i = scanlist; i != 0; i = i->next) {
		struct snapraid_scan* scan = i->data;
		struct snapraid_disk* disk = scan->disk;
		tommy_node* node;

		/*
		 * Phase 3: Removals (deleted files and old versions of modified files)
		 *
		 * Invariants on entry and during this phase:
		 * - All Phase 1 and Phase 2 operations have terminated, so removals are serialized and
		 *   no copy/relocate detection can still reference an old stamp or allocation.
		 * - Existing files not marked FILE_IS_PRESENT are still in their applicable
		 *   inode/path/stamp sets and filelist, ready for normal removal.
		 * - FILE_IS_MODIFIED_OLD is no longer in inodeset and has INODE_INVALID, so
		 *   scan_file_remove() removes only its old path/stamp entries and allocation.
		 *   A replacement with a valid inode remains in inodeset during the removal.
		 * - FILE_IS_REALLOC_OLD is still in all applicable inode/path/stamp sets and
		 *   filelist, while FILE_IS_REALLOC_NEW is in no file container.
		 * - New normal files are already in all applicable inode/path/stamp sets,
		 *   modified new files have only their applicable inode entry, and all new
		 *   versions are also in file_insert_list, without parity allocation or
		 *   filelist membership.
		 * - Every old file/link/directory node is removed at most once. Deallocation
		 *   happens before any Phase 4 allocation, making the freed parity reusable.
		 * - On exit, no old path/stamp entry conflicts with a queued modified version;
		 *   a queued reallocation has no old inode/path/stamp entry left either.
		 */

		/* check for removed files */
		node = disk->filelist;
		while (node) {
			struct snapraid_file* file = node->data;

			/* next node */
			node = node->next;

			if (file_flag_has(file, FILE_IS_REALLOC_OLD)) {
				scan_file_remove(scan, file, 0);
			} else if (file_flag_has(file, FILE_IS_MODIFIED_OLD)) {
				scan_file_remove(scan, file, 1);
			} else if (!file_flag_has(file, FILE_IS_PRESENT)) {
				/* check if the file was relocated to another disk */
				if (!file_flag_has(file, FILE_IS_RELOCATED)) {
					++scan->count_remove;

					log_tag("scan:remove:%s:%s\n", disk->name, esc_tag(file->sub));
					if (is_diff) {
						msg_info("remove %s\n", fmt_term(disk, file->sub));
					}
				}

				scan_file_remove(scan, file, 1);
			}
		}

		/* check for removed links */
		node = disk->linklist;
		while (node) {
			struct snapraid_link* slink = node->data;

			/* next node */
			node = node->next;

			/* remove if not present */
			if (!link_flag_has(slink, FILE_IS_PRESENT)) {
				++scan->count_remove;

				log_tag("scan:remove:%s:%s\n", disk->name, esc_tag(slink->sub));
				if (is_diff) {
					msg_info("remove %s\n", fmt_term(disk, slink->sub));
				}

				scan_link_remove(scan, slink);
			}
		}

		/* check for removed dirs */
		node = disk->dirlist;
		while (node) {
			struct snapraid_dir* dir = node->data;

			/* next node */
			node = node->next;

			/* remove if not present */
			if (!dir_flag_has(dir, FILE_IS_PRESENT)) {
				scan_emptydir_remove(scan, dir);
			}
		}

		/*
		 * Phase 4: Insertions (new files and new versions of modified files)
		 *
		 * Invariants on entry and after each insertion:
		 * - All removals and deallocations for this disk are complete, and every file
		 *   to allocate is present exactly once in file_insert_list.
		 * - A new normal file is already in all applicable inode/path/stamp sets, so
		 *   it requires no further container insertion.
		 * - A FILE_IS_MODIFIED_NEW with a valid inode is already in inodeset from
		 *   Phase 2 for hardlink detection; only its path/stamp nodes are inserted here.
		 * - FILE_IS_REALLOC_NEW is in no file container; all its applicable
		 *   inode/path/stamp nodes are inserted here after FILE_IS_REALLOC_OLD was
		 *   removed in Phase 3.
		 * - The flags select the insertion path directly. No lookup or idempotent
		 *   insertion is used, and every TommyDS node is inserted exactly once.
		 * - Container insertion precedes scan_file_allocate(), which adds the parity
		 *   allocation and filelist node. Links and directories are inserted after files.
		 * - On exit, every current file is in pathset/stampset, every valid inode has
		 *   one file in inodeset, and all other paths for that inode are hardlinks.
		 *   state_fscheck() validates the resulting structures after all disks finish.
		 *
		 * Sort the files before inserting them
		 * we use a stable sort to ensure that if the reported inode
		 * are always 0, we keep at least the directory order
		 */
		switch (state->opt.force_order) {
		case SORT_INODE :
			tommy_list_sort(&scan->file_insert_list, file_inode_compare);
			break;
		case SORT_ALPHA :
			tommy_list_sort(&scan->file_insert_list, file_path_compare);
			break;
		case SORT_DIR :
			/* already in order */
			break;
		}

		/*
		 * Insert all the new files, we insert them only after the deletion
		 * to reuse the just freed space
		 */
		node = scan->file_insert_list;
		while (node) {
			struct snapraid_file* file = node->data;

			/* next node */
			node = node->next;

			/* insert the delayed containers before allocating the file */
			if (file_flag_has(file, FILE_IS_MODIFIED_NEW)) {
				/*
				 * The inode was already inserted in Phase 2, so later paths with the same
				 * inode could be recognized as hardlinks. Now that Phase 3 removed the old
				 * version, insert only the new path/stamp entries.
				 */
				scan_file_stamp_insert(scan, file);
			} else if (file_flag_has(file, FILE_IS_REALLOC_NEW)) {
				scan_file_inode_insert(scan, file);
				scan_file_stamp_insert(scan, file);
			}

			/* insert in the parity */
			scan_file_allocate(scan, file);
		}

		/* insert all the new links */
		node = scan->link_insert_list;
		while (node) {
			struct snapraid_link* slink = node->data;

			/* next node */
			node = node->next;

			/* insert it */
			scan_link_insert(scan, slink);
		}

		/* insert all the new dirs */
		node = scan->dir_insert_list;
		while (node) {
			struct snapraid_dir* dir = node->data;

			/* next node */
			node = node->next;

			/* insert it */
			scan_emptydir_insert(scan, dir);
		}
	}

	/* propagate the state change (after all the scan operations are called) */
	for (i = scanlist; i != 0; i = i->next) {
		struct snapraid_scan* scan = i->data;
		if (scan->need_write) {
			state->need_write = 1;
		}
	}

	/* check for disks where all the previously existing files where removed */
	if (!state->opt.force_empty) {
		int all_missing = 0;
		int all_rewritten = 0;
		done = 0;

		for (i = state->disklist, j = scanlist; i != 0; i = i->next, j = j->next) {
			assert(j != 0); /* silence the clang static analyzer (both lists have the same number of elements) */

			struct snapraid_disk* disk = i->data;
			struct snapraid_scan* scan = j->data;

			if (scan->count_equal == 0
				&& scan->count_move == 0
				&& scan->count_restore == 0
				&& (scan->count_remove != 0 || scan->count_change != 0)
			) {
				if (!done) {
					done = 1;
					log_error(ESOFT, "WARNING! All the files previously present in disk '%s' at dir '%s'", disk->name, disk->mount_point);
				} else {
					log_error(ESOFT, ", disk '%s' at dir '%s'", disk->name, disk->mount_point);
				}

				/* detect the special condition of all files missing */
				if (scan->count_change == 0)
					all_missing = 1;

				/* detect the special condition of all files rewritten */
				if (scan->count_remove == 0)
					all_rewritten = 1;
			}
		}
		if (done) {
			log_error(ESOFT, "\nare now missing or have been rewritten!\n");
			if (all_rewritten) {
				log_error(ESOFT, "This could occur when restoring a disk from a backup\n");
				log_error(ESOFT, "program that is not setting correctly the timestamps.\n");
			}
			if (all_missing) {
				log_error(ESOFT, "This could occur when some disks are not mounted\n");
				log_error(ESOFT, "in the expected directory.\n");
			}
			if (!is_diff) {
				log_fatal(ESOFT, "If you want to '%s' anyway, use 'snapraid --force-empty %s'.\n", state->command, state->command);
				exit(EXIT_FAILURE);
			}
		}
	}

	/* check for disks without persistent inodes */
	done = 0;
	for (i = state->disklist; i != 0; i = i->next) {
		struct snapraid_disk* disk = i->data;

		if (disk->has_volatile_inodes) {
			if (!done) {
				done = 1;
				log_info(ESOFT, "WARNING! Inodes are not persistent for disks: '%s'", disk->name);
			} else {
				log_info(ESOFT, ", '%s'", disk->name);
			}
		}
	}
	if (done) {
		log_info(ESOFT, ". Inodes are not used to detect move operations.\n");
	}

	/* check for disks with changed UUID */
	done = 0;
	for (i = state->disklist; i != 0; i = i->next) {
		struct snapraid_disk* disk = i->data;

		/*
		 * Don't print the message if the UUID changed because before
		 * it was no set.
		 * This is the normal condition for an empty disk because it
		 * isn't stored
		 */
		if (disk->has_different_uuid && !disk->had_empty_uuid) {
			if (!done) {
				done = 1;
				log_error(ESOFT, "WARNING! UUID is changed for disks: '%s'", disk->name);
			} else {
				log_error(ESOFT, ", '%s'", disk->name);
			}
		}
	}
	if (done) {
		log_error(ESOFT, ". Inodes are not used to detect move operations.\n");
	}

	/* check for disks with unsupported UUID */
	done = 0;
	for (i = state->disklist; i != 0; i = i->next) {
		struct snapraid_disk* disk = i->data;

		if (disk->has_unsupported_uuid) {
			if (!done) {
				done = 1;
				log_error(ESOFT, "WARNING! UUID is unsupported for disks: '%s'", disk->name);
			} else {
				log_error(ESOFT, ", '%s'", disk->name);
			}
		}
	}
	if (done) {
		log_error(ESOFT, ". Not using inodes to detect move operations.\n");
	}

	total.count_equal = 0;
	total.count_move = 0;
	total.count_copy = 0;
	total.count_relocate = 0;
	total.count_restore = 0;
	total.count_change = 0;
	total.count_remove = 0;
	total.count_insert = 0;

	for (i = scanlist; i != 0; i = i->next) {
		struct snapraid_scan* scan = i->data;
		total.count_equal += scan->count_equal;
		total.count_move += scan->count_move;
		total.count_copy += scan->count_copy;
		total.count_relocate += scan->count_relocate;
		total.count_restore += scan->count_restore;
		total.count_change += scan->count_change;
		total.count_remove += scan->count_remove;
		total.count_insert += scan->count_insert;
	}

	if (is_diff) {
		msg_status("\n");
		msg = msg_status;
	} else {
		msg = msg_verbose;
	}

	msg("%8u equal\n", total.count_equal);
	msg("%8u added\n", total.count_insert);
	msg("%8u removed\n", total.count_remove);
	msg("%8u updated\n", total.count_change);
	msg("%8u moved\n", total.count_move);
	msg("%8u copied\n", total.count_copy);
	msg("%8u relocated\n", total.count_relocate);
	msg("%8u restored\n", total.count_restore);

	log_tag("summary:equal:%u\n", total.count_equal);
	log_tag("summary:added:%u\n", total.count_insert);
	log_tag("summary:removed:%u\n", total.count_remove);
	log_tag("summary:updated:%u\n", total.count_change);
	log_tag("summary:moved:%u\n", total.count_move);
	log_tag("summary:copied:%u\n", total.count_copy);
	log_tag("summary:relocated:%u\n", total.count_relocate);
	log_tag("summary:restored:%u\n", total.count_restore);
	log_tag("list:scan_end\n");

	/* save in the state */
	state->removed_files = total.count_remove;
	state->updated_files = total.count_change;

	no_difference = !total.count_move && !total.count_copy && !total.count_relocate && !total.count_restore
		&& !total.count_change && !total.count_remove && !total.count_insert;

	if (is_diff) {
		if (!no_difference) {
			msg_status("There are differences!\n");
		} else {
			msg_status("No differences\n");
		}
		if (state->unsynced_blocks != 0)
			log_info(EUSER, "The last sync was interrupted. Run it again!\n");

		if (state->unsynced_blocks != 0) {
			log_tag("summary:exit:unsynced\n");
		} else if (!no_difference) {
			log_tag("summary:exit:diff\n");
		} else {
			log_tag("summary:exit:equal\n");
		}
	}

	log_flush();

	tommy_list_foreach(&scanlist, scan_free);

	/* check the file-system on all disks */
	state_fscheck(state, "after scan");

	if (is_diff) {
		/* check for file difference */
		if (!no_difference)
			return 1;

		/* check also for incomplete "sync" */
		if (state->unsynced_blocks != 0)
			return 1;
	}

	return 0;
}

int state_diff(struct snapraid_state* state)
{
	return state_diffscan(state, 1);
}

void state_scan(struct snapraid_state* state)
{
	(void)state_diffscan(state, 0); /* ignore return value */
}

