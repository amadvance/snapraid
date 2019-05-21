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
#include "handle.h"

/**
 * Pseudo random limits for parity
 */
#define PARITY_LIMIT(size, split, level) \
	size ? size + (123562341 + split * 634542351 + level * 983491341) % size : 0

/****************************************************************************/
/* parity */

block_off_t parity_allocated_size(struct snapraid_state* state)
{
	block_off_t parity_block;
	tommy_node* i;

	/* compute the size of the parity file */
	parity_block = 0;
	for (i = state->disklist; i != 0; i = i->next) {
		struct snapraid_disk* disk = i->data;

		/* start from the declared size */
		block_off_t block = fs_size(disk);

		/* decrease the block until an allocated one, but part of a file */
		/* we don't stop at deleted blocks, because we want to have them cleared */
		/* if they are at the end of the parity */
		while (block > parity_block && !block_has_file(fs_par2block_find(disk, block - 1)))
			--block;

		/* get the highest value */
		if (block > parity_block)
			parity_block = block;
	}

	return parity_block;
}

block_off_t parity_used_size(struct snapraid_state* state)
{
	block_off_t parity_block;
	tommy_node* i;

	/* compute the size of the parity file */
	parity_block = 0;
	for (i = state->disklist; i != 0; i = i->next) {
		struct snapraid_disk* disk = i->data;

		/* start from the declared size */
		block_off_t block = fs_size(disk);

		/* decrease the block until an used one */
		while (block > parity_block && !block_has_file_and_valid_parity(fs_par2block_find(disk, block - 1)))
			--block;

		/* get the highest value */
		if (block > parity_block)
			parity_block = block;
	}

	return parity_block;
}

int parity_is_invalid(struct snapraid_state* state)
{
	block_off_t blockmax;
	block_off_t i;

	blockmax = parity_allocated_size(state);

	for (i = 0; i < blockmax; ++i) {
		tommy_node* node_disk;
		int one_invalid;
		int one_valid;

		/* for each disk */
		one_invalid = 0;
		one_valid = 0;
		for (node_disk = state->disklist; node_disk != 0; node_disk = node_disk->next) {
			struct snapraid_disk* disk = node_disk->data;
			struct snapraid_block* block = fs_par2block_find(disk, i);

			if (block_has_file(block))
				one_valid = 1;
			if (block_has_invalid_parity(block))
				one_invalid = 1;
		}

		/* if both valid and invalid, we need to update */
		if (one_invalid && one_valid)
			return 1;
	}

	return 0;
}

void parity_overflow(struct snapraid_state* state, data_off_t size)
{
	tommy_node* i;
	block_off_t blockalloc;
	int found = 0;
	char esc_buffer[ESC_MAX];

	/* don't report if everything is outside or if the file is not accessible */
	if (size == 0) {
		return;
	}

	blockalloc = size / state->block_size;

	/* for all disks */
	for (i = state->disklist; i != 0; i = i->next) {
		struct snapraid_disk* disk = i->data;
		tommy_node* j;

		/* for all files */
		for (j = disk->filelist; j != 0; j = j->next) {
			struct snapraid_file* file = j->data;

			if (file->blockmax > 0) {
				block_off_t parity_pos = fs_file2par_get(disk, file, file->blockmax - 1);
				if (parity_pos >= blockalloc) {
					found = 1;
					log_tag("outofparity:%s:%s\n", disk->name, esc_tag(file->sub, esc_buffer));
					log_fatal("outofparity %s%s\n", disk->dir, file->sub);
				}
			}
		}
	}

	if (found) {
		log_fatal("\nYour data requires more parity than the available space.\n");
		log_fatal("Please move the files 'outofparity' to another data disk.\n");
	}
}

void parity_size(struct snapraid_parity_handle* handle, data_off_t* out_size)
{
	unsigned s;
	data_off_t size;

	/* now compute the size summing all the parity splits */
	size = 0;

	for (s = 0; s < handle->split_mac; ++s) {
		struct snapraid_split_handle* split = &handle->split_map[s];

		size += split->size;
	}

	*out_size = size;
}

