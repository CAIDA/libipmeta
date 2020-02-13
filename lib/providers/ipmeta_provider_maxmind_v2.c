/*
 * libipmeta
 *
 * Alistair King, CAIDA, UC San Diego
 * corsaro-info@caida.org
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
#include "ip_utils.h"

#include "ipmeta_ds.h"
#include "ipmeta_provider_maxmind_v2.h"

#define PROVIDER_NAME "maxmind_v2"

#define STATE(provname) (IPMETA_PROVIDER_STATE(maxmind_v2, provname))

#define BUFFER_LEN 1024

KHASH_INIT(loctemp_rcd, uint32_t, ipmeta_record_t, 1, kh_int_hash_func,
           kh_int_hash_equal)

/** The default file name for the locations file */
#define LOCATIONS_FILE_NAME "GeoLiteCity-Location.csv.gz"

/** The default file name for the blocks file */
#define BLOCKS_FILE_NAME "GeoLiteCity-Blocks.csv.gz"

/** The basic fields that every instance of this provider have in common */
static ipmeta_provider_t ipmeta_provider_maxmind_v2 = {
  IPMETA_PROVIDER_MAXMIND_v2, PROVIDER_NAME,
  IPMETA_PROVIDER_GENERATE_PTRS(maxmind_v2)};

/** Holds the state for an instance of this provider */
typedef struct ipmeta_provider_maxmind_v2_state {

  /* info extracted from args */
  char *locations_file;
  char *blocks_file;

  /* State for CSV parser */
  struct csv_parser parser;
  int current_line;
  int current_column;
  /* create a temporary record */
  ipmeta_record_t tmp_record;
  ip_prefix_t block_network;

  /* create a hash table that maps geonameid to locations */
  khash_t(loctemp_rcd) * locations;
} ipmeta_provider_maxmind_v2_state_t;

/** Columns in the maxmind_v2 locations CSV file */
typedef enum locations_cols {
  /** ID */
  LOCATION_COL_ID = 0,
  /** 2 char local code */
  LOCATION_COL_LOCALECODE = 1,
  /** 2 Char Continent/Region Code */
  LOCATION_COL_CONTINENTCODE = 2,
  /** Region/Continent String */
  LOCATION_COL_REGION = 3,
  /** 2 Char Country Code */
  LOCATION_COL_CC = 4,
  /** Country String */
  LOCATION_COL_COUNTRY = 5,
  /** Columns 6 is not parsed */
  LOCATION_COL_ISO1_CODE = 6,
  /** City_name */
  LOCATION_COL_ISO1_NAME = 7,
  /** Columns 8-9 not parsed */
  LOCATION_COL_ISO2_CODE = 8,
  LOCATION_COL_ISO2_NAME = 9,
  /** City String */
  LOCATION_COL_CITY = 10,
  /** Metro Code */
  LOCATION_COL_METRO = 11,
  /** Time zone string */
  LOCATION_COL_TIMEZONE = 12,
  /** Int 0 or 1 is_in_EU? */
  LOCATION_COL_IN_EU = 13,

  /** Total number of columns in locations table */
  LOCATION_COL_COUNT = 14
} locations_cols_t;

/** The columns in the maxmind_v2 locations CSV file */
typedef enum blocks_cols {
  /** Network */
  BLOCKS_COL_NETWORK = 0,
  /** Geoname ID */
  BLOCKS_COL_GEONAMEID = 1,
  /** Registered Country Geoname ID */
  BLOCKS_COL_CCGEONAMEID = 2,
  /** Column 3 is not parsed */
  BLOCKS_COL_REPRESENTED_CCGEONAME_ID = 3,
  /** Int 0 or 1 is proxy? */
  BLOCKS_COL_PROXY = 4,
  /** Int 0 or 1 is satellite provider? */
  BLOCKS_COL_SATTELLITEPROV = 5,
  /** Postal Code String */
  BLOCKS_COL_POSTAL = 6,
  /** Latitude */
  BLOCKS_COL_LAT = 7,
  /** Longitude */
  BLOCKS_COL_LONG = 8,
  /** Accuracy radius */
  BLOCKS_COL_ACCURACY = 9,

  /** Total number of columns in blocks table */
  BLOCKS_COL_COUNT = 10
} blocks_cols_t;

