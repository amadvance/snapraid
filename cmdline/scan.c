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
#include "elem.h"
#include "state.h"
#include "parity.h"

struct snapraid_scan {
	struct snapraid_state* state; /**< State used. */
	struct snapraid_disk* disk; /**< Disk used. */

	/**
	 * Counters of changes.
	 */
	unsigned count_equal; /**< Files equal. */
	unsigned count_move; /**< Files with a different name, but equal inode, size and timestamp in the same disk. */
	unsigned count_copy; /**< Files with same name, size and timestamp, in a different disk. */
	unsigned count_restore; /**< Files with equal name, size and timestamp, but different inode. */
	unsigned count_change; /**< Files modified. */
	unsigned count_remove; /**< Files removed. */
	unsigned count_insert; /**< Files new. */

	tommy_list file_insert_list; /**< Files to insert. */
	tommy_list link_insert_list; /**< Links to insert. */
	tommy_list dir_insert_list; /**< Dirs to insert. */

	/* nodes for data structures */
	tommy_node node;
};

/**
 * Removes the specified link from the data set.
 */
static void scan_link_remove(struct snapraid_scan* scan, struct snapraid_link* link)
{
	struct snapraid_state* state = scan->state;
	struct snapraid_disk* disk = scan->disk;

	/* state changed */
	state->need_write = 1;

	/* remove the file from the link containers */
	tommy_hashdyn_remove_existing(&disk->linkset, &link->nodeset);
	tommy_list_remove_existing(&disk->linklist, &link->nodelist);

	/* deallocate */
	link_free(link);
}

/**
 * Inserts the specified link in the data set.
 */
static void scan_link_insert(struct snapraid_scan* scan, struct snapraid_link* link)
{
	struct snapraid_state* state = scan->state;
	struct snapraid_disk* disk = scan->disk;

	/* state changed */
	state->need_write = 1;

	/* insert the link in the link containers */
	tommy_hashdyn_insert(&disk->linkset, &link->nodeset, link, link_name_hash(link->sub));
	tommy_list_insert_tail(&disk->linklist, &link->nodelist, link);
}

/**
 * Processes a symbolic link.
 */
