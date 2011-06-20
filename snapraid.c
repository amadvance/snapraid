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

#include "snapraid.h"
#include "util.h"
#include "elem.h"
#include "state.h"
#include "raid.h"

/****************************************************************************/
/* main */

/**
 * Default configuration file.
 */
#ifdef _WIN32
#define CONF PACKAGE ".conf"
#else
#define CONF "/etc/" PACKAGE ".conf"
#endif

void version(void)
{
	printf(PACKAGE " v" VERSION " by Andrea Mazzoleni, " PACKAGE_URL "\n");
}

void usage(void)
{
	version();

	printf("Usage: " PACKAGE " [options]\n");
	printf("\n");
	printf("Options:\n");
	printf("  " SWITCH_GETOPT_LONG("-c, --conf FILE     ", "-c") "  Configuration file (default " CONF ")\n");
	printf("  " SWITCH_GETOPT_LONG("-f, --filter PATTERN", "-f") "  Filter the files to processs\n");
	printf("  " SWITCH_GETOPT_LONG("-Z, --force-zero    ", "-Z") "  Force synching of files that get zero size\n");
	printf("  " SWITCH_GETOPT_LONG("-E, --force-empty   ", "-E") "  Force synching of disks that get empty\n");
	printf("  " SWITCH_GETOPT_LONG("-s, --start BLKSTART", "-s") "  Start from the specified block number\n");
	printf("  " SWITCH_GETOPT_LONG("-t, --count BLKCOUNT", "-t") "  Count of block to process\n");
	printf("  " SWITCH_GETOPT_LONG("-v, --verbose       ", "-v") "  Verbose\n");
	printf("  " SWITCH_GETOPT_LONG("-h, --help          ", "-h") "  Help\n");
	printf("  " SWITCH_GETOPT_LONG("-V, --version       ", "-V") "  Version\n");
}

#if HAVE_GETOPT_LONG
struct option long_options[] = {
	{ "conf", 1, 0, 'c' },
	{ "filter", 1, 0, 'f' },
	{ "start", 1, 0, 's' },
	{ "count", 1, 0, 't' },
	{ "force-zero", 0, 0, 'Z' },
	{ "force-empty", 0, 0, 'E' },
	{ "skip-self-test", 0, 0, 'S' },
	{ "expect-unrecoverable", 0, 0, 'U' },
	{ "expect-recoverable", 0, 0, 'R' },
	{ "speed-test", 0, 0, 'T' },
	{ "verbose", 0, 0, 'v' },
	{ "gui", 0, 0, 'G' },
	{ "help", 0, 0, 'h' },
	{ "version", 0, 0, 'V' },
	{ 0, 0, 0, 0 }
};
#endif

#define OPTIONS "c:f:s:t:ZESURTvhVG"

volatile int global_interrupt = 0;

void signal_handler(int signal)
{
	switch (signal) {
	case SIGINT :
		global_interrupt = 1;
		break;
	}
}

#define OPERATION_DIFF 0
#define OPERATION_SYNC 1
#define OPERATION_CHECK 2
#define OPERATION_FIX 3
#define OPERATION_DRY 4

