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

#include <arpa/inet.h>
#include <assert.h>

#include "interval_tree.h"

#include "ipmeta_ds_intervaltree.h"
#include "libipmeta_int.h"

#define DS_NAME "intervaltree"

#define STATE(ds) (IPMETA_DS_STATE(intervaltree, ds))

static ipmeta_ds_t ipmeta_ds_intervaltree = {
  IPMETA_DS_INTERVALTREE, DS_NAME, IPMETA_DS_GENERATE_PTRS(intervaltree) NULL};

typedef struct ipmeta_ds_intervaltree_state {
  interval_tree_t *tree;
  uint8_t providerid;

} ipmeta_ds_intervaltree_state_t;

ipmeta_ds_t *ipmeta_ds_intervaltree_alloc()
{
  return &ipmeta_ds_intervaltree;
}

int ipmeta_ds_intervaltree_init(ipmeta_ds_t *ds)
{
  /* the ds structure is malloc'd already, we just need to init the state */

  assert(STATE(ds) == NULL);

  if ((ds->state = malloc(sizeof(ipmeta_ds_intervaltree_state_t))) == NULL) {
    ipmeta_log(__func__, "could not malloc ipmeta ds interval tree");
    return -1;
  }

  if ((STATE(ds)->tree = interval_tree_init()) == NULL) {
    ipmeta_log(__func__, "could not malloc interval tree");
    return -1;
  }
  STATE(ds)->providerid = 0;

  return 0;
}

void ipmeta_ds_intervaltree_free(ipmeta_ds_t *ds)
{
  if (ds == NULL) {
    return;
  }

  if (STATE(ds) != NULL) {
    if (STATE(ds)->tree != NULL) {
      interval_tree_free(STATE(ds)->tree);
      STATE(ds)->tree = NULL;
    }

    free(STATE(ds));
    ds->state = NULL;
  }

  free(ds);

  return;
}

int ipmeta_ds_intervaltree_add_prefix(ipmeta_ds_t *ds, int family, void *addrp,
                                      uint8_t pfxlen, ipmeta_record_t *record)
{
  if (family != AF_INET) {
    ipmeta_log(__func__, "intervaltree datastructure only supports IPv4");
    return -1;
  }
  uint32_t addr = *(uint32_t *)addrp;

  assert(ds != NULL && ds->state != NULL);
  interval_tree_t *tree = STATE(ds)->tree;
  assert(tree != NULL);

  interval_t interval;

  interval.start = ntohl(addr);
  interval.end = interval.start + (1 << (32 - pfxlen)) - 1;
  interval.data = record;

  if (STATE(ds)->providerid == 0) {
    STATE(ds)->providerid = record->source;
  } else if (STATE(ds)->providerid != record->source) {
    ipmeta_log(
      __func__,
      "interval tree does not support storing records from multiple providers");
    ipmeta_log(__func__,
               "please use a separate ipmeta instance for each provider");
    return -1;
  }

  if (interval_tree_add_interval(tree, &interval) == -1) {
    ipmeta_log(__func__, "could not malloc to insert prefix in interval tree");
    return -1;
  }

  return 0;
}

int ipmeta_ds_intervaltree_lookup_pfx(ipmeta_ds_t *ds, int family, void *addrp,
    uint8_t pfxlen, uint32_t providermask, ipmeta_record_set_t *records)
{
  if (family != AF_INET) {
    ipmeta_log(__func__, "intervaltree datastructure only supports IPv4");
    return -1;
  }
  uint32_t addr = *(uint32_t *)addrp;
  interval_tree_t *tree = STATE(ds)->tree;
  interval_t interval;
  int num_matches = 0;
  interval_t **matches = NULL;
  uint32_t ov_start;
  uint32_t ov_end;
  int i;

  interval.start = ntohl(addr);
  interval.end = interval.start + (1 << (32 - pfxlen)) - 1;
  interval.data = NULL;

  matches = getOverlapping(tree, &interval, &num_matches);

  for (i = 0; i < num_matches; i++) {
    /* Calculate number of (overlapping) IPs in record match */
    ov_start =
      (interval.start > matches[i]->start) ? interval.start : matches[i]->start;

    ov_end = (interval.end < matches[i]->end) ? interval.end : matches[i]->end;

    if (ipmeta_record_set_add_record(records,
                                     (ipmeta_record_t *)matches[i]->data,
                                     ov_end - ov_start + 1) != 0) {
      return -1;
    }
  }

  return (int)records->n_recs;
}

int ipmeta_ds_intervaltree_lookup_addr(ipmeta_ds_t *ds, int family, void *addrp,
    uint32_t providermask, ipmeta_record_set_t *found)
{
  if (family != AF_INET) {
    ipmeta_log(__func__, "intervaltree datastructure only supports IPv4");
    return -1;
  }
  uint32_t addr = *(uint32_t *)addrp;
  interval_tree_t *tree = STATE(ds)->tree;
  interval_t interval;
  int num_matches = 0, i;
  interval_t **matches = NULL;

  interval.start = ntohl(addr);
  interval.end = interval.start;
  interval.data = NULL;

  matches = getOverlapping(tree, &interval, &num_matches);

  /* we only have a single IP! */
  if (num_matches == 0) {
    return 0;
  }
  for (i = 0; i < num_matches; i++) {
    if (ipmeta_record_set_add_record(
          found, (ipmeta_record_t *)(matches[i]->data), 1) != 0) {
      return -1;
    }
  }

  return (int)found->n_recs;
}
