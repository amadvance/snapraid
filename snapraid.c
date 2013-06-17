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
#include "import.h"
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

	printf("Usage: " PACKAGE " sync|pool|diff|dup|check|fix [options]\n");
	printf("\n");
	printf("Commands:\n");
	printf("  sync   Syncronize the state of the array of disks\n");
	printf("  pool   Create or update the virtual view of the array of disks\n");
	printf("  diff   Show the changes that needs to be syncronized\n");
	printf("  dup    Find duplicate files\n");
	printf("  check  Check the array of disks\n");
	printf("  fix    Fix the array of disks\n");
	printf("\n");
	printf("Options:\n");
	printf("  " SWITCH_GETOPT_LONG("-c, --conf FILE       ", "-c") "  Configuration file (default " CONF ")\n");
	printf("  " SWITCH_GETOPT_LONG("-f, --filter PATTERN  ", "-f") "  Process only files matching the pattern\n");
	printf("  " SWITCH_GETOPT_LONG("-d, --filter-dist NAME", "-f") "  Process only files in the disk\n");
	printf("  " SWITCH_GETOPT_LONG("-m, --filter-missing  ", "-m") "  Process only missing/delete files\n");
	printf("  " SWITCH_GETOPT_LONG("-i, --import DIR      ", "-i") "  Import deleted files\n");
	printf("  " SWITCH_GETOPT_LONG("-l, --log FILE        ", "-l") "  Log file. Default none\n");
	printf("  " SWITCH_GETOPT_LONG("-a, --audit-only      ", "-A") "  Check only file data and not parity\n");
	printf("  " SWITCH_GETOPT_LONG("-N, --find-by-name    ", "-N") "  Find the files by name instead than by inode\n");
	printf("  " SWITCH_GETOPT_LONG("-Z, --force-zero      ", "-Z") "  Force synching of files that get zero size\n");
	printf("  " SWITCH_GETOPT_LONG("-E, --force-empty     ", "-E") "  Force synching of disks that get empty\n");
	printf("  " SWITCH_GETOPT_LONG("-U, --force-uuid      ", "-U") "  Force commands on disks with uuid changed\n");
	printf("  " SWITCH_GETOPT_LONG("-s, --start BLKSTART  ", "-s") "  Start from the specified block number\n");
	printf("  " SWITCH_GETOPT_LONG("-t, --count BLKCOUNT  ", "-t") "  Count of block to process\n");
	printf("  " SWITCH_GETOPT_LONG("-v, --verbose         ", "-v") "  Verbose\n");
	printf("  " SWITCH_GETOPT_LONG("-h, --help            ", "-h") "  Help\n");
	printf("  " SWITCH_GETOPT_LONG("-V, --version         ", "-V") "  Version\n");
}

#define OPT_TEST_SKIP_SELF 256
#define OPT_TEST_KILL_AFTER_SYNC 257
#define OPT_TEST_EXPECT_UNRECOVERABLE 258
#define OPT_TEST_EXPECT_RECOVERABLE 259
#define OPT_TEST_SKIP_SIGN 260
#define OPT_TEST_SKIP_FALLOCATE 261
#define OPT_TEST_SKIP_DEVICE 262
#define OPT_TEST_SKIP_SEQUENTIAL 263
#define OPT_TEST_FORCE_MURMUR3 264
#define OPT_TEST_FORCE_SPOOKY2 265
#define OPT_TEST_SKIP_PID 266

