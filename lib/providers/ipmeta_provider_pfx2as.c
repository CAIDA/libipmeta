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
#include "wandio_utils.h"

#include "ipmeta_ds.h"
#include "ipmeta_provider_pfx2as.h"

#define PROVIDER_NAME "pfx2as"

#define STATE(provname)				\
  (IPMETA_PROVIDER_STATE(pfx2as, provname))

#define BUFFER_LEN 1024

/** Initialize the map type (string keys, geo_record values */
KHASH_MAP_INIT_STR(strrec, ipmeta_record_t *)

/** The basic fields that every instance of this provider have in common */
static ipmeta_provider_t ipmeta_provider_pfx2as = {
  IPMETA_PROVIDER_PFX2AS,
  PROVIDER_NAME,
  IPMETA_PROVIDER_GENERATE_PTRS(pfx2as)
};

/** Holds the state for an instance of this provider */
typedef struct ipmeta_provider_pfx2as_state {
  /* datastructure name */
  char *ds_name;

  /* info extracted from args */

  /** The filename of the CAIDA pfx2as database to use */
  char *pfx2as_file;

} ipmeta_provider_pfx2as_state_t;

#define COL_CNT 3

/** Print usage information to stderr */
static void usage(ipmeta_provider_t *provider)
{
  int i;
  const char **names = ipmeta_ds_get_all();
  assert(names != NULL);

  fprintf(stderr,
	  "provider usage: %s -f pfx2as-file\n"
	  "       -D            datastructure to use. may be one of:\n",
	  provider->name);

  for(i=0; i<IPMETA_DS_MAX; i++)
    {
      fprintf(stderr,
	      "                      - %s%s\n",
	      names[i],
	      (i+1 == IPMETA_DS_DEFAULT) ? " (default)" : "");
    }

  fprintf(stderr,
	  "       -f            pfx2as file to use for lookups\n");
}


/** Parse the arguments given to the provider
 * @todo add option to choose datastructure
 */
static int parse_args(ipmeta_provider_t *provider, int argc, char **argv)
{
  ipmeta_provider_pfx2as_state_t *state = STATE(provider);
  int opt;

  /* no args */
  if(argc == 0)
    {
      usage(provider);
      return -1;
    }

  /* NB: remember to reset optind to 1 before using getopt! */
  optind = 1;

  /* remember the argv strings DO NOT belong to us */

  while((opt = getopt(argc, argv, ":D:f:?")) >= 0)
    {
      switch(opt)
	{
	case 'D':
	  state->ds_name = strdup(optarg);
	  break;

	case 'f':
	  state->pfx2as_file = strdup(optarg);
	  break;

	case '?':
	case ':':
	default:
	  usage(provider);
	  return -1;
	}
    }

  if(state->pfx2as_file == NULL)
    {
      fprintf(stderr, "ERROR: %s requires '-f'\n",
	      provider->name);
      usage(provider);
      return -1;
    }

  return 0;
}

/** Parse an underscore-separated list of ASNs */
static int parse_asn(char *asn_str, uint32_t **asn_arr)
{
  int asn_cnt = 0;
  uint32_t *asn = NULL;
  char *tok = NULL;
  char *period = NULL;

  /* WARNING:

     As of 2014-04-09 AK added the following code to convert AS sets into MOAS
     format. This is because we previously did not handle AS sets at all, and
     there are no current uses of MOAS nor AS sets. This may/will need to be
     revisited in the future
  */
  /* s/,/_/g; */
  tok = asn_str;
  while(*tok != '\0')
    {
      if(*tok == ',')
	{
	  *tok = '_';
	}
      tok++;
    }

  while((tok = strsep(&asn_str, "_")) != NULL)
    {
      /* realloc the asn array to buy us one more */
      if((asn = realloc(asn, sizeof(uint32_t) * (asn_cnt+1))) == NULL)
	{
	  if(asn != NULL)
	    {
	      free(asn);
	    }
	  return -1;
	}

      /* check if this is a 32bit asn */
      if((period = strchr(tok, '.')) != NULL)
	{
	  /* set this to a nul */
	  *period = '\0';
	  /* get the value of the first 16 bits and the second */
	  asn[asn_cnt] = (atoi(tok)<<16) | atoi(period+1);
	}
      else
	{
	  /* do a simple atoi and be done */
	  asn[asn_cnt] = atoi(tok);
	}
      asn_cnt++;
    }

  /* return the array of asn values and the count */
  *asn_arr = asn;
  return asn_cnt;
}

/** Free a string (for use with the map) */
static inline void str_free(const char *str)
{
  free((char*)str);
}

