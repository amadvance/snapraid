/*
 * Copyright (C) 2013 Andrea Mazzoleni
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

#include "util.h"
#include "elem.h"
#include "state.h"

/**
 * Clean a directory tree removing all the symlinks and empty directories.
 * Return == 0 if the directory is empty, and it can be removed
 */
static int clean_dir(struct snapraid_state* state, const char* dir)
{
	DIR* d;
	int ignored = 0;

	d = opendir(dir);
	if (!d) {
		fprintf(stderr, "Error opening pool directory '%s'. %s.\n", dir, strerror(errno));
		exit(EXIT_FAILURE);
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
			fprintf(stderr, "Error reading pool directory '%s'. %s.\n", dir, strerror(errno));
			exit(EXIT_FAILURE);
		}
		if (dd == 0 && errno == 0) {
			break; /* finished */
		}

		/* skip "." and ".." files */
		name = dd->d_name;
		if (name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0)))
			continue;

		pathprint(path_next, sizeof(path_next), "%s%s", dir, name);

#if HAVE_DIRENT_LSTAT
		/* convert dirent to lstat result */
		dirent_lstat(dd, &st);
#else
		/* get lstat info about the file */
		if (lstat(path_next, &st) != 0) {
			fprintf(stderr, "Error in stat file/directory '%s'. %s.\n", path_next, strerror(errno));
			exit(EXIT_FAILURE);
		}
#endif

		if (S_ISLNK(st.st_mode)) {
			int ret;

			/* delete the link */
			ret = remove(path_next);
			if (ret < 0) {
				fprintf(stderr, "Error removing symlink '%s'. %s.\n", path_next, strerror(errno));
				exit(EXIT_FAILURE);
			}
		} else if (S_ISDIR(st.st_mode)) {
			/* recurse */
			pathslash(path_next, sizeof(path_next));
			if (clean_dir(state, path_next) == 0) {
				int ret;

				/* directory is empty, try to remove it */
				ret = rmdir(path_next);
				if (ret < 0) {
#ifdef _WIN32
					if (errno == EACCES) {
						/* in Windows just ignore EACCES errors removing directories */
						/* because it could happen that the directory is in use */
						/* and it cannot be removed */
						fprintf(stderr, "Directory '%s' not removed because it's in use.\n", path_next);
						ignored = 1;
					} else
#endif
					{
						fprintf(stderr, "Error removing pool directory '%s'. %s.\n", path_next, strerror(errno));
						exit(EXIT_FAILURE);
					}
				}
			} else {
				/* something was ignored inside the subdir */
				ignored = 1;
			}
		} else {
			ignored = 1;
			if (state->verbose) {
				printf("Ignoring pool file '%s'\n", path_next);
			}
		}
	}

	if (closedir(d) != 0) {
		fprintf(stderr, "Error closing pool directory '%s'. %s.\n", dir, strerror(errno));
		exit(EXIT_FAILURE);
	}

	return ignored;
}

/**
 * Creates a link to the specified disk entry.
 */
static void make_link(const char* pool_dir, struct snapraid_disk* disk, const char* sub)
{
	char path[PATH_MAX];
	char linkto[PATH_MAX];
	int ret;

	/* make the paths */
	pathprint(path, sizeof(path), "%s%s", pool_dir, sub);
	pathprint(linkto, sizeof(linkto), "%s%s", disk->dir, sub);

	/* create the ancestor directories */
	ret = mkancestor(path);
	if (ret != 0) {
		exit(EXIT_FAILURE);
	}

	/* create the symlink */
	ret = symlink(linkto, path);
	if (ret != 0) {
		if (errno == EEXIST) {
			fprintf(stderr, "warning: Duplicate pooling for '%s'\n", path);
#ifdef _WIN32
		} else if (errno == EPERM) {
			fprintf(stderr, "You must run as Adminstrator to be able to create symlinks.\n");
			exit(EXIT_FAILURE);
#endif
		} else {
			fprintf(stderr, "Error writing symlink '%s'. %s.\n", path, strerror(errno));
			exit(EXIT_FAILURE);
		}
	}
}

void state_pool(struct snapraid_state* state)
{
	tommy_node* i;
	char pool_dir[PATH_MAX];
	unsigned count;

	if (state->pool[0] == 0) {
		fprintf(stderr, "To use the 'pool' command you must set the pool directory in the configuration file\n");
		exit(EXIT_FAILURE);
	}

	printf("Cleaning...\n");

	/* pool directory with final slash */
	pathprint(pool_dir, sizeof(pool_dir), "%s", state->pool);
	pathslash(pool_dir, sizeof(pool_dir));

	/* first clear the previous pool tree */
	clean_dir(state, pool_dir);

	printf("Pooling...\n");

	/* for each disk */
	count = 0;
	for(i=state->disklist;i!=0;i=i->next) {
		tommy_node* j;
		struct snapraid_disk* disk = i->data;

		/* for each file */
		for(j=disk->filelist;j!=0;j=j->next) {
			struct snapraid_file* file = j->data;
			make_link(pool_dir, disk, file->sub);
			++count;
		}

		/* for each link */
		for(j=disk->linklist;j!=0;j=j->next) {
			struct snapraid_link* link = j->data;
			make_link(pool_dir, disk, link->sub);
			++count;
		}

		/* we ignore empty dirs in disk->dir */
	}

	if (!state->gui) {
		if (count)
			printf("%u links created\n", count);
		else
			printf("No link created\n");
	}
}

