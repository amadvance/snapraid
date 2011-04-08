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

#include "util.h"
#include "elem.h"
#include "state.h"

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

/**
 * Test vectors for MD5 from RFC1321.
 *
 * MD5("") = d41d8cd98f00b204e9800998ecf8427e
 * MD5("a") = 0cc175b9c0f1b6a831c399e269772661
 * MD5("abc") = 900150983cd24fb0d6963f7d28e17f72
 * MD5("message digest") = f96b697d7cb7938d525a2f31aaf161d0
 * MD5("abcdefghijklmnopqrstuvwxyz") = c3fcd3d76192e4007dfb496cca67e13b
 * MD5("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789") = d174ab98d277d9f5a5611c2c9f419d9f
 * MD5("12345678901234567890123456789012345678901234567890123456789012345678901234567890") = 57edf4a22be3c955ac49da2e2107b67a
 */
struct test_vector {
	const char* data;
	unsigned char digest[MD5_SIZE];
} TEST[] = {
{ "", { 0xd4, 0x1d, 0x8c, 0xd9, 0x8f, 0x00, 0xb2, 0x04, 0xe9, 0x80, 0x09, 0x98, 0xec, 0xf8, 0x42, 0x7e } },
{ "a", { 0x0c, 0xc1, 0x75, 0xb9, 0xc0, 0xf1, 0xb6, 0xa8, 0x31, 0xc3, 0x99, 0xe2, 0x69, 0x77, 0x26, 0x61 } },
{ "abc", { 0x90, 0x01, 0x50, 0x98, 0x3c, 0xd2, 0x4f, 0xb0, 0xd6, 0x96, 0x3f, 0x7d, 0x28, 0xe1, 0x7f, 0x72 } },
{ "message digest", { 0xf9, 0x6b, 0x69, 0x7d, 0x7c, 0xb7, 0x93, 0x8d, 0x52, 0x5a, 0x2f, 0x31, 0xaa, 0xf1, 0x61, 0xd0 } },
{ "abcdefghijklmnopqrstuvwxyz", { 0xc3, 0xfc, 0xd3, 0xd7, 0x61, 0x92, 0xe4, 0x00, 0x7d, 0xfb, 0x49, 0x6c, 0xca, 0x67, 0xe1, 0x3b } },
{ "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789", { 0xd1, 0x74, 0xab, 0x98, 0xd2, 0x77, 0xd9, 0xf5, 0xa5, 0x61, 0x1c, 0x2c, 0x9f, 0x41, 0x9d, 0x9f } },
{ "12345678901234567890123456789012345678901234567890123456789012345678901234567890", { 0x57, 0xed, 0xf4, 0xa2, 0x2b, 0xe3, 0xc9, 0x55, 0xac, 0x49, 0xda, 0x2e, 0x21, 0x07, 0xb6, 0x7a, } },
{ 0, { 0 } }
};

void selftest(void)
{
	unsigned i;

	printf("Self test...\n");

	/* large file check */
	if (sizeof(off_t) < sizeof(uint64_t)) {
		fprintf(stderr, "Missing support for large files\n");
		exit(EXIT_FAILURE);
	}

	/* test vectors for MD5. It could be dynamically linked, so we have to test it at runtime */
	for(i=0;TEST[i].data;++i) {
		unsigned char digest[MD5_SIZE];
		memmd5(digest, TEST[i].data, strlen(TEST[i].data));
		if (memcmp(digest, TEST[i].digest, MD5_SIZE) != 0) {
			fprintf(stderr, "Failed MD5 test vector\n");
			exit(EXIT_FAILURE);
		}
	}
}

void speed(void)
{
	struct timeval start;
	struct timeval stop;
	int64_t ds;
	int64_t dt;
	unsigned i;
	unsigned char digest[MD5_SIZE];

	unsigned count = 10000;
	unsigned blocksize = 256 * 1024;
	void* block = malloc_nofail(blocksize);

	printf(PACKAGE " v" VERSION " by Andrea Mazzoleni, " PACKAGE_URL "\n");
	printf("Hashing speed test...\n");

	gettimeofday(&start, 0);
	for(i=0;i<count;++i)
		memmd5(digest, block, blocksize);
	gettimeofday(&stop, 0);

	ds = blocksize * (int64_t)count;
	dt = (int64_t)1000000 * (stop.tv_sec - start.tv_sec) + (stop.tv_usec - start.tv_usec);

	printf("%llu [MB/s]\n", ds / dt);

	free(block);
}

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
	printf("  " SWITCH_GETOPT_LONG("-T, --speed-test    ", "-T") "  Speed test of the MD5 implementation\n");
	printf("  " SWITCH_GETOPT_LONG("-v, --verbose       ", "-v") "  Verbose\n");
	printf("  " SWITCH_GETOPT_LONG("-h, --help          ", "-h") "  Help\n");
	printf("  " SWITCH_GETOPT_LONG("-V, --version       ", "-V") "  Version\n");
}

#if HAVE_GETOPT_LONG
struct option long_options[] = {
	{ "conf", 1, 0, 'c' },
	{ "filter", 1, 0, 'f' },
	{ "force-zero", 0, 0, 'Z' },
	{ "force-empty", 0, 0, 'E' },
	{ "start", 1, 0, 's' },
	{ "count", 1, 0, 't' },
	{ "speed-test", 0, 0, 'T' },
	{ "verbose", 0, 0, 'v' },
	{ "help", 0, 0, 'h' },
	{ "version", 0, 0, 'V' },
	{ 0, 0, 0, 0 }
};
#endif

#define OPTIONS "c:f:s:t:ZETvhV"

volatile int global_interrupt = 0;

void signal_handler(int signal)
{
	switch (signal) {
	case SIGINT :
		global_interrupt = 1;
		break;
	}
}

#define OPERATION_SYNC 0
#define OPERATION_CHECK 1
#define OPERATION_FIX 2
#define OPERATION_DRY 3

int main(int argc, char* argv[])
{
	int c;
	int verbose;
	int force_zero;
	int force_empty;
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
	force_zero = 0;
	force_empty = 0;
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
			struct snapraid_filter* filter = filter_alloc(optarg);
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
		case 'v' :
			verbose = 1;
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

	if (strcmp(argv[optind], "sync") == 0) {
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

	selftest();

	state_init(&state);

	state_config(&state, conf, verbose, force_zero, force_empty);

	if (operation == OPERATION_SYNC) {
		if (!tommy_list_empty(&filterlist)) {
			fprintf(stderr, "You cannot filter with the sync command\n");
			exit(EXIT_FAILURE);
		}

		state_read(&state);

		state_scan(&state);

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

