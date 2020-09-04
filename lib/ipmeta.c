/*
 * libipmeta
 *
 * Alistair King, CAIDA, UC San Diego
 * corsaro-info@caida.org
 *
 * Copyright (C) 2013-2018, 2019, 2020 The Regents of the University of California.
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
#include <wandio.h>

#include "khash.h"
#include "utils.h"
#include "parse_cmd.h"

#include "libipmeta_int.h"
#include "ipmeta_ds.h"
#include "ipmeta_provider.h"
#include "ipvx_utils.h"

#define MAXOPTS 1024

#define SEPARATOR "|"

ipmeta_t *ipmeta_init(enum ipmeta_ds_id dstype)
{
  ipmeta_t *ipmeta;
  int i;
  ipmeta_log(__func__, "initializing libipmeta");

  /* allocate some memory for our state */
  if ((ipmeta = malloc_zero(sizeof(ipmeta_t))) == NULL) {
    ipmeta_log(__func__, "could not malloc ipmeta_t");
    return NULL;
  }

  /* allocate the providers */
  if (ipmeta_provider_alloc_all(ipmeta) != 0) {
    free(ipmeta);
    return NULL;
  }

  if (ipmeta_ds_init(&(ipmeta->datastore), dstype) != 0) {
    for (i = 0; i < IPMETA_PROVIDER_MAX; i++) {
      ipmeta_provider_free(ipmeta, ipmeta->providers[i]);
    }
    free(ipmeta);
    return NULL;
  }
  ipmeta_log(__func__, "using datastore %s", ipmeta->datastore->name);

  return ipmeta;
}

void ipmeta_free(ipmeta_t *ipmeta)
{
  int i;

  /* no mercy for double frees */
  assert(ipmeta != NULL);

  /* loop across all providers and free each one */
  for (i = 0; i < IPMETA_PROVIDER_MAX; i++) {
    ipmeta_provider_free(ipmeta, ipmeta->providers[i]);
  }
  ipmeta->datastore->free(ipmeta->datastore);
  free(ipmeta);
  return;
}

int ipmeta_enable_provider(ipmeta_t *ipmeta, ipmeta_provider_t *provider,
                           const char *options)
{
  char *local_args = NULL;
  char *process_argv[MAXOPTS];
  int len;
  int process_argc = 0;
  int rc;

  ipmeta_log(__func__, "enabling provider (%s)", provider->name);

  /* first we need to parse the options */
  if (options != NULL && (len = strlen(options)) > 0) {
    local_args = strndup(options, len);
    parse_cmd(local_args, &process_argc, process_argv, MAXOPTS, provider->name);
  }

  /* we just need to pass this along to the provider framework */
  rc = ipmeta_provider_init(ipmeta, provider, process_argc, process_argv);

  if (local_args != NULL) {
    free(local_args);
  }

  ipmeta->all_provmask |= IPMETA_PROV_TO_MASK(provider->id);
  return rc;
}

inline ipmeta_provider_t *ipmeta_get_provider_by_id(ipmeta_t *ipmeta,
                                                    ipmeta_provider_id_t id)
{
  assert(ipmeta != NULL);
  if (id <= 0 || id > IPMETA_PROVIDER_MAX) {
    return NULL;
  }
  return ipmeta->providers[id - 1];
}

ipmeta_provider_t *ipmeta_get_provider_by_name(ipmeta_t *ipmeta,
                                               const char *name)
{
  ipmeta_provider_t *provider;
  int i;

  for (i = 1; i <= IPMETA_PROVIDER_MAX; i++) {
    if ((provider = ipmeta_get_provider_by_id(ipmeta, i)) != NULL &&
        strcasecmp(provider->name, name) == 0) {
      return provider;
    }
  }

  return NULL;
}

inline int ipmeta_lookup_pfx(ipmeta_t *ipmeta, int family, void *addrp,
                             uint8_t pfxlen, uint32_t providermask,
                             ipmeta_record_set_t *records)
{
  assert(ipmeta != NULL && records != NULL);

  ipmeta_record_set_clear(records);
  if (providermask == 0) {
    providermask = ipmeta->all_provmask;
  }

  return ipmeta->datastore->lookup_pfx(ipmeta->datastore, family, addrp, pfxlen,
                                       providermask, records);
}

