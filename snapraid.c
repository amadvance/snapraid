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
#include "os.h"

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

	printf("Usage: " PACKAGE " sync|diff|check|fix [options]\n");
	printf("\n");
	printf("Commands:\n");
	printf("  sync   Syncronize the state of the array of disks\n");
	printf("  diff   Show the changes that needs to be syncronized\n");
	printf("  dup    Find duplicate files\n");
	printf("  check  Check the array of disks\n");
	printf("  fix    Fix the array of disks\n");
	printf("\n");
	printf("Options:\n");
	printf("  " SWITCH_GETOPT_LONG("-c, --conf FILE     ", "-c") "  Configuration file (default " CONF ")\n");
	printf("  " SWITCH_GETOPT_LONG("-f, --filter PATTERN", "-f") "  Filter the files to processs\n");
	printf("  " SWITCH_GETOPT_LONG("-N, --find-by-name  ", "-N") "  Find the file by name instead than by inode\n");
	printf("  " SWITCH_GETOPT_LONG("-Z, --force-zero    ", "-Z") "  Force synching of files that get zero size\n");
	printf("  " SWITCH_GETOPT_LONG("-E, --force-empty   ", "-E") "  Force synching of disks that get empty\n");
	printf("  " SWITCH_GETOPT_LONG("-s, --start BLKSTART", "-s") "  Start from the specified block number\n");
	printf("  " SWITCH_GETOPT_LONG("-t, --count BLKCOUNT", "-t") "  Count of block to process\n");
	printf("  " SWITCH_GETOPT_LONG("-v, --verbose       ", "-v") "  Verbose\n");
	printf("  " SWITCH_GETOPT_LONG("-h, --help          ", "-h") "  Help\n");
	printf("  " SWITCH_GETOPT_LONG("-V, --version       ", "-V") "  Version\n");
}

#define OPT_TEST_SKIP_SELF 256
#define OPT_TEST_KILL_AFTER_SYNC 257
#define OPT_TEST_EXPECT_UNRECOVERABLE 258
#define OPT_TEST_EXPECT_RECOVERABLE 259
#define OPT_TEST_SKIP_DEVICE 260

#if HAVE_GETOPT_LONG
struct option long_options[] = {
	{ "conf", 1, 0, 'c' },
	{ "filter", 1, 0, 'f' },
	{ "filter-nohidden", 0, 0, 'H' },
	{ "start", 1, 0, 's' },
	{ "count", 1, 0, 't' },
	{ "force-zero", 0, 0, 'Z' },
	{ "force-empty", 0, 0, 'E' },
	{ "find-by-name", 0, 0, 'N' },
	{ "audit-only", 0, 0, 'A' },
	{ "speed-test", 0, 0, 'T' },
	{ "verbose", 0, 0, 'v' },
	{ "gui", 0, 0, 'G' }, /* undocumented GUI interface command */
	{ "help", 0, 0, 'h' },
	{ "version", 0, 0, 'V' },
	/* test specific options, DO NOT USE! */
	{ "test-kill-after-sync", 0, 0, OPT_TEST_KILL_AFTER_SYNC },
	{ "test-expect-unrecoverable", 0, 0, OPT_TEST_EXPECT_UNRECOVERABLE },
	{ "test-expect-recoverable", 0, 0, OPT_TEST_EXPECT_RECOVERABLE },
	{ "test-skip-self", 0, 0, OPT_TEST_SKIP_SELF },
	{ "test-skip-device", 0, 0, OPT_TEST_SKIP_DEVICE },
	{ 0, 0, 0, 0 }
};
#endif

#define OPTIONS "c:f:Hs:t:ZENATvhVG"

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
#define OPERATION_DUP 5

