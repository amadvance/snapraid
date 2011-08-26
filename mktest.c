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

void generate(int disk, int size)
{
	char path[PATH_MAX];
	char* file;

	snprintf(path, sizeof(path), "test/disk%d/", disk);
	file = path + strlen(path);

	/* add a directory */
	if (rnd(2) == 0) {
		*file++ = '_';
		*file++ = 'a' + rnd(2);
		*file = 0;

		/* create it */
		if (mkdir(path, 0777) != 0) {
			if (errno != EEXIST) {
				fprintf(stderr, "Error creating directory %s\n", path);
				exit(EXIT_FAILURE);
			}
		}

		*file++ = '/';
	}

	/* add another directory */
	if (rnd(2) == 0) {
		*file++ = '_';
		*file++ = 'a' + rnd(2);
		*file = 0;

		/* create it */
		if (mkdir(path, 0777) != 0) {
			if (errno != EEXIST) {
				fprintf(stderr, "Error creating directory %s\n", path);
				exit(EXIT_FAILURE);
			}
		}

		*file++ = '/';
	}

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

	/* remove the existing file if any */
	if (remove(path) != 0) {
		if (errno != ENOENT) {
			fprintf(stderr, "Error removing file %s\n", path);
			exit(EXIT_FAILURE);
		}
	}

#ifndef WIN32 /* Windows XP doesn't support symlinks */
	if (rnd(32) == 0) {
		/* symlink */
		char linkto[PATH_MAX];
		int ret;

		rnd_name(linkto);

		ret = symlink(linkto, path);
		if (ret != 0) {
			fprintf(stderr, "Error writing symlink %s\n", path);
			exit(EXIT_FAILURE);
		}
	} else
#endif
	{
		/* file */
		FILE* f;
		int count;
	
		f = fopen(path, "wb");
		if (!f) {
			fprintf(stderr, "Error writing file %s\n", path);
			exit(EXIT_FAILURE);
		}

		count = size;
		while (count) {
			fputc(rnd(256), f);
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
		fprintf(stderr, "Error accessing %s\n", path);
		exit(EXIT_FAILURE);
	}

	if (S_ISLNK(st.st_mode)) {
		/* symlink */
		int ret;
		
		ret = remove(path);
		if (ret != 0) {
			fprintf(stderr, "Error removing %s\n", path);
			exit(EXIT_FAILURE);
		}
	} else {
		/* file */
		FILE* f;
		off_t start;
		int count;

		f = fopen(path, "r+b");
		if (!f) {
			fprintf(stderr, "Error writing %s\n", path);
			exit(EXIT_FAILURE);
		}

		/* start at random position inside the file */
		if (st.st_size)
			start = rnd(st.st_size);
		else
			start = 0;
		if (fseek(f, start, SEEK_SET) != 0) {
			fprintf(stderr, "Error seeking %s\n", path);
			exit(EXIT_FAILURE);
		}

		/* write garbage, in case also over the end */
		count = rnd(size);
		while (count) {
			fputc(rnd(256), f);
			--count;
		}

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
		fprintf(stderr, "Error accessing %s\n", path);
		exit(EXIT_FAILURE);
	}

	if (S_ISLNK(st.st_mode)) {
		/* symlink */
		int ret;
		
		ret = remove(path);
		if (ret != 0) {
			fprintf(stderr, "Error removing %s\n", path);
			exit(EXIT_FAILURE);
		}
	} else {
		/* file */

		FILE* f;
		int count;

		f = fopen(path, "ab");
		if (!f) {
			fprintf(stderr, "Error appending %s\n", path);
			exit(EXIT_FAILURE);
		}

		/* write garbage, in case also over the end */
		count = rnd(size);
		while (count) {
			fputc(rnd(256), f);
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
		fprintf(stderr, "Error accessing %s\n", path);
		exit(EXIT_FAILURE);
	}

	if (S_ISLNK(st.st_mode)) {
		/* symlink */
		int ret;
		
		ret = remove(path);
		if (ret != 0) {
			fprintf(stderr, "Error removing %s\n", path);
			exit(EXIT_FAILURE);
		}
	} else {
		/* file */
		FILE* f;
		off_t start;
		int count;

		f = fopen(path, "r+b");
		if (!f) {
			fprintf(stderr, "Error writing %s\n", path);
			exit(EXIT_FAILURE);
		}

		/* truncate at random position inside the file */
		count = rnd(size);
		if (count > st.st_size)
			count = st.st_size;
		start = st.st_size - count;
    
		if (ftruncate(fileno(f), start) != 0) {
			fprintf(stderr, "Error truncating %s\n", path);
			exit(EXIT_FAILURE);
		}

		fclose(f);
	}
}


void help(void)
{
	printf("Test for " PACKAGE " v" VERSION " by Andrea Mazzoleni, " PACKAGE_URL "\n");
	printf("Usage:\n");
	printf("\tmktest generate SEED DISK_NUM FILE_NUM FILE_SIZE\n");
	printf("\tmktest damage SEED FAIL_NUM FAIL_SIZE FILE\n");
	printf("\tmktest append SEED FAIL_SIZE FILE\n");
	printf("\tmktest truncate SEED FAIL_SIZE FILE\n");
}

int main(int argc, char* argv[])
{
	int i, j;

	if (argc < 2) {
		help();
		exit(EXIT_FAILURE);
	}

	if (strcmp(argv[1], "generate") == 0) {
		int disk, file, size;

		if (argc != 6) {
			help();
			exit(EXIT_FAILURE);
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
	} else if (strcmp(argv[1], "damage") == 0) {
		int fail, size;

		if (argc < 6) {
			help();
			exit(EXIT_FAILURE);
		}

		seed = atoi(argv[2]);
		fail = atoi(argv[3]);
		size = atoi(argv[4]);

		for(i=5;i<argc;++i) {
			for(j=0;j<fail;++j) {
				damage(argv[i], rnd(size));
			}
		}
	} else if (strcmp(argv[1], "append") == 0) {
		int size;

		if (argc < 5) {
			help();
			exit(EXIT_FAILURE);
		}

		seed = atoi(argv[2]);
		size = atoi(argv[3]);

		for(i=4;i<argc;++i) {
			append(argv[i], rnd(size));
		}
	} else if (strcmp(argv[1], "truncate") == 0) {
		int size;

		if (argc < 5) {
			help();
			exit(EXIT_FAILURE);
		}

		seed = atoi(argv[2]);
		size = atoi(argv[3]);

		for(i=4;i<argc;++i) {
			truncat(argv[i], rnd(size));
		}
	} else {
		help();
		exit(EXIT_FAILURE);
	}

	return 0;
}

