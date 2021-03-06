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

#include "utils.h"
#include "patricia.h"

#include "libipmeta_int.h"
#include "ipmeta_ds_patricia.h"

#define DS_NAME "patricia"

#define STATE(ds) (IPMETA_DS_STATE(patricia, ds))

static ipmeta_ds_t ipmeta_ds_patricia = {
  IPMETA_DS_PATRICIA, DS_NAME, IPMETA_DS_GENERATE_PTRS(patricia) NULL};

enum { IPV4_IDX, IPV6_IDX, NUM_IPV };

#define family_to_idx(fam) ((fam) == AF_INET6)
#define family_size(fam) \
  ((fam) == AF_INET6 ? sizeof(struct in6_addr) : sizeof(struct in_addr))

typedef struct ipmeta_ds_patricia_state {
  patricia_tree_t *trie[NUM_IPV];

} ipmeta_ds_patricia_state_t;

ipmeta_ds_t *ipmeta_ds_patricia_alloc()
{
  return &ipmeta_ds_patricia;
}

int ipmeta_ds_patricia_init(ipmeta_ds_t *ds)
{
  /* the ds structure is malloc'd already, we just need to init the state */

  assert(STATE(ds) == NULL);

  if ((ds->state = malloc(sizeof(ipmeta_ds_patricia_state_t))) == NULL) {
    ipmeta_log(__func__, "could not malloc patricia state");
    return -1;
  }

  STATE(ds)->trie[IPV4_IDX] = New_Patricia(32);
  assert(STATE(ds)->trie[IPV4_IDX]);
  STATE(ds)->trie[IPV6_IDX] = New_Patricia(128);
  assert(STATE(ds)->trie[IPV6_IDX]);

  return 0;
}

static void free_prefix(void *data)
{
  free(data);
}

void ipmeta_ds_patricia_free(ipmeta_ds_t *ds)
{
  if (ds == NULL) {
    return;
  }

  if (STATE(ds) != NULL) {
    for (int i = 0; i < NUM_IPV; i++) {
      if (STATE(ds)->trie[i] != NULL) {
        Destroy_Patricia(STATE(ds)->trie[i], free_prefix);
        STATE(ds)->trie[i] = NULL;
      }
    }

    free(STATE(ds));
    ds->state = NULL;
  }

  free(ds);

  return;
}

int ipmeta_ds_patricia_add_prefix(ipmeta_ds_t *ds, int family, void *addrp, uint8_t pfxlen,
                                  ipmeta_record_t *record)
{
  assert(ds != NULL && ds->state != NULL);
  patricia_tree_t *trie = STATE(ds)->trie[family_to_idx(family)];
  ipmeta_record_t **recarray = NULL;
  assert(trie != NULL);

  prefix_t trie_pfx;
  trie_pfx.family = family;
  trie_pfx.ref_count = 0;
  patricia_node_t *trie_node;

  trie_pfx.bitlen = pfxlen;
  memcpy(&trie_pfx.add, addrp, family_size(family));

  if ((trie_node = patricia_lookup(trie, &trie_pfx)) == NULL) {
    ipmeta_log(__func__, "failed to insert prefix in trie");
    return -1;
  }

  if (trie_node->data == NULL) {
    trie_node->data = calloc(IPMETA_PROVIDER_MAX, sizeof(ipmeta_record_t *));
  }
  recarray = (ipmeta_record_t **)(trie_node->data);
  recarray[record->source - 1] = record;

  return 0;
}

static inline int extract_records_from_pnode(patricia_node_t *node,
                                             uint32_t provmask,
                                             uint32_t *foundsofar,
                                             ipmeta_record_set_t *found,
                                             uint8_t ascendallowed,
                                             uint8_t masklen)
{
  ipmeta_record_t **recfound;

  while (*foundsofar != provmask && node != NULL) {
    int i;
    if (node->prefix == NULL) {
      node = node->parent;
      continue;
    }

    recfound = (ipmeta_record_t **)(node->data);
    for (i = 0; i < IPMETA_PROVIDER_MAX; i++) {
      if (((1 << i) & provmask) == 0) {
        continue;
      }
      if (((1 << i) & *foundsofar) != 0) {
        continue;
      }
      if (recfound[i] == NULL) {
        continue;
      }

      // For IPv6, we count /64 subnets, not addresses.  Prefixes longer than
      // /64 don't count.
      int maxlen = (node->prefix->family == AF_INET6) ? 64 :
        family_size(node->prefix->family) * 8;
      uint64_t num_ips = (masklen <= maxlen) ? (1UL << (maxlen - masklen)) : 0;

      if (ipmeta_record_set_add_record(found, recfound[i], num_ips) != 0) {
        return -1;
      }
      *foundsofar |= (1 << (i));
    }
    if (!ascendallowed) {
      node = NULL;
    } else {
      node = node->parent;
    }
  }
  return 0;
}

