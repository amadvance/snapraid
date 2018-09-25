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
#include "parity.h"
#include "handle.h"
#include "raid/raid.h"

/****************************************************************************/
/* status */

unsigned day_ago(time_t ref, time_t now)
{
	/* in case some dates is in the future */
	if (now < ref)
		return 0;

	return (now - ref) / (24 * 3600);
}

#define GRAPH_COLUMN 70
#define GRAPH_ROW 15

static unsigned perc(uint64_t part, uint64_t total)
{
	if (!total)
		return 0;

	return (unsigned)(part * 100 / total);
}

/**
 * Bit used to mark unscrubbed time info.
 */
#define TIME_NEW 1

int state_status(struct snapraid_state* state)
{
	block_off_t blockmax;
	block_off_t i;
	time_t* timemap;
	time_t now;
	block_off_t bad;
	block_off_t bad_first;
	block_off_t bad_last;
	block_off_t rehash;
	block_off_t count;
	unsigned l;
	unsigned dayoldest, daymedian, daynewest;
	unsigned bar_scrubbed[GRAPH_COLUMN];
	unsigned bar_new[GRAPH_COLUMN];
	unsigned barpos;
	unsigned barmax;
	time_t oldest, newest, median;
	unsigned x, y;
	tommy_node* node_disk;
	unsigned file_count;
	unsigned file_fragmented;
	unsigned extra_fragment;
	unsigned file_zerosubsecond;
	uint64_t file_size;
	uint64_t file_block_count;
	uint64_t file_block_free;
	block_off_t parity_block_free;
	unsigned unsynced_blocks;
	unsigned unscrubbed_blocks;
	uint64_t all_wasted;
	int free_not_zero;

	/* get the present time */
	now = time(0);

	/* keep track if at least a free info is available */
	free_not_zero = 0;

	blockmax = parity_allocated_size(state);

	log_tag("summary:block_size:%u\n", state->block_size);
	log_tag("summary:parity_block_count:%u\n", blockmax);

	/* get the minimum parity free space */
	parity_block_free = state->parity[0].free_blocks;
	for (l = 0; l < state->level; ++l) {
		log_tag("summary:parity_block_total:%s:%u\n", lev_config_name(l), state->parity[l].total_blocks);
		log_tag("summary:parity_block_free:%s:%u\n", lev_config_name(l), state->parity[l].free_blocks);
		if (state->parity[l].free_blocks < parity_block_free)
			parity_block_free = state->parity[l].free_blocks;
		if (state->parity[l].free_blocks != 0)
			free_not_zero = 1;
	}
	log_tag("summary:parity_block_free_min:%u\n", parity_block_free);

	printf("SnapRAID status report:\n");
	printf("\n");
	printf("   Files Fragmented Excess  Wasted  Used    Free  Use Name\n");
	printf("            Files  Fragments  GB      GB      GB\n");

	/* count fragments */
	file_count = 0;
	file_size = 0;
	file_block_count = 0;
	file_block_free = 0;
	file_fragmented = 0;
	extra_fragment = 0;
	file_zerosubsecond = 0;
	all_wasted = 0;
	for (node_disk = state->disklist; node_disk != 0; node_disk = node_disk->next) {
		struct snapraid_disk* disk = node_disk->data;
		tommy_node* node;
		block_off_t j;
		unsigned disk_file_count = 0;
		unsigned disk_file_fragmented = 0;
		unsigned disk_extra_fragment = 0;
		unsigned disk_file_zerosubsecond = 0;
		block_off_t disk_block_count = 0;
		uint64_t disk_file_size = 0;
		block_off_t disk_block_latest_used = 0;
		block_off_t disk_block_max_by_space;
		block_off_t disk_block_max_by_parity;
		block_off_t disk_block_max;
		int64_t wasted;

		/* for each file in the disk */
		node = disk->filelist;
		while (node) {
			struct snapraid_file* file;

			file = node->data;
			node = node->next; /* next node */

			if (file->mtime_nsec == STAT_NSEC_INVALID
				|| file->mtime_nsec == 0
			) {
				++file_zerosubsecond;
				++disk_file_zerosubsecond;
				if (disk_file_zerosubsecond < 50)
					log_tag("zerosubsecond:%s:%s: \n", disk->name, file->sub);
				if (disk_file_zerosubsecond == 50)
					log_tag("zerosubsecond:%s:%s: (more follow)\n", disk->name, file->sub);
			}

			/* check fragmentation */
			if (file->blockmax != 0) {
				block_off_t prev_pos;
				block_off_t last_pos;
				int fragmented;

				fragmented = 0;
				prev_pos = fs_file2par_get(disk, file, 0);
				for (j = 1; j < file->blockmax; ++j) {
					block_off_t parity_pos = fs_file2par_get(disk, file, j);
					if (prev_pos + 1 != parity_pos) {
						fragmented = 1;
						++extra_fragment;
						++disk_extra_fragment;
					}
					prev_pos = parity_pos;
				}

				/* keep track of latest block used */
				last_pos = fs_file2par_get(disk, file, file->blockmax - 1);
				if (last_pos > disk_block_latest_used) {
					disk_block_latest_used = last_pos;
				}

				if (fragmented) {
					++file_fragmented;
					++disk_file_fragmented;
				}

				disk_block_count += file->blockmax;
			}

			/* count files */
			++file_count;
			++disk_file_count;
			file_size += file->size;
			file_block_count += file->blockmax;
			disk_file_size += file->size;
		}

		if (disk->free_blocks != 0)
			free_not_zero = 1;

		/* get the free block info */
		disk_block_max_by_space = disk_block_count + disk->free_blocks;
		disk_block_max_by_parity = blockmax + parity_block_free;

		/* the maximum usable space in a disk is limited by the smallest */
		/* of the disk size and the parity size */
		/* the wasted space is the space that we have to leave */
		/* free on the data disk, when the parity is filled up */
		if (disk_block_max_by_space < disk_block_max_by_parity) {
			disk_block_max = disk_block_max_by_space;
		} else {
			disk_block_max = disk_block_max_by_parity;
		}

		/* wasted space is the difference of the two maximum size */
		/* if negative, it's extra space available in parity */
		wasted = (int64_t)disk_block_max_by_space - (int64_t)disk_block_max_by_parity;
		wasted *= state->block_size;

		if (wasted > 0)
			all_wasted += wasted;
		file_block_free += disk_block_max - disk_block_count;

		printf("%8u", disk_file_count);
		printf("%8u", disk_file_fragmented);
		printf("%8u", disk_extra_fragment);
		if (wasted < -100LL * GIGA) {
			printf("       -");
		} else {
			printf("%8.1f", (double)wasted / GIGA);
		}
		printf("%8" PRIu64, disk_file_size / GIGA);

		if (disk_block_max == 0 && disk_block_count == 0) {
			/* if the disk is empty and we don't have the free space info */
			printf("       -");
			printf("   - ");
		} else {
			printf("%8" PRIu64, (disk_block_max - disk_block_count) * (uint64_t)state->block_size / GIGA);
			printf(" %3u%%", perc(disk_block_count, disk_block_max));
		}
		printf(" %s\n", disk->name);

		log_tag("summary:disk_file_count:%s:%u\n", disk->name, disk_file_count);
		log_tag("summary:disk_block_count:%s:%u\n", disk->name, disk_block_count);
		log_tag("summary:disk_fragmented_file_count:%s:%u\n", disk->name, disk_file_fragmented);
		log_tag("summary:disk_excess_fragment_count:%s:%u\n", disk->name, disk_extra_fragment);
		log_tag("summary:disk_zerosubsecond_file_count:%s:%u\n", disk->name, disk_file_zerosubsecond);
		log_tag("summary:disk_file_size:%s:%" PRIu64 "\n", disk->name, disk_file_size);
		log_tag("summary:disk_block_allocated:%s:%u\n", disk->name, disk_block_latest_used + 1);
		log_tag("summary:disk_block_total:%s:%u\n", disk->name, disk->total_blocks);
		log_tag("summary:disk_block_free:%s:%u\n", disk->name, disk->free_blocks);
		log_tag("summary:disk_block_max_by_space:%s:%u\n", disk->name, disk_block_max_by_space);
		log_tag("summary:disk_block_max_by_parity:%s:%u\n", disk->name, disk_block_max_by_parity);
		log_tag("summary:disk_block_max:%s:%u\n", disk->name, disk_block_max);
		log_tag("summary:disk_space_wasted:%s:%" PRId64 "\n", disk->name, wasted);
	}

	/* totals */
	printf(" --------------------------------------------------------------------------\n");
	printf("%8u", file_count);
	printf("%8u", file_fragmented);
	printf("%8u", extra_fragment);
	printf("%8.1f", (double)all_wasted / GIGA);
	printf("%8" PRIu64, file_size / GIGA);
	printf("%8" PRIu64, file_block_free * state->block_size / GIGA);
	printf(" %3u%%", perc(file_block_count, file_block_count + file_block_free));
	printf("\n");

	/* warn about invalid data free info */
	if (!free_not_zero)
		printf("\nWARNING! Free space info will be valid after the first sync.\n");

	log_tag("summary:file_count:%u\n", file_count);
	log_tag("summary:file_block_count:%" PRIu64 "\n", file_block_count);
	log_tag("summary:fragmented_file_count:%u\n", file_fragmented);
	log_tag("summary:excess_fragment_count:%u\n", extra_fragment);
	log_tag("summary:zerosubsecond_file_count:%u\n", file_zerosubsecond);
	log_tag("summary:file_size:%" PRIu64 "\n", file_size);
	log_tag("summary:parity_size:%" PRIu64 "\n", blockmax * (uint64_t)state->block_size);
	log_tag("summary:parity_size_max:%" PRIu64 "\n", (blockmax + parity_block_free) * (uint64_t)state->block_size);
	log_tag("summary:hash:%s\n", hash_config_name(state->hash));
	log_tag("summary:prev_hash:%s\n", hash_config_name(state->prevhash));
	log_tag("summary:best_hash:%s\n", hash_config_name(state->besthash));
	log_flush();

	/* copy the info a temp vector, and count bad/rehash/unsynced blocks */
	timemap = malloc_nofail(blockmax * sizeof(time_t));
	bad = 0;
	bad_first = 0;
	bad_last = 0;
	count = 0;
	rehash = 0;
	unsynced_blocks = 0;
	unscrubbed_blocks = 0;
	log_tag("block_count:%u\n", blockmax);
	for (i = 0; i < blockmax; ++i) {
		int one_invalid;
		int one_valid;

		snapraid_info info = info_get(&state->infoarr, i);

		/* for each disk */
		one_invalid = 0;
		one_valid = 0;
		for (node_disk = state->disklist; node_disk != 0; node_disk = node_disk->next) {
			struct snapraid_disk* disk = node_disk->data;
			struct snapraid_block* block = fs_par2block_find(disk, i);

			if (block_has_file(block))
				one_valid = 1;
			if (block_has_invalid_parity(block))
				one_invalid = 1;
		}

		/* if both valid and invalid, we need to update */
		if (one_invalid && one_valid) {
			++unsynced_blocks;
		}

		/* skip unused blocks */
		if (info != 0) {
			time_t scrub_time;

			if (info_get_bad(info)) {
				if (bad == 0)
					bad_first = i;
				bad_last = i;
				++bad;
			}

			if (info_get_rehash(info))
				++rehash;

			scrub_time = info_get_time(info);

			if (info_get_justsynced(info)) {
				++unscrubbed_blocks;

				/* mark the time as not scrubbed */
				scrub_time |= TIME_NEW;
			}

			timemap[count++] = scrub_time;
		}

		if (state->opt.gui) {
			if (info != 0)
				log_tag("block:%u:%" PRIu64 ":%s:%s:%s:%s\n", i, (uint64_t)info_get_time(info), one_valid ? "used" : "", one_invalid ? "unsynced" : "", info_get_bad(info) ? "bad" : "", info_get_rehash(info) ? "rehash" : "");
			else
				log_tag("block_noinfo:%u:%s:%s\n", i, one_valid ? "used" : "", one_invalid ? "unsynced" : "");
		}
	}

	log_tag("summary:has_unsynced:%u\n", unsynced_blocks);
	log_tag("summary:has_unscrubbed:%u\n", unscrubbed_blocks);
	log_tag("summary:has_rehash:%u\n", rehash);
	log_tag("summary:has_bad:%u:%u:%u\n", bad, bad_first, bad_last);
	log_flush();

	if (!count) {
		log_fatal("The array is empty.\n");
		free(timemap);
		return 0;
	}

	/* sort the info to get the time info */
	qsort(timemap, count, sizeof(time_t), time_compare);

	/* output the info map */
	i = 0;
	log_tag("info_count:%u\n", count);
	while (i < count) {
		unsigned j = i + 1;
		while (j < count && timemap[i] == timemap[j])
			++j;
		if ((timemap[i] & TIME_NEW) == 0) {
			log_tag("info_time:%" PRIu64 ":%u:scrubbed\n", (uint64_t)timemap[i], j - i);
		} else {
			log_tag("info_time:%" PRIu64 ":%u:new\n", (uint64_t)(timemap[i] & ~TIME_NEW), j - i);
		}
		i = j;
	}

	oldest = timemap[0];
	median = timemap[count / 2];
	newest = timemap[count - 1];
	dayoldest = day_ago(oldest, now);
	daymedian = day_ago(median, now);
	daynewest = day_ago(newest, now);

	/* compute graph limits */
	barpos = 0;
	barmax = 0;
	for (i = 0; i < GRAPH_COLUMN; ++i) {
		time_t limit;
		unsigned step_scrubbed, step_new;

		limit = oldest + (newest - oldest) * (i + 1) / GRAPH_COLUMN;

		step_scrubbed = 0;
		step_new = 0;
		while (barpos < count && timemap[barpos] <= limit) {
			if ((timemap[barpos] & TIME_NEW) != 0)
				++step_new;
			else
				++step_scrubbed;
			++barpos;
		}

		if (step_new + step_scrubbed > barmax)
			barmax = step_new + step_scrubbed;

		bar_scrubbed[i] = step_scrubbed;
		bar_new[i] = step_new;
	}

	printf("\n\n");

	/* print the graph */
	for (y = 0; y < GRAPH_ROW; ++y) {
		if (y == 0)
			printf("%3u%%|", barmax * 100 / count);
		else if (y == GRAPH_ROW - 1)
			printf("  0%%|");
		else if (y == GRAPH_ROW / 2)
			printf("%3u%%|", barmax * 50 / count);
		else
			printf("    |");
		for (x = 0; x < GRAPH_COLUMN; ++x) {
			unsigned pivot_upper = barmax * (GRAPH_ROW - y) / GRAPH_ROW;
			unsigned pivot_lower = barmax * (GRAPH_ROW - 1 - y) / GRAPH_ROW;
			unsigned both = bar_scrubbed[x] + bar_new[x];
			unsigned scrubbed = bar_scrubbed[x];

			if (both > pivot_upper) {
				if (scrubbed > pivot_lower)
					printf("*");
				else
					printf("o");
			} else if (both > pivot_lower) {
				if (scrubbed == both)
					printf("*");
				else
					printf("o");
			} else {
				if (y == GRAPH_ROW - 1)
					printf("_");
				else
					printf(" ");
			}
		}
		printf("\n");
	}
	printf("   %3u                    days ago of the last scrub/sync               %3u\n", dayoldest, daynewest);

	printf("\n");

	printf("The oldest block was scrubbed %u days ago, the median %u, the newest %u.\n", dayoldest, daymedian, daynewest);

	printf("\n");

	if (newest > now) {
		printf("WARNING! You have scrub dates in the future! The next sync/scrub will truncate them!\n");
	}

	if (unsynced_blocks) {
		printf("WARNING! The array is NOT fully synced.\n");
		printf("You have a sync in progress at %u%%.\n", (blockmax - unsynced_blocks) * 100 / blockmax);
	} else {
		printf("No sync is in progress.\n");
	}

	if (unscrubbed_blocks) {
		printf("The %u%% of the array is not scrubbed.\n", (unscrubbed_blocks * 100 + blockmax - 1) / blockmax);
	} else {
		printf("The full array was scrubbed at least one time.\n");
	}

	if (file_zerosubsecond) {
		printf("You have %u files with zero sub-second timestamp.\n", file_zerosubsecond);
		printf("Run the 'touch' command to set it to a not zero value.\n");
	} else {
		printf("No file has a zero sub-second timestamp.\n");
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
		block_off_t bad_print;

		printf("DANGER! In the array there are %u errors!\n\n", bad);

		printf("They are from block %u to %u, specifically at blocks:", bad_first, bad_last);

		/* print some of the errors */
		bad_print = 0;
		for (i = 0; i < blockmax; ++i) {
			snapraid_info info = info_get(&state->infoarr, i);

			/* skip unused blocks */
			if (info == 0)
				continue;

			if (info_get_bad(info)) {
				printf(" %u", i);
				++bad_print;
			}

			if (bad_print > 100) {
				printf(" and %u more...", bad - bad_print);
				break;
			}
		}

		printf("\n\n");

		printf("To fix them use the command 'snapraid -e fix'.\n");
		printf("The errors will disappear from the 'status' at the next 'scrub' command.\n");
	} else {
		printf("No error detected.\n");
	}

	/* free the temp vector */
	free(timemap);

	return 0;
}

