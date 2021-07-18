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
