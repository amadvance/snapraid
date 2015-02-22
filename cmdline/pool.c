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

#include "support.h"
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
		/* LCOV_EXCL_START */
		msg_error("Error opening pool directory '%s'. %s.\n", dir, strerror(errno));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
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
			/* LCOV_EXCL_START */
			msg_error("Error reading pool directory '%s'. %s.\n", dir, strerror(errno));
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
		if (dd == 0 && errno == 0) {
			break; /* finished */
		}

		/* skip "." and ".." files */
		name = dd->d_name;
		if (name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0)))
			continue;

		pathprint(path_next, sizeof(path_next), "%s%s", dir, name);

#if HAVE_STRUCT_DIRENT_D_STAT
		/* convert dirent to lstat result */
		dirent_lstat(dd, &st);

		/* if the st_mode field is missing, takes care to fill it using normal lstat() */
		/* at now this can happen only in Windows */
		if (st.st_mode == 0) {
			if (lstat(path_next, &st) != 0) {
				/* LCOV_EXCL_START */
				msg_error("Error in stat file/directory '%s'. %s.\n", path_next, strerror(errno));
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
		}
#else
		/* get lstat info about the file */
		if (lstat(path_next, &st) != 0) {
			/* LCOV_EXCL_START */
			msg_error("Error in stat file/directory '%s'. %s.\n", path_next, strerror(errno));
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
#endif

		if (S_ISLNK(st.st_mode)) {
			int ret;

			/* delete the link */
			ret = remove(path_next);
			if (ret < 0) {
				/* LCOV_EXCL_START */
				msg_error("Error removing symlink '%s'. %s.\n", path_next, strerror(errno));
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
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
						msg_error("Directory '%s' not removed because it's in use.\n", path_next);
						ignored = 1;
					} else
#endif
					{
						/* LCOV_EXCL_START */
						msg_error("Error removing pool directory '%s'. %s.\n", path_next, strerror(errno));
						exit(EXIT_FAILURE);
						/* LCOV_EXCL_STOP */
					}
				}
			} else {
				/* something was ignored inside the subdir */
				ignored = 1;
			}
		} else {
			ignored = 1;
			msg_verbose("Ignoring pool file '%s'\n", path_next);
		}
	}

	if (closedir(d) != 0) {
		/* LCOV_EXCL_START */
		msg_error("Error closing pool directory '%s'. %s.\n", dir, strerror(errno));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	return ignored;
}

/**
 * Creates a link to the specified disk entry.
 */
static void make_link(const char* pool_dir, const char* share_dir, struct snapraid_disk* disk, const char* sub)
{
	char path[PATH_MAX];
	char linkto[PATH_MAX];
	char linkto_exported[PATH_MAX];
	int ret;

	/* make the source path */
	pathprint(path, sizeof(path), "%s%s", pool_dir, sub);

	/* make the linkto path */
	if (share_dir[0] != 0) {
		/* with a shared directory, use it */
		pathprint(linkto, sizeof(linkto), "%s%s/%s", share_dir, disk->name, sub);
	} else {
		/* without a share directory, use the local disk paths */
		pathprint(linkto, sizeof(linkto), "%s%s", disk->dir, sub);
	}

	/* create the ancestor directories */
	ret = mkancestor(path);
	if (ret != 0) {
		/* LCOV_EXCL_START */
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* convert back slashes */
	pathexport(linkto_exported, sizeof(linkto_exported), linkto);

	/* create the symlink */
	ret = symlink(linkto_exported, path);
	if (ret != 0) {
		if (errno == EEXIST) {
			msg_warning("WARNING! Duplicate pooling for '%s'\n", path);
#ifdef _WIN32
		} else if (errno == EPERM) {
			/* LCOV_EXCL_START */
			msg_error("You must run as Adminstrator to be able to create symlinks.\n");
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
#endif
		} else {
			/* LCOV_EXCL_START */
			msg_error("Error writing symlink '%s'. %s.\n", path, strerror(errno));
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}
}

void state_pool(struct snapraid_state* state)
{
	tommy_node* i;
	char pool_dir[PATH_MAX];
	char share_dir[PATH_MAX];
	unsigned count;

	if (state->pool[0] == 0) {
		/* LCOV_EXCL_START */
		msg_error("To use the 'pool' command you must set the pool directory in the configuration file\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	msg_progress("Cleaning...\n");

	/* pool directory with final slash */
	pathprint(pool_dir, sizeof(pool_dir), "%s", state->pool);
	pathslash(pool_dir, sizeof(pool_dir));

	/* share directory with final slash */
	pathprint(share_dir, sizeof(share_dir), "%s", state->share);
	pathslash(share_dir, sizeof(share_dir));

	/* first clear the previous pool tree */
	clean_dir(state, pool_dir);

	msg_progress("Pooling...\n");

	/* for each disk */
	count = 0;
	for (i = state->disklist; i != 0; i = i->next) {
		tommy_node* j;
		struct snapraid_disk* disk = i->data;

		/* for each file */
		for (j = disk->filelist; j != 0; j = j->next) {
			struct snapraid_file* file = j->data;
			make_link(pool_dir, share_dir, disk, file->sub);
			++count;
		}

		/* for each link */
		for (j = disk->linklist; j != 0; j = j->next) {
			struct snapraid_link* link = j->data;
			make_link(pool_dir, share_dir, disk, link->sub);
			++count;
		}

		/* we ignore empty dirs in disk->dir */
	}

	if (count)
		msg_status("%u links created\n", count);
	else
		msg_status("No link created\n");

	msg_tag("summary:link_count::%u\n", count);
	msg_tag("summary:exit:ok\n");
	msg_flush();
}

