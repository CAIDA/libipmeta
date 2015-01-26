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

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parse_cmd.h"
#include "utils.h"
#include "wandio_utils.h"

#include "libipmeta_int.h"
#include "ipmeta_ds.h"
#include "ipmeta_provider.h"

#define MAXOPTS 1024

#define SEPARATOR "|"


ipmeta_t *ipmeta_init()
{
  ipmeta_t *ipmeta;

  ipmeta_log(__func__, "initializing libipmeta");

  /* allocate some memory for our state */
  if((ipmeta = malloc_zero(sizeof(ipmeta_t))) == NULL)
    {
      ipmeta_log(__func__, "could not malloc ipmeta_t");
      return NULL;
    }

  /* allocate the providers */
  if(ipmeta_provider_alloc_all(ipmeta) != 0)
    {
      free(ipmeta);
      return NULL;
    }

  return ipmeta;
}

void ipmeta_free(ipmeta_t *ipmeta)
{
  int i;

  /* no mercy for double frees */
  assert(ipmeta != NULL);

  /* loop across all providers and free each one */
  for(i = 0; i < IPMETA_PROVIDER_MAX; i++)
    {
      ipmeta_provider_free(ipmeta, ipmeta->providers[i]);
    }

  free(ipmeta);
  return;
}

int ipmeta_enable_provider(ipmeta_t *ipmeta,
			   ipmeta_provider_t *provider,
			   const char *options,
			   ipmeta_provider_default_t set_default)
{
  char *local_args = NULL;
  char *process_argv[MAXOPTS];
  int len;
  int process_argc = 0;
  int rc;

  ipmeta_log(__func__, "enabling provider (%s)%s", provider->name,
	     set_default == IPMETA_PROVIDER_DEFAULT_YES ? " (default)" : "");

  /* first we need to parse the options */
  if(options != NULL && (len = strlen(options)) > 0)
    {
      local_args = strndup(options, len);
      parse_cmd(local_args, &process_argc, process_argv, MAXOPTS,
		provider->name);
    }

  /* we just need to pass this along to the provider framework */
  rc = ipmeta_provider_init(ipmeta, provider,
			    process_argc, process_argv, set_default);

  if(local_args != NULL)
    {
      free(local_args);
    }

  return rc;
}

ipmeta_provider_t *ipmeta_get_default_provider(ipmeta_t *ipmeta)
{
  assert(ipmeta != NULL);
  return ipmeta->provider_default;
}

inline ipmeta_provider_t *ipmeta_get_provider_by_id(ipmeta_t *ipmeta,
					     ipmeta_provider_id_t id)
{
  assert(ipmeta != NULL);
  assert(id > 0 && id <= IPMETA_PROVIDER_MAX);
  return ipmeta->providers[id - 1];
}

ipmeta_provider_t *ipmeta_get_provider_by_name(ipmeta_t *ipmeta,
					       const char *name)
{
  ipmeta_provider_t *provider;
  int i;

  for(i = 1; i <= IPMETA_PROVIDER_MAX; i++)
    {
      if((provider = ipmeta_get_provider_by_id(ipmeta, i)) != NULL &&
	 strncasecmp(provider->name, name, strlen(provider->name)) == 0)
	{
	  return provider;
	}
    }

  return NULL;
}

inline int ipmeta_lookup(ipmeta_provider_t *provider,
			       uint32_t addr, uint8_t mask,
             ipmeta_record_set_t *records)
{
  assert(provider != NULL && provider->enabled != 0);

  ipmeta_record_set_clear(records);

  return provider->lookup(provider, addr, mask, records);
}

inline int ipmeta_lookup_single(ipmeta_provider_t *provider,
             uint32_t addr,
             ipmeta_record_set_t *records)
{
  ipmeta_record_set_clear(records);

  /* Single IP address = /32 */
  return ipmeta_lookup(provider, addr, 32, records);
}

inline int ipmeta_is_provider_enabled(ipmeta_provider_t *provider)
{
  assert(provider != NULL);
  return provider->enabled;
}

inline int ipmeta_get_provider_id(ipmeta_provider_t *provider)
{
  assert(provider != NULL);

  return provider->id;
}

