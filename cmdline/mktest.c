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

/****************************************************************************/
/* random */

/**
 * Pseudo random number generator.
 */
unsigned long long seed = 0;

unsigned rnd(unsigned max)
{
	seed = seed * 6364136223846793005LL + 1442695040888963407LL;

	return (seed >> 32) % max;
}

unsigned rndnz(unsigned max)
{
	if (max <= 1)
		return 1;
	else
		return rnd(max - 1) + 1;
}

void rndnz_range(unsigned char* data, int size)
{
	int i;

	for (i = 0; i < size; ++i)
		data[i] = rndnz(256);
}

void rndnz_damage(unsigned char* data, int size)
{
	int i;

	/* corrupt ensuring always different data */
	for (i = 0; i < size; ++i) {
		unsigned char c;

		do {
			c = rndnz(256);
		} while (c == data[i]);

		data[i] = c;
	}
}

char CHARSET[] = "qwertyuiopasdfghjklzxcvbnm1234567890 .-+";
#define CHARSET_LEN (sizeof(CHARSET) - 1)

void rnd_name(char* file)
{
	int l = 1 + rnd(20);

	while (l) {
		*file++ = CHARSET[rnd(CHARSET_LEN)];
		--l;
	}
	*file = 0;
}

/****************************************************************************/
/* file */

int file_cmp(const void* a, const void* b)
{
	return strcmp(a, b);
}

int fallback(int f, struct stat* st)
{
#if HAVE_FUTIMENS
	struct timespec tv[2];
#else
	struct timeval tv[2];
#endif
	int ret;

#if HAVE_FUTIMENS /* futimens() is preferred because it gives nanosecond precision */
	tv[0].tv_sec = st->st_mtime;
	if (STAT_NSEC(st) != STAT_NSEC_INVALID)
		tv[0].tv_nsec = STAT_NSEC(st);
	else
		tv[0].tv_nsec = 0;
	tv[1].tv_sec = tv[0].tv_sec;
	tv[1].tv_nsec = tv[0].tv_nsec;

	ret = futimens(f, tv);
#elif HAVE_FUTIMES /* fallback to futimes() if nanosecond precision is not available */
	tv[0].tv_sec = st->st_mtime;
	if (STAT_NSEC(st) != STAT_NSEC_INVALID)
		tv[0].tv_usec = STAT_NSEC(st) / 1000;
	else
		tv[0].tv_usec = 0;
	tv[1].tv_sec = tv[0].tv_sec;
	tv[1].tv_usec = tv[0].tv_usec;

	ret = futimes(f, tv);
#elif HAVE_FUTIMESAT /* fallback to futimesat() for Solaris, it only has futimesat() */
	tv[0].tv_sec = st->st_mtime;
	if (STAT_NSEC(st) != STAT_NSEC_INVALID)
		tv[0].tv_usec = STAT_NSEC(st) / 1000;
	else
		tv[0].tv_usec = 0;
	tv[1].tv_sec = tv[0].tv_sec;
	tv[1].tv_usec = tv[0].tv_usec;

	ret = futimesat(f, 0, tv);
#else
#error No function available to set file timestamps
#endif
	return ret;
}

/****************************************************************************/
/* cmd */

/**
 * Create a file with random content.
 * - If the file exists it's rewritten, but avoiding to truncating it to 0.
 */
