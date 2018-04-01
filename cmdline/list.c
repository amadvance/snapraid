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
#include "parity.h"
#include "handle.h"

/****************************************************************************/
/* list */

void state_list(struct snapraid_state* state)
{
	tommy_node* i;
	unsigned file_count;
	data_off_t file_size;
	unsigned link_count;
	char esc_buffer[ESC_MAX];
	char esc_buffer_alt[ESC_MAX];

	file_count = 0;
	file_size = 0;
	link_count = 0;

	msg_progress("Listing...\n");

	/* for each disk */
	for (i = state->disklist; i != 0; i = i->next) {
		tommy_node* j;
		struct snapraid_disk* disk = i->data;

		/* sort by name */
		tommy_list_sort(&disk->filelist, file_path_compare);

		/* for each file */
		for (j = disk->filelist; j != 0; j = j->next) {
			struct snapraid_file* file = j->data;
#if HAVE_LOCALTIME_R
			struct tm tm_res;
#endif
			struct tm* tm;
			time_t t;

			++file_count;
			file_size += file->size;

			log_tag("file:%s:%s:%" PRIu64 ":%" PRIi64 ":%u:%" PRIi64 "\n", disk->name, esc_tag(file->sub, esc_buffer), file->size, file->mtime_sec, file->mtime_nsec, file->inode);

			t = file->mtime_sec;
#if HAVE_LOCALTIME_R
			tm = localtime_r(&t, &tm_res);
#else
			tm = localtime(&t);
#endif

			printf("%12" PRIu64 " ", file->size);
			if (tm) {
				printf("%04u/%02u/%02u %02u:%02u", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min);
				if (msg_level >= MSG_VERBOSE)
					printf(":%02u.%09u", tm->tm_sec, file->mtime_nsec);
				printf(" ");
			}
			printf("%s\n", fmt_term(disk, file->sub, esc_buffer));
		}

		/* sort by name */
		tommy_list_sort(&disk->linklist, link_alpha_compare);

		/* for each link */
		for (j = disk->linklist; j != 0; j = j->next) {
			struct snapraid_link* slink = j->data;
			const char* type;

			switch (slink->flag & FILE_IS_LINK_MASK) {
			case FILE_IS_HARDLINK : type = "hardlink"; break;
			case FILE_IS_SYMLINK : type = "symlink"; break;
			case FILE_IS_SYMDIR : type = "symdir"; break;
			case FILE_IS_JUNCTION : type = "junction"; break;
			/* LCOV_EXCL_START */
			default : type = "unknown"; break;
				/* LCOV_EXCL_STOP */
			}

			++link_count;

			log_tag("link_%s:%s:%s:%s\n", type, disk->name, esc_tag(slink->sub, esc_buffer), esc_tag(slink->linkto, esc_buffer_alt));

			printf("%12s ", type);
			printf("                 ");
			if (msg_level >= MSG_VERBOSE)
				printf("             ");
			printf("%s -> %s\n", fmt_term(disk, slink->sub, esc_buffer), fmt_term(disk, slink->linkto, esc_buffer_alt));
		}
	}

	msg_status("\n");
	msg_status("%8u files, for %" PRIu64 " GB\n", file_count, file_size / GIGA);
	msg_status("%8u links\n", link_count);

	log_tag("summary:file_count:%u\n", file_count);
	log_tag("summary:file_size:%" PRIu64 "\n", file_size);
	log_tag("summary:link_count:%u\n", link_count);
	log_tag("summary:exit:ok\n");
	log_flush();
}

