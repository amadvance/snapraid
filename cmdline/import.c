/*
 * Copyright (C) 2013 Andrea Mazzoleni
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

#include "import.h"

/****************************************************************************/
/* import */

/**
 * Compares the hash of two import blocks.
 */
int import_block_hash_compare(const void* void_arg, const void* void_data)
{
	const unsigned char* arg = void_arg;
	const struct snapraid_import_block* block = void_data;

	return memcmp(arg, block->hash, HASH_SIZE);
}

int import_block_prevhash_compare(const void* void_arg, const void* void_data)
{
	const unsigned char* arg = void_arg;
	const struct snapraid_import_block* block = void_data;

	return memcmp(arg, block->prevhash, HASH_SIZE);
}

/**
 * Computes the hash of the hash for an import block.
 * We just use the first 32 bit of the hash itself.
 */
static inline tommy_uint32_t import_block_hash(const unsigned char* hash)
{
	return *(const tommy_uint32_t*)hash;
}

static void import_file(struct snapraid_state* state, const char* path, uint64_t size)
{
	struct snapraid_import_file* file;
	block_off_t i;
	data_off_t offset;
	void* buffer;
	int ret;
	int f;
	int flags;
	unsigned block_size = state->block_size;

	file = malloc_nofail(sizeof(struct snapraid_import_file));
	file->path = strdup_nofail(path);
	file->size = size;
	file->blockmax = (size + block_size - 1) / block_size;
	file->blockvec = malloc_nofail(file->blockmax * sizeof(struct snapraid_import_block));

	buffer = malloc_nofail(block_size);

	/* open for read */
	/* O_SEQUENTIAL: opening in sequential mode in Windows */
	flags = O_RDONLY | O_BINARY;
	if ((state->file_mode & MODE_SEQUENTIAL) != 0)
		flags |= O_SEQUENTIAL;
	f = open(path, flags);
	if (f == -1) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Error opening file '%s'. %s.\n", path, strerror(errno));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

#if HAVE_POSIX_FADVISE
	if ((state->file_mode & MODE_SEQUENTIAL) != 0) {
		/* advise sequential access */
		ret = posix_fadvise(f, 0, 0, POSIX_FADV_SEQUENTIAL);
		if (ret != 0) {
			/* LCOV_EXCL_START */
			fprintf(stderr, "Error advising file '%s'. %s.\n", path, strerror(ret));
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}
#endif

	offset = 0;
	for (i = 0; i < file->blockmax; ++i) {
		struct snapraid_import_block* block = &file->blockvec[i];
		unsigned read_size = block_size;
		if (read_size > size)
			read_size = size;

		ret = read(f, buffer, read_size);
		if (ret < 0 || (unsigned)ret != read_size) {
			/* LCOV_EXCL_START */
			fprintf(stderr, "Error reading file '%s'. %s.\n", path, strerror(errno));
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		block->file = file;
		block->offset = offset;
		block->size = read_size;

		memhash(state->hash, state->hashseed, block->hash, buffer, read_size);
		tommy_hashdyn_insert(&state->importset, &block->nodeset, block, import_block_hash(block->hash));

		/* if we are in a rehash state */
		if (state->prevhash != HASH_UNDEFINED) {
			/* compute also the previous hash */
			memhash(state->prevhash, state->prevhashseed, block->prevhash, buffer, read_size);
			tommy_hashdyn_insert(&state->previmportset, &block->prevnodeset, block, import_block_hash(block->prevhash));
		}

		offset += read_size;
		size -= read_size;
	}

	ret = close(f);
	if (ret != 0) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Error closing file '%s'. %s.\n", path, strerror(errno));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	tommy_list_insert_tail(&state->importlist, &file->nodelist, file);

	free(buffer);
}

void import_file_free(struct snapraid_import_file* file)
{
	free(file->path);
	free(file->blockvec);
	free(file);
}

int state_import_fetch(struct snapraid_state* state, int rehash, struct snapraid_block* missing_block, unsigned char* buffer)
{
	struct snapraid_import_block* block;
	int ret;
	int f;
	const unsigned char* hash = missing_block->hash;
	unsigned block_size = state->block_size;
	unsigned read_size;
	unsigned char buffer_hash[HASH_SIZE];
	const char* path;

	if (rehash) {
		block = tommy_hashdyn_search(&state->previmportset, import_block_prevhash_compare, hash, import_block_hash(hash));
	} else {
		block = tommy_hashdyn_search(&state->importset, import_block_hash_compare, hash, import_block_hash(hash));
	}
	if (!block)
		return -1;

	path = block->file->path;
	read_size = block->size;

	f = open(path, O_RDONLY | O_BINARY);
	if (f == -1) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Error opening file '%s'. %s.\n", path, strerror(errno));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	if (lseek(f, block->offset, SEEK_SET) != block->offset) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Error seeking file '%s'. %s.\n", path, strerror(errno));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	ret = read(f, buffer, read_size);
	if (ret < 0 || (unsigned)ret != read_size) {
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

	if (read_size != block_size) {
		/* fill the remaining with 0 */
		memset(buffer + read_size, 0, block_size - read_size);
	}

	/* recheck the hash */
	if (rehash)
		memhash(state->prevhash, state->prevhashseed, buffer_hash, buffer, read_size);
	else
		memhash(state->hash, state->hashseed, buffer_hash, buffer, read_size);

	if (memcmp(buffer_hash, hash, HASH_SIZE) != 0) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Error in data reading file '%s'.\n", path);
		fprintf(stderr, "Please don't change imported files while running.\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	return 0;
}

static void import_dir(struct snapraid_state* state, const char* dir)
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
			import_file(state, path, st.st_size);
		} else if (S_ISDIR(st.st_mode)) {
			import_dir(state, path);
		}
	}

	if (closedir(d) != 0) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Error closing directory '%s'. %s.\n", dir, strerror(errno));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}
}

void state_import(struct snapraid_state* state, const char* dir)
{
	printf("Importing...\n");

	import_dir(state, dir);
}