int main(int argc, char* argv[])
{
	int c;
	int verbose;
	int gui;
	int force_zero;
	int force_empty;
	int expect_unrecoverable;
	int expect_recoverable;
	int skip_self_test;
	const char* conf;
	struct snapraid_state state;
	int operation;
	block_off_t blockstart;
	block_off_t blockcount;
	int ret;
	tommy_list filterlist;

	/* defaults */
	conf = CONF;
	verbose = 0;
	gui = 0;
	force_zero = 0;
	force_empty = 0;
	expect_unrecoverable = 0;
	expect_recoverable = 0;
	skip_self_test = 0;
	blockstart = 0;
	blockcount = 0;
	tommy_list_init(&filterlist);

	opterr = 0;
	while ((c =
#if HAVE_GETOPT_LONG
		getopt_long(argc, argv, OPTIONS, long_options, 0))
#else
		getopt(argc, argv, OPTIONS))
#endif
	!= EOF) {
		switch (c) {
		case 'c' :
			conf = optarg;
			break;
		case 'f' : {
			struct snapraid_filter* filter = filter_alloc(1, optarg);
			if (!filter) {
				fprintf(stderr, "Invalid filter specification '%s'\n", optarg);
				exit(EXIT_FAILURE);
			}
			tommy_list_insert_tail(&filterlist, &filter->node, filter);
			} break;
		case 's' :
			if (stru32(optarg, &blockstart) != 0) {
				fprintf(stderr, "Invalid start position '%s'\n", optarg);
				exit(EXIT_FAILURE);
			}
			break;
		case 't' :
			if (stru32(optarg, &blockcount) != 0) {
				fprintf(stderr, "Invalid count number '%s'\n", optarg);
				exit(EXIT_FAILURE);
			}
			break;
		case 'Z' :
			force_zero = 1;
			break;
		case 'E' :
			force_empty = 1;
			break;
		case 'S' :
			skip_self_test = 1;
			break;
		case 'U' :
			expect_unrecoverable = 1;
			break;
		case 'R' :
			expect_recoverable = 1;
			break;
		case 'v' :
			verbose = 1;
			break;
		case 'G' :
			gui = 1;
			break;
		case 'h' :
			usage();
			exit(EXIT_SUCCESS);
		case 'V' :
			version();
			exit(EXIT_SUCCESS);
		case 'T' :
			speed();
			exit(EXIT_SUCCESS);
		default:
			fprintf(stderr, "Unknown option '%c'\n", (char)c);
			exit(EXIT_FAILURE);
		}
	}

	if (optind + 1 != argc) {
		usage();
		exit(EXIT_FAILURE);
	}

	if (strcmp(argv[optind], "diff") == 0) {
		operation = OPERATION_DIFF;
	} else if (strcmp(argv[optind], "sync") == 0) {
		operation = OPERATION_SYNC;
	} else if (strcmp(argv[optind], "check") == 0) {
		operation = OPERATION_CHECK;
	} else  if (strcmp(argv[optind], "fix") == 0) {
		operation = OPERATION_FIX;
	} else  if (strcmp(argv[optind], "dry") == 0) {
		operation = OPERATION_DRY;
	} else {
		fprintf(stderr, "Unknown command '%s'\n", argv[optind]);
		exit(EXIT_FAILURE);
	}

	raid_init();

	if (!skip_self_test)
		selftest(gui);

	state_init(&state);

	state_config(&state, conf, verbose, gui, force_zero, force_empty, expect_unrecoverable, expect_recoverable);

	if (operation == OPERATION_DIFF) {
		state_read(&state);

		state_scan(&state, 1);
	} else if (operation == OPERATION_SYNC) {
		if (!tommy_list_empty(&filterlist)) {
			fprintf(stderr, "You cannot filter with the sync command\n");
			exit(EXIT_FAILURE);
		}

		state_read(&state);

		state_scan(&state, 0);

		/* intercept Ctrl+C */
		signal(SIGINT, &signal_handler);

		ret = state_sync(&state, blockstart, blockcount);

		/* save the new state if required */
		if (state.need_write)
			state_write(&state);

		/* abort if required by the sync command */
		if (ret == -1)
			exit(EXIT_FAILURE);
	} else if (operation == OPERATION_DRY) {
		state_read(&state);

		state_filter(&state, &filterlist);

		/* intercept Ctrl+C */
		signal(SIGINT, &signal_handler);

		state_dry(&state, blockstart, blockcount);
	} else {
		state_read(&state);

		state_filter(&state, &filterlist);

		/* intercept Ctrl+C */
		signal(SIGINT, &signal_handler);

		state_check(&state, operation == OPERATION_FIX, blockstart, blockcount);
	}

	state_done(&state);
	tommy_list_foreach(&filterlist, (tommy_foreach_func*)filter_free);

	return EXIT_SUCCESS;
}