int parity_create(struct snapraid_parity_handle* handle, const struct snapraid_parity* parity, unsigned level, int mode, uint32_t block_size, data_off_t limit_size)
{
	unsigned s;
	data_off_t block_mask;

	/* mask of bits used by the block size */
	block_mask = ((data_off_t)block_size) - 1;

	handle->level = level;
	handle->split_mac = 0;

	for (s = 0; s < parity->split_mac; ++s) {
		struct snapraid_split_handle* split = &handle->split_map[s];
		int ret;
		int flags;

		advise_init(&split->advise, mode);
		pathcpy(split->path, sizeof(split->path), parity->split_map[s].path);
		split->size = parity->split_map[s].size;
		split->limit_size = PARITY_LIMIT(limit_size, s, level);

		/* opening in sequential mode in Windows */
		flags = O_RDWR | O_CREAT | O_BINARY | advise_flags(&split->advise);
		split->f = open(split->path, flags, 0600);
		if (split->f == -1) {
			/* LCOV_EXCL_START */
			log_fatal("Error opening parity file '%s'. %s.\n", split->path, strerror(errno));
			goto bail;
			/* LCOV_EXCL_STOP */
		}

		/* we have a valid file handle */
		++handle->split_mac;

		/* get the stat info */
		ret = fstat(split->f, &split->st);
		if (ret != 0) {
			/* LCOV_EXCL_START */
			log_fatal("Error accessing parity file '%s'. %s.\n", split->path, strerror(errno));
			goto bail;
			/* LCOV_EXCL_STOP */
		}

		/* the initial valid size is the size on disk */
		split->valid_size = split->st.st_size;

		/**
		 * If the parity size is not yet set, set it now.
		 * This happens when expanding the number of parities,
		 * or when upgrading from a content file that has not split->size data.
		 */
		if (split->size == PARITY_SIZE_INVALID) {
			split->size = split->st.st_size;

			/* ensure that the resulting size if block aligned */
			if ((split->size & block_mask) != 0) {
				/* LCOV_EXCL_START */
				log_fatal("Error in preallocated size of parity file '%s' with size %" PRIu64 " and block %u .\n", split->path, split->size, block_size);
				goto bail;
				/* LCOV_EXCL_STOP */
			}
		}

		ret = advise_open(&split->advise, split->f);
		if (ret != 0) {
			/* LCOV_EXCL_START */
			log_fatal("Error advising parity file '%s'. %s.\n", split->path, strerror(errno));
			goto bail;
			/* LCOV_EXCL_STOP */
		}
	}

	return 0;

bail:
	/* LCOV_EXCL_START */
	for (s = 0; s < handle->split_mac; ++s) {
		struct snapraid_split_handle* split = &handle->split_map[s];
		close(split->f);
		split->f = -1;
	}
	return -1;
	/* LCOV_EXCL_STOP */
}

