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
	unsigned char buffer_hash[HASH_SIZE];
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
		fprintf(stderr, "Error opening file '%s'. %s.\n", path, strerror(errno));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	if (lseek(f, arg->offset, SEEK_SET) != arg->offset) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Error seeking file '%s'. %s.\n", path, strerror(errno));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	ret = read(f, arg->buffer, arg->read_size);
	if (ret < 0 || (unsigned)ret != arg->read_size) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Error reading file '%s'. %s.\n", path, strerror(errno));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	ret = close(f);
	if (ret != 0) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Error closing file '%s'. %s.\n", path, strerror(errno));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* compute the hash */
	if (arg->prevhash)
		memhash(state->prevhash, state->prevhashseed, buffer_hash, arg->buffer, arg->read_size);
	else
		memhash(state->hash, state->hashseed, buffer_hash, arg->buffer, arg->read_size);

	/* check if the hash is matching */
	if (memcmp(buffer_hash, arg->block->hash, HASH_SIZE) != 0)
		return -1;

	if (arg->read_size != state->block_size) {
		/* fill the remaining with 0 */
		memset(arg->buffer + arg->read_size, 0, state->block_size - arg->read_size);
	}

	return 0;
}

int state_search_fetch(struct snapraid_state* state, int prevhash, struct snapraid_block* missing_block, unsigned char* buffer)
{
	struct snapraid_search_file* file;
	tommy_uint32_t file_hash;
	struct search_file_compare_arg arg;

	arg.state = state;
	arg.block = missing_block;
	arg.file = block_file_get(missing_block);
	arg.buffer = buffer;
	arg.offset = state->block_size * (data_off_t)block_file_pos(missing_block);
	arg.read_size = block_file_size(missing_block, state->block_size);
	arg.prevhash = prevhash;

	file_hash = file_stamp_hash(arg.file->size, arg.file->mtime_sec, arg.file->mtime_nsec);

	/* search in the hashtable, and also check if the data matches the hash */
	file = tommy_hashdyn_search(&state->searchset, search_file_compare, &arg, file_hash);
	if (!file)
		return -1;

	/* if found, buffer is already set with data */
	return 0;
}

static void search_dir(struct snapraid_state* state, const char* dir)
{
	DIR* d;

	d = opendir(dir);
	if (!d) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Error opening directory '%s'. %s.\n", dir, strerror(errno));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	while (1) {
		char path[PATH_MAX];
		struct stat st;
		const char* name;
		struct dirent* dd;

		/* clear errno to detect erroneous conditions */
		errno = 0;
		dd = readdir(d);
		if (dd == 0 && errno != 0) {
			/* LCOV_EXCL_START */
			fprintf(stderr, "Error reading directory '%s'. %s.\n", dir, strerror(errno));
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

		pathprint(path, sizeof(path), "%s", dir);
		pathslash(path, sizeof(path));
		pathcat(path, sizeof(path), name);

#if HAVE_STRUCT_DIRENT_D_STAT
		/* convert dirent to lstat result */
		dirent_lstat(dd, &st);

		/* if the st_mode field is missing, takes care to fill it using normal lstat() */
		/* at now this can happen only in Windows (with HAVE_STRUCT_DIRENT_D_STAT defined), */
		/* because we use a directory reading method that doesn't read info about ReparsePoint. */
		/* Note that here we cannot call here lstat_sync(), because we don't know what kind */
		/* of file is it, and lstat_sync() doesn't always work */
		if (st.st_mode == 0)  {
			if (lstat(path, &st) != 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Error in stat file/directory '%s'. %s.\n", path, strerror(errno));
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
		}
#else
		/* get lstat info about the file */
		if (lstat(path, &st) != 0) {
			/* LCOV_EXCL_START */
			fprintf(stderr, "Error in stat file/directory '%s'. %s.\n", path, strerror(errno));
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
#endif

		if (S_ISREG(st.st_mode)) {
			search_file(state, path, st.st_size, st.st_mtime, STAT_NSEC(&st));
		} else if (S_ISDIR(st.st_mode)) {
			search_dir(state, path);
		}
	}

	if (closedir(d) != 0) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Error closing directory '%s'. %s.\n", dir, strerror(errno));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}
}

void state_search(struct snapraid_state* state, const char* dir)
{
	printf("Importing...\n");

	search_dir(state, dir);
}

void state_search_array(struct snapraid_state* state)
{
	tommy_node* i;

	/* import from all the disks */
	for (i = state->disklist; i != 0; i = i->next) {
		struct snapraid_disk* disk = i->data;

		printf("Scanning disk %s...\n", disk->name);

		search_dir(state, disk->dir);
	}
}

