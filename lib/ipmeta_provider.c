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

/* netacq edge */
#include "ipmeta_provider_netacq_edge.h"

/* ipinfo */
#include "ipmeta_provider_ipinfo.h"

#if 0
/* netacq legacy */
#include "ipmeta_provider_netacq.h"
#endif

/* pfx2as */
#include "ipmeta_provider_pfx2as.h"

/** Convenience typedef for the provider alloc function type */
typedef ipmeta_provider_t *(*provider_alloc_func_t)(void);

/** Array of datastructure allocation functions.
 *
 * @note the indexes of these functions must exactly match the ID in
 * ipmeta_ds_id_t. The element at index 0 MUST be NULL.
 */
static const provider_alloc_func_t provider_alloc_functions[] = {
  NULL,
  ipmeta_provider_maxmind_alloc,
  ipmeta_provider_netacq_edge_alloc,
  ipmeta_provider_pfx2as_alloc,
  ipmeta_provider_ipinfo_alloc,
};

void ipmeta_clean_record(ipmeta_record_t *record)
{
  if (record == NULL) {
    return;
  }

  if (record->region) {
    free(record->region);
  }

  if (record->city) {
    free(record->city);
  }

  if (record->post_code) {
    free(record->post_code);
  }

  if (record->conn_speed) {
    free(record->conn_speed);
  }

  if (record->polygon_ids) {
    free(record->polygon_ids);
  }

  if (record->asn) {
    free(record->asn);
  }

  if (record->timezone) {
    free(record->timezone);
  }

  memset(record, 0, sizeof(ipmeta_record_t));
}

void ipmeta_free_record(ipmeta_record_t *record) {
  if (record) {
    ipmeta_clean_record(record);
    free(record);
  }
  return;
}

/* --- Public functions below here -- */

int ipmeta_provider_alloc_all(ipmeta_t *ipmeta)
{
  assert(ipmeta != NULL);
  assert(ARR_CNT(provider_alloc_functions) == IPMETA_PROVIDER_MAX + 1);

  int i;

  /* loop across all providers and alloc each one */
  for (i = 1; i <= IPMETA_PROVIDER_MAX; i++) {
    ipmeta_provider_t *provider;
    /* first, create the struct */
    if ((provider = malloc_zero(sizeof(ipmeta_provider_t))) == NULL) {
      ipmeta_log(__func__, "could not malloc ipmeta_provider_t");
      return -1;
    }

    /* get the core provider details (id, name) from the provider plugin */
    memcpy(provider, provider_alloc_functions[i](), sizeof(ipmeta_provider_t));

    /* poke it into ipmeta */
    ipmeta->providers[i - 1] = provider;
  }

  return 0;
}

int ipmeta_provider_init(ipmeta_t *ipmeta, ipmeta_provider_t *provider,
                         int argc, char **argv)
{
  assert(ipmeta != NULL);
  assert(provider != NULL);

  /* if it has already been initialized, then we simply return */
  if (provider->enabled != 0) {
    ipmeta_log(__func__,
               "WARNING: provider (%s) is already initialized, "
               "ignoring new settings",
               provider->name);
    return 0;
  }

  /* otherwise, we need to init this plugin */

  /* initialize the record hash */
  provider->all_records = kh_init(ipmeta_rechash);
  provider->ds = ipmeta->datastore;

  /* now that we have set up the datastructure stuff, ask the provider to
     initialize. this will normally mean that it reads in some database and
     populates the datatructures */
  if (provider->init(provider, argc, argv) != 0) {
    goto err;
  }

  /* 2017-03-31 AK moves this to after a successful init, otherwise the provider
     is marked as enabled even when it is not. But I'm not sure if this leads to
     a memory leak :/ */
  provider->enabled = 1;

  return 0;

err:
  if (provider != NULL) {
    provider->ds = NULL;
    /* do not free the provider as we did not alloc it */
  }
  return -1;
}

