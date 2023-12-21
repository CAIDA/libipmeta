/*
 * libipmeta
 *
 * Alistair King, CAIDA, UC San Diego
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

#include "libipmeta_int.h"
#include "config.h"

#include <arpa/inet.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "wandio.h"

#include "khash.h"
#include "utils.h"
#include "csv.h"
#include "ipvx_utils.h"

#include "ipmeta_ds.h"
#include "ipmeta_provider_netacq_edge.h"

#define PROVIDER_NAME "netacq-edge"

#define STATE(provname) (IPMETA_PROVIDER_STATE(netacq_edge, provname))

#define BUFFER_LEN 1024

#define POLYGON_FILE_CNT_MAX 8 /* increase as you like */

#pragma GCC diagnostic ignored "-Wtrigraphs"

/** The basic fields that every instance of this provider have in common */
static ipmeta_provider_t ipmeta_provider_netacq_edge = {
  IPMETA_PROVIDER_NETACQ_EDGE, PROVIDER_NAME,
  IPMETA_PROVIDER_GENERATE_PTRS(netacq_edge)};

/** Maps a single Net Acuity location id to a single Polygon id */
typedef struct na_to_polygon {
  /** Net Acuity location id */
  uint32_t na_loc_id;

  /** Polygon ids (index <==> polygon table id) */
  uint32_t polygon_ids[POLYGON_FILE_CNT_MAX];

} na_to_polygon_t;

/** Holds the state for an instance of this provider */
typedef struct ipmeta_provider_netacq_edge_state {

  /* info extracted from args */
  char *locations_file;
  char *blocks_file;
  char *ipv6_file;
  char *region_file;
  char *country_file;
  char *polygon_files[POLYGON_FILE_CNT_MAX];
  int polygon_files_cnt;
  char *na_to_polygon_file;

  /* array of region decode info */
  ipmeta_provider_netacq_edge_region_t **regions;
  int regions_cnt;

  /* array of country decode info */
  ipmeta_provider_netacq_edge_country_t **countries;
  int countries_cnt;

  /* array of polygon decode info */
  ipmeta_polygon_table_t **polygon_tables;
  int polygon_tables_cnt;

  /* temp mapping array of netacq2polygon info (one per locid) */
  na_to_polygon_t **na_to_polygons;
  int na_to_polygons_cnt;

  /* State for CSV parser */
  struct csv_parser parser;
  int current_line;
  int current_column; // column ID (not column number)
  ipmeta_record_t tmp_record;
  uint32_t loc_id;
  uint32_t max_loc_id;
  ipvx_prefix_t block_lower;
  ipvx_prefix_t block_upper;
  ipmeta_provider_netacq_edge_region_t tmp_region;
  int tmp_region_ignore;
  ipmeta_provider_netacq_edge_country_t tmp_country;
  int tmp_country_ignore;
  ipmeta_polygon_t tmp_polygon; /* to be inserted in the current poly table */
  na_to_polygon_t tmp_na_to_polygon;
  int tmp_na_col_to_tbl[POLYGON_FILE_CNT_MAX];

} ipmeta_provider_netacq_edge_state_t;

/** Provides a mapping from the integer continent code to the 2 character
    strings that we use in libipmeta */
static const char *continent_strings[] = {
  "??", /* 0 */
  "AF", /* 1 */
  "AN", /* 2 */
  "OC", /* 3 */
  "AS", /* 4 */
  "EU", /* 5 */
  "NA", /* 6 */
  "SA", /* 7 */
};

#define CONTINENT_MAX 7

// Column ids start at a multiple of 1000 and count up from there.  To convert
// a column id to a column number, we must mod by 1000.  This allows two
// different tables to have non-overlapping sets of column IDs so they can
// share some of the same column parsers.

/** The columns in the netacq_edge locations CSV file */
typedef enum locations_cols {
  LOCATION_COL_FIRSTCOL = 0,   ///< ID of first column in table
  LOCATION_COL_ID = 0,         ///< ID
  LOCATION_COL_CC,             ///< 2 Char Country Code
  LOCATION_COL_REGION,         ///< Region String
  LOCATION_COL_CITY,           ///< City String
  LOCATION_COL_POSTAL,         ///< Postal Code
  LOCATION_COL_LAT,            ///< Latitude
  LOCATION_COL_LONG,           ///< Longitude
  LOCATION_COL_METRO,          ///< Metro Code
  LOCATION_COL_AREACODES,      ///< Area Codes (plural) (not used)
  LOCATION_COL_CC3,            ///< 3 Char Country Code (not used)
  LOCATION_COL_CNTRYCODE,      ///< Country Code (not used)
  LOCATION_COL_RCODE,          ///< Region Code
  LOCATION_COL_CITYCODE,       ///< City Code (not used)
  LOCATION_COL_CONTCODE,       ///< Continent Code
  LOCATION_COL_INTERNAL,       ///< Internal Code (not used)
  LOCATION_COL_CONN,           ///< Connection Speed String
  LOCATION_COL_CNTRYCONF,      ///< Country-Conf ?? (not used)
  LOCATION_COL_REGCONF,        ///< Region-Conf ?? (not used)
  LOCATION_COL_CITYCONF,       ///< City-Conf ?? (not used)
  LOCATION_COL_POSTCONF,       ///< Postal-Conf ?? (not used)
  LOCATION_COL_GMTOFF,         ///< GMT-Offset (not used)
  LOCATION_COL_INDST,          ///< In DST (not used)
  LOCATION_COL_ENDCOL          ///< 1 past the last column ID in the table
} locations_cols_t;

/** The columns in the netacq_edge blocks CSV file */
typedef enum blocks_cols {
  BLOCKS_COL_FIRSTCOL = 1000,  ///< ID of first column in table
  BLOCKS_COL_STARTIP = 1000,   ///< Range Start IP, numeric
  BLOCKS_COL_ENDIP,            ///< Range End IP, numeric
  BLOCKS_COL_ID,               ///< ID
  BLOCKS_COL_ENDCOL            ///< 1 past the last column ID in the table
} blocks_cols_t;

/** The columns in the netacq_edge ipv6 CSV file */
typedef enum ipv6_cols {
  IPV6_COL_FIRSTCOL = 2000,    ///< ID of first column in table
  IPV6_COL_STARTIPTEXT = 2000, ///< Range Start IP addr, text
  IPV6_COL_ENDIPTEXT,          ///< Range End IP addr, text
  IPV6_COL_STARTIP,            ///< Range Start IP addr, numeric
  IPV6_COL_ENDIP,              ///< Range End IP addr, numeric
  IPV6_COL_CC,                 ///< 2 Char Country Code
  IPV6_COL_REGION,             ///< Region String
  IPV6_COL_CITY,               ///< City String
  IPV6_COL_LAT,                ///< Latitude
  IPV6_COL_LONG,               ///< Longitude
  IPV6_COL_POSTAL,             ///< Postal Code
  IPV6_COL_METRO,              ///< Metro Code
  IPV6_COL_AREACODES,          ///< Area Codes (plural) (not used)
  IPV6_COL_CC3,                ///< 3 Char Country Code (not used)
  IPV6_COL_CNTRYCODE,          ///< Country Code (not used)
  IPV6_COL_RCODE,              ///< Region Code
  IPV6_COL_CITYCODE,           ///< City Code (not used)
  IPV6_COL_CONTCODE,           ///< Continent Code
  IPV6_COL_INTERNAL,           ///< Internal Code (not used)
  IPV6_COL_CONN,               ///< Connection Speed String
  IPV6_COL_CNTRYCONF,          ///< Country-Conf ?? (not used)
  IPV6_COL_REGCONF,            ///< Region-Conf ?? (not used)
  IPV6_COL_CITYCONF,           ///< City-Conf ?? (not used)
  IPV6_COL_POSTCONF,           ///< Postal-Conf ?? (not used)
  IPV6_COL_GMTOFF,             ///< GMT-Offset (not used)
  IPV6_COL_INDST,              ///< In CST (not used)
  IPV6_COL_ENDCOL              ///< 1 past the last column ID in the table
} ipv6_cols_t;

