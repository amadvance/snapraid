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

	printf("Usage: " PACKAGE " sync|status|scrub|diff|dup|pool|check|fix [options]\n");
	printf("\n");
	printf("Commands:\n");
	printf("  sync   Syncronize the state of the array of disks\n");
	printf("  pool   Create or update the virtual view of the array of disks\n");
	printf("  diff   Show the changes that needs to be syncronized\n");
	printf("  dup    Find duplicate files\n");
	printf("  scrub  Scrub the array of disks\n");
	printf("  status Print the status of the array\n");
	printf("  check  Check the array of disks\n");
	printf("  fix    Fix the array of disks\n");
	printf("\n");
	printf("Options:\n");
	printf("  " SWITCH_GETOPT_LONG("-c, --conf FILE       ", "-c") "  Configuration file (default " CONF ")\n");
	printf("  " SWITCH_GETOPT_LONG("-f, --filter PATTERN  ", "-f") "  Process only files matching the pattern\n");
	printf("  " SWITCH_GETOPT_LONG("-d, --filter-dist NAME", "-f") "  Process only files in the specified disk\n");
	printf("  " SWITCH_GETOPT_LONG("-m, --filter-missing  ", "-m") "  Process only missing/deleted files\n");
	printf("  " SWITCH_GETOPT_LONG("-e, --filter-error    ", "-e") "  Process only files with errors\n");
	printf("  " SWITCH_GETOPT_LONG("-p, --percentage PERC ", "-p") "  Process only a part of the array\n");
	printf("  " SWITCH_GETOPT_LONG("-o, --older-than DAYS ", "-o") "  Process only the older part of the array\n");
	printf("  " SWITCH_GETOPT_LONG("-i, --import DIR      ", "-i") "  Import deleted files\n");
	printf("  " SWITCH_GETOPT_LONG("-l, --log FILE        ", "-l") "  Log file. Default none\n");
	printf("  " SWITCH_GETOPT_LONG("-a, --audit-only      ", "-A") "  Check only file data and not parity\n");
	printf("  " SWITCH_GETOPT_LONG("-Z, --force-zero      ", "-Z") "  Force synching of files that get zero size\n");
	printf("  " SWITCH_GETOPT_LONG("-E, --force-empty     ", "-E") "  Force synching of disks that get empty\n");
	printf("  " SWITCH_GETOPT_LONG("-U, --force-uuid      ", "-U") "  Force commands on disks with uuid changed\n");
	printf("  " SWITCH_GETOPT_LONG("-D, --force-device    ", "-D") "  Force commands on disks with same device id\n");
	printf("  " SWITCH_GETOPT_LONG("-s, --start BLKSTART  ", "-s") "  Start from the specified block number\n");
	printf("  " SWITCH_GETOPT_LONG("-t, --count BLKCOUNT  ", "-t") "  Count of block to process\n");
	printf("  " SWITCH_GETOPT_LONG("-v, --verbose         ", "-v") "  Verbose\n");
	printf("  " SWITCH_GETOPT_LONG("-h, --help            ", "-h") "  Help\n");
	printf("  " SWITCH_GETOPT_LONG("-V, --version         ", "-V") "  Version\n");
}

