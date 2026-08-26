// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2013 Andrea Mazzoleni

#include "os/portable.h"

#include "support.h"
#include "import.h"

/****************************************************************************/
/* import */

/**
 * Compare the hash of two import blocks.
 */
int import_block_hash_compare(const void* void_arg, const void* void_data)
{
	const unsigned char* arg = void_arg;
	const struct snapraid_import_block* block = void_data;

	return memcmp(arg, block->hash, BLOCK_HASH_SIZE);
}

int import_block_prevhash_compare(const void* void_arg, const void* void_data)
{
	const unsigned char* arg = void_arg;
	const struct snapraid_import_block* block = void_data;

	return memcmp(arg, block->prevhash, BLOCK_HASH_SIZE);
}

/**
 * Compute the hash of the hash for an import block.
 * We just use the first 32 bit of the hash itself.
 */
static inline tommy_uint32_t import_block_hash(const unsigned char* hash)
{
	/* the hash data is not aligned, and we cannot access it with a direct cast */
	return hash[0] | ((uint32_t)hash[1] << 8) | ((uint32_t)hash[2] << 16) | ((uint32_t)hash[3] << 24);
}

static void import_file(struct snapraid_state* state, const char* path, uint64_t size)
{
	struct snapraid_import_file* file;
	block_off_t i;
	data_off_t offset;
	void* buffer;
	ssize_t ret;
	int f;
	int flags;
	unsigned block_size = state->block_size;
	struct advise_struct advise;
	block_off_t blockmax;

	blockmax = size / block_size;
	if (size % block_size != 0)
		++blockmax;

#if SIZE_MAX == UINT32_MAX
	if (blockmax > (block_off_t)(SIZE_MAX / sizeof(struct snapraid_import_block))) {
		log_fatal(ESOFT, "File '%s' with %" PRIu64 " blocks is too large for a 32-bit build. Use a 64-bit build or increase the block size.\n", path, blockmax);
		exit(EXIT_FAILURE);
	}
#endif

	file = malloc_nofail(sizeof(struct snapraid_import_file));
	file->path = strdup_nofail(path);
	file->size = size;
	file->blockmax = blockmax;
	file->blockimp = nalloc_nofail((size_t)file->blockmax, sizeof(struct snapraid_import_block));
	file->is_runtime = 1;

	buffer = malloc_nofail(block_size);

	advise_init(&advise, state->file_mode);

	/* open for read */
	flags = O_RDONLY | O_BINARY | advise_flags(&advise);
	f = open(path, flags);
	if (f == -1) {
		/* LCOV_EXCL_START */
		log_fatal(errno, "Error opening file '%s'. %s.\n", path, strerror(errno));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	ret = advise_open(&advise, f);
	if (ret != 0) {
		/* LCOV_EXCL_START */
		log_fatal(errno, "Error advising file '%s'. %s.\n", path, strerror(errno));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	offset = 0;
	for (i = 0; i < file->blockmax; ++i) {
		struct snapraid_import_block* block = &file->blockimp[i];
		size_t read_size = block_size;
		size_t count;
		if (read_size > size)
			read_size = size;

		count = 0;
		do {
			ret = read(f, (char*)buffer + count, read_size - count);
			if (ret < 0) {
				if (errno == EINTR)
					continue;

				/* LCOV_EXCL_START */
				log_fatal(errno, "Error reading file '%s'. %s.\n", path, strerror(errno));
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			if (ret == 0) {
				/* LCOV_EXCL_START */
				errno = ENXIO;
				log_fatal(errno, "Unexpected end of file '%s'. %s.\n", path, strerror(errno));
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			count += ret;
		} while (count < read_size);

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

	advise_close(&advise, f);

	ret = close(f);
	if (ret != 0) {
		/* LCOV_EXCL_START */
		log_fatal(errno, "Error closing file '%s'. %s.\n", path, strerror(errno));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	tommy_list_insert_tail(&state->importlist, &file->nodelist, file);

	free(buffer);
}

static void import_dealloc(struct snapraid_state* state, const char* dir, struct snapraid_dealloc* dealloc)
{
	char path[PATH_MAX];
	struct snapraid_import_file* file;
	block_off_t i;
	block_off_t blockmax;
	data_off_t offset;
	data_off_t size;
	unsigned block_size = state->block_size;

	pathcpy(path, sizeof(path), dir);
	pathcat(path, sizeof(path), dealloc->sub);
	size = dealloc->size;

	blockmax = size / block_size;
	if (size % block_size != 0)
		++blockmax;

#if SIZE_MAX == UINT32_MAX
	if (blockmax > (block_off_t)(SIZE_MAX / sizeof(struct snapraid_import_block))) {
		log_fatal(ESOFT, "File '%s' with %" PRIu64 " blocks is too large for a 32-bit build. Use a 64-bit build or increase the block size.\n", path, blockmax);
		exit(EXIT_FAILURE);
	}
#endif

	file = malloc_nofail(sizeof(struct snapraid_import_file));
	file->path = strdup_nofail(path);
	file->size = size;
	file->blockmax = blockmax;
	file->blockimp = nalloc_nofail((size_t)file->blockmax, sizeof(struct snapraid_import_block));
	file->is_runtime = 0;

	offset = 0;
	for (i = 0; i < file->blockmax; ++i) {
		struct snapraid_import_block* block = &file->blockimp[i];
		ssize_t read_size = block_size;
		if (read_size > size)
			read_size = size;

		block->file = file;
		block->offset = offset;
		block->size = read_size;

		memcpy(block->hash, dealloc->blockhash + i * BLOCK_HASH_SIZE, BLOCK_HASH_SIZE);

		/* do not insert invalid hashes */
		if (!hash_is_invalid(block->hash)) {
			tommy_hashdyn_insert(&state->importset, &block->nodeset, block, import_block_hash(block->hash));

			/* if we are in a rehash state */
			if (state->prevhash != HASH_UNDEFINED) {
				/*
				 * The deallocation record stores only one digest per block without
				 * recording which hash algorithm generated it. To avoid disk I/O at
				 * startup, index the stored digest under both algorithms.
				 *
				 * This is safe against false positives because state_import_fetch()
				 * validates candidate bytes with the requested algorithm upon read.
				 *
				 * Note that cross-generation matches (an old-hash deallocation supplying
				 * a new-hash block, or vice-versa) will miss in hash lookup and fall
				 * back to parity reconstruction. Only same-generation matches will succeed.
				 */
				memcpy(block->prevhash, block->hash, BLOCK_HASH_SIZE);
				tommy_hashdyn_insert(&state->previmportset, &block->prevnodeset, block, import_block_hash(block->prevhash));
			} else {
				hash_invalid_set(block->prevhash);
			}
		} else {
			hash_invalid_set(block->prevhash);
		}

		offset += read_size;
		size -= read_size;
	}

	tommy_list_insert_tail(&state->importlist, &file->nodelist, file);
}

void import_file_free(void* void_file)
{
	struct snapraid_import_file* file = void_file;

	free(file->path);
	free(file->blockimp);
	free(file);
}

static int state_import_fetch_candidate(struct snapraid_state* state, int rehash, struct snapraid_import_block* block, const unsigned char* hash, unsigned char* buffer)
{
	ssize_t ret;
	int f;
	unsigned block_size = state->block_size;
	size_t read_size;
	size_t count;
	unsigned char buffer_hash[HASH_MAX];
	const char* path;

	path = block->file->path;
	read_size = block->size;

	if (!block->file->is_runtime) {
		struct stat st;

		/*
		 * A deallocated candidate is historical and may be stale or shorter.
		 * Validate metadata to ensure the file exists, is regular, and contains data at the offset.
		 */
		if (stat(path, &st) != 0) {
			if (errno == ENOENT) {
				log_error(EUSER, "WARNING! Unexpected missing deallocated file '%s'.\n", path);
			} else {
				log_error(errno, "WARNING! Error stating deallocated file '%s'. %s.\n", path, strerror(errno));
			}
			return -1;
		}

		if (!S_ISREG(st.st_mode)) {
			log_error(EUSER, "WARNING! Unexpected non-regular deallocated file '%s'.\n", path);
			return -1;
		}

		if ((data_off_t)st.st_size < block->offset || (data_off_t)st.st_size - block->offset < (data_off_t)read_size) {
			log_error(EUSER, "WARNING! Unexpected short deallocated file '%s'.\n", path);
			return -1;
		}
	}

	f = open(path, O_RDONLY | O_BINARY);
	if (f == -1) {
		/*
		 * A deallocated source is historical and may be stale, so failure to open is a
		 * best-effort miss that lets the caller try another matching candidate.
		 * A runtime import was hashed in this run; its disappearance violates
		 * that invariant and must remain fatal.
		 */
		if (!block->file->is_runtime) {
			if (errno == ENOENT) {
				log_error(EUSER, "WARNING! Unexpected missing deallocated file '%s'.\n", path);
			} else {
				log_error(errno, "WARNING! Error opening deallocated file '%s'. %s.\n", path, strerror(errno));
			}
			return -1;
		}

		/* LCOV_EXCL_START */
		if (errno == ENOENT) {
			log_fatal(errno, "DANGER! file '%s' disappeared.\n", path);
			log_fatal(errno, "If you moved it, please rerun the same command.\n");
		} else {
			log_fatal(errno, "Error opening file '%s'. %s.\n", path, strerror(errno));
		}
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	count = 0;
	do {
		ret = pread(f, (char*)buffer + count, read_size - count, block->offset + count);
		if (ret < 0) {
			if (errno == EINTR)
				continue;

			if (!block->file->is_runtime) {
				close(f);
				log_error(errno, "WARNING! Error reading deallocated file '%s'. %s.\n", path, strerror(errno));
				return -1;
			}

			/* LCOV_EXCL_START */
			log_fatal(errno, "Error reading file '%s'. %s.\n", path, strerror(errno));
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
		if (ret == 0) {
			if (!block->file->is_runtime) {
				close(f);
				log_error(EUSER, "WARNING! Unexpected end of file in deallocated file '%s'.\n", path);
				return -1;
			}

			/* LCOV_EXCL_START */
			errno = ENXIO;
			log_fatal(errno, "Unexpected end of file '%s'. %s.\n", path, strerror(errno));
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		count += ret;
	} while (count < read_size);

	ret = close(f);
	if (ret != 0) {
		/* LCOV_EXCL_START */
		log_fatal(errno, "Error closing file '%s'. %s.\n", path, strerror(errno));
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

	if (memcmp(buffer_hash, hash, BLOCK_HASH_SIZE) != 0) {
		/*
		 * Deallocated contents may have changed since they were recorded, so a
		 * mismatch is best-effort and another candidate may still be valid.
		 * A runtime import was hashed in this run; a mismatch means concurrent
		 * modification and must remain fatal.
		 */
		if (!block->file->is_runtime) {
			log_error(EUSER, "WARNING! Unexpected hash mismatch from deallocated file '%s'.\n", path);
			return -1;
		}

		/* LCOV_EXCL_START */
		log_fatal(EUSER, "Mismatch in data reading file '%s'.\n", path);
		log_fatal(EUSER, "Please don't change imported files while running.\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	return 0;
}

int state_import_fetch(struct snapraid_state* state, int rehash, struct snapraid_block* missing_block, unsigned char* buffer)
{
	tommy_hashdyn* importset;
	tommy_hashdyn_node* node;
	tommy_uint32_t hash32;
	const unsigned char* hash = missing_block->hash;

	if (rehash)
		importset = &state->previmportset;
	else
		importset = &state->importset;

	hash32 = import_block_hash(hash);

	/*
	 * Multiple blocks may have the same digest. tommy_hashdyn_search() would
	 * stop at the first one, but a deallocated source may be stale, so walk
	 * the bucket until an exact candidate is reread and validated.
	 */
	node = tommy_hashdyn_bucket(importset, hash32);
	while (node) {
		struct snapraid_import_block* block = node->data;

		/* hash32 is only an index: reject other bucket keys, then compare the full digest */
		if (node->index == hash32) {
			int equal;

			if (rehash)
				equal = import_block_prevhash_compare(hash, block) == 0;
			else
				equal = import_block_hash_compare(hash, block) == 0;

			if (equal) {
				if (state_import_fetch_candidate(state, rehash, block, hash, buffer) == 0)
					return 0;
			}
		}

		node = node->next;
	}

	return -1;
}

static void import_dir(struct snapraid_state* state, const char* dir)
{
	DIR* d;

	d = opendir(dir);
	if (!d) {
		/* LCOV_EXCL_START */
		log_fatal(errno, "Error opening directory '%s'. %s.\n", dir, strerror(errno));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	while (1) {
		char path_next[PATH_MAX];
		struct stat st;
		const char* name;
		struct dirent* dd;

		/* clear errno to detect erroneous conditions */
		errno = 0;
		dd = readdir(d);
		if (dd == 0 && errno != 0) {
			/* LCOV_EXCL_START */
			log_fatal(errno, "Error reading directory '%s'. %s.\n", dir, strerror(errno));
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

#if HAVE_STRUCT_DIRENT_D_STAT
		/* convert dirent to lstat result */
		dirent_lstat(dd, &st);

		/*
		 * If the st_mode field is missing, takes care to fill it using normal lstat()
		 * at now this can happen only in Windows (with HAVE_STRUCT_DIRENT_D_STAT defined),
		 * because we use a directory reading method that doesn't read info about ReparsePoint.
		 * Note that here we cannot call here lstat_sync(), because we don't know what kind
		 * of file is it, and lstat_sync() doesn't always work
		 */
		if (st.st_mode == 0) {
			if (lstat(path_next, &st) != 0) {
				/* LCOV_EXCL_START */
				log_fatal(errno, "Error in stat file/directory '%s'. %s.\n", path_next, strerror(errno));
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
		}
#else
		/* get lstat info about the file */
		if (lstat(path_next, &st) != 0) {
			/* LCOV_EXCL_START */
			log_fatal(errno, "Error in stat file/directory '%s'. %s.\n", path_next, strerror(errno));
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
#endif

		if (S_ISREG(st.st_mode)) {
			import_file(state, path_next, st.st_size);
		} else if (S_ISDIR(st.st_mode)) {
			pathslash(path_next, sizeof(path_next));
			import_dir(state, path_next);
		}
	}

	if (closedir(d) != 0) {
		/* LCOV_EXCL_START */
		log_fatal(errno, "Error closing directory '%s'. %s.\n", dir, strerror(errno));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}
}

void state_import(struct snapraid_state* state, const char* dir)
{
	char path[PATH_MAX];

	msg_progress("Importing...\n");

	/* if the hash is not full */
	if (BLOCK_HASH_SIZE != HASH_MAX) {
		/* LCOV_EXCL_START */
		log_fatal(EUSER, "You cannot import files when using a reduced hash.\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* add the final slash */
	pathimport(path, sizeof(path), dir);
	pathslash(path, sizeof(path));

	import_dir(state, path);
}

void state_dealloc(struct snapraid_state* state, const char* dir, tommy_list* dealloclist)
{
	/* snapshot should be enabled */
	if (!state->snapshot)
		return;

	/* the hash must be full */
	if (BLOCK_HASH_SIZE != HASH_MAX)
		return;

	for (tommy_node* i = tommy_list_head(dealloclist); i != 0; i = i->next) {
		struct snapraid_dealloc* dealloc = i->data;

		import_dealloc(state, dir, dealloc);
	}
}