/** The columns in the netacq region decode CSV file */
typedef enum region_cols {
  REGION_COL_FIRSTCOL = 0,     ///< ID of first column in table
  REGION_COL_COUNTRY = 0,      ///< Country
  REGION_COL_REGION,           ///< Region
  REGION_COL_DESC,             ///< Description
  REGION_COL_CODE,             ///< Region Code
  REGION_COL_ENDCOL            ///< 1 past the last column ID in the table
} region_cols_t;

/** The columns in the netacq country decode CSV file */
typedef enum country_cols {
  COUNTRY_COL_FIRSTCOL = 0,    ///< ID of first column in table
  COUNTRY_COL_ISO3 = 0,        ///< ISO 3 char
  COUNTRY_COL_ISO2,            ///< ISO 2 char
  COUNTRY_COL_NAME,            ///< Name
  COUNTRY_COL_REGIONS,         ///< Is region info?
  COUNTRY_COL_CONTCODE,        ///< Continent code
  COUNTRY_COL_CONTNAME,        ///< Continent name-abbr
  COUNTRY_COL_CODE,            ///< Country code (internal)
  COUNTRY_COL_ENDCOL           ///< 1 past the last column ID in the table
} country_cols_t;

/** The columns in the polygon decode CSV file */
typedef enum polygon_cols {
  POLYGON_COL_FIRSTCOL = 0,    ///< ID of first column in table
  POLYGON_COL_ID = 0,          ///< id
  POLYGON_COL_FQID,            ///< FQ-ID
  POLYGON_COL_NAME,            ///< Name
  POLYGON_COL_USERCODE,        ///< User ID
  POLYGON_COL_ENDCOL           ///< 1 past the last column ID in the table
} polygon_cols_t;

/** The columns in the netacq2polygon mapping CSV file */
typedef enum na_to_polygon_cols {
  NA_TO_POLYGON_COL_FIRSTCOL = 0,      ///< ID of first column in table
  NA_TO_POLYGON_COL_NETACQ_LOC_ID = 0, ///< netacq location id
  // Plus an arbitrary number of other polygon id columns
} na_to_polygon_cols_t;

/** The number of header rows in the netacq_edge CSV files */
#define HEADER_ROW_CNT 1

/** Print usage information to stderr */
static void usage(ipmeta_provider_t *provider)
{
  fprintf(stderr,
          "provider usage: %s [<options>]\n"
          "options:\n"
          "       -b <file>     ipv4 blocks file (must be used with -l)\n"
          "       -l <file>     ipv4 locations file (must be used with -b)\n"
          "       -6 <file>     ipv6 file\n"
          "       -c <file>     country decode file\n"
          "       -r <file>     region decode file\n"
          "       -p <file>     netacq2polygon mapping file\n"
          "       -t <file>     polygon table file\n"
          "                       (can be used up to %d times to specify "
          "multiple tables)\n",
          provider->name, POLYGON_FILE_CNT_MAX);
}

/** Parse the arguments given to the provider
 * @todo add option to choose datastructure
 */
static int parse_args(ipmeta_provider_t *provider, int argc, char **argv)
{
  ipmeta_provider_netacq_edge_state_t *state = STATE(provider);
  int opt;

  /* no args */
  if (argc == 0) {
    usage(provider);
    return -1;
  }

  /* NB: remember to reset optind to 1 before using getopt! */
  optind = 1;

  /* remember the argv strings DO NOT belong to us */

  while ((opt = getopt(argc, argv, "b:c:D:l:6:r:p:t:?")) >= 0) {
    switch (opt) {
    case 'b':
      state->blocks_file = strdup(optarg);
      break;

    case 'c':
      state->country_file = strdup(optarg);
      break;

    case 'D':
      fprintf(
        stderr,
        "WARNING: -D option is no longer supported by individual providers.\n");
      break;

    case 'l':
      state->locations_file = strdup(optarg);
      break;

    case '6':
      state->ipv6_file = strdup(optarg);
      break;

    case 'r':
      state->region_file = strdup(optarg);
      break;

    case 'p':
      state->na_to_polygon_file = strdup(optarg);
      break;

    case 't':
      state->polygon_files[state->polygon_files_cnt++] = strdup(optarg);
      break;

    case '?':
    case ':':
    default:
      usage(provider);
      return -1;
    }
  }

  if (optind != argc) {
    fprintf(stderr, "ERROR: extra arguments to %s\n", provider->name);
    usage(provider);
    return -1;
  }

  if (!((state->locations_file && state->blocks_file) || state->ipv6_file)) {
    fprintf(stderr, "ERROR: %s requires both '-b' and '-l', or '-6'\n",
        provider->name);
    usage(provider);
    return -1;
  }

  return 0;
}

static int read_netacq_edge_file(ipmeta_provider_t *provider, io_t *file,
    const char *label,
    void (*parse_cell)(void *, size_t, void *),
    void (*parse_row)(int, void *))
{
  ipmeta_provider_netacq_edge_state_t *state = STATE(provider);

  char buffer[BUFFER_LEN];
  int read = 0;

  csv_init(&(state->parser), CSV_STRICT | CSV_REPALL_NL | CSV_STRICT_FINI |
      CSV_APPEND_NULL | CSV_EMPTY_IS_NULL);

  while ((read = wandio_read(file, &buffer, BUFFER_LEN)) > 0) {
    if (csv_parse(&(state->parser), buffer, read, parse_cell, parse_row,
          provider) != read) {
      ipmeta_log(__func__, "Error parsing %s %s file", provider->name, label);
      ipmeta_log(__func__, "CSV Error: %s",
                 csv_strerror(csv_error(&(state->parser))));
      return -1;
    }
  }
  if (read < 0) {
    ipmeta_log(__func__, "Error reading %s file", label);
    return -1;
  }

  if (csv_fini(&(state->parser), parse_cell, parse_row, provider) != 0) {
    ipmeta_log(__func__, "Error parsing %s %s file", provider->name, label);
    ipmeta_log(__func__, "CSV Error: %s",
               csv_strerror(csv_error(&(state->parser))));
    return -1;
  }

  csv_free(&(state->parser));
  return 0;
}

#define log_invalid_col(state, label, tok) \
    ipmeta_log(__func__, "%s \"%s\" at %d:%d", label, tok ? tok : "(empty)",   \
        state->current_line, state->current_column % 1000)

#define check_column_count(state, label, endcol)                               \
  if ((state)->current_column != (endcol)) {                                   \
    ipmeta_log(__func__,                                                       \
      "ERROR in %s file, line %d: Expected %d columns, found %d", label,       \
      (state)->current_line, (endcol) % 1000, (state)->current_column % 1000); \
    (state)->parser.status = CSV_EUSER;                                        \
    return;                                                                    \
  } else (void)0 /* this is here to make a semicolon after it valid */

