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

#include "config.h"
#include "libipmeta_int.h"

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

#include "csv.h"
#include "ip_utils.h"
#include "khash.h"
#include "utils.h"

#include "ipmeta_provider_netacq_edge.h"

#define PROVIDER_NAME "netacq-edge"

#define STATE(provname)				\
  (IPMETA_PROVIDER_STATE(netacq_edge, provname))

#define BUFFER_LEN 1024

KHASH_INIT(u16u16, uint16_t, uint16_t, 1,
	   kh_int_hash_func, kh_int_hash_equal)

/** The basic fields that every instance of this provider have in common */
static ipmeta_provider_t ipmeta_provider_netacq_edge = {
  IPMETA_PROVIDER_NETACQ_EDGE,
  PROVIDER_NAME,
  IPMETA_PROVIDER_GENERATE_PTRS(netacq_edge)
};

/** Holds the state for an instance of this provider */
typedef struct ipmeta_provider_netacq_edge_state {
  /* info extracted from args */
  char *locations_file;
  char *blocks_file;

  /* State for CSV parser */
  struct csv_parser parser;
  int current_line;
  int current_column;
  ipmeta_record_t tmp_record;
  uint32_t block_id;
  ip_prefix_t block_lower;
  ip_prefix_t block_upper;

} ipmeta_provider_netacq_edge_state_t;

/** Provides a mapping from the integer continent code to the 2 character
    strings that we use in libipmeta */
static const char *continent_strings[] = {
  "--", /* 0 */
  "AF", /* 1 */
  "AN", /* 2 */
  "OC", /* 3 */
  "AS", /* 4 */
  "EU", /* 5 */
  "NA", /* 6 */
  "SA", /* 7 */
};

#define CONTINENT_MAX 7

/** The columns in the netacq_edge locations CSV file */
typedef enum locations_cols {
  /** ID */
  LOCATION_COL_ID        = 0,
  /** 2 Char Country Code */
  LOCATION_COL_CC        = 1,
  /** Region String */
  LOCATION_COL_REGION    = 2,   /* not used */
  /** City String */
  LOCATION_COL_CITY      = 3,
  /** Postal Code */
  LOCATION_COL_POSTAL    = 4,
  /** Latitude */
  LOCATION_COL_LAT       = 5,
  /** Longitude */
  LOCATION_COL_LONG      = 6,
  /** Metro Code */
  LOCATION_COL_METRO     = 7,
  /** Area Codes (plural) */
  LOCATION_COL_AREACODES = 8,  /* not used */
  /** 3 Char Country Code */
  LOCATION_COL_CC3       = 9,   /* not used */
  /** Country Code */
  LOCATION_COL_CNTRYCODE = 10,   /* not used */
  /** Region Code */
  LOCATION_COL_RCODE     = 11,  /* not used */
  /** City Code */
  LOCATION_COL_CITYCODE  = 12,  /* not used */
  /** Continent Code */
  LOCATION_COL_CONTCODE  = 13,
  /** Internal Code */
  LOCATION_COL_INTERNAL  = 14,  /* not used */
  /** Connection Speed String */
  LOCATION_COL_CONN      = 15,
  /** Country-Conf ?? */
  LOCATION_COL_CNTRYCONF = 16,  /* not used */
  /** Region-Conf ?? */
  LOCATION_COL_REGCONF   = 17,  /* not used */
  /** City-Conf ?? */
  LOCATION_COL_CITYCONF  = 18,  /* not used */
  /** Postal-Conf ?? */
  LOCATION_COL_POSTCONF  = 19,  /* not used */
  /** GMT-Offset */
  LOCATION_COL_GMTOFF    = 20,  /* not used */
  /** In CST */
  LOCATION_COL_INDST     = 21,  /* not used */

  /** Total number of columns in the locations table */
  LOCATION_COL_COUNT     = 22
} locations_cols_t;

/** The columns in the netacq_edge locations CSV file */
typedef enum blocks_cols {
  /** Range Start IP */
  BLOCKS_COL_STARTIP     = 0,
  /** Range End IP */
  BLOCKS_COL_ENDIP       = 1,
  /** ID */
  BLOCKS_COL_ID          = 2,

  /** Total number of columns in blocks table */
  BLOCKS_COL_COUNT  = 3
} blocks_cols_t;