inline int ipmeta_lookup_addr(ipmeta_t *ipmeta, int family, void *addrp,
                              uint32_t providermask, ipmeta_record_set_t *found)
{
  ipmeta_record_set_clear(found);
  if (providermask == 0) {
    providermask = ipmeta->all_provmask;
  }
  return ipmeta->datastore->lookup_addr(ipmeta->datastore, family, addrp,
                                        providermask, found);
}

inline int ipmeta_lookup(ipmeta_t *ipmeta, const char *addr_str,
                         uint32_t providermask, ipmeta_record_set_t *found)
{
  ipvx_prefix_t pfx;
  int rc;

  if ((rc = ipvx_pton_pfx(addr_str, &pfx)) < 0) {
    return IPMETA_ERR_INPUT;
  }

  if (pfx.masklen == ipvx_family_size(pfx.family)) {
    rc = ipmeta_lookup_addr(ipmeta, pfx.family, &pfx.addr, providermask, found);
  } else {
    rc = ipmeta_lookup_pfx(ipmeta, pfx.family, &pfx.addr, pfx.masklen, providermask, found);
  }
  return (rc < 0) ? IPMETA_ERR_INTERNAL : rc;
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
  ipmeta_record_set_t *record_set;

  if ((record_set = malloc_zero(sizeof(ipmeta_record_set_t))) == NULL) {
    ipmeta_log(__func__, "could not malloc ipmeta_record_set_t");
    return NULL;
  }

  /* always have space for a single record */
  if (ipmeta_record_set_add_record(record_set, NULL, 0) != 0) {
    free(record_set);
    return NULL;
  }
  ipmeta_record_set_clear(record_set);

  return record_set;
}

void ipmeta_record_set_clear(ipmeta_record_set_t *record_set)
{
  if (record_set == NULL) {
    return;
  }

  record_set->n_recs = 0;
  record_set->_cursor = 0;
}

void ipmeta_record_set_free(ipmeta_record_set_t **record_set_p)
{
  ipmeta_record_set_t *record_set = *record_set_p;
  if (record_set_p == NULL || record_set == NULL) {
    return;
  }

  free(record_set->records);
  record_set->records = NULL;

  free(record_set->ip_cnts);
  record_set->ip_cnts = NULL;

  record_set->n_recs = 0;
  record_set->_cursor = 0;
  record_set->_alloc_size = 0;

  free(record_set);
  *record_set_p = NULL;
}

void ipmeta_record_set_rewind(ipmeta_record_set_t *record_set)
{
  record_set->_cursor = 0;
}

ipmeta_record_t *ipmeta_record_set_next(ipmeta_record_set_t *record_set,
                                        uint64_t *num_ips)
{
  if (record_set->n_recs <= record_set->_cursor) {
    /* No more records */
    return NULL;
  }

  if (num_ips != NULL) {
    *num_ips = record_set->ip_cnts[record_set->_cursor];
  }

  return record_set->records[record_set->_cursor++]; /* Advance head */
}

int ipmeta_record_set_add_record(ipmeta_record_set_t *record_set,
                                 ipmeta_record_t *rec, uint64_t num_ips)
{
  record_set->n_recs++;

  /* Realloc if necessary */
  if (record_set->_alloc_size < record_set->n_recs) {
    /* round n_recs up to next pow 2 */
    record_set->_alloc_size = record_set->n_recs;
    kroundup32(record_set->_alloc_size);

    if ((record_set->records = realloc(record_set->records,
        sizeof(*record_set->records) * record_set->_alloc_size)) == NULL) {
      ipmeta_log(__func__, "could not realloc records in record set");
      return -1;
    }

    if ((record_set->ip_cnts = realloc(record_set->ip_cnts,
       sizeof(*record_set->ip_cnts) * record_set->_alloc_size)) == NULL) {
      ipmeta_log(__func__, "could not realloc ip_cnts in record set");
      return -1;
    }
  }

  record_set->records[record_set->n_recs - 1] = rec;
  record_set->ip_cnts[record_set->n_recs - 1] = num_ips;

  return 0;
}

void ipmeta_dump_record_set(ipmeta_record_set_t *record_set, char *ip_str)
{
  ipmeta_write_record_set(record_set, NULL, ip_str);
}

void ipmeta_write_record_set(ipmeta_record_set_t *record_set, iow_t *file,
                             char *ip_str)
{
  ipmeta_record_t *rec;
  uint64_t num_ips;
  ipmeta_record_set_rewind(record_set);
  while ((rec = ipmeta_record_set_next(record_set, &num_ips))) {
    ipmeta_write_record(file, rec, ip_str, num_ips);
  }
}

