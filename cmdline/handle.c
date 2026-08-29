// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2011 Andrea Mazzoleni

#include "os/portable.h"

#include "elem.h"
#include "support.h"
#include "handle.h"

/****************************************************************************/
/* handle */

int handle_create(struct snapraid_handle* handle, struct snapraid_file* file, int mode)
{
	int ret;
	int flags;
	char path[PATH_MAX];
	char path_unrecoverable[PATH_MAX];

	/* if it's the same file, and already opened, nothing to do */
	if (handle->file == file && handle->f != -1) {
		return 0;
	}

	advise_init(&handle->advise, mode);

	pathprint(path, sizeof(path), "%s%s", handle->disk->dir, file->sub);
	pathprint(path_unrecoverable, sizeof(path_unrecoverable), "%s.unrecoverable", path);

	ret = mkancestor(path);
	if (ret != 0) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/* initial values, changed later if required */
	handle->created = 0;
	handle->readonly_errno = 0;

	/*
	 * Flags for opening
	 * O_BINARY: open as binary file (Windows only)
	 * O_NOFOLLOW: do not follow links to ensure to open the real file
	 */
	flags = O_BINARY | O_NOFOLLOW | advise_flags(&handle->advise);

	/*
	 * If this recovery attempt was actually quarantined during the current fix run,
	 * continue operating on its .unrecoverable copy.
	 *
	 * FILE_IS_QUARANTINED is runtime state and is not carried to a later fix.
	 * FILE_IS_DAMAGED alone does not imply that the file was physically quarantined,
	 * because some fix modes deliberately keep damaged files under their normal name.
	 */
	if (file_flag_has(file, FILE_IS_QUARANTINED)) {
		handle->f = open(path_unrecoverable, flags | O_RDWR);

		/* if failed for missing write permission */
		if (handle->f == -1 && (errno == EACCES || errno == EROFS)) {
			int saved_errno = errno;
			/* open for read-only */
			handle->f = open(path_unrecoverable, flags | O_RDONLY);
			if (handle->f != -1) {
				handle->readonly_errno = saved_errno;
			}
		}

		pathcpy(handle->path, sizeof(handle->path), path_unrecoverable);
	} else {
		/*
		 * Start every recovery attempt from the normal filename.
		 *
		 * Do not reuse an .unrecoverable file left by a previous fix. It may
		 * contain recovered CHG blocks whose CURRENT generation could not be
		 * proven. That ambiguity is runtime state and is not persisted with
		 * the file. Reopening such a file in a later fix would make those CHG
		 * blocks appear merely readable and could incorrectly treat them as
		 * valid CURRENT data.
		 *
		 * A previous .unrecoverable file is therefore kept only as the
		 * best-effort result of that recovery attempt, not as input for a
		 * subsequent one.
		 */
		handle->f = open(path, flags | O_RDWR);

		/* if failed for missing write permission */
		if (handle->f == -1 && (errno == EACCES || errno == EROFS)) {
			int saved_errno = errno;
			/* open for read-only */
			handle->f = open(path, flags | O_RDONLY);
			if (handle->f != -1) {
				handle->readonly_errno = saved_errno;
			}
		}

		/* if failed for missing file */
		if (handle->f == -1 && errno == ENOENT) {
			handle->f = open(path, flags | O_RDWR | O_CREAT | O_EXCL, 0600);
			if (handle->f != -1) {
				handle->created = 1;
			} else if (errno == EEXIST) {
				/* if created concurrently, reopen safely */
				handle->f = open(path, flags | O_RDWR);
			}
		}

		pathcpy(handle->path, sizeof(handle->path), path);
	}

	if (handle->f == -1) {
		/* LCOV_EXCL_START */
		log_fatal(errno, "Error opening file '%s'. %s.\n", handle->path, strerror(errno));
		handle_close(handle);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/* just opened */
	handle->file = file;

	/* get the stat info */
	ret = fstat(handle->f, &handle->st);
	if (ret != 0) {
		/* LCOV_EXCL_START */
		log_fatal(errno, "Error accessing file '%s'. %s.\n", handle->path, strerror(errno));
		handle_close(handle);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/* get the size of the existing data */
	handle->physical_reach_size = handle->st.st_size;

	ret = advise_open(&handle->advise, handle->f);
	if (ret != 0) {
		/* LCOV_EXCL_START */
		log_fatal(errno, "Error advising file '%s'. %s.\n", handle->path, strerror(errno));
		handle_close(handle);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	return 0;
}

/**
 * Move an opened file to its persistent .unrecoverable name.
 */
int handle_quarantine(struct snapraid_handle* handle, struct snapraid_file* file, int mode)
{
	char path[PATH_MAX];
	char path_to[PATH_MAX];
	int ret;

	(void)mode;

	/* already quarantined during the current fix run */
	if (file_flag_has(file, FILE_IS_QUARANTINED))
		return 0;

	/* mark this recovery attempt as damaged */
	file_flag_set(file, FILE_IS_DAMAGED);

	pathprint(path, sizeof(path), "%s%s", handle->disk->dir, file->sub);
	pathprint(path_to, sizeof(path_to), "%s%s.unrecoverable", handle->disk->dir, file->sub);

#ifdef _WIN32
	/*
	 * Windows may not allow renaming an opened file. Close it before
	 * replacing the previous quarantine and renaming the current file.
	 */
	ret = handle_close(handle);
	if (ret != 0)
		return -1;

	ret = rename(path, path_to);
	if (ret != 0) {
		/* LCOV_EXCL_START */
		log_error(errno, "Error renaming file '%s' to '%s'. %s.\n", path, path_to, strerror(errno));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	file_flag_set(file, FILE_IS_QUARANTINED);

	/*
	 * FILE_IS_QUARANTINED is set, so handle_create() reopens the
	 * .unrecoverable file belonging to the current recovery attempt.
	 */
	ret = handle_create(handle, file, mode);
	if (ret != 0)
		return -1;
#else
	/*
	 * POSIX rename() atomically replaces an existing destination file.
	 * The opened descriptor continues to reference the renamed inode, so
	 * no close/reopen is required.
	 */
	ret = rename(path, path_to);
	if (ret != 0) {
		/* LCOV_EXCL_START */
		log_error(errno, "Error renaming file '%s' to '%s'. %s.\n", path, path_to, strerror(errno));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	file_flag_set(file, FILE_IS_QUARANTINED);
	pathcpy(handle->path, sizeof(handle->path), path_to);
#endif

	return 0;
}

/**
 * Remove an .unrecoverable file left by a previous recovery attempt.
 */
int handle_unrecoverable_remove(struct snapraid_handle* handle, struct snapraid_file* file)
{
	char path[PATH_MAX];
	int ret;

	pathprint(path, sizeof(path), "%s%s.unrecoverable", handle->disk->dir, file->sub);

	ret = remove(path);
	if (ret != 0 && errno != ENOENT) {
		/* LCOV_EXCL_START */
		log_error(errno, "Error removing file '%s'. %s.\n", path, strerror(errno));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	return 0;
}

int handle_truncate(struct snapraid_handle* handle, struct snapraid_file* file)
{
	int ret;

	if (handle->readonly_errno != 0) {
		if (file->size == handle->st.st_size)
			return 0;
		/* LCOV_EXCL_START */
		errno = handle->readonly_errno;
		log_fatal(errno, "Failed to truncate file '%s' for missing write permission. %s.\n", handle->path, strerror(errno));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	ret = ftruncate(handle->f, file->size);
	if (ret != 0) {
		/* LCOV_EXCL_START */
		if (errno == EACCES) {
			log_fatal(errno, "Failed to truncate file '%s' for missing write permission.\n", handle->path);
		} else {
			log_fatal(errno, "Error truncating file '%s'. %s.\n", handle->path, strerror(errno));
		}
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/* adjust the size to the truncated size */
	handle->physical_reach_size = file->size;

	return 0;
}

int handle_open(struct snapraid_handle* handle, struct snapraid_file* file, int mode, log_ptr* out_missing)
{
	int ret;
	int flags;

	if (!out_missing)
		out_missing = log_error;

	/* if already opened, nothing to do */
	if (handle->file != 0 && handle->file == file && handle->f != -1) {
		return 0;
	}

	advise_init(&handle->advise, mode);
	pathprint(handle->path, sizeof(handle->path), "%s%s", handle->disk->dir, file->sub);

	/* for sure not created */
	handle->created = 0;
	handle->readonly_errno = 0;

	/*
	 * Flags for opening
	 * O_BINARY: open as binary file (Windows only)
	 * O_NOFOLLOW: do not follow links to ensure to open the real file
	 */
	flags = O_BINARY | O_NOFOLLOW | advise_flags(&handle->advise);

	/* open for read */
	handle->f = open_noatime(handle->path, flags | O_RDONLY);
	if (handle->f == -1) {
		if (errno == ENOENT)
			out_missing(errno, "Missing file '%s'.\n", handle->path);
		else if (errno == EACCES)
			log_error(errno, "Permission denied for file '%s'.\n", handle->path);
		else
			log_error(errno, "Error opening file '%s'. %s.\n", handle->path, strerror(errno));
		handle_close(handle);
		return -1;
	}

	/* just opened */
	handle->file = file;

	/* get the stat info */
	ret = fstat(handle->f, &handle->st);
	if (ret != 0) {
		/* LCOV_EXCL_START */
		log_error(errno, "Error accessing file '%s'. %s.\n", handle->path, strerror(errno));
		handle_close(handle);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/* get the size of the existing data */
	handle->physical_reach_size = handle->st.st_size;

	ret = advise_open(&handle->advise, handle->f);
	if (ret != 0) {
		/* LCOV_EXCL_START */
		log_error(errno, "Error advising file '%s'. %s.\n", handle->path, strerror(errno));
		handle_close(handle);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	return 0;
}

int handle_close(struct snapraid_handle* handle)
{
	int ret;

	/* close if open */
	if (handle->f != -1) {
		advise_close(&handle->advise, handle->f);

		ret = close(handle->f);
		if (ret != 0) {
			/* LCOV_EXCL_START */
			log_fatal(errno, "Error closing file '%s'. %s.\n", handle->path, strerror(errno));

			/* invalidate for error */
			handle->file = 0;
			handle->f = -1;
			handle->physical_reach_size = 0;
			return -1;
			/* LCOV_EXCL_STOP */
		}
	}

	/* reset the descriptor */
	handle->file = 0;
	handle->f = -1;
	handle->physical_reach_size = 0;
	handle->readonly_errno = 0;

	return 0;
}

ssize_t handle_read(struct snapraid_handle* handle, block_off_t file_pos, unsigned char* block_buffer, unsigned block_size, log_ptr* out_missing)
{
	ssize_t read_ret;
	data_off_t offset;
	size_t read_size;
	unsigned count;
	int ret;

	offset = file_pos * (data_off_t)block_size;

	if (!out_missing)
		out_missing = log_error;

	/* check if we are going to read only not initialized data */
	if (offset >= handle->physical_reach_size) {
		/* if the file is missing, it's at 0 size, or it's rebuilt while reading */
		if (offset == handle->physical_reach_size || handle->physical_reach_size == 0) {
			errno = ENOENT;
			if (offset == 0) {
				out_missing(errno, "Missing file '%s'.\n", handle->path);
			} else {
				out_missing(errno, "Missing data in file '%s' at offset %" PRIu64 ".\n", handle->path, offset);
			}
		} else {
			errno = ENXIO;
			log_error(errno, "Reading over the end from file '%s' at offset %" PRIu64 ".\n", handle->path, offset);
		}
		return -1;
	}

	read_size = file_block_size(handle->file, file_pos, block_size);

	bw_limit(handle->bw, block_size);

	count = 0;
	do {
		read_ret = pread(handle->f, block_buffer + count, block_size - count, offset + count);
		if (read_ret == -1) {
			if (errno == EINTR)
				continue;

			/* LCOV_EXCL_START */
			log_error(errno, "Error reading file '%s' at offset %" PRIu64 " for size %u. %s.\n", handle->path, offset + count, block_size - count, strerror(errno));
			return -1;
			/* LCOV_EXCL_STOP */
		}
		if (read_ret == 0) {
			/* LCOV_EXCL_START */
			errno = ENXIO;
			log_error(errno, "Unexpected end of file '%s' at offset %" PRIu64 ". %s.\n", handle->path, offset, strerror(errno));
			return -1;
			/* LCOV_EXCL_STOP */
		}

		count += read_ret;
	} while (count < read_size);

	/* pad with 0 */
	if (read_size < block_size) {
		memset(block_buffer + read_size, 0, block_size - read_size);
	}

	ret = advise_read(&handle->advise, handle->f, offset, block_size);
	if (ret != 0) {
		/* LCOV_EXCL_START */
		log_error(errno, "Error advising file '%s'. %s.\n", handle->path, strerror(errno));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	return read_size;
}

int handle_write(struct snapraid_handle* handle, block_off_t file_pos, unsigned char* block_buffer, unsigned block_size)
{
	ssize_t write_ret;
	data_off_t offset;
	ssize_t write_size;
	ssize_t count;
	int ret;

	if (handle->readonly_errno != 0) {
		/* LCOV_EXCL_START */
		errno = handle->readonly_errno;
		log_fatal(errno, "Failed to write file '%s' for missing write permission. %s.\n", handle->path, strerror(errno));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	offset = file_pos * (data_off_t)block_size;

	write_size = file_block_size(handle->file, file_pos, block_size);

	bw_limit(handle->bw, write_size);

	count = 0;
	do {
		write_ret = pwrite(handle->f, block_buffer + count, write_size - count, offset + count);
		if (write_ret == -1) {
			if (errno == EINTR)
				continue;

			/* LCOV_EXCL_START */
			log_fatal(errno, "Error writing file '%s'. %s.\n", handle->path, strerror(errno));
			return -1;
			/* LCOV_EXCL_STOP */
		}
		if (write_ret == 0) {
			/* LCOV_EXCL_START */
			errno = ENXIO;
			log_fatal(errno, "Unexpected 0 write to file '%s'. %s.\n", handle->path, strerror(errno));
			return -1;
			/* LCOV_EXCL_STOP */
		}

		count += write_ret;
	} while (count < write_size);

	/* adjust the size of the written/retained data */
	if (handle->physical_reach_size < offset + write_size) {
		handle->physical_reach_size = offset + write_size;
	}

	ret = advise_write(&handle->advise, handle->f, offset, block_size);
	if (ret != 0) {
		/* LCOV_EXCL_START */
		log_fatal(errno, "Error advising file '%s'. %s.\n", handle->path, strerror(errno));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	return 0;
}

int handle_utime(struct snapraid_handle* handle)
{
	int ret;

	/* do nothing if not opened */
	if (handle->f == -1)
		return 0;

	ret = fmtime(handle->f, handle->file->mtime_sec, handle->file->mtime_nsec);

	if (ret != 0) {
		/* LCOV_EXCL_START */
		log_fatal(errno, "Error timing file '%s'. %s.\n", handle->file->sub, strerror(errno));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	return 0;
}

struct snapraid_handle* handle_mapping(struct snapraid_state* state, unsigned* handlemax)
{
	tommy_node* i;
	unsigned j;
	unsigned size = 0;
	struct snapraid_handle* handle;

	/*
	 * The handle array is indexed by persistent parity mapping position,
	 * not by data disk count or enumeration order.
	 *
	 * Mapping holes must remain as empty handles: they contribute zero data
	 * to RAID while preserving the parity column of following disks.
	 *
	 * Do not compact this array: renumbering a disk changes its RAID column
	 * and invalidates existing parity against the content file mapping.
	 */
	/* get the size of the mapping */
	size = 0;
	for (i = state->maplist; i != 0; i = i->next) {
		struct snapraid_map* map = i->data;
		if (map->position > size)
			size = map->position;
	}
	++size; /* size is one more than the max */

	handle = nalloc_nofail(size, sizeof(struct snapraid_handle));

	for (j = 0; j < size; ++j) {
		/* default for empty position */
		handle[j].disk = 0;
		handle[j].file = 0;
		handle[j].f = -1;
		handle[j].physical_reach_size = 0;
		handle[j].readonly_errno = 0;
		handle[j].bw = 0;
	}

	/* set the vector */
	for (i = state->disklist; i != 0; i = i->next) {
		struct snapraid_map* map;
		struct snapraid_disk* disk = i->data;
		tommy_node* k;

		/* search the mapping for this disk */
		map = 0;
		for (k = state->maplist; k != 0; k = k->next) {
			map = k->data;
			if (strcmp(disk->name, map->name) == 0)
				break;
		}
		if (!map) {
			/* LCOV_EXCL_START */
			log_fatal(EINTERNAL, "Internal error for inconsistent disk mapping.\n");
			os_abort();
			/* LCOV_EXCL_STOP */
		}

		handle[map->position].disk = disk;
	}

	*handlemax = size;
	return handle;
}