static int parity_handle_grow(struct snapraid_split_handle* split, data_off_t previous_size, data_off_t size, int skip_fallocate)
{
	int ret;

	(void)previous_size;

	/* simulate a failure for testing limits */
	if (split->limit_size != 0 && size > (data_off_t)split->limit_size)
		return -1;

#if HAVE_FALLOCATE
	if (!skip_fallocate) {
		/*
		 * Allocate real space using the specific Linux fallocate() operation.
		 * If the underline file-system doesn't support it, this operation fails.
		 *
		 * Instead posix_fallocate() fallbacks to write the whole file,
		 * and we cannot use it as we may need to initialize a multi terabyte
		 * file.
		 *
		 * See: fallocate vs posix_fallocate
		 * http://stackoverflow.com/questions/14063046/fallocate-vs-posix-fallocate
		 *
		 * To work better with Btrfs, use as offset the previous allocated size.
		 * Otherwise Btrfs will count as space needed even the already allocated one.
		 *
		 * See: Massive loss of disk space
		 * https://www.mail-archive.com/linux-btrfs@vger.kernel.org/msg66454.html
		 */
		ret = fallocate(split->f, 0, previous_size, size - previous_size);

		/*
		 * In some legacy system fallocate() may return the error number
		 * as  positive integer, and in this case it doesn't set errno.
		 *
		 * Detect and handle this case.
		 *
		 * See: Fix fallocate error return on i386
		 * https://sourceware.org/ml/libc-hacker/2010-04/msg00000.html
		 *
		 * See: [PATCH XFS] Fix error return for fallocate() on XFS
		 * http://oss.sgi.com/archives/xfs/2009-11/msg00201.html
		 */
		if (ret > 0) {
			/* LCOV_EXCL_START */
			errno = ret;
			ret = -1;
			/* LCOV_EXCL_STOP */
		}
	} else {
		errno = EOPNOTSUPP;
		ret = -1;
	}

	/*
	 * Fallback to ftruncate() if the operation is not supported.
	 *
	 * We get EOPNOTSUPP if the operation is not supported, like in ext3/ext2
	 * or ENOSYS with kernel before 2.6.23, because fallocate is not supported
	 * at all.
	 *
	 * See: man fallocate
	 * ENOSYS - This kernel does not implement fallocate().
	 * EOPNOTSUPP - The file system containing the file referred to by fd does not support this operation
	 */
	if (ret != 0 && (errno == EOPNOTSUPP || errno == ENOSYS)) {
		/* fallback using ftruncate() */
		ret = ftruncate(split->f, size);
	}
#else
	(void)skip_fallocate; /* avoid the warning */

	/* allocate using a sparse file */
	ret = ftruncate(split->f, size);
#endif

	if (ret != 0)
		log_tag("split:grow:%s:%" PRIu64 ": failed with error %s\n", split->path, size, strerror(errno));
	else
		log_tag("split:grow:%s:%" PRIu64 ": ok\n", split->path, size);

	return ret;
}

static int parity_handle_shrink(struct snapraid_split_handle* split, data_off_t size)
{
	int ret;

	ret = ftruncate(split->f, size);

	if (ret != 0)
		log_tag("split:shrink:%s:%" PRIu64 ": failed with error %s\n", split->path, size, strerror(errno));
	else
		log_tag("split:shrink:%s:%" PRIu64 ": ok\n", split->path, size);

	return ret;
}

/**
 * Get the highest bit set.
 */
uint64_t hbit_u64(uint64_t v)
{
	unsigned ilog;

	ilog = 0;
	while ((v /= 2) != 0)
		++ilog;

	return 1ULL << ilog;
}