/** The number of header rows in the maxmind_v2 CSV files */
#define HEADER_ROW_CNT 1

/** Print usage information to stderr */
static void usage(ipmeta_provider_t *provider)
{
  fprintf(
    stderr,
    "provider usage: %s (-l locations -b blocks)|(-d directory)\n"
    "       -d            directory containing blocks and location files\n"
    "       -b            blocks file (must be used with -l)\n"
    "       -l            locations file (must be used with -b)\n",
    provider->name);
}

/** Parse the arguments given to the provider
 * @todo add option to choose datastructure
 */
static int parse_args(ipmeta_provider_t *provider, int argc, char **argv)
{
  ipmeta_provider_maxmind_v2_state_t *state = STATE(provider);
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
      state->blocks_file = strdup(optarg);
      break;
    case 'D':
      fprintf(
        stderr,
        "WARNING: -D option is no longer supported by individual providers.\n");
      break;
    case 'd':
      /* no need to dup right now because we will do it later */
      directory = optarg;
      break;

    case 'l':
      state->locations_file = strdup(optarg);
      break;

    case '?':
    case ':':
    default:
      usage(provider);
      return -1;
    }
  }

  if (directory != NULL) {
    /* check if they were daft and specified explicit files too */
    if (state->locations_file != NULL || state->blocks_file != NULL) {
      fprintf(stderr, "WARNING: both directory and file name specified.\n");

      /* free up the dup'd strings */
      if (state->locations_file != NULL) {
        free(state->locations_file);
        state->locations_file = NULL;
      }

      if (state->blocks_file != NULL) {
        free(state->blocks_file);
        state->blocks_file = NULL;
      }
    }

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

    if ((state->blocks_file = malloc(strlen(directory) + 1 +
                                     strlen(BLOCKS_FILE_NAME) + 1)) == NULL) {
      ipmeta_log(__func__, "could not malloc blocks file string");
      return -1;
    }

    /** @todo make this check for both .gz and non-.gz files */

    ptr = stpncpy(state->locations_file, directory, strlen(directory));
    *ptr++ = '/';
    /* last copy needs a +1 to get the terminating nul. d'oh */
    ptr = stpncpy(ptr, LOCATIONS_FILE_NAME, strlen(LOCATIONS_FILE_NAME) + 1);

    ptr = stpncpy(state->blocks_file, directory, strlen(directory));
    *ptr++ = '/';
    ptr = stpncpy(ptr, BLOCKS_FILE_NAME, strlen(BLOCKS_FILE_NAME) + 1);
  }

  if (state->locations_file == NULL || state->blocks_file == NULL) {
    fprintf(stderr, "ERROR: %s requires either '-d' or both '-b' and '-l'\n",
            provider->name);
    usage(provider);
    return -1;
  }
  return 0;
}

