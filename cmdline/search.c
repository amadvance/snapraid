/*
 * Copyright (C) 2014 Andrea Mazzoleni
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
#include "search.h"

/****************************************************************************/
/* search */

static void search_file(struct snapraid_state* state, const char* path, data_off_t size, int64_t mtime_sec, int mtime_nsec)
{
	struct snapraid_search_file* file;
	tommy_uint32_t file_hash;

	file = malloc_nofail(sizeof(struct snapraid_search_file));
	file->path = strdup_nofail(path);
	file->size = size;
	file->mtime_sec = mtime_sec;
	file->mtime_nsec = mtime_nsec;

	file_hash = file_stamp_hash(file->size, file->mtime_sec, file->mtime_nsec);

	tommy_hashdyn_insert(&state->searchset, &file->node, file, file_hash);
}

void search_file_free(struct snapraid_search_file* file)
{
	free(file->path);
	free(file);
}

struct search_file_compare_arg {
	const struct snapraid_state* state;
	const struct snapraid_block* block;
	const struct snapraid_file* file;
	unsigned char* buffer;
	data_off_t offset;
	unsigned read_size;
	int prevhash;
};

int search_file_compare(const void* void_arg, const void* void_data)
{
	const struct search_file_compare_arg* arg = void_arg;
	const struct snapraid_search_file* file = void_data;
	const struct snapraid_state* state = arg->state;
	unsigned char buffer_hash[HASH_MAX];
	const char* path = file->path;
	int f;
	ssize_t ret;

	/* compare file info */
	if (arg->file->size != file->size)
		return -1;

	if (arg->file->mtime_sec != file->mtime_sec)
		return -1;

	if (arg->file->mtime_nsec != file->mtime_nsec)
		return -1;

	/* read the block and compare the hash */
	f = open(path, O_RDONLY | O_BINARY);
	if (f == -1) {
		/* LCOV_EXCL_START */
		if (errno == ENOENT) {
			log_fatal("DANGER! file '%s' disappeared.\n", path);
			log_fatal("If you moved it, please rerun the same command.\n");
		} else {
			log_fatal("Error opening file '%s'. %s.\n", path, strerror(errno));
		}
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	ret = pread(f, arg->buffer, arg->read_size, arg->offset);
	if (ret < 0 || (unsigned)ret != arg->read_size) {
		/* LCOV_EXCL_START */
		log_fatal("Error reading file '%s'. %s.\n", path, strerror(errno));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	ret = close(f);
	if (ret != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Error closing file '%s'. %s.\n", path, strerror(errno));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* compute the hash */
	if (arg->prevhash)
		memhash(state->prevhash, state->prevhashseed, buffer_hash, arg->buffer, arg->read_size);
	else
		memhash(state->hash, state->hashseed, buffer_hash, arg->buffer, arg->read_size);

	/* check if the hash is matching */
	if (memcmp(buffer_hash, arg->block->hash, BLOCK_HASH_SIZE) != 0)
		return -1;

	if (arg->read_size != state->block_size) {
		/* fill the remaining with 0 */
		memset(arg->buffer + arg->read_size, 0, state->block_size - arg->read_size);
	}

	return 0;
}

int state_search_fetch(struct snapraid_state* state, int prevhash, struct snapraid_file* missing_file, block_off_t missing_file_pos, struct snapraid_block* missing_block, unsigned char* buffer)
{
	struct snapraid_search_file* file;
	tommy_uint32_t file_hash;
	struct search_file_compare_arg arg;

	arg.state = state;
	arg.block = missing_block;
	arg.file = missing_file;
	arg.buffer = buffer;
	arg.offset = state->block_size * (data_off_t)missing_file_pos;
	arg.read_size = file_block_size(missing_file, missing_file_pos, state->block_size);
	arg.prevhash = prevhash;

	file_hash = file_stamp_hash(arg.file->size, arg.file->mtime_sec, arg.file->mtime_nsec);

	/* search in the hashtable, and also check if the data matches the hash */
	file = tommy_hashdyn_search(&state->searchset, search_file_compare, &arg, file_hash);
	if (!file)
		return -1;

	/* if found, buffer is already set with data */
	return 0;
}

static void search_dir(struct snapraid_state* state, struct snapraid_disk* disk, const char* dir, const char* sub)
{
	DIR* d;

	d = opendir(dir);
	if (!d) {
		/* LCOV_EXCL_START */
		log_fatal("Error opening directory '%s'. %s.\n", dir, strerror(errno));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	while (1) {
		char path_next[PATH_MAX];
		char sub_next[PATH_MAX];
		char out[PATH_MAX];
		struct snapraid_filter* reason = 0;
		struct stat st;
		const char* name;
		struct dirent* dd;

		/* clear errno to detect erroneous conditions */
		errno = 0;
		dd = readdir(d);
		if (dd == 0 && errno != 0) {
			/* LCOV_EXCL_START */
			log_fatal("Error reading directory '%s'. %s.\n", dir, strerror(errno));
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

		/* exclude hidden files even before calling lstat() */
		if (disk != 0 && filter_hidden(state->filter_hidden, dd) != 0) {
			msg_verbose("Excluding hidden '%s'\n", path_next);
			continue;
		}

		/* exclude content files even before calling lstat() */
		if (disk != 0 && filter_content(&state->contentlist, path_next) != 0) {
			msg_verbose("Excluding content '%s'\n", path_next);
			continue;
		}

#if HAVE_STRUCT_DIRENT_D_STAT
		/* convert dirent to lstat result */
		dirent_lstat(dd, &st);

		/* if the st_mode field is missing, takes care to fill it using normal lstat() */
		/* at now this can happen only in Windows (with HAVE_STRUCT_DIRENT_D_STAT defined), */
		/* because we use a directory reading method that doesn't read info about ReparsePoint. */
		/* Note that here we cannot call here lstat_sync(), because we don't know what kind */
		/* of file is it, and lstat_sync() doesn't always work */
		if (st.st_mode == 0) {
			if (lstat(path_next, &st) != 0) {
				/* LCOV_EXCL_START */
				log_fatal("Error in stat file/directory '%s'. %s.\n", path_next, strerror(errno));
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
		}
#else
		/* get lstat info about the file */
		if (lstat(path_next, &st) != 0) {
			/* LCOV_EXCL_START */
			log_fatal("Error in stat file/directory '%s'. %s.\n", path_next, strerror(errno));
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
#endif

		if (S_ISREG(st.st_mode)) {
			if (disk == 0 || filter_path(&state->filterlist, &reason, disk->name, sub_next) == 0) {
				search_file(state, path_next, st.st_size, st.st_mtime, STAT_NSEC(&st));
			} else {
				msg_verbose("Excluding link '%s' for rule '%s'\n", path_next, filter_type(reason, out, sizeof(out)));
			}
		} else if (S_ISDIR(st.st_mode)) {
			if (disk == 0 || filter_subdir(&state->filterlist, &reason, disk->name, sub_next) == 0) {
				pathslash(path_next, sizeof(path_next));
				pathslash(sub_next, sizeof(sub_next));
				search_dir(state, disk, path_next, sub_next);
			} else {
				msg_verbose("Excluding directory '%s' for rule '%s'\n", path_next, filter_type(reason, out, sizeof(out)));
			}
		}
	}

	if (closedir(d) != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Error closing directory '%s'. %s.\n", dir, strerror(errno));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}
}

void state_search(struct snapraid_state* state, const char* dir)
{
	char path[PATH_MAX];

	msg_progress("Importing...\n");

	/* add the final slash */
	pathimport(path, sizeof(path), dir);
	pathslash(path, sizeof(path));

	search_dir(state, 0, path, "");
}

void state_search_array(struct snapraid_state* state)
{
	tommy_node* i;

	/* import from all the disks */
	for (i = state->disklist; i != 0; i = i->next) {
		struct snapraid_disk* disk = i->data;

		/* skip data disks that are not accessible */
		if (disk->skip_access)
			continue;

		msg_progress("Searching disk %s...\n", disk->name);

		search_dir(state, disk, disk->dir, "");
	}
}

