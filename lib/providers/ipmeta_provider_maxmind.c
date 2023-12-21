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

#include "libipmeta_int.h"
#include "config.h"

#include <arpa/inet.h>
#include <assert.h>
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

#include "ipmeta_parsing_helpers.h"
#include "ipmeta_ds.h"
#include "ipmeta_provider_maxmind.h"

#define PROVIDER_NAME "maxmind"

#define STATE(provname) (IPMETA_PROVIDER_STATE(maxmind, provname))

#define BUFFER_LEN 1024
#define EARTH_CIRCUMFERENCE 40000 /* km */

KHASH_INIT(u16u16, uint16_t, uint16_t, 1, kh_int_hash_func, kh_int_hash_equal)
KHASH_INIT(ipm_records, int, ipmeta_record_t *, 1, kh_int_hash_func,
    kh_int_hash_equal)

/** The default file name for the locations file */
#define LOCATIONS_FILE_NAME "GeoLiteCity-Location.csv.gz"

/** The default file name for the blocks file */
#define BLOCKS_FILE_NAME "GeoLiteCity-Blocks.csv.gz"

// convert char[2] to uint16_t
#define c2_to_u16(c2) (((c2)[0] << 8) | (c2)[1])

// convert uint16_t to char[2]
#define u16_to_c2(u16, c2)                                                     \
  do {                                                                         \
    (c2)[0] = (((u16) >> 8) & 0xFF);                                           \
    (c2)[1] = ((u16) & 0xFF);                                                  \
  } while (0)

/** The basic fields that every instance of this provider have in common */
static ipmeta_provider_t ipmeta_provider_maxmind = {
  IPMETA_PROVIDER_MAXMIND, PROVIDER_NAME,
  IPMETA_PROVIDER_GENERATE_PTRS(maxmind)};

/** Holds the state for an instance of this provider */
typedef struct ipmeta_provider_maxmind_state {

  /* info extracted from args */
  char *locations_file;
  char *blocks_file[8];
  int blocks_file_cnt;

  /* State for CSV parser */
  struct csv_parser parser;
  const char *current_filename;
  int current_line;
  int current_column; // column ID (not column number)
  int first_column; // ID of first column for the current file format
  void (*parse_row)(int, void *);
  ipmeta_record_t *record;
  uint32_t loc_id;
  ipvx_prefix_t block_lower; // v1: low end of range; v2: prefix
  ipvx_prefix_t block_upper; // v1: high end of range; v2: not used

  // maxmind version
  int maxmind_version;

  // map from country code to continent code (for v1)
  khash_t(u16u16) *country_continent;

  // v2 location records
  khash_t(ipm_records) *loc_records;

  uint32_t block_cnt; // for v2
} ipmeta_provider_maxmind_state_t;

enum { FILETYPE_BLK, FILETYPE_LOC };
static const char *filetype_name[] = { "blocks", "locations" };

// Column ids start at a multiple of 1000 and count up from there.  To convert
// a column id to a column number, we must mod by 1000.  This allows two
// different tables to have non-overlapping sets of column IDs so they can
// share some of the same column parsers.

/** The columns in the maxmind v1 locations CSV file */
typedef enum locations1_cols {
  LOCATION1_COL_FIRSTCOL = 1000, ///< ID of first column in table
  LOCATION1_COL_ID = 1000,       ///< location ID
  LOCATION1_COL_CC,              ///< 2 Char Country Code
  LOCATION1_COL_REGION,          ///< Region String
  LOCATION1_COL_CITY,            ///< City String
  LOCATION1_COL_POSTAL,          ///< Postal Code String
  LOCATION1_COL_LAT,             ///< Latitude
  LOCATION1_COL_LONG,            ///< Longitude
  LOCATION1_COL_METRO,           ///< Metro Code
  LOCATION1_COL_AREA,            ///< Area Code (phone)
  LOCATION1_COL_ENDCOL           ///< 1 past the last column ID in the table
} locations1_cols_t;

/** The columns in the maxmind v1 locations CSV file */
typedef enum blocks1_cols {
  BLOCKS1_COL_FIRSTCOL = 2000,   ///< ID of first column in table
  BLOCKS1_COL_STARTIP = 2000,    ///< Range Start IP
  BLOCKS1_COL_ENDIP,             ///< Range End IP
  BLOCKS1_COL_ID,                ///< location ID
  BLOCKS1_COL_ENDCOL             ///< 1 past the last column ID in the table
} blocks1_cols_t;

