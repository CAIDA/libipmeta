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
			   ipmeta_ds_id_t ds_id,
			   const char *options,
			   ipmeta_provider_default_t set_default)
{
  char *local_args = NULL;
  char *process_argv[MAXOPTS];
  int len;
  int process_argc = 0;
  int rc;

  ipmeta_log(__func__, "enabling provider (%s)", provider->name);

  /* first we need to parse the options */
  if(options != NULL && (len = strlen(options)) > 0)
    {
      local_args = strndup(options, len);
      parse_cmd(local_args, &process_argc, process_argv, MAXOPTS,
		provider->name);
    }

  /* we just need to pass this along to the provider framework */
  rc = ipmeta_provider_init(ipmeta, provider, ds_id,
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

inline ipmeta_record_t *ipmeta_lookup(ipmeta_provider_t *provider,
			       uint32_t addr)
{
  assert(provider != NULL && provider->enabled != 0);

  return provider->lookup(provider, addr);
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

void ipmeta_dump_record(ipmeta_record_t *record)
{
  int i;

  if(record == NULL)
    {
      return;
    }

  fprintf(stdout,
	  "%"PRIu32
	  SEPARATOR
	  "%s"
	  SEPARATOR
	  "%s"
	  SEPARATOR
	  "%s"
	  SEPARATOR
	  "%s"
	  SEPARATOR
	  "%s"
	  SEPARATOR
	  "%f"
	  SEPARATOR
	  "%f"
	  SEPARATOR
	  "%"PRIu32
	  SEPARATOR
	  "%"PRIu32
	  SEPARATOR
	  "%s"
	  SEPARATOR,
	  record->id,
	  record->country_code,
	  record->continent_code,
	  record->region,
	  record->city,
	  record->post_code,
	  record->latitude,
	  record->longitude,
	  record->metro_code,
	  record->area_code,
	  record->conn_speed
	  );

      for(i=0; i<record->asn_cnt; i++)
	{
	  fprintf(stdout, "%d", record->asn[i]);
	  if(i<record->asn_cnt-1)
	    fprintf(stdout, "_");
	}
      fprintf(stdout, "\n");
}

void ipmeta_dump_record_header()
{
  fprintf(stdout,
	  "id"
	  SEPARATOR
	  "country-code"
	  SEPARATOR
	  "continent-code"
	  SEPARATOR
	  "region"
	  SEPARATOR
	  "city"
	  SEPARATOR
	  "post-code"
	  SEPARATOR
	  "latitude"
	  SEPARATOR
	  "longitude"
	  SEPARATOR
	  "metro-code"
	  SEPARATOR
	  "area-code"
	  SEPARATOR
	  "connection-speed"
	  SEPARATOR
	  "asn"
	  "\n");
}