/* Parse a netacq_edge location cell or ipv6 cell */
static void parse_location_or_ipv6_cell(void *s, size_t i, void *data)
{
  ipmeta_provider_t *provider = (ipmeta_provider_t *)data;
  ipmeta_provider_netacq_edge_state_t *state = STATE(provider);
  ipmeta_record_t *tmp = &(state->tmp_record);
  char *tok = (char *)s;

  uint16_t tmp_continent;

  char *end;

  /* skip header */
  if (state->current_line < HEADER_ROW_CNT) {
    return;
  }

  /*
    ipmeta_log(__func__, "row: %d, column: %d, tok: %s",
    state->current_line,
    state->current_column % 1000,
    tok);
  */

  switch (state->current_column) {
  case LOCATION_COL_ID:
    /* init this record */
    tmp->id = strtoul(tok, &end, 10);
    if (end == tok || *end || errno == ERANGE) {
      log_invalid_col(state, "Invalid ID", tok);
      state->parser.status = CSV_EUSER;
      return;
    }
    break;

  case IPV6_COL_STARTIPTEXT:
    if (inet_pton(AF_INET6, tok, &state->block_lower.addr.v6) != 1) {
      log_invalid_col(state, "Invalid Start IP", tok);
      state->parser.status = CSV_EUSER;
    }
    break;

  case IPV6_COL_ENDIPTEXT:
    if (inet_pton(AF_INET6, tok, &state->block_upper.addr.v6) != 1) {
      log_invalid_col(state, "Invalid End IP", tok);
      state->parser.status = CSV_EUSER;
    }
    break;

  case IPV6_COL_STARTIP:
  case IPV6_COL_ENDIP:
    break; // not used

  case LOCATION_COL_CC:
  case IPV6_COL_CC:
    if (tok == NULL ||
        ((strlen(tok) != 2) && !(strlen(tok) == 1 && tok[0] == '?'))) {
      log_invalid_col(state, "Invalid country code", tok);
      state->parser.status = CSV_EUSER;
      return;
    }
    // ugly hax to s/uk/GB/ in country names
    if (tok[0] == 'u' && tok[1] == 'k') {
      tmp->country_code[0] = 'G';
      tmp->country_code[1] = 'B';
    }
    // s/**/??/ and s/?/??/
    else if (tok[0] == '?' || (tok[0] == '*' && tok[1] == '*')) {
      tmp->country_code[0] = '?';
      tmp->country_code[1] = '?';
      /* continent code will be 0 */
    } else {
      tmp->country_code[0] = (char)toupper(tok[0]);
      tmp->country_code[1] = (char)toupper(tok[1]);
    }
    break;

  case LOCATION_COL_REGION:
  case IPV6_COL_REGION:
    if (tok == NULL) {
      log_invalid_col(state, "Invalid region code", tok);
      state->parser.status = CSV_EUSER;
      return;
    }
    // s/*/?/g
    for (i = 0; i < strlen(tok); i++) {
      if (tok[i] == '*') {
        tok[i] = '?';
      }
    }
    if ((tmp->region = strdup(tok)) == NULL) {
      ipmeta_log(__func__, "Region code copy failed (%s)", tok);
      state->parser.status = CSV_EUSER;
      return;
    }
    break;

  case LOCATION_COL_CITY:
  case IPV6_COL_CITY:
    if (tok != NULL) {
      tmp->city = strndup(tok, strlen(tok));
    }
    break;

  case LOCATION_COL_POSTAL:
  case IPV6_COL_POSTAL:
    tmp->post_code = strndup(tok, strlen(tok));
    break;

  case LOCATION_COL_LAT:
  case IPV6_COL_LAT:
    tmp->latitude = strtod(tok, &end);
    if (end == tok || *end || tmp->latitude < -90 || tmp->latitude > 90) {
      log_invalid_col(state, "Invalid latitude", tok);
      state->parser.status = CSV_EUSER;
      return;
    }
    break;

  case LOCATION_COL_LONG:
  case IPV6_COL_LONG:
    /* longitude */
    tmp->longitude = strtod(tok, &end);
    if (end == tok || *end || tmp->longitude < -180 || tmp->longitude > 180) {
      log_invalid_col(state, "Invalid longitude", tok);
      state->parser.status = CSV_EUSER;
      return;
    }
    break;

  case LOCATION_COL_METRO:
  case IPV6_COL_METRO:
    /* metro code - whatever the heck that is */
    if (tok != NULL) {
      tmp->metro_code = strtoul(tok, &end, 10);
      if (end == tok || *end || errno == ERANGE) {
        log_invalid_col(state, "Invalid metro code", tok);
        state->parser.status = CSV_EUSER;
        return;
      }
    }
    break;

  case LOCATION_COL_AREACODES:
  case IPV6_COL_AREACODES:
  case LOCATION_COL_CC3:
  case IPV6_COL_CC3:
  case LOCATION_COL_CNTRYCODE:
  case IPV6_COL_CNTRYCODE:
    break;

  case LOCATION_COL_RCODE:
  case IPV6_COL_RCODE:
    tmp->region_code = strtoul(tok, &end, 10);
    if (end == tok || *end || errno == ERANGE) {
      log_invalid_col(state, "Invalid region code", tok);
      state->parser.status = CSV_EUSER;
      return;
    }
    break;

  case LOCATION_COL_CITYCODE:
  case IPV6_COL_CITYCODE:
    break;

  case LOCATION_COL_CONTCODE:
  case IPV6_COL_CONTCODE:
    if (tok != NULL) {
      tmp_continent = strtoul(tok, &end, 10);
      if (end == tok || *end || tmp_continent > CONTINENT_MAX) {
        log_invalid_col(state, "Invalid continent code", tok);
        state->parser.status = CSV_EUSER;
        return;
      }
      memcpy(tmp->continent_code, continent_strings[tmp_continent], 2);
    }
    break;

  case LOCATION_COL_INTERNAL:
  case IPV6_COL_INTERNAL:
    break;

  case LOCATION_COL_CONN:
  case IPV6_COL_CONN:
    if (tok != NULL) {
      tmp->conn_speed = strndup(tok, strlen(tok));
    }
    break;

  case LOCATION_COL_CNTRYCONF:
  case IPV6_COL_CNTRYCONF:
  case LOCATION_COL_REGCONF:
  case IPV6_COL_REGCONF:
  case LOCATION_COL_CITYCONF:
  case IPV6_COL_CITYCONF:
  case LOCATION_COL_POSTCONF:
  case IPV6_COL_POSTCONF:
  case LOCATION_COL_GMTOFF:
  case IPV6_COL_GMTOFF:
  case LOCATION_COL_INDST:
  case IPV6_COL_INDST:
    break;

  default:
    log_invalid_col(state, "Unexpected trailing column", tok);
    state->parser.status = CSV_EUSER;
    return;
  }

  /* move on to the next column */
  state->current_column++;
}

/** Handle an end-of-row event from the CSV parser */
static void parse_location_row(int c, void *data)
{
  ipmeta_provider_t *provider = (ipmeta_provider_t *)data;
  ipmeta_provider_netacq_edge_state_t *state = STATE(provider);
  ipmeta_record_t *record;
  int i;

  /* skip header */
  if (state->current_line < HEADER_ROW_CNT) {
    state->current_line++;
    return;
  }

  /* make sure we parsed exactly as many columns as we anticipated */
  check_column_count(state, "locations", LOCATION_COL_ENDCOL);

  if ((record = ipmeta_provider_init_record(provider, state->tmp_record.id)) ==
      NULL) {
    ipmeta_log(__func__, "ERROR: Could not initialize meta record");
    state->parser.status = CSV_EUSER;
    return;
  }

  state->tmp_record.source = provider->id;
  memcpy(record, &(state->tmp_record), sizeof(ipmeta_record_t));

  if (state->max_loc_id < state->tmp_record.id) {
    state->max_loc_id = state->tmp_record.id;
  }

  /* tag with polygon id, if there is a match in the netacq2polygons table */
  if ((record->id < state->na_to_polygons_cnt) &&
      state->na_to_polygons[record->id] != NULL) {
    if ((record->polygon_ids =
           malloc(sizeof(uint32_t) * state->polygon_tables_cnt)) == NULL) {
      ipmeta_log(__func__, "ERROR: Could not allocate polygon ids array");
      state->parser.status = CSV_EUSER;
      return;
    }

    for (i = 0; i < state->polygon_tables_cnt; i++) {
      record->polygon_ids[i] =
        state->na_to_polygons[record->id]->polygon_ids[i];
    }
    record->polygon_ids_cnt = state->polygon_tables_cnt;
  }

  /* done processing the line */

  /* increment the current line */
  state->current_line++;
  /* reset the current column */
  state->current_column = LOCATION_COL_FIRSTCOL;
  /* reset the temp record */
  memset(&(state->tmp_record), 0, sizeof(ipmeta_record_t));

  return;
}