#if HAVE_GETOPT_LONG
struct option long_options[] = {
	{ "conf", 1, 0, 'c' },
	{ "filter", 1, 0, 'f' },
	{ "filter-disk", 1, 0, 'd' },
	{ "filter-missing", 0, 0, 'm' },
	{ "start", 1, 0, 's' },
	{ "count", 1, 0, 't' },
	{ "import", 1, 0, 'i' },
	{ "log", 1, 0, 'l' },
	{ "force-zero", 0, 0, 'Z' },
	{ "force-empty", 0, 0, 'E' },
	{ "force-uuid", 0, 0, 'U' },
	{ "find-by-name", 0, 0, 'N' },
	{ "audit-only", 0, 0, 'a' },
	{ "speed-test", 0, 0, 'T' },
	{ "verbose", 0, 0, 'v' },
	{ "gui", 0, 0, 'G' }, /* undocumented GUI interface command */
	{ "help", 0, 0, 'h' },
	{ "version", 0, 0, 'V' },

	/* The following are test specific options, DO NOT USE! */

	/* After syncing, do not write the new content file */
	{ "test-kill-after-sync", 0, 0, OPT_TEST_KILL_AFTER_SYNC },

	/* Exit with failure if after check/fix there ARE NOT unrecoverable errors. */
	{ "test-expect-unrecoverable", 0, 0, OPT_TEST_EXPECT_UNRECOVERABLE },

	/* Exit with failure if after check/fix there ARE NOT recoverable errors. */
	{ "test-expect-recoverable", 0, 0, OPT_TEST_EXPECT_RECOVERABLE },

	/* Skip the initial self test */
	{ "test-skip-self", 0, 0, OPT_TEST_SKIP_SELF },

	/* Skip the initial sign check when reading the content file */
	{ "test-skip-sign", 0, 0, OPT_TEST_SKIP_SIGN },

	/* Skip the fallocate() when growing the parity files */
	{ "test-skip-fallocate", 0, 0, OPT_TEST_SKIP_FALLOCATE },

	/* Skip the sequential hint when reading files */
	{ "test-skip-sequential", 0, 0, OPT_TEST_SKIP_SEQUENTIAL },

	/* Skip the device check */
	{ "test-skip-device", 0, 0, OPT_TEST_SKIP_DEVICE },

	/* Force Murmur3 hash */
	{ "test-force-murmur3", 0, 0, OPT_TEST_FORCE_MURMUR3 },

	/* Force Spooky2 hash */
	{ "test-force-spooky2", 0, 0, OPT_TEST_FORCE_SPOOKY2 },

	/* Skip the use of pid file */
	{ "test-skip-pid", 0, 0, OPT_TEST_SKIP_PID },

	{ 0, 0, 0, 0 }
};
#endif

#define OPTIONS "c:f:d:ms:t:i:l:ZEUNaTvhVG"

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
#define OPERATION_POOL 6
#define OPERATION_REHASH 7

