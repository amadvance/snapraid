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
#include "state.h"
#include "parity.h"

/****************************************************************************/
/* parity */

pos_t parity_resize(struct snapraid_state* state)
{
	unsigned disk_count = tommy_array_size(&state->diskarr);
	pos_t parity_block;
	unsigned i;

	/* compute the size of the parity file */
	parity_block = 0;
	for(i=0;i<disk_count;++i) {
		struct snapraid_disk* disk = tommy_array_get(&state->diskarr, i);

		/* start from the declared size */
		pos_t block = tommy_array_size(&disk->blockarr);

		/* decrease the block until an allocated block */
		while (block > 0 && tommy_array_get(&disk->blockarr, block - 1) == 0)
			--block;

		if (i == 0 || parity_block < block)
			parity_block = block;
	}

	return parity_block;
}

int parity_create(const char* path, off_t size)
{
	struct stat st;
	int ret;
	int f;

	f = open(path, O_RDWR | O_CREAT | O_BINARY, 0600);
	if (f == -1) {
		fprintf(stderr, "Error opening parity file '%s'. %s.\n", path, strerror(errno));
		exit(EXIT_FAILURE);
	}

	ret = fstat(f, &st);
	if (ret != 0) {
		fprintf(stderr, "Error accessing parity file '%s'. %s.\n", path, strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (st.st_size < size) {
#if HAVE_POSIX_FALLOCATE
		/* allocate real space */
		ret = posix_fallocate(f, 0, size);
#else
		/* allocate using a sparse file */
		ret = ftruncate(f, size);
#endif
		if (ret != 0) {
			if (errno == ENOSPC) {
				fprintf(stderr, "Failed to grow parity file '%s' due lack of space.\n", path);
			} else {
				fprintf(stderr, "Error growing parity file '%s'. %s.\n", path, strerror(errno));
			}
			exit(EXIT_FAILURE);
		}
	} else if (st.st_size > size) {
		ret = ftruncate(f, size);
		if (ret != 0) {
			fprintf(stderr, "Error truncating parity file '%s'. %s.\n", path, strerror(errno));
			exit(EXIT_FAILURE);
		}
	}

#if HAVE_POSIX_FADVISE
	/* advise sequential access */
	ret = posix_fadvise(f, 0, 0, POSIX_FADV_SEQUENTIAL);
	if (ret != 0) {
		fprintf(stderr, "Error advising parity file '%s'. %s.\n", path, strerror(errno));
		exit(EXIT_FAILURE);
	}
#endif

	return f;
}

int parity_open(int ret_on_error, const char* path)
{
	int ret;
	int f;

	f = open(path, O_RDONLY | O_BINARY);
	if (f == -1) {
		if (ret_on_error)
			return -1;
		fprintf(stderr, "Error opening parity file '%s'. %s.\n", path, strerror(errno));
		exit(EXIT_FAILURE);
	}

#if HAVE_POSIX_FADVISE
	/* advise sequential access */
	ret = posix_fadvise(f, 0, 0, POSIX_FADV_SEQUENTIAL);
	if (ret != 0) {
		/* here we always abort on error, as advise is supposed to never fail */
		fprintf(stderr, "Error advising parity file '%s'. %s.\n", path, strerror(errno));
		exit(EXIT_FAILURE);
	}
#endif

	return f;
}

void parity_close(const char* path, int f)
{
	int ret;

	/* ensure that data changes are written to disk */
	/* this is required to ensure that parity is more updated than content */
	/* in case of a system crash */
	ret = fsync(f);
	if (ret != 0) {
		fprintf(stderr, "Error synching parity file '%s'. %s.\n", path, strerror(errno));
		exit(EXIT_FAILURE);
	}

	ret = close(f);
	if (ret != 0) {
		fprintf(stderr, "Error closing parity file '%s'. %s.\n", path, strerror(errno));
		exit(EXIT_FAILURE);
	}
}

void parity_write(const char* path, int f, pos_t pos, unsigned char* block_buffer, unsigned block_size)
{
	ssize_t write_ret;
	off_t offset;

	offset = pos * (off_t)block_size;

	write_ret = pwrite(f, block_buffer, block_size, offset);
	if (write_ret != block_size) {
		if (errno == ENOSPC) {
			fprintf(stderr, "Failed to grow parity file '%s' due lack of space.\n", path);
		} else {
			fprintf(stderr, "Error writing file '%s'. %s.\n", path, strerror(errno));
		}
		exit(EXIT_FAILURE);
	}
}

int parity_read(int ret_on_error, const char* path, int f, pos_t pos, unsigned char* block_buffer, unsigned block_size)
{
	ssize_t read_ret;
	off_t offset;

	/* if the file is not opened, return an error without trying */
	if (f == -1 && ret_on_error)
		return -1;

	offset = pos * (off_t)block_size;

	read_ret = pread(f, block_buffer, block_size, offset);
	if (read_ret != block_size) {
		if (ret_on_error)
			return ret_on_error;
		fprintf(stderr, "Error reading file '%s'. %s.\n", path, strerror(errno));
		exit(EXIT_FAILURE);
	}

	return block_size;
}

