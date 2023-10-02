/*
 * libipmeta
 *
 * Alistair King and Ken Keys, CAIDA, UC San Diego
 * corsaro-info@caida.org
 *
 * Copyright (C) 2013-2020 The Regents of the University of California.
 *
 * This file is part of libipmeta.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

/* File containing macros that are helpful when parsing data files that
 * are in CSV format -- defined in one place, so they can be used by
 * multiple provider modules.
 *
 * File created by Shane Alcock, but the actual macros were written by
 * Alistair King.
 */

#ifndef __LIBIPMETA_PARSING_HELPERS_H
#define __LIBIPMETA_PARSING_HELPERS_H

#include <stdio.h>

#define startswith(buf, str)  (strncmp(buf, str "", sizeof(str)-1) == 0)

// Handle a column error.  (Requires at least 3 arguments.)
#define col_error(state, fmt, ...)                                             \
  do {                                                                         \
    ipmeta_log(__func__, "ERROR: " fmt " at %s:%d:%d",                         \
      __VA_ARGS__, (state)->current_filename, (state)->current_line,           \
      state->current_column % 1000);                                           \
    (state)->parser.status = CSV_EUSER;                                        \
    return;                                                                    \
  } while (0)

#define col_invalid(state, msg, tok)                                           \
  do {                                                                         \
    if (tok) {                                                                 \
      col_error((state), "%s \"%s\"", msg, (tok));                             \
    } else {                                                                   \
      col_error((state), "%s (empty)", msg);                                   \
    }                                                                          \
  } while (0)

// Handle a row error.  (Requires at least 3 arguments.)
#define row_error(state, fmt, ...)                                             \
  do {                                                                         \
    ipmeta_log(__func__, "ERROR: " fmt " at %s:%d",                            \
      __VA_ARGS__, (state)->current_filename, (state)->current_line);          \
    (state)->parser.status = CSV_EUSER;                                        \
    return;                                                                    \
  } while (0)

#define check_column_count(state, endcol)                                      \
  if ((state)->current_column != (endcol)) {                                   \
    row_error((state), "Expected %d columns, found %d",                        \
      (endcol) % 1000, (state)->current_column % 1000);                        \
  } else (void)0 /* this makes a semicolon after it valid */

// strdup a non-NULL column value and handle error.  type must be row or col.
#define coldup(state, type, dst, src)                                          \
  if ((src) && !((dst) = strdup(src))) {                                       \
    type##_error((state), "Out of memory for \"%s\"", (src));                  \
  } else (void)0 /* this makes a semicolon after it valid */

extern const char *country_code_iso2[];
extern const char *country_continent[];

inline size_t calculate_country_count(void) {
    size_t size = 0;

    while (country_code_iso2[size]) {
        size ++;
    }
    return size;
}

#define COUNTRY_CNT (calculate_country_count())

#endif