/* Parse a maxmind_v2 location cell */
static void parse_maxmind_v2_location_cell(void *s, size_t i, void *data)
{
  ipmeta_provider_t *provider = (ipmeta_provider_t *)data;
  ipmeta_provider_maxmind_v2_state_t *state = STATE(provider);
  ipmeta_record_t *tmp = &(state->tmp_record);
  char *tok = (char *)s;
  char *end;
  /* skip the first line */
  if (state->current_line < HEADER_ROW_CNT) {
    return;
  }

  switch (state->current_column) {
  case LOCATION_COL_ID:
    /* init this record */
    tmp->id = strtol(tok, &end, 10);
    if (end == tok || *end != '\0' || errno == ERANGE) {
      ipmeta_log(__func__, "Invalid ID Value (%s)", tok);
      state->parser.status = CSV_EUSER;
      return;
    }
    break;

  case LOCATION_COL_LOCALECODE:
    /* country code */
    if (tok == NULL || strlen(tok) != 2) {
      ipmeta_log(__func__, "Invalid Locale Code (%s)", tok);
      state->parser.status = CSV_EUSER;
      return;
    }
    memcpy(tmp->locale_code, tok, 2);
    break;

  case LOCATION_COL_CONTINENTCODE:
    /* continent/region code */
    if (tok == NULL || strlen(tok) != 2) {
      ipmeta_log(__func__, "Invalid  Continent Code (%s)", tok);
      state->parser.status = CSV_EUSER;
      return;
    }
    if (tok[0] == '-' && tok[1] == '-') {
      tok[0] = '?';
      tok[1] = '?';
    }
    memcpy(tmp->continent_code, tok, 2);
    break;

  case LOCATION_COL_REGION:
    /* region string */
    if (tok != NULL) {
      if ((tmp->region = strdup(tok)) == NULL) {
        ipmeta_log(__func__, "Region code copy failed (%s)", tok);
        state->parser.status = CSV_EUSER;
        return;
      }
    }
    break;

  case LOCATION_COL_CC:
    /* country code */
    if (tok != NULL && strlen(tok) == 2) {
      if (tok[0] == '-' && tok[1] == '-') {
        tok[0] = '?';
        tok[1] = '?';
      }
      memcpy(tmp->country_code, tok, 2);
    }
    break;

  case LOCATION_COL_COUNTRY:
    /* country */
    if (tok != NULL) {
      tmp->country = strndup(tok, strlen(tok));
    }
    break;

  case LOCATION_COL_ISO1_CODE:
    /* skip column */
    break;

  case LOCATION_COL_ISO1_NAME:
    /* subdivision name string */
    if (tok != NULL) {
      if ((tmp->sub_name = strdup(tok)) == NULL) {
        ipmeta_log(__func__, "Region code copy failed (%s)", tok);
        state->parser.status = CSV_EUSER;
        return;
      }
    }
    break;

  case LOCATION_COL_ISO2_CODE:
  case LOCATION_COL_ISO2_NAME:
    /* skip column */
    break;

  case LOCATION_COL_CITY:
    /* city */
    if (tok != NULL) {
      tmp->city = strndup(tok, strlen(tok));
    }
    break;

  case LOCATION_COL_METRO:
    /* metro code*/
    if (tok != NULL) {
      tmp->metro_code = strtol(tok, &end, 10);
      if (*tok != '\0' && (end == tok || *end != '\0' || errno == ERANGE)) {
        ipmeta_log(__func__, "Invalid Metro Value (%s)", tok);
        state->parser.status = CSV_EUSER;
        return;
      }
    }
    break;

  case LOCATION_COL_TIMEZONE:
    /* Timezone */
    if (tok != NULL) {
      tmp->timezone = strndup(tok, strlen(tok));
    }
    break;

  case LOCATION_COL_IN_EU:
    /* In EU or not */
    if (tok != NULL) {
      tmp->in_eu = strtol(tok, &end, 10);
    }
    break;

  default:
    ipmeta_log(__func__, "Invalid maxmind_v2 Location Column (%d:%d)",
               state->current_line, state->current_column);
    state->parser.status = CSV_EUSER;
    return;
    break;
  }
  /* move on to the next column */
  state->current_column++;
}

