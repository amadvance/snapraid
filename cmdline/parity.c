// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2011 Andrea Mazzoleni

#include "os/portable.h"

#include "support.h"
#include "elem.h"
#include "state.h"
#include "parity.h"
#include "handle.h"

/**
 * Pseudo random limits for parity
 */
#define PARITY_LIMIT(size, split, level) \
	(size ? (size) + (123562341U + (split) * 634542351U + (level) * 983491341U) % (size) : 0)

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

		/*
		 * Decrease the block until an allocated one, but part of a file
		 * we don't stop at deleted blocks, because we want to have them cleared
		 * if they are at the end of the parity
		 */
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

void parity_overflow(struct snapraid_state* state, data_off_t size)
{
	tommy_node* i;
	block_off_t blockalloc;
	int found = 0;
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
					log_tag("outofparity:%s:%s\n", disk->name, esc_tag(file->sub));
					log_fatal(ESOFT, "outofparity %s%s\n", disk->dir, file->sub);
				}
			}
		}
	}

	if (found) {
		log_fatal(ESOFT, "\nInsufficient parity space. Data requires more parity than available.\n");
		log_fatal(ESOFT, "Move the 'outofparity' files to a larger disk.\n");
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

void parity_physical_reach_size(struct snapraid_parity_handle* handle, data_off_t* out_size)
{
	unsigned s;
	data_off_t size;

	/*
	 * Compute the physical reach of the logical parity layout across splits.
	 *
	 * physical_reach_size represents physical file presence, not parity validity.
	 * Parity is not necessarily contiguous or valid within this range: blocks
	 * may be invalid, stale, unwritten, or modified. Correctness is determined
	 * per-block by the content state and validated independently via data hashes
	 * and recomputation.
	 *
	 * For split parity, splits concatenate linearly. If an earlier split is
	 * physically shorter than its logical size, logical offsets beyond that
	 * missing region cannot be addressed, so subsequent splits are not counted.
	 */
	size = 0;

	for (s = 0; s < handle->split_mac; ++s) {
		struct snapraid_split_handle* split = &handle->split_map[s];
		data_off_t run = split->physical_reach_size;

		/* don't count physical data outside the logical split */
		if (run > split->size)
			run = split->size;

		size += run;

		/* later splits cannot extend the logical extent past this boundary */
		if (run < split->size)
			break;
	}

	*out_size = size;
}

int parity_create(struct snapraid_parity_handle* handle, const struct snapraid_parity* parity, unsigned level, int mode, uint32_t block_size, data_off_t limit_size)
{
	unsigned s;
	int zero_seen = 0;
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

		/*
		 * Open for writing.
		 * O_NOFOLLOW: do not follow links to ensure to open the real parity file.
		 */
		flags = O_RDWR | O_CREAT | O_BINARY | O_NOFOLLOW | advise_flags(&split->advise);
		split->f = open(split->path, flags, 0600);
		if (split->f == -1) {
			/* LCOV_EXCL_START */
			log_fatal(errno, "Error opening parity file '%s'. %s.\n", split->path, strerror(errno));
			goto bail;
			/* LCOV_EXCL_STOP */
		}

		/* we have a valid file handle */
		++handle->split_mac;

		/* get the stat info */
		ret = fstat(split->f, &split->st);
		if (ret != 0) {
			/* LCOV_EXCL_START */
			log_fatal(errno, "Error accessing parity file '%s'. %s.\n", split->path, strerror(errno));
			goto bail;
			/* LCOV_EXCL_STOP */
		}

		/*
		 * Initialize the physical read/truncation boundary from the current EOF.
		 *
		 * physical_reach_size does NOT imply that parity below this offset is valid.
		 * Parity may be invalid, stale, unwritten, or sparse in any region below
		 * this boundary. It only records the physical EOF boundary for I/O and
		 * truncation. Parity validity is tracked independently per block through
		 * the content state, data hashes, and recomputation.
		 *
		 * While a handle is open, physical_reach_size can intentionally differ from EOF.
		 * In particular, preallocation may extend EOF without extending physical_reach_size.
		 * Writable fix paths normally truncate the file back to physical_reach_size before
		 * closing.
		 *
		 * Across close/reopen there is no separate persistent physical_reach value, so EOF
		 * is used to reconstruct this physical boundary. This reconstruction must not
		 * be interpreted as validation of the parity contained below EOF.
		 */
		split->physical_reach_size = split->st.st_size;

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
				errno = ESOFT;
				log_fatal(ESOFT, "Error in preallocated size of parity file '%s' with size %" PRIu64 " and block %u .\n", split->path, split->size, block_size);
				goto bail;
				/* LCOV_EXCL_STOP */
			}
		}

		/*
		 * Non-empty parity splits must form a contiguous prefix.
		 * Once an empty split is found, all following splits must
		 * also be empty. Otherwise logical parity would contain an
		 * internal hole in the split layout.
		 */
		if (split->size == 0) {
			zero_seen = 1;
		} else if (zero_seen) {
			/* LCOV_EXCL_START */
			errno = ESOFT;
			log_fatal(ESOFT, "Invalid parity split layout: parity file '%s' has size %" PRIu64 " after an empty split.\n", split->path, split->size);
			goto bail;
			/* LCOV_EXCL_STOP */
		}

		ret = advise_open(&split->advise, split->f);
		if (ret != 0) {
			/* LCOV_EXCL_START */
			log_fatal(errno, "Error advising parity file '%s'. %s.\n", split->path, strerror(errno));
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
	if (split->limit_size != 0 && size > (data_off_t)split->limit_size) {
		errno = ENXIO;
		return -1;
	}

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
		log_tag("split:grow:%s:%" PRIu64 ": failed with error %s\n", esc_tag(split->path), size, strerror(errno));
	else
		log_tag("split:grow:%s:%" PRIu64 ": ok\n", esc_tag(split->path), size);

	return ret;
}

