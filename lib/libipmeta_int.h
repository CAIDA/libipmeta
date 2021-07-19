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
  uint64_t *ip_cnts; // count of IPv4 addresses or IPv6 /64 subnets matched
  size_t n_recs;

  size_t _cursor;
  size_t _alloc_size;
};

/** @} */

/** Add a record to a record set. If necessary the internal structures will be
 * realloc'd (only enlarging, never shrinking)
 *
 * @param record_set    The record set instance to add the record to
 * @param rec           The record to add
 * @param num_ips       The number of IPv4 addresses or IPv6 /64 subnets
 *                      matched in this record
 *
 * @return 0 if insertion was successful, or -1 if realloc failed
 */
int ipmeta_record_set_add_record(ipmeta_record_set_t *record_set,
                                 ipmeta_record_t *rec, uint64_t num_ips);

/** Empties the set.
 *
 * @param record_set    The record set instance to clear the records for
 *
 * @note this function does not actually destroy any memory.
 */
void ipmeta_record_set_clear(ipmeta_record_set_t *record_set);

#endif /* __LIBIPMETA_INT_H */