/** Read a locations file */
static int read_locations(ipmeta_provider_t *provider, io_t *file)
{
  ipmeta_provider_netacq_edge_state_t *state = STATE(provider);

  /* reset the state variables before we start */
  state->current_column = LOCATION_COL_FIRSTCOL;
  state->current_line = 0;
  memset(&(state->tmp_record), 0, sizeof(ipmeta_record_t));
  assert(state->max_loc_id == 0);

  return read_netacq_edge_file(provider, file, "Location",
      parse_location_or_ipv6_cell, parse_location_row);
}

/** Parse a blocks cell */
static void parse_blocks_cell(void *s, size_t i, void *data)
{
  ipmeta_provider_t *provider = (ipmeta_provider_t *)data;
  ipmeta_provider_netacq_edge_state_t *state = STATE(provider);
  char *tok = (char *)s;
  char *end;

  /* skip header */
  if (state->current_line < HEADER_ROW_CNT) {
    return;
  }

  switch (state->current_column) {
  case BLOCKS_COL_STARTIP:
    /* start ip */
    state->block_lower.addr.v4.s_addr = htonl(strtoul(tok, &end, 10));
    if (end == tok || *end || errno == ERANGE) {
      log_invalid_col(state, "Invalid start IP", tok);
      state->parser.status = CSV_EUSER;
    }
    break;

  case BLOCKS_COL_ENDIP:
    /* end ip */
    state->block_upper.addr.v4.s_addr = htonl(strtoul(tok, &end, 10));
    if (end == tok || *end || errno == ERANGE) {
      log_invalid_col(state, "Invalid end IP", tok);
      state->parser.status = CSV_EUSER;
    }
    break;

  case BLOCKS_COL_ID:
    /* id */
    state->loc_id = strtoul(tok, &end, 10);
    if (end == tok || *end || errno == ERANGE) {
      log_invalid_col(state, "Invalid ID", tok);
      state->parser.status = CSV_EUSER;
    }
    break;

  default:
    log_invalid_col(state, "Unexpected trailing column", tok);
    state->parser.status = CSV_EUSER;
    break;
  }

  /* move on to the next column */
  state->current_column++;
}

static void parse_blocks_row(int c, void *data)
{
  ipmeta_provider_t *provider = (ipmeta_provider_t *)data;
  ipmeta_provider_netacq_edge_state_t *state = STATE(provider);

  ipvx_prefix_list_t *pfx_list, *pfx_node;
  ipmeta_record_t *record = NULL;

  if (state->current_line < HEADER_ROW_CNT) {
    state->current_line++;
    return;
  }

  /* done processing the line */

  /* make sure we parsed exactly as many columns as we anticipated */
  check_column_count(state, "blocks", BLOCKS_COL_ENDCOL);

  assert(state->loc_id > 0);

  /* convert the range to prefixes */
  if (ipvx_range_to_prefix(&state->block_lower, &state->block_upper, &pfx_list) !=
      0) {
    ipmeta_log(__func__, "ERROR: Could not convert range to pfxs");
    state->parser.status = CSV_EUSER;
    return;
  }
  assert(pfx_list != NULL);

  /* get the record from the provider */
  if ((record = ipmeta_provider_get_record(provider, state->loc_id)) ==
      NULL) {
    ipmeta_log(__func__, "ERROR: Missing record for location %d",
               state->loc_id);
    state->parser.status = CSV_EUSER;
    return;
  }

  /* iterate over and add each prefix to the trie */
  for (pfx_node = pfx_list; pfx_node; pfx_node = pfx_node->next) {
    ipvx_prefix_t *pfx = &pfx_node->prefix;
    if (ipmeta_provider_associate_record(provider, pfx->family,
          &pfx->addr.v4, pfx->masklen, record) != 0) {
      ipmeta_log(__func__, "ERROR: Failed to associate record");
      state->parser.status = CSV_EUSER;
      return;
    }
  }
  ipvx_prefix_list_free(pfx_list);

  /* increment the current line */
  state->current_line++;
  /* reset the current column */
  state->current_column = BLOCKS_COL_FIRSTCOL;
}

/** Read a blocks file  */
static int read_blocks(ipmeta_provider_t *provider, io_t *file)
{
  ipmeta_provider_netacq_edge_state_t *state = STATE(provider);

  /* reset the state variables before we start */
  state->current_column = BLOCKS_COL_FIRSTCOL;
  state->current_line = 0;
  state->loc_id = 0;
  state->block_lower.family = AF_INET;
  state->block_upper.family = AF_INET;
  state->block_lower.masklen = 32;
  state->block_upper.masklen = 32;

  return read_netacq_edge_file(provider, file, "Blocks",
      parse_blocks_cell, parse_blocks_row);
}

/** Handle an end-of-row event from the CSV parser */
static void parse_ipv6_row(int c, void *data)
{
  ipmeta_provider_t *provider = (ipmeta_provider_t *)data;
  ipmeta_provider_netacq_edge_state_t *state = STATE(provider);
  ipvx_prefix_list_t *pfx_list, *pfx_node;
  ipmeta_record_t *record = NULL;

  /* skip header */
  if (state->current_line < HEADER_ROW_CNT) {
    state->current_line++;
    return;
  }

  /* make sure we parsed exactly as many columns as we anticipated */
  check_column_count(state, "ipv6", IPV6_COL_ENDCOL);

  if ((record = ipmeta_provider_init_record(provider, state->loc_id)) ==
      NULL) {
    ipmeta_log(__func__, "ERROR: Could not initialize meta record");
    state->parser.status = CSV_EUSER;
    return;
  }

  state->tmp_record.source = provider->id;
  memcpy(record, &(state->tmp_record), sizeof(ipmeta_record_t));

  /* convert the range to prefixes */
  if (ipvx_range_to_prefix(&state->block_lower, &state->block_upper, &pfx_list) !=
      0) {
    ipmeta_log(__func__, "ERROR: Could not convert range to prefixes");
    state->parser.status = CSV_EUSER;
    return;
  }
  assert(pfx_list);

  /* iterate over and add each prefix to the trie */
  for (pfx_node = pfx_list; pfx_node; pfx_node = pfx_node->next) {
    ipvx_prefix_t *pfx = &pfx_node->prefix;
    if (ipmeta_provider_associate_record(provider, pfx->family,
          &pfx->addr.v6, pfx->masklen, record) != 0) {
      ipmeta_log(__func__, "ERROR: Failed to associate record");
      state->parser.status = CSV_EUSER;
      return;
    }
  }
  ipvx_prefix_list_free(pfx_list);

  /* reset the temp record */
  memset(&(state->tmp_record), 0, sizeof(ipmeta_record_t));

  state->loc_id++; // generate our own ids

  /* increment the current line */
  state->current_line++;
  /* reset the current column */
  state->current_column = IPV6_COL_FIRSTCOL;
}

/** Read a ipv6 file */
static int read_ipv6(ipmeta_provider_t *provider, io_t *file)
{
  ipmeta_provider_netacq_edge_state_t *state = STATE(provider);

  /* reset the state variables before we start */
  state->current_column = IPV6_COL_FIRSTCOL;
  state->current_line = 0;
  // The IPv4 loc_ids are (nearly) contiguous.  If we break this trend now,
  // khash's linear probing would perform very poorly due to clustering of the
  // default int_hash_func.  To maintain good performance, we must continue
  // the contiguous trend with our self-generated IPv6 loc_ids.
  state->loc_id = state->max_loc_id + 1;
  state->block_lower.family = AF_INET6;
  state->block_upper.family = AF_INET6;
  state->block_lower.masklen = 128;
  state->block_upper.masklen = 128;
  memset(&(state->tmp_record), 0, sizeof(ipmeta_record_t));

  return read_netacq_edge_file(provider, file, "IPv6",
      parse_location_or_ipv6_cell, parse_ipv6_row);
}