int main(int argc, char* argv[])
{
	int c;
	int verbose;
	int gui;
	int force_zero;
	int force_empty;
	int force_uuid;
	int find_by_name;
	int audit_only;
	int test_kill_after_sync;
	int test_expect_unrecoverable;
	int test_expect_recoverable;
	int test_skip_self;
	int test_skip_sign;
	int test_skip_fallocate;
	int test_skip_sequential;
	int test_skip_device;
	int test_force_murmur3;
	int test_force_spooky2;
	int test_skip_pid;
	const char* conf;
	struct snapraid_state state;
	int operation;
	block_off_t blockstart;
	block_off_t blockcount;
	int ret;
	tommy_list filterlist_file;
	tommy_list filterlist_disk;
	int filter_missing;
	char* e;
	const char* command;
	const char* import;
	const char* log;
	int pid;

	os_init();

	/* defaults */
	conf = CONF;
	verbose = 0;
	gui = 0;
	force_zero = 0;
	force_empty = 0;
	force_uuid = 0;
	find_by_name = 0;
	audit_only = 0;
	test_kill_after_sync = 0;
	test_expect_unrecoverable = 0;
	test_expect_recoverable = 0;
	test_skip_self = 0;
	test_skip_sign = 0;
	test_skip_fallocate = 0;
	test_skip_sequential = 0;
	test_skip_device = 0;
	test_force_murmur3 = 0;
	test_force_spooky2 = 0;
	test_skip_pid = 0;
	blockstart = 0;
	blockcount = 0;
	tommy_list_init(&filterlist_file);
	tommy_list_init(&filterlist_disk);
	filter_missing = 0;
	import = 0;
	log = 0;
	pid = 0;

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
			struct snapraid_filter* filter = filter_alloc_file(1, optarg);
			if (!filter) {
				fprintf(stderr, "Invalid filter specification '%s'\n", optarg);
				exit(EXIT_FAILURE);
			}
			tommy_list_insert_tail(&filterlist_file, &filter->node, filter);
			} break;
		case 'd' : {
			struct snapraid_filter* filter = filter_alloc_disk(1, optarg);
			if (!filter) {
				fprintf(stderr, "Invalid filter specification '%s'\n", optarg);
				exit(EXIT_FAILURE);
			}
			tommy_list_insert_tail(&filterlist_disk, &filter->node, filter);
			} break;
		case 'm' :
			filter_missing = 1;
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
		case 'i' :
			if (import) {
				fprintf(stderr, "Import directory '%s' already specified as '%s'\n", optarg, import);
				exit(EXIT_FAILURE);
			}
			import = optarg;
			break;
		case 'l' :
			if (log) {
				fprintf(stderr, "Log file '%s' already specified as '%s'\n", optarg, log);
				exit(EXIT_FAILURE);
			}
			log = optarg;
			break;
		case 'Z' :
			force_zero = 1;
			break;
		case 'E' :
			force_empty = 1;
			break;
		case 'U' :
			force_uuid = 1;
			break;
		case 'N' :
			find_by_name = 1;
			break;
		case 'a' :
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
		case OPT_TEST_SKIP_SIGN :
			test_skip_sign = 1;
			break;
		case OPT_TEST_SKIP_FALLOCATE :
			test_skip_fallocate = 1;
			break;
		case OPT_TEST_SKIP_SEQUENTIAL :
			test_skip_sequential = 1;
			break;
		case OPT_TEST_SKIP_DEVICE :
			test_skip_device = 1;
			break;
		case OPT_TEST_FORCE_MURMUR3 :
			test_force_murmur3 = 1;
			break;
		case OPT_TEST_FORCE_SPOOKY2 :
			test_force_spooky2 = 1;
			break;
		case OPT_TEST_SKIP_PID :
			test_skip_pid = 1;
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

	command = argv[optind];
	if (strcmp(command, "diff") == 0) {
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
	} else  if (strcmp(argv[optind], "pool") == 0) {
		operation = OPERATION_POOL;
	} else  if (strcmp(argv[optind], "rehash") == 0) {
		operation = OPERATION_REHASH;
	} else {
		fprintf(stderr, "Unknown command '%s'\n", argv[optind]);
		exit(EXIT_FAILURE);
	}

	/* check options compatibility */
	switch (operation) {
	case OPERATION_CHECK :
		break;
	default:
		if (audit_only) {
			fprintf(stderr, "You cannot use -A, --audit-only with the '%s' command\n", command);
			exit(EXIT_FAILURE);
		}
	}

	switch (operation) {
	case OPERATION_SYNC :
	case OPERATION_DIFF :
		break;
	default:
		if (find_by_name) {
			fprintf(stderr, "You cannot use -N, --find-by-name with the '%s' command\n", command);
			exit(EXIT_FAILURE);
		}
	}

	switch (operation) {
	case OPERATION_CHECK :
	case OPERATION_FIX :
	case OPERATION_DRY :
		break;
	default:
		if (!tommy_list_empty(&filterlist_file) || !tommy_list_empty(&filterlist_disk) || filter_missing) {
			fprintf(stderr, "You cannot filter with the '%s' command\n", command);
			exit(EXIT_FAILURE);
		}
	}

	switch (operation) {
	case OPERATION_CHECK :
	case OPERATION_FIX :
		break;
	default:
		if (import != 0) {
			fprintf(stderr, "You cannot import with the '%s' command\n", command);
			exit(EXIT_FAILURE);
		}
	}

	raid_init();

	if (!test_skip_self)
		selftest(gui);

	state_init(&state);

	/* read the configuration file */
	state_config(&state, conf, command, verbose, gui, force_zero, force_empty, force_uuid, find_by_name, test_expect_unrecoverable, test_expect_recoverable, test_skip_sign, test_skip_fallocate, test_skip_sequential, test_skip_device, test_force_murmur3, test_force_spooky2);