/** Handle an end-of-row event from the CSV parser */
static void parse_maxmind_v2_location_row(int c, void *data)
{
  ipmeta_provider_t *provider = (ipmeta_provider_t *)data;
  ipmeta_provider_maxmind_v2_state_t *state = STATE(provider);
  khiter_t khiter;
  int khret;

  /* skip the first two lines */
  if (state->current_line < HEADER_ROW_CNT) {
    state->current_line++;
    return;
  }

  /* at the end of successful row parsing, current_column will be 9 */
  /* make sure we parsed exactly as many columns as we anticipated */
  if (state->current_column != LOCATION_COL_COUNT) {
    ipmeta_log(__func__,
               "ERROR: Expecting %d columns in the locations file, "
               "but actually got %d",
               LOCATION_COL_COUNT, state->current_column);
    state->parser.status = CSV_EUSER;
    return;
  }
  /* done processing the line */

  assert(state->tmp_record.id > 0);

  /* create a mapping for this location line */
  khiter = kh_put(loctemp_rcd, state->locations, state->tmp_record.id, &khret);
  memcpy(&(kh_value(state->locations, khiter)), &(state->tmp_record),
         sizeof(ipmeta_record_t));
  /* increment the current line */
  state->current_line++;
  /* reset the current column */
  state->current_column = 0;
  /* reset the temp record */
  memset(&(state->tmp_record), 0, sizeof(ipmeta_record_t));
  return;
}

/** Read a locations file */
static int read_locations(ipmeta_provider_t *provider, io_t *file)
{
  ipmeta_provider_maxmind_v2_state_t *state = STATE(provider);
  char buffer[BUFFER_LEN];
  int read = 0;
  /* reset the state variables before we start */
  state->current_column = 0;
  state->current_line = 0;
  memset(&(state->tmp_record), 0, sizeof(ipmeta_record_t));
  /* options for the csv parser */
  int options = CSV_STRICT | CSV_REPALL_NL | CSV_STRICT_FINI | CSV_APPEND_NULL |
                CSV_EMPTY_IS_NULL;

  csv_init(&(state->parser), options);
  while ((read = wandio_read(file, &buffer, BUFFER_LEN)) > 0) {
    if (csv_parse(&(state->parser), buffer, read,
                  parse_maxmind_v2_location_cell, parse_maxmind_v2_location_row,
                  provider) != read) {
      ipmeta_log(__func__, "Error parsing %s Location file", provider->name);
      ipmeta_log(__func__, "CSV Error: %s",
                 csv_strerror(csv_error(&(state->parser))));
      return -1;
    }
  }
  if (csv_fini(&(state->parser), parse_maxmind_v2_location_cell,
               parse_maxmind_v2_location_row, provider) != 0) {
    ipmeta_log(__func__, "Error parsing %s Location file", provider->name);
    ipmeta_log(__func__, "CSV Error: %s",
               csv_strerror(csv_error(&(state->parser))));
    return -1;
  }
  csv_free(&(state->parser));
  return 0;
}

