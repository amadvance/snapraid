// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2011 Andrea Mazzoleni

#ifndef __SNAPRAID_H
#define __SNAPRAID_H

/****************************************************************************/
/* snapraid */

void speed(int period, int disks_number, int blocks_size);
void selftest(void);
int snapraid_main(int argc, char* argv[]);

#endif