static int parity_handle_fill(struct snapraid_split_handle* split, data_off_t size, uint32_t block_size, int skip_fallocate, int skip_space_holder)
{
	data_off_t base;
	data_off_t delta;
	data_off_t block_mask;

#ifdef _WIN32
	/*
	 * In Windows we want to avoid the annoying warning
	 * message of disk full.
	 *
	 * To ensure to leave some space available, we first create
	 * a spaceholder file >200 MB, to ensure to not fill completely
	 * the disk.
	 */
	char spaceholder_path[PATH_MAX];

	pathprint(spaceholder_path, sizeof(spaceholder_path), "%s%s", split->path, ".spaceholder");

	if (!skip_space_holder) {
		data_off_t spaceholder_size = 256 * 1024 * 1024;
		int spaceholder_f;

		spaceholder_f = open(spaceholder_path, O_RDWR | O_CREAT | O_TRUNC | O_BINARY, 0600);
		if (spaceholder_f == -1) {
			log_fatal("Failed to create space holder file '%s'.\n", spaceholder_path);
			return -1;
		}

		/* note that in Windows ftruncate is really allocating space */
		if (ftruncate(spaceholder_f, spaceholder_size) != 0) {
			log_fatal("WARNING Failed to resize the space holder file '%s' to %" PRIu64 " bytes.\n", spaceholder_path, spaceholder_size);
			log_fatal("Assuming that no more space is available.\n");
			close(spaceholder_f);
			remove(spaceholder_path);
			return 0;
		}

		if (fsync(spaceholder_f) != 0) {
			log_fatal("Failed to sync the space holder file '%s'.\n", spaceholder_path);
			close(spaceholder_f);
			remove(spaceholder_path);
			return -1;
		}

		if (close(spaceholder_f) != 0) {
			log_fatal("Failed to close the space holder file '%s'.\n", spaceholder_path);
			remove(spaceholder_path);
			return -1;
		}
	}
#else
	(void)skip_space_holder;
#endif

	/* mask of bits used by the block size */
	block_mask = ((data_off_t)block_size) - 1;

	/* present size */
	base = split->st.st_size;

	/* truncate it to block size multiplier */
	/* in case of damage the size may get wrong */
	base &= ~block_mask;

	/* size we have to increase */
	delta = size - base;

	log_tag("split:fill:%s:%" PRIu64 ":%" PRIu64 ":\n", split->path, base, size);

	/* grow the size one bit at time, like a kind of binary search */
	while (delta != 0) {
		int ret;
		data_off_t run = hbit_u64(delta);

		/* mask out the bit we process */
		delta &= ~run;

		log_tag("split:delta:%s:%" PRIu64 ":%" PRIu64 ":\n", split->path, base, run);

		ret = parity_handle_grow(split, base, base + run, skip_fallocate);
		if (ret != 0) {
			/* we cannot grow, fallback enabling all the smaller bits */
			delta = run - 1;

			/* mask out the block size */
			delta &= ~block_mask;
		} else {
			/* increase the effective size */
			base += run;
		}
	}

	/* ensure that the resulting size if block aligned */
	if ((base & block_mask) != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Internal inconsistency in requested parity size %" PRIu64 " with block %u\n", base, block_size);
		os_abort();
		/* LCOV_EXCL_STOP */
	}

#ifdef _WIN32
	/* now delete the spaceholder file */
	if (remove(spaceholder_path) != 0) {
		log_fatal("WARNING Failed to remove the space holder file '%s'.\n", spaceholder_path);
		log_fatal("Continuing anyway.\n");
	}
#endif

	/* shrink to the expected size to ensure to throw away any extra */
	/* data allocated when the grow operation fails */
	return parity_handle_shrink(split, base);
}