/** The columns in the maxmind v2 locations CSV file */
typedef enum locations2_cols {
  LOCATION2_COL_FIRSTCOL = 3000, ///< ID of first column in table
  LOCATION2_COL_GNID = 3000,     ///< geonames ID
  LOCATION2_COL_LOCALE_CODE,     ///< locale code for location strings
  LOCATION2_COL_CONTINENT_CODE,  ///< 2-char continent code
  LOCATION2_COL_CONTINENT_NAME,  ///< continent name
  LOCATION2_COL_CC,              ///< ISO 3166-1 2-char country code
  LOCATION2_COL_COUNTRY_NAME,    ///< country name
  LOCATION2_COL_SUBDIV1_CODE,    ///< subsivision 1 iso code (1-3 chars)
  LOCATION2_COL_SUBDIV1_NAME,    ///< subsivision 1 name
  LOCATION2_COL_SUBDIV2_CODE,    ///< subsivision 2 iso code (1-3 chars)
  LOCATION2_COL_SUBDIV2_NAME,    ///< subsivision 2 name
  LOCATION2_COL_CITY_NAME,       ///< city name
  LOCATION2_COL_METRO_CODE,      ///< metro code (US only)
  LOCATION2_COL_TIMEZONE,        ///< time zone from IANA time zone db
  LOCATION2_COL_IS_IN_EU,        ///< is in European Union: 0 or 1
  LOCATION2_COL_ENDCOL           ///< 1 past the last column ID in the table
} locations2_cols_t;

/** The columns in the maxmind v2 locations CSV file */
typedef enum blocks2_cols {
  BLOCKS2_COL_FIRSTCOL = 4000,   ///< ID of first column in table
  BLOCKS2_COL_NETWORK = 4000,    ///< Network prefix
  BLOCKS2_COL_GNID,              ///< geonames ID of network
  BLOCKS2_COL_REG_CNTRY_GNID,    ///< geonames ID of registered country
  BLOCKS2_COL_REP_CNTRY_GNID,    ///< geonames ID of represented country
  BLOCKS2_COL_IS_ANON_PROXY,     ///< deprecated
  BLOCKS2_COL_IS_SATELLITE_PROV, ///< deprecated
  BLOCKS2_COL_POSTAL,            ///< postal code (string)
  BLOCKS2_COL_LAT,               ///< latitude (decimal)
  BLOCKS2_COL_LONG,              ///< longitude (decimal)
  BLOCKS2_COL_ACCURACY_RADIUS,   ///< accuracy radius (integer kilometers)
  BLOCKS2_COL_ENDCOL             ///< 1 past the last column ID in the table
} blocks2_cols_t;


/** Print usage information to stderr */
static void usage(ipmeta_provider_t *provider)
{
  fprintf(
    stderr,
    "provider usage: %s {-l locations -b blocks}|{-d directory}\n"
    "   -d <dir>    directory containing v1 blocks and location files\n"
    "   -l <file>   v1 or v2 locations file (requires -b)\n"
    "   -b <file>   v1 or v2 blocks file (requires -l; may be repeated)\n",
    provider->name);
}

/** Parse the arguments given to the provider
 */
