/*
 * libipmeta
 *
 * Alistair King, CAIDA, UC San Diego
 * corsaro-info@caida.org
 *
 * This file is adapted from corsaro_log.c from corsaro:
 *   http://www.caida.org/tools/measurement/corsaro/
 *
 * libipmeta_log and timestamp_str functions adapted from scamper:
 *   http://www.wand.net.nz/scamper
 *
 * Copyright (C) 2012 The Regents of the University of California.
 *
 * This file is part of libipmeta.
 *
 * libipmeta is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * libipmeta is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with libipmeta.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

#include <assert.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef HAVE_TIME_H
#include <time.h>
#endif

#include "utils.h"

#include "libipmeta_int.h"

static char *timestamp_str(char *buf, const size_t len)
{
  struct timeval tv;
  struct tm *tm;
  int ms;
  time_t t;

  buf[0] = '\0';
  gettimeofday_wrap(&tv);
  t = tv.tv_sec;
  if ((tm = localtime(&t)) == NULL)
    return buf;

  ms = tv.tv_usec / 1000;
  snprintf(buf, len, "[%02d:%02d:%02d:%03d] ", tm->tm_hour, tm->tm_min,
           tm->tm_sec, ms);

  return buf;
}

ATTR_FORMAT_PRINTF(2, 0)
static void generic_log(const char *func, const char *format, va_list ap)
{
  char message[512];
  char ts[16];
  char fs[64];

  assert(format != NULL);

  vsnprintf(message, sizeof(message), format, ap);

  timestamp_str(ts, sizeof(ts));

  if (func != NULL)
    snprintf(fs, sizeof(fs), "%s: ", func);
  else
    fs[0] = '\0';

  fprintf(stderr, "%s%s%s\n", ts, fs, message);
  fflush(stderr);
}

void ipmeta_log(const char *func, const char *format, ...)
{
  va_list ap;
  va_start(ap, format);
  generic_log(func, format, ap);
  va_end(ap);
}