int main(int argc, char* argv[])
{
	int c;
	int verbose;
	int gui;
	int force_zero;
	int force_empty;
	int filter_hidden;
	int find_by_name;
	int audit_only;
	int test_expect_unrecoverable;
	int test_expect_recoverable;
	int test_kill_after_sync;
	int test_skip_self;
	int test_skip_device;
	const char* conf;
	struct snapraid_state state;
	int operation;
	block_off_t blockstart;
	block_off_t blockcount;
	int ret;
	tommy_list filterlist;
	char* e;

	os_init();

	/* defaults */
	conf = CONF;
	verbose = 0;
	gui = 0;
	force_zero = 0;
	force_empty = 0;
	filter_hidden = 0;
	find_by_name = 0;
	audit_only = 0;
	test_expect_unrecoverable = 0;
	test_expect_recoverable = 0;
	test_kill_after_sync = 0;
	test_skip_self = 0;
	test_skip_device = 0;
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
		case 'H' :
			filter_hidden = 1;
			break;
		case 's' :
			blockstart = strtoul(optarg, &e, 0);
			if (!e || *e) {
				fprintf(stderr, "Invalid start position '%s'\n", optarg);
				exit(EXIT_FAILURE);
			}
			break;
		case 't' :
			blockcount = strtoul(optarg, &e, 0);
			if (!e || *e) {
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
		case 'N' :
			find_by_name = 1;
			break;
		case 'A' :
			audit_only = 1;
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
		case OPT_TEST_KILL_AFTER_SYNC :
			test_kill_after_sync = 1;
			break;
		case OPT_TEST_EXPECT_UNRECOVERABLE :
			test_expect_unrecoverable = 1;
			break;
		case OPT_TEST_EXPECT_RECOVERABLE :
			test_expect_recoverable = 1;
			break;
		case OPT_TEST_SKIP_SELF :
			test_skip_self = 1;
			break;
		case OPT_TEST_SKIP_DEVICE :
			test_skip_device = 1;
			break;
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
	} else  if (strcmp(argv[optind], "dup") == 0) {
		operation = OPERATION_DUP;
	} else {
		fprintf(stderr, "Unknown command '%s'\n", argv[optind]);
		exit(EXIT_FAILURE);
	}

	raid_init();

	if (!test_skip_self)
		selftest(gui);

	state_init(&state);

	state_config(&state, conf, verbose, gui, force_zero, force_empty, filter_hidden, find_by_name, test_expect_unrecoverable, test_expect_recoverable, test_skip_device);

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

		/* save the new state before the sync */
		/* this allow to recover the case of the changes in the array after an aborted sync. */
		
		/* for example, think at this case: */
		/* - add some files at the array */
		/* - run a sync command, it will recompute the parity adding the new files */
		/* - abort the sync command before it stores the new content file */
		/* - delete the not yet synched files from the array */
		/* - run a new sync command */
		
		/* the new sync command has now way to know that the parity file was modified */
		/* because the files triggering these changes are now deleted */
		/* and they aren't listed in the content file */

		if (state.need_write)
			state_write(&state);

		/* waits some time to ensure that any concurrent modification done at the files, */
		/* using the same mtime read by the scan process, will be read by sync. */
		/* Note that any later modification done, potentially not read by sync, will have */
		/* a different mtime, and it will be syncronized at the next sync. */
		/* The worst case is the FAT filesystem with a two seconds resolution for mtime. */
		/* If you don't use FAT, the wait is not needed, because most filesystems have now */
		/* at least microseconds resolution, but better to be safe. */
		if (!test_skip_self)
			sleep(2);

		ret = state_sync(&state, blockstart, blockcount);

		/* save the new state if required */
		if (!test_kill_after_sync && state.need_write)
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
	} else if (operation == OPERATION_DUP) {
		state_read(&state);

		state_filter(&state, &filterlist);

		state_dup(&state);
	} else {
		state_read(&state);

		state_filter(&state, &filterlist);

		/* intercept Ctrl+C */
		signal(SIGINT, &signal_handler);

		if (operation == OPERATION_CHECK)
			state_check(&state, !audit_only, 0, blockstart, blockcount);
		else /* it's fix */
			state_check(&state, 1, 1, blockstart, blockcount);
	}

	state_done(&state);
	tommy_list_foreach(&filterlist, (tommy_foreach_func*)filter_free);

	os_done();

	return EXIT_SUCCESS;
}

