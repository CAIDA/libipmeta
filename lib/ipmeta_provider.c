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
 * This software is Copyright (c) 2013 The Regents of the University of
 * California. All Rights Reserved. Permission to copy, modify, and distribute this
 * software and its documentation for academic research and education purposes,
 * without fee, and without a written agreement is hereby granted, provided that
 * the above copyright notice, this paragraph and the following three paragraphs
 * appear in all copies. Permission to make use of this software for other than
 * academic research and education purposes may be obtained by contacting:
 * 
 * Office of Innovation and Commercialization
 * 9500 Gilman Drive, Mail Code 0910
 * University of California
 * La Jolla, CA 92093-0910
 * (858) 534-5815
 * invent@ucsd.edu
 * 
 * This software program and documentation are copyrighted by The Regents of the
 * University of California. The software program and documentation are supplied
 * "as is", without any accompanying services from The Regents. The Regents does
 * not warrant that the operation of the program will be uninterrupted or
 * error-free. The end-user understands that the program was developed for research
 * purposes and is advised not to rely exclusively on the program for any reason.
 * 
 * IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING LOST
 * PROFITS, ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF
 * THE UNIVERSITY OF CALIFORNIA HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE. THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE. THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS
 * IS" BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATIONS TO PROVIDE
 * MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS. 
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
};

void ipmeta_free_record(ipmeta_record_t *record)
{
  if (record == NULL) {
    return;
  }

  free(record->region);
  record->region = NULL;

  free(record->city);
  record->city = NULL;

  free(record->post_code);
  record->post_code = NULL;

  free(record->conn_speed);
  record->conn_speed = NULL;

  free(record->polygon_ids);
  record->polygon_ids = NULL;

  free(record->asn);
  record->asn = NULL;
  record->asn_cnt = 0;

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
      kh_free_vals(ipmeta_rechash, provider->all_records, ipmeta_free_record);
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
