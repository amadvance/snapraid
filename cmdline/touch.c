/*
 * Copyright (C) 2014 Andrea Mazzoleni
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
#include "handle.h"

void state_touch(struct snapraid_state* state)
{
	tommy_node* i;
	char esc_buffer[ESC_MAX];

	msg_progress("Setting sub-second timestamps...\n");

	/* for all disks */
	for (i = state->disklist; i != 0; i = i->next) {
		struct snapraid_disk* disk = i->data;
		tommy_node* j;

		/* for all files */
		for (j = disk->filelist; j != 0; j = j->next) {
			struct snapraid_file* file = j->data;

			/* if the file has a zero nanosecond timestamp */
			/* note that symbolic links are not in the file list */
			/* and then are not processed */
			if (file->mtime_nsec == 0) {
				char path[PATH_MAX];
				struct stat st;
				int f;
				int ret;
				int nsec;
				int flags;

				pathprint(path, sizeof(path), "%s%s", disk->dir, file->sub);

				/* set a new nanosecond timestamp different than 0 */
				do {
					uint32_t nano;

					/* get a random nanosecond value */
					if (randomize(&nano, sizeof(nano)) != 0) {
						/* LCOV_EXCL_START */
						log_fatal("Failed to get random values.\n");
						exit(EXIT_FAILURE);
						/* LCOV_EXCL_STOP */
					}

					nsec = nano % 1000000000;
				} while (nsec == 0);

				/* O_BINARY: open as binary file (Windows only) */
				/* O_NOFOLLOW: do not follow links to ensure to open the real file */
				flags = O_BINARY | O_NOFOLLOW;
#ifdef _WIN32
				/* in Windows we must have write access at the file */
				flags |= O_RDWR;
#else
				/* in all others platforms, read access is enough */
				flags |= O_RDONLY;
#endif

				/* open it */
				f = open(path, flags);
				if (f == -1) {
					/* LCOV_EXCL_START */
					log_fatal("Error opening file '%s'. %s.\n", path, strerror(errno));
					continue;
					/* LCOV_EXCL_STOP */
				}

				/* get the present timestamp, that may be different than the one */
				/* in the content file */
				ret = fstat(f, &st);
				if (ret == -1) {
					/* LCOV_EXCL_START */
					close(f);
					log_fatal("Error accessing file '%s'. %s.\n", path, strerror(errno));
					continue;
					/* LCOV_EXCL_STOP */
				}

				/* set the tweaked modification time, with new nano seconds */
				ret = fmtime(f, st.st_mtime, nsec);
				if (ret != 0) {
					/* LCOV_EXCL_START */
					close(f);
					log_fatal("Error timing file '%s'. %s.\n", path, strerror(errno));
					continue;
					/* LCOV_EXCL_STOP */
				}

				/* uses fstat again to get the present timestamp */
				/* this is needed because the value read */
				/* may be different than the written one */
				ret = fstat(f, &st);
				if (ret == -1) {
					/* LCOV_EXCL_START */
					close(f);
					log_fatal("Error accessing file '%s'. %s.\n", path, strerror(errno));
					continue;
					/* LCOV_EXCL_STOP */
				}

				/* close it */
				ret = close(f);
				if (ret != 0) {
					/* LCOV_EXCL_START */
					log_fatal("Error closing file '%s'. %s.\n", path, strerror(errno));
					continue;
					/* LCOV_EXCL_STOP */
				}

				/* set the same nanosecond value in the content file */
				/* note that if the seconds value is already matching */
				/* the file won't be synced because the content file will */
				/* contain the new updated timestamp */
				file->mtime_nsec = STAT_NSEC(&st);

				/* state changed, we need to update it */
				state->need_write = 1;

				log_tag("touch:%s:%s: %" PRIu64 ".%d\n", disk->name, esc_tag(file->sub, esc_buffer), (uint64_t)st.st_mtime, STAT_NSEC(&st));
				msg_info("touch %s\n", fmt_term(disk, file->sub, esc_buffer));
			}
		}
	}
}

