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

void version(void) {
	printf(PACKAGE " v" VERSION " by Andrea Mazzoleni, " PACKAGE_URL "\n");
}

void usage(void) {
	version();

	printf("Usage: " PACKAGE " [options]\n");
	printf("\n");
	printf("Options:\n");
	printf("  " SWITCH_GETOPT_LONG("-c, --conf FILE    ", "-c") "  Configuration file (default /etc/" PACKAGE ".conf)\n");
	printf("  " SWITCH_GETOPT_LONG("-s, --start BLK    ", "-s") "  Start from the specified block number\n");
	printf("  " SWITCH_GETOPT_LONG("-Z, --force-zero   ", "-Z") "  Force synching of files that get zero size\n");
	printf("  " SWITCH_GETOPT_LONG("-E, --force-empty  ", "-E") "  Force synching of disks that get empty\n");
	printf("  " SWITCH_GETOPT_LONG("-v, --verbose      ", "-v") "  Verbose\n");
	printf("  " SWITCH_GETOPT_LONG("-h, --help         ", "-h") "  Help\n");
	printf("  " SWITCH_GETOPT_LONG("-V, --version      ", "-V") "  Version\n");
}

#if HAVE_GETOPT_LONG
struct option long_options[] = {
	{ "conf", 1, 0, 'c' },
	{ "start", 1, 0, 's' },
	{ "force-zero", 0, 0, 'Z' },
	{ "force-empty", 0, 0, 'E' },
	{ "verbose", 0, 0, 'v' },
	{ "help", 0, 0, 'h' },
	{ "version", 0, 0, 'V' },
	{ 0, 0, 0, 0 }
};
#endif

#define OPTIONS "c:s:ZEvhV"

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
	int ret;

	/* intercept Ctrl+C */
	signal(SIGINT, &signal_handler);    
    
	/* defaults */
	conf = "/etc/" PACKAGE ".conf";
	verbose = 0;
	force_zero = 0;
	force_empty = 0;
	blockstart = 0;

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
		case 's' :
			if (stru32(optarg, &blockstart) != 0) {
				fprintf(stderr, "Invalid start position '%s'\n", optarg);
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
	} else {
		fprintf(stderr, "Unknown command '%s'\n", argv[optind]);
		exit(EXIT_FAILURE);
	}

	state_init(&state);

	state_config(&state, conf, verbose, force_zero, force_empty);

	if (operation == OPERATION_SYNC) {
		state_read(&state);

		state_scan(&state);

		ret = state_sync(&state, blockstart);

		/* save the new state if required */
		if (state.need_write)
			state_write(&state);

		/* abort if required by the sync command */
		if (ret == -1)
			exit(EXIT_FAILURE);
	} else {
		state_read(&state);

		state_check(&state, operation == OPERATION_FIX, blockstart);
	}

	state_done(&state);

	return EXIT_SUCCESS;
}