/** Parse a regions cell */
static void parse_regions_cell(void *s, size_t i, void *data)
{
  ipmeta_provider_t *provider = (ipmeta_provider_t *)data;
  ipmeta_provider_netacq_edge_state_t *state = STATE(provider);
  char *tok = (char *)s;
  char *end;

  int j;
  int len;

  /* skip header */
  if (state->current_line < HEADER_ROW_CNT) {
    return;
  }

  switch (state->current_column) {
  case REGION_COL_COUNTRY:
    /* country */
    if (tok == NULL) {
      log_invalid_col(state, "Invalid ISO country code", tok);
      state->parser.status = CSV_EUSER;
      return;
    }
    len = strnlen(tok, 3);
    for (j = 0; j < len; j++) {
      if (tok[j] == '*') {
        tok[j] = '?';
      }
      state->tmp_region.country_iso[j] = (char)toupper(tok[j]);
    }
    state->tmp_region.country_iso[len] = '\0';

    break;

  case REGION_COL_REGION:
    /* region */
    if (tok == NULL) {
      log_invalid_col(state, "Invalid ISO region code", tok);
      state->parser.status = CSV_EUSER;
      return;
    }

    /* remove the ***-? region and the ?-? regions */
    if (tok[0] == '?') {
      state->tmp_region_ignore = 1;
    }

    /* special check for "no region" */
    if (strncmp(tok, "no region", 9) == 0) {
      tok = "???";
    }

    len = strnlen(tok, 3);
    for (j = 0; j < len; j++) {
      if (tok[j] == '*') {
        tok[j] = '?';
      }
      state->tmp_region.region_iso[j] = (char)toupper(tok[j]);
    }
    state->tmp_region.region_iso[len] = '\0';
    break;

  case REGION_COL_DESC:
    /* description */
    if (tok == NULL) {
      log_invalid_col(state, "Invalid description code", tok);
      state->parser.status = CSV_EUSER;
      return;
    }
    state->tmp_region.name = strndup(tok, strlen(tok));
    break;

  case REGION_COL_CODE:
    state->tmp_region.code = strtoul(tok, &end, 10);
    if (end == tok || *end || errno == ERANGE) {
      log_invalid_col(state, "Invalid code", tok);
      state->parser.status = CSV_EUSER;
      return;
    }
    break;

  default:
    log_invalid_col(state, "Unexpected trailing column", tok);
    state->parser.status = CSV_EUSER;
    break;
  }

  /* move on to the next column */
  state->current_column++;
}

static void parse_regions_row(int c, void *data)
{
  ipmeta_provider_t *provider = (ipmeta_provider_t *)data;
  ipmeta_provider_netacq_edge_state_t *state = STATE(provider);

  ipmeta_provider_netacq_edge_region_t *region = NULL;

  if (state->current_line < HEADER_ROW_CNT) {
    state->current_line++;
    return;
  }

  /* done processing the line */

  /* make sure we parsed exactly as many columns as we anticipated */
  check_column_count(state, "regions", REGION_COL_ENDCOL);

  if (state->tmp_region_ignore == 0) {
    /* copy the tmp region structure into a new struct */
    if ((region = malloc(sizeof(ipmeta_provider_netacq_edge_region_t))) ==
        NULL) {
      ipmeta_log(__func__, "ERROR: Could not allocate memory for region");
      state->parser.status = CSV_EUSER;
      return;
    }
    memcpy(region, &(state->tmp_region),
           sizeof(ipmeta_provider_netacq_edge_region_t));

    /* make room in the region array for this region */
    if ((state->regions = realloc(
           state->regions, sizeof(ipmeta_provider_netacq_edge_region_t *) *
                             (state->regions_cnt + 1))) == NULL) {
      ipmeta_log(__func__, "ERROR: Could not allocate memory for region array");
      state->parser.status = CSV_EUSER;
      return;
    }
    /* now poke it in */
    state->regions[state->regions_cnt++] = region;
  }

  /* increment the current line */
  state->current_line++;
  /* reset the current column */
  state->current_column = REGION_COL_FIRSTCOL;
  /* reset the tmp region info */
  memset(&(state->tmp_region), 0, sizeof(ipmeta_provider_netacq_edge_region_t));
  state->tmp_region_ignore = 0;
}

/** Read a region decode file  */
static int read_regions(ipmeta_provider_t *provider, io_t *file)
{
  ipmeta_provider_netacq_edge_state_t *state = STATE(provider);

  /* reset the state variables before we start */
  state->current_column = REGION_COL_FIRSTCOL;
  state->current_line = 0;
  memset(&(state->tmp_region), 0, sizeof(ipmeta_provider_netacq_edge_region_t));
  state->tmp_region_ignore = 0;

  return read_netacq_edge_file(provider, file, "Regions",
      parse_regions_cell, parse_regions_row);
}

/** Parse a country cell */
static void parse_country_cell(void *s, size_t i, void *data)
{
  ipmeta_provider_t *provider = (ipmeta_provider_t *)data;
  ipmeta_provider_netacq_edge_state_t *state = STATE(provider);
  char *tok = (char *)s;
  char *end;

  int j;

  /* skip header */
  if (state->current_line < HEADER_ROW_CNT) {
    return;
  }

  switch (state->current_column) {
  case COUNTRY_COL_ISO3:
    /* country 3 char */
    if (tok == NULL) {
      log_invalid_col(state, "Invalid ISO-3 country code", tok);
      state->parser.status = CSV_EUSER;
      return;
    }
    if (tok[0] == '*' && tok[1] == '*' && tok[2] == '*') {
      state->tmp_country.iso3[0] = '?';
      state->tmp_country.iso3[1] = '?';
      state->tmp_country.iso3[2] = '?';
    } else if (tok[0] == '?') {
      /* we have manually deprecated the ? country */
      state->tmp_country_ignore = 1;
    } else {
      assert(strnlen(tok, 3) == 3);
      for (j = 0; j < 3; j++) {
        state->tmp_country.iso3[j] = (char)toupper(tok[j]);
      }
    }
    state->tmp_country.iso3[3] = '\0';
    break;

  case COUNTRY_COL_ISO2:
    /* country 2 char */
    if (tok == NULL) {
      log_invalid_col(state, "Invalid ISO-2 country code", tok);
      state->parser.status = CSV_EUSER;
      return;
    }
    // ugly hax to s/uk/GB/ in country names
    if (tok[0] == 'u' && tok[1] == 'k') {
      state->tmp_country.iso2[0] = 'G';
      state->tmp_country.iso2[1] = 'B';
    }
    // s/**/??/
    else if (tok[0] == '*' && tok[1] == '*') {
      state->tmp_country.iso2[0] = '?';
      state->tmp_country.iso2[1] = '?';
    }
    /* ignore ? country (not used, replaced by ??) */
    else if (tok[0] == '?') {
      state->tmp_country_ignore = 1;
    } else {
      state->tmp_country.iso2[0] = (char)toupper(tok[0]);
      state->tmp_country.iso2[1] = (char)toupper(tok[1]);
    }
    state->tmp_country.iso2[2] = '\0';
    break;

  case COUNTRY_COL_NAME:
    /* name */
    if (tok == NULL) {
      log_invalid_col(state, "Invalid country name", tok);
      state->parser.status = CSV_EUSER;
      return;
    }
    state->tmp_country.name = strndup(tok, strlen(tok));
    break;

  case COUNTRY_COL_REGIONS:
    state->tmp_country.regions = strtoul(tok, &end, 10);
    if (end == tok || *end || state->tmp_country.regions > 1) {
      log_invalid_col(state, "Invalid regions value", tok);
      state->parser.status = CSV_EUSER;
      return;
    }
    break;

  case COUNTRY_COL_CONTCODE:
    state->tmp_country.continent_code = strtoul(tok, &end, 10);
    if (end == tok || *end || errno == ERANGE) {
      log_invalid_col(state, "Invalid continent code", tok);
      state->parser.status = CSV_EUSER;
      return;
    }
    break;

  case COUNTRY_COL_CONTNAME:
    /* continent 2 char*/
    if (tok == NULL || strnlen(tok, 2) != 2) {
      log_invalid_col(state, "Invalid 2-char continent code", tok);
      state->parser.status = CSV_EUSER;
      return;
    }
    // s/**/??/
    if (tok[0] == '*' && tok[1] == '*') {
      tok[0] = '?';
      tok[1] = '?';
    }
    // s/au/oc/
    if (tok[0] == 'a' && tok[1] == 'u') {
      tok[0] = 'o';
      tok[1] = 'c';
    }
    state->tmp_country.continent[0] = (char)toupper(tok[0]);
    state->tmp_country.continent[1] = (char)toupper(tok[1]);
    break;

  case COUNTRY_COL_CODE:
    state->tmp_country.code = strtoul(tok, &end, 10);
    if (end == tok || *end || errno == ERANGE) {
      log_invalid_col(state, "Invalid code", tok);
      state->parser.status = CSV_EUSER;
      return;
    }
    break;

  default:
    log_invalid_col(state, "Unexpected trailing column", tok);
    state->parser.status = CSV_EUSER;
    break;
  }

  /* move on to the next column */
  state->current_column++;
}