/** Parse a maxmind_v2 blocks cell */
static void parse_blocks_cell(void *s, size_t i, void *data)
{
  ipmeta_provider_t *provider = (ipmeta_provider_t *)data;
  ipmeta_provider_maxmind_v2_state_t *state = STATE(provider);
  ipmeta_record_t *tmp = &(state->tmp_record);
  char *tok = (char *)s;
  char *end;
  char *mask_str;

  /* skip the first lines */
  if (state->current_line < HEADER_ROW_CNT) {
    return;
  }

  switch (state->current_column) {
  case BLOCKS_COL_NETWORK:
    /* extract the mask from the prefix */
    if ((mask_str = strchr(tok, '/')) != NULL) {
      *mask_str = '\0';
      mask_str++;
      state->block_network.masklen = atoi(mask_str);
    } else {
      state->block_network.masklen = 32;
    }
    state->block_network.addr = inet_addr(tok);
    if (state->block_network.addr == INADDR_NONE) {
      ipmeta_log(__func__, "Invalid Start IP Value (%s)", tok);
      state->parser.status = CSV_EUSER;
    }
    break;

  case BLOCKS_COL_GEONAMEID:
    /* Geoname ID*/
    if (tok != NULL) {
      /* id */
      tmp->id = strtol(tok, &end, 10);
      if (end == tok || *end != '\0' || errno == ERANGE) {
        ipmeta_log(__func__, "Invalid ID Value (%s)", tok);
        state->parser.status = CSV_EUSER;
      }
    }
    break;

  /* In case tmp->id == 0 i.e. GEONAMEID is NULL, the id field is filled
   * with the content of CCGEONAMEID rather than GEONAMEID. */
  case BLOCKS_COL_CCGEONAMEID:
    /* Registered country geoname ID */
    if (tok != NULL && tmp->id == 0) {
      /* id */
      tmp->id = strtol(tok, &end, 10);
      if (end == tok || *end != '\0' || errno == ERANGE) {
        ipmeta_log(__func__, "Invalid ID Value (%s)", tok);
        state->parser.status = CSV_EUSER;
      }
    }
    break;

  case BLOCKS_COL_REPRESENTED_CCGEONAME_ID:
    /* skip column */
    break;

  case BLOCKS_COL_PROXY:
    /* proxy? */
    tmp->proxy = atoi(tok);
    break;

  case BLOCKS_COL_SATTELLITEPROV:
    /* sattelite provider? */
    if (tok != NULL) {
      tmp->satprov = atoi(tok);
    }
    break;

  case BLOCKS_COL_POSTAL:
    /* postal code */
    if (tok != NULL) {
      tmp->post_code = strndup(tok, strlen(tok));
    }
    break;

  case BLOCKS_COL_LAT:
    /* latitude */
    if (tok != NULL) {
      tmp->latitude = strtof(tok, &end);
      if (end == tok || *end != '\0' || errno == ERANGE) {
        ipmeta_log(__func__, "Invalid Latitude Value (%s)", tok);
        state->parser.status = CSV_EUSER;
        return;
      }
    }
    break;

  case BLOCKS_COL_LONG:
    /* longitude */
    if (tok != NULL) {
      tmp->longitude = strtof(tok, &end);
      if (end == tok || *end != '\0' || errno == ERANGE) {
        ipmeta_log(__func__, "Invalid Longitude Value (%s)", tok);
        state->parser.status = CSV_EUSER;
        return;
      }
    }
    break;

  case BLOCKS_COL_ACCURACY:
    /* accuracy */
    if (tok != NULL) {
      tmp->accuracy = strtol(tok, &end, 10);
      if (*tok != '\0' && (end == tok || *end != '\0' || errno == ERANGE)) {
        ipmeta_log(__func__, "Invalid Area Code Value (%s)", tok);
        state->parser.status = CSV_EUSER;
        return;
      }
    }
    break;

  default:
    ipmeta_log(__func__, "Invalid Blocks Column (%d:%d)", state->current_line,
               state->current_column);
    state->parser.status = CSV_EUSER;
    break;
  }

  /* move on to the next column */
  state->current_column++;
}

