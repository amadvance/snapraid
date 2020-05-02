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

struct snapraid_pool {
	char file[PATH_MAX];
	char linkto[PATH_MAX];
	int64_t mtime_sec;
	int mtime_nsec;

	/* nodes for data structures */
	tommy_hashdyn_node node;
};

struct snapraid_pool* pool_alloc(const char* dir, const char* name, const char* linkto, const struct stat* st)
{
	struct snapraid_pool* pool;

	pool = malloc_nofail(sizeof(struct snapraid_pool));
	pathprint(pool->file, sizeof(pool->file), "%s%s", dir, name);
	pathcpy(pool->linkto, sizeof(pool->linkto), linkto);
	pool->mtime_sec = st->st_mtime;
	pool->mtime_nsec = STAT_NSEC(st);

	return pool;
}

static inline tommy_uint32_t pool_hash(const char* file)
{
	return tommy_hash_u32(0, file, strlen(file));
}

void pool_free(struct snapraid_pool* pool)
{
	free(pool);
}

int pool_compare(const void* void_arg, const void* void_data)
{
	const char* arg = void_arg;
	const struct snapraid_pool* pool = void_data;

	return strcmp(arg, pool->file);
}

/**
 * Remove empty dir.
 * Return == 0 if the directory is empty, and it can be removed
 */