static void parse_country_row(int c, void *data)
{
  ipmeta_provider_t *provider = (ipmeta_provider_t *)data;
  ipmeta_provider_netacq_edge_state_t *state = STATE(provider);

  ipmeta_provider_netacq_edge_country_t *country = NULL;

  if (state->current_line < HEADER_ROW_CNT) {
    state->current_line++;
    return;
  }

  /* done processing the line */

  /* make sure we parsed exactly as many columns as we anticipated */
  check_column_count(state, "country", COUNTRY_COL_ENDCOL);

  if (state->tmp_country_ignore == 0) {
    /* copy the tmp country structure into a new struct */
    if ((country = malloc(sizeof(ipmeta_provider_netacq_edge_country_t))) ==
        NULL) {
      ipmeta_log(__func__, "ERROR: Could not allocate memory for country");
      state->parser.status = CSV_EUSER;
      return;
    }
    memcpy(country, &(state->tmp_country),
           sizeof(ipmeta_provider_netacq_edge_country_t));

    /* make room in the country array for this country */
    if ((state->countries = realloc(
           state->countries, sizeof(ipmeta_provider_netacq_edge_country_t *) *
                               (state->countries_cnt + 1))) == NULL) {
      ipmeta_log(__func__,
                 "ERROR: Could not allocate memory for country array");
      state->parser.status = CSV_EUSER;
      return;
    }
    /* now poke it in */
    state->countries[state->countries_cnt++] = country;
  }

  /* increment the current line */
  state->current_line++;
  /* reset the current column */
  state->current_column = COUNTRY_COL_FIRSTCOL;
  /* reset the tmp country info */
  memset(&(state->tmp_country), 0,
         sizeof(ipmeta_provider_netacq_edge_country_t));
  state->tmp_country_ignore = 0;
}

/** Read a country decode file  */
static int read_countries(ipmeta_provider_t *provider, io_t *file)
{
  ipmeta_provider_netacq_edge_state_t *state = STATE(provider);

  /* reset the state variables before we start */
  state->current_column = COUNTRY_COL_FIRSTCOL;
  state->current_line = 0;
  memset(&(state->tmp_country), 0,
         sizeof(ipmeta_provider_netacq_edge_country_t));
  state->tmp_country_ignore = 0;

  return read_netacq_edge_file(provider, file, "Country",
      parse_country_cell, parse_country_row);
}

/* Parse a polygon decode table cell */
static void parse_polygons_cell(void *s, size_t i, void *data)
{
  ipmeta_provider_t *provider = (ipmeta_provider_t *)data;
  ipmeta_provider_netacq_edge_state_t *state = STATE(provider);
  char *tok = (char *)s;
  char *end;
  char *sfx;

  ipmeta_polygon_table_t *new_table;

  /* process the header row, creating polygon table objects */
  if (state->current_line == 0) {
    if (state->current_column == POLYGON_COL_FIRSTCOL) {
      /* create a new polygon table */
      if ((new_table = malloc_zero(sizeof(ipmeta_polygon_table_t))) == NULL) {
        ipmeta_log(__func__, "Cannot allocate polygon table (%s)", tok);
        state->parser.status = CSV_EUSER;
        return;
      }
      /* we extract the table name from this column name */
      new_table->id = state->polygon_tables_cnt;

      /* chop off the -id suffix */
      if ((sfx = strstr(tok, "-id")) != NULL) {
        *sfx = '\0';
      }
      if ((new_table->ascii_id = strdup(tok)) == NULL) {
        ipmeta_log(__func__, "Cannot allocate polygon table name");
        state->parser.status = CSV_EUSER;
        return;
      }

      /* malloc some space in the table array for this table */
      if ((state->polygon_tables =
             realloc(state->polygon_tables,
                     sizeof(ipmeta_polygon_table_t *) *
                       (state->polygon_tables_cnt + 1))) == NULL) {
        ipmeta_log(__func__, "ERROR: Could not allocate polygon table array");
        state->parser.status = CSV_EUSER;
        return;
      }

      state->polygon_tables[state->polygon_tables_cnt++] = new_table;
    }
    /* else, ignore the other column names, we don't care */

    state->current_column++;
    return;
  }

  switch (state->current_column) {
  case POLYGON_COL_ID:
    /* Polygon id */
    state->tmp_polygon.id = strtoul(tok, &end, 10);
    if (end == tok || *end || errno == ERANGE) {
      log_invalid_col(state, "Invalid polygon ID", tok);
      state->parser.status = CSV_EUSER;
      return;
    }
    break;
  case POLYGON_COL_NAME:
    /* Polygon name string */
    if ((state->tmp_polygon.name = strdup(tok == NULL ? "" : tok)) == NULL) {
      ipmeta_log(__func__, "Cannot allocate memory for Polygon name");
      state->parser.status = CSV_EUSER;
      return;
    }
    break;
  case POLYGON_COL_FQID:
    /* Fully-Qualified ID */
    if ((state->tmp_polygon.fqid = strdup(tok == NULL ? "" : tok)) == NULL) {
      ipmeta_log(__func__, "Cannot allocate memory for Polygon FQID");
      state->parser.status = CSV_EUSER;
      return;
    }
    break;
  case POLYGON_COL_USERCODE:
    /* User code string */
    if ((state->tmp_polygon.usercode = strdup(tok == NULL ? "" : tok)) ==
        NULL) {
      ipmeta_log(__func__, "Cannot allocate memory for Polygon user code");
      state->parser.status = CSV_EUSER;
      return;
    }
    break;
  default:
    /* Just ignore non-relevant cols */
    break;
  }

  /* move on to the next column */
  state->current_column++;
}

