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

#include "ipmeta_provider_maxmind.h"

#define PROVIDER_NAME "maxmind"

#define STATE(provname)				\
  (IPMETA_PROVIDER_STATE(maxmind, provname))

#define BUFFER_LEN 1024

KHASH_INIT(u16u16, uint16_t, uint16_t, 1,
	   kh_int_hash_func, kh_int_hash_equal)

/** The default file name for the locations file */
#define LOCATIONS_FILE_NAME "GeoLiteCity-Location.csv.gz"

/** The default file name for the blocks file */
#define BLOCKS_FILE_NAME "GeoLiteCity-Blocks.csv.gz"

/** The basic fields that every instance of this provider have in common */
static ipmeta_provider_t ipmeta_provider_maxmind = {
  IPMETA_PROVIDER_MAXMIND,
  PROVIDER_NAME,
  IPMETA_PROVIDER_GENERATE_PTRS(maxmind)
};

/** Holds the state for an instance of this provider */
typedef struct ipmeta_provider_maxmind_state {
  /* info extracted from args */
  char *locations_file;
  char *blocks_file;

  /* State for CSV parser */
  struct csv_parser parser;
  int current_line;
  int current_column;
  ipmeta_record_t tmp_record;
  uint16_t cntry_code;
  uint32_t block_id;
  ip_prefix_t block_lower;
  ip_prefix_t block_upper;

  /* hash that maps from country code to continent code */
  khash_t(u16u16) *country_continent;
} ipmeta_provider_maxmind_state_t;

/** The columns in the maxmind locations CSV file */
typedef enum locations_cols {
  /** ID */
  LOCATION_COL_ID     = 0,
  /** 2 Char Country Code */
  LOCATION_COL_CC     = 1,
  /** Region String */
  LOCATION_COL_REGION = 2,
  /** City String */
  LOCATION_COL_CITY   = 3,
  /** Postal Code String */
  LOCATION_COL_POSTAL = 4,
  /** Latitude */
  LOCATION_COL_LAT    = 5,
  /** Longitude */
  LOCATION_COL_LONG   = 6,
  /** Metro Code */
  LOCATION_COL_METRO  = 7,
  /** Area Code (phone) */
  LOCATION_COL_AREA   = 8,

  /** Total number of columns in locations table */
  LOCATION_COL_COUNT  = 9
} locations_cols_t;

/** The columns in the maxmind locations CSV file */
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

/** The number of header rows in the maxmind CSV files */
#define HEADER_ROW_CNT 2