void cmd_generate_file(const char* path, int size)
{
	unsigned char* data;
	int f;

	/* remove the existing file/symlink if any */
	if (remove(path) != 0) {
		if (errno != ENOENT) {
			/* LCOV_EXCL_START */
			log_fatal("Error removing file %s\n", path);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	} else {
		/* don't truncate files to 0 size to avoid ZERO file size protection */
		++size;
	}

	data = malloc(size);

	/* We don't write zero bytes because we want to test */
	/* the recovering of new files, after an aborted sync */
	/* If the files contains full blocks at zero */
	/* this is an impossible condition to recover */
	/* because we cannot differentiate between an unused block */
	/* and a file filled with 0 */
	rndnz_range(data, size);

	f = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY | O_NOFOLLOW, 0600);
	if (f < 0) {
		/* LCOV_EXCL_START */
		log_fatal("Error creating file %s\n", path);
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	if (write(f, data, size) != size) {
		/* LCOV_EXCL_START */
		log_fatal("Error writing file %s\n", path);
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	if (close(f) != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Error closing file %s\n", path);
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	free(data);
}

/**
 * Create a symlink.
 * - If the file already exists, it's removed.
 */
void cmd_generate_symlink(const char* path, const char* linkto)
{
	/* remove the existing file/symlink if any */
	if (remove(path) != 0) {
		if (errno != ENOENT) {
			/* LCOV_EXCL_START */
			log_fatal("Error removing file %s\n", path);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}

	if (symlink(linkto, path) != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Error writing symlink %s\n", path);
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}
}

/**
 * Create a file or a symlink with a random name.
 */
void cmd_generate(int disk, int size)
{
	char path[PATH_MAX];
	char* file;

	snprintf(path, sizeof(path), "bench/disk%d/", disk);
	file = path + strlen(path);

	/* add a directory */
	*file++ = 'a' + rnd(2);
	*file = 0;

	/* create it */
	if (mkdir(path, 0777) != 0) {
		if (errno != EEXIST) {
			/* LCOV_EXCL_START */
			log_fatal("Error creating directory %s\n", path);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}

	*file++ = '/';

	while (1) {
		/* add a random file */
		rnd_name(file);

		/* skip some invalid file name, see http://en.wikipedia.org/wiki/Filename */
		if (strcmp(file, ".") == 0
			|| strcmp(file, "..") == 0
			|| strcmp(file, "prn") == 0
			|| strcmp(file, "con") == 0
			|| strcmp(file, "nul") == 0
			|| strcmp(file, "aux") == 0
			|| file[0] == ' '
			|| file[strlen(file) - 1] == ' '
			|| file[strlen(file) - 1] == '.'
		) {
			continue;
		}

		break;
	}

#ifndef WIN32 /* Windows XP doesn't support symlinks */
	if (rnd(32) == 0) {
		/* symlink */
		char linkto[PATH_MAX];

		rnd_name(linkto);

		cmd_generate_symlink(path, linkto);
	} else
#endif
	{
		/* file */
		cmd_generate_file(path, size);
	}
}

/**
 * Write a partially a file.
 * - The file must exist.
 * - The file size is not changed.
 * - The written data may be equal or not at the already existing one.
 * - If it's a symlink nothing is done.
 */
void cmd_write(const char* path, int size)
{
	struct stat st;

	if (lstat(path, &st) != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Error accessing %s\n", path);
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	if (S_ISREG(st.st_mode)) {
		unsigned char* data;
		off_t off;
		int f;

		/* not over the end */
		if (size > st.st_size)
			size = st.st_size;

		/* start at random position inside the file */
		if (size < st.st_size)
			off = rnd(st.st_size - size);
		else
			off = 0;

		data = malloc(size);

		rndnz_range(data, size);

		f = open(path, O_WRONLY | O_BINARY | O_NOFOLLOW);
		if (f < 0) {
			/* LCOV_EXCL_START */
			log_fatal("Error creating file %s\n", path);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		if (lseek(f, off, SEEK_SET) != off) {
			/* LCOV_EXCL_START */
			log_fatal("Error seeking file %s\n", path);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		if (write(f, data, size) != size) {
			/* LCOV_EXCL_START */
			log_fatal("Error writing file %s\n", path);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		if (close(f) != 0) {
			/* LCOV_EXCL_START */
			log_fatal("Error closing file %s\n", path);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		free(data);
	}
}

/**
 * Damage a file.
 * - The file must exist.
 * - The file size is not changed.
 * - The written data is SURELY different than the already existing one.
 * - The file timestamp is NOT modified.
 * - If it's a symlink nothing is done.
 */
void cmd_damage(const char* path, int size)
{
	struct stat st;

	/* here a 0 size means to change nothing */
	/* as also the timestamp should not be changed */
	if (!size)
		return;

	if (lstat(path, &st) != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Error accessing %s\n", path);
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	if (st.st_size == 0)
		return;

	if (S_ISREG(st.st_mode)) {
		off_t off;
		unsigned char* data;
		int f;

		/* not over the end */
		if (size > st.st_size)
			size = st.st_size;

		/* start at random position inside the file */
		if (size < st.st_size)
			off = rnd(st.st_size - size);
		else
			off = 0;

		data = malloc(size);

		f = open(path, O_RDWR | O_BINARY | O_NOFOLLOW);
		if (f < 0) {
			/* LCOV_EXCL_START */
			log_fatal("Error creating file %s\n", path);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		if (lseek(f, off, SEEK_SET) != off) {
			/* LCOV_EXCL_START */
			log_fatal("Error seeking file %s\n", path);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		if (read(f, data, size) != size) {
			/* LCOV_EXCL_START */
			log_fatal("Error writing file %s\n", path);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		rndnz_damage(data, size);

		if (lseek(f, off, SEEK_SET) != off) {
			/* LCOV_EXCL_START */
			log_fatal("Error seeking file %s\n", path);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		if (write(f, data, size) != size) {
			/* LCOV_EXCL_START */
			log_fatal("Error writing file %s\n", path);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		if (fallback(f, &st) != 0) {
			/* LCOV_EXCL_START */
			log_fatal("Error setting time for file %s\n", path);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		if (close(f) != 0) {
			/* LCOV_EXCL_START */
			log_fatal("Error closing file %s\n", path);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		free(data);
	}
}

/**
 * Append to a file.
 * - The file must exist.
 * - If it's a symlink nothing is done.
 */
void cmd_append(const char* path, int size)
{
	struct stat st;

	if (lstat(path, &st) != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Error accessing %s\n", path);
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	if (S_ISREG(st.st_mode)) {
		unsigned char* data;
		int f;

		data = malloc(size);

		rndnz_range(data, size);

		f = open(path, O_WRONLY | O_APPEND | O_BINARY | O_NOFOLLOW);
		if (f < 0) {
			/* LCOV_EXCL_START */
			log_fatal("Error opening file %s\n", path);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		if (write(f, data, size) != size) {
			/* LCOV_EXCL_START */
			log_fatal("Error writing file %s\n", path);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		if (close(f) != 0) {
			/* LCOV_EXCL_START */
			log_fatal("Error closing file %s\n", path);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		free(data);
	}
}

/**
 * Truncate a file.
 * - The file must exist.
 * - The file is NEVER truncated to 0.
 * - If it's a symlink nothing is done.
 */
void cmd_truncate(const char* path, int size)
{
	struct stat st;

	if (lstat(path, &st) != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Error accessing %s\n", path);
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	if (S_ISREG(st.st_mode)) {
		off_t off;
		int f;

		/* if file is empty, just rewrite it */
		if (st.st_size == 0) {
			size = 0;
		} else {
			/* don't truncate files to 0 size to avoid ZERO file size protection */
			if (size >= st.st_size)
				size = st.st_size - 1;
		}

		off = st.st_size - size;

		f = open(path, O_WRONLY | O_BINARY | O_NOFOLLOW);
		if (f < 0) {
			/* LCOV_EXCL_START */
			log_fatal("Error opening file %s\n", path);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		if (ftruncate(f, off) != 0) {
			/* LCOV_EXCL_START */
			log_fatal("Error truncating file %s\n", path);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		if (close(f) != 0) {
			/* LCOV_EXCL_START */
			log_fatal("Error closing file %s\n", path);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}
}

/**
 * Delete a file.
 * - The file must exist.
 */
void cmd_delete(const char* path)
{
	if (remove(path) != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Error removing %s\n", path);
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}
}

/**
 * Change a file. Or deleted/truncated/append/created.
 * - The file must exist.
 */
void cmd_change(const char* path, int size)
{
	struct stat st;

	if (!size)
		return;

	if (lstat(path, &st) != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Error accessing %s\n", path);
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	if (S_ISLNK(st.st_mode)) {
		/* symlink */
		if (rnd(2) == 0) {
			/* delete */
			if (remove(path) != 0) {
				/* LCOV_EXCL_START */
				log_fatal("Error removing %s\n", path);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
		} else {
			/* recreate */
			char linkto[PATH_MAX];

			if (remove(path) != 0) {
				/* LCOV_EXCL_START */
				log_fatal("Error removing %s\n", path);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			rnd_name(linkto);

			cmd_generate_symlink(path, linkto);
		}
	} else if (S_ISREG(st.st_mode)) {
		int r;

		r = rnd(4);

		if (r == 0) {
			cmd_write(path, size);
		} else if (r == 1) {
			cmd_append(path, size);
		} else if (r == 2) {
			cmd_truncate(path, size);
		} else {
			cmd_delete(path);
		}
	}
}

void help(void)
{
	printf("Test for " PACKAGE " v" VERSION " by Andrea Mazzoleni, " PACKAGE_URL "\n");
	printf("Usage:\n");
	printf("\tmktest generate SEED DISK_NUM FILE_NUM FILE_SIZE\n");
	printf("\tmktest damage SEED NUM SIZE FILE\n");
	printf("\tmktest write SEED NUM SIZE FILE\n");
	printf("\tmktest change SEED SIZE FILE\n");
	printf("\tmktest append SEED SIZE FILE\n");
	printf("\tmktest truncate SEED SIZE FILE\n");
}

int main(int argc, char* argv[])
{
	int i, j, b;

	lock_init();

	if (argc < 2) {
		help();
		exit(EXIT_SUCCESS);
	}

	if (strcmp(argv[1], "generate") == 0) {
		int disk, file, size;

		if (argc != 6) {
			/* LCOV_EXCL_START */
			help();
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		seed = atoi(argv[2]);
		disk = atoi(argv[3]);
		file = atoi(argv[4]);
		size = atoi(argv[5]);

		for (i = 0; i < disk; ++i) {
			for (j = 0; j < file; ++j) {
				if (j == 0)
					/* create at least a big one */
					cmd_generate(i + 1, size);
				else if (j == 1)
					/* create at least an empty one */
					cmd_generate(i + 1, 0);
				else
					cmd_generate(i + 1, rnd(size));
			}
		}
	} else if (strcmp(argv[1], "write") == 0) {
		int fail, size;

		if (argc < 6) {
			/* LCOV_EXCL_START */
			help();
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		seed = atoi(argv[2]);
		fail = atoi(argv[3]);
		size = atoi(argv[4]);
		b = 5;

		/* sort the file names */
		qsort(&argv[b], argc - b, sizeof(argv[b]), file_cmp);

		for (i = b; i < argc; ++i)
			for (j = 0; j < fail; ++j)
				cmd_write(argv[i], rndnz(size));
	} else if (strcmp(argv[1], "damage") == 0) {
		int fail, size;

		if (argc < 6) {
			/* LCOV_EXCL_START */
			help();
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		seed = atoi(argv[2]);
		fail = atoi(argv[3]);
		size = atoi(argv[4]);
		b = 5;

		/* sort the file names */
		qsort(&argv[b], argc - b, sizeof(argv[b]), file_cmp);

		for (i = b; i < argc; ++i)
			for (j = 0; j < fail; ++j)
				cmd_damage(argv[i], rndnz(size)); /* at least one byte */
	} else if (strcmp(argv[1], "append") == 0) {
		int size;

		if (argc < 5) {
			/* LCOV_EXCL_START */
			help();
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		seed = atoi(argv[2]);
		size = atoi(argv[3]);
		b = 4;

		/* sort the file names */
		qsort(&argv[b], argc - b, sizeof(argv[b]), file_cmp);

		for (i = b; i < argc; ++i)
			cmd_append(argv[i], rndnz(size)); /* at least one byte */
	} else if (strcmp(argv[1], "truncate") == 0) {
		int size;

		if (argc < 5) {
			/* LCOV_EXCL_START */
			help();
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		seed = atoi(argv[2]);
		size = atoi(argv[3]);
		b = 4;

		/* sort the file names */
		qsort(&argv[b], argc - b, sizeof(argv[b]), file_cmp);

		for (i = b; i < argc; ++i)
			cmd_truncate(argv[i], rnd(size));
	} else if (strcmp(argv[1], "change") == 0) {
		int size;

		if (argc < 5) {
			/* LCOV_EXCL_START */
			help();
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		seed = atoi(argv[2]);
		size = atoi(argv[3]);
		b = 4;

		/* sort the file names */
		qsort(&argv[b], argc - b, sizeof(argv[b]), file_cmp);

		for (i = b; i < argc; ++i)
			cmd_change(argv[i], rnd(size));
	} else {
		/* LCOV_EXCL_START */
		help();
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	lock_done();

	return 0;
}