void ipmeta_dump_record_set_by_provider(ipmeta_record_set_t *this, char *ip_str,
                                        int provid)
{
  ipmeta_write_record_set_by_provider(this, NULL, ip_str, provid);
}

void ipmeta_write_record_set_by_provider(ipmeta_record_set_t *this, iow_t *file,
                                         char *ip_str, int provid)
{
  ipmeta_record_t *rec;
  uint64_t num_ips = 0;
  ipmeta_record_set_rewind(this);
  int dumped = 0;
  while ((rec = ipmeta_record_set_next(this, &num_ips))) {
    if (rec->source != provid)
      continue;
    ipmeta_write_record(file, rec, ip_str, num_ips);
    dumped++;
  }
  if (dumped == 0) {
    ipmeta_write_record(file, NULL, ip_str, num_ips);
  }
}

int64_t ipmeta_printf(iow_t *file, const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  if (file)
    return wandio_vprintf(file, fmt, ap);
  else
    return vprintf(fmt, ap);
}

void ipmeta_dump_record(ipmeta_record_t *record, char *ip_str, uint64_t num_ips)
{
  ipmeta_write_record(NULL, record, ip_str, num_ips);
}

void ipmeta_dump_record_header()
{
  ipmeta_write_record_header(NULL);
}

void ipmeta_write_record(iow_t *file, ipmeta_record_t *record, char *ip_str,
                         uint64_t num_ips)
{
  int i;

  if (record == NULL) {
    ipmeta_printf(file,
             "%s" SEPARATOR "%" PRIu64 SEPARATOR SEPARATOR SEPARATOR SEPARATOR
               SEPARATOR SEPARATOR SEPARATOR SEPARATOR SEPARATOR SEPARATOR
                 SEPARATOR SEPARATOR SEPARATOR SEPARATOR SEPARATOR "\n",
             ip_str, num_ips);
  } else {
    ipmeta_printf(file,
             "%s" SEPARATOR "%" PRIu64 SEPARATOR "%" PRIu32 SEPARATOR
             "%s" SEPARATOR "%s" SEPARATOR "%s" SEPARATOR "%s" SEPARATOR
             "%s" SEPARATOR "%f" SEPARATOR "%f" SEPARATOR "%" PRIu32 SEPARATOR
             "%" PRIu32 SEPARATOR "%" PRIu16 SEPARATOR "%s" SEPARATOR,
             ip_str, num_ips, record->id, record->country_code,
             record->continent_code,
             (record->region == NULL ? "" : record->region),
             (record->city == NULL ? "" : record->city),
             (record->post_code == NULL ? "" : record->post_code),
             record->latitude, record->longitude, record->metro_code,
             record->area_code, record->region_code,
             (record->conn_speed == NULL ? "" : record->conn_speed));
    for (i = 0; i < record->polygon_ids_cnt; i++) {
      ipmeta_printf(file, "%" PRIu32, record->polygon_ids[i]);
      if (i < record->polygon_ids_cnt - 1)
        ipmeta_printf(file, ",");
    }
    ipmeta_printf(file, "|");
    if (record->asn_cnt > 0) {
      for (i = 0; i < record->asn_cnt; i++) {
        ipmeta_printf(file, "%" PRIu32, record->asn[i]);
        if (i < record->asn_cnt - 1)
          ipmeta_printf(file, "_");
      }
      ipmeta_printf(file, "|%" PRIu64, record->asn_ip_cnt);
    } else {
      ipmeta_printf(file, "|");
    }
    ipmeta_printf(file,
             "%s" SEPARATOR "%d" "\n",
             record->timezone == NULL ? "" : record->timezone,
             record->accuracy);
  }
  return;
}

void ipmeta_write_record_header(iow_t *file)
{
  ipmeta_printf(file, "ip-prefix" SEPARATOR "num-ips" SEPARATOR "id" SEPARATOR
                 "country-code" SEPARATOR "continent-code" SEPARATOR
                 "region" SEPARATOR "city" SEPARATOR "post-code" SEPARATOR
                 "latitude" SEPARATOR "longitude" SEPARATOR
                 "metro-code" SEPARATOR "area-code" SEPARATOR
                 "region-code" SEPARATOR "connection-speed" SEPARATOR
                 "polygon-ids" SEPARATOR "asn" SEPARATOR "asn-ip-cnt" SEPARATOR
                 "timezone" SEPARATOR "accuracy"
                 "\n");
}