/** Handle an end-of-row event for the polygon decode table*/
static void parse_polygons_row(int c, void *data)
{
  ipmeta_provider_t *provider = (ipmeta_provider_t *)data;
  ipmeta_provider_netacq_edge_state_t *state = STATE(provider);

  ipmeta_polygon_table_t *table = NULL;
  ipmeta_polygon_t *polygon = NULL;

  /* process the header row */
  if (state->current_line == 0) {
    /* all is done by the col parser */
    state->current_column = POLYGON_COL_FIRSTCOL;
    state->current_line++;
    return;
  }

  table = state->polygon_tables[state->polygon_tables_cnt - 1];
  assert(table != NULL);

  /* done processing the line */

  /* make sure we parsed exactly as many columns as we anticipated */
  check_column_count(state, "polygons", POLYGON_COL_ENDCOL);

  /* copy the tmp polygon struct into a new one */
  if ((polygon = malloc(sizeof(ipmeta_polygon_t))) == NULL) {
    ipmeta_log(__func__, "ERROR: Could not allocate memory for polygon");
    state->parser.status = CSV_EUSER;
    return;
  }
  memcpy(polygon, &(state->tmp_polygon), sizeof(ipmeta_polygon_t));

  /* make room in the polygons array for this polygon */
  if ((table->polygons =
         realloc(table->polygons, sizeof(ipmeta_polygon_t *) *
                                    (table->polygons_cnt + 1))) == NULL) {
    ipmeta_log(__func__, "ERROR: Could not allocate memory for polygon array");
    state->parser.status = CSV_EUSER;
    return;
  }
  /* now poke it in */
  table->polygons[table->polygons_cnt++] = polygon;

  /* increment the current line */
  state->current_line++;
  /* reset the current column */
  state->current_column = POLYGON_COL_FIRSTCOL;
  /* reset the tmp region info */
  memset(&(state->tmp_polygon), 0, sizeof(ipmeta_polygon_t));
}

/** Read a polygon decode file */
static int read_polygons(ipmeta_provider_t *provider, io_t *file)
{
  ipmeta_provider_netacq_edge_state_t *state = STATE(provider);

  /* reset the state variables before we start */
  state->current_column = POLYGON_COL_FIRSTCOL;
  state->current_line = 0;
  memset(&(state->tmp_polygon), 0, sizeof(ipmeta_polygon_t));

  return read_netacq_edge_file(provider, file, "Polygons",
      parse_polygons_cell, parse_polygons_row);
}

/* Parse a netacq2polygon table cell */
static void parse_na_to_polygon_cell(void *s, size_t i, void *data)
{
  ipmeta_provider_t *provider = (ipmeta_provider_t *)data;
  ipmeta_provider_netacq_edge_state_t *state = STATE(provider);
  char *tok = (char *)s;
  char *end;
  char *sfx;
  int found = 0;
  int table_id;

  /* process the first line */
  if (state->current_line == 0) {
    if (state->current_column > NA_TO_POLYGON_COL_FIRSTCOL) {
      /* what table does this refer to? */
      for (i = 0; i < state->polygon_tables_cnt; i++) {
        /* chop off the -id suffix */
        if ((sfx = strstr(tok, "-id")) != NULL) {
          *sfx = '\0';
        }
        if (strcmp(tok, state->polygon_tables[i]->ascii_id) == 0) {
          /* this is it! */
          state->tmp_na_col_to_tbl[state->current_column % 1000] = i;
          found = 1;
          break;
        }
      }

      if (found == 0) {
        ipmeta_log(__func__, "Missing Polygon Table for (%s)", tok);
        state->parser.status = CSV_EUSER;
        return;
      }
    }

    state->current_column++;
    return;
  }

  switch (state->current_column) {
  case NA_TO_POLYGON_COL_NETACQ_LOC_ID:
    /* Netacq id */
    if (tok == NULL) {
      log_invalid_col(state, "Invalid Net Acuity ID", tok);
      state->parser.status = CSV_EUSER;
      return;
    }
    state->tmp_na_to_polygon.na_loc_id = strtoul(tok, &end, 10);
    if (end == tok || *end || errno == ERANGE) {
      log_invalid_col(state, "Invalid Net Acuity ID", tok);
      state->parser.status = CSV_EUSER;
      return;
    }
    break;
  default:
    table_id = state->tmp_na_col_to_tbl[state->current_column % 1000];
    if (tok == NULL) {
      log_invalid_col(state, "Invalid polygon ID", tok);
      state->parser.status = CSV_EUSER;
      return;
    }
    state->tmp_na_to_polygon.polygon_ids[table_id] = strtoul(tok, &end, 10);
    if (end == tok || *end || errno == ERANGE) {
      log_invalid_col(state, "Invalid polygon ID", tok);
      state->parser.status = CSV_EUSER;
      return;
    }
    break;
  }

  /* move on to the next column */
  state->current_column++;
}

/** Handle an end-of-row event for the netacq2polygon table*/
static void parse_na_to_polygon_row(int c, void *data)
{
  int i;
  ipmeta_provider_t *provider = (ipmeta_provider_t *)data;
  ipmeta_provider_netacq_edge_state_t *state = STATE(provider);

  na_to_polygon_t *n2p = NULL;

  /* process the header row */
  if (state->current_line == 0) {
    /** all work is done in the col parser ? */
    state->current_column = NA_TO_POLYGON_COL_FIRSTCOL;
    state->current_line++;
    return;
  }

  /* done processing the line */

  /* make sure we parsed exactly as many columns as we anticipated */
  if (state->current_column <= NA_TO_POLYGON_COL_NETACQ_LOC_ID) {
    ipmeta_log(__func__, "Missing location ID");
    state->parser.status = CSV_EUSER;
    return;
  }

  /* copy the tmp polygon struct into a new one */
  if ((n2p = malloc(sizeof(na_to_polygon_t))) == NULL) {
    ipmeta_log(__func__, "ERROR: Could not allocate memory for polygon");
    state->parser.status = CSV_EUSER;
    return;
  }
  memcpy(n2p, &(state->tmp_na_to_polygon), sizeof(na_to_polygon_t));

  /* if this id would overflow the table, just make the table bigger */
  if ((n2p->na_loc_id + 1) > state->na_to_polygons_cnt) {
    if ((state->na_to_polygons =
           realloc(state->na_to_polygons,
                   sizeof(na_to_polygon_t *) * (n2p->na_loc_id + 1))) == NULL) {
      ipmeta_log(__func__,
                 "ERROR: Could not allocate memory for na2polygon array");
      state->parser.status = CSV_EUSER;
      return;
    }
    /* zero out the newly allocated memory */
    for (i = state->na_to_polygons_cnt; i < n2p->na_loc_id; i++) {
      state->na_to_polygons[i] = NULL;
    }

    state->na_to_polygons_cnt = n2p->na_loc_id + 1;
  } else if (state->na_to_polygons[n2p->na_loc_id] != NULL) {
    /* About to override an already inserted location */
    ipmeta_log(__func__, "ERROR: Duplicate location ID: %d in polygons file",
               n2p->na_loc_id);
    state->parser.status = CSV_EUSER;
    return;
  }

  /* now poke it in */
  state->na_to_polygons[n2p->na_loc_id] = n2p;

  /* increment the current line */
  state->current_line++;
  /* reset the current column */
  state->current_column = NA_TO_POLYGON_COL_FIRSTCOL;
  /* reset the tmp region info */
  memset(&(state->tmp_na_to_polygon), 0, sizeof(na_to_polygon_t));
}

/** Read a netacq2polygon mapping file */
static int read_na_to_polygon(ipmeta_provider_t *provider, io_t *file)
{
  ipmeta_provider_netacq_edge_state_t *state = STATE(provider);

  /* reset the state variables before we start */
  state->current_column = NA_TO_POLYGON_COL_FIRSTCOL;
  state->current_line = 0;
  memset(&(state->tmp_na_to_polygon), 0, sizeof(na_to_polygon_t));

  return read_netacq_edge_file(provider, file, "netacq2polygon",
      parse_na_to_polygon_cell, parse_na_to_polygon_row);
}