static void parse_blocks_row(int c, void *data)
{
  ipmeta_provider_t *provider = (ipmeta_provider_t *)data;
  ipmeta_provider_maxmind_v2_state_t *state = STATE(provider);
  /* Defining a block record*/
  ipmeta_record_t *block_record = &(state->tmp_record);
  /* the pointer to the final record */
  ipmeta_record_t *record = NULL;
  /*location record */
  ipmeta_record_t *loc_record = NULL;
  khiter_t khiter;

  if (state->current_line < HEADER_ROW_CNT) {
    state->current_line++;
    return;
  }

  /* done processing the line */

  /* make sure we parsed exactly as many columns as we anticipated */
  if (state->current_column != BLOCKS_COL_COUNT) {
    ipmeta_log(__func__,
               "ERROR: Expecting %d columns in the blocks file, "
               "but actually got %d",
               BLOCKS_COL_COUNT, state->current_column);
    state->parser.status = CSV_EUSER;
    return;
  }

  /* In the following, we merge a location record into a new block record,
   * because several locations (CC1, Cont1, pref1 - CC2, cont2, pref2 - CC3,
   * cont3, pref3) can correspond to the same block (lat,
   * long, accuracy); we thus use block_record->id as key of the final record */
  if ((record = ipmeta_provider_init_record(provider, state->current_line)) ==
      NULL) {
    ipmeta_log(__func__, "ERROR: Could not initialize meta record");
    state->parser.status = CSV_EUSER;
    return;
  }

  /* Check if the block_record has an id that is non-null, before trying to
  match it to its corresponding location. This allows to keep Geoname_id as
  a key in the hash table loctemp_rcd*/
  if (block_record->id != 0) {
    if ((khiter = kh_get(loctemp_rcd, state->locations, block_record->id)) ==
        kh_end(state->locations)) {
      ipmeta_log(__func__, "ERROR: Invalid geoname id (%" PRIu32 ")",
                 block_record->id);
      state->parser.status = CSV_EUSER;
      return;
    }
    /* copy anything in the locations row into the temp record. */
    loc_record = &(kh_value(state->locations, khiter));
    memcpy(record, loc_record, sizeof(ipmeta_record_t));
    /* Duplicate fields in loc_record, get the pointer to the null terminated
    byte thus obtained and push these into the block record to avoid a
    non-allocated memory error. */
    if (loc_record->region != NULL &&
        (record->region = strdup(loc_record->region)) == NULL) {
      ipmeta_log(__func__, "ERROR: Failed to duplicate region");
    }
    if (loc_record->city != NULL &&
        (record->city = strdup(loc_record->city)) == NULL) {
      ipmeta_log(__func__, "ERROR: Failed to duplicate city");
    }
    if (loc_record->timezone != NULL &&
        (record->timezone = strdup(loc_record->timezone)) == NULL) {
      ipmeta_log(__func__, "ERROR: Failed to duplicate timezone");
    }
    if (loc_record->country != NULL &&
        (record->country = strdup(loc_record->country)) == NULL) {
      ipmeta_log(__func__, "ERROR: Failed to duplicate country");
    }
    if (loc_record->sub_name != NULL &&
        (record->sub_name = strdup(loc_record->sub_name)) == NULL) {
      ipmeta_log(__func__, "ERROR: Failed to duplicate sub_name");
    }
  }

  /* Fill in the values for the remaining  keys of the block record */
  record->latitude = block_record->latitude;
  record->longitude = block_record->longitude;
  record->accuracy = block_record->accuracy;
  record->post_code = block_record->post_code;
  record->satprov = block_record->satprov;
  record->proxy = block_record->proxy;
  record->source = provider->id;

  /* Associate network address and prefix lenght to the right record.*/
  if (ipmeta_provider_associate_record(provider, state->block_network.addr,
                                       state->block_network.masklen,
                                       record) != 0) {
    ipmeta_log(__func__, "ERROR: Failed to associate record");
    state->parser.status = CSV_EUSER;
    return;
  }

  /* increment the current line */
  state->current_line++;
  /* reset the current column */
  state->current_column = 0;
  /* empty block record before continuing */
  memset(&(state->tmp_record), 0, sizeof(ipmeta_record_t));
}

/** Read a blocks file  */
static int read_blocks(ipmeta_provider_t *provider, io_t *file)
{
  ipmeta_provider_maxmind_v2_state_t *state = STATE(provider);
  char buffer[BUFFER_LEN];
  int read = 0;

  /* reset the state variables before we start*/
  state->current_column = 0;
  state->current_line = 0;
  state->block_network.masklen = 32;
  memset(&(state->tmp_record), 0, sizeof(ipmeta_record_t));

  /* options for the csv parser */
  int options = CSV_STRICT | CSV_REPALL_NL | CSV_STRICT_FINI | CSV_APPEND_NULL |
                CSV_EMPTY_IS_NULL;

  csv_init(&(state->parser), options);

  while ((read = wandio_read(file, &buffer, BUFFER_LEN)) > 0) {
    if (csv_parse(&(state->parser), buffer, read, parse_blocks_cell,
                  parse_blocks_row, provider) != read) {
      ipmeta_log(__func__, "Error parsing Blocks file");
      ipmeta_log(__func__, "CSV Error: %s",
                 csv_strerror(csv_error(&(state->parser))));
      return -1;
    }
  }
  if (csv_fini(&(state->parser), parse_blocks_cell, parse_blocks_row,
               provider) != 0) {
    ipmeta_log(__func__, "Error parsing maxmind_v2 Location file");
    ipmeta_log(__func__, "CSV Error: %s",
               csv_strerror(csv_error(&(state->parser))));
    return -1;
  }
  csv_free(&(state->parser));
  return 0;
}