inline const char *ipmeta_get_provider_name(ipmeta_provider_t *provider)
{
  assert(provider != NULL);

  return provider->name;
}

ipmeta_provider_t **ipmeta_get_all_providers(ipmeta_t *ipmeta)
{
  return ipmeta->providers;
}

ipmeta_record_set_t *ipmeta_record_set_init()
{
  ipmeta_record_set_t *this;

  if((this = malloc_zero(sizeof(ipmeta_record_set_t))) == NULL)
  {
    ipmeta_log(__func__, "could not malloc ipmeta_record_set_t");
    return NULL;
  }
  return this;
}

void ipmeta_record_set_free(ipmeta_record_set_t **this_p)
{
  ipmeta_record_set_t *this = *this_p;

  free(this->records);
  this->records=NULL;
  free(this->ip_cnts);
  this->ip_cnts=NULL;
  this->n_recs=0;
  this->_cursor=0;
  this->_alloc_size=0;
  free(this);
  *this_p=NULL;
}

void ipmeta_record_set_rewind(ipmeta_record_set_t *this) 
{
  this->_cursor=0;
}

ipmeta_record_t *ipmeta_record_set_next(ipmeta_record_set_t *this, uint32_t *num_ips)
{
  if(this->n_recs<=this->_cursor) {
    // No more records
    return NULL;
  }
  *num_ips = this->ip_cnts[this->_cursor];
  return this->records[this->_cursor++]; // Advance head
}

int ipmeta_record_set_add_record(ipmeta_record_set_t *this, ipmeta_record_t *rec, int num_ips)
{
  this->n_recs++;
  // Realloc if necessary
  if (this->_alloc_size<this->n_recs) {
    if ( (this->records = realloc(this->records, sizeof(ipmeta_record_t*) * this->n_recs)) == NULL) 
    {
      ipmeta_log(__func__, "could not realloc records in record set");
      return -1;
    }

    if ( (this->ip_cnts = realloc(this->ip_cnts, sizeof(uint32_t) * this->n_recs)) == NULL) 
    {
      ipmeta_log(__func__, "could not realloc ip_cnts in record set");
      return -1;
    }    
    this->_alloc_size = this->n_recs;
  }
  this->records[this->n_recs-1] = rec;
  this->ip_cnts[this->n_recs-1] = num_ips;

  return 0;
}

void ipmeta_record_set_clear(ipmeta_record_set_t *this)
{
  this->n_recs=0;
}

void ipmeta_dump_record_set(ipmeta_record_set_t *this, char *ip_str)
{
  ipmeta_record_t *rec;
  uint32_t num_ips;
  ipmeta_record_set_rewind(this);
  while ( (rec = ipmeta_record_set_next(this, &num_ips)) ) {
    ipmeta_dump_record(rec, ip_str, num_ips);
  }
}

void ipmeta_write_record_set(ipmeta_record_set_t *this, iow_t *file, char *ip_str)
{
  ipmeta_record_t *rec;
  uint32_t num_ips;
  ipmeta_record_set_rewind(this);
  while ( (rec = ipmeta_record_set_next(this, &num_ips)) ) {
    ipmeta_write_record(file, rec, ip_str, num_ips);
  }
}

#define PRINT_EMPTY_RECORD(function, file, ip_str, num_ips)	\
  do {							\
    function(file,					\
	     "%s"					\
	     SEPARATOR					\
       "%"PRIu32         \
       SEPARATOR          \
	     SEPARATOR					\
	     SEPARATOR					\
	     SEPARATOR					\
	     SEPARATOR					\
	     SEPARATOR					\
	     SEPARATOR					\
	     SEPARATOR					\
	     SEPARATOR					\
	     SEPARATOR					\
	     SEPARATOR					\
	     SEPARATOR					\
	     SEPARATOR					\
	     SEPARATOR					\
	     SEPARATOR					\
	     "\n",					    \
	     ip_str,            \
       num_ips);					\
  } while(0)

