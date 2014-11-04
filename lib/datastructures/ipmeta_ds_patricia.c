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
 #include <math.h>

#include "patricia.h"

#include "libipmeta_int.h"
#include "ipmeta_ds_patricia.h"

#define DS_NAME "patricia"

#define STATE(ds)				\
  (IPMETA_DS_STATE(patricia, ds))

static ipmeta_ds_t ipmeta_ds_patricia = {
  IPMETA_DS_PATRICIA,
  DS_NAME,
  IPMETA_DS_GENERATE_PTRS(patricia)
  NULL
};


khint_t _kh_patricia_record_hash_func (ipmeta_record_t *rec) {
  khint32_t h = rec->id;
  return __ac_Wang_hash(h);
}

int _kh_patricia_record_hash_equal (ipmeta_record_t * rec1, ipmeta_record_t * rec2) {
  if (rec1->id == rec2->id) {
    return 1;
  }
  return 0;
}

KHASH_INIT(recordu32, ipmeta_record_t *, uint32_t, 1, _kh_patricia_record_hash_func, _kh_patricia_record_hash_equal)


typedef struct ipmeta_ds_patricia_state
{
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

  if((ds->state = malloc(sizeof(ipmeta_ds_patricia_state_t)))
     == NULL)
    {
      ipmeta_log(__func__, "could not malloc patricia state");
      return -1;
    }

  /** @todo make support IPv6 */
  STATE(ds)->trie = New_Patricia(32);
  assert(STATE(ds)->trie != NULL);

  return 0;
}

void ipmeta_ds_patricia_free(ipmeta_ds_t *ds)
{
  if(ds == NULL)
    {
      return;
    }

  if(STATE(ds) != NULL)
    {
      if(STATE(ds)->trie != NULL)
	{
	  Destroy_Patricia(STATE(ds)->trie, NULL);
	  STATE(ds)->trie = NULL;
	}
      free(STATE(ds));
      ds->state = NULL;
    }

  free(ds);

  return;
}

int ipmeta_ds_patricia_add_prefix(ipmeta_ds_t *ds,
				  uint32_t addr, uint8_t mask,
				  ipmeta_record_t *record)
{
  assert(ds != NULL && ds->state != NULL);
  patricia_tree_t *trie = STATE(ds)->trie;
  assert(trie != NULL);

  prefix_t trie_pfx;
  /** @todo make support IPv6 */
  trie_pfx.family = AF_INET;
  trie_pfx.ref_count = 0;
  patricia_node_t *trie_node;

  trie_pfx.bitlen = mask;
  trie_pfx.add.sin.s_addr = addr;
  if((trie_node = patricia_lookup(trie, &trie_pfx)) == NULL)
    {
      ipmeta_log(__func__, "failed to insert prefix in trie");
      return -1;
    }
  trie_node->data = record;

  return 0;
}

void _patricia_prefix_lookup(ipmeta_ds_t *ds, khash_t(recordu32) *rec_h, prefix_t pfx)
{
  patricia_tree_t *trie = STATE(ds)->trie;
  patricia_node_t *node = NULL;

  if((node = patricia_search_best2(trie, &pfx, 1)) != NULL)
    {
      // Found match
      int new_key;
      khiter_t rec_k = kh_put(recordu32, rec_h, node->data, &new_key);
      if (new_key)
        {
          kh_value(rec_h, rec_k) = 0;
        } 
      kh_value(rec_h, rec_k)+=pow(2,32-pfx.bitlen);
    }
  else if (pfx.bitlen<32)
    {
      // Recursive lookup down the CIDR tree
      prefix_t subpfx;
      subpfx.family = AF_INET;
      subpfx.ref_count = 0;

      subpfx.add.sin.s_addr = pfx.add.sin.s_addr;
      subpfx.bitlen = pfx.bitlen+1;

      // 1st CIDR half
      _patricia_prefix_lookup(ds, rec_h, subpfx);

      // 2nd CIDR half
      subpfx.add.sin.s_addr = htonl(ntohl(subpfx.add.sin.s_addr) + pow(2,32-subpfx.bitlen));
      _patricia_prefix_lookup(ds, rec_h, subpfx);
    }
} 

int ipmeta_ds_patricia_lookup_records(ipmeta_ds_t *ds,
						  uint32_t addr, uint8_t mask,
              ipmeta_record_set_t *records)
{
  assert(ds != NULL && ds->state != NULL);
  patricia_tree_t *trie = STATE(ds)->trie;
  assert(trie != NULL);

  patricia_node_t *node = NULL;
  prefix_t pfx;
  /** @todo make support IPv6 */
  pfx.family = AF_INET;
  pfx.ref_count = 0;
  pfx.add.sin.s_addr = addr;
  pfx.bitlen = mask;

  ipmeta_record_set_clear_records(records);

  // Optimisation for single IP special case (no hashing required)
  if (mask==32) 
    {
      if((node = patricia_search_best2(trie, &pfx, 1)) != NULL)
        {
          ipmeta_record_set_add_record(records, node->data, pow(2,32-mask));
          return 1;
        }
      return 0;
    }

  // Hash records -  Key: record pointers, Values: ip counter
  khash_t(recordu32) *rec_h = kh_init(recordu32);

  // Map: index by record
  _patricia_prefix_lookup(ds, rec_h, pfx);

  // Reduce: unique records
  for (khiter_t rec_k = kh_begin(rec_h); rec_k != kh_end(rec_h); rec_k++)
    {
      if (kh_exist(rec_h, rec_k))
        {
          ipmeta_record_set_add_record(records, kh_key(rec_h, rec_k), kh_value(rec_h, rec_k));
        }
    }

  kh_destroy(recordu32, rec_h);

  return records->n_recs;
}
