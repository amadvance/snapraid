/*
 * This file is part of the Advance project.
 *
 * Copyright (C) 2001, 2002 Andrea Mazzoleni
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details. 
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#if HAVE_CONFIG_H
#include <config.h>
#endif

#if !HAVE_GETOPT

/* This source is extracted from the DJGPP LIBC library */

#define unconst(var, type) ((type)(var))

/* Copyright (C) 1995 DJ Delorie, see COPYING.DJ for details */
#include <string.h>
#include <stdio.h>

int opterr = 1, optind = 1, optopt = 0;
char *optarg = 0;

#define BADCH (int)'?'
#define EMSG ""

int getopt(int nargc, char *const nargv[], const char *ostr)
{
  static const char *place = EMSG; /* option letter processing */
  char *oli; /* option letter list index */
  char *p;

  if (!*place)
  {
    if (optind >= nargc || *(place = nargv[optind]) != '-')
    {
      place = EMSG;
      return(EOF);
    }
    if (place[1] && *++place == '-')
    {
      ++optind;
      place = EMSG;
      return(EOF);
    }
  }

  if ((optopt = (int)*place++) == (int)':'
      || !(oli = strchr(ostr, optopt)))
  {
    /*
     * if the user didn't specify '-' as an option,
     * assume it means EOF.
     */
    if (optopt == (int)'-')
      return EOF;
    if (!*place)
      ++optind;
    if (opterr)
    {
      if (!(p = strrchr(*nargv, '/')))
        p = *nargv;
      else
        ++p;
      fprintf(stderr, "%s: illegal option -- %c\n", p, optopt);
    }
    return BADCH;
  }
  if (*++oli != ':')
  { /* don't need argument */
    optarg = NULL;
    if (!*place)
      ++optind;
  }
  else
  { /* need an argument */
    if (*place) /* no white space */
      optarg = unconst(place, char *);
    else if (nargc <= ++optind)
    { /* no arg */
      place = EMSG;
      if (!(p = strrchr(*nargv, '/')))
        p = *nargv;
      else
        ++p;
      if (opterr)
        fprintf(stderr, "%s: option requires an argument -- %c\n", p, optopt);
      return BADCH;
    }
    else /* white space */
      optarg = nargv[optind];
    place = EMSG;
    ++optind;
  }
  return optopt; /* dump back option letter */
}

#endif