static void scan_link(struct snapraid_scan* scan, int is_diff, const char* sub, const char* linkto, unsigned link_flag)
{
	struct snapraid_state* state = scan->state;
	struct snapraid_disk* disk = scan->disk;
	struct snapraid_link* link;

	/* check if the link already exists */
	link = tommy_hashdyn_search(&disk->linkset, link_name_compare_to_arg, sub, link_name_hash(sub));
	if (link) {
		/* check if multiple files have the same name */
		if (link_flag_has(link, FILE_IS_PRESENT)) {
			/* LCOV_EXCL_START */
			msg_error("Internal inconsistency for link '%s%s'\n", disk->dir, sub);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		/* mark as present */
		link_flag_set(link, FILE_IS_PRESENT);

		/* check if the link is not changed and it's of the same kind */
		if (strcmp(link->linkto, linkto) == 0 && link_flag == link_flag_get(link, FILE_IS_LINK_MASK)) {
			/* it's equal */
			++scan->count_equal;

			if (state->opt.gui) {
				msg_tag("scan:equal:%s:%s\n", disk->name, esc(link->sub));
			}
		} else {
			/* it's an update */

			/* we have to save the linkto/type */
			state->need_write = 1;

			++scan->count_change;

			msg_tag("scan:update:%s:%s\n", disk->name, esc(link->sub));
			if (is_diff) {
				printf("update %s%s\n", disk->dir, link->sub);
			}

			/* update it */
			free(link->linkto);
			link->linkto = strdup_nofail(linkto);
			link_flag_let(link, link_flag, FILE_IS_LINK_MASK);
		}

		/* nothing more to do */
		return;
	} else {
		/* create the new link */
		++scan->count_insert;

		msg_tag("scan:add:%s:%s\n", disk->name, esc(sub));
		if (is_diff) {
			printf("add %s%s\n", disk->dir, sub);
		}

		/* and continue to insert it */
	}

	/* insert it */
	link = link_alloc(sub, linkto, link_flag);

	/* mark it as present */
	link_flag_set(link, FILE_IS_PRESENT);

	/* insert it in the delayed insert list */
	tommy_list_insert_tail(&scan->link_insert_list, &link->nodelist, link);
}

/**
 * Removes the specified file from the parity.
 */
static void scan_file_deallocate(struct snapraid_scan* scan, struct snapraid_file* file)
{
	struct snapraid_state* state = scan->state;
	struct snapraid_disk* disk = scan->disk;

	block_off_t i;

	/* state changed */
	state->need_write = 1;

	/* free all the blocks of the file */
	for (i = 0; i < file->blockmax; ++i) {
		struct snapraid_block* block = &file->blockvec[i];
		block_off_t block_pos = block->parity_pos;
		unsigned block_state;
		struct snapraid_deleted* deleted;

		/* adjust the first free position */
		/* note that doing all the deletions before alllocations, */
		/* first_free_block is always 0 and the "if" is never triggered */
		/* but we keep this code anyway for completeness. */
		if (disk->first_free_block > block_pos)
			disk->first_free_block = block_pos;

		/* allocated a new deleted block from the block we are going to delete */
		deleted = deleted_dup(block);

		/* in case we scan after an aborted sync, */
		/* we could get also intermediate states */
		block_state = block_state_get(block);
		switch (block_state) {
		case BLOCK_STATE_BLK :
			/* we keep the hash making it an "old" hash, because the parity is still containing data for it */
			break;
		case BLOCK_STATE_CHG :
			/* if we have not already cleared the past hash */
			if (!state->clear_past_hash) {
				/* in these cases we don't know if the old state is still the one */
				/* stored inside the parity, because after an aborted sync, the parity */
				/* may be or may be not have been updated with the data that it's now */
				/* deleted. Then we reset the hash to a bogus value. */
				/* For example: */
				/* - One file is added */
				/* - Sync aborted after updating the parity to the new state, */
				/*   but without saving the content file representing this new state. */
				/* - File is now deleted after the aborted sync */
				/* - Sync again, deleting the blocks (exactly here) */
				/*   with the hash of CHG block not represeting the real parity state */
				hash_invalid_set(deleted->block.hash);
			}
			break;
		case BLOCK_STATE_REP :
			/* we just don't know the old hash, and then we set it to invalid */
			hash_invalid_set(deleted->block.hash);
			break;
		default :
			/* LCOV_EXCL_START */
			msg_error("Internal inconsistency in deallocating for block %u state %u\n", block->parity_pos, block_state);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		/* insert it in the list of deleted blocks */
		tommy_list_insert_tail(&disk->deletedlist, &deleted->node, deleted);

		/* set the deleted block in the block array */
		tommy_arrayblk_set(&disk->blockarr, block_pos, &deleted->block);
	}
}

/**
 * Checks if a file is completely formed of blocks with invalid parity,
 * and no rehash is tagged, and if it has at least one block.
 */
static int file_is_full_invalid_parity_and_stable(struct snapraid_state* state, struct snapraid_file* file)
{
	block_off_t i;

	/* with no block, it never has an invalid parity */
	if (file->blockmax == 0)
		return 0;

	/* check all blocks */
	for (i = 0; i < file->blockmax; ++i) {
		struct snapraid_block* block = &file->blockvec[i];
		snapraid_info info;

		/* exclude blocks with parity */
		if (!block_has_invalid_parity(block))
			return 0;

		/* get block specific info */
		info = info_get(&state->infoarr, block->parity_pos);

		/* if rehash fails */
		if (info_get_rehash(info))
			return 0;
	}

	return 1;
}

/**
 * Checks if a file is completely formed of blocks with an updated hash,
 * and no rehash is tagged, and if it has at least one block.
 */
static int file_is_full_hashed_and_stable(struct snapraid_state* state, struct snapraid_file* file)
{
	block_off_t i;

	/* with no block, it never has a hash */
	if (file->blockmax == 0)
		return 0;

	/* check all blocks */
	for (i = 0; i < file->blockmax; ++i) {
		struct snapraid_block* block = &file->blockvec[i];
		snapraid_info info;

		/* exclude blocks without hash */
		if (!block_has_updated_hash(block))
			return 0;

		/* get block specific info */
		info = info_get(&state->infoarr, block->parity_pos);

		/* exclude blocks needing a rehash */
		if (info_get_rehash(info))
			return 0;
	}

	return 1;
}

static void scan_file_delayed_insert(struct snapraid_scan* scan, struct snapraid_file* file)
{
	struct snapraid_state* state = scan->state;
	struct snapraid_disk* disk = scan->disk;

	/* if we sort for physical offsets we have to read them for new files */
	if (state->opt.force_order == SORT_PHYSICAL
		&& file->physical == FILEPHY_UNREAD_OFFSET
	) {
		char path_next[PATH_MAX];

		pathprint(path_next, sizeof(path_next), "%s%s", disk->dir, file->sub);

		if (filephy(path_next, file->size, &file->physical) != 0) {
			/* LCOV_EXCL_START */
			msg_error("Error in getting the physical offset of file '%s'. %s.\n", path_next, strerror(errno));
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}

	tommy_list_insert_tail(&scan->file_insert_list, &file->nodelist, file);
}

/**
 * Refresh the file info.
 *
 * This is needed by Windows as the normal way to list directories may report not
 * updated info. Only the GetFileInformationByHandle() func, called file-by-file,
 * really ensures to return synced info.
 *
 * If this happens, we read also the physical offset, to avoid to read it later.
 */
static void scan_file_refresh(struct snapraid_scan* scan, const char* sub, struct stat* st, uint64_t* physical)
{
#if HAVE_LSTAT_SYNC
	struct snapraid_state* state = scan->state;
	struct snapraid_disk* disk = scan->disk;

	/* if the st_sync is not set, ensure to get synced info */
	if (st->st_sync == 0) {
		char path_next[PATH_MAX];
		struct stat synced_st;

		pathprint(path_next, sizeof(path_next), "%s%s", disk->dir, sub);

		/* if we sort for physical offsets we have to read them for new files */
		if (state->opt.force_order == SORT_PHYSICAL
			&& *physical == FILEPHY_UNREAD_OFFSET
		) {
			/* do nothing, leave the pointer to read the physical offset */
		} else {
			physical = 0; /* set the pointer to 0 to read nothing */
		}

		if (lstat_sync(path_next, &synced_st, physical) != 0) {
			/* LCOV_EXCL_START */
			msg_error("Error in stat file '%s'. %s.\n", path_next, strerror(errno));
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		if (st->st_mtime != synced_st.st_mtime
			|| st->st_mtimensec != synced_st.st_mtimensec
		) {
#ifndef _WIN32
			/*
			 * In Windows having different metadata is expected with open files
			 * because the metadata in the directory is updated only when the file
			 * is closed.
			 *
			 * See also:
			 * Why is the file size reported incorrectly for files that are still being written to?
			 * http://blogs.msdn.com/b/oldnewthing/archive/2011/12/26/10251026.aspx
			 */
			msg_error("WARNING! Detected uncached time change for file '%s'\n", sub);
			msg_error("It's better if you run SnapRAID without other processes running.\n");
#endif
			st->st_mtime = synced_st.st_mtime;
			st->st_mtimensec = synced_st.st_mtimensec;
		}

		if (st->st_size != synced_st.st_size) {
#ifndef _WIN32
			msg_error("WARNING! Detected uncached size change for file '%s'\n", sub);
			msg_error("It's better if you run SnapRAID without other processes running.\n");
#endif
			st->st_size = synced_st.st_size;
		}

		if (st->st_ino != synced_st.st_ino) {
			msg_error("DANGER! Detected uncached inode change for file '%s'\n", sub);
			msg_error("Please ensure to run SnapRAID without other processes running.\n");
			/* at this point, it's too late to change inode */
			/* and having inconsistent inodes may result to internal failures */
			/* so, it's better to abort */
			exit(EXIT_FAILURE);
		}
	}
#else
	(void)scan;
	(void)sub;
	(void)st;
	(void)physical;
#endif
}

/**
 * Keeps the file as it's (or with only a name/inode modification).
 *
 * If the file is kept, nothing has to be done.
 *
 * But if a file contains only blocks with invalid parity, it's reallocated to ensure
 * to always minimize the space used in the parity.
 *
 * This could happen after a failed sync, when some other files are deleted,
 * and then new ones can be moved backward to fill the hole created.
 */
static void scan_file_keep(struct snapraid_scan* scan, struct snapraid_file* file)
{
	struct snapraid_disk* disk = scan->disk;

	/* if the file is full invalid, schedule a reinsert at later stage */
	if (file_is_full_invalid_parity_and_stable(scan->state, file)) {
		/* deallocate the file from the parity */
		scan_file_deallocate(scan, file);

		/* remove the file from the list */
		tommy_list_remove_existing(&disk->filelist, &file->nodelist);

		/* insert the file in the delayed block allocation */
		scan_file_delayed_insert(scan, file);
	}
}

/**
 * Removes the specified file from the data set.
 */
static void scan_file_remove(struct snapraid_scan* scan, struct snapraid_file* file)
{
	struct snapraid_disk* disk = scan->disk;

	/* deallocate the file from the parity */
	scan_file_deallocate(scan, file);

	/* remove the file from the file containers */
	if (!file_flag_has(file, FILE_IS_WITHOUT_INODE)) {
		tommy_hashdyn_remove_existing(&disk->inodeset, &file->nodeset);
	}
	tommy_hashdyn_remove_existing(&disk->pathset, &file->pathset);
	tommy_hashdyn_remove_existing(&disk->stampset, &file->stampset);
	tommy_list_remove_existing(&disk->filelist, &file->nodelist);

	/* deallocate */
	file_free(file);
}

/**
 * Inserts the specified file in the data set.
 */
static void scan_file_insert(struct snapraid_scan* scan, struct snapraid_file* file)
{
	struct snapraid_state* state = scan->state;
	struct snapraid_disk* disk = scan->disk;
	block_off_t i;
	block_off_t block_max;
	block_off_t block_pos;

	/* state changed */
	state->need_write = 1;

	/* allocate the blocks of the file */
	block_pos = disk->first_free_block;
	block_max = tommy_arrayblk_size(&disk->blockarr);
	for (i = 0; i < file->blockmax; ++i) {
		snapraid_info info;
		struct snapraid_block* block = &file->blockvec[i];

		/* increment the position until the first really free block */
		while (block_pos < block_max && block_has_file(tommy_arrayblk_get(&disk->blockarr, block_pos)))
			++block_pos;

		/* if not found, allocate a new one */
		if (block_pos == block_max) {
			++block_max;
			tommy_arrayblk_grow(&disk->blockarr, block_max);
		}

		/* set the position */
		block->parity_pos = block_pos;

		/* get block specific info */
		info = info_get(&state->infoarr, block_pos);

		/* if the file block already has an updated hash without rehash */
		if (block_has_updated_hash(block) && !info_get_rehash(info)) {
			/* the only possible case is for REP blocks */
			assert(block_state_get(block) == BLOCK_STATE_REP);

			/* convert to a REP block */
			block_state_set(&file->blockvec[i], BLOCK_STATE_REP);

			/* and keep the hash as it's */
		} else {
			struct snapraid_block* over_block;
			unsigned block_state;

			/* convert to a CHG block */
			block_state_set(block, BLOCK_STATE_CHG);

			/* block to overwrite */
			over_block = tommy_arrayblk_get(&disk->blockarr, block_pos);

			/* state of the block we are going to overwrite */
			block_state = block_state_get(over_block);

			/* if the block is an empty one */
			if (block_state == BLOCK_STATE_EMPTY) {
				/* the block was empty and filled with zeros */
				/* set the hash to the special ZERO value */
				hash_zero_set(block->hash);
			} else {
				/* otherwise it's a DELETED one */
				assert(block_state == BLOCK_STATE_DELETED);

				/* copy the past hash of the block */
				memcpy(block->hash, over_block->hash, HASH_SIZE);

				/* if we have not already cleared the past hash */
				if (!state->clear_past_hash) {
					/* in this case we don't know if the old state is still the one */
					/* stored inside the parity, because after an aborted sync, the parity */
					/* may be or may be not have been updated with the new data */
					/* Then we reset the hash to a bogus value */
					/* For example: */
					/* - One file is deleted */
					/* - Sync aborted after, updating the parity to the new state, */
					/*   but without saving the content file representing this new state. */
					/* - Another file is added again (exactly here) */
					/*   with the hash of DELETED block not represeting the real parity state */
					hash_invalid_set(block->hash);
				}
			}
		}

		/* store in the disk map, after invalidating all the other blocks */
		tommy_arrayblk_set(&disk->blockarr, block_pos, block);

		/* set the new free position */
		disk->first_free_block = block_pos + 1;
	}

	/* note that the file is already added in the file hashtables */
	tommy_list_insert_tail(&disk->filelist, &file->nodelist, file);
}

/**
 * Processes a file.
 */
static void scan_file(struct snapraid_scan* scan, int is_diff, const char* sub, struct stat* st, uint64_t physical)
{
	struct snapraid_state* state = scan->state;
	struct snapraid_disk* disk = scan->disk;
	struct snapraid_file* file;
	tommy_node* i;
	int is_original_file_size_different_than_zero;
	int is_file_already_present;
	int is_file_reported;

	/*
	 * If the disk has persistent inodes and UUID, try a search on the past inodes,
	 * to detect moved files.
	 *
	 * For persistent inodes we mean inodes that keep their values when the filesystem
	 * is unmounted and remounted. This don't always happen.
	 *
	 * Cases found are:
	 * - Linux FUSE with exFAT driver from https://code.google.com/p/exfat/.
	 *   Inodes are reassigned at every mount restarting from 1 and incrementing.
	 *   As worse, the exFAT support in FUSE doesn't use subsecond precision in timestamps
	 *   making inode collision more easy (exFAT by design supports 10ms precision).
	 * - Linux VFAT kernel (3.2) driver. Inodes are fully reassigned at every mount.
	 *
	 * In such cases, to avoid possible random collisions, it's better to disable the moved
	 * file recognition.
	 *
	 * For persistent UUID we mean that it has the same UUID as before.
	 * Otherwise, if the UUID is changed, likely it's a new recreated filesystem,
	 * and then the inode have no meaning.
	 *
	 * Note that to disable the search by past inode, we do this implicitely
	 * removing all the past inode before searching for files.
	 * This ensures that no file is found with a past inode, but at the same time,
	 * it allows to find new files with the same inode, to identify them as hardlinks.
	 */
	int has_past_inodes = !disk->has_volatile_inodes && !disk->has_different_uuid && !disk->has_unsupported_uuid;

	/* always search with the new inode, in the all new inodes found until now, */
	/* with the eventual presence of also the past inodes */
	uint64_t inode = st->st_ino;

	file = tommy_hashdyn_search(&disk->inodeset, file_inode_compare_to_arg, &inode, file_inode_hash(inode));

	/* identify moved files with past inodes and hardlinks with the new inodes */
	if (file) {
		/* check if the file is not changed */
		if (file->size == st->st_size
			&& file->mtime_sec == st->st_mtime
			&& (file->mtime_nsec == STAT_NSEC(st)
		        /* always accept the stored value if it's STAT_NSEC_INVALID */
		        /* it happens when upgrading from an old version of SnapRAID */
		        /* not yet supporting the nanosecond field */
			|| file->mtime_nsec == STAT_NSEC_INVALID
			)
		) {
			/* check if multiple files have the same inode */
			if (file_flag_has(file, FILE_IS_PRESENT)) {
#if HAVE_STRUCT_STAT_ST_NLINK
				if (st->st_nlink <= 1) {
					/* LCOV_EXCL_START */
					msg_error("Internal inode '%" PRIu64 "' inconsistency for file '%s%s' already present\n", (uint64_t)st->st_ino, disk->dir, sub);
					exit(EXIT_FAILURE);
					/* LCOV_EXCL_STOP */
				}
#endif
				/* it's a hardlink */
				scan_link(scan, is_diff, sub, file->sub, FILE_IS_HARDLINK);
				return;
			}

			/* mark as present */
			file_flag_set(file, FILE_IS_PRESENT);

			/* update the nanoseconds mtime only if different */
			/* to avoid unneeded updates */
			if (file->mtime_nsec == STAT_NSEC_INVALID
				&& STAT_NSEC(st) != file->mtime_nsec
			) {
				file->mtime_nsec = STAT_NSEC(st);

				/* we have to save the new mtime */
				state->need_write = 1;
			}

			if (strcmp(file->sub, sub) != 0) {
				/* if the path is different, it means a moved file with the same inode */
				++scan->count_move;

				msg_tag("scan:move:%s:%s:%s\n", disk->name, esc(file->sub), esc(sub));
				if (is_diff) {
					printf("move %s%s -> %s%s\n", disk->dir, file->sub, disk->dir, sub);
				}

				/* remove from the name set */
				tommy_hashdyn_remove_existing(&disk->pathset, &file->pathset);

				/* save the new name */
				file_rename(file, sub);

				/* reinsert in the name set */
				tommy_hashdyn_insert(&disk->pathset, &file->pathset, file, file_path_hash(file->sub));

				/* we have to save the new name */
				state->need_write = 1;
			} else {
				/* otherwise it's equal */
				++scan->count_equal;

				if (state->opt.gui) {
					msg_tag("scan:equal:%s:%s\n", disk->name, esc(file->sub));
				}
			}

			/* mark the file as kept */
			scan_file_keep(scan, file);

			/* nothing more to do */
			return;
		}

		/* here the file matches the inode, but not the other info */
		/* if could be a modified file with the same name, */
		/* or a restored/copied file that get assigned a previously used inode, */
		/* or a filesystem with not persistent inodes */

		/* for sure it cannot be already present */
		if (file_flag_has(file, FILE_IS_PRESENT)) {
			/* LCOV_EXCL_START */
			msg_error("Internal inode '%" PRIu64 "' inconsistency for files '%s%s' and '%s%s' matching and already present but different\n", file->inode, disk->dir, sub, disk->dir, file->sub);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		/* assume a previously used inode, it's the worst case */
		/* and we handle it removing the duplicate stored inode. */
		/* If the file is found by name (not necessarely in this function call), */
		/* it will have the inode restored, otherwise, it will get removed */

		/* remove from the inode set */
		tommy_hashdyn_remove_existing(&disk->inodeset, &file->nodeset);

		/* clear the inode */
		/* this is not really needed for correct functionality */
		/* because we are going to set FILE_IS_WITHOUT_INODE */
		/* but it's easier for debugging to have invalid inodes set to 0 */
		file->inode = 0;

		/* mark as missing inode */
		file_flag_set(file, FILE_IS_WITHOUT_INODE);

		/* go further to find it by name */
	}

	/* initialize for later overwrite */
	is_file_reported = 0;
	is_original_file_size_different_than_zero = 0;

	/* then try finding it by name */
	file = tommy_hashdyn_search(&disk->pathset, file_path_compare_to_arg, sub, file_path_hash(sub));

	/* keep track if the file already exists */
	is_file_already_present = file != 0;

	if (is_file_already_present) {
		/* if the file is without an inode */
		if (file_flag_has(file, FILE_IS_WITHOUT_INODE)) {
			/* set it now */
			file->inode = st->st_ino;

			/* insert in the set */
			tommy_hashdyn_insert(&disk->inodeset, &file->nodeset, file, file_inode_hash(file->inode));

			/* unmark as missing inode */
			file_flag_clear(file, FILE_IS_WITHOUT_INODE);
		} else {
			/* here the inode has to be different, otherwise we would have found it before */
			if (file->inode == st->st_ino) {
				/* LCOV_EXCL_START */
				msg_error("Internal inconsistency in inode '%" PRIu64 "' for files '%s%s' as unexpected matching\n", file->inode, disk->dir, sub);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
		}

		/* for sure it cannot be already present */
		if (file_flag_has(file, FILE_IS_PRESENT)) {
			/* LCOV_EXCL_START */
			msg_error("Internal inconsistency in path for file '%s%s' matching and already present\n", disk->dir, sub);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		/* check if the file is not changed */
		if (file->size == st->st_size
			&& file->mtime_sec == st->st_mtime
			&& (file->mtime_nsec == STAT_NSEC(st)
		        /* always accept the stored value if it's STAT_NSEC_INVALID */
		        /* it happens when upgrading from an old version of SnapRAID */
		        /* not yet supporting the nanosecond field */
			|| file->mtime_nsec == STAT_NSEC_INVALID
			)
		) {
			/* mark as present */
			file_flag_set(file, FILE_IS_PRESENT);

			/* update the nano seconds mtime only if different */
			/* to avoid unneeded updates */
			if (file->mtime_nsec == STAT_NSEC_INVALID
				&& STAT_NSEC(st) != STAT_NSEC_INVALID
			) {
				file->mtime_nsec = STAT_NSEC(st);

				/* we have to save the new mtime */
				state->need_write = 1;
			}

			/* if when processing the disk we used the past inodes values */
			if (has_past_inodes) {
				/* if persistent inodes are supported, we are sure that the inode number */
				/* is now different, because otherwise the file would have been found */
				/* when searching by inode. */
				/* if the inode is different, it means a rewritten file with the same path */
				/* like when restoring a backup that restores also the timestamp */
				++scan->count_restore;

				msg_tag("scan:restore:%s:%s\n", disk->name, esc(sub));
				if (is_diff) {
					printf("restore %s%s\n", disk->dir, sub);
				}

				/* remove from the inode set */
				tommy_hashdyn_remove_existing(&disk->inodeset, &file->nodeset);

				/* save the new inode */
				file->inode = st->st_ino;

				/* reinsert in the inode set */
				tommy_hashdyn_insert(&disk->inodeset, &file->nodeset, file, file_inode_hash(file->inode));

				/* we have to save the new inode */
				state->need_write = 1;
			} else {
				/* otherwise it's the case of not persistent inode, where doesn't */
				/* matter if the inode is different or equal, because they have no */
				/* meaning, and then we don't even save them */
				++scan->count_equal;

				if (state->opt.gui) {
					msg_tag("scan:equal:%s:%s\n", disk->name, esc(file->sub));
				}
			}

			/* mark the file as kept */
			scan_file_keep(scan, file);

			/* nothing more to do */
			return;
		}

		/* here if the file is changed but with the correct name */

		/* keep track if the original file was not of zero size */
		is_original_file_size_different_than_zero = file->size != 0;

		/* remove it, and continue to insert it again */
		scan_file_remove(scan, file);

		/* and continue to insert it again */
	}

	/* refresh the info, to ensure that they are synced, */
	/* note that we refresh only the info of the new or modified files */
	/* because this is slow operation */
	scan_file_refresh(scan, sub, st, &physical);

	/* do a safety check to ensure that the common ext4 case of zeroing */
	/* the size of a file after a crash doesn't propagate to the backup */
	if (is_original_file_size_different_than_zero && st->st_size == 0) {
		if (!state->opt.force_zero) {
			/* LCOV_EXCL_START */
			msg_error("The file '%s%s' has unexpected zero size!\n", disk->dir, sub);
			msg_error("It's possible that after a kernel crash this file was lost,\n");
			msg_error("and you can use 'snapraid --filter %s fix' to recover it.\n", sub);
			if (!is_diff) {
				msg_error("If this an expected condition you can '%s' anyway using 'snapraid --force-zero %s'\n", state->command, state->command);
				exit(EXIT_FAILURE);
			}
			/* LCOV_EXCL_STOP */
		}
	}

	/* insert it */
	file = file_alloc(state->block_size, sub, st->st_size, st->st_mtime, STAT_NSEC(st), st->st_ino, physical);

	/* mark it as present */
	file_flag_set(file, FILE_IS_PRESENT);

	/* if copy detection is enabled */
	/* note that the copy detection is tried also for updated files */
	/* this makes sense because it may happen to have two different copies */
	/* of the same file, and we move the right one over the wrong one */
	/* in such case we have a "copy" over an "update" */
	if (!state->opt.force_nocopy) {
		tommy_uint32_t hash = file_stamp_hash(file->size, file->mtime_sec, file->mtime_nsec);

		/* search for a file with the same name and stamp in all the disks */
		for (i = state->disklist; i != 0; i = i->next) {
			struct snapraid_disk* other_disk = i->data;
			struct snapraid_file* other_file;

			/* if the nanosecond part of the time stamp is valid, search */
			/* for name and stamp, otherwise for path and stamp */
			if (file->mtime_nsec != 0 && file->mtime_nsec != STAT_NSEC_INVALID)
				other_file = tommy_hashdyn_search(&other_disk->stampset, file_namestamp_compare, file, hash);
			else
				other_file = tommy_hashdyn_search(&other_disk->stampset, file_pathstamp_compare, file, hash);

			/* if found, and it's a fully hashed file */
			if (other_file && file_is_full_hashed_and_stable(scan->state, other_file)) {
				/* assume that the file is a copy, and reuse the hash */
				file_copy(other_file, file);

				/* revert old counter and use the copy one */
				++scan->count_copy;

				msg_tag("scan:copy:%s:%s:%s:%s\n", other_disk->name, esc(other_file->sub), disk->name, esc(file->sub));
				if (is_diff) {
					printf("copy %s%s -> %s%s\n", other_disk->dir, other_file->sub, disk->dir, file->sub);
				}

				/* mark it as reported */
				is_file_reported = 1;

				/* no need to continue the search */
				break;
			}
		}
	}

	/* if not yet reported, do it now */
	if (!is_file_reported) {
		if (is_file_already_present) {
			++scan->count_change;

			msg_tag("scan:update:%s:%s\n", disk->name, esc(sub));
			if (is_diff) {
				printf("update %s%s\n", disk->dir, sub);
			}
		} else {
			++scan->count_insert;

			msg_tag("scan:add:%s:%s\n", disk->name, esc(sub));
			if (is_diff) {
				printf("add %s%s\n", disk->dir, sub);
			}
		}
	}

	/* insert the file in the file hashtables, to allow to find duplicate hardlinks */
	tommy_hashdyn_insert(&disk->inodeset, &file->nodeset, file, file_inode_hash(file->inode));
	tommy_hashdyn_insert(&disk->pathset, &file->pathset, file, file_path_hash(file->sub));
	tommy_hashdyn_insert(&disk->stampset, &file->stampset, file, file_stamp_hash(file->size, file->mtime_sec, file->mtime_nsec));

	/* insert the file in the delayed block allocation */
	scan_file_delayed_insert(scan, file);
}

/**
 * Removes the specified dir from the data set.
 */
static void scan_emptydir_remove(struct snapraid_scan* scan, struct snapraid_dir* dir)
{
	struct snapraid_state* state = scan->state;
	struct snapraid_disk* disk = scan->disk;

	/* state changed */
	state->need_write = 1;

	/* remove the file from the dir containers */
	tommy_hashdyn_remove_existing(&disk->dirset, &dir->nodeset);
	tommy_list_remove_existing(&disk->dirlist, &dir->nodelist);

	/* deallocate */
	dir_free(dir);
}

/**
 * Inserts the specified dir in the data set.
 */
static void scan_emptydir_insert(struct snapraid_scan* scan, struct snapraid_dir* dir)
{
	struct snapraid_state* state = scan->state;
	struct snapraid_disk* disk = scan->disk;

	/* state changed */
	state->need_write = 1;

	/* insert the dir in the dir containers */
	tommy_hashdyn_insert(&disk->dirset, &dir->nodeset, dir, dir_name_hash(dir->sub));
	tommy_list_insert_tail(&disk->dirlist, &dir->nodelist, dir);
}

/**
 * Processes a dir.
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
			msg_error("Internal inconsistency for dir '%s%s'\n", disk->dir, sub);
			exit(EXIT_FAILURE);
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
 * Returns the stat info of a dir entry.
 */
#if HAVE_STRUCT_DIRENT_D_STAT
#define DSTAT(file, dd, buf) dstat(dd)
struct stat* dstat(struct dirent_sorted* dd)
{
	return &dd->d_stat;
}
#else
#define DSTAT(file, dd, buf) dstat(file, buf)
struct stat* dstat(const char* file, struct stat* st)
{
	if (lstat(file, st) != 0) {
		/* LCOV_EXCL_START */
		msg_error("Error in stat file/directory '%s'. %s.\n", file, strerror(errno));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}
	return st;
}
#endif

/**
 * Processes a directory.
 * Return != 0 if at least one file or link is processed.
 */
static int scan_dir(struct snapraid_scan* scan, int is_diff, const char* dir, const char* sub)
{
	struct snapraid_state* state = scan->state;
	struct snapraid_disk* disk = scan->disk;
	int processed = 0;
	DIR* d;
	tommy_list list;
	tommy_node* node;

	tommy_list_init(&list);

	d = opendir(dir);
	if (!d) {
		/* LCOV_EXCL_START */
		msg_error("Error opening directory '%s'. %s.\n", dir, strerror(errno));
		msg_error("You can exclude it in the config file with:\n\texclude /%s\n", sub);
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* read the full directory */
	while (1) {
		char path_next[PATH_MAX];
		char sub_next[PATH_MAX];
		struct dirent_sorted* entry;
		const char* name;
		struct dirent* dd;
		size_t name_len;

		/* clear errno to detect erroneous conditions */
		errno = 0;
		dd = readdir(d);
		if (dd == 0 && errno != 0) {
			/* LCOV_EXCL_START */
			msg_error("Error reading directory '%s'. %s.\n", dir, strerror(errno));
			msg_error("You can exclude it in the config file with:\n\texclude /%s\n", sub);
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

		pathprint(path_next, sizeof(path_next), "%s%s", dir, name);
		pathprint(sub_next, sizeof(sub_next), "%s%s", sub, name);

		/* check for not supported file names */
		if (name[0] == 0) {
			/* LCOV_EXCL_START */
			msg_error("Unsupported name '%s' in file '%s'.\n", name, path_next);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		/* exclude hidden files even before calling lstat() */
		if (filter_hidden(state->filter_hidden, dd) != 0) {
			msg_verbose("Excluding hidden '%s'\n", path_next);
			continue;
		}

		/* exclude content files even before calling lstat() */
		if (filter_content(&state->contentlist, path_next) != 0) {
			msg_verbose("Excluding content '%s'\n", path_next);
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
	}

	if (closedir(d) != 0) {
		/* LCOV_EXCL_START */
		msg_error("Error closing directory '%s'. %s.\n", dir, strerror(errno));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	if (state->opt.force_order == SORT_ALPHA) {
		/* if requested sort alphabetically */
		/* this is mainly done for testing to ensure to always */
		/* process in the same way in different platforms */
		tommy_list_sort(&list, dd_name_compare);
	}
#if HAVE_STRUCT_DIRENT_D_INO
	else if (!disk->has_volatile_inodes) {
		/* if inodes are persistent */
		/* sort the list of dir entries by inodes */
		tommy_list_sort(&list, dd_ino_compare);
	}
	/* otherwise just keep the insertion order */
#endif

	/* process the sorted dir entries */
	node = list;
	while (node != 0) {
		char path_next[PATH_MAX];
		char sub_next[PATH_MAX];
		char out[PATH_MAX];
		struct snapraid_filter* reason = 0;
		struct dirent_sorted* dd = node->data;
		const char* name = dd->d_name;
		struct stat* st;
		int type;
#if !HAVE_STRUCT_DIRENT_D_STAT
		struct stat st_buf;
#endif

		pathprint(path_next, sizeof(path_next), "%s%s", dir, name);
		pathprint(sub_next, sizeof(sub_next), "%s%s", sub, name);

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
			/* if the st_mode field is missing, takes care to fill it using normal lstat() */
			/* at now this can happen only in Windows (with HAVE_STRUCT_DIRENT_D_STAT defined), */
			/* because we use a directory reading method that doesn't read info about ReparsePoint. */
			/* Note that here we cannot call here lstat_sync(), because we don't know what kind */
			/* of file is it, and lstat_sync() doesn't always work */
			if (st->st_mode == 0) {
				if (lstat(path_next, st) != 0) {
					/* LCOV_EXCL_START */
					msg_error("Error in stat file/directory '%s'. %s.\n", path_next, strerror(errno));
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
			if (filter_path(&state->filterlist, &reason, disk->name, sub_next) == 0) {

				/* late stat, if not yet called */
				if (!st)
					st = DSTAT(path_next, dd, &st_buf);

#if HAVE_LSTAT_SYNC
				/* if the st_ino field is missing, takes care to fill it using the extended lstat() */
				/* this can happen only in Windows */
				if (st->st_ino == 0) {
					if (lstat_sync(path_next, st, 0) != 0) {
						/* LCOV_EXCL_START */
						msg_error("Error in stat file '%s'. %s.\n", path_next, strerror(errno));
						exit(EXIT_FAILURE);
						/* LCOV_EXCL_STOP */
					}
				}
#endif

				scan_file(scan, is_diff, sub_next, st, FILEPHY_UNREAD_OFFSET);
				processed = 1;
			} else {
				msg_verbose("Excluding file '%s' for rule '%s'\n", path_next, filter_type(reason, out, sizeof(out)));
			}
		} else if (type == 1) { /* LNK */
			if (filter_path(&state->filterlist, &reason, disk->name, sub_next) == 0) {
				char subnew[PATH_MAX];
				int ret;

				ret = readlink(path_next, subnew, sizeof(subnew));
				if (ret >= PATH_MAX) {
					/* LCOV_EXCL_START */
					msg_error("Error in readlink file '%s'. Symlink too long.\n", path_next);
					exit(EXIT_FAILURE);
					/* LCOV_EXCL_STOP */
				}
				if (ret < 0) {
					/* LCOV_EXCL_START */
					msg_error("Error in readlink file '%s'. %s.\n", path_next, strerror(errno));
					exit(EXIT_FAILURE);
					/* LCOV_EXCL_STOP */
				}

				/* readlink doesn't put the final 0 */
				subnew[ret] = 0;

				/* process as a symbolic link */
				scan_link(scan, is_diff, sub_next, subnew, FILE_IS_SYMLINK);
				processed = 1;
			} else {
				msg_verbose("Excluding link '%s' for rule '%s'\n", path_next, filter_type(reason, out, sizeof(out)));
			}
		} else if (type == 2) { /* DIR */
			if (filter_dir(&state->filterlist, &reason, disk->name, sub_next) == 0) {
#ifndef _WIN32
				/* late stat, if not yet called */
				if (!st)
					st = DSTAT(path_next, dd, &st_buf);

				/* in Unix don't follow mount points in different devices */
				/* in Windows we are already skipping them reporting them as special files */
				if ((uint64_t)st->st_dev != disk->device) {
					msg_error("WARNING! Ignoring mount point '%s' because it appears to be in a different device\n", path_next);
				} else
#endif
				{
					char sub_dir[PATH_MAX];

					/* recurse */
					pathslash(path_next, sizeof(path_next));
					pathcpy(sub_dir, sizeof(sub_dir), sub_next);
					pathslash(sub_dir, sizeof(sub_dir));
					if (scan_dir(scan, is_diff, path_next, sub_dir) == 0) {
						/* scan the directory as empty dir */
						scan_emptydir(scan, sub_next);
					}
					/* or we processed something internally, or we have added the empty dir */
					processed = 1;
				}
			} else {
				msg_verbose("Excluding directory '%s' for rule '%s'\n", path_next, filter_type(reason, out, sizeof(out)));
			}
		} else {
			if (filter_path(&state->filterlist, &reason, disk->name, sub_next) == 0) {
				/* late stat, if not yet called */
				if (!st)
					st = DSTAT(path_next, dd, &st_buf);

				msg_error("WARNING! Ignoring special '%s' file '%s'\n", stat_desc(st), path_next);
			} else {
				msg_verbose("Excluding special file '%s' for rule '%s'\n", path_next, filter_type(reason, out, sizeof(out)));
			}
		}

		/* next entry */
		node = node->next;

		/* free the present one */
		free(dd);
	}

	return processed;
}

static int state_diffscan(struct snapraid_state* state, int is_diff)
{
	tommy_node* i;
	tommy_node* j;
	tommy_list scanlist;
	int done;
	fptr* msg;
	struct snapraid_scan total;
	int no_difference;

	tommy_list_init(&scanlist);

	if (is_diff)
		msg_progress("Comparing...\n");

	/* first scan all the directory and find new and deleted files */
	for (i = state->disklist; i != 0; i = i->next) {
		struct snapraid_disk* disk = i->data;
		struct snapraid_scan* scan;
		tommy_node* node;
		int ret;
		int has_persistent_inode;

		scan = malloc_nofail(sizeof(struct snapraid_scan));
		scan->state = state;
		scan->disk = disk;
		scan->count_equal = 0;
		scan->count_move = 0;
		scan->count_copy = 0;
		scan->count_restore = 0;
		scan->count_change = 0;
		scan->count_remove = 0;
		scan->count_insert = 0;
		tommy_list_init(&scan->file_insert_list);
		tommy_list_init(&scan->link_insert_list);
		tommy_list_init(&scan->dir_insert_list);

		tommy_list_insert_tail(&scanlist, &scan->node, scan);

		if (!is_diff)
			msg_progress("Scanning disk %s...\n", disk->name);

		/* check if the disk supports persistent inodes */
		ret = fsinfo(disk->dir, &has_persistent_inode, 0, 0);
		if (ret < 0) {
			/* LCOV_EXCL_START */
			msg_error("Error accessing disk '%s' to get filesystem info. %s.\n", disk->dir, strerror(errno));
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
		if (!has_persistent_inode) {
			disk->has_volatile_inodes = 1;
		}

		/* if inodes or UUID are not persistent/changed/unsupported */
		if (disk->has_volatile_inodes || disk->has_different_uuid || disk->has_unsupported_uuid) {
			/* removes all the inodes from the inode collection */
			/* if they are not persistent, all of them could be changed now */
			/* and we don't want to find false matching ones */
			/* see scan_file() for more details */
			node = disk->filelist;
			while (node) {
				struct snapraid_file* file = node->data;

				node = node->next;

				/* remove from the inode set */
				tommy_hashdyn_remove_existing(&disk->inodeset, &file->nodeset);

				/* clear the inode */
				file->inode = 0;

				/* mark as missing inode */
				file_flag_set(file, FILE_IS_WITHOUT_INODE);
			}
		}

		scan_dir(scan, is_diff, disk->dir, "");
	}

	/* we split the search in two phases because to detect files */
	/* moved from one disk to another we have to start deletion */
	/* only when all disks have all the new files found */

	/* now process all the new and deleted files */
	for (i = scanlist; i != 0; i = i->next) {
		struct snapraid_scan* scan = i->data;
		struct snapraid_disk* disk = scan->disk;
		tommy_node* node;
		unsigned phy_count;
		unsigned phy_dup;
		uint64_t phy_last;
		struct snapraid_file* phy_file_last;

		/* check for removed files */
		node = disk->filelist;
		while (node) {
			struct snapraid_file* file = node->data;

			/* next node */
			node = node->next;

			/* remove if not present */
			if (!file_flag_has(file, FILE_IS_PRESENT)) {
				++scan->count_remove;

				msg_tag("scan:remove:%s:%s\n", disk->name, esc(file->sub));
				if (is_diff) {
					printf("remove %s%s\n", disk->dir, file->sub);
				}

				scan_file_remove(scan, file);
			}
		}

		/* check for removed links */
		node = disk->linklist;
		while (node) {
			struct snapraid_link* link = node->data;

			/* next node */
			node = node->next;

			/* remove if not present */
			if (!link_flag_has(link, FILE_IS_PRESENT)) {
				++scan->count_remove;

				msg_tag("scan:remove:%s:%s\n", disk->name, esc(link->sub));
				if (is_diff) {
					printf("remove %s%s\n", disk->dir, link->sub);
				}

				scan_link_remove(scan, link);
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

		/* sort the files before inserting them */
		/* we use a stable sort to ensure that if the reported physical offset/inode */
		/* are always 0, we keep at least the directory order */
		switch (state->opt.force_order) {
		case SORT_PHYSICAL :
			tommy_list_sort(&scan->file_insert_list, file_physical_compare);
			break;
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

		/* insert all the new files, we insert them only after the deletion */
		/* to reuse the just freed space */
		/* also check if the physical offset reported are fakes or not */
		node = scan->file_insert_list;
		phy_count = 0;
		phy_dup = 0;
		phy_last = FILEPHY_UNREAD_OFFSET;
		phy_file_last = 0;
		while (node) {
			struct snapraid_file* file = node->data;

			/* if the file is not empty, count duplicate physical offsets */
			if (state->opt.force_order == SORT_PHYSICAL && file->size != 0) {
				if (phy_file_last != 0 && file->physical == phy_last
				        /* files without offset are expected to have duplicates */
					&& phy_last != FILEPHY_WITHOUT_OFFSET
				) {
					/* if verbose, prints the list of duplicates real offsets */
					/* other cases are for offsets not supported, so we don't need to report them file by file */
					if (phy_last >= FILEPHY_REAL_OFFSET) {
						msg_error("WARNING! Files '%s%s' and '%s%s' have the same physical offset %" PRId64 ".\n", disk->dir, phy_file_last->sub, disk->dir, file->sub, phy_last);
					}
					++phy_dup;
				}
				phy_file_last = file;
				phy_last = file->physical;
				++phy_count;
			}

			/* next node */
			node = node->next;

			/* insert it */
			scan_file_insert(scan, file);
		}

		/* mark the disk without reliable physical offset if it has duplicates */
		/* here it should never happen because we already sorted out hardlinks */
		if (state->opt.force_order == SORT_PHYSICAL && phy_dup > 0) {
			disk->has_unreliable_physical = 1;
		}

		/* insert all the new links */
		node = scan->link_insert_list;
		while (node) {
			struct snapraid_link* link = node->data;

			/* next node */
			node = node->next;

			/* insert it */
			scan_link_insert(scan, link);
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

	/* checks for disks where all the previously existing files where removed */
	if (!state->opt.force_empty) {
		done = 0;
		for (i = state->disklist, j = scanlist; i != 0; i = i->next, j = j->next) {
			struct snapraid_disk* disk = i->data;
			struct snapraid_scan* scan = j->data;

			if (scan->count_equal == 0 && scan->count_move == 0 && scan->count_restore == 0 && (scan->count_remove != 0 || scan->count_change != 0)) {
				if (!done) {
					done = 1;
					msg_error("WARNING! All the files previously present in disk '%s' at dir '%s'", disk->name, disk->dir);
				} else {
					msg_error(", disk '%s' at dir '%s'", disk->name, disk->dir);
				}
			}
		}
		if (done) {
			msg_error("\nare now missing or rewritten!\n");
			msg_error("This could happen when deleting all the files from a disk,\n");
			msg_error("and restoring them with a program that it's not setting\n");
			msg_error("correctly the timestamps.\n");
			msg_error("It's also possible that you have some disks not mounted.\n");
			if (!is_diff) {
				msg_error("If you want to '%s' anyway, use 'snapraid --force-empty %s'.", state->command, state->command);
				exit(EXIT_FAILURE);
			}
		}
	}

	/* checks for disks without the physical offset support */
	if (state->opt.force_order == SORT_PHYSICAL) {
		done = 0;
		for (i = state->disklist; i != 0; i = i->next) {
			struct snapraid_disk* disk = i->data;

			if (disk->has_unreliable_physical) {
				if (!done) {
					done = 1;
					msg_error("WARNING! Physical offsets not supported for disk '%s'", disk->name);
				} else {
					msg_error(", '%s'", disk->name);
				}
			}
		}
		if (done) {
			msg_error(". Files order won't be optimal.\n");
		}
	}

	/* checks for disks without persistent inodes */
	done = 0;
	for (i = state->disklist; i != 0; i = i->next) {
		struct snapraid_disk* disk = i->data;

		if (disk->has_volatile_inodes) {
			if (!done) {
				done = 1;
				msg_error("WARNING! Inodes are not persistent for disks: '%s'", disk->name);
			} else {
				msg_error(", '%s'", disk->name);
			}
		}
	}
	if (done) {
		msg_error(". Move operations won't be optimal.\n");
	}

	/* checks for disks with changed UUID */
	done = 0;
	for (i = state->disklist; i != 0; i = i->next) {
		struct snapraid_disk* disk = i->data;

		/* don't print the message if the UUID changed because before */
		/* it was no set. */
		/* this is the normal condition for an empty disk because it */
		/* isn't stored */
		if (disk->has_different_uuid && !disk->had_empty_uuid) {
			if (!done) {
				done = 1;
				msg_error("WARNING! UUID is changed for disks: '%s'", disk->name);
			} else {
				msg_error(", '%s'", disk->name);
			}
		}
	}
	if (done) {
		msg_error(". Move operations won't be optimal.\n");
	}

	/* checks for disks with unsupported UUID */
	done = 0;
	for (i = state->disklist; i != 0; i = i->next) {
		struct snapraid_disk* disk = i->data;

		if (disk->has_unsupported_uuid) {
			if (!done) {
				done = 1;
				msg_error("WARNING! UUID is unsupported for disks: '%s'", disk->name);
			} else {
				msg_error(", '%s'", disk->name);
			}
		}
	}
	if (done) {
		msg_error(". Move operations won't be optimal.\n");
	}

	total.count_equal = 0;
	total.count_move = 0;
	total.count_copy = 0;
	total.count_restore = 0;
	total.count_change = 0;
	total.count_remove = 0;
	total.count_insert = 0;

	for (i = scanlist; i != 0; i = i->next) {
		struct snapraid_scan* scan = i->data;
		total.count_equal += scan->count_equal;
		total.count_move += scan->count_move;
		total.count_copy += scan->count_copy;
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
	msg("%8u moved\n", total.count_move);
	msg("%8u copied\n", total.count_copy);
	msg("%8u restored\n", total.count_restore);
	msg("%8u updated\n", total.count_change);
	msg("%8u removed\n", total.count_remove);
	msg("%8u added\n", total.count_insert);

	msg_tag("summary:equal:%u\n", total.count_equal);
	msg_tag("summary:moved:%u\n", total.count_move);
	msg_tag("summary:copied:%u\n", total.count_copy);
	msg_tag("summary:restored:%u\n", total.count_restore);
	msg_tag("summary:updated:%u\n", total.count_change);
	msg_tag("summary:removed:%u\n", total.count_remove);
	msg_tag("summary:added:%u\n", total.count_insert);

	no_difference = !total.count_move && !total.count_copy && !total.count_restore
		&& !total.count_change && !total.count_remove && !total.count_insert;

	if (is_diff) {
		if (no_difference) {
			msg_status("No differences\n");
		} else {
			msg_status("There are differences!\n");
		}
	}

	if (no_difference) {
		msg_tag("summary:exit:equal\n");
	} else {
		msg_tag("summary:exit:diff\n");
	}
	msg_flush();

	tommy_list_foreach(&scanlist, (tommy_foreach_func*)free);

	if (is_diff) {
		/* check for file difference */
		if (!no_difference)
			return -1;

		/* check also for incomplete "sync" */
		if (parity_is_invalid(state))
			return -1;
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

