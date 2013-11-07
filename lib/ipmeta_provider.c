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

/* now include the providers */

/* maxmind csv */
#include "ipmeta_provider_maxmind.h"

#if 0
/* netacq edge */
#include "ipmeta_provider_netacq_edge.h"

/* netacq legacy */
#include "ipmeta_provider_netacq.h"
#endif

/** Convenience typedef for the provider alloc function type */
typedef ipmeta_provider_t* (*provider_alloc_func_t)();

/** Array of datastructure allocation functions.
 *
 * @note the indexes of these functions must exactly match the ID in
 * ipmeta_ds_id_t. The element at index 0 MUST be NULL.
 */
static const provider_alloc_func_t provider_alloc_functions[] = {
  NULL,
  ipmeta_provider_maxmind_alloc,
};

static void free_record(ipmeta_record_t *record)
{
  if(record == NULL)
    {
      return;
    }

  /* free the strings */
  if(record->city != NULL)
    {
      free(record->city);
      record->city = NULL;
    }

  if(record->post_code != NULL)
    {
      free(record->post_code);
      record->post_code = NULL;
    }

  if(record->asn != NULL)
    {
      free(record->asn);
      record->asn = NULL;
      record->asn_cnt = 0;
    }

  free(record);
  return;
}

/* --- Public functions below here -- */

int ipmeta_provider_alloc_all(ipmeta_t *ipmeta)
{
  assert(ipmeta != NULL);
  assert(ARR_CNT(provider_alloc_functions) == IPMETA_PROVIDER_MAX + 1);

  int i;

  /* loop across all providers and alloc each one */
  for(i = 1; i <= IPMETA_PROVIDER_MAX; i++)
    {
      ipmeta_provider_t *provider;
      /* first, create the struct */
      if((provider = malloc_zero(sizeof(ipmeta_provider_t))) == NULL)
	{
	  ipmeta_log(__func__, "could not malloc ipmeta_provider_t");
	  return NULL;
	}

      /* get the core provider details (id, name) from the provider plugin */
      memcpy(provider,
	     provider_alloc_functions[provider_id](),
	     sizeof(ipmeta_ds_t));

      /* poke it into ipmeta */
      ipmeta->providers[i] = provider;
    }

}

ipmeta_provider_t *ipmeta_provider_init(ipmeta_t *ipmeta,
					ipmeta_provider_t *provider,
					ipmeta_ds_id_t ds_id,
					int argc, char **argv,
					ipmeta_provider_default_t set_default)
{
  assert(ipmeta != NULL);
  assert(provider != NULL);
  assert(ipmeta->providers[provider_id] != NULL);

  /* if it has already been initialized, then we simply return a pointer */
  if(provider->enabled != 0)
    {
      ipmeta_log(__func__,
		 "WARNING: provider (%s) is already initialized, "
		 "ignoring new settings", provider->name);
      return provider;
    }

  /* otherwise, we need to init this plugin */

  /* initialize the record hash */
  provider->all_records = kh_init(ipmeta_rechash);

  /* initialize the datastructure */
  if(ipmeta_ds_init(provider, ds_id) != 0)
    {
      ipmeta_log(__func__, "could not initialize datastructure");
      goto err;
    }

  if(set_default == IPMETA_PROVIDER_DEFAULT_YES)
    {
      ipmeta->provider_default = provider;
    }

  /* now that we have set up the datastructure stuff, ask the provider to
     initialize. this will normally mean that it reads in some database and
     populates the datatructures */
  if(provider->init(provider, argc, argv) != 0)
    {
      goto err;
    }

  provider->enabled = 1;

  return provider;

 err:
  if(provider != NULL)
    {
      if(provider->ds != NULL)
	{
	  provider->ds->free(provider->ds);
	  provider->ds = NULL;
	}
      /* do not free the provider as we did not alloc it */
    }
  return NULL;
}


void ipmeta_provider_free(ipmeta_t *ipmeta,
			  ipmeta_provider_t *provider)
{
  assert(ipmeta != NULL);
  assert(provider != NULL);

  /* ok, lets check if we were the default */
  if(ipmeta->provider_default == provider)
    {
      ipmeta->provider_default = NULL;
    }

  /* remove the pointer from ipmeta */
  ipmeta->providers[provider->id - 1] = NULL;

  /* free the ds */
  if(provider->ds != NULL)
    {
      /* @todo consider adding a ipmeta_ds_free wrapper? */
      provider->ds->free(provider->ds);
      provider->ds = NULL;
    }

  /* free the records hash */
  if(provider->all_records != NULL)
    {
      /* this is where the records are free'd */
      kh_free_vals(ipmeta_rechash, provider->all_records, free_record);
      kh_destroy(ipmeta_rechash, provider->all_records);
      provider->all_records = NULL;
    }

  /* finally, free the actual provider structure */
  free(provider);

  return;
}

ipmeta_record_t *ipmeta_provider_init_record(ipmeta_provider_t *provider,
					     uint32_t id)
{
  ipmeta_record_t *record;
  khiter_t khiter;
  int khret;

  if((record = malloc_zero(sizeof(ipmeta_record_t))) == NULL)
    {
      return NULL;
    }

  record->id = id;

  assert(kh_get(ipmeta_rechash, provider->all_records, id) ==
	 kh_end(provider->all_records));

  khiter = kh_put(ipmeta_rechash, provider->all_records, id, &khret);
  kh_value(provider->all_records, khiter) = record;

  assert(kh_get(ipmeta_rechash, provider->all_records, id) !=
	 kh_end(provider->all_records));

  return record;
}

ipmeta_record_t *ipmeta_provider_get_record(ipmeta_provider_t *provider,
					    uint32_t id)
{
  khiter_t khiter;

  /* grab the corresponding record from the hash */
  if((khiter = kh_get(ipmeta_rechash, provider->all_records, id))
     == kh_end(provider->all_records))
    {
      return NULL;
    }
  return kh_val(provider->all_records, khiter);
}

int ipmeta_provider_get_all_records(ipmeta_provider_t *provider,
				    ipmeta_record_t ***records)
{
  ipmeta_record_t **rec_arr = NULL;
  ipmeta_record_t **rec_ptr = NULL;
  int rec_cnt = kh_size(provider->all_records);
  khiter_t i;

  /* if there are no records in the array, don't bother */
  if(rec_cnt == 0)
    {
      *records = NULL;
      return 0;
    }

  /* first we malloc an array to hold all the records */
  if((rec_arr = malloc(sizeof(ipmeta_record_t*) * rec_cnt)) == NULL)
    {
      return -1;
    }

  rec_ptr = rec_arr;
  /* insert all the records into the array */
  for(i = kh_begin(provider->all_records);
      i != kh_end(provider->all_records);
      ++i)
    {
      if(kh_exist(provider->all_records, i))
	{
	  *rec_ptr = kh_value(provider->all_records, i);
	  rec_ptr++;
	}
    }

  /* return the array and the count */
  *records = rec_arr;
  return rec_cnt;
}

int ipmeta_provider_associate_record(ipmeta_provider_t *provider,
				     uint32_t addr,
				     uint8_t mask,
				     ipmeta_record_t *record)
{
  assert(provider != NULL && record != NULL);
  assert(provider->ds != NULL);

  return provider->ds->add_prefix(provider->ds, addr, mask, record);
}

ipmeta_record_t *ipmeta_provider_lookup_record(ipmeta_provider_t *provider,
					       uint32_t addr)
{
  assert(provider != NULL);
  assert(provider->ds != NULL);

  return provider->ds->lookup_record(provider->ds, addr);
}