static int parity_handle_shrink(struct snapraid_split_handle* split, data_off_t size)
{
	int ret;

	ret = ftruncate(split->f, size);

	if (ret != 0)
		log_tag("split:shrink:%s:%" PRIu64 ": failed with error %s\n", esc_tag(split->path), size, strerror(errno));
	else
		log_tag("split:shrink:%s:%" PRIu64 ": ok\n", esc_tag(split->path), size);

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
		int spaceholder_f;

		spaceholder_f = open(spaceholder_path, O_RDWR | O_CREAT | O_TRUNC | O_BINARY, 0600);
		if (spaceholder_f == -1) {
			log_fatal(errno, "Failed to create space holder file '%s'.\n", spaceholder_path);
			return -1;
		}

		/* note that in Windows ftruncate is really allocating space */
		if (ftruncate(spaceholder_f, WINDOWS_SPACEHOLDER_SIZE) != 0) {
			log_fatal(errno, "WARNING Failed to resize the space holder file '%s' to %u bytes.\n", spaceholder_path, WINDOWS_SPACEHOLDER_SIZE);
			log_fatal(errno, "Assuming that no more space is available.\n");
			close(spaceholder_f);
			remove(spaceholder_path);
			return 0;
		}

		if (fsync(spaceholder_f) != 0) {
			log_fatal(errno, "Failed to sync the space holder file '%s'.\n", spaceholder_path);
			close(spaceholder_f);
			remove(spaceholder_path);
			return -1;
		}

		if (close(spaceholder_f) != 0) {
			log_fatal(errno, "Failed to close the space holder file '%s'.\n", spaceholder_path);
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

	/*
	 * Truncate it to block size multiplier
	 * in case of damage the size may get wrong
	 */
	base &= ~block_mask;

	/* size we have to increase */
	delta = size - base;

	log_tag("split:fill:%s:%" PRIu64 ":%" PRIu64 ":\n", esc_tag(split->path), base, size);

	/* grow the size one bit at time, like a kind of binary search */
	while (delta != 0) {
		int ret;
		data_off_t run = hbit_u64(delta);

		/* mask out the bit we process */
		delta &= ~run;

		log_tag("split:delta:%s:%" PRIu64 ":%" PRIu64 ":\n", esc_tag(split->path), base, run);

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
		log_fatal(EINTERNAL, "Internal inconsistency in requested parity size %" PRIu64 " with block %u\n", base, block_size);
		os_abort();
		/* LCOV_EXCL_STOP */
	}

#ifdef _WIN32
	/* now delete the spaceholder file */
	if (remove(spaceholder_path) != 0) {
		log_error(errno, "WARNING Failed to remove the space holder file '%s'.\n", spaceholder_path);
		log_error(errno, "Continuing anyway.\n");
	}
#endif

	/*
	 * Shrink to the expected size to ensure to throw away any extra
	 * data allocated when the grow operation fails
	 */
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
		log_fatal(errno, "Error accessing parity file '%s'. %s.\n", split->path, strerror(errno));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/* now check the error */
	if (f_ret != 0) {
		/* LCOV_EXCL_START */
		if (f_dir > 0) {
			if (f_errno == ENOSPC) {
				log_fatal(errno, "Failed to grow parity file '%s' to size %" PRIu64 " due lack of space.\n", split->path, size);
			} else {
				log_fatal(errno, "Error growing parity file '%s' to size %" PRIu64 ". Do you have enough space? %s.\n", split->path, size, strerror(f_errno));
			}
		} else {
			log_fatal(errno, "Error truncating parity file '%s' to size %" PRIu64 ". %s.\n", split->path, size, strerror(f_errno));
		}
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/*
	 * Keep the physical read/truncation boundary consistent with resizing.
	 *
	 * Shrinking reduces physical_reach_size because bytes beyond the new EOF no longer
	 * exist physically and therefore cannot be read.
	 *
	 * Growing leaves physical_reach_size unchanged. Allocation or preallocation alone
	 * does not make the newly created physical extent readable as parity; the
	 * boundary is advanced only after a parity write has completed.
	 *
	 * This must not be confused with parity validity. Advancing physical_reach_size
	 * after a completed write only records the physical offset reached by writes.
	 * It does not certify preceding bytes as correct parity; parity may remain
	 * invalid or unwritten in any region below this boundary.
	 */
	if (split->physical_reach_size > split->st.st_size)
		split->physical_reach_size = split->st.st_size;

	return 0;
}

static int parity_split_is_fixed(struct snapraid_parity_handle* handle, unsigned s)
{
	/*
	 * Parity splits concatenate into a single logical file.
	 *
	 * Once a following split contains data, this split's boundary becomes
	 * fixed: changing it would shift logical offsets in all subsequent
	 * splits, invalidating their parity positions.
	 *
	 * Only the last active split may grow. If total parity shrinks into a
	 * previously fixed split, that split becomes the new tail and may shrink.
	 *
	 * An elastic split may also grow partially; remaining space is allocated
	 * in the next split.
	 */
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

int parity_chsize(struct snapraid_parity_handle* handle, struct snapraid_parity* parity, int* is_modified, data_off_t size, uint32_t block_size, int skip_fallocate, int skip_space_holder, int allow_split_realloc)
{
	int ret;
	unsigned s;
	data_off_t block_mask;

	/* mask of bits used by the block size */
	block_mask = ((data_off_t)block_size) - 1;

	if (size < 0) {
		/* LCOV_EXCL_START */
		errno = ENXIO;
		return -1;
		/* LCOV_EXCL_STOP */
	}

	for (s = 0; s < handle->split_mac; ++s) {
		struct snapraid_split_handle* split = &handle->split_map[s];
		int is_fixed = parity_split_is_fixed(handle, s);
		data_off_t run;

		if (is_fixed) {
			/*
			 * If the required size is smaller, we have to reduce also the file
			 * ignoring the previous size
			 */
			if (size <= split->size) {
				/* mark it as not fixed anymore for the later check */
				is_fixed = 0;

				run = size; /* allocate only the needed size */
			} else {
				/* if the size cannot be changed, use the fixed one */
				run = split->size;

				if ((run & block_mask) != 0) {
					/* LCOV_EXCL_START */
					errno = ENXIO;
					log_fatal(EINTERNAL, "Internal inconsistency in split '%s' size with extra '%" PRIu64 "' bytes.\n", split->path, run & block_mask);
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
			errno = ENXIO;
			log_fatal(errno, "Unexpected over resizing parity file '%s' to size %" PRIu64 " resulting in size %" PRIu64 ".\n", split->path, run, (uint64_t)split->st.st_size);
			return -1;
			/* LCOV_EXCL_STOP */
		} else if ((is_fixed || !allow_split_realloc) && split->st.st_size < run) {
			/*
			 * A fixed split must always reach its logical boundary because changing
			 * it would shift the logical offsets of data already present in following
			 * splits.
			 *
			 * Some callers also require the existing split layout to remain unchanged
			 * even when this split would normally be elastic. In particular, fix does
			 * not persist a changed split mapping. Accepting a partial allocation here
			 * would make the remaining size move into a following split and fix could
			 * then write parity using a transient layout that is lost on the next run.
			 */
			/* LCOV_EXCL_START */
			errno = ENXIO;
			log_fatal(errno, "Failed restoring parity file '%s' to size %" PRIu64 " resulting in size %" PRIu64 ".\n", split->path, run, (uint64_t)split->st.st_size);
			return -1;
			/* LCOV_EXCL_STOP */
		} else {
			/* here it's possible to get less than the requested size */
			run = split->st.st_size;

			if ((run & block_mask) != 0) {
				/* LCOV_EXCL_START */
				log_fatal(EINTERNAL, "Internal inconsistency in final parity size %" PRIu64 " with block size %u\n", run, block_size);
				os_abort();
				/* LCOV_EXCL_STOP */
			}

			/*
			 * Non-empty parity splits must form a contiguous prefix.
			 *
			 * If this split cannot allocate even one block while
			 * parity space is still required, do not skip it and
			 * continue allocation in a later split. Doing so would
			 * create an internal zero-sized split such as
			 * [100, 0, 50].
			 */
			if (run == 0 && size != 0) {
				/* LCOV_EXCL_START */
				errno = ENXIO;
				log_fatal(errno, "Failed to allocate all the required parity space. You miss %" PRIu64 " bytes.\n", size);
				return -1;
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
		errno = ENXIO;
		log_fatal(errno, "Failed to allocate all the required parity space. You miss %" PRIu64 " bytes.\n", size);
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
	int zero_seen = 0;
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

		/*
		 * Open for read
		 * O_NOATIME: do not change access time
		 * O_NOFOLLOW: do not follow links to ensure to open the real parity file
		 */
		flags = O_RDONLY | O_BINARY | O_NOFOLLOW | advise_flags(&split->advise);

		split->f = open_noatime(split->path, flags);
		if (split->f == -1) {
			/* LCOV_EXCL_START */
			log_fatal(errno, "Error opening parity file '%s'. %s.\n", split->path, strerror(errno));
			goto bail;
			/* LCOV_EXCL_STOP */
		}

		/* we have a valid file handle */
		++handle->split_mac;

		/* get the stat info */
		ret = fstat(split->f, &split->st);
		if (ret != 0) {
			/* LCOV_EXCL_START */
			log_fatal(errno, "Error accessing parity file '%s'. %s.\n", split->path, strerror(errno));
			goto bail;
			/* LCOV_EXCL_STOP */
		}

		/*
		 * Initialize the physical read/truncation boundary from the current EOF.
		 *
		 * physical_reach_size does NOT imply that parity below this offset is valid.
		 * Parity may be invalid, stale, unwritten, or sparse in any region below
		 * this boundary. It only records the physical EOF boundary for I/O and
		 * truncation. Parity validity is tracked independently per block through
		 * the content state, data hashes, and recomputation.
		 *
		 * While a handle is open, physical_reach_size can intentionally differ from EOF.
		 * In particular, preallocation may extend EOF without extending physical_reach_size.
		 * Writable fix paths normally truncate the file back to physical_reach_size before
		 * closing.
		 *
		 * Across close/reopen there is no separate persistent physical_reach value, so EOF
		 * is used to reconstruct this physical boundary. This reconstruction must not
		 * be interpreted as validation of the parity contained below EOF.
		 */
		split->physical_reach_size = split->st.st_size;

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
				errno = ESOFT;
				log_fatal(ESOFT, "Error in preallocated size of parity file '%s' with size %" PRIu64 " and block %u .\n", split->path, split->size, block_size);
				goto bail;
				/* LCOV_EXCL_STOP */
			}
		}

		/*
		 * Non-empty parity splits must form a contiguous prefix.
		 * Once an empty split is found, all following splits must
		 * also be empty. Otherwise logical parity would contain an
		 * internal hole in the split layout.
		 */
		if (split->size == 0) {
			zero_seen = 1;
		} else if (zero_seen) {
			/* LCOV_EXCL_START */
			errno = ESOFT;
			log_fatal(ESOFT, "Invalid parity split layout: parity file '%s' has size %" PRIu64 " after an empty split.\n", split->path, split->size);
			goto bail;
			/* LCOV_EXCL_STOP */
		}

		ret = advise_open(&split->advise, split->f);
		if (ret != 0) {
			/* LCOV_EXCL_START */
			log_fatal(errno, "Error advising parity file '%s'. %s.\n", split->path, strerror(errno));
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

		/*
		 * Ensure that data changes are written to disk.
		 * This is required to ensure that parity is more updated than content
		 * in case of a system crash.
		 */
		ret = fsync(split->f);
		if (ret != 0) {
			/* LCOV_EXCL_START */
			log_fatal(errno, "Error syncing parity file '%s'. %s.\n", split->path, strerror(errno));
			return -1;
			/* LCOV_EXCL_STOP */
		}
	}
#endif

	return 0;
}

/*
 * Truncate each split to its current physical_reach_size boundary.
 *
 * This removes preallocated or otherwise disposable data beyond physical_reach_size.
 * It does not imply that parity blocks retained below physical_reach_size are
 * valid; parity validity is tracked independently per block by the content state.
 *
 * Persisting this boundary in physical EOF allows the next open to restore
 * physical_reach_size from st_size.
 */
int parity_truncate(struct snapraid_parity_handle* handle)
{
	unsigned s;
	int f_ret = 0;

	for (s = 0; s < handle->split_mac; ++s) {
		struct snapraid_split_handle* split = &handle->split_map[s];
		int ret;

		/*
		 * Discard physical space beyond the extent reached by parity writes
		 * during this operation.
		 *
		 * The retained region below physical_reach_size is not thereby declared valid
		 * parity. physical_reach_size is only a physical truncation boundary; parity
		 * validity is tracked independently per block in the content state.
		 */
		ret = ftruncate(split->f, split->physical_reach_size);
		if (ret != 0) {
			/* LCOV_EXCL_START */
			log_fatal(errno, "Error truncating the parity file '%s' to size %" PRIu64 ". %s.\n", split->path, split->physical_reach_size, strerror(errno));
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

		advise_close(&split->advise, split->f);

		ret = close(split->f);
		if (ret != 0) {
			/* LCOV_EXCL_START */
			/*
			 * This is a serious error, as it may be the result of a failed write
			 * identified at later time.
			 * In a normal file-system (not NFS) it should never happen
			 */
			log_fatal(errno, "Error closing parity file '%s'. %s.\n", split->path, strerror(errno));
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
	unsigned count;
	int ret;

	offset = pos * (data_off_t)block_size;

	split = parity_split_find(handle, &offset);
	if (!split) {
		/* LCOV_EXCL_START */
		errno = ENXIO;
		log_fatal(errno, "Writing parity data outside range at extra offset %" PRIu64 ".\n", offset);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/*
	 * Parity blocks cannot cross split boundaries: offsets and split sizes
	 * are block-aligned, so the entire block belongs to this split.
	 */

	bw_limit(handle->bw, block_size);

	count = 0;
	do {
		write_ret = pwrite(split->f, block_buffer + count, block_size - count, offset + count);
		if (write_ret == -1) {
			if (errno == EINTR)
				continue;

			/* LCOV_EXCL_START */
			if (errno == ENOSPC) {
				log_fatal(errno, "Failed to grow parity file '%s' using write due lack of space.\n", split->path);
			} else {
				log_fatal(errno, "Error writing parity file '%s'. %s.\n", split->path, strerror(errno));
			}
			return -1;
			/* LCOV_EXCL_STOP */
		}
		if (write_ret == 0) {
			/* LCOV_EXCL_START */
			errno = ENXIO;
			log_fatal(errno, "Unexpected 0 write to file '%s'. %s.\n", split->path, strerror(errno));
			return -1;
			/* LCOV_EXCL_STOP */
		}

		count += write_ret;
	} while (count < block_size);

	/*
	 * Extend the physical read/truncation limit only after the complete block
	 * has been written.
	 *
	 * This is not a validity marker. In particular, writes may occur non-contiguously,
	 * so bytes between the old boundary and this block are not implied to contain
	 * valid parity. Parity correctness is tracked independently per block in the
	 * content state.
	 */
	if (split->physical_reach_size < offset + block_size)
		split->physical_reach_size = offset + block_size;

	ret = advise_write(&split->advise, split->f, offset, block_size);
	if (ret != 0) {
		/* LCOV_EXCL_START */
		log_fatal(errno, "Error advising parity file '%s'. %s.\n", split->path, strerror(errno));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	return 0;
}

int parity_read(struct snapraid_parity_handle* handle, block_off_t pos, unsigned char* block_buffer, unsigned block_size)
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
		errno = ENXIO;
		log_error(errno, "Reading parity data outside range at extra offset %" PRIu64 ".\n", offset);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/*
	 * Parity blocks cannot cross split boundaries: offsets and split sizes
	 * are block-aligned, so the entire block belongs to this split.
	 */

	/*
	 * physical_reach_size only limits the physical offset that this handle considers
	 * readable. Passing this check does not mean that the parity bytes are valid.
	 * A readable region may contain stale, sparse, unwritten, or corrupted parity,
	 * which higher-level check/recovery logic must validate independently.
	 */
	if (offset >= split->physical_reach_size) {
		/* LCOV_EXCL_START */
		errno = ENXIO;
		log_error(errno, "Reading over the end from parity file '%s' at offset %" PRIu64 " for size %u.\n", split->path, offset, block_size);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	bw_limit(handle->bw, block_size);

	count = 0;
	do {
		read_ret = pread(split->f, block_buffer + count, block_size - count, offset + count);
		if (read_ret == -1) {
			if (errno == EINTR)
				continue;

			/* LCOV_EXCL_START */
			log_error(errno, "Error reading parity file '%s' at offset %" PRIu64 " for size %u. %s.\n", split->path, offset + count, block_size - count, strerror(errno));
			return -1;
			/* LCOV_EXCL_STOP */
		}
		if (read_ret == 0) {
			/* LCOV_EXCL_START */
			errno = ENXIO;
			log_error(errno, "Unexpected end of parity file '%s' at offset %" PRIu64 ". %s.\n", split->path, offset, strerror(errno));
			return -1;
			/* LCOV_EXCL_STOP */
		}

		count += read_ret;
	} while (count < block_size);

	ret = advise_read(&split->advise, split->f, offset, block_size);
	if (ret != 0) {
		/* LCOV_EXCL_START */
		log_error(errno, "Error advising parity file '%s'. %s.\n", split->path, strerror(errno));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	return block_size;
}