/** The number of header rows in the netacq_edge CSV files */
#define HEADER_ROW_CNT 1

/** Print usage information to stderr */
static void usage(ipmeta_provider_t *provider)
{
  fprintf(stderr,
	  "provider usage: %s -l locations -b blocks\n"
	  "       -b            blocks file (must be used with -l)\n"
	  "       -l            locations file (must be used with -b)\n",
	  provider->name);
}


/** Parse the arguments given to the provider
 * @todo add option to choose datastructure
 */
static int parse_args(ipmeta_provider_t *provider, int argc, char **argv)
{
  ipmeta_provider_netacq_edge_state_t *state = STATE(provider);
  int opt;

  assert(argc > 0 && argv != NULL);

  if(argc == 1)
    {
      usage(provider);
      return -1;
    }

  /* NB: remember to reset optind to 1 before using getopt! */
  optind = 1;

  /* remember the argv strings DO NOT belong to us */

  while((opt = getopt(argc, argv, "b:l:?")) >= 0)
    {
      switch(opt)
	{
	case 'b':
	  state->blocks_file = strdup(optarg);
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

  if(state->locations_file == NULL || state->blocks_file == NULL)
    {
      fprintf(stderr, "ERROR: %s requires either '-d' or both '-b' and '-l'\n",
	      provider->name);
      usage(provider);
      return -1;
    }

  return 0;
}

/* Parse a netacq_edge location cell */
static void parse_netacq_edge_location_cell(void *s, size_t i, void *data)
{
  ipmeta_provider_t *provider = (ipmeta_provider_t*)data;
  ipmeta_provider_netacq_edge_state_t *state = STATE(provider);
  ipmeta_record_t *tmp = &(state->tmp_record);
  char *tok = (char*)s;

  uint16_t tmp_continent;

  char *end;

  /* skip the first two lines */
  if(state->current_line < HEADER_ROW_CNT)
    {
      return;
    }

  /*
  ipmeta_log(__func__, "row: %d, column: %d, tok: %s",
	      state->current_line,
	      state->current_column,
	      tok);
  */

  switch(state->current_column)
    {
    case LOCATION_COL_ID:
      /* init this record */
      tmp->id = strtol(tok, &end, 10);
      if (end == tok || *end != '\0' || errno == ERANGE)
	{
	  ipmeta_log(__func__, "Invalid ID Value (%s)", tok);
	  state->parser.status = CSV_EUSER;
	  return;
	}
      break;

    case LOCATION_COL_CC:
      if(tok == NULL || strlen(tok) != 2)
	{
	  ipmeta_log(__func__, "Invalid Country Code (%s)", tok);
      ipmeta_log(__func__,
		  "Invalid Net Acuity Edge Location Column (%d:%d)",
	     state->current_line, state->current_column);
	  state->parser.status = CSV_EUSER;
	  return;
	}
      tmp->country_code[0] = toupper(tok[0]);
      tmp->country_code[1] = toupper(tok[1]);
      break;

      /** @todo fix our region support */
    case LOCATION_COL_REGION:
      break;

    case LOCATION_COL_CITY:
      if(tok != NULL)
	{
	  tmp->city = strndup(tok, strlen(tok));
	}
      break;

    case LOCATION_COL_POSTAL:
      tmp->post_code = strndup(tok, strlen(tok));
      break;

    case LOCATION_COL_LAT:
      tmp->latitude = strtof(tok, &end);
      if (end == tok || *end != '\0' || errno == ERANGE)
	{
	  ipmeta_log(__func__, "Invalid Latitude Value (%s)", tok);
	  state->parser.status = CSV_EUSER;
	  return;
	}
      break;

    case LOCATION_COL_LONG:
      /* longitude */
      tmp->longitude = strtof(tok, &end);
      if (end == tok || *end != '\0' || errno == ERANGE)
	{
	  ipmeta_log(__func__, "Invalid Longitude Value (%s)", tok);
	  state->parser.status = CSV_EUSER;
	  return;
	}
      break;

    case LOCATION_COL_METRO:
      /* metro code - whatever the heck that is */
      if(tok != NULL)
	{
	  tmp->metro_code = strtol(tok, &end, 10);
	  if (end == tok || *end != '\0' || errno == ERANGE)
	    {
	      ipmeta_log(__func__, "Invalid Metro Value (%s)", tok);
	      state->parser.status = CSV_EUSER;
	      return;
	    }
	}
      break;

    case LOCATION_COL_AREACODES:
    case LOCATION_COL_CC3:
    case LOCATION_COL_CNTRYCODE:
    case LOCATION_COL_RCODE:
    case LOCATION_COL_CITYCODE:
      break;

    case LOCATION_COL_CONTCODE:
      if(tok != NULL)
	{
	  tmp_continent = strtol(tok, &end, 10);
	  if (end == tok || *end != '\0' || errno == ERANGE ||
	      tmp_continent > CONTINENT_MAX)
	    {
	      ipmeta_log(__func__,
			  "Invalid Continent Code Value (%s)", tok);
	      state->parser.status = CSV_EUSER;
	      return;
	    }
	  memcpy(tmp->continent_code, continent_strings[tmp_continent], 2);
	}
      break;

    case LOCATION_COL_INTERNAL:
      break;

    case LOCATION_COL_CONN:
      if(tok != NULL)
	{
	  tmp->conn_speed = strndup(tok, strlen(tok));
	}
      break;

    case LOCATION_COL_CNTRYCONF:
    case LOCATION_COL_REGCONF:
    case LOCATION_COL_CITYCONF:
    case LOCATION_COL_POSTCONF:
    case LOCATION_COL_GMTOFF:
    case LOCATION_COL_INDST:
      break;

    default:
      ipmeta_log(__func__, "Invalid Net Acuity Edge Location Column (%d:%d)",
	     state->current_line, state->current_column);
      state->parser.status = CSV_EUSER;
      return;
      break;
    }

  /* move on to the next column */
  state->current_column++;
}

/** Handle an end-of-row event from the CSV parser */
static void parse_netacq_edge_location_row(int c, void *data)
{
  ipmeta_provider_t *provider = (ipmeta_provider_t*)data;
  ipmeta_provider_netacq_edge_state_t *state = STATE(provider);
  ipmeta_record_t *record;

  /* skip the first two lines */
  if(state->current_line < HEADER_ROW_CNT)
    {
      state->current_line++;
      return;
    }

  /* at the end of successful row parsing, current_column will be 9 */
  /* make sure we parsed exactly as many columns as we anticipated */
  if(state->current_column != LOCATION_COL_COUNT)
    {
      ipmeta_log(__func__,
		  "ERROR: Expecting %d columns in the locations file, "
		  "but actually got %d",
		  LOCATION_COL_COUNT, state->current_column);
      state->parser.status = CSV_EUSER;
      return;
    }

  if((record = ipmeta_provider_init_record(provider,
					   state->tmp_record.id)) == NULL)
    {
      ipmeta_log(__func__, "ERROR: Could not initialize meta record");
      state->parser.status = CSV_EUSER;
      return;
    }

  memcpy(record, &(state->tmp_record), sizeof(ipmeta_record_t));

  /* done processing the line */

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
  ipmeta_provider_netacq_edge_state_t *state = STATE(provider);

  char buffer[BUFFER_LEN];
  int read = 0;

  /* reset the state variables before we start */
  state->current_column = 0;
  state->current_line = 0;
  memset(&(state->tmp_record), 0, sizeof(ipmeta_record_t));

  /* options for the csv parser */
  int options = CSV_STRICT | CSV_REPALL_NL | CSV_STRICT_FINI |
    CSV_APPEND_NULL | CSV_EMPTY_IS_NULL;

  csv_init(&(state->parser), options);

  while((read = wandio_read(file, &buffer, BUFFER_LEN)) > 0)
    {
      if(csv_parse(&(state->parser), buffer, read,
		   parse_netacq_edge_location_cell,
		   parse_netacq_edge_location_row,
		   provider) != read)
	{
	  ipmeta_log(__func__,
		      "Error parsing %s Location file", provider->name);
	  ipmeta_log(__func__,
		      "CSV Error: %s",
		      csv_strerror(csv_error(&(state->parser))));
	  return -1;
	}
    }

  if(csv_fini(&(state->parser),
	      parse_netacq_edge_location_cell,
	      parse_netacq_edge_location_row,
	      provider) != 0)
    {
      ipmeta_log(__func__,
		  "Error parsing %s Location file", provider->name);
      ipmeta_log(__func__,
		  "CSV Error: %s",
		  csv_strerror(csv_error(&(state->parser))));
      return -1;
    }

  csv_free(&(state->parser));

  return 0;
}

/** Parse a blocks cell */
static void parse_blocks_cell(void *s, size_t i, void *data)
{
  ipmeta_provider_t *provider = (ipmeta_provider_t*)data;
  ipmeta_provider_netacq_edge_state_t *state = STATE(provider);
  char *tok = (char*)s;
  char *end;

  /* skip the first lines */
  if(state->current_line < HEADER_ROW_CNT)
    {
      return;
    }

  switch(state->current_column)
    {
    case BLOCKS_COL_STARTIP:
      /* start ip */
      state->block_lower.addr = strtol(tok, &end, 10);
      if (end == tok || *end != '\0' || errno == ERANGE)
	{
	  ipmeta_log(__func__, "Invalid Start IP Value (%s)", tok);
	  state->parser.status = CSV_EUSER;
	}
      break;

    case BLOCKS_COL_ENDIP:
      /* end ip */
      state->block_upper.addr = strtol(tok, &end, 10);
      if (end == tok || *end != '\0' || errno == ERANGE)
	{
	  ipmeta_log(__func__, "Invalid End IP Value (%s)", tok);
	  state->parser.status = CSV_EUSER;
	}
      break;

    case BLOCKS_COL_ID:
      /* id */
      state->block_id = strtol(tok, &end, 10);
      if (end == tok || *end != '\0' || errno == ERANGE)
	{
	  ipmeta_log(__func__, "Invalid ID Value (%s)", tok);
	  state->parser.status = CSV_EUSER;
	}
      break;

    default:
      ipmeta_log(__func__, "Invalid Blocks Column (%d:%d)",
		  state->current_line, state->current_column);
      state->parser.status = CSV_EUSER;
      break;
    }

  /* move on to the next column */
  state->current_column++;
}

static void parse_blocks_row(int c, void *data)
{
  ipmeta_provider_t *provider = (ipmeta_provider_t*)data;
  ipmeta_provider_netacq_edge_state_t *state = STATE(provider);

  ip_prefix_list_t *pfx_list = NULL;
  ip_prefix_list_t *temp = NULL;
  ipmeta_record_t *record = NULL;

  if(state->current_line < HEADER_ROW_CNT)
    {
      state->current_line++;
      return;
    }

  /* done processing the line */

  /* make sure we parsed exactly as many columns as we anticipated */
  if(state->current_column != BLOCKS_COL_COUNT)
    {
      ipmeta_log(__func__,
		  "ERROR: Expecting %d columns in the blocks file, "
		  "but actually got %d",
		  BLOCKS_COL_COUNT, state->current_column);
      state->parser.status = CSV_EUSER;
      return;
    }

  assert(state->block_id > 0);

  /* convert the range to prefixes */
  if(ip_range_to_prefix(state->block_lower,
			state->block_upper,
			&pfx_list) != 0)
    {
      ipmeta_log(__func__,
		  "ERROR: Could not convert range to pfxs");
      state->parser.status = CSV_EUSER;
      return;
    }
  assert(pfx_list != NULL);

  /* get the record from the provider */
  if((record = ipmeta_provider_get_record(provider,
					  state->block_id)) == NULL)
    {
      ipmeta_log(__func__,
		  "ERROR: Missing record for location %d",
		  state->block_id);
      state->parser.status = CSV_EUSER;
      return;
    }

  /* iterate over and add each prefix to the trie */
  while(pfx_list != NULL)
    {
      if(ipmeta_provider_associate_record(provider,
					  htonl(pfx_list->prefix.addr),
					  pfx_list->prefix.masklen,
					  record) != 0)
	{
	  ipmeta_log(__func__,
		      "ERROR: Failed to associate record");
	  state->parser.status = CSV_EUSER;
	  return;
	}

      /* store this node so we can free it */
      temp = pfx_list;
      /* move on to the next pfx */
      pfx_list = pfx_list->next;
      /* free this node (saves us walking the list twice) */
      free(temp);
    }

  /* increment the current line */
  state->current_line++;
  /* reset the current column */
  state->current_column = 0;
}

/** Read a blocks file  */
static int read_blocks(ipmeta_provider_t *provider, io_t *file)
{
  ipmeta_provider_netacq_edge_state_t *state = STATE(provider);
  char buffer[BUFFER_LEN];
  int read = 0;

  /* reset the state variables before we start */
  state->current_column = 0;
  state->current_line = 0;
  state->block_id = 0;
  state->block_lower.masklen = 32;
  state->block_upper.masklen = 32;

  /* options for the csv parser */
  int options = CSV_STRICT | CSV_REPALL_NL | CSV_STRICT_FINI |
    CSV_APPEND_NULL | CSV_EMPTY_IS_NULL;

  csv_init(&(state->parser), options);


  while((read = wandio_read(file, &buffer, BUFFER_LEN)) > 0)
    {
      if(csv_parse(&(state->parser), buffer, read,
		   parse_blocks_cell,
		   parse_blocks_row,
		   provider) != read)
	{
	  ipmeta_log(__func__,
		      "Error parsing Blocks file");
	  ipmeta_log(__func__,
		      "CSV Error: %s",
		      csv_strerror(csv_error(&(state->parser))));
	  return -1;
	}
    }

  if(csv_fini(&(state->parser),
	      parse_blocks_cell,
	      parse_blocks_row,
	      provider) != 0)
    {
      ipmeta_log(__func__,
		  "Error parsing Netacq_Edge Location file");
      ipmeta_log(__func__,
		  "CSV Error: %s",
		  csv_strerror(csv_error(&(state->parser))));
      return -1;
    }

  csv_free(&(state->parser));

  return 0;
}

/* ===== PUBLIC FUNCTIONS BELOW THIS POINT ===== */

ipmeta_provider_t *ipmeta_provider_netacq_edge_alloc()
{
  return &ipmeta_provider_netacq_edge;
}

int ipmeta_provider_netacq_edge_init(ipmeta_provider_t *provider,
				 int argc, char ** argv)
{
  ipmeta_provider_netacq_edge_state_t *state;
  io_t *file = NULL;

  /* allocate our state */
  if((state = malloc_zero(sizeof(ipmeta_provider_netacq_edge_state_t)))
     == NULL)
    {
      ipmeta_log(__func__,
		  "could not malloc ipmeta_provider_netacq_edge_state_t");
      return -1;
    }
  ipmeta_provider_register_state(provider, state);

  /* parse the command line args */
  if(parse_args(provider, argc, argv) != 0)
    {
      return -1;
    }

  assert(state->locations_file != NULL && state->blocks_file != NULL);

  /* open the locations file */
  if((file = wandio_create(state->locations_file)) == NULL)
    {
      ipmeta_log(__func__,
		 "failed to open location file '%s'", state->locations_file);
      return -1;
    }

  /* populate the locations hash */
  if(read_locations(provider, file) != 0)
    {
      ipmeta_log(__func__, "failed to parse locations file");
      goto err;
    }

  /* close the locations file */
  wandio_destroy(file);
  file = NULL;

  /* open the blocks file */
  if((file = wandio_create(state->blocks_file)) == NULL)
    {
      ipmeta_log(__func__,
		  "failed to open blocks file '%s'", state->blocks_file);
      goto err;
    }

  /* populate the ds (by joining on the id in the hash) */
  if(read_blocks(provider, file) != 0)
    {
      ipmeta_log(__func__, "failed to parse blocks file");
      goto err;
    }

  /* close the blocks file */
  wandio_destroy(file);

  /* ready to rock n roll */

  return 0;

 err:
  if(file != NULL)
    {
      wandio_destroy(file);
    }
  usage(provider);
  return -1;
}

void ipmeta_provider_netacq_edge_free(ipmeta_provider_t *provider)
{
  ipmeta_provider_netacq_edge_state_t *state = STATE(provider);
  if(state != NULL)
    {
      if(state->locations_file != NULL)
	{
	  free(state->locations_file);
	  state->locations_file = NULL;
	}

      if(state->blocks_file != NULL)
	{
	  free(state->blocks_file);
	  state->blocks_file = NULL;
	}

      ipmeta_provider_free_state(provider);
    }
  return;
}

inline ipmeta_record_t *ipmeta_provider_netacq_edge_lookup(
						ipmeta_provider_t *provider,
						uint32_t addr)
{
  /* just call the lookup helper func in provider manager */
  return ipmeta_provider_lookup_record(provider, addr);
}
