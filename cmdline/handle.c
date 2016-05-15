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
/* handle */

int handle_create(struct snapraid_handle* handle, struct snapraid_file* file, int mode)
{
	int ret;
	int flags;

	/* if it's the same file, and already opened, nothing to do */
	if (handle->file == file && handle->f != -1) {
		return 0;
	}

	advise_init(&handle->advise, mode);
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
	flags = O_BINARY | O_NOFOLLOW | advise_flags(&handle->advise);

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

	ret = advise_open(&handle->advise, handle->f);
	if (ret != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Error advising file '%s'. %s.\n", handle->path, strerror(errno));
		return -1;
		/* LCOV_EXCL_STOP */
	}

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
	if (handle->file == file && handle->file != 0 && handle->f != -1) {
		return 0;
	}

	advise_init(&handle->advise, mode);
	pathprint(handle->path, sizeof(handle->path), "%s%s", handle->disk->dir, file->sub);

	/* for sure not created */
	handle->created = 0;

	/* flags for opening */
	/* O_BINARY: open as binary file (Windows only) */
	/* O_NOFOLLOW: do not follow links to ensure to open the real file */
	flags = O_BINARY | O_NOFOLLOW | advise_flags(&handle->advise);

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

	ret = advise_open(&handle->advise, handle->f);
	if (ret != 0) {
		/* LCOV_EXCL_START */
		out("Error advising file '%s'. %s.\n", handle->path, strerror(errno));
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
	int ret;

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

	count = 0;
	do {
		/* read the full block to support O_DIRECT */
		read_ret = pread(handle->f, block_buffer + count, block_size - count, offset + count);
		if (read_ret < 0) {
			/* LCOV_EXCL_START */
			out("Error reading file '%s' at offset %" PRIu64 " for size %u. %s.\n", handle->path, offset + count, block_size - count, strerror(errno));
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

	ret = advise_read(&handle->advise, handle->f, offset, block_size);
	if (ret != 0) {
		/* LCOV_EXCL_START */
		out("Error advising file '%s'. %s.\n", handle->path, strerror(errno));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	return read_size;
}

int handle_write(struct snapraid_handle* handle, block_off_t file_pos, unsigned char* block_buffer, unsigned block_size)
{
	ssize_t write_ret;
	data_off_t offset;
	unsigned write_size;
	int ret;

	offset = file_pos * (data_off_t)block_size;

	write_size = file_block_size(handle->file, file_pos, block_size);

	write_ret = pwrite(handle->f, block_buffer, write_size, offset);
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

	ret = advise_write(&handle->advise, handle->f, offset, block_size);
	if (ret != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Error advising file '%s'. %s.\n", handle->path, strerror(errno));
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
		log_fatal("Error timing file '%s'. %s.\n", handle->file->sub, strerror(errno));
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
		map = 0;
		for (k = state->maplist; k != 0; k = k->next) {
			map = k->data;
			if (strcmp(disk->name, map->name) == 0)
				break;
		}
		if (!map) {
			/* LCOV_EXCL_START */
			log_fatal("Internal error for inconsistent disk mapping.\n");
			os_abort();
			/* LCOV_EXCL_STOP */
		}

		handle[map->position].disk = disk;
	}

	*handlemax = size;
	return handle;
}