#if HAVE_PIDFILE
	/* create the pid file */
	if (!test_skip_pid) {
		pid = pid_lock(state.pidfile);
		if (pid == -1) {
			if (errno != EWOULDBLOCK) {
				fprintf(stderr, "Error creating the pid file '%s'. %s.\n", state.pidfile, strerror(errno));
			} else {
				fprintf(stderr, "The pid file '%s' is already locked!\n", state.pidfile);
				fprintf(stderr, "SnapRAID is already in use!\n");
			}
			exit(EXIT_FAILURE);
		}
	}
#endif

	/* open the log file */
	if (log == 0)
		log = "2";
	if (strcmp(log, "1") == 0) {
		stdlog = stdout;
	} else if (strcmp(log, "2") == 0) {
		stdlog = stderr;
	} else {
		stdlog = fopen(log, "wt");
		if (!stdlog) {
			fprintf(stderr, "Error opening the log file '%s'. %s.\n", log, strerror(errno));
			exit(EXIT_FAILURE);
		}
	}

	if (operation == OPERATION_DIFF) {
		state_read(&state);

		state_scan(&state, 1);
	} else if (operation == OPERATION_SYNC) {
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

		/* apply the command line filter */
		state_filter(&state, &filterlist_file, &filterlist_disk, filter_missing);

		/* intercept Ctrl+C */
		signal(SIGINT, &signal_handler);

		state_dry(&state, blockstart, blockcount);
	} else if (operation == OPERATION_REHASH) {
		state_read(&state);

		/* intercept Ctrl+C */
		signal(SIGINT, &signal_handler);

		state_rehash(&state);

		/* save the new state if required */
		if (state.need_write)
			state_write(&state);
	} else if (operation == OPERATION_DUP) {
		state_read(&state);

		state_dup(&state);
	} else if (operation == OPERATION_POOL) {
		state_read(&state);

		state_pool(&state);
	} else {
		state_read(&state);

		if (import != 0)
			state_import(&state, import);

		/* apply the command line filter */
		state_filter(&state, &filterlist_file, &filterlist_disk, filter_missing);

		/* intercept Ctrl+C */
		signal(SIGINT, &signal_handler);

		if (operation == OPERATION_CHECK) {
			state_check(&state, !audit_only, 0, blockstart, blockcount);
		} else { /* it's fix */
			state_check(&state, 1, 1, blockstart, blockcount);
		}
	}

	/* close log file */
	if (stdlog != stdout && stdlog != stderr) {
		if (fclose(stdlog) != 0) {
			fprintf(stderr, "Error closing the log file '%s'. %s.\n", log, strerror(errno));
			exit(EXIT_FAILURE);
		}
	}

#if HAVE_PIDFILE
	if (!test_skip_pid) {
		if (pid_unlock(pid, state.pidfile) == -1) {
			fprintf(stderr, "Error closing the pid file '%s'. %s.\n", state.pidfile, strerror(errno));
			exit(EXIT_FAILURE);
		}
	}
#endif

	state_done(&state);
	tommy_list_foreach(&filterlist_file, (tommy_foreach_func*)filter_free);
	tommy_list_foreach(&filterlist_disk, (tommy_foreach_func*)filter_free);

	os_done();

	return EXIT_SUCCESS;
}

