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
#include "support.h"
#include "elem.h"
#include "import.h"
#include "search.h"
#include "state.h"
#include "io.h"
#include "raid/raid.h"

/****************************************************************************/
/* main */

void version(void)
{
	msg_status(PACKAGE " v" VERSION " by Andrea Mazzoleni, " PACKAGE_URL "\n");
}

void usage(void)
{
	version();

	printf("Usage: " PACKAGE " status|diff|sync|scrub|list|dup|up|down|smart|pool|check|fix [options]\n");
	printf("\n");
	printf("Commands:\n");
	printf("  status Print the status of the array\n");
	printf("  diff   Show the changes that needs to be synchronized\n");
	printf("  sync   Synchronize the state of the array\n");
	printf("  scrub  Scrub the array\n");
	printf("  list   List the array content\n");
	printf("  dup    Find duplicate files\n");
	printf("  up     Spin-up the array\n");
	printf("  down   Spin-down the array\n");
	printf("  smart  SMART attributes of the array\n");
	printf("  pool   Create or update the virtual view of the array\n");
	printf("  check  Check the array\n");
	printf("  fix    Fix the array\n");
	printf("\n");
	printf("Options:\n");
	printf("  " SWITCH_GETOPT_LONG("-c, --conf FILE       ", "-c") "  Configuration file\n");
	printf("  " SWITCH_GETOPT_LONG("-f, --filter PATTERN  ", "-f") "  Process only files matching the pattern\n");
	printf("  " SWITCH_GETOPT_LONG("-d, --filter-disk NAME", "-f") "  Process only files in the specified disk\n");
	printf("  " SWITCH_GETOPT_LONG("-m, --filter-missing  ", "-m") "  Process only missing/deleted files\n");
	printf("  " SWITCH_GETOPT_LONG("-e, --filter-error    ", "-e") "  Process only files with errors\n");
	printf("  " SWITCH_GETOPT_LONG("-p, --plan PLAN       ", "-p") "  Define a scrub plan or percentage\n");
	printf("  " SWITCH_GETOPT_LONG("-o, --older-than DAYS ", "-o") "  Process only the older part of the array\n");
	printf("  " SWITCH_GETOPT_LONG("-i, --import DIR      ", "-i") "  Import deleted files\n");
	printf("  " SWITCH_GETOPT_LONG("-l, --log FILE        ", "-l") "  Log file. Default none\n");
	printf("  " SWITCH_GETOPT_LONG("-a, --audit-only      ", "-a") "  Check only file data and not parity\n");
	printf("  " SWITCH_GETOPT_LONG("-h, --pre-hash        ", "-h") "  Pre-hash all the new data\n");
	printf("  " SWITCH_GETOPT_LONG("-Z, --force-zero      ", "-Z") "  Force syncing of files that get zero size\n");
	printf("  " SWITCH_GETOPT_LONG("-E, --force-empty     ", "-E") "  Force syncing of disks that get empty\n");
	printf("  " SWITCH_GETOPT_LONG("-U, --force-uuid      ", "-U") "  Force commands on disks with uuid changed\n");
	printf("  " SWITCH_GETOPT_LONG("-D, --force-device    ", "-D") "  Force commands with inaccessible/shared disks\n");
	printf("  " SWITCH_GETOPT_LONG("-N, --force-nocopy    ", "-N") "  Force commands disabling the copy detection\n");
	printf("  " SWITCH_GETOPT_LONG("-F, --force-full      ", "-F") "  Force a full parity computation in sync\n");
	printf("  " SWITCH_GETOPT_LONG("-R, --force-realloc   ", "-R") "  Force a full parity reallocation in sync\n");
	printf("  " SWITCH_GETOPT_LONG("-v, --verbose         ", "-v") "  Verbose\n");
}

void memory(void)
{
	log_tag("memory:used:%" PRIu64 "\n", (uint64_t)malloc_counter_get());

	/* size of the block */
	log_tag("memory:block:%" PRIu64 "\n", (uint64_t)(sizeof(struct snapraid_block)));
	log_tag("memory:extent:%" PRIu64 "\n", (uint64_t)(sizeof(struct snapraid_extent)));
	log_tag("memory:file:%" PRIu64 "\n", (uint64_t)(sizeof(struct snapraid_file)));
	log_tag("memory:link:%" PRIu64 "\n", (uint64_t)(sizeof(struct snapraid_link)));
	log_tag("memory:dir:%" PRIu64 "\n", (uint64_t)(sizeof(struct snapraid_dir)));

	msg_progress("Using %u MiB of memory for the file-system.\n", (unsigned)(malloc_counter_get() / MEBI));
}

void test(int argc, char* argv[])
{
	int i;
	char buffer[ESC_MAX];

	/* special testing code for quoting */
	if (argc < 2 || strcmp(argv[1], "test") != 0)
		return;

	for (i = 2; i < argc; ++i) {
		printf("argv[%d]\n", i);
		printf("\t#%s#\n", argv[i]);
		printf("\t#%s#\n", esc_shell(argv[i], buffer));
	}

#ifdef _WIN32
	assert(strcmp(esc_shell(" ", buffer), "\" \"") == 0);
	assert(strcmp(esc_shell(" \" ", buffer), "\" \"\\\"\" \"") == 0);
	assert(strcmp(esc_shell("&|()<>^", buffer), "^&^|^(^)^<^>^^") == 0);
	assert(strcmp(esc_shell("&|()<>^ ", buffer), "\"&|()<>^ \"") == 0);
#else
	assert(strcmp(esc_shell(",._+:@%%/-", buffer), ",._+:@%%/-") == 0);
	assert(strcmp(esc_shell(" ", buffer), "\\ ") == 0);
#endif

	printf("Everything OK\n");

	exit(EXIT_SUCCESS);
}

