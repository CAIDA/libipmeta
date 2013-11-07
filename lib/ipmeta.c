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

#include "utils.h"

#include "libipmeta_int.h"
#include "ipmeta_ds.h"
#include "ipmeta_provider.h"

ipmeta_t *ipmeta_init()
{
  return NULL;
}

ipmeta_provider_t *ipmeta_enable_provider(ipmeta_t *ipmeta,
					  ipmeta_provider_id_t *provider_id,
					  ipmeta_ds_id_t ds_id,
					  ipmeta_provider_default_t set_default)
{
  return NULL;
}

ipmeta_provider_t *ipmeta_get_default_provider(ipmeta_t *ipmeta)
{
  assert(ipmeta != NULL);
  return ipmeta->provider_default;
}

ipmeta_provider_t *ipmeta_get_provider_by_id(ipmeta_t *ipmeta,
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

ipmeta_record_t *ipmeta_lookup(ipmeta_provider_t *provider,
			       uint32_t addr)
{
  assert(provider != NULL && provider->enabled != 0);

  return provider->lookup(provider, addr);
}

int ipmeta_get_provider_id(ipmeta_provider_t *provider)
{
  assert(provider != NULL);

  return provider->id;
}

const char *ipmeta_get_provider_name(ipmeta_provider_t *provider)
{
  assert(provider != NULL);

  return provider->name;
}

const char **ipmeta_get_provider_names()
{
  assert(0);
  return NULL;
}

void ipmeta_dump_record(ipmeta_record_t *record)
{
  int i;
  if(record == NULL)
    {
      return;
    }

  fprintf(stdout,
	  "id: %"PRIu32", cc: %s, cont: %d, reg: %s, city: %s, post: %s, "
	  "lat: %f, long: %f, met: %"PRIu32", area: %"PRIu32", "
	  "speed: %s, asn: ",
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
