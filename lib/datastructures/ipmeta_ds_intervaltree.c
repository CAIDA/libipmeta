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