static void na_to_polygon_free(ipmeta_provider_netacq_edge_state_t *state)
{
  int i;

  for (i = 0; i < state->na_to_polygons_cnt; i++) {
    free(state->na_to_polygons[i]);
    state->na_to_polygons[i] = NULL;
  }
  free(state->na_to_polygons);
  state->na_to_polygons = NULL;
  state->na_to_polygons_cnt = 0;
}

static int load_file(ipmeta_provider_t *provider, const char *filename,
    const char *label, int (*readfn)(ipmeta_provider_t *, io_t *))
{
  io_t *file;
  int rc;

  ipmeta_log(__func__, "processing %s file '%s'", label, filename);
  if ((file = wandio_create(filename)) == NULL) {
    ipmeta_log(__func__, "failed to open %s file '%s'",
               label, filename);
    return -1;
  }

  if ((rc = readfn(provider, file)) != 0) {
    ipmeta_log(__func__, "failed to parse %s file '%s'", label, filename);
  }

  wandio_destroy(file);

  return rc;
}

/* ===== PUBLIC FUNCTIONS BELOW THIS POINT ===== */

ipmeta_provider_t *ipmeta_provider_netacq_edge_alloc(void)
{
  return &ipmeta_provider_netacq_edge;
}

int ipmeta_provider_netacq_edge_init(ipmeta_provider_t *provider, int argc,
                                     char **argv)
{
  ipmeta_provider_netacq_edge_state_t *state;
  int i;

  /* allocate our state */
  if ((state = malloc_zero(sizeof(ipmeta_provider_netacq_edge_state_t))) ==
      NULL) {
    ipmeta_log(__func__,
               "could not malloc ipmeta_provider_netacq_edge_state_t");
    return -1;
  }
  ipmeta_provider_register_state(provider, state);

  /* parse the command line args */
  if (parse_args(provider, argc, argv) != 0) {
    return -1;
  }

  if (state->blocks_file) {
    /* if provided, open the region decode file and populate the lookup arrays */
    if (state->region_file != NULL) {
      if (load_file(provider, state->region_file, "region", read_regions) < 0)
        return -1;
    }

    /* if provided, open the country decode file and populate the lookup arrays */
    if (state->country_file != NULL) {
      if (load_file(provider, state->country_file, "country", read_countries) < 0)
        return -1;
    }

    /* open each polygon decode file and populate the lookup arrays */
    for (i = 0; i < state->polygon_files_cnt; i++) {
      assert(state->polygon_files[i] != NULL);
      if (load_file(provider, state->polygon_files[i], "polygon",
            read_polygons) < 0)
        return -1;
    }

    /* if provided, open the netacq2polygon mapping file and populate the
       temporary join table */
    if (state->na_to_polygon_file != NULL) {
      if (load_file(provider, state->na_to_polygon_file, "Net Acuity to Polygon",
            read_na_to_polygon) < 0)
        return -1;
    }

    /* load the locations file */
    if (load_file(provider, state->locations_file, "location",
          read_locations) < 0)
      return -1;

    /* load the blocks file */
    if (load_file(provider, state->blocks_file, "blocks", read_blocks) < 0)
      return -1;

    /* free the netacq 2 polygon temporary mapping table */
    na_to_polygon_free(state);

  }

  if (state->ipv6_file) {
    /* load the ipv6 file */
    if (load_file(provider, state->ipv6_file, "IPv6", read_ipv6) < 0)
      return -1;
  }

  /* ready to rock n roll */

  return 0;
}

void ipmeta_provider_netacq_edge_free(ipmeta_provider_t *provider)
{
  ipmeta_provider_netacq_edge_state_t *state = STATE(provider);
  int i, j;
  ipmeta_polygon_table_t *table;

  if (state != NULL) {
    free(state->locations_file);
    state->locations_file = NULL;

    free(state->blocks_file);
    state->blocks_file = NULL;

    free(state->region_file);
    state->region_file = NULL;

    free(state->country_file);
    state->country_file = NULL;

    for (i = 0; i < state->polygon_files_cnt; i++) {
      free(state->polygon_files[i]);
      state->polygon_files[i] = NULL;
    }
    state->polygon_files_cnt = 0;

    free(state->na_to_polygon_file);
    state->na_to_polygon_file = NULL;

    if (state->regions != NULL) {
      for (i = 0; i < state->regions_cnt; i++) {
        if (state->regions[i]->name != NULL) {
          free(state->regions[i]->name);
          state->regions[i]->name = NULL;
        }
        free(state->regions[i]);
        state->regions[i] = NULL;
      }
      free(state->regions);
      state->regions = NULL;
      state->regions_cnt = 0;
    }

    if (state->countries != NULL) {
      for (i = 0; i < state->countries_cnt; i++) {
        if (state->countries[i]->name != NULL) {
          free(state->countries[i]->name);
          state->countries[i]->name = NULL;
        }
        free(state->countries[i]);
        state->countries[i] = NULL;
      }
      free(state->countries);
      state->countries = NULL;
      state->countries_cnt = 0;
    }

    if (state->polygon_tables != NULL) {
      /* @todo move to a polygon_table_free function */
      for (i = 0; i < state->polygon_tables_cnt; i++) {
        table = state->polygon_tables[i];

        free(table->ascii_id);
        table->ascii_id = NULL;

        /** @todo move a a polygon_free function */
        for (j = 0; j < table->polygons_cnt; j++) {
          free(table->polygons[j]->name);
          table->polygons[j]->name = NULL;

          free(table->polygons[j]->fqid);
          table->polygons[j]->fqid = NULL;

          free(table->polygons[j]->usercode);
          table->polygons[j]->usercode = NULL;

          free(table->polygons[j]);
          table->polygons[j] = NULL;
        }

        free(table->polygons);
        table->polygons = NULL;

        free(table);
        state->polygon_tables[i] = NULL;
      }
      free(state->polygon_tables);
      state->polygon_tables = NULL;
      state->polygon_tables_cnt = 0;
    }

    /* just in case */
    na_to_polygon_free(state);
    csv_free(&(state->parser));

    ipmeta_provider_free_state(provider);
  }
  return;
}

int ipmeta_provider_netacq_edge_lookup_pfx(ipmeta_provider_t *provider,
    int family, void *addrp, uint8_t pfxlen, ipmeta_record_set_t *records)
{
  /* just call the lookup helper func in provider manager */
  return ipmeta_provider_lookup_pfx(provider, family, addrp, pfxlen, records);
}

int ipmeta_provider_netacq_edge_lookup_addr(ipmeta_provider_t *provider,
    int family, void *addrp, ipmeta_record_set_t *found)
{
  /* just call the lookup helper func in provider manager */
  return ipmeta_provider_lookup_addr(provider, family, addrp, found);
}

void ipmeta_provider_netacq_edge_free_record(ipmeta_record_t *record)
{
  ipmeta_free_record(record);
}
int ipmeta_provider_netacq_edge_get_regions(
  ipmeta_provider_t *provider, ipmeta_provider_netacq_edge_region_t ***regions)
{
  assert(provider != NULL && provider->enabled != 0);
  ipmeta_provider_netacq_edge_state_t *state = STATE(provider);
  *regions = state->regions;
  return state->regions_cnt;
}

int ipmeta_provider_netacq_edge_get_countries(
  ipmeta_provider_t *provider,
  ipmeta_provider_netacq_edge_country_t ***countries)
{
  assert(provider != NULL && provider->enabled != 0);
  ipmeta_provider_netacq_edge_state_t *state = STATE(provider);
  *countries = state->countries;
  return state->countries_cnt;
}

int ipmeta_provider_netacq_edge_get_polygon_tables(
  ipmeta_provider_t *provider, ipmeta_polygon_table_t ***tables)
{
  assert(provider != NULL && provider->enabled != 0);
  ipmeta_provider_netacq_edge_state_t *state = STATE(provider);
  *tables = state->polygon_tables;
  return state->polygon_tables_cnt;
}
