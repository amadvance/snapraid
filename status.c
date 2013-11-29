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
#include "raid.h"

/****************************************************************************/
/* status */

unsigned day_ago(time_t time, time_t now)
{
	return (now - time) / (24 * 3600);
}

#define GRAPH_COLUMN 70
#define GRAPH_ROW 15

int state_status(struct snapraid_state* state)
{
	block_off_t blockmax;
	block_off_t i;
	snapraid_info* infomap;
	time_t now;
	block_off_t bad;
	block_off_t rehash;
	block_off_t count;
	unsigned dayoldest, daymedian, daynewest;
	unsigned bar[GRAPH_COLUMN];
	unsigned barpos;
	unsigned barmax;
	time_t oldest, newest, median;
	unsigned x, y;
	tommy_node* node_disk;
	unsigned file_count;
	unsigned file_fragmented;
	unsigned extra_fragment;
	uint64_t file_size;
	unsigned unsynched_blocks;

	/* get the present time */
	now = time(0);

	blockmax = parity_size(state);

	/* count fragments */
	file_count = 0;
	file_size = 0;
	file_fragmented = 0;
	extra_fragment = 0;
	for(node_disk=state->disklist;node_disk!=0;node_disk=node_disk->next) {
		struct snapraid_disk* disk = node_disk->data;
		tommy_node* node;
		unsigned disk_file_count = 0;
		unsigned disk_file_fragmented = 0;
		unsigned disk_extra_fragment = 0;
		uint64_t disk_file_size = 0;

		/* for each file in the disk */
		node = disk->filelist;
		while (node) {
			struct snapraid_file* file;

			file = node->data;
			node = node->next; /* next node */

			/* check fragmentation */
			if (file->size) {
				block_off_t prev_pos;
				int fragmented;

				fragmented = 0;
				prev_pos = file->blockvec[0].parity_pos;
				for(i=1;i<file->blockmax;++i) {
					block_off_t parity_pos = file->blockvec[i].parity_pos;
					if (prev_pos + 1 != parity_pos) {
						fragmented = 1;
						++extra_fragment;
						++disk_extra_fragment;
					}
					prev_pos = parity_pos;
				}

				if (fragmented) {
					++file_fragmented;
					++disk_file_fragmented;
				}
			}

			/* count files */
			++file_count;
			++disk_file_count;
			file_size += file->size;
			disk_file_size += file->size;
		}

		fprintf(stdlog, "summary:disk_file_count:%s:%u\n", disk->name, disk_file_count);
		fprintf(stdlog, "summary:disk_fragmented_file_count:%s:%u\n", disk->name, disk_file_fragmented);
		fprintf(stdlog, "summary:disk_excess_fragment_count:%s:%u\n", disk->name, disk_extra_fragment);
		fprintf(stdlog, "summary:disk_file_size:%s:%"PRIu64"\n", disk->name, disk_file_size);
	}

	printf("\n");

	printf("Files: %u\n", file_count);
	printf("Fragmented files: %u\n", file_fragmented);
	printf("Excess fragments: %u\n", extra_fragment);
	printf("Files size: %"PRIu64" GiB\n", file_size / (1024*1024*1024) );
	printf("Parity size: %"PRIu64" GiB\n", blockmax * (uint64_t)state->block_size / (1024*1024*1024) );

	printf("\n");

	fprintf(stdlog, "summary:file_count:%u\n", file_count);
	fprintf(stdlog, "summary:fragmented_file_count:%u\n", file_fragmented);
	fprintf(stdlog, "summary:excess_fragment_count:%u\n", extra_fragment);
	fprintf(stdlog, "summary:file_size:%"PRIu64"\n", file_size);
	fprintf(stdlog, "summary:parity_size:%"PRIu64"\n", blockmax * (uint64_t)state->block_size);
	fprintf(stdlog, "summary:hash:%s\n", hash_config_name(state->hash));
	fprintf(stdlog, "summary:prev_hash:%s\n", hash_config_name(state->prevhash));
	fprintf(stdlog, "summary:best_hash:%s\n", hash_config_name(state->besthash));
	fprintf(stdlog, "summary:block_count:%u\n", blockmax);
	fflush(stdlog);

	/* copy the info a temp vector, and count bad/rehash/unsynched blocks */
	infomap = malloc_nofail(blockmax * sizeof(snapraid_info));
	bad = 0;
	count = 0;
	rehash = 0;
	unsynched_blocks = 0;
	for(i=0;i<blockmax;++i) {
		int one_invalid;
		int one_valid;

		snapraid_info info = info_get(&state->infoarr, i);

		/* for each disk */
		one_invalid = 0;
		one_valid = 0;
		for(node_disk=state->disklist;node_disk!=0;node_disk=node_disk->next) {
			struct snapraid_disk* disk = node_disk->data;
			struct snapraid_block* block = disk_block_get(disk, i);

			if (block_has_file(block))
				one_valid = 1;
			if (block_has_invalid_parity(block))
				one_invalid = 1;
		}

		/* if both valid and invalid, we need to update */
		if (one_invalid && one_valid) {
			++unsynched_blocks;
		}

		/* skip unused blocks */
		if (info != 0) {
			if (info_get_bad(info))
				++bad;

			if (info_get_rehash(info))
				++rehash;

			infomap[count++] = info;
		}

		if (state->opt.gui) {
			if (info != 0)
				fprintf(stdlog, "block:%u:%"PRIu64":%s:%s:%s:%s\n", i, (uint64_t)info_get_time(info), one_valid ? "used": "", one_invalid ? "unsynched" : "", info_get_bad(info) ? "bad" : "", info_get_rehash(info) ? "rehash" : "");
			else
				fprintf(stdlog, "block_noinfo:%u:%s:%s\n", i, one_valid ? "used": "", one_invalid ? "unsynched" : "");
		}
	}

	fprintf(stdlog, "summary:has_unsynched:%u\n", unsynched_blocks);
	fprintf(stdlog, "summary:has_rehash:%u\n", rehash);
	fprintf(stdlog, "summary:has_bad:%u\n", bad);
	fprintf(stdlog, "summary:time_count:%u\n", count);
	fflush(stdlog);

	if (!count) {
		fprintf(stderr, "The array is empty.\n");
		free(infomap);
		return 0;
	}

	/* sort the info to get the time info */
	qsort(infomap, count, sizeof(snapraid_info), info_time_compare);

	/* info for making the graph */
	i = 0;
	while (i < count) {
		unsigned j = i + 1;
		while (j < count && info_get_time(infomap[i]) == info_get_time(infomap[j]))
			++j;
		fprintf(stdlog, "time:%"PRIu64":%u\n", (uint64_t)info_get_time(infomap[i]), j - i);
		i = j;
	}
	fflush(stdlog);

	oldest = info_get_time(infomap[0]);
	median = info_get_time(infomap[count / 2]);
	newest = info_get_time(infomap[count - 1]);
	dayoldest = day_ago(oldest, now);
	daymedian = day_ago(median, now);
	daynewest = day_ago(newest, now);

	/* compute graph limits */
	barpos = 0;
	barmax = 0;
	for(i=0;i<GRAPH_COLUMN;++i) {
		time_t limit;
		unsigned step;

		limit = oldest + (newest - oldest) * (i+1) / GRAPH_COLUMN;

		step = 0;
		while (barpos < count && info_get_time(infomap[barpos]) <= limit) {
			++barpos;
			++step;
		}

		if (step > barmax)
			barmax = step;

		bar[i] = step;
	}

	/* print the graph */
	for(y=0;y<GRAPH_ROW;++y) {
		if (y == 0)
			printf("%3u%% ", barmax * 100 / count);
		else if (y == GRAPH_ROW - 1)
			printf("  0%% ");
		else if (y == GRAPH_ROW/2)
			printf("%3u%% ", barmax * 50 / count);
		else
			printf("     ");
		for(x=0;x<GRAPH_COLUMN;++x) {
			unsigned pivot = barmax * (GRAPH_ROW-y) / GRAPH_ROW;
			/* if it's the baseline */
			if (y == GRAPH_ROW-1) {
				if (bar[x] >= pivot) {
					printf("*");
				} else if (bar[x] > 0) {
					printf("+");
				} else {
					printf("_");
				}
			} else {
				unsigned halfpivot = pivot - barmax / GRAPH_ROW / 2;
				if (bar[x] >= pivot) {
					printf("*");
				} else if (bar[x] >= halfpivot) {
					printf("+");
				} else {
					printf(" ");
				}
			}
		}
		printf("\n");
	}
	printf("   %3u                    days ago of the last check                    %3u\n", dayoldest, daynewest);

	printf("\n");

	printf("The oldest block was checked %u days ago, the median %u, the newest %u.\n", dayoldest, daymedian, daynewest);

	printf("\n");

	if (unsynched_blocks) {
		printf("WARNING! The array is NOT fully synched.\n");
		printf("You have a sync in progress at %u%%.\n", (blockmax - unsynched_blocks) * 100 / blockmax);
	} else {
		printf("No sync is in progress.\n");
	}

	if (rehash) {
		printf("You have a rehash in progress at %u%%.\n", (count - rehash) * 100 / count);
	} else {
		if (state->besthash != state->hash) {
			printf("No rehash is in progress, but for optimal performance one is recommended.\n");
		} else {
			printf("No rehash is in progress or needed.\n");
		}
	}

	if (bad) {
		printf("DANGER! In the array there are %u silent errors!\n\n", bad);

		printf("They are at blocks:");

		/* print all the errors */
		for(i=0;i<blockmax;++i) {
			snapraid_info info = info_get(&state->infoarr, i);

			/* skip unused blocks */
			if (info == 0)
				continue;
			
			if (info_get_bad(info))
				printf(" %u", i);
		}

		printf("\n\n");

		printf("To fix them use the command 'snapraid -e fix'.\n");
		printf("The errors will disapper from the 'status' at the next 'scrub' command.\n");
	} else {
		printf("No silent error detected.\n");
	}

	/* free the temp vector */
	free(infomap);

	return 0;
}

