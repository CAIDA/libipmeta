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

#include "utils.h"
#include "patricia.h"

#include "libipmeta_int.h"
#include "ipmeta_ds_patricia.h"

#define DS_NAME "patricia"

#define STATE(ds) (IPMETA_DS_STATE(patricia, ds))

static ipmeta_ds_t ipmeta_ds_patricia = {
  IPMETA_DS_PATRICIA, DS_NAME, IPMETA_DS_GENERATE_PTRS(patricia) NULL};

typedef struct ipmeta_ds_patricia_state {
  patricia_tree_t *trie;

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

  /** @todo make support IPv6 */
  STATE(ds)->trie = New_Patricia(32);
  assert(STATE(ds)->trie != NULL);

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
    if (STATE(ds)->trie != NULL) {
      Destroy_Patricia(STATE(ds)->trie, free_prefix);
      STATE(ds)->trie = NULL;
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
  patricia_tree_t *trie = STATE(ds)->trie;
  ipmeta_record_t **recarray = NULL;
  assert(trie != NULL);
  if (family != AF_INET) {
    ipmeta_log(__func__, "patricia datastructure only supports IPv4");
    return -1;
  }
  uint32_t addr = *(uint32_t *)addrp;

  prefix_t trie_pfx;
  /** @todo make support IPv6 */
  trie_pfx.family = AF_INET;
  trie_pfx.ref_count = 0;
  patricia_node_t *trie_node;

  trie_pfx.bitlen = pfxlen;
  trie_pfx.add.sin.s_addr = addr;
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
      uint64_t num_ips = (1 << (32 - masklen));
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

static int descend_ptree(ipmeta_ds_t *ds, prefix_t pfx, uint32_t provmask,
                         uint32_t foundsofar, ipmeta_record_set_t *records)
{
  prefix_t subpfx;
  patricia_node_t *node = NULL;
  patricia_tree_t *trie = STATE(ds)->trie;
  uint32_t sub_foundsofar;

  subpfx.family = AF_INET;
  subpfx.ref_count = 0;
  subpfx.bitlen = pfx.bitlen + 1;

  // try the two CIDR halves
  for (int i = 0; i < 2; i++) {
    subpfx.add.sin.s_addr = (i == 0) ? pfx.add.sin.s_addr :
      pfx.add.sin.s_addr | htonl(1 << (32 - subpfx.bitlen));

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
    if (sub_foundsofar != provmask && subpfx.bitlen < 32) {
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
  patricia_tree_t *trie = STATE(ds)->trie;
  patricia_node_t *node = NULL;
  uint32_t foundsofar = 0;

  if (foundsofar == provmask) {
    return 0;
  }

  node = patricia_search_best2(trie, &pfx, 1);

  if (node) {
    if (extract_records_from_pnode(node, provmask, &foundsofar, records, 1,
          pfx.bitlen) < 0) {
      ipmeta_log(__func__, "error while extracting records for prefix");
      return -1;
    }
  }

  if (foundsofar != provmask && pfx.bitlen < 32) {
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
  if (family != AF_INET) {
    ipmeta_log(__func__, "patricia datastructure only supports IPv4");
    return -1;
  }
  uint32_t addr = *(uint32_t *)addrp;
  prefix_t pfx;

  /** @todo make support IPv6 */
  pfx.family = AF_INET;
  pfx.ref_count = 0;
  pfx.add.sin.s_addr = addr;
  pfx.bitlen = pfxlen;

  _patricia_prefix_lookup(ds, pfx, providermask, records);

  return records->n_recs;
}

int ipmeta_ds_patricia_lookup_addr(ipmeta_ds_t *ds, int family, void *addrp,
    uint32_t provmask, ipmeta_record_set_t *found)
{
  if (family != AF_INET) {
    ipmeta_log(__func__, "patricia datastructure only supports IPv4");
    return -1;
  }
  uint32_t addr = *(uint32_t *)addrp;
  patricia_tree_t *trie = STATE(ds)->trie;
  patricia_node_t *node = NULL;
  prefix_t pfx;
  uint32_t foundsofar = 0;

  /** @todo make support IPv6 */
  pfx.family = AF_INET;
  pfx.ref_count = 0;
  pfx.add.sin.s_addr = addr;
  pfx.bitlen = 32;

  if ((node = patricia_search_best2(trie, &pfx, 1)) == NULL) {
    return 0;
  }

  if (extract_records_from_pnode(node, provmask, &foundsofar, found, 1, 32) < 0) {
    return -1;
  }

  return found->n_recs;
}
