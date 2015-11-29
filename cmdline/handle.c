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

#include "elem.h"
#include "support.h"
#include "handle.h"

/****************************************************************************/
/* xls */

static unsigned char XLS_MAGIC[8] = { 0xd0, 0xcf, 0x11, 0xe0, 0xa1, 0xb1, 0x1a, 0xe1 };

/**
 * Patch XLS files to clear hidden modification made by Excel.
 *
 * Excel modifies .XLS files even when just opening them, restoring
 * the timestamp to its original value.
 *
 * To avoid that this operation invalidates the parity, we here patches
 * the XLS files, clearing the date/time data usually modified.
 *
 * How to prevent Excel from modifying the file on exit?
 * http://superuser.com/questions/664549/how-to-prevent-excel-from-modifying-the-file-on-exit
 */
void xls_patch(unsigned char* data, unsigned size)
{
	unsigned char* p;
	unsigned char* end;

	if (size < 512)
		return;

	/* check the header magic */
	if (memcmp(data, XLS_MAGIC, 8) != 0)
		return;

	/* check the endianess */
	if (data[0x1c] != 0xfe || data[0x1d] != 0xff)
		return;

	/* walk all the records */
	end = data + size;
	p = data;
	while (p + 4 <= end) {
		unsigned id = p[0] + ((unsigned)p[1] << 8);
		unsigned len = p[2] + ((unsigned)p[3] << 8);

		p += 4;

		/* check if the record is complely in the block */
		if (p + len > end)
			return;

		/* search for the USRINFO 193h record */
		if (id == 0x193 && len >= 33) {
			/* clear the data/time information */
			memset(p + 24,0, 8);
		}

		p += len;
	}
}

/****************************************************************************/
/* handle */