static int parity_handle_chsize(struct snapraid_split_handle* split, data_off_t size, uint32_t block_size, int skip_fallocate, int skip_space_holder)
{
	int ret;
	int f_ret;
	int f_errno;
	int f_dir;

	if (split->st.st_size < size) {
		f_ret = parity_handle_fill(split, size, block_size, skip_fallocate, skip_space_holder);
		f_errno = errno;
		f_dir = 1;
	} else if (split->st.st_size > size) {
		f_ret = parity_handle_shrink(split, size);
		f_errno = errno;
		f_dir = -1;
	} else {
		f_ret = 0;
		f_errno = 0;
		f_dir = 0;
	}

	/* get the stat info */
	ret = fstat(split->f, &split->st);
	if (ret != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Error accessing parity file '%s'. %s.\n", split->path, strerror(errno));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/* now check the error */
	if (f_ret != 0) {
		/* LCOV_EXCL_START */
		if (f_dir > 0) {
			if (f_errno == ENOSPC) {
				log_fatal("Failed to grow parity file '%s' to size %" PRIu64 " due lack of space.\n", split->path, size);
			} else {
				log_fatal("Error growing parity file '%s' to size %" PRIu64 ". Do you have enough space? %s.\n", split->path, size, strerror(f_errno));
			}
		} else {
			log_fatal("Error truncating parity file '%s' to size %" PRIu64 ". %s.\n", split->path, size, strerror(f_errno));
		}
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/* if we shrink, update the valid size, but don't update when growing */
	if (split->valid_size > split->st.st_size)
		split->valid_size = split->st.st_size;

	return 0;
}

static int parity_split_is_fixed(struct snapraid_parity_handle* handle, unsigned s)
{
	/* next one */
	++s;

	/* the latest one is always growing */
	if (s >= handle->split_mac)
		return 0;

	/* if the next it's 0, this one is growing */
	if (handle->split_map[s].size == 0)
		return 0;

	return 1;
}

int parity_chsize(struct snapraid_parity_handle* handle, struct snapraid_parity* parity, int* is_modified, data_off_t size, uint32_t block_size, int skip_fallocate, int skip_space_holder)
{
	int ret;
	unsigned s;
	data_off_t block_mask;

	/* mask of bits used by the block size */
	block_mask = ((data_off_t)block_size) - 1;

	if (size < 0) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	for (s = 0; s < handle->split_mac; ++s) {
		struct snapraid_split_handle* split = &handle->split_map[s];
		int is_fixed = parity_split_is_fixed(handle, s);
		data_off_t run;

		if (is_fixed) {
			/* if the required size is smaller, we have to reduce also the file */
			/* ignoring the previous size */
			if (size <= split->size) {
				/* mark it as not fixed anymore for the later check */
				is_fixed = 0;

				run = size; /* allocate only the needed size */
			} else {
				/* if the size cannot be changed, use the fixed one */
				run = split->size;

				if ((run & block_mask) != 0) {
					/* LCOV_EXCL_START */
					log_fatal("Internal inconsistency in split '%s' size with extra '%" PRIu64 "' bytes.\n", split->path, run & block_mask);
					return -1;
					/* LCOV_EXCL_STOP */
				}
			}
		} else {
			/* otherwise tries to allocate all the needed remaining size */
			run = size;
		}

		ret = parity_handle_chsize(split, run, block_size, skip_fallocate, skip_space_holder);
		if (ret != 0) {
			/* LCOV_EXCL_START */
			return -1;
			/* LCOV_EXCL_STOP */
		}

		if (split->st.st_size > run) {
			/* LCOV_EXCL_START */
			log_fatal("Unexpected over resizing parity file '%s' to size %" PRIu64 " resulting in size %" PRIu64 ".\n", split->path, run, (uint64_t)split->st.st_size);
			return -1;
			/* LCOV_EXCL_STOP */
		} else if (is_fixed && split->st.st_size < run) {
			/* LCOV_EXCL_START */
			log_fatal("Failed restoring parity file '%s' to size %" PRIu64 " resulting in size %" PRIu64 ".\n", split->path, run, (uint64_t)split->st.st_size);
			return -1;
			/* LCOV_EXCL_STOP */
		} else {
			/* here it's possible to get less than the requested size */
			run = split->st.st_size;

			if ((run & block_mask) != 0) {
				/* LCOV_EXCL_START */
				log_fatal("Internal inconsistency in final parity size %" PRIu64 " with block size %u\n", run, block_size);
				os_abort();
				/* LCOV_EXCL_STOP */
			}

			/* store what we have allocated */
			split->size = run;

			/* decrease the remaining size */
			size -= run;
		}
	}

	/* if we cannot allocate all the space */
	if (size != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Failed to allocate all the required parity space. You miss %" PRIu64 " bytes.\n", size);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/* now copy the new size in the parity data */
	if (is_modified)
		*is_modified = 0;

	for (s = 0; s < handle->split_mac; ++s) {
		struct snapraid_split_handle* split = &handle->split_map[s];

		if (parity->split_map[s].size != split->size) {
			parity->split_map[s].size = split->size;
			if (is_modified)
				*is_modified = 1;
		}
	}

	return 0;
}

int parity_open(struct snapraid_parity_handle* handle, const struct snapraid_parity* parity, unsigned level, int mode, uint32_t block_size, data_off_t limit_size)
{
	unsigned s;
	data_off_t block_mask;

	handle->level = level;
	handle->split_mac = 0;

	/* mask of bits used by the block size */
	block_mask = ((data_off_t)block_size) - 1;

	for (s = 0; s < parity->split_mac; ++s) {
		struct snapraid_split_handle* split = &handle->split_map[s];
		int ret;
		int flags;

		advise_init(&split->advise, mode);
		pathcpy(split->path, sizeof(split->path), parity->split_map[s].path);
		split->size = parity->split_map[s].size;
		split->limit_size = PARITY_LIMIT(limit_size, s, level);

		/* open for read */
		/* O_NOATIME: do not change access time */
		flags = O_RDONLY | O_BINARY | advise_flags(&split->advise);

		split->f = open_noatime(split->path, flags);
		if (split->f == -1) {
			/* LCOV_EXCL_START */
			log_fatal("Error opening parity file '%s'. %s.\n", split->path, strerror(errno));
			goto bail;
			/* LCOV_EXCL_STOP */
		}

		/* we have a valid file handle */
		++handle->split_mac;

		/* get the stat info */
		ret = fstat(split->f, &split->st);
		if (ret != 0) {
			/* LCOV_EXCL_START */
			log_fatal("Error accessing parity file '%s'. %s.\n", split->path, strerror(errno));
			goto bail;
			/* LCOV_EXCL_STOP */
		}

		/* the initial valid size is the size on disk */
		split->valid_size = split->st.st_size;

		/**
		 * If the parity size is not yet set, set it now.
		 * This happens when expanding the number of parities,
		 * or when upgrading from a content file that has not split->size data.
		 */
		if (split->size == PARITY_SIZE_INVALID) {
			split->size = split->st.st_size;

			/* ensure that the resulting size if block aligned */
			if ((split->size & block_mask) != 0) {
				/* LCOV_EXCL_START */
				log_fatal("Error in preallocated size of parity file '%s' with size %" PRIu64 " and block %u .\n", split->path, split->size, block_size);
				goto bail;
				/* LCOV_EXCL_STOP */
			}
		}

		ret = advise_open(&split->advise, split->f);
		if (ret != 0) {
			/* LCOV_EXCL_START */
			log_fatal("Error advising parity file '%s'. %s.\n", split->path, strerror(errno));
			goto bail;
			/* LCOV_EXCL_STOP */
		}
	}

	return 0;

bail:
	/* LCOV_EXCL_START */
	for (s = 0; s < handle->split_mac; ++s) {
		struct snapraid_split_handle* split = &handle->split_map[s];
		close(split->f);
		split->f = -1;
	}
	return -1;
	/* LCOV_EXCL_STOP */
}

int parity_sync(struct snapraid_parity_handle* handle)
{
#if HAVE_FSYNC
	unsigned s;

	for (s = 0; s < handle->split_mac; ++s) {
		struct snapraid_split_handle* split = &handle->split_map[s];
		int ret;

		/* Ensure that data changes are written to disk. */
		/* This is required to ensure that parity is more updated than content */
		/* in case of a system crash. */
		ret = fsync(split->f);
		if (ret != 0) {
			/* LCOV_EXCL_START */
			log_fatal("Error synching parity file '%s'. %s.\n", split->path, strerror(errno));
			return -1;
			/* LCOV_EXCL_STOP */
		}
	}
#endif

	return 0;
}

int parity_truncate(struct snapraid_parity_handle* handle)
{
	unsigned s;
	int f_ret = 0;

	for (s = 0; s < handle->split_mac; ++s) {
		struct snapraid_split_handle* split = &handle->split_map[s];
		int ret;

		/* truncate any data that we know it's not valid */
		ret = ftruncate(split->f, split->valid_size);
		if (ret != 0) {
			/* LCOV_EXCL_START */
			log_fatal("Error truncating the parity file '%s' to size %" PRIu64 ". %s.\n", split->path, split->valid_size, strerror(errno));
			f_ret = -1;
			/* LCOV_EXCL_STOP */

			/* continue to truncate the others */
		}
	}

	return f_ret;
}

int parity_close(struct snapraid_parity_handle* handle)
{
	unsigned s;
	int f_ret = 0;

	for (s = 0; s < handle->split_mac; ++s) {
		struct snapraid_split_handle* split = &handle->split_map[s];
		int ret;

		ret = close(split->f);
		if (ret != 0) {
			/* LCOV_EXCL_START */
			/* This is a serious error, as it may be the result of a failed write */
			/* identified at later time. */
			/* In a normal file-system (not NFS) it should never happen */
			log_fatal("Error closing parity file '%s'. %s.\n", split->path, strerror(errno));
			f_ret = -1;
			/* LCOV_EXCL_STOP */

			/* continue to close the others */
		}

		/* reset the descriptor */
		split->f = -1;
	}

	return f_ret;
}

struct snapraid_split_handle* parity_split_find(struct snapraid_parity_handle* handle, data_off_t* offset)
{
	unsigned s;

	if (*offset < 0)
		return 0;

	for (s = 0; s < handle->split_mac; ++s) {
		struct snapraid_split_handle* split = &handle->split_map[s];

		if (*offset < split->size)
			return split;

		*offset -= split->size;
	}

	return 0;
}

int parity_write(struct snapraid_parity_handle* handle, block_off_t pos, unsigned char* block_buffer, unsigned block_size)
{
	ssize_t write_ret;
	data_off_t offset;
	struct snapraid_split_handle* split;
	int ret;

	offset = pos * (data_off_t)block_size;

	split = parity_split_find(handle, &offset);
	if (!split) {
		/* LCOV_EXCL_START */
		log_fatal("Writing parity data outside range at extra offset %" PRIu64 ".\n", offset);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/* update the valid range */
	if (split->valid_size < offset + block_size)
		split->valid_size = offset + block_size;

	write_ret = pwrite(split->f, block_buffer, block_size, offset);
	if (write_ret != (ssize_t)block_size) { /* conversion is safe because block_size is always small */
		/* LCOV_EXCL_START */
		if (errno == ENOSPC) {
			log_fatal("Failed to grow parity file '%s' using write due lack of space.\n", split->path);
		} else {
			log_fatal("Error writing file '%s'. %s.\n", split->path, strerror(errno));
		}
		return -1;
		/* LCOV_EXCL_STOP */
	}

	ret = advise_write(&split->advise, split->f, offset, block_size);
	if (ret != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Error advising parity file '%s'. %s.\n", split->path, strerror(errno));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	return 0;
}

int parity_read(struct snapraid_parity_handle* handle, block_off_t pos, unsigned char* block_buffer, unsigned block_size, fptr* out)
{
	ssize_t read_ret;
	data_off_t offset;
	unsigned count;
	struct snapraid_split_handle* split;
	int ret;

	offset = pos * (data_off_t)block_size;

	split = parity_split_find(handle, &offset);
	if (!split) {
		/* LCOV_EXCL_START */
		out("Reading parity data outside range at extra offset %" PRIu64 ".\n", offset);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/* if read is completely out of the valid range */
	if (offset >= split->valid_size) {
		/* LCOV_EXCL_START */
		out("Missing data reading file '%s' at offset %" PRIu64 " for size %u.\n", split->path, offset, block_size);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	count = 0;
	do {
		read_ret = pread(split->f, block_buffer + count, block_size - count, offset + count);
		if (read_ret < 0) {
			/* LCOV_EXCL_START */
			out("Error reading file '%s' at offset %" PRIu64 " for size %u. %s.\n", split->path, offset + count, block_size - count, strerror(errno));
			return -1;
			/* LCOV_EXCL_STOP */
		}
		if (read_ret == 0) {
			/* LCOV_EXCL_START */
			out("Unexpected end of file '%s' at offset %" PRIu64 ". %s.\n", split->path, offset, strerror(errno));
			return -1;
			/* LCOV_EXCL_STOP */
		}

		count += read_ret;
	} while (count < block_size);

	ret = advise_read(&split->advise, split->f, offset, block_size);
	if (ret != 0) {
		/* LCOV_EXCL_START */
		out("Error advising parity file '%s'. %s.\n", split->path, strerror(errno));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	return block_size;
}