void memory(void)
{
	fprintf(stdlog, "memory:used:%"PRIu64"\n", (uint64_t)malloc_counter());
	printf("Using %u MiB of memory.\n", (unsigned)(malloc_counter() / 1024 / 1024));
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
#define OPT_TEST_SKIP_LOCK 266
#define OPT_TEST_FORCE_ORDER_PHYSICAL 267
#define OPT_TEST_FORCE_ORDER_INODE 268
#define OPT_TEST_FORCE_ORDER_ALPHA 269
#define OPT_TEST_FORCE_ORDER_DIR 270
#define OPT_TEST_FORCE_SCRUB 271
#define OPT_TEST_FORCE_SCRUB_EVEN 272
#define OPT_TEST_FORCE_CONTENT_WRITE 273
#define OPT_TEST_FORCE_CONTENT_TEXT 274

#if HAVE_GETOPT_LONG
struct option long_options[] = {
	{ "conf", 1, 0, 'c' },
	{ "filter", 1, 0, 'f' },
	{ "filter-disk", 1, 0, 'd' },
	{ "filter-missing", 0, 0, 'm' },
	{ "filter-error", 0, 0, 'e' },
	{ "percentage", 1, 0, 'p' },
	{ "older-than", 1, 0, 'o' },
	{ "start", 1, 0, 's' },
	{ "count", 1, 0, 't' },
	{ "import", 1, 0, 'i' },
	{ "log", 1, 0, 'l' },
	{ "force-zero", 0, 0, 'Z' },
	{ "force-empty", 0, 0, 'E' },
	{ "force-uuid", 0, 0, 'U' },
	{ "force-device", 0, 0, 'D' },
	{ "find-by-name", 0, 0, 'N' }, /* deprecated in SnapRAID 4.0 */
	{ "audit-only", 0, 0, 'a' },
	{ "speed-test", 0, 0, 'T' },
	{ "gen-conf", 1, 0, 'C' },
	{ "verbose", 0, 0, 'v' },
	{ "quiet", 0, 0, 'q' },
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

	/* Skip the use of lock file */
	{ "test-skip-lock", 0, 0, OPT_TEST_SKIP_LOCK },

	/* Force a sort order for files */
	{ "test-force-order-physical", 0, 0, OPT_TEST_FORCE_ORDER_PHYSICAL },
	{ "test-force-order-inode", 0, 0, OPT_TEST_FORCE_ORDER_INODE },
	{ "test-force-order-alpha", 0, 0, OPT_TEST_FORCE_ORDER_ALPHA },
	{ "test-force-order-dir", 0, 0, OPT_TEST_FORCE_ORDER_DIR },

	/* Force scrub of the specified number of blocks */
	{ "test-force-scrub", 1, 0, OPT_TEST_FORCE_SCRUB },

	/* Force scrub of all the even blocks. This is really for testing, don't try it. */
	{ "test-force-scrub-even", 0, 0, OPT_TEST_FORCE_SCRUB_EVEN },

	/* Force write of the content file even if no modification is done. */
	{ "test-force-content-write", 0, 0, OPT_TEST_FORCE_CONTENT_WRITE },

	/* Force the use of text content file . */
	{ "test-force-content-text", 0, 0, OPT_TEST_FORCE_CONTENT_TEXT },

	{ 0, 0, 0, 0 }
};
#endif

#define OPTIONS "c:f:d:mep:o:s:t:i:l:ZEUDNaTC:vqhVG"

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
#define OPERATION_LIST 6
#define OPERATION_POOL 7
#define OPERATION_REHASH 8
#define OPERATION_SCRUB 9
#define OPERATION_STATUS 10
#define OPERATION_REWRITE 11

int main(int argc, char* argv[])
{
	int c;
	struct snapraid_option opt;
	int audit_only;
	const char* conf;
	struct snapraid_state state;
	int operation;
	block_off_t blockstart;
	block_off_t blockcount;
	int ret;
	tommy_list filterlist_file;
	tommy_list filterlist_disk;
	int filter_missing;
	int filter_error;
	int percentage;
	int olderthan;
	char* e;
	const char* command;
	const char* import;
	const char* log;
	int lock;
	const char* gen_conf;

	os_init();
	raid_init();
	crc32c_init();

	/* defaults */
	conf = CONF;
	memset(&opt, 0, sizeof(opt));
	audit_only = 0;
	blockstart = 0;
	blockcount = 0;
	tommy_list_init(&filterlist_file);
	tommy_list_init(&filterlist_disk);
	filter_missing = 0;
	filter_error = 0;
	percentage = -1;
	olderthan = -1;
	import = 0;
	log = 0;
	lock = 0;
	gen_conf = 0;

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
		case 'e' :
			filter_error = 1;
			break;
		case 'p' :
			percentage = strtoul(optarg, &e, 10);
			if (!e || *e || percentage > 100) {
				fprintf(stderr, "Invalid percentage '%s'\n", optarg);
				exit(EXIT_FAILURE);
			}
			break;
		case 'o' :
			olderthan = strtoul(optarg, &e, 10);
			if (!e || *e || olderthan > 1000) {
				fprintf(stderr, "Invalid number of days '%s'\n", optarg);
				exit(EXIT_FAILURE);
			}
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
			opt.force_zero = 1;
			break;
		case 'E' :
			opt.force_empty = 1;
			break;
		case 'U' :
			opt.force_uuid = 1;
			break;
		case 'D' :
			opt.force_device = 1;
			break;
		case 'N' :
			fprintf(stderr, "WARNING! Option --find-by-name, -N is deprecated and does nothing!\n");
			break;
		case 'a' :
			audit_only = 1;
			break;
		case 'v' :
			opt.verbose = 1;
			break;
		case 'q' :
			opt.quiet = 1;
			break;
		case 'G' :
			opt.gui = 1;
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
		case 'C' :
			gen_conf = optarg;
			break;
		case OPT_TEST_KILL_AFTER_SYNC :
			opt.kill_after_sync = 1;
			break;
		case OPT_TEST_EXPECT_UNRECOVERABLE :
			opt.expect_unrecoverable = 1;
			break;
		case OPT_TEST_EXPECT_RECOVERABLE :
			opt.expect_recoverable = 1;
			break;
		case OPT_TEST_SKIP_SELF :
			opt.skip_self = 1;
			break;
		case OPT_TEST_SKIP_SIGN :
			opt.skip_sign = 1;
			break;
		case OPT_TEST_SKIP_FALLOCATE :
			opt.skip_fallocate = 1;
			break;
		case OPT_TEST_SKIP_SEQUENTIAL :
			opt.skip_sequential = 1;
			break;
		case OPT_TEST_SKIP_DEVICE :
			opt.skip_device = 1;
			break;
		case OPT_TEST_FORCE_MURMUR3 :
			opt.force_murmur3 = 1;
			break;
		case OPT_TEST_FORCE_SPOOKY2 :
			opt.force_spooky2 = 1;
			break;
		case OPT_TEST_SKIP_LOCK :
			opt.skip_lock = 1;
			break;
		case OPT_TEST_FORCE_ORDER_PHYSICAL :
			opt.force_order = SORT_PHYSICAL;
			break;
		case OPT_TEST_FORCE_ORDER_INODE :
			opt.force_order = SORT_INODE;
			break;
		case OPT_TEST_FORCE_ORDER_ALPHA :
			opt.force_order = SORT_ALPHA;
			break;
		case OPT_TEST_FORCE_ORDER_DIR :
			opt.force_order = SORT_DIR;
			break;
		case OPT_TEST_FORCE_SCRUB :
			opt.force_scrub = atoi(optarg);
			break;
		case OPT_TEST_FORCE_SCRUB_EVEN :
			opt.force_scrub_even = 1;
			break;
		case OPT_TEST_FORCE_CONTENT_WRITE :
			opt.force_content_write = 1;
			break;
		case OPT_TEST_FORCE_CONTENT_TEXT :
			opt.force_content_text = 1;
			break;
		default:
			fprintf(stderr, "Unknown option '%c'\n", (char)c);
			exit(EXIT_FAILURE);
		}
	}

	if (gen_conf != 0) {
		generate_configuration(gen_conf);
		os_done();
		exit(EXIT_SUCCESS);
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
	} else if (strcmp(argv[optind], "fix") == 0) {
		operation = OPERATION_FIX;
	} else if (strcmp(argv[optind], "dry") == 0) {
		operation = OPERATION_DRY;
	} else if (strcmp(argv[optind], "dup") == 0) {
		operation = OPERATION_DUP;
	} else if (strcmp(argv[optind], "list") == 0) {
		operation = OPERATION_LIST;
	} else if (strcmp(argv[optind], "pool") == 0) {
		operation = OPERATION_POOL;
	} else if (strcmp(argv[optind], "rehash") == 0) {
		operation = OPERATION_REHASH;
	} else if (strcmp(argv[optind], "scrub") == 0) {
		operation = OPERATION_SCRUB;
	} else if (strcmp(argv[optind], "status") == 0) {
		operation = OPERATION_STATUS;
	} else if (strcmp(argv[optind], "test-rewrite") == 0) {
		operation = OPERATION_REWRITE;
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
	case OPERATION_CHECK :
	case OPERATION_FIX :
	case OPERATION_DRY :
		break;
	default:
		if (!tommy_list_empty(&filterlist_file)) {
			fprintf(stderr, "You cannot use -f, --filter with the '%s' command\n", command);
			exit(EXIT_FAILURE);
		}
		if (!tommy_list_empty(&filterlist_disk)) {
			fprintf(stderr, "You cannot use -d, --filter-disk with the '%s' command\n", command);
			exit(EXIT_FAILURE);
		}
		if (filter_missing != 0) {
			fprintf(stderr, "You cannot use -m, --filter-missing with the '%s' command\n", command);
			exit(EXIT_FAILURE);
		}
	}

	switch (operation) {
	case OPERATION_CHECK :
	case OPERATION_FIX :
		break;
	default:
		if (filter_error != 0) {
			fprintf(stderr, "You cannot use -e, --filter-error with the '%s' command\n", command);
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

	/* if no log file is specified, don't output */
	if (log == 0) {
#ifdef _WIN32
		log = "NUL";
#else
		log = "/dev/null";
#endif
	}
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

	if (!opt.skip_self)
		selftest();

	state_init(&state);

	/* read the configuration file */
	state_config(&state, conf, command, &opt, &filterlist_disk);

#if HAVE_LOCKFILE
	/* create the lock file */
	if (!opt.skip_lock) {
		lock = lock_lock(state.lockfile);
		if (lock == -1) {
			if (errno != EWOULDBLOCK) {
				fprintf(stderr, "Error creating the lock file '%s'. %s.\n", state.lockfile, strerror(errno));
			} else {
				fprintf(stderr, "The lock file '%s' is already locked!\n", state.lockfile);
				fprintf(stderr, "SnapRAID is already in use!\n");
			}
			exit(EXIT_FAILURE);
		}
	}
#else
	(void)lock;
#endif

	if (operation == OPERATION_DIFF) {
		state_read(&state);

		state_scan(&state, 1);
	} else if (operation == OPERATION_SYNC) {

		/* in the next state read ensures to clear all the undeterminated hashes in case */
		/* we are reading from an incomplete sync */
		/* The undeterminated hash are only for CHG/DELETED blocks for which we don't */
		/* know if the previous interrupted sync was able to update or not the parity */
		/* The sync process instead needs to trust this information because it's used */
		/* to avoid to recompute the parity if all the input are equals as before. */
		state.clear_undeterminate_hash = 1;

		state_read(&state);

		state_scan(&state, 0);

		memory();

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
		if (!opt.skip_self)
			sleep(2);

		ret = state_sync(&state, blockstart, blockcount);

		/* save the new state if required */
		if (!opt.kill_after_sync && (state.need_write || state.opt.force_content_write))
			state_write(&state);

		/* abort if required */
		if (ret == -1)
			exit(EXIT_FAILURE);
	} else if (operation == OPERATION_DRY) {
		state_read(&state);

		/* apply the command line filter */
		state_filter(&state, &filterlist_file, &filterlist_disk, filter_missing, filter_error);

		memory();

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
	} else if (operation == OPERATION_SCRUB) {
		state_read(&state);

		memory();

		/* intercept Ctrl+C */
		signal(SIGINT, &signal_handler);

		ret = state_scrub(&state, percentage, olderthan);

		/* save the new state if required */
		if (state.need_write || state.opt.force_content_write)
			state_write(&state);

		/* abort if required */
		if (ret != 0)
			exit(EXIT_FAILURE);
	} else if (operation == OPERATION_REWRITE) {
		state_read(&state);

		state_write(&state);

		memory();
	} else if (operation == OPERATION_STATUS) {
		state_read(&state);

		memory();

		state_status(&state);
	} else if (operation == OPERATION_DUP) {
		state_read(&state);

		state_dup(&state);
	} else if (operation == OPERATION_LIST) {
		state_read(&state);

		state_list(&state);
	} else if (operation == OPERATION_POOL) {
		state_read(&state);

		state_pool(&state);
	} else {
		state_read(&state);

		if (import != 0)
			state_import(&state, import);

		/* apply the command line filter */
		state_filter(&state, &filterlist_file, &filterlist_disk, filter_missing, filter_error);

		memory();

		/* intercept Ctrl+C */
		signal(SIGINT, &signal_handler);

		if (operation == OPERATION_CHECK) {
			ret = state_check(&state, !audit_only, 0, blockstart, blockcount);
		} else { /* it's fix */
			ret = state_check(&state, 1, 1, blockstart, blockcount);
		}

		/* abort if required */
		if (ret != 0)
			exit(EXIT_FAILURE);
	}

	/* close log file */
	if (stdlog != stdout && stdlog != stderr) {
		if (fclose(stdlog) != 0) {
			fprintf(stderr, "Error closing the log file '%s'. %s.\n", log, strerror(errno));
			exit(EXIT_FAILURE);
		}
	}

#if HAVE_LOCKFILE
	if (!opt.skip_lock) {
		if (lock_unlock(lock) == -1) {
			fprintf(stderr, "Error closing the lock file '%s'. %s.\n", state.lockfile, strerror(errno));
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

