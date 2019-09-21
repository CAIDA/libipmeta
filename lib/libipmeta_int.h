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

#ifndef __LIBIPMETA_INT_H
#define __LIBIPMETA_INT_H

#include <inttypes.h>

#include "khash.h"

#include "libipmeta.h"

/** @file
 *
 * @brief Header file that contains the private components of libipmeta.
 *
 * @author Alistair King
 *
 */

KHASH_MAP_INIT_INT(ipmeta_rechash, struct ipmeta_record *)

/**
 * @name Internal Datastructures
 *
 * These datastructures are internal to libipmeta. Some may be exposed as opaque
 * structures by libipmeta.h (e.g ipmeta_t)
 *
 * @{ */

/** Structure which holds state for a libipmeta instance */
struct ipmeta {

  /** Array of metadata providers
   * @note index of provider is given by (ipmeta_provider_id_t - 1)
   */
  struct ipmeta_provider *providers[IPMETA_PROVIDER_MAX];

  struct ipmeta_ds *datastore;

  uint32_t all_provmask;
};

/** Structure which holds a set of records, returned by a query */
struct ipmeta_record_set {

  ipmeta_record_t **records;
  uint32_t *ip_cnts;
  int n_recs;

  int _cursor;
  int _alloc_size;
};

/** @} */

/** Add a record to a record set. If necessary the internal structures will be
 * realloc'd (only enlarging, never shrinking)
 *
 * @param record_set    The record set instance to add the record to
 * @param rec           The record to add
 * @param num_ips       The number of IPs matched in this record
 *
 * @return 0 if insertion was successful, or -1 if realloc failed
 */
int ipmeta_record_set_add_record(ipmeta_record_set_t *record_set,
                                 ipmeta_record_t *rec, int num_ips);

/** Empties the set.
 *
 * @param record_set    The record set instance to clear the records for
 *
 * @note this function does not actually destroy any memory.
 */
void ipmeta_record_set_clear(ipmeta_record_set_t *record_set);

#endif /* __LIBIPMETA_INT_H */
