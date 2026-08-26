// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2011 Andrea Mazzoleni

#ifndef __SNAPRAID_H
#define __SNAPRAID_H

/****************************************************************************/
/* snapraid */

void speed(int period, int disks_number, int blocks_size);

/**
 * Runs the extensive test suite, including all unit tests and selftest.
 */
void test(int argc, char* argv[]);

/**
 * Runs a fast self-test for functionality that could be miscompiled,
 * primarily to verify inline assembly clobber and ABI requirements.
 */
void selftest(void);

int snapraid_main(int argc, char* argv[]);

#endif