int handle_create(struct snapraid_handle* handle, struct snapraid_file* file, int mode)
{
	int ret;
	int flags;

	/* if it's the same file, and already opened, nothing to do */
	if (handle->file == file && handle->f != -1) {
		return 0;
	}

	pathprint(handle->path, sizeof(handle->path), "%s%s", handle->disk->dir, file->sub);

	ret = mkancestor(handle->path);
	if (ret != 0) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/* initial values, changed later if required */
	handle->created = 0;

	/* flags for opening */
	/* O_BINARY: open as binary file (Windows only) */
	/* O_NOFOLLOW: do not follow links to ensure to open the real file */
	/* O_SEQUENTIAL: improve performance for sequential access (Windows only) */
	flags = O_BINARY | O_NOFOLLOW;
	if ((mode & MODE_SEQUENTIAL) != 0)
		flags |= O_SEQUENTIAL;

	/* open for read write */
	handle->f = open(handle->path, flags | O_RDWR);

	/* if failed for missing write permission */
	if (handle->f == -1 && (errno == EACCES || errno == EROFS)) {
		/* open for real-only */
		handle->f = open(handle->path, flags | O_RDONLY);
	}

	/* if failed for missing file */
	if (handle->f == -1 && errno == ENOENT) {
		char path_from[PATH_MAX];

		/* check if exists a .unrecoverable copy, and rename to the real one */
		pathprint(path_from, sizeof(path_from), "%s.unrecoverable", handle->path);

		if (rename(path_from, handle->path) == 0) {
			/* open for read write */
			handle->f = open(handle->path, flags | O_RDWR);
		} else {
			/* create it */
			handle->f = open(handle->path, flags | O_RDWR | O_CREAT, 0600);
			if (handle->f != -1) {
				/* mark it as created if really done */
				handle->created = 1;
			}
		}
	}

	if (handle->f == -1) {
		/* LCOV_EXCL_START */
		/* invalidate for error */
		handle->file = 0;
		handle->f = -1;
		handle->valid_size = 0;

		log_fatal("Error opening file '%s'. %s.\n", handle->path, strerror(errno));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/* just opened */
	handle->file = file;

	/* get the stat info */
	ret = fstat(handle->f, &handle->st);
	if (ret != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Error accessing file '%s'. %s.\n", handle->path, strerror(errno));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/* get the size of the existing data */
	handle->valid_size = handle->st.st_size;

#if HAVE_POSIX_FADVISE
	if ((mode & MODE_SEQUENTIAL) != 0) {
		/* advise sequential access */
		ret = posix_fadvise(handle->f, 0, 0, POSIX_FADV_SEQUENTIAL);
		if (ret != 0) {
			/* LCOV_EXCL_START */
			log_fatal("Error advising file '%s'. %s.\n", handle->path, strerror(ret));
			return -1;
			/* LCOV_EXCL_STOP */
		}
	}
#endif

	return 0;
}

int handle_truncate(struct snapraid_handle* handle, struct snapraid_file* file)
{
	int ret;

	ret = ftruncate(handle->f, file->size);
	if (ret != 0) {
		/* LCOV_EXCL_START */
		if (errno == EACCES) {
			log_fatal("Failed to truncate file '%s' for missing write permission.\n", handle->path);
		} else {
			log_fatal("Error truncating file '%s'. %s.\n", handle->path, strerror(errno));
		}
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/* adjust the size to the truncated size */
	handle->valid_size = file->size;

	return 0;
}

int handle_open(struct snapraid_handle* handle, struct snapraid_file* file, int mode, fptr* out, fptr* out_missing)
{
	int ret;
	int flags;

	if (!out_missing)
		out_missing = out;

	/* if already opened, nothing to do */
	if (handle->file == file && handle->f != -1) {
		return 0;
	}

	/* identify Excel files (FNM_CASEFOLD is case insensitive) */
	handle->xls = fnmatch("*.xls", file->sub, FNM_CASEFOLD) == 0;

	pathprint(handle->path, sizeof(handle->path), "%s%s", handle->disk->dir, file->sub);

	/* for sure not created */
	handle->created = 0;

	/* flags for opening */
	/* O_BINARY: open as binary file (Windows only) */
	/* O_NOFOLLOW: do not follow links to ensure to open the real file */
	/* O_SEQUENTIAL: improve performance for sequential access (Windows only) */
	flags = O_BINARY | O_NOFOLLOW;
	if ((mode & MODE_SEQUENTIAL) != 0)
		flags |= O_SEQUENTIAL;

	/* open for read */
	handle->f = open_noatime(handle->path, flags | O_RDONLY);
	if (handle->f == -1) {
		/* invalidate for error */
		handle->file = 0;
		handle->f = -1;
		handle->valid_size = 0;

		if (errno == ENOENT)
			out_missing("Missing file '%s'.\n", handle->path);
		else
			out("Error opening file '%s'. %s.\n", handle->path, strerror(errno));
		return -1;
	}

	/* just opened */
	handle->file = file;

	/* get the stat info */
	ret = fstat(handle->f, &handle->st);
	if (ret != 0) {
		/* LCOV_EXCL_START */
		out("Error accessing file '%s'. %s.\n", handle->path, strerror(errno));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/* get the size of the existing data */
	handle->valid_size = handle->st.st_size;

#if HAVE_POSIX_FADVISE
	if ((mode & MODE_SEQUENTIAL) != 0) {
		/* advise sequential access */
		ret = posix_fadvise(handle->f, 0, 0, POSIX_FADV_SEQUENTIAL);
		if (ret != 0) {
			/* LCOV_EXCL_START */
			out("Error advising file '%s'. %s.\n", handle->path, strerror(ret));
			return -1;
			/* LCOV_EXCL_STOP */
		}
	}
#endif

	return 0;
}

int handle_close(struct snapraid_handle* handle)
{
	int ret;

	/* close if open */
	if (handle->f != -1) {
		ret = close(handle->f);
		if (ret != 0) {
			/* LCOV_EXCL_START */
			log_fatal("Error closing file '%s'. %s.\n", handle->file->sub, strerror(errno));

			/* invalidate for error */
			handle->file = 0;
			handle->f = -1;
			handle->valid_size = 0;
			return -1;
			/* LCOV_EXCL_STOP */
		}
	}

	/* reset the descriptor */
	handle->file = 0;
	handle->f = -1;
	handle->valid_size = 0;

	return 0;
}

int handle_read(struct snapraid_handle* handle, block_off_t file_pos, unsigned char* block_buffer, unsigned block_size, fptr* out, fptr* out_missing)
{
	ssize_t read_ret;
	data_off_t offset;
	unsigned read_size;
	unsigned count;

	offset = file_pos * (data_off_t)block_size;

	if (!out_missing)
		out_missing = out;

	/* check if we are going to read only not initialized data */
	if (offset >= handle->valid_size) {
		/* if the file is missing, it's at 0 size, or it's rebuilt while reading */
		if (offset == handle->valid_size || handle->valid_size == 0)
			out_missing("Reading data from missing file '%s' at offset %" PRIu64 ".\n", handle->path, offset);
		else
			out("Reading missing data from file '%s' at offset %" PRIu64 ".\n", handle->path, offset);
		return -1;
	}

	read_size = file_block_size(handle->file, file_pos, block_size);

#if !HAVE_PREAD
	if (lseek(handle->f, offset, SEEK_SET) != offset) {
		/* LCOV_EXCL_START */
		out("Error seeking file '%s' at offset %" PRIu64 ". %s.\n", handle->path, offset, strerror(errno));
		return -1;
		/* LCOV_EXCL_STOP */
	}
#endif

	count = 0;
	do {

#if HAVE_PREAD
		read_ret = pread(handle->f, block_buffer + count, read_size - count, offset + count);
#else
		read_ret = read(handle->f, block_buffer + count, read_size - count);
#endif

		if (read_ret < 0) {
			/* LCOV_EXCL_START */
			out("Error reading file '%s' at offset %" PRIu64 " for size %u. %s.\n", handle->path, offset + count, read_size - count, strerror(errno));
			return -1;
			/* LCOV_EXCL_STOP */
		}
		if (read_ret == 0) {
			out("Unexpected end of file '%s' at offset %" PRIu64 ". %s.\n", handle->path, offset, strerror(errno));
			return -1;
		}

		count += read_ret;
	} while (count < read_size);

	/* pad with 0 */
	if (read_size < block_size) {
		memset(block_buffer + read_size, 0, block_size - read_size);
	}

	/* if it's an xls file, patch the header */
	if (handle->xls && file_pos == 0)
		xls_patch(block_buffer, block_size);

	/* Here isn't needed to call posix_fadvise(..., POSIX_FADV_DONTNEED) */
	/* because we already advised sequential access with POSIX_FADV_SEQUENTIAL. */
	/* In Linux 2.6.33 it's enough to ensure that data is not kept in the cache. */
	/* Better to do nothing and save a syscall for each block. */

	/* Here we cannot call posix_fadvise(..., POSIX_FADV_WILLNEED) for the next block */
	/* because it may be blocking */
	/* See Ted Ts'o comment in "posix_fadvise(POSIX_FADV_WILLNEED) waits before returning?" */
	/* at: https://lkml.org/lkml/2010/12/6/122 */
	/* We relay only on the automatic readahead triggered by POSIX_FADV_SEQUENTIAL. */

	return read_size;
}

int handle_write(struct snapraid_handle* handle, block_off_t file_pos, unsigned char* block_buffer, unsigned block_size)
{
	ssize_t write_ret;
	data_off_t offset;
	unsigned write_size;

	offset = file_pos * (data_off_t)block_size;

	write_size = file_block_size(handle->file, file_pos, block_size);

#if HAVE_PWRITE
	write_ret = pwrite(handle->f, block_buffer, write_size, offset);
#else
	if (lseek(handle->f, offset, SEEK_SET) != offset) {
		/* LCOV_EXCL_START */
		log_fatal("Error seeking file '%s'. %s.\n", handle->path, strerror(errno));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	write_ret = write(handle->f, block_buffer, write_size);
#endif
	if (write_ret != (ssize_t)write_size) { /* conversion is safe because block_size is always small */
		/* LCOV_EXCL_START */
		log_fatal("Error writing file '%s'. %s.\n", handle->path, strerror(errno));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/* adjust the size of the valid data */
	if (handle->valid_size < offset + write_size) {
		handle->valid_size = offset + write_size;
	}

	/* Here doesn't make sense to call posix_fadvise(..., POSIX_FADV_DONTNEED) because */
	/* at this time the data is still not yet written and it cannot be discharged. */

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
		log_fatal("Error timing file '%s'. %s.\n", handle->file->sub, strerror(errno));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	return 0;
}

struct snapraid_handle* handle_map(struct snapraid_state* state, unsigned* handlemax)
{
	tommy_node* i;
	unsigned j;
	unsigned size = 0;
	struct snapraid_handle* handle;

	/* get the size of the mapping */
	size = 0;
	for (i = state->maplist; i != 0; i = i->next) {
		struct snapraid_map* map = i->data;
		if (map->position > size)
			size = map->position;
	}
	++size; /* size is one more than the max */

	handle = malloc_nofail(size * sizeof(struct snapraid_handle));

	for (j = 0; j < size; ++j) {
		/* default for empty position */
		handle[j].disk = 0;
		handle[j].file = 0;
		handle[j].f = -1;
		handle[j].valid_size = 0;
	}

	/* set the vector */
	for (i = state->disklist; i != 0; i = i->next) {
		struct snapraid_map* map;
		struct snapraid_disk* disk = i->data;
		tommy_node* k;

		/* search the mapping for this disk */
		for (k = state->maplist; k != 0; k = k->next) {
			map = k->data;
			if (strcmp(disk->name, map->name) == 0)
				break;
		}
		if (k == 0) {
			/* LCOV_EXCL_START */
			log_fatal("Internal error for inconsistent disk mapping.\n");
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		handle[map->position].disk = disk;
	}

	*handlemax = size;
	return handle;
}