static int parse_args(ipmeta_provider_t *provider, int argc, char **argv)
{
  ipmeta_provider_maxmind_state_t *state = STATE(provider);
  int opt;
  char *directory = NULL;
  char *ptr = NULL;

  /* no args */
  if (argc == 0) {
    usage(provider);
    return -1;
  }

  /* NB: remember to reset optind to 1 before using getopt! */
  optind = 1;

  /* remember the argv strings DO NOT belong to us */

  while ((opt = getopt(argc, argv, "b:d:D:l:?")) >= 0) {
    switch (opt) {
    case 'b':
      if (state->blocks_file_cnt >= ARR_CNT(state->blocks_file)) {
        fprintf(stderr, "ERROR: too many block files\n");
        return -1;
      }
      state->blocks_file[state->blocks_file_cnt++] = strdup(optarg);
      break;
    case 'D':
      fprintf(stderr,
        "WARNING: -D option is no longer supported by individual providers.\n");
      break;
    case 'd':
      if (directory) {
        fprintf(stderr, "ERROR: only one directory is allowed\n");
        return -1;
      }
      /* no need to dup right now because we will do it later */
      directory = optarg;
      break;

    case 'l':
      if (state->locations_file) {
        fprintf(stderr, "ERROR: only one location file is allowed\n");
        return -1;
      }
      state->locations_file = strdup(optarg);
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

  if ((state->locations_file || state->blocks_file[0]) && directory) {
    /* check if they were daft and specified files AND directory */
    fprintf(stderr, "WARNING: both directory and file name specified; "
        "ignoring directory.\n");
    directory = NULL;
  }

  if (directory) {
    /* remove the trailing slash if there is one */
    if (directory[strlen(directory) - 1] == '/') {
      directory[strlen(directory) - 1] = '\0';
    }

    /* malloc storage for the dir+/+file string */
    if ((state->locations_file = malloc(
           strlen(directory) + 1 + strlen(LOCATIONS_FILE_NAME) + 1)) == NULL) {
      ipmeta_log(__func__, "could not malloc location file string");
      return -1;
    }

    if ((state->blocks_file[0] = malloc(strlen(directory) + 1 +
                                     strlen(BLOCKS_FILE_NAME) + 1)) == NULL) {
      ipmeta_log(__func__, "could not malloc blocks file string");
      return -1;
    }
    state->blocks_file_cnt++;

    /** @todo make this check for both .gz and non-.gz files */

    ptr = stpncpy(state->locations_file, directory, strlen(directory));
    *ptr++ = '/';
    /* last copy needs a +1 to get the terminating nul. d'oh */
    ptr = stpncpy(ptr, LOCATIONS_FILE_NAME, strlen(LOCATIONS_FILE_NAME) + 1);

    ptr = stpncpy(state->blocks_file[0], directory, strlen(directory));
    *ptr++ = '/';
    ptr = stpncpy(ptr, BLOCKS_FILE_NAME, strlen(BLOCKS_FILE_NAME) + 1);
  }

  if (state->locations_file == NULL || state->blocks_file[0] == NULL) {
    fprintf(stderr, "ERROR: %s requires either '-d' or both '-b' and '-l'\n",
            provider->name);
    usage(provider);
    return -1;
  }

  return 0;
}

/* Parse a maxmind cell */
static void parse_maxmind_cell(void *s, size_t i, void *data)
{
  ipmeta_provider_t *provider = (ipmeta_provider_t *)data;
  ipmeta_provider_maxmind_state_t *state = STATE(provider);
  char *tok = (char *)s;
  char *end;

  /*
  ipmeta_log(__func__, "row: %d, column: %d, tok: %s",
             state->current_line,
             state->current_column % 1000,
             tok);
  */

#define rec (state->record) /* convenient code abbreviation */

  switch (state->current_column) {
  case LOCATION1_COL_ID:
  case LOCATION2_COL_GNID:
    state->record = malloc_zero(sizeof(ipmeta_record_t));
    rec->id = strtoul(tok, &end, 10);
    if (end == tok || *end || errno == ERANGE) {
      col_invalid(state, "Invalid ID", tok);
    }
    break;

  case LOCATION2_COL_LOCALE_CODE:
    break; // not used (it's the same for every record in the file)

  case LOCATION2_COL_CONTINENT_CODE:
    // continent code
    if (tok == NULL || strlen(tok) != 2) {
      col_invalid(state, "Invalid continent code", tok);
    }
    memcpy(rec->continent_code, tok, 2);
    break;

  case LOCATION2_COL_CONTINENT_NAME:
    break; // not used

  case LOCATION1_COL_CC:
  case LOCATION2_COL_CC:
    // country code
    if (!tok || !*tok || (tok[0] == '-' && tok[1] == '-')) {
      rec->country_code[0] = '?';
      rec->country_code[1] = '?';
    } else if (strlen(tok) != 2) {
      col_invalid(state, "Invalid country code", tok);
    } else {
      memcpy(rec->country_code, tok, 2);
    }
    break;

  case LOCATION2_COL_COUNTRY_NAME:
    break; // not used

  case LOCATION1_COL_REGION:
  case LOCATION2_COL_SUBDIV1_CODE:
    /* region string */
    coldup(state, col, rec->region, tok);
    break;

  case LOCATION2_COL_SUBDIV1_NAME:
  case LOCATION2_COL_SUBDIV2_CODE:
  case LOCATION2_COL_SUBDIV2_NAME:
    break; // not used

  case LOCATION1_COL_CITY:
  case LOCATION2_COL_CITY_NAME:
    /* city */
    coldup(state, col, rec->city, tok);
    break;

  case BLOCKS2_COL_POSTAL:
    if (!rec)
      break; // we're ignoring this record because it had no GNID
    // fall through
  case LOCATION1_COL_POSTAL:
    /* postal code */
    coldup(state, col, rec->post_code, tok);
    break;

  case BLOCKS2_COL_LAT:
    if (!rec)
      break; // we're ignoring this record because it had no GNID
    // fall through
  case LOCATION1_COL_LAT:
    /* latitude */
    if (tok && *tok) {
        rec->latitude = strtod(tok, &end);
        if (end == tok || *end || rec->latitude < -90 || rec->latitude > 90) {
          col_invalid(state, "Invalid latitude", tok);
        }
    } else {
        /*
         * Sometimes (but rarely) the maxmind data is malformed and contains
         * a row with a GNID but no other useful values.
         *
         * Ignoring it will mean it is left as zero (which is wrong), and
         * setting any sort of placeholder value will likely break programs
         * that expect it to be a double in an appropriate range.
         *
         * There isn't an easy way to express a missing latitude, so for now
         * we'll throw the whole row away, even if that means throwing away
         * a possibly valid GNID.
         */
        state->loc_id = 0;
        free(state->record);
        state->record = NULL;
    }
    break;

  case BLOCKS2_COL_LONG:
    if (!rec)
      break; // we're ignoring this record because it had no GNID
    // fall through
  case LOCATION1_COL_LONG:
    /* longitude */
    if (tok && *tok) {
        rec->longitude = strtod(tok, &end);
        if (end == tok || *end || rec->longitude < -180 || rec->longitude > 180) {
          col_invalid(state, "Invalid longitude", tok);
        }
    } else {
        /*
         * This case has probably already been caught by the latitude being
         * empty. Drop any row with a GNID but without other useful values.
         */
        state->loc_id = 0;
        free(state->record);
        state->record = NULL;
    }
    break;

  case LOCATION1_COL_METRO:
  case LOCATION2_COL_METRO_CODE:
    /* metro code - whatever the heck that is */
    if (tok && *tok) {
      rec->metro_code = strtoul(tok, &end, 10);
      if (end == tok || *end || errno == ERANGE) {
        col_invalid(state, "Invalid metro code", tok);
      }
    } // else, empty field is 0
    break;

  case LOCATION1_COL_AREA:
    /* area code - (phone) */
    if (tok && *tok) {
      rec->area_code = strtoul(tok, &end, 10);
      if (end == tok || *end || errno == ERANGE) {
        col_invalid(state, "Invalid area code", tok);
      }
    } // else, empty field is 0
    break;

  case LOCATION2_COL_TIMEZONE:
    coldup(state, col, rec->timezone, tok);
    break;

  case LOCATION2_COL_IS_IN_EU:
    break; // not used

  case BLOCKS2_COL_ACCURACY_RADIUS:
    if (!rec)
      break; // ignore this record because it had no GNID or was malformed
    if (tok && *tok) {
      rec->accuracy = strtoul(tok, &end, 10);
      if (end == tok || *end || rec->accuracy > EARTH_CIRCUMFERENCE / 4) {
        col_invalid(state, "Invalid accuracy radius", tok);
      }
    } // else, empty field is 0
    break;

  case BLOCKS1_COL_STARTIP:
    /* start ip */
    state->block_lower.addr.v4.s_addr = htonl(strtoul(tok, &end, 10));
    if (end == tok || *end || errno == ERANGE) {
      col_invalid(state, "Invalid start IP", tok);
    }
    break;

  case BLOCKS1_COL_ENDIP:
    /* end ip */
    state->block_upper.addr.v4.s_addr = htonl(strtoul(tok, &end, 10));
    if (end == tok || *end || errno == ERANGE) {
      col_invalid(state, "Invalid end IP", tok);
    }
    break;

  case BLOCKS2_COL_NETWORK:
    // network prefix
    if (ipvx_pton_pfx(tok, &state->block_lower) < 0) {
      col_invalid(state, "Invalid network", tok);
    }
    break;

  case BLOCKS2_COL_GNID:
    if (!tok) {
      // Maxmind v2 apparently has some blocks that are missing GNID and
      // everything else except REG_CNTRY_GNID.  We'll ignore these blocks.
      state->loc_id = 0;
      break;
    }
    // Now we know we'll need state->record.
    state->record = malloc_zero(sizeof(ipmeta_record_t));
    // fall through
  case BLOCKS1_COL_ID:
    // location id (foreign key)
    state->loc_id = strtoul(tok, &end, 10);
    if (end == tok || *end || errno == ERANGE) {
      col_invalid(state, "Invalid ID", tok);
    }
    break;

  case BLOCKS2_COL_REG_CNTRY_GNID:
  case BLOCKS2_COL_REP_CNTRY_GNID:
  case BLOCKS2_COL_IS_ANON_PROXY:
  case BLOCKS2_COL_IS_SATELLITE_PROV:
    break; // not used

  default:
    col_invalid(state, "Unexpected trailing column", tok);
  }

#undef rec

  /* move on to the next column */
  state->current_column++;
}

/** Handle an end-of-row event from the CSV parser */
static void parse_maxmind_location1_row(int c, void *data)
{
  ipmeta_provider_t *provider = (ipmeta_provider_t *)data;
  ipmeta_provider_maxmind_state_t *state = STATE(provider);

  khiter_t khiter;

  /* make sure we parsed exactly as many columns as we anticipated */
  check_column_count(state, LOCATION1_COL_ENDCOL);

  /* look up the continent code */
  char *cc = state->record->country_code;
  if ((khiter = kh_get(u16u16, state->country_continent, c2_to_u16(cc))) ==
      kh_end(state->country_continent)) {
    row_error(state, "Unknown country code (%s)", cc);
  }

  uint16_t u16_continent = kh_value(state->country_continent, khiter);
  u16_to_c2(u16_continent, state->record->continent_code);

  ipmeta_provider_insert_record(provider, state->record);

  // reset for next record
  state->current_line++;
  state->current_column = state->first_column;
  state->record = NULL;

  return;
}

static void parse_blocks1_row(int c, void *data)
{
  ipmeta_provider_t *provider = (ipmeta_provider_t *)data;
  ipmeta_provider_maxmind_state_t *state = STATE(provider);

  ipvx_prefix_list_t *pfx_list, *pfx_node;
  ipmeta_record_t *record = NULL;

  /* make sure we parsed exactly as many columns as we anticipated */
  check_column_count(state, BLOCKS1_COL_ENDCOL);

  assert(state->loc_id > 0);

  /* convert the range to prefixes */
  if (ipvx_range_to_prefix(&state->block_lower, &state->block_upper, &pfx_list) !=
      0) {
    row_error(state, "%s", "Could not convert range to prefixes");
  }
  assert(pfx_list != NULL);

  /* get the record from the provider */
  if ((record = ipmeta_provider_get_record(provider, state->loc_id)) ==
      NULL) {
    row_error(state, "Missing record for location %d", state->loc_id);
  }

  /* iterate over and add each prefix to the trie */
  for (pfx_node = pfx_list; pfx_node; pfx_node = pfx_node->next) {
    if (ipmeta_provider_associate_record(provider, pfx_node->prefix.family,
          &pfx_node->prefix.addr, pfx_node->prefix.masklen, record) != 0) {
      row_error(state, "%s", "Failed to associate record");
    }
  }
  ipvx_prefix_list_free(pfx_list);

  // reset for next record
  state->current_line++;
  state->current_column = state->first_column;
  state->loc_id = 0;
}

static void parse_maxmind_location2_row(int c, void *data)
{
  ipmeta_provider_t *provider = (ipmeta_provider_t *)data;
  ipmeta_provider_maxmind_state_t *state = STATE(provider);
  int khret;

  // make sure we parsed exactly as many columns as we anticipated
  check_column_count(state, LOCATION2_COL_ENDCOL);

  // In maxmind v2, location information is split across location and block
  // records.  We store this incomplete location record in state->loc_records,
  // so it can be merged into each block record that needs it later.
  khiter_t k = kh_put(ipm_records, state->loc_records, state->record->id,
      &khret);
  kh_val(state->loc_records, k) = state->record;
  state->record = NULL;

  // reset for next record
  state->current_line++;
  state->current_column = state->first_column;

  return;
}

static void parse_blocks2_row(int c, void *data)
{
  ipmeta_provider_t *provider = (ipmeta_provider_t *)data;
  ipmeta_provider_maxmind_state_t *state = STATE(provider);

  // make sure we parsed exactly as many columns as we anticipated
  check_column_count(state, BLOCKS2_COL_ENDCOL);

  ipmeta_record_t *blk_rec = state->record;
  if (!state->record)
    goto end; // we're ignoring this record because it had no GNID

  blk_rec->id = ++state->block_cnt;

  ipmeta_provider_insert_record(provider, blk_rec);

  // Copy fields from the loc record to the block record.  (We can't just use
  // a single record structure, because multiple block records may refer to
  // the same location record).
  khiter_t k = kh_get(ipm_records, state->loc_records, state->loc_id);
  ipmeta_record_t *loc_rec = kh_val(state->loc_records, k);

  // coldup(state, row, blk_rec->locale_code, loc_rec->locale_code);
  memcpy(blk_rec->continent_code, loc_rec->continent_code, 2);
  memcpy(blk_rec->country_code, loc_rec->country_code, 2);
  coldup(state, row, blk_rec->region, loc_rec->region);
  coldup(state, row, blk_rec->city, loc_rec->city);
  blk_rec->metro_code = loc_rec->metro_code;
  coldup(state, row, blk_rec->timezone, loc_rec->timezone);
  // TODO: Share the strings with loc_rec instead of duplicating them.

  // add prefix to the trie
  if (ipmeta_provider_associate_record(provider, state->block_lower.family,
        &state->block_lower.addr, state->block_lower.masklen, blk_rec) != 0) {
    row_error(state, "%s", "Failed to associate record");
    return;
  }

end:
  // reset for next record
  state->current_line++;
  state->current_column = state->first_column;
  state->record = NULL;
  state->loc_id = 0;
}

#define startswith(buf, str)  (strncmp(buf, str "", sizeof(str)-1) == 0)

/** Read a maxmind file */
static int read_maxmind_file(ipmeta_provider_t *provider, int filetype,
    const char *filename)
{
  ipmeta_provider_maxmind_state_t *state = STATE(provider);
  char buffer[BUFFER_LEN];
  io_t *file;
  int read = 0;
  int rc = -1; // fail, until proven otherwise
  int found_type = -1;
  int found_version = 0;
  state->first_column = -1;
  state->current_line = 0;
  state->parse_row = NULL;

  // open the file
  if ((file = wandio_create(filename)) == NULL) {
    ipmeta_log(__func__, "failed to open file '%s'", filename);
    goto end;
  }
  state->current_filename = filename;

  // Examine header lines to determine file format
  while (state->first_column < 0) {
    // wandio_fgets is slow, but we use it only for the header
    read = wandio_fgets(file, &buffer, BUFFER_LEN, 0);
    if (read < 0) {
      ipmeta_log(__func__, "Error reading file: %s", filename);
      goto end;
    }
    if (read == 0) {
      ipmeta_log(__func__, "Empty file: %s", filename);
      goto end;
    }

    if (startswith(buffer, "Copyright")) {
      // skip

    } else if (startswith(buffer, "locId,")) {
      found_version = 1;
      found_type = FILETYPE_LOC;
      state->current_column = state->first_column = LOCATION1_COL_FIRSTCOL;
      state->parse_row = parse_maxmind_location1_row;
      // initialize state specific to location1
      state->record = NULL;
      if (!state->country_continent) {
        // populate the country2continent hash
        int country_cnt;
        const char **countries;
        const char **continents;
        state->country_continent = kh_init(u16u16);
        country_cnt = ipmeta_provider_maxmind_get_iso2_list(&countries);
        ipmeta_provider_maxmind_get_country_continent_list(&continents);
        /*assert(country_cnt == continent_cnt);*/
        for (int i = 0; i < country_cnt; i++) {
          // create a mapping for this country
          int khret;
          khiter_t k = kh_put(u16u16, state->country_continent,
              c2_to_u16(countries[i]), &khret);
          kh_value(state->country_continent, k) = c2_to_u16(continents[i]);
        }
      }

    } else if (startswith(buffer, "startIpNum,")) {
      found_version = 1;
      found_type = FILETYPE_BLK;
      state->current_column = state->first_column = BLOCKS1_COL_FIRSTCOL;
      state->parse_row = parse_blocks1_row;
      // initialize state specific to blocks1
      state->loc_id = 0;
      state->block_lower.family = AF_INET;
      state->block_lower.masklen = 32;
      state->block_upper.family = AF_INET;
      state->block_upper.masklen = 32;

    } else if (startswith(buffer, "geoname_id,")) {
      found_version = 2;
      found_type = FILETYPE_LOC;
      state->current_column = state->first_column = LOCATION2_COL_FIRSTCOL;
      state->parse_row = parse_maxmind_location2_row;
      // initialize state specific to location2
      state->record = NULL;
      state->loc_records = kh_init(ipm_records);

    } else if (startswith(buffer, "network,")) {
      found_version = 2;
      found_type = FILETYPE_BLK;
      state->current_column = state->first_column = BLOCKS2_COL_FIRSTCOL;
      state->parse_row = parse_blocks2_row;
      // initialize state specific to blocks2
      state->record = NULL;
      state->loc_id = 0;

    } else {
      break;
    }

    state->current_line++;
    continue;
  }

  if (found_type != filetype) {
    ipmeta_log(__func__, "Error: %s is not a MaxMind %s file",
        filename, filetype_name[filetype]);
    goto end;
  }
  if (state->maxmind_version != 0 && state->maxmind_version != found_version) {
    ipmeta_log(__func__, "Error: cannot mix files with different versions");
    goto end;
  }
  state->maxmind_version = found_version;

  csv_init(&(state->parser), CSV_STRICT | CSV_REPALL_NL | CSV_STRICT_FINI |
      CSV_APPEND_NULL | CSV_EMPTY_IS_NULL);

  while ((read = wandio_read(file, &buffer, BUFFER_LEN)) > 0) {
    if (csv_parse(&(state->parser), buffer, read, parse_maxmind_cell,
                  state->parse_row, provider) != read) {
      ipmeta_log(__func__, "Error parsing %s file", provider->name);
      ipmeta_log(__func__, "CSV Error: %s",
                 csv_strerror(csv_error(&(state->parser))));
      goto end;
    }
  }
  if (read < 0) {
    ipmeta_log(__func__, "Error reading file %s", filename);
    goto end;
  }

  if (csv_fini(&(state->parser), parse_maxmind_cell, state->parse_row,
        provider) != 0) {
    ipmeta_log(__func__, "Error parsing %s file %s", provider->name, filename);
    ipmeta_log(__func__, "CSV Error: %s",
               csv_strerror(csv_error(&(state->parser))));
    goto end;
  }

  rc = 0; // success

end:
  csv_free(&(state->parser));
  wandio_destroy(file);
  return rc;
}

/* ----- Class Helper Functions below here ------ */

#if 0
/** Array of ISO 3 char country codes. Extracted from libGeoIP v1.5.0 */
static const char *country_code_iso3[] = {
  "???","AP","EU","AND","ARE","AFG","ATG","AIA","ALB","ARM","CUW",
  "AGO","ATA","ARG","ASM","AUT","AUS","ABW","AZE","BIH","BRB",
  "BGD","BEL","BFA","BGR","BHR","BDI","BEN","BMU","BRN","BOL",
  "BRA","BHS","BTN","BVT","BWA","BLR","BLZ","CAN","CCK","COD",
  "CAF","COG","CHE","CIV","COK","CHL","CMR","CHN","COL","CRI",
  "CUB","CPV","CXR","CYP","CZE","DEU","DJI","DNK","DMA","DOM",
  "DZA","ECU","EST","EGY","ESH","ERI","ESP","ETH","FIN","FJI",
  "FLK","FSM","FRO","FRA","SXM","GAB","GBR","GRD","GEO","GUF",
  "GHA","GIB","GRL","GMB","GIN","GLP","GNQ","GRC","SGS","GTM",
  "GUM","GNB","GUY","HKG","HMD","HND","HRV","HTI","HUN","IDN",
  "IRL","ISR","IND","IOT","IRQ","IRN","ISL","ITA","JAM","JOR",
  "JPN","KEN","KGZ","KHM","KIR","COM","KNA","PRK","KOR","KWT",
  "CYM","KAZ","LAO","LBN","LCA","LIE","LKA","LBR","LSO","LTU",
  "LUX","LVA","LBY","MAR","MCO","MDA","MDG","MHL","MKD","MLI",
  "MMR","MNG","MAC","MNP","MTQ","MRT","MSR","MLT","MUS","MDV",
  "MWI","MEX","MYS","MOZ","NAM","NCL","NER","NFK","NGA","NIC",
  "NLD","NOR","NPL","NRU","NIU","NZL","OMN","PAN","PER","PYF",
  "PNG","PHL","PAK","POL","SPM","PCN","PRI","PSE","PRT","PLW",
  "PRY","QAT","REU","ROU","RUS","RWA","SAU","SLB","SYC","SDN",
  "SWE","SGP","SHN","SVN","SJM","SVK","SLE","SMR","SEN","SOM",
  "SUR","STP","SLV","SYR","SWZ","TCA","TCD","ATF","TGO","THA",
  "TJK","TKL","TKM","TUN","TON","TLS","TUR","TTO","TUV","TWN",
  "TZA","UKR","UGA","UMI","USA","URY","UZB","VAT","VCT","VEN",
  "VGB","VIR","VNM","VUT","WLF","WSM","YEM","MYT","SRB","ZAF",
  "ZMB","MNE","ZWE","A1","A2","O1","ALA","GGY","IMN","JEY",
  "BLM","MAF", "BES", "SSD",
  /* see above about AN */
  "ANT",
};

/** Array of country names. Extracted from libGeoIP v1.4.8 */
static const char *country_name[] = {
  "Unknown","Asia/Pacific Region","Europe","Andorra","United Arab Emirates",
  "Afghanistan","Antigua and Barbuda","Anguilla","Albania","Armenia",
  "Cura" "\xe7" "ao","Angola","Antarctica","Argentina","American Samoa",
  "Austria","Australia","Aruba","Azerbaijan","Bosnia and Herzegovina",
  "Barbados","Bangladesh","Belgium","Burkina Faso","Bulgaria","Bahrain",
  "Burundi","Benin","Bermuda","Brunei Darussalam","Bolivia","Brazil",
  "Bahamas","Bhutan","Bouvet Island","Botswana","Belarus","Belize",
  "Canada","Cocos (Keeling) Islands","Congo, The Democratic Republic of the",
  "Central African Republic","Congo","Switzerland","Cote D'Ivoire",
  "Cook Islands","Chile","Cameroon","China","Colombia","Costa Rica","Cuba",
  "Cape Verde","Christmas Island","Cyprus","Czech Republic","Germany",
  "Djibouti","Denmark","Dominica","Dominican Republic","Algeria","Ecuador",
  "Estonia","Egypt","Western Sahara","Eritrea","Spain","Ethiopia","Finland",
  "Fiji","Falkland Islands (Malvinas)","Micronesia, Federated States of",
  "Faroe Islands","France","Sint Maarten (Dutch part)","Gabon",
  "United Kingdom","Grenada","Georgia","French Guiana","Ghana","Gibraltar",
  "Greenland","Gambia","Guinea","Guadeloupe","Equatorial Guinea","Greece",
  "South Georgia and the South Sandwich Islands","Guatemala","Guam",
  "Guinea-Bissau","Guyana","Hong Kong","Heard Island and McDonald Islands",
  "Honduras","Croatia","Haiti","Hungary","Indonesia","Ireland","Israel",
  "India","British Indian Ocean Territory","Iraq","Iran, Islamic Republic of",
  "Iceland","Italy","Jamaica","Jordan","Japan","Kenya","Kyrgyzstan","Cambodia",
  "Kiribati","Comoros","Saint Kitts and Nevis",
  "Korea, Democratic People's Republic of","Korea, Republic of","Kuwait",
  "Cayman Islands","Kazakhstan","Lao People's Democratic Republic","Lebanon",
  "Saint Lucia","Liechtenstein","Sri Lanka","Liberia","Lesotho","Lithuania",
  "Luxembourg","Latvia","Libyan Arab Jamahiriya","Morocco","Monaco",
  "Moldova, Republic of","Madagascar","Marshall Islands","Macedonia","Mali",
  "Myanmar","Mongolia","Macau","Northern Mariana Islands","Martinique",
  "Mauritania","Montserrat","Malta","Mauritius","Maldives","Malawi","Mexico",
  "Malaysia","Mozambique","Namibia","New Caledonia","Niger","Norfolk Island",
  "Nigeria","Nicaragua","Netherlands","Norway","Nepal","Nauru","Niue",
  "New Zealand","Oman","Panama","Peru","French Polynesia","Papua New Guinea",
  "Philippines","Pakistan","Poland","Saint Pierre and Miquelon",
  "Pitcairn Islands","Puerto Rico","Palestinian Territory","Portugal","Palau",
  "Paraguay","Qatar","Reunion","Romania","Russian Federation","Rwanda",
  "Saudi Arabia","Solomon Islands","Seychelles","Sudan","Sweden","Singapore",
  "Saint Helena","Slovenia","Svalbard and Jan Mayen","Slovakia","Sierra Leone",
  "San Marino","Senegal","Somalia","Suriname","Sao Tome and Principe",
  "El Salvador","Syrian Arab Republic","Swaziland","Turks and Caicos Islands",
  "Chad","French Southern Territories","Togo","Thailand","Tajikistan",
  "Tokelau","Turkmenistan","Tunisia","Tonga","Timor-Leste","Turkey",
  "Trinidad and Tobago","Tuvalu","Taiwan","Tanzania, United Republic of",
  "Ukraine","Uganda","United States Minor Outlying Islands","United States",
  "Uruguay","Uzbekistan","Holy See (Vatican City State)",
  "Saint Vincent and the Grenadines","Venezuela","Virgin Islands, British",
  "Virgin Islands, U.S.","Vietnam","Vanuatu","Wallis and Futuna","Samoa",
  "Yemen","Mayotte","Serbia","South Africa","Zambia","Montenegro","Zimbabwe",
  "Anonymous Proxy","Satellite Provider","Other","Aland Islands","Guernsey",
  "Isle of Man","Jersey","Saint Barthelemy","Saint Martin",
  "Bonaire, Saint Eustatius and Saba", "South Sudan",
  /* again, see above about AN */
  "Netherlands Antilles",
};
#endif


#if 0
static const char *get_iso2(int country_id)
{
  assert(country_id < COUNTRY_CNT);
  return country_code_iso2[country_id];
}
#endif

#if 0
static const char *get_iso3(int country_id)
{
  assert(country_id < COUNTRY_CNT);
  return country_code_iso3[country_id];
}

static int get_iso3_list(const char ***countries)
{
  *countries = country_code_iso3;
  return COUNTRY_CNT;
}

static const char *get_country_name(int country_id)
{
  assert(country_id < COUNTRY_CNT);
  return country_name[country_id];
}

static int get_country_name_list(const char ***countries)
{
  *countries = country_name;
  return COUNTRY_CNT;
}

static const char *get_continent(int country_id)
{
  assert(country_id < COUNTRY_CNT);
  return country_continent[country_id];
}
#endif

/* ===== PUBLIC FUNCTIONS BELOW THIS POINT ===== */

ipmeta_provider_t *ipmeta_provider_maxmind_alloc()
{
  return &ipmeta_provider_maxmind;
}

int ipmeta_provider_maxmind_init(ipmeta_provider_t *provider, int argc,
                                 char **argv)
{
  ipmeta_provider_maxmind_state_t *state;

  /* allocate our state */
  if ((state = malloc_zero(sizeof(ipmeta_provider_maxmind_state_t))) == NULL) {
    ipmeta_log(__func__, "could not malloc ipmeta_provider_maxmind_state_t");
    return -1;
  }
  ipmeta_provider_register_state(provider, state);

  /* parse the command line args */
  if (parse_args(provider, argc, argv) != 0) {
    return -1;
  }

  assert(state->locations_file != NULL && state->blocks_file[0] != NULL);

  // load locations
  if (read_maxmind_file(provider, FILETYPE_LOC, state->locations_file) != 0) {
    ipmeta_log(__func__, "failed to parse locations file");
    goto err;
  }

  // load blocks
  for (int i = 0; i < state->blocks_file_cnt; i++) {
    if (read_maxmind_file(provider, FILETYPE_BLK, state->blocks_file[i]) != 0) {
      ipmeta_log(__func__, "failed to parse blocks file");
      goto err;
    }
  }

  /* ready to rock n roll */
  return 0;

err:
  usage(provider);
  return -1;
}

void ipmeta_provider_maxmind_free(ipmeta_provider_t *provider)
{
  ipmeta_provider_maxmind_state_t *state = STATE(provider);
  if (state != NULL) {
    if (state->locations_file != NULL) {
      free(state->locations_file);
      state->locations_file = NULL;
    }

    for (int i = 0; i < state->blocks_file_cnt; i++) {
      free(state->blocks_file[i]);
      state->blocks_file[i] = NULL;
    }

    if (state->country_continent != NULL) {
      kh_destroy(u16u16, state->country_continent);
      state->country_continent = NULL;
    }

    if (state->loc_records) {
      kh_free_vals(ipm_records, state->loc_records, ipmeta_free_record);
      kh_destroy(ipm_records, state->loc_records);
      state->loc_records = NULL;
    }

    ipmeta_provider_free_state(provider);
  }
  return;
}

int ipmeta_provider_maxmind_lookup_pfx(ipmeta_provider_t *provider, int family,
    void *addrp, uint8_t pfxlen, ipmeta_record_set_t *records)
{
  /* just call the lookup helper func in provider manager */
  return ipmeta_provider_lookup_pfx(provider, family, addrp, pfxlen, records);
}

int ipmeta_provider_maxmind_lookup_addr(ipmeta_provider_t *provider, int family,
    void *addrp, ipmeta_record_set_t *found)
{
  /* just call the lookup helper func in provider manager */
  return ipmeta_provider_lookup_addr(provider, family, addrp, found);
}

void ipmeta_provider_maxmind_free_record(ipmeta_record_t *record)
{
  ipmeta_free_record(record);
}

/* ========== HELPER FUNCTIONS ========== */

int ipmeta_provider_maxmind_get_iso2_list(const char ***countries)
{
  *countries = country_code_iso2;
  return COUNTRY_CNT;
}

int ipmeta_provider_maxmind_get_country_continent_list(const char ***continents)
{
  *continents = country_continent;
  return COUNTRY_CNT;
}
