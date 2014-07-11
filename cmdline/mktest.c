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

/**
 * Pseudo random number generator.
 */
unsigned long long seed = 0;

unsigned rnd(unsigned max) 
{
	seed = seed * 6364136223846793005LL + 1442695040888963407LL;

	return (seed >> 32) % max;
}

unsigned rndnotzero(unsigned max)
{
	if (max <= 1)
		return 1;
	else
		return rnd(max - 1) + 1;
}

char CHARSET[] = "qwertyuiopasdfghjklzxcvbnm1234567890 .-+";
#define CHARSET_LEN (sizeof(CHARSET)-1)

void rnd_name(char* file)
{
	int l = 1 + rnd(20);
	while (l) {
		*file++ = CHARSET[rnd(CHARSET_LEN)];
		--l;
	}
	*file = 0;
}

void create_file(const char* path, int size)
{
	FILE* f;
	int count;

	/* remove the existing file if any */
	if (remove(path) != 0) {
		if (errno != ENOENT) {
			/* LCOV_EXCL_START */
			fprintf(stderr, "Error removing file %s\n", path);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	} else {
		/* don't truncate files to 0 size to avoid ZERO file size protection */
		++size;
	}

	f = fopen(path, "wb");
	if (!f) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Error writing file %s\n", path);
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	count = size;
	while (count) {
		/* We don't write zero bytes because we want to test */
		/* the recovering of new files, after an aborted sync */
		/* If the files contains full blocks at zero */
		/* this is an impossible condition to recover */
		/* because we cannot differentiate between an unused block */
		/* and a file filled with 0 */
		fputc(rndnotzero(256), f);
		--count;
	}

	fclose(f);
}

void create_symlink(const char* path, const char* linkto)
{
	/* remove the existing file if any */
	if (remove(path) != 0) {
		if (errno != ENOENT) {
			/* LCOV_EXCL_START */
			fprintf(stderr, "Error removing file %s\n", path);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}

	if (symlink(linkto, path) != 0) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Error writing symlink %s\n", path);
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}
}

void generate(int disk, int size)
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
			fprintf(stderr, "Error creating directory %s\n", path);
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
			|| file[strlen(file)-1] == ' '
			|| file[strlen(file)-1] == '.'
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

		create_symlink(path, linkto);
	} else
#endif
	{
		/* file */
		create_file(path, size);
	}
}

void fallback(int f, struct stat* st)
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
	if (ret != 0) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Error restoring time\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}
}