/** Print usage information to stderr */
static void usage(ipmeta_provider_t *provider)
{
  fprintf(stderr,
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
  ipmeta_provider_maxmind_state_t *state = STATE(provider);
  int opt;
  char *directory = NULL;
  char *ptr = NULL;

  /* no args */
  if(argc == 0)
    {
      usage(provider);
      return -1;
    }

  /* NB: remember to reset optind to 1 before using getopt! */
  optind = 1;

  /* remember the argv strings DO NOT belong to us */

  while((opt = getopt(argc, argv, "b:d:l:?")) >= 0)
    {
      switch(opt)
	{
	case 'b':
	  state->blocks_file = strdup(optarg);
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

  if(directory != NULL)
    {
      /* check if they were daft and specified explicit files too */
      if(state->locations_file != NULL || state->blocks_file != NULL)
	{
	  fprintf(stderr, "WARNING: both directory and file name specified.\n");

	  /* free up the dup'd strings */
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
	}

      /* remove the trailing slash if there is one */
      if(directory[strlen(directory)-1] == '/')
	{
	  directory[strlen(directory)-1] = '\0';
	}

      /* malloc storage for the dir+/+file string */
      if((state->locations_file = malloc(
					 strlen(directory)+1+
					 strlen(LOCATIONS_FILE_NAME)+1))
	 == NULL)
	{
	  ipmeta_log(__func__,
		      "could not malloc location file string");
	  return -1;
	}

      if((state->blocks_file = malloc(
				      strlen(directory)+1+
				      strlen(BLOCKS_FILE_NAME)+1))
	 == NULL)
	{
	  ipmeta_log(__func__,
		      "could not malloc blocks file string");
	  return -1;
	}

      /** @todo make this check for both .gz and non-.gz files */

      ptr = stpncpy(state->locations_file, directory, strlen(directory));
      *ptr++ = '/';
      /* last copy needs a +1 to get the terminating nul. d'oh */
      ptr = stpncpy(ptr, LOCATIONS_FILE_NAME, strlen(LOCATIONS_FILE_NAME)+1);

      ptr = stpncpy(state->blocks_file, directory, strlen(directory));
      *ptr++ = '/';
      ptr = stpncpy(ptr, BLOCKS_FILE_NAME, strlen(BLOCKS_FILE_NAME)+1);

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

/* Parse a maxmind location cell */
static void parse_maxmind_location_cell(void *s, size_t i, void *data)
{
  ipmeta_provider_t *provider = (ipmeta_provider_t*)data;
  ipmeta_provider_maxmind_state_t *state = STATE(provider);
  ipmeta_record_t *tmp = &(state->tmp_record);
  char *tok = (char*)s;

  char *end;

  /* skip the first two lines */
  if(state->current_line < HEADER_ROW_CNT)
    {
      return;
    }

  /*
  corsaro_log(__func__, corsaro, "row: %d, column: %d, tok: %s",
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
      /* country code */
      if(tok == NULL || strlen(tok) != 2)
	{
	  ipmeta_log(__func__, "Invalid Country Code (%s)", tok);
	  state->parser.status = CSV_EUSER;
	  return;
	}
      state->cntry_code = (tok[0]<<8) | tok[1];
      memcpy(tmp->country_code, tok, 2);
      break;

    case LOCATION_COL_REGION:
      /* region string */
      if(tok == NULL || strlen(tok) == 0)
	{
	  tmp->region[0] = '\0';
	}
      else
	{
	  tmp->region[2] = '\0';
	  memcpy(tmp->region, tok, 2);
	}
      break;

    case LOCATION_COL_CITY:
      /* city */
      tmp->city = strndup(tok, strlen(tok));
      break;

    case LOCATION_COL_POSTAL:
      /* postal code */
      tmp->post_code = strndup(tok, strlen(tok));
      break;

    case LOCATION_COL_LAT:
      /* latitude */
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

    case LOCATION_COL_AREA:
      /* area code - (phone) */
      if(tok != NULL)
	{
	  tmp->area_code = strtol(tok, &end, 10);
	  if (end == tok || *end != '\0' || errno == ERANGE)
	    {
	      ipmeta_log(__func__,
			  "Invalid Area Code Value (%s)", tok);
	      state->parser.status = CSV_EUSER;
	      return;
	    }
	}
      break;

    default:
      ipmeta_log(__func__, "Invalid Maxmind Location Column (%d:%d)",
	     state->current_line, state->current_column);
      state->parser.status = CSV_EUSER;
      return;
      break;
    }

  /* move on to the next column */
  state->current_column++;
}

/** Handle an end-of-row event from the CSV parser */
static void parse_maxmind_location_row(int c, void *data)
{
  ipmeta_provider_t *provider = (ipmeta_provider_t*)data;
  ipmeta_provider_maxmind_state_t *state = STATE(provider);
  ipmeta_record_t *record;

  uint16_t tmp_continent;

  khiter_t khiter;

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

  /* look up the continent code */
  if((khiter = kh_get(u16u16, state->country_continent, state->cntry_code)) ==
     kh_end(state->country_continent))
    {
      ipmeta_log(__func__, "ERROR: Invalid country code (%s) (%x)",
		  state->tmp_record.country_code,
		  state->cntry_code);
      state->parser.status = CSV_EUSER;
      return;
    }

  tmp_continent = kh_value(state->country_continent, khiter);
  state->tmp_record.continent_code[0] = (tmp_continent & 0xFF00) >> 8;
  state->tmp_record.continent_code[1] = (tmp_continent & 0x00FF);

  /*
  ipmeta_log(__func__, NULL, "looking up %s (%x) got %x",
	      tmp.country_code, cntry_code, tmp.continent_code);
  */

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
  /* reset the country code */
  state->cntry_code = 0;

  return;
}

/** Read a locations file */
static int read_locations(ipmeta_provider_t *provider, io_t *file)
{
  ipmeta_provider_maxmind_state_t *state = STATE(provider);

  char buffer[BUFFER_LEN];
  int read = 0;

  /* reset the state variables before we start */
  state->current_column = 0;
  state->current_line = 0;
  memset(&(state->tmp_record), 0, sizeof(ipmeta_record_t));
  state->cntry_code = 0;

  /* options for the csv parser */
  int options = CSV_STRICT | CSV_REPALL_NL | CSV_STRICT_FINI |
    CSV_APPEND_NULL | CSV_EMPTY_IS_NULL;

  csv_init(&(state->parser), options);

  while((read = wandio_read(file, &buffer, BUFFER_LEN)) > 0)
    {
      if(csv_parse(&(state->parser), buffer, read,
		   parse_maxmind_location_cell,
		   parse_maxmind_location_row,
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
	      parse_maxmind_location_cell,
	      parse_maxmind_location_row,
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
  ipmeta_provider_maxmind_state_t *state = STATE(provider);
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
  ipmeta_provider_maxmind_state_t *state = STATE(provider);

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
  ipmeta_provider_maxmind_state_t *state = STATE(provider);
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
		  "Error parsing Maxmind Location file");
      ipmeta_log(__func__,
		  "CSV Error: %s",
		  csv_strerror(csv_error(&(state->parser))));
      return -1;
    }

  csv_free(&(state->parser));

  return 0;
}

/* ----- Class Helper Functions below here ------ */

/** Array of ISO 2char country codes. Extracted from libGeoIP v1.5.0 */
static const char *country_code_iso2[] = {
  "--","AP","EU","AD","AE","AF","AG","AI","AL","AM","CW",
  "AO","AQ","AR","AS","AT","AU","AW","AZ","BA","BB",
  "BD","BE","BF","BG","BH","BI","BJ","BM","BN","BO",
  "BR","BS","BT","BV","BW","BY","BZ","CA","CC","CD",
  "CF","CG","CH","CI","CK","CL","CM","CN","CO","CR",
  "CU","CV","CX","CY","CZ","DE","DJ","DK","DM","DO",
  "DZ","EC","EE","EG","EH","ER","ES","ET","FI","FJ",
  "FK","FM","FO","FR","SX","GA","GB","GD","GE","GF",
  "GH","GI","GL","GM","GN","GP","GQ","GR","GS","GT",
  "GU","GW","GY","HK","HM","HN","HR","HT","HU","ID",
  "IE","IL","IN","IO","IQ","IR","IS","IT","JM","JO",
  "JP","KE","KG","KH","KI","KM","KN","KP","KR","KW",
  "KY","KZ","LA","LB","LC","LI","LK","LR","LS","LT",
  "LU","LV","LY","MA","MC","MD","MG","MH","MK","ML",
  "MM","MN","MO","MP","MQ","MR","MS","MT","MU","MV",
  "MW","MX","MY","MZ","NA","NC","NE","NF","NG","NI",
  "NL","NO","NP","NR","NU","NZ","OM","PA","PE","PF",
  "PG","PH","PK","PL","PM","PN","PR","PS","PT","PW",
  "PY","QA","RE","RO","RU","RW","SA","SB","SC","SD",
  "SE","SG","SH","SI","SJ","SK","SL","SM","SN","SO",
  "SR","ST","SV","SY","SZ","TC","TD","TF","TG","TH",
  "TJ","TK","TM","TN","TO","TL","TR","TT","TV","TW",
  "TZ","UA","UG","UM","US","UY","UZ","VA","VC","VE",
  "VG","VI","VN","VU","WF","WS","YE","YT","RS","ZA",
  "ZM","ME","ZW","A1","A2","O1","AX","GG","IM","JE",
  "BL","MF", "BQ", "SS",
  /* Alistair adds AN because Maxmind does not include it, but uses it */
  "AN",
};

#if 0
/** Array of ISO 3 char country codes. Extracted from libGeoIP v1.5.0 */
static const char *country_code_iso3[] = {
  "--","AP","EU","AND","ARE","AFG","ATG","AIA","ALB","ARM","CUW",
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
  "N/A","Asia/Pacific Region","Europe","Andorra","United Arab Emirates",
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

static const char *country_continent[] = {
  "--", "AS","EU","EU","AS","AS","NA","NA","EU","AS","NA",
  "AF","AN","SA","OC","EU","OC","NA","AS","EU","NA",
  "AS","EU","AF","EU","AS","AF","AF","NA","AS","SA",
  "SA","NA","AS","AN","AF","EU","NA","NA","AS","AF",
  "AF","AF","EU","AF","OC","SA","AF","AS","SA","NA",
  "NA","AF","AS","AS","EU","EU","AF","EU","NA","NA",
  "AF","SA","EU","AF","AF","AF","EU","AF","EU","OC",
  "SA","OC","EU","EU","NA","AF","EU","NA","AS","SA",
  "AF","EU","NA","AF","AF","NA","AF","EU","AN","NA",
  "OC","AF","SA","AS","AN","NA","EU","NA","EU","AS",
  "EU","AS","AS","AS","AS","AS","EU","EU","NA","AS",
  "AS","AF","AS","AS","OC","AF","NA","AS","AS","AS",
  "NA","AS","AS","AS","NA","EU","AS","AF","AF","EU",
  "EU","EU","AF","AF","EU","EU","AF","OC","EU","AF",
  "AS","AS","AS","OC","NA","AF","NA","EU","AF","AS",
  "AF","NA","AS","AF","AF","OC","AF","OC","AF","NA",
  "EU","EU","AS","OC","OC","OC","AS","NA","SA","OC",
  "OC","AS","AS","EU","NA","OC","NA","AS","EU","OC",
  "SA","AS","AF","EU","EU","AF","AS","OC","AF","AF",
  "EU","AS","AF","EU","EU","EU","AF","EU","AF","AF",
  "SA","AF","NA","AS","AF","NA","AF","AN","AF","AS",
  "AS","OC","AS","AF","OC","AS","EU","NA","OC","AS",
  "AF","EU","AF","OC","NA","SA","AS","EU","NA","SA",
  "NA","NA","AS","OC","OC","OC","AS","AF","EU","AF",
  "AF","EU","AF","--","--","--","EU","EU","EU","EU",
  "NA","NA","NA","AF",
  /* see above about AN */
  "NA",
};

#define COUNTRY_CNT ((unsigned)(					\
		  sizeof(country_code_iso2) /	\
		  sizeof(country_code_iso2[0])))

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

int ipmeta_provider_maxmind_init(ipmeta_provider_t *provider,
				 int argc, char ** argv)
{
  ipmeta_provider_maxmind_state_t *state;
  io_t *file = NULL;

  int country_cnt;
  /*int continent_cnt;*/
  const char **countries;
  const char **continents;
  uint16_t cntry_code = 0;
  uint16_t cont_code = 0;
  int i;
  khiter_t khiter;
  int khret;

  /* allocate our state */
  if((state = malloc_zero(sizeof(ipmeta_provider_maxmind_state_t)))
     == NULL)
    {
      ipmeta_log(__func__,
		  "could not malloc ipmeta_provider_maxmind_state_t");
      return -1;
    }
  ipmeta_provider_register_state(provider, state);

  /* parse the command line args */
  if(parse_args(provider, argc, argv) != 0)
    {
      return -1;
    }

  assert(state->locations_file != NULL && state->blocks_file != NULL);

  /* populate the country2continent hash */
  state->country_continent = kh_init(u16u16);
  country_cnt = ipmeta_provider_maxmind_get_iso2_list(&countries);
  ipmeta_provider_maxmind_get_country_continent_list(&continents);
  /*assert(country_cnt == continent_cnt);*/
  for(i=0; i< country_cnt; i++)
    {
      cntry_code = (countries[i][0]<<8) | countries[i][1];
      cont_code = (continents[i][0]<<8) | continents[i][1];

      /* create a mapping for this country */
      khiter = kh_put(u16u16, state->country_continent, cntry_code, &khret);
      kh_value(state->country_continent, khiter) = cont_code;
    }

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

void ipmeta_provider_maxmind_free(ipmeta_provider_t *provider)
{
  ipmeta_provider_maxmind_state_t *state = STATE(provider);
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

      if(state->country_continent != NULL)
	{
	  kh_destroy(u16u16, state->country_continent);
	  state->country_continent = NULL;
	}

      ipmeta_provider_free_state(provider);
    }
  return;
}

inline ipmeta_record_t *ipmeta_provider_maxmind_lookup(
						ipmeta_provider_t *provider,
						uint32_t addr)
{
  /* just call the lookup helper func in provider manager */
  return ipmeta_provider_lookup_record(provider, addr);
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