#define PRINT_RECORD(function, file, record, ip_str, num_ips)			\
  do {									\
    function(file,							\
	     "%s"							\
	     SEPARATOR							\
       "%"PRIu32              \
       SEPARATOR              \
	     "%"PRIu32							\
	     SEPARATOR							\
	     "%s"							\
	     SEPARATOR							\
	     "%s"							\
	     SEPARATOR							\
	     "%s"							\
	     SEPARATOR							\
	     "%s"							\
	     SEPARATOR							\
	     "%s"							\
	     SEPARATOR							\
	     "%f"							\
	     SEPARATOR							\
	     "%f"							\
	     SEPARATOR							\
	     "%"PRIu32							\
	     SEPARATOR							\
	     "%"PRIu32							\
	     SEPARATOR							\
	     "%"PRIu16							\
	     SEPARATOR							\
	     "%s"							\
             SEPARATOR,                                                 \
	     ip_str,							\
       num_ips,             \
	     record->id,						\
	     record->country_code,					\
	     record->continent_code,					\
	     record->region,						\
	     (record->city == NULL ? "" : record->city),		\
	     (record->post_code == NULL ? "" : record->post_code),	\
	     record->latitude,						\
	     record->longitude,						\
	     record->metro_code,					\
	     record->area_code,						\
	     record->region_code,					\
	     (record->conn_speed == NULL ? "" : record->conn_speed)     \
             );                                                         \
    for(i=0; i<record->polygon_ids_cnt; i++)                            \
          {                                                             \
            function(file, "%"PRIu32, record->polygon_ids[i]);          \
            if(i<record->polygon_ids_cnt-1)                             \
              function(file, ",");                                      \
          }                                                             \
    function(file, "|");                                                \
    if(record->asn_cnt > 0)						\
      {									\
	for(i=0; i<record->asn_cnt; i++)				\
	  {								\
	    function(file, "%d", record->asn[i]);			\
	    if(i<record->asn_cnt-1)					\
	      function(file, "_");					\
	  }								\
	function(file, "|%"PRIu32"\n", record->asn_ip_cnt);		\
      }									\
    else                                                                \
      {                                                                 \
        function(file, "|\n");                                          \
      }                                                                 \
  } while(0)

void ipmeta_dump_record(ipmeta_record_t *record, char *ip_str, int num_ips)
{
  int i;

  if(record == NULL)
    {
      /* dump an empty record */
      PRINT_EMPTY_RECORD(fprintf, stdout, ip_str, num_ips);
    }
  else
    {
      PRINT_RECORD(fprintf, stdout, record, ip_str, num_ips);
    }
  return;
}

#define PRINT_RECORD_HEADER(function, file)	\
  do {						\
  function(file,				\
	   "ip-prefix"					\
	   SEPARATOR				\
     "num-ips"          \
     SEPARATOR        \
	   "id"					\
	   SEPARATOR				\
	   "country-code"			\
	   SEPARATOR				\
	   "continent-code"			\
	   SEPARATOR				\
	   "region"				\
	   SEPARATOR				\
	   "city"				\
	   SEPARATOR				\
	   "post-code"				\
	   SEPARATOR				\
	   "latitude"				\
	   SEPARATOR				\
	   "longitude"				\
	   SEPARATOR				\
	   "metro-code"				\
	   SEPARATOR				\
	   "area-code"				\
	   SEPARATOR				\
	   "region-code"			\
	   SEPARATOR				\
	   "connection-speed"			\
	   SEPARATOR				\
     "polygon-ids"                        \
     SEPARATOR                            \
	   "asn"				\
	   SEPARATOR				\
	   "asn-ip-cnt"				\
	   "\n");				\
    } while(0)

void ipmeta_dump_record_header()
{
  PRINT_RECORD_HEADER(fprintf, stdout);
}

inline void ipmeta_write_record(iow_t *file, ipmeta_record_t *record,
				char *ip_str, int num_ips)
{
  int i;

  if(record == NULL)
    {
      PRINT_EMPTY_RECORD(wandio_printf, file, ip_str, num_ips);
    }
  else
    {
      PRINT_RECORD(wandio_printf, file, record, ip_str, num_ips);
    }
  return;
}

void ipmeta_write_record_header(iow_t *file)
{
  PRINT_RECORD_HEADER(wandio_printf, file);
}
