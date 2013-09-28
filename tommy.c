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

#include "util.h" /* for safe_malloc */

/* redefine the malloc for tommy use */
#define tommy_malloc malloc_nofail
#define tommy_free free

/* include tommy source */
#include "tommylist.c"
#include "tommyhash.c"
#include "tommyhashdyn.c"
#include "tommyarray.c"
#include "tommyarrayof.c"
#include "tommyarrayblk.c"
#include "tommyarrayblkof.c"

