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
  pfx.bitlen = 32;
  pfx.ref_count = 0;
  pfx.add.sin.s_addr = addr;

  /*
  if((node = patricia_search_best2(trie, &pfx, 1)) == NULL)
    {
      return NULL;
    }
  else
    {
      return node->data;
    }

  return NULL;
  */

  ipmeta_record_set_clear_records(records);
  // Temp return just the 1 record for the IP
  if((node = patricia_search_best2(trie, &pfx, 1)) != NULL)
    {
      ipmeta_record_set_add_record(records, node->data, 1);
    }

  return records->n_recs;
}