/* ===== PUBLIC FUNCTIONS BELOW THIS POINT ===== */
ipmeta_provider_t *ipmeta_provider_maxmind_v2_alloc()
{
  return &ipmeta_provider_maxmind_v2;
}

int ipmeta_provider_maxmind_v2_init(ipmeta_provider_t *provider, int argc,
                                    char **argv)
{
  ipmeta_provider_maxmind_v2_state_t *state;
  io_t *file = NULL;
  /* allocate our state */
  if ((state = malloc_zero(sizeof(ipmeta_provider_maxmind_v2_state_t))) ==
      NULL) {
    ipmeta_log(__func__, "could not malloc ipmeta_provider_maxmind_v2_state_t");
    return -1;
  }
  ipmeta_provider_register_state(provider, state);
  /* parse the command line args */
  if (parse_args(provider, argc, argv) != 0) {
    return -1;
  }
  /* Initialize the locations hash */
  state->locations = kh_init(loctemp_rcd);
  assert(state->locations_file != NULL && state->blocks_file != NULL);
  /* open the locations file */
  if ((file = wandio_create(state->locations_file)) == NULL) {
    ipmeta_log(__func__, "failed to open location file '%s'",
               state->locations_file);
    return -1;
  }
  /* populate the locations hash */
  if (read_locations(provider, file) != 0) {
    ipmeta_log(__func__, "failed to parse locations file");
    goto err;
  }
  /* close the locations file */
  wandio_destroy(file);
  file = NULL;
  /* open the blocks file */
  if ((file = wandio_create(state->blocks_file)) == NULL) {
    ipmeta_log(__func__, "failed to open blocks file '%s'", state->blocks_file);
    goto err;
  }
  /* populate the ds (by joining on the id in the hash) */
  if (read_blocks(provider, file) != 0) {
    ipmeta_log(__func__, "failed to parse blocks file");
    goto err;
  }
  /* close the blocks file */
  wandio_destroy(file);
  /* ready to rock n roll */
  return 0;

err:
  if (file != NULL) {
    wandio_destroy(file);
  }
  usage(provider);
  return -1;
}

void ipmeta_provider_maxmind_v2_free(ipmeta_provider_t *provider)
{
  ipmeta_record_t *rec_ptr = NULL;
  khiter_t i;
  ipmeta_provider_maxmind_v2_state_t *state = STATE(provider);
  if (state == NULL) {
    return;
  }
  if (state->locations_file != NULL) {
    free(state->locations_file);
    state->locations_file = NULL;
  }
  if (state->blocks_file != NULL) {
    free(state->blocks_file);
    state->blocks_file = NULL;
  }

  /* destroy hash table locations */
  if (state->locations != NULL) {
    /* free the memory of each elment in the record */
    for (i = kh_begin(state->locations); i != kh_end(state->locations); ++i) {
      if (kh_exist(state->locations, i)) {
        rec_ptr = &(kh_value(state->locations, i));
        ipmeta_record_clear(rec_ptr);
      }
    }
    kh_destroy(loctemp_rcd, state->locations);
    state->locations = NULL;
  }
  ipmeta_provider_free_state(provider);
}

int ipmeta_provider_maxmind_v2_lookup(ipmeta_provider_t *provider,
                                      uint32_t addr, uint8_t mask,
                                      ipmeta_record_set_t *records)
{
  /* just call the lookup helper func in provider manager */
  return ipmeta_provider_lookup_records(provider, addr, mask, records);
}

int ipmeta_provider_maxmind_v2_lookup_single(ipmeta_provider_t *provider,
                                             uint32_t addr,
                                             ipmeta_record_set_t *found)
{
  /* just call the lookup helper func in provider manager */
  return ipmeta_provider_lookup_record_single(provider, addr, found);
}