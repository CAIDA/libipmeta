/*
 * corsaro
 *
 * Alistair King, CAIDA, UC San Diego
 * corsaro-info@caida.org
 *
 * bytes_htons, bytes_htonl, gettimeofday_wrap, malloc_zero from scamper
 *   http://www.wand.net.nz/scamper
 *
 * timeval_subtract code from
 *   http://www.gnu.org/software/libc/manual/html_node/Elapsed-Time.html
 *
 * Copyright (C) 2012 The Regents of the University of California.
 *
 * This file is part of corsaro.
 *
 * corsaro is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * corsaro is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with corsaro.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

#include <arpa/inet.h>
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

void bytes_htons(uint8_t *bytes, uint16_t u16)
{
  uint16_t tmp = htons(u16);
  memcpy(bytes, &tmp, 2);
  return;
}

void bytes_htonl(uint8_t *bytes, uint32_t u32)
{
  uint32_t tmp = htonl(u32);
  memcpy(bytes, &tmp, 4);
  return;
}

void bytes_htonll(uint8_t *bytes, uint64_t u64)
{
  uint64_t tmp = htonll(u64);
  memcpy(bytes, &tmp, 8);
  return;
}

void gettimeofday_wrap(struct timeval *tv)
{
  struct timezone tz;
  gettimeofday(tv, &tz);
  return;
}

void *malloc_zero(const size_t size)
{
  void *ptr;
  if((ptr = malloc(size)) != NULL)
    {
      memset(ptr, 0, size);
    }
  return ptr;
}

int timeval_subtract (struct timeval *result,
		      const struct timeval *a, const struct timeval *b)
{
  struct timeval y = *b;

  /* Perform the carry for the later subtraction by updating y. */
  if (a->tv_usec < b->tv_usec) {
    int nsec = (b->tv_usec - a->tv_usec) / 1000000 + 1;
    y.tv_usec -= 1000000 * nsec;
    y.tv_sec += nsec;
  }
  if (a->tv_usec - b->tv_usec > 1000000) {
    int nsec = (a->tv_usec - b->tv_usec) / 1000000;
    y.tv_usec += 1000000 * nsec;
    y.tv_sec -= nsec;
  }

  /* Compute the time remaining to wait.
     tv_usec is certainly positive. */
  result->tv_sec = a->tv_sec - y.tv_sec;
  result->tv_usec = a->tv_usec - y.tv_usec;

  /* Return 1 if result is negative. */
  return a->tv_sec < y.tv_sec;
}

void chomp(char *line)
{
  char *newln;
  if((newln = strchr(line, '\n')) != NULL)
    {
      *newln = '\0';
    }
}

off_t wandio_fgets(io_t *file, void *buffer, off_t len, int chomp)
{
  assert(file != NULL);

  char cbuf;
  int rval;
  int i;
  int done = 0;

  if(buffer == NULL || len <= 0)
    {
      return 0;
    }

  for(i=0; !done && i < len-1; i++)
    {
      if((rval = wandio_read(file, &cbuf, 1)) < 0)
	{
	  return rval;
	}
      if(rval == 0)
	{
	  done = 1;
	  i--;
	}
      else
	{
	  ((char*)buffer)[i] = cbuf;
	  if(cbuf == '\n')
	    {
	      if(chomp != 0)
		{
		  ((char*)buffer)[i] = '\0';
		}
	      done = 1;
	    }
	}
    }

  ((char*)buffer)[i] = '\0';
  return i;
}

#define WANDIO_ZLIB_SUFFIX ".gz"
#define WANDIO_BZ2_SUFFIX ".bz2"

int wandio_detect_compression_type(const char *filename)
{
  const char *ptr = filename;

  int len = strlen(filename);

  if(len >= strlen(WANDIO_ZLIB_SUFFIX))
    {
      /* check for a .gz extension */
      ptr += (len - strlen(WANDIO_ZLIB_SUFFIX));
      if(strcmp(ptr, WANDIO_ZLIB_SUFFIX) == 0)
	{
	  return WANDIO_COMPRESS_ZLIB;
	}

      ptr = filename;
    }

  if(len >= strlen(WANDIO_BZ2_SUFFIX))
    {
      /* check for a .bz2 extension */
      ptr += (len - strlen(WANDIO_BZ2_SUFFIX));
      if(strcmp(ptr, WANDIO_BZ2_SUFFIX) == 0)
	{
	  return WANDIO_COMPRESS_BZ2;
	}
    }

  /* this is a suffix we don't know. don't compress */
  return WANDIO_COMPRESS_NONE;
}

static size_t wiovprintf(iow_t *io, const char *fmt, va_list args)
{
  char *buf;
  size_t len;
  int ret;

  if ((ret = vasprintf(&buf, fmt, args)) < 0)
    return ret;
  len = strlen(buf);
  len = len == (unsigned)len ? (size_t)wandio_wwrite(io, buf,
						     (unsigned)len) : 0;
  free(buf);
  return len;
}

inline off_t wandio_vprintf(iow_t *file, const char *format, va_list args)
{
  assert(file != NULL);

  return wiovprintf(file, format, args);
}

inline off_t wandio_printf(iow_t *file, const char *format, ...)
{
  va_list ap;

  va_start(ap, format);
  return wandio_vprintf(file, format, ap);
  va_end(ap);
}