void ipmeta_provider_free(ipmeta_t *ipmeta, ipmeta_provider_t *provider)
{
  assert(ipmeta != NULL);
  assert(provider != NULL);
  /* only free everything if we were enabled */
  if (provider->enabled != 0) {
    /* ask the provider to free it's own state */
    provider->free(provider);

    /* remove the pointer from ipmeta */
    ipmeta->providers[provider->id - 1] = NULL;

    /* free the records hash */
    if (provider->all_records != NULL) {
      /* this is where the records are free'd */
      kh_free_vals(ipmeta_rechash, provider->all_records,
            provider->free_record);
      kh_destroy(ipmeta_rechash, provider->all_records);
      provider->all_records = NULL;
    }
  }

  /* finally, free the actual provider structure */
  free(provider);

  return;
}

void ipmeta_provider_register_state(ipmeta_provider_t *provider, void *state)
{
  assert(provider != NULL);
  assert(state != NULL);

  provider->state = state;
}

void ipmeta_provider_free_state(ipmeta_provider_t *provider)
{
  assert(provider != NULL);

  free(provider->state);
  provider->state = NULL;
}

ipmeta_record_t *ipmeta_provider_insert_record(ipmeta_provider_t *provider,
                                               ipmeta_record_t *record)
{
  khiter_t khiter;
  int khret;

  record->source = provider->id;

  khiter = kh_put(ipmeta_rechash, provider->all_records, record->id, &khret);
  assert(khret != 0); // id was not already present
  kh_value(provider->all_records, khiter) = record;

  return record;
}

ipmeta_record_t *ipmeta_provider_init_record(ipmeta_provider_t *provider,
                                             uint32_t id)
{
  ipmeta_record_t *record;

  if ((record = malloc_zero(sizeof(ipmeta_record_t))) == NULL) {
    return NULL;
  }

  record->id = id;

  return ipmeta_provider_insert_record(provider, record);
}

ipmeta_record_t *ipmeta_provider_get_record(ipmeta_provider_t *provider,
                                            uint32_t id)
{
  khiter_t khiter;

  /* grab the corresponding record from the hash */
  if ((khiter = kh_get(ipmeta_rechash, provider->all_records, id)) ==
      kh_end(provider->all_records)) {
    return NULL;
  }
  return kh_val(provider->all_records, khiter);
}

int ipmeta_provider_get_all_records(ipmeta_provider_t *provider,
                                    ipmeta_record_t ***records)
{
  ipmeta_record_t **rec_arr = NULL;
  ipmeta_record_t **rec_ptr = NULL;
  unsigned rec_cnt = kh_size(provider->all_records);
  khiter_t i;

  /* if there are no records in the array, don't bother */
  if (rec_cnt == 0) {
    *records = NULL;
    return 0;
  }

  /* first we malloc an array to hold all the records */
  if ((rec_arr = malloc(sizeof(ipmeta_record_t *) * rec_cnt)) == NULL) {
    return -1;
  }

  rec_ptr = rec_arr;
  /* insert all the records into the array */
  for (i = kh_begin(provider->all_records); i != kh_end(provider->all_records);
       ++i) {
    if (kh_exist(provider->all_records, i)) {
      *rec_ptr = kh_value(provider->all_records, i);
      rec_ptr++;
    }
  }

  /* return the array and the count */
  *records = rec_arr;
  return (int)rec_cnt;
}

int ipmeta_provider_associate_record(ipmeta_provider_t *provider, int family,
    void *addrp, uint8_t pfxlen, ipmeta_record_t *record)
{
  assert(provider != NULL && record != NULL);
  assert(provider->ds != NULL);

  return provider->ds->add_prefix(provider->ds, family, addrp, pfxlen, record);
}

int ipmeta_provider_lookup_pfx(ipmeta_provider_t *provider, int family,
    void *addrp, uint8_t pfxlen, ipmeta_record_set_t *records)
{
  return provider->ds->lookup_pfx(provider->ds, family, addrp, pfxlen,
                                  IPMETA_PROV_TO_MASK(provider->id), records);
}

int ipmeta_provider_lookup_addr(ipmeta_provider_t *provider, int family,
    void *addrp, ipmeta_record_set_t *found)
{
  return provider->ds->lookup_addr(provider->ds, family, addrp,
                                   IPMETA_PROV_TO_MASK(provider->id), found);
}