static int clean_dir(const char* dir)
{
	DIR* d;
	int full = 0;

	d = opendir(dir);
	if (!d) {
		/* LCOV_EXCL_START */
		log_fatal("Error opening pool directory '%s'. %s.\n", dir, strerror(errno));
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
			log_fatal("Error reading pool directory '%s'. %s.\n", dir, strerror(errno));
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
				log_fatal("Error in stat file/directory '%s'. %s.\n", path_next, strerror(errno));
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
		}
#else
		/* get lstat info about the file */
		if (lstat(path_next, &st) != 0) {
			/* LCOV_EXCL_START */
			log_fatal("Error in stat file/directory '%s'. %s.\n", path_next, strerror(errno));
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
#endif

		if (S_ISDIR(st.st_mode)) {
			/* recurse */
			pathslash(path_next, sizeof(path_next));
			if (clean_dir(path_next) == 0) {
				int ret;

				/* directory is empty, try to remove it */
				ret = rmdir(path_next);
				if (ret < 0) {
#ifdef _WIN32
					if (errno == EACCES) {
						/* in Windows just ignore EACCES errors removing directories */
						/* because it could happen that the directory is in use */
						/* and it cannot be removed */
						log_fatal("Directory '%s' not removed because it's in use.\n", path_next);
						full = 1;
					} else
#endif
					{
						/* LCOV_EXCL_START */
						log_fatal("Error removing pool directory '%s'. %s.\n", path_next, strerror(errno));
						exit(EXIT_FAILURE);
						/* LCOV_EXCL_STOP */
					}
				}
			} else {
				/* something is present */
				full = 1;
			}
		} else {
			/* something is present */
			full = 1;
		}
	}

	if (closedir(d) != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Error closing pool directory '%s'. %s.\n", dir, strerror(errno));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	return full;
}

/**
 * Read all the links in a directory tree.
 */
static void read_dir(tommy_hashdyn* poolset, const char* base_dir, const char* sub_dir)
{
	char dir[PATH_MAX];
	DIR* d;

	pathprint(dir, sizeof(dir), "%s%s", base_dir, sub_dir);
	d = opendir(dir);
	if (!d) {
		/* LCOV_EXCL_START */
		log_fatal("Error opening pool directory '%s'. %s.\n", dir, strerror(errno));
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
			log_fatal("Error reading pool directory '%s'. %s.\n", dir, strerror(errno));
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
				log_fatal("Error in stat file/directory '%s'. %s.\n", path_next, strerror(errno));
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
		}
#else
		/* get lstat info about the file */
		if (lstat(path_next, &st) != 0) {
			/* LCOV_EXCL_START */
			log_fatal("Error in stat file/directory '%s'. %s.\n", path_next, strerror(errno));
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
#endif

		if (S_ISLNK(st.st_mode)) {
			struct snapraid_pool* pool;
			char linkto[PATH_MAX];
			int ret;

			ret = readlink(path_next, linkto, sizeof(linkto));
			if (ret < 0 || ret >= PATH_MAX) {
				/* LCOV_EXCL_START */
				log_fatal("Error in readlink symlink '%s'. %s.\n", path_next, strerror(errno));
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			linkto[ret] = 0;

			/* store the link info */
			pool = pool_alloc(sub_dir, name, linkto, &st);

			tommy_hashdyn_insert(poolset, &pool->node, pool, pool_hash(pool->file));

		} else if (S_ISDIR(st.st_mode)) {
			pathprint(path_next, sizeof(path_next), "%s%s/", sub_dir, name);

			read_dir(poolset, base_dir, path_next);
		} else {
			msg_verbose("Ignoring pool file '%s'\n", path_next);
		}
	}

	if (closedir(d) != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Error closing pool directory '%s'. %s.\n", dir, strerror(errno));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}
}

/**
 * Remove the link
 */
static void remove_link(void* void_arg, void* void_pool)
{
	char path[PATH_MAX];
	const char* arg = void_arg;
	struct snapraid_pool* pool = void_pool;
	int ret;

	pathprint(path, sizeof(path), "%s%s", arg, pool->file);

	/* delete the link */
	ret = remove(path);
	if (ret < 0) {
		/* LCOV_EXCL_START */
		log_fatal("Error removing symlink '%s'. %s.\n", path, strerror(errno));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}
}

/**
 * Create a link to the specified disk link.
 */
static void make_link(tommy_hashdyn* poolset, const char* pool_dir, const char* share_dir, struct snapraid_disk* disk, const char* sub, int64_t mtime_sec, int mtime_nsec)
{
	char path[PATH_MAX];
	char linkto[PATH_MAX];
	char linkto_exported[PATH_MAX];
	struct snapraid_pool* found;
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

	/* search for the sub path */
	found = tommy_hashdyn_search(poolset, pool_compare, sub, pool_hash(sub));
	if (found) {
		/* remove from the set */
		tommy_hashdyn_remove_existing(poolset, &found->node);

		/* check if the info match */
		if (found->mtime_sec == mtime_sec
			&& found->mtime_nsec == mtime_nsec
			&& strcmp(found->linkto, linkto) == 0
		) {
			/* nothing to do */
			pool_free(found);
			return;
		}

		/* delete the link */
		ret = remove(path);
		if (ret < 0) {
			/* LCOV_EXCL_START */
			log_fatal("Error removing symlink '%s'. %s.\n", path, strerror(errno));
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		pool_free(found);
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
			log_fatal("WARNING! Duplicate pooling for '%s'\n", path);
#ifdef _WIN32
		} else if (errno == EPERM) {
			/* LCOV_EXCL_START */
			log_fatal("You must run as Administrator to be able to create symlinks.\n");
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
#endif
		} else {
			/* LCOV_EXCL_START */
			log_fatal("Error writing symlink '%s'. %s.\n", path, strerror(errno));
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}

	if (mtime_sec) {
		ret = lmtime(path, mtime_sec, mtime_nsec);
		if (ret != 0) {
			/* LCOV_EXCL_START */
			log_fatal("Error setting time to symlink '%s'. %s.\n", path, strerror(errno));
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}
}

void state_pool(struct snapraid_state* state)
{
	tommy_hashdyn poolset;
	tommy_node* i;
	char pool_dir[PATH_MAX];
	char share_dir[PATH_MAX];
	unsigned count;

	tommy_hashdyn_init(&poolset);

	if (state->pool[0] == 0) {
		/* LCOV_EXCL_START */
		log_fatal("To use the 'pool' command you must set the pool directory in the configuration file\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	msg_progress("Reading...\n");

	/* pool directory with final slash */
	pathprint(pool_dir, sizeof(pool_dir), "%s", state->pool);
	pathslash(pool_dir, sizeof(pool_dir));

	/* share directory with final slash */
	pathprint(share_dir, sizeof(share_dir), "%s", state->share);
	pathslash(share_dir, sizeof(share_dir));

	/* first read the previous pool tree */
	read_dir(&poolset, pool_dir, "");

	msg_progress("Writing...\n");

	/* for each disk */
	count = 0;
	for (i = state->disklist; i != 0; i = i->next) {
		tommy_node* j;
		struct snapraid_disk* disk = i->data;

		/* for each file */
		for (j = disk->filelist; j != 0; j = j->next) {
			struct snapraid_file* file = j->data;
			make_link(&poolset, pool_dir, share_dir, disk, file->sub, file->mtime_sec, file->mtime_nsec);
			++count;
		}

		/* for each link */
		for (j = disk->linklist; j != 0; j = j->next) {
			struct snapraid_link* slink = j->data;
			make_link(&poolset, pool_dir, share_dir, disk, slink->sub, 0, 0);
			++count;
		}

		/* we ignore empty dirs in disk->dir */
	}

	msg_progress("Cleaning...\n");

	/* delete all the remaining links */
	tommy_hashdyn_foreach_arg(&poolset, (tommy_foreach_arg_func*)remove_link, pool_dir);

	/* delete empty dirs */
	clean_dir(pool_dir);

	tommy_hashdyn_foreach(&poolset, (tommy_foreach_func*)pool_free);
	tommy_hashdyn_done(&poolset);

	if (count)
		msg_status("%u links\n", count);
	else
		msg_status("No link\n");

	log_tag("summary:link_count::%u\n", count);
	log_tag("summary:exit:ok\n");
	log_flush();
}

