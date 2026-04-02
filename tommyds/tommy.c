// SPDX-License-Identifier: BSD-2-Clause
// Copyright (C) 2010 Andrea Mazzoleni

/* redefine the malloc for tommy use */
#define tommy_malloc malloc_nofail
#define tommy_calloc calloc_nofail
#define tommy_free free

#include "cmdline/portable.h"
#include "cmdline/support.h" /* for malloc/calloc_nofail() */

#include "tommyhash.c"
#include "tommyarray.c"
#include "tommyarrayblkof.c"
#include "tommylist.c"
#include "tommytree.c"
#include "tommyhashdyn.c"