void writ(const char* path, int size)
{
	struct stat st;

	if (!size)
		return;

	if (lstat(path, &st) != 0) {
		if (errno == ENOENT)
			return; /* it may be already deleted */
		/* LCOV_EXCL_START */
		fprintf(stderr, "Error accessing %s\n", path);
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	if (S_ISREG(st.st_mode)) {
		/* file */
		FILE* f;
		off_t start;
		int count;

		f = fopen(path, "r+b");
		if (!f) {
			/* LCOV_EXCL_START */
			fprintf(stderr, "Error writing %s\n", path);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		/* start at random position inside the file */
		if (st.st_size)
			start = rnd(st.st_size);
		else
			start = 0;

		if (fseek(f, start, SEEK_SET) != 0) {
			/* LCOV_EXCL_START */
			fprintf(stderr, "Error seeking %s\n", path);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		/* write garbage, in case also over the end */
		count = size;
		while (count) {
			fputc(rndnotzero(256), f);
			--count;
		}

		fclose(f);
	}
}

void damage(const char* path, int size)
{
	struct stat st;

	if (!size)
		return;

	if (lstat(path, &st) != 0) {
		if (errno == ENOENT)
			return; /* it may be already deleted */
		/* LCOV_EXCL_START */
		fprintf(stderr, "Error accessing %s\n", path);
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	if (st.st_size == 0)
		return;

	if (S_ISREG(st.st_mode)) {
		/* file */
		FILE* f;
		off_t start;
		unsigned char* data;
		int i;

		f = fopen(path, "r+b");
		if (!f) {
			/* LCOV_EXCL_START */
			fprintf(stderr, "Error writing %s\n", path);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		/* not over the end */
		if (size > st.st_size)
			size = st.st_size;

		/* start at random position inside the file */
		if (size < st.st_size)
			start = rnd(st.st_size - size);
		else
			start = 0;

		data = malloc(size);
		if (!data) {
			/* LCOV_EXCL_START */
			fprintf(stderr, "Low memoru %s\n", path);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		if (fseek(f, start, SEEK_SET) != 0) {
			/* LCOV_EXCL_START */
			fprintf(stderr, "Error seeking %s\n", path);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		if (fread(data, size, 1, f) != 1) {
			/* LCOV_EXCL_START */
			fprintf(stderr, "Error reading %s\n", path);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		/* corrupt ensuring always different data */
		for(i=0;i<size;++i) {
			unsigned char c;

			do {
				c = rndnotzero(256);
			} while (c == data[i]);

			data[i] = c;
		}

		if (fseek(f, start, SEEK_SET) != 0) {
			/* LCOV_EXCL_START */
			fprintf(stderr, "Error seeking %s\n", path);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		if (fwrite(data, size, 1, f) != 1) {
			/* LCOV_EXCL_START */
			fprintf(stderr, "Error writing %s\n", path);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		free(data);

		/* flush changes before restoring the time */
		fflush(f);
		
		/* restore the previous modification time */
		fallback(fileno(f), &st);

		fclose(f);
	}
}

void append(const char* path, int size)
{
	struct stat st;

	if (!size)
		return;

	if (lstat(path, &st) != 0) {
		if (errno == ENOENT)
			return; /* it may be already deleted */
		/* LCOV_EXCL_START */
		fprintf(stderr, "Error accessing %s\n", path);
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	if (S_ISREG(st.st_mode)) {
		FILE* f;
		int count;

		f = fopen(path, "ab");
		if (!f) {
			/* LCOV_EXCL_START */
			fprintf(stderr, "Error appending %s\n", path);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		/* write garbage, in case also over the end */
		count = rnd(size);
		while (count) {
			fputc(rndnotzero(256), f);
			--count;
		}

		fclose(f);
	}
}

void truncat(const char* path, int size)
{
	struct stat st;

	if (!size)
		return;

	if (lstat(path, &st) != 0) {
		if (errno == ENOENT)
			return; /* it may be already deleted */
		/* LCOV_EXCL_START */
		fprintf(stderr, "Error accessing %s\n", path);
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	if (S_ISREG(st.st_mode)) {
		FILE* f;
		off_t start;
		int count;

		f = fopen(path, "r+b");
		if (!f) {
			/* LCOV_EXCL_START */
			fprintf(stderr, "Error writing %s\n", path);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		/* truncate at random position inside the file */
		count = rnd(size);
		if (count > st.st_size)
			count = st.st_size;
		start = st.st_size - count;

		/* don't truncate to 0 to avoid ZERO protection */
		if (start == 0)
			start = 1;
    
		if (ftruncate(fileno(f), start) != 0) {
			/* LCOV_EXCL_START */
			fprintf(stderr, "Error truncating %s\n", path);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		fclose(f);
	}
}

void change(const char* path, int size)
{
	struct stat st;

	if (!size)
		return;

	if (lstat(path, &st) != 0) {
		if (errno == ENOENT)
			return; /* it may be already deleted */
		/* LCOV_EXCL_START */
		fprintf(stderr, "Error accessing %s\n", path);
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	if (S_ISLNK(st.st_mode)) {
		/* symlink */
		if (rnd(2) == 0) {
			/* delete */
			if (remove(path) != 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Error removing %s\n", path);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
		} else {
			/* recreate */
			char linkto[PATH_MAX];

			if (remove(path) != 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Error removing %s\n", path);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			rnd_name(linkto);

			create_symlink(path, linkto);
		}
	} else if (S_ISREG(st.st_mode)) {
		int r;

		r = rnd(4);

		if (r == 0) {
			/* write */
			writ(path, size);
		} else if (r == 1) {
			/* append */
			append(path, size);
		} else if (r == 2) {
			/* truncate */
			truncat(path, size);
		} else {
			/* delete */
			if (remove(path) != 0) {
				/* LCOV_EXCL_START */
				fprintf(stderr, "Error removing %s\n", path);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
		}
	}
}

void help(void)
{
	printf("Test for " PACKAGE " v" VERSION " by Andrea Mazzoleni, " PACKAGE_URL "\n");
	printf("Usage:\n");
	printf("\tmktest generate SEED DISK_NUM FILE_NUM FILE_SIZE\n");
	printf("\tmktest damage SEED FAIL_NUM FAIL_SIZE FILE\n");
	printf("\tmktest write SEED FAIL_NUM FAIL_SIZE FILE\n");
	printf("\tmktest change SEED FAIL_SIZE FILE\n");
	printf("\tmktest append SEED FAIL_SIZE FILE\n");
	printf("\tmktest truncate SEED FAIL_SIZE FILE\n");
}

int qsort_strcmp(const void* a, const void* b)
{
	return strcmp(a, b);
}

int main(int argc, char* argv[])
{
	int i, j, b;

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

		for(i=0;i<disk;++i) {
			for(j=0;j<file;++j) {
				if (j == 0)
					generate(i+1, size);
				else if (j == 1)
					generate(i+1, 0);
				else
					generate(i+1, rnd(size));
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
		qsort(&argv[b], argc - b,  sizeof(argv[b]), qsort_strcmp);

		for(i=b;i<argc;++i) {
			for(j=0;j<fail;++j) {
				writ(argv[i], rndnotzero(size));
			}
		}
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
		qsort(&argv[b], argc - b,  sizeof(argv[b]), qsort_strcmp);

		for(i=b;i<argc;++i) {
			for(j=0;j<fail;++j) {
				damage(argv[i], rndnotzero(size));
			}
		}
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
		qsort(&argv[b], argc - b,  sizeof(argv[b]), qsort_strcmp);

		for(i=b;i<argc;++i) {
			append(argv[i], rndnotzero(size));
		}
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
		qsort(&argv[b], argc - b,  sizeof(argv[b]), qsort_strcmp);

		for(i=b;i<argc;++i) {
			truncat(argv[i], rnd(size));
		}
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
		qsort(&argv[b], argc - b,  sizeof(argv[b]), qsort_strcmp);

		for(i=b;i<argc;++i) {
			change(argv[i], rnd(size));
		}
	} else {
		/* LCOV_EXCL_START */
		help();
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	return 0;
}