// Toggle nth bit of byte array starting at *p
#define TOGGLEBIT(p, n)  (((uint8_t *)(p))[(n)/8] ^= (0x80 >> ((n) % 8)))

static int descend_ptree(ipmeta_ds_t *ds, prefix_t pfx, uint32_t provmask,
                         uint32_t foundsofar, ipmeta_record_set_t *records)
{
  prefix_t subpfx;
  patricia_node_t *node = NULL;
  patricia_tree_t *trie = STATE(ds)->trie[family_to_idx(pfx.family)];
  uint32_t sub_foundsofar;

  subpfx.family = pfx.family;
  subpfx.ref_count = 0;
  subpfx.bitlen = pfx.bitlen + 1;
  unsigned size = family_size(pfx.family);
  unsigned descend_limit = 32;

  if (pfx.family == AF_INET6) {
    descend_limit = 72;         // don't descend lower than a /72 for v6 prefix
  }

  // try the two CIDR halves
  for (int i = 0; i < 2; i++) {

    if (i == 0) {
      memcpy(&subpfx.add, &pfx.add, size);
    } else {
      TOGGLEBIT(&subpfx.add, pfx.bitlen);
    }

    node = patricia_search_exact(trie, &subpfx);

    // count ancestors only, not siblings or their descendants
    sub_foundsofar = foundsofar;

    if (node) {
      if (extract_records_from_pnode(node, provmask, &sub_foundsofar, records,
            0, subpfx.bitlen) < 0) {
        ipmeta_log(__func__, "error while extracting records for prefix");
        return -1;
      }
    }

    // If we don't have answers for subpfx from all providers, try below subpfx
    if (sub_foundsofar != provmask && subpfx.bitlen < descend_limit) {
      if (descend_ptree(ds, subpfx, provmask, sub_foundsofar, records) < 0) {
        return -1;
      }
    }
  }

  return 0;
}

static int _patricia_prefix_lookup(ipmeta_ds_t *ds, prefix_t pfx,
    uint32_t provmask, ipmeta_record_set_t *records)
{
  patricia_tree_t *trie = STATE(ds)->trie[family_to_idx(pfx.family)];
  patricia_node_t *node = NULL;
  uint32_t foundsofar = 0;

  if (foundsofar == provmask) {
    return 0;
  }

  node = patricia_search_best2(trie, &pfx, 1);

  if (pfx.family == AF_INET6) {
    if (node) {
       assert(node->prefix->family == AF_INET6);
    }
  }

  if (node) {
    if (extract_records_from_pnode(node, provmask, &foundsofar, records, 1,
          pfx.bitlen) < 0) {
      ipmeta_log(__func__, "error while extracting records for prefix");
      return -1;
    }
  }

  if (foundsofar != provmask && pfx.bitlen < 32 && pfx.family == AF_INET) {
    // try looking for more specific prefixes for any providers where we
    // have no answer, but don't waste time ascending the tree
    if (descend_ptree(ds, pfx, provmask, foundsofar, records) < 0) {
      return -1;
    }
  }

  return 0;
}

int ipmeta_ds_patricia_lookup_pfx(ipmeta_ds_t *ds, int family, void *addrp,
    uint8_t pfxlen, uint32_t providermask, ipmeta_record_set_t *records)
{
  prefix_t pfx;

  pfx.family = family;
  pfx.ref_count = 0;
  memcpy(&pfx.add, addrp, family_size(family));
  pfx.bitlen = pfxlen;

  _patricia_prefix_lookup(ds, pfx, providermask, records);

  return (int)records->n_recs;
}

int ipmeta_ds_patricia_lookup_addr(ipmeta_ds_t *ds, int family, void *addrp,
    uint32_t provmask, ipmeta_record_set_t *found)
{
  patricia_tree_t *trie = STATE(ds)->trie[family_to_idx(family)];
  patricia_node_t *node = NULL;
  prefix_t pfx;
  uint32_t foundsofar = 0;

  pfx.family = family;
  pfx.ref_count = 0;
  memcpy(&pfx.add, addrp, family_size(family));
  pfx.bitlen = family_size(family) * 8;

  if ((node = patricia_search_best2(trie, &pfx, 1)) == NULL) {
    return 0;
  }

  if (extract_records_from_pnode(node, provmask, &foundsofar, found, 1, 32) < 0) {
    return -1;
  }

  return (int)found->n_recs;
}