/** Read the prefix2as file */
static int read_pfx2as(ipmeta_provider_t *provider, io_t *file)
{
  /* we have to normalize the asns on the fly */
  /* so we read the file, reading records into here */
  int khret;
  khiter_t khiter;
  khash_t(strrec)   *asn_table = kh_init(strrec);

  char buffer[BUFFER_LEN];
  char *rowp;
  char *tok = NULL;
  int tokc = 0;

  int asn_id = 0;
  in_addr_t addr = 0;
  uint8_t mask = 0;
  uint32_t *asn = NULL;
  char *asn_str = NULL;
  int asn_cnt = 0;

  ipmeta_record_t *record;

  while(wandio_fgets(file, &buffer, BUFFER_LEN, 1) > 0)
    {
      rowp = buffer;
      tokc = 0;

      while((tok = strsep(&rowp, "\t")) != NULL)
	{
	  switch(tokc)
	    {
	    case 0:
	      /* network */
	      addr = inet_addr(tok);
	      break;

	    case 1:
	      /* mask */
	      mask = atoi(tok);
	      break;

	    case 2:
	      /* asn */
	      asn_str = strdup(tok);
	      asn_cnt = parse_asn(tok, &asn);
	      break;

	    default:
	      ipmeta_log(__func__, "invalid pfx2as file");
	      return -1;
	      break;
	    }
	  tokc++;
	}

      if(tokc != COL_CNT)
	{
	  ipmeta_log(__func__, "invalid pfx2as file");
	  return -1;
	}

      if(asn_cnt <= 0 || asn_str == NULL)
	{
	  ipmeta_log(__func__, "could not parse asn string");
	  return -1;
	}

      /* check our hash for this asn */
      if((khiter = kh_get(strrec, asn_table, asn_str)) == kh_end(asn_table))
	{
	  /* need to create a record for this asn */
	  if((record = ipmeta_provider_init_record(provider, asn_id)) == NULL)
	    {
	      ipmeta_log(__func__, "could not alloc geo record");
	      return -1;
	    }

	  /* set the fields */
	  record->asn = asn;
	  record->asn_cnt = asn_cnt;

	  /* put it into our table */
	  khiter = kh_put(strrec, asn_table, asn_str, &khret);
	  kh_value(asn_table, khiter) = record;

	  /* move on to the next id */
	  asn_id++;
	}
      else
	{
	  /* we've seen this ASN before, just use that! */
	  record = kh_value(asn_table, khiter);
	  assert(record != NULL);
	  /* BUT! remember that we strdup'd the asn string */
	  /* and that parse_asn did some malloc'ing */
	  free(asn_str);
	  free(asn);
	  asn = NULL;
	  asn_cnt = 0;
	}

      assert(record != NULL);

      /* how many IP addresses does this prefix cover ? */
      /* we will add this to the record and then use the total count for the asn
	 to find the 'biggest' ASes */
      record->asn_ip_cnt += (ip_broadcast_addr(addr, mask) -
			       ip_network_addr(addr, mask)) + 1;

      /* by here record is the right asn record, associate it with this pfx */
      if(ipmeta_provider_associate_record(provider, addr, mask, record) != 0)
	{
	  ipmeta_log(__func__, "failed to associate record");
	  return -1;
	}
    }

  /* free our asn_table hash */
  kh_free(strrec, asn_table, str_free);
  kh_destroy(strrec, asn_table);

  return 0;
}

/* ===== PUBLIC FUNCTIONS BELOW THIS POINT ===== */

ipmeta_provider_t *ipmeta_provider_pfx2as_alloc()
{
  return &ipmeta_provider_pfx2as;
}

int ipmeta_provider_pfx2as_init(ipmeta_provider_t *provider,
				 int argc, char ** argv)
{
  ipmeta_provider_pfx2as_state_t *state;
  io_t *file = NULL;

  /* allocate our state */
  if((state = malloc_zero(sizeof(ipmeta_provider_pfx2as_state_t)))
     == NULL)
    {
      ipmeta_log(__func__,
		  "could not malloc ipmeta_provider_pfx2as_state_t");
      return -1;
    }
  ipmeta_provider_register_state(provider, state);

  /* parse the command line args */
  if(parse_args(provider, argc, argv) != 0)
    {
      return -1;
    }

  /* initialize the datastructure */
  if(state->ds_name == NULL)
    {
      if(ipmeta_ds_init(provider, IPMETA_DS_DEFAULT) != 0)
	{
	  ipmeta_log(__func__, "could not initialize datastructure");
	  goto err;
	}
    }
  else
    {
      if(ipmeta_ds_init_by_name(provider, state->ds_name) != 0)
	{
	  ipmeta_log(__func__, "could not initialize datastructure");
	  fprintf(stderr, "ERROR: Check datastructure name (%s)\n",
		  state->ds_name);
	  goto err;
	}
    }

  assert(state->pfx2as_file != NULL);

  /* open the pfx2as file */
  if((file = wandio_create(state->pfx2as_file)) == NULL)
    {
      ipmeta_log(__func__,
		 "failed to open pfx2as file '%s'", state->pfx2as_file);
      return -1;
    }

  /* populate the locations hash */
  if(read_pfx2as(provider, file) != 0)
    {
      ipmeta_log(__func__, "failed to parse pfx2as file");
      goto err;
    }

  /* close the locations file */
  wandio_destroy(file);
  file = NULL;

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

void ipmeta_provider_pfx2as_free(ipmeta_provider_t *provider)
{
  ipmeta_provider_pfx2as_state_t *state = STATE(provider);
  if(state != NULL)
    {
      if(state->pfx2as_file != NULL)
	{
	  free(state->pfx2as_file);
	  state->pfx2as_file = NULL;
	}

      ipmeta_provider_free_state(provider);
    }
  return;
}

inline ipmeta_record_t *ipmeta_provider_pfx2as_lookup(
						ipmeta_provider_t *provider,
						uint32_t addr, uint8_t mask)
{
  /* just call the lookup helper func in provider manager */
  return ipmeta_provider_lookup_record(provider, addr, mask);
}