/****************************************************************************/
/* log */

void log_open(const char* file)
{
	char path[PATH_MAX];
	const char* mode;
	char text_T[32];
	char text_D[32];
	time_t t;
	struct tm* tm;

	/* leave stdlog at 0 if not specified */
	if (file == 0)
		return;

	t = time(0);
	tm = localtime(&t);
	if (tm) {
		strftime(text_T, sizeof(text_T), "%H%M%S", tm);
		strftime(text_D, sizeof(text_T), "%Y%m%d", tm);
	} else {
		/* LCOV_EXCL_START */
		strcpy(text_T, "invalid");
		strcpy(text_D, "invalid");
		/* LCOV_EXCL_STOP */
	}

	/* file mode */
	mode = "wt";
	if (*file == '>') {
		++file;

		if (*file == '>') {
			mode = "at";
			++file;
		}

		if (file[0] == '&' && file[1] == '1') {
			stdlog = stdout;
			return;
		}

		if (file[0] == '&' && file[1] == '2') {
			stdlog = stderr;
			return;
		}
	}

	/* process the path */
	for (*path = 0; *file != 0; ) {
		switch (*file) {
		case '%' :
			++file;
			switch (*file) {
			case '%' :
				pathcatc(path, sizeof(path), '%');
				break;
			case 'T' :
				pathcat(path, sizeof(path), text_T);
				break;
			case 'D' :
				pathcat(path, sizeof(path), text_D);
				break;
			default :
				/* LCOV_EXCL_START */
				log_fatal("Invalid type specifier '%c' in the log file.\n", *file);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			break;
		default :
			pathcatc(path, sizeof(path), *file);
			break;
		}
		++file;
	}

	stdlog = fopen(path, mode);
	if (!stdlog) {
		/* LCOV_EXCL_START */
		log_fatal("Error opening the log file '%s'. %s.\n", path, strerror(errno));
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}
}

void log_close(const char* file)
{
	if (stdlog != stdout && stdlog != stderr && stdlog != 0) {
		if (fclose(stdlog) != 0) {
			/* LCOV_EXCL_START */
			log_fatal("Error closing the log file '%s'. %s.\n", file, strerror(errno));
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}

	stdlog = 0;
}

/****************************************************************************/
/* config */

void config(char* conf, size_t conf_size, const char* argv0)
{
#ifdef _WIN32
	char* slash;

	pathimport(conf, conf_size, argv0);

	slash = strrchr(conf, '/');
	if (slash) {
		slash[1] = 0;
		pathcat(conf, conf_size, PACKAGE ".conf");
	} else {
		pathcpy(conf, conf_size, PACKAGE ".conf");
	}
#else
	(void)argv0;

#ifdef SYSCONFDIR
	/* if it exists, give precedence at sysconfdir, usually /usr/local/etc */
	if (access(SYSCONFDIR "/" PACKAGE ".conf", F_OK) == 0)
		pathcpy(conf, conf_size, SYSCONFDIR "/" PACKAGE ".conf");
	else /* otherwise fallback to plain /etc */
#endif
		pathcpy(conf, conf_size, "/etc/" PACKAGE ".conf");
#endif
}

/****************************************************************************/
/* main */

#define OPT_TEST_SKIP_SELF 256
#define OPT_TEST_KILL_AFTER_SYNC 257
#define OPT_TEST_EXPECT_UNRECOVERABLE 258
#define OPT_TEST_EXPECT_RECOVERABLE 259
#define OPT_TEST_SKIP_SIGN 260
#define OPT_TEST_SKIP_FALLOCATE 261
#define OPT_TEST_SKIP_DEVICE 262
#define OPT_TEST_FORCE_MURMUR3 264
#define OPT_TEST_FORCE_SPOOKY2 265
#define OPT_TEST_SKIP_LOCK 266
#define OPT_TEST_FORCE_ORDER_PHYSICAL 267
#define OPT_TEST_FORCE_ORDER_INODE 268
#define OPT_TEST_FORCE_ORDER_ALPHA 269
#define OPT_TEST_FORCE_ORDER_DIR 270
#define OPT_TEST_FORCE_SCRUB_AT 271
#define OPT_TEST_FORCE_SCRUB_EVEN 272
#define OPT_TEST_FORCE_CONTENT_WRITE 273
#define OPT_TEST_SKIP_CONTENT_CHECK 275
#define OPT_TEST_SKIP_PARITY_ACCESS 276
#define OPT_TEST_EXPECT_FAILURE 277
#define OPT_TEST_RUN 278
#define OPT_TEST_FORCE_SCAN_WINFIND 279
#define OPT_TEST_IMPORT_CONTENT 280
#define OPT_TEST_FORCE_PROGRESS 281
#define OPT_TEST_SKIP_DISK_ACCESS 282
#define OPT_TEST_FORCE_AUTOSAVE_AT 283
#define OPT_TEST_FAKE_DEVICE 284
#define OPT_TEST_EXPECT_NEED_SYNC 285
#define OPT_NO_WARNINGS 286
#define OPT_TEST_FAKE_UUID 287
#define OPT_TEST_MATCH_FIRST_UUID 288
#define OPT_TEST_FORCE_PARITY_UPDATE 289
#define OPT_TEST_IO_CACHE 290
#define OPT_TEST_IO_STATS 291
#define OPT_TEST_COND_SIGNAL_OUTSIDE 292
#define OPT_TEST_IO_ADVISE_NONE 293
#define OPT_TEST_IO_ADVISE_SEQUENTIAL 294
#define OPT_TEST_IO_ADVISE_FLUSH 295
#define OPT_TEST_IO_ADVISE_FLUSH_WINDOW 296
#define OPT_TEST_IO_ADVISE_DISCARD 297
#define OPT_TEST_IO_ADVISE_DISCARD_WINDOW 298
#define OPT_TEST_IO_ADVISE_DIRECT 299
#define OPT_TEST_PARITY_LIMIT 301
#define OPT_TEST_SKIP_CONTENT_WRITE 302
#define OPT_TEST_SKIP_SPACE_HOLDER 303
#define OPT_TEST_FORMAT 304

#if HAVE_GETOPT_LONG
struct option long_options[] = {
	{ "conf", 1, 0, 'c' },
	{ "filter", 1, 0, 'f' },
	{ "filter-disk", 1, 0, 'd' },
	{ "filter-missing", 0, 0, 'm' },
	{ "filter-error", 0, 0, 'e' },
	{ "percentage", 1, 0, 'p' }, /* legacy name for --plan */
	{ "plan", 1, 0, 'p' },
	{ "older-than", 1, 0, 'o' },
	{ "start", 1, 0, 'S' },
	{ "count", 1, 0, 'B' },
	{ "error-limit", 1, 0, 'L' },
	{ "import", 1, 0, 'i' },
	{ "log", 1, 0, 'l' },
	{ "force-zero", 0, 0, 'Z' },
	{ "force-empty", 0, 0, 'E' },
	{ "force-uuid", 0, 0, 'U' },
	{ "force-device", 0, 0, 'D' },
	{ "force-nocopy", 0, 0, 'N' },
	{ "force-full", 0, 0, 'F' },
	{ "force-realloc", 0, 0, 'R' },
	{ "audit-only", 0, 0, 'a' },
	{ "pre-hash", 0, 0, 'h' },
	{ "speed-test", 0, 0, 'T' }, /* undocumented speed test command */
	{ "gen-conf", 1, 0, 'C' },
	{ "verbose", 0, 0, 'v' },
	{ "quiet", 0, 0, 'q' }, /* undocumented quiet option */
	{ "gui", 0, 0, 'G' }, /* undocumented GUI interface option */
	{ "help", 0, 0, 'H' },
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
	{ "test-force-scrub-at", 1, 0, OPT_TEST_FORCE_SCRUB_AT },

	/* Force scrub of all the even blocks. This is really for testing, don't try it */
	{ "test-force-scrub-even", 0, 0, OPT_TEST_FORCE_SCRUB_EVEN },

	/* Force write of the content file even if no modification is done */
	{ "test-force-content-write", 0, 0, OPT_TEST_FORCE_CONTENT_WRITE },

	/* Relax the checks done at the content file */
	{ "test-skip-content-check", 0, 0, OPT_TEST_SKIP_CONTENT_CHECK },

	/* Skip the parity access */
	{ "test-skip-parity-access", 0, 0, OPT_TEST_SKIP_PARITY_ACCESS },

	/* Exit generic failure */
	{ "test-expect-failure", 0, 0, OPT_TEST_EXPECT_FAILURE },

	/* Exit generic need sync */
	{ "test-expect-need-sync", 0, 0, OPT_TEST_EXPECT_NEED_SYNC },

	/* Run some command after loading the state and before the command */
	{ "test-run", 1, 0, OPT_TEST_RUN },

	/* Use the FindFirst/Next approach in Windows to list files */
	{ "test-force-scan-winfind", 0, 0, OPT_TEST_FORCE_SCAN_WINFIND },

	/* Alternative import working by data */
	{ "test-import-content", 1, 0, OPT_TEST_IMPORT_CONTENT },

	/* Force immediate progress state update */
	{ "test-force-progress", 0, 0, OPT_TEST_FORCE_PROGRESS },

	/* Skip the disk access */
	{ "test-skip-disk-access", 0, 0, OPT_TEST_SKIP_DISK_ACCESS },

	/* Force autosave at the specified block */
	{ "test-force-autosave-at", 1, 0, OPT_TEST_FORCE_AUTOSAVE_AT },

	/* Fake device data */
	{ "test-fake-device", 0, 0, OPT_TEST_FAKE_DEVICE },

	/* Disable annoying warnings */
	{ "no-warnings", 0, 0, OPT_NO_WARNINGS },

	/* Fake UUID */
	{ "test-fake-uuid", 0, 0, OPT_TEST_FAKE_UUID },

	/* Match first UUID */
	{ "test-match-first-uuid", 0, 0, OPT_TEST_MATCH_FIRST_UUID },

	/* Force parity update even if all the data hash is already matching */
	{ "test-force-parity-update", 0, 0, OPT_TEST_FORCE_PARITY_UPDATE },

	/* Number of IO buffers */
	{ "test-io-cache", 1, 0, OPT_TEST_IO_CACHE },

	/* Print IO stats */
	{ "test-io-stats", 0, 0, OPT_TEST_IO_STATS },

	/* Signal condition variable outside the mutex */
	{ "test-cond-signal-outside", 0, 0, OPT_TEST_COND_SIGNAL_OUTSIDE },

	/* Set the io advise to none */
	{ "test-io-advise-none", 0, 0, OPT_TEST_IO_ADVISE_NONE },

	/* Set the io advise to sequential */
	{ "test-io-advise-sequential", 0, 0, OPT_TEST_IO_ADVISE_SEQUENTIAL },

	/* Set the io advise to flush */
	{ "test-io-advise-flush", 0, 0, OPT_TEST_IO_ADVISE_FLUSH },

	/* Set the io advise to flush window */
	{ "test-io-advise-flush-window", 0, 0, OPT_TEST_IO_ADVISE_FLUSH_WINDOW },

	/* Set the io advise to discard */
	{ "test-io-advise-discard", 0, 0, OPT_TEST_IO_ADVISE_DISCARD },

	/* Set the io advise to discard window */
	{ "test-io-advise-discard-window", 0, 0, OPT_TEST_IO_ADVISE_DISCARD_WINDOW },

	/* Set the io advise to direct */
	{ "test-io-advise-direct", 0, 0, OPT_TEST_IO_ADVISE_DIRECT },

	/* Set an artificial parity limit */
	{ "test-parity-limit", 1, 0, OPT_TEST_PARITY_LIMIT },

	/* Skip content write */
	{ "test-skip-content-write", 0, 0, OPT_TEST_SKIP_CONTENT_WRITE },

	/* Skip space holder file in parity disks */
	{ "test-skip-space-holder", 0, 0, OPT_TEST_SKIP_SPACE_HOLDER },

	/* Set the output format */
	{ "test-fmt", 1, 0, OPT_TEST_FORMAT },

	{ 0, 0, 0, 0 }
};
#endif

#define OPTIONS "c:f:d:mep:o:S:B:L:i:l:ZEUDNFRahTC:vqHVG"

volatile int global_interrupt = 0;

/* LCOV_EXCL_START */
void signal_handler(int signum)
{
	(void)signum;

	/* report the request of interruption */
	global_interrupt = 1;
}
/* LCOV_EXCL_STOP */

void signal_init(void)
{
#if HAVE_SIGACTION
	struct sigaction sa;

	sa.sa_handler = signal_handler;
	sigemptyset(&sa.sa_mask);

	/* use the SA_RESTART to automatically restart interrupted system calls */
	sa.sa_flags = SA_RESTART;

	sigaction(SIGHUP, &sa, 0);
	sigaction(SIGTERM, &sa, 0);
	sigaction(SIGINT, &sa, 0);
	sigaction(SIGQUIT, &sa, 0);
#else
	signal(SIGINT, signal_handler);
#endif
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
#define OPERATION_READ 12
#define OPERATION_TOUCH 13
#define OPERATION_SPINUP 14
#define OPERATION_SPINDOWN 15
#define OPERATION_DEVICES 16
#define OPERATION_SMART 17

int main(int argc, char* argv[])
{
	int c;
	struct snapraid_option opt;
	char conf[PATH_MAX];
	struct snapraid_state state;
	int operation;
	block_off_t blockstart;
	block_off_t blockcount;
	int ret;
	tommy_list filterlist_file;
	tommy_list filterlist_disk;
	int filter_missing;
	int filter_error;
	int plan;
	int olderthan;
	char* e;
	const char* command;
	const char* import_timestamp;
	const char* import_content;
	const char* log_file;
	int lock;
	const char* gen_conf;
	const char* run;
	int speedtest;
	int period;
	time_t t;
	struct tm* tm;
	int i;

	test(argc, argv);

	lock_init();

	/* defaults */
	config(conf, sizeof(conf), argv[0]);
	memset(&opt, 0, sizeof(opt));
	opt.io_error_limit = 100;
	blockstart = 0;
	blockcount = 0;
	tommy_list_init(&filterlist_file);
	tommy_list_init(&filterlist_disk);
	period = 1000;
	filter_missing = 0;
	filter_error = 0;
	plan = SCRUB_AUTO;
	olderthan = SCRUB_AUTO;
	import_timestamp = 0;
	import_content = 0;
	log_file = 0;
	lock = 0;
	gen_conf = 0;
	speedtest = 0;
	run = 0;

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
			pathimport(conf, sizeof(conf), optarg);
			break;
		case 'f' : {
			struct snapraid_filter* filter = filter_alloc_file(1, optarg);
			if (!filter) {
				/* LCOV_EXCL_START */
				log_fatal("Invalid filter specification '%s'\n", optarg);
				log_fatal("Filters using relative paths are not supported. Ensure to add an initial slash\n");
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			tommy_list_insert_tail(&filterlist_file, &filter->node, filter);
		} break;
		case 'd' : {
			struct snapraid_filter* filter = filter_alloc_disk(1, optarg);
			if (!filter) {
				/* LCOV_EXCL_START */
				log_fatal("Invalid filter specification '%s'\n", optarg);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			tommy_list_insert_tail(&filterlist_disk, &filter->node, filter);
		} break;
		case 'm' :
			filter_missing = 1;
			opt.expected_missing = 1;
			break;
		case 'e' :
			/* when processing only error, we filter both files and blocks */
			/* and we apply fixes only to synced ones */
			filter_error = 1;
			opt.badonly = 1;
			opt.syncedonly = 1;
			break;
		case 'p' :
			if (strcmp(optarg, "bad") == 0) {
				plan = SCRUB_BAD;
			} else if (strcmp(optarg, "new") == 0) {
				plan = SCRUB_NEW;
			} else if (strcmp(optarg, "full") == 0) {
				plan = SCRUB_FULL;
			} else {
				plan = strtoul(optarg, &e, 10);
				if (!e || *e || plan > 100) {
					/* LCOV_EXCL_START */
					log_fatal("Invalid plan/percentage '%s'\n", optarg);
					exit(EXIT_FAILURE);
					/* LCOV_EXCL_STOP */
				}
			}
			break;
		case 'o' :
			olderthan = strtoul(optarg, &e, 10);
			if (!e || *e || olderthan > 1000) {
				/* LCOV_EXCL_START */
				log_fatal("Invalid number of days '%s'\n", optarg);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			break;
		case 'S' :
			blockstart = strtoul(optarg, &e, 0);
			if (!e || *e) {
				/* LCOV_EXCL_START */
				log_fatal("Invalid start position '%s'\n", optarg);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			break;
		case 'B' :
			blockcount = strtoul(optarg, &e, 0);
			if (!e || *e) {
				/* LCOV_EXCL_START */
				log_fatal("Invalid count number '%s'\n", optarg);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			break;
		case 'L' :
			opt.io_error_limit = strtoul(optarg, &e, 0);
			if (!e || *e) {
				/* LCOV_EXCL_START */
				log_fatal("Invalid error limit number '%s'\n", optarg);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			break;
		case 'i' :
			if (import_timestamp) {
				/* LCOV_EXCL_START */
				log_fatal("Import directory '%s' already specified as '%s'\n", optarg, import_timestamp);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			import_timestamp = optarg;
			break;
		case OPT_TEST_IMPORT_CONTENT :
			if (import_content) {
				/* LCOV_EXCL_START */
				log_fatal("Import directory '%s' already specified as '%s'\n", optarg, import_content);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			import_content = optarg;
			break;
		case 'l' :
			if (log_file) {
				/* LCOV_EXCL_START */
				log_fatal("Log file '%s' already specified as '%s'\n", optarg, log_file);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			log_file = optarg;
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
			opt.force_nocopy = 1;
			break;
		case 'F' :
			opt.force_full = 1;
			break;
		case 'R' :
			opt.force_realloc = 1;
			break;
		case 'a' :
			opt.auditonly = 1;
			break;
		case 'h' :
			opt.prehash = 1;
			break;
		case 'v' :
			++msg_level;
			break;
		case 'q' :
			--msg_level;
			break;
		case 'G' :
			opt.gui = 1;
			break;
		case 'H' :
			usage();
			exit(EXIT_SUCCESS);
		case 'V' :
			version();
			exit(EXIT_SUCCESS);
		case 'T' :
			speedtest = 1;
			break;
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
		case OPT_TEST_SKIP_DEVICE :
			opt.skip_device = 1;
			period = 50; /* reduce period of the speed test */
			break;
		case OPT_TEST_SKIP_CONTENT_CHECK :
			opt.skip_content_check = 1;
			break;
		case OPT_TEST_SKIP_PARITY_ACCESS :
			opt.skip_parity_access = 1;
			break;
		case OPT_TEST_SKIP_DISK_ACCESS :
			opt.skip_disk_access = 1;
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
		case OPT_TEST_FORCE_SCRUB_AT :
			opt.force_scrub_at = atoi(optarg);
			break;
		case OPT_TEST_FORCE_SCRUB_EVEN :
			opt.force_scrub_even = 1;
			break;
		case OPT_TEST_FORCE_CONTENT_WRITE :
			opt.force_content_write = 1;
			break;
		case OPT_TEST_EXPECT_FAILURE :
			/* invert the exit codes */
			exit_success = 1;
			exit_failure = 0;
			break;
		case OPT_TEST_EXPECT_NEED_SYNC :
			/* invert the exit codes */
			exit_success = 1;
			exit_sync_needed = 0;
			break;
		case OPT_TEST_RUN :
			run = optarg;
			break;
		case OPT_TEST_FORCE_SCAN_WINFIND :
			opt.force_scan_winfind = 1;
			break;
		case OPT_TEST_FORCE_PROGRESS :
			opt.force_progress = 1;
			break;
		case OPT_TEST_FORCE_AUTOSAVE_AT :
			opt.force_autosave_at = atoi(optarg);
			break;
		case OPT_TEST_FAKE_DEVICE :
			opt.fake_device = 1;
			break;
		case OPT_NO_WARNINGS :
			opt.no_warnings = 1;
			break;
		case OPT_TEST_FAKE_UUID :
			opt.fake_uuid = 2;
			break;
		case OPT_TEST_MATCH_FIRST_UUID :
			opt.match_first_uuid = 1;
			break;
		case OPT_TEST_FORCE_PARITY_UPDATE :
			opt.force_parity_update = 1;
			break;
		case OPT_TEST_IO_CACHE :
			opt.io_cache = atoi(optarg);
			if (opt.io_cache != 1 && (opt.io_cache < IO_MIN || opt.io_cache > IO_MAX)) {
				/* LCOV_EXCL_START */
				log_fatal("The IO cache should be between %u and %u.\n", IO_MIN, IO_MAX);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			break;
		case OPT_TEST_IO_STATS :
			opt.force_stats = 1;
			break;
		case OPT_TEST_COND_SIGNAL_OUTSIDE :
#if HAVE_PTHREAD
			thread_cond_signal_outside = 1;
#endif
			break;
		case OPT_TEST_IO_ADVISE_NONE :
			opt.file_mode = ADVISE_NONE;
			break;
		case OPT_TEST_IO_ADVISE_SEQUENTIAL :
			opt.file_mode = ADVISE_SEQUENTIAL;
			break;
		case OPT_TEST_IO_ADVISE_FLUSH :
			opt.file_mode = ADVISE_FLUSH;
			break;
		case OPT_TEST_IO_ADVISE_FLUSH_WINDOW :
			opt.file_mode = ADVISE_FLUSH_WINDOW;
			break;
		case OPT_TEST_IO_ADVISE_DISCARD :
			opt.file_mode = ADVISE_DISCARD;
			break;
		case OPT_TEST_IO_ADVISE_DISCARD_WINDOW :
			opt.file_mode = ADVISE_DISCARD_WINDOW;
			break;
		case OPT_TEST_IO_ADVISE_DIRECT :
			opt.file_mode = ADVISE_DIRECT;
			break;
		case OPT_TEST_PARITY_LIMIT :
			opt.parity_limit_size = atoll(optarg);
			break;
		case OPT_TEST_SKIP_CONTENT_WRITE :
			opt.skip_content_write = 1;
			break;
		case OPT_TEST_SKIP_SPACE_HOLDER :
			opt.skip_space_holder = 1;
			break;
		case OPT_TEST_FORMAT :
			if (strcmp(optarg, "file") == 0)
				FMT_MODE = FMT_FILE;
			else if (strcmp(optarg, "disk") == 0)
				FMT_MODE = FMT_DISK;
			else if (strcmp(optarg, "path") == 0)
				FMT_MODE = FMT_PATH;
			else {
				/* LCOV_EXCL_START */
				log_fatal("Unknown format '%s'\n", optarg);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			break;
		default :
			/* LCOV_EXCL_START */
			log_fatal("Unknown option '%c'\n", (char)c);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}

	os_init(opt.force_scan_winfind);
	raid_init();
	crc32c_init();

	if (speedtest != 0) {
		speed(period, plan);
		os_done();
		exit(EXIT_SUCCESS);
	}

	if (gen_conf != 0) {
		generate_configuration(gen_conf);
		os_done();
		exit(EXIT_SUCCESS);
	}

	if (optind + 1 != argc) {
		/* LCOV_EXCL_START */
		usage();
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
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
	} else if (strcmp(argv[optind], "test-dry") == 0) {
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
	} else if (strcmp(argv[optind], "test-read") == 0) {
		operation = OPERATION_READ;
	} else if (strcmp(argv[optind], "touch") == 0) {
		operation = OPERATION_TOUCH;
	} else if (strcmp(argv[optind], "up") == 0) {
		operation = OPERATION_SPINUP;
	} else if (strcmp(argv[optind], "down") == 0) {
		operation = OPERATION_SPINDOWN;
	} else if (strcmp(argv[optind], "devices") == 0) {
		operation = OPERATION_DEVICES;
	} else if (strcmp(argv[optind], "smart") == 0) {
		operation = OPERATION_SMART;
	} else {
		/* LCOV_EXCL_START */
		log_fatal("Unknown command '%s'\n", argv[optind]);
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* check options compatibility */
	switch (operation) {
	case OPERATION_CHECK :
		break;
	default :
		if (opt.auditonly) {
			/* LCOV_EXCL_START */
			log_fatal("You cannot use -a, --audit-only with the '%s' command\n", command);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}

	switch (operation) {
	case OPERATION_FIX :
	case OPERATION_CHECK :
	case OPERATION_SMART :
	case OPERATION_DEVICES :
	case OPERATION_SPINUP :
	case OPERATION_SPINDOWN :
		break;
	default :
		if (opt.force_device) {
			/* LCOV_EXCL_START */
			log_fatal("You cannot use -D, --force-device with the '%s' command\n", command);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}

	switch (operation) {
	case OPERATION_SYNC :
	case OPERATION_CHECK :
	case OPERATION_FIX :
		break;
	default :
		if (opt.force_nocopy) {
			/* LCOV_EXCL_START */
			log_fatal("You cannot use -N, --force-nocopy with the '%s' command\n", command);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}

	switch (operation) {
	case OPERATION_SYNC :
		break;
	default :
		if (opt.prehash) {
			/* LCOV_EXCL_START */
			log_fatal("You cannot use -h, --pre-hash with the '%s' command\n", command);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		if (opt.force_full) {
			/* LCOV_EXCL_START */
			log_fatal("You cannot use -F, --force-full with the '%s' command\n", command);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		if (opt.force_realloc) {
			/* LCOV_EXCL_START */
			log_fatal("You cannot use -R, --force-realloc with the '%s' command\n", command);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}

	if (opt.force_full && opt.force_nocopy) {
		/* LCOV_EXCL_START */
		log_fatal("You cannot use the -F, --force-full and -N, --force-nocopy options at the same time\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	if (opt.force_realloc && opt.force_nocopy) {
		/* LCOV_EXCL_START */
		log_fatal("You cannot use the -R, --force-realloc and -N, --force-nocopy options at the same time\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	if (opt.force_realloc && opt.force_full) {
		/* LCOV_EXCL_START */
		log_fatal("You cannot use the -R, --force-realloc and -F, --force-full options at the same time\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	if (opt.prehash && opt.force_nocopy) {
		/* LCOV_EXCL_START */
		log_fatal("You cannot use the -h, --pre-hash and -N, --force-nocopy options at the same time\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	switch (operation) {
	case OPERATION_CHECK :
	case OPERATION_FIX :
	case OPERATION_DRY :
		break;
	default :
		if (!tommy_list_empty(&filterlist_disk)) {
			/* LCOV_EXCL_START */
			log_fatal("You cannot use -d, --filter-disk with the '%s' command\n", command);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
		/* fallthrough */
	case OPERATION_SPINUP :
	case OPERATION_SPINDOWN :
		if (!tommy_list_empty(&filterlist_file)) {
			/* LCOV_EXCL_START */
			log_fatal("You cannot use -f, --filter with the '%s' command\n", command);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
		if (filter_missing != 0) {
			/* LCOV_EXCL_START */
			log_fatal("You cannot use -m, --filter-missing with the '%s' command\n", command);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
		if (filter_error != 0) {
			/* LCOV_EXCL_START */
			log_fatal("You cannot use -e, --filter-error with the '%s' command\n", command);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}

	/* errors must be always fixed on all disks */
	/* because we don't keep the information on what disk is the error */
	if (filter_error != 0 && !tommy_list_empty(&filterlist_disk)) {
		/* LCOV_EXCL_START */
		log_fatal("You cannot use -e, --filter-error and -d, --filter-disk at the same time\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	switch (operation) {
	case OPERATION_CHECK :
	case OPERATION_FIX :
		break;
	default :
		if (import_timestamp != 0 || import_content != 0) {
			/* LCOV_EXCL_START */
			log_fatal("You cannot import with the '%s' command\n", command);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}

	switch (operation) {
	case OPERATION_LIST :
	case OPERATION_DUP :
	case OPERATION_STATUS :
	case OPERATION_REWRITE :
	case OPERATION_READ :
	case OPERATION_REHASH :
	case OPERATION_SPINUP : /* we want to do it in different threads to avoid blocking */
		/* avoid to check and access data disks if not needed */
		opt.skip_disk_access = 1;
		break;
	}

	switch (operation) {
	case OPERATION_DIFF :
	case OPERATION_LIST :
	case OPERATION_DUP :
	case OPERATION_POOL :
	case OPERATION_STATUS :
	case OPERATION_REWRITE :
	case OPERATION_READ :
	case OPERATION_REHASH :
	case OPERATION_TOUCH :
	case OPERATION_SPINUP : /* we want to do it in different threads to avoid blocking */
		/* avoid to check and access parity disks if not needed */
		opt.skip_parity_access = 1;
		break;
	}

	switch (operation) {
	case OPERATION_FIX :
	case OPERATION_CHECK :
		/* avoid to stop processing if a content file is not accessible */
		opt.skip_content_access = 1;
		break;
	}

	switch (operation) {
	case OPERATION_DIFF :
	case OPERATION_LIST :
	case OPERATION_DUP :
	case OPERATION_POOL :
	case OPERATION_TOUCH :
	case OPERATION_SPINUP :
	case OPERATION_SPINDOWN :
	case OPERATION_DEVICES :
	case OPERATION_SMART :
		opt.skip_self = 1;
		break;
	}

	switch (operation) {
#if HAVE_DIRECT_IO
	case OPERATION_SYNC :
	case OPERATION_SCRUB :
	case OPERATION_DRY :
		break;
#endif
	default:
		/* we allow direct IO only on some commands */
		if (opt.file_mode == ADVISE_DIRECT)
			opt.file_mode = ADVISE_SEQUENTIAL;
		break;
	}

	switch (operation) {
	case OPERATION_DEVICES :
	case OPERATION_SMART :
		/* we may need to use these commands during operations */
		opt.skip_lock = 1;
		break;
	}

	switch (operation) {
	case OPERATION_SMART :
		/* allow to run without configuration file */
		opt.auto_conf = 1;
		break;
	}

	/* open the log file */
	log_open(log_file);

	/* print generic info into the log */
	t = time(0);
	tm = localtime(&t);
	log_tag("version:%s\n", PACKAGE_VERSION);
	log_tag("unixtime:%" PRIi64 "\n", (int64_t)t);
	if (tm) {
		char datetime[64];
		strftime(datetime, sizeof(datetime), "%Y-%m-%d %H:%M:%S", tm);
		log_tag("time:%s\n", datetime);
	}
	log_tag("command:%s\n", command);
	for (i = 0; i < argc; ++i)
		log_tag("argv:%u:%s\n", i, argv[i]);
	log_flush();

	if (!opt.skip_self)
		selftest();

	state_init(&state);

	/* read the configuration file */
	state_config(&state, conf, command, &opt, &filterlist_disk);

	/* set the raid mode */
	raid_mode(state.raid_mode);

#if HAVE_LOCKFILE
	/* create the lock file */
	if (!opt.skip_lock && state.lockfile[0]) {
		lock = lock_lock(state.lockfile);
		if (lock == -1) {
			/* LCOV_EXCL_START */
			if (errno != EWOULDBLOCK) {
				log_fatal("Error creating the lock file '%s'. %s.\n", state.lockfile, strerror(errno));
			} else {
				log_fatal("The lock file '%s' is already locked!\n", state.lockfile);
				log_fatal("SnapRAID is already in use!\n");
			}
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}
#else
	(void)lock;
#endif

	if (operation == OPERATION_DIFF) {
		state_read(&state);

		ret = state_diff(&state);

		/* abort if sync needed */
		if (ret > 0)
			exit(EXIT_SYNC_NEEDED);
	} else if (operation == OPERATION_SYNC) {

		/* in the next state read ensures to clear all the past hashes in case */
		/* we are reading from an incomplete sync */
		/* The indeterminate hash are only for CHG/DELETED blocks for which we don't */
		/* know if the previous interrupted sync was able to update or not the parity. */
		/* The sync process instead needs to trust this information because it's used */
		/* to avoid to recompute the parity if all the input are equals as before. */

		/* In these cases we don't know if the old state is still the one */
		/* stored inside the parity, because after an aborted sync, the parity */
		/* may be or may be not have been updated with the data that may be now */
		/* deleted. Then we reset the hash to a bogus value. */

		/* An example for CHG blocks is: */
		/* - One file is added creating a CHG block with ZERO state */
		/* - Sync aborted after updating the parity to the new state, */
		/*   but without saving the content file representing this new BLK state. */
		/* - File is now deleted after the aborted sync */
		/* - Sync again, deleting the blocks over the CHG ones */
		/*   with the hash of CHG blocks not representing the real parity state */

		/* An example for DELETED blocks is: */
		/* - One file is deleted creating DELETED blocks */
		/* - Sync aborted after, updating the parity to the new state, */
		/*   but without saving the content file representing this new EMPTY state. */
		/* - Another file is added again over the DELETE ones */
		/*   with the hash of DELETED blocks not representing the real parity state */
		state.clear_past_hash = 1;

		state_read(&state);

		state_scan(&state);

		/* refresh the size info before the content write */
		state_refresh(&state);

		memory();

		/* intercept signals while operating */
		signal_init();

		/* run a test command if required */
		if (run != 0) {
			ret = system(run); /* ignore error */
			if (ret != 0) {
				/* LCOV_EXCL_START */
				log_fatal("Error in running command '%s'.\n", run);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
		}

		/* waits some time to ensure that any concurrent modification done at the files, */
		/* using the same mtime read by the scan process, will be read by sync. */
		/* Note that any later modification done, potentially not read by sync, will have */
		/* a different mtime, and it will be synchronized at the next sync. */
		/* The worst case is the FAT file-system with a two seconds resolution for mtime. */
		/* If you don't use FAT, the wait is not needed, because most file-systems have now */
		/* at least microseconds resolution, but better to be safe. */
		if (!opt.skip_self)
			sleep(2);

		ret = state_sync(&state, blockstart, blockcount);

		/* save the new state if required */
		if (!opt.kill_after_sync) {
			if ((state.need_write || state.opt.force_content_write))
				state_write(&state);
		} else {
			log_fatal("WARNING! Skipped state write for --test-kill-after-sync option.\n");
		}

		/* abort if required */
		if (ret != 0) {
			/* LCOV_EXCL_START */
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	} else if (operation == OPERATION_DRY) {
		state_read(&state);

		/* filter */
		state_skip(&state);
		state_filter(&state, &filterlist_file, &filterlist_disk, filter_missing, filter_error);

		memory();

		/* intercept signals while operating */
		signal_init();

		state_dry(&state, blockstart, blockcount);
	} else if (operation == OPERATION_REHASH) {
		state_read(&state);

		/* intercept signals while operating */
		signal_init();

		state_rehash(&state);

		/* save the new state if required */
		if (state.need_write)
			state_write(&state);
	} else if (operation == OPERATION_SCRUB) {
		state_read(&state);

		memory();

		/* intercept signals while operating */
		signal_init();

		ret = state_scrub(&state, plan, olderthan);

		/* save the new state if required */
		if (state.need_write || state.opt.force_content_write)
			state_write(&state);

		/* abort if required */
		if (ret != 0) {
			/* LCOV_EXCL_START */
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	} else if (operation == OPERATION_REWRITE) {
		state_read(&state);

		/* intercept signals while operating */
		signal_init();

		state_write(&state);

		memory();
	} else if (operation == OPERATION_READ) {
		state_read(&state);

		memory();
	} else if (operation == OPERATION_TOUCH) {
		state_read(&state);

		state_touch(&state);

		/* intercept signals while operating */
		signal_init();

		state_write(&state);

		memory();
	} else if (operation == OPERATION_SPINUP) {
		state_device(&state, DEVICE_UP, &filterlist_disk);
	} else if (operation == OPERATION_SPINDOWN) {
		state_device(&state, DEVICE_DOWN, &filterlist_disk);
	} else if (operation == OPERATION_DEVICES) {
		state_device(&state, DEVICE_LIST, 0);
	} else if (operation == OPERATION_SMART) {
		state_device(&state, DEVICE_SMART, 0);
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

		/* if we are also trying to recover */
		if (!state.opt.auditonly) {
			/* import the user specified dirs */
			if (import_timestamp != 0)
				state_search(&state, import_timestamp);
			if (import_content != 0)
				state_import(&state, import_content);

			/* import from all the array */
			if (!state.opt.force_nocopy)
				state_search_array(&state);
		}

		/* filter */
		state_skip(&state);
		state_filter(&state, &filterlist_file, &filterlist_disk, filter_missing, filter_error);

		memory();

		/* intercept signals while operating */
		signal_init();

		if (operation == OPERATION_CHECK) {
			ret = state_check(&state, 0, blockstart, blockcount);
		} else { /* it's fix */
			ret = state_check(&state, 1, blockstart, blockcount);
		}

		/* abort if required */
		if (ret != 0) {
			/* LCOV_EXCL_START */
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}

	/* close log file */
	log_close(log_file);

#if HAVE_LOCKFILE
	if (!opt.skip_lock && state.lockfile[0]) {
		if (lock_unlock(lock) == -1) {
			/* LCOV_EXCL_START */
			log_fatal("Error closing the lock file '%s'. %s.\n", state.lockfile, strerror(errno));
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}
#endif

	state_done(&state);
	tommy_list_foreach(&filterlist_file, (tommy_foreach_func*)filter_free);
	tommy_list_foreach(&filterlist_disk, (tommy_foreach_func*)filter_free);

	os_done();
	lock_done();

	return EXIT_SUCCESS;
}

