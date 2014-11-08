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

#include "libipmeta_int.h"
#include "ipmeta_ds_intervaltree.h"

#include "interval_tree.h" 

#define DS_NAME "intervaltree"

#define STATE(ds)				\
  (IPMETA_DS_STATE(intervaltree, ds))

static ipmeta_ds_t ipmeta_ds_intervaltree = {
  IPMETA_DS_INTERVALTREE,
  DS_NAME,
  IPMETA_DS_GENERATE_PTRS(intervaltree)
  NULL
};

khint_t _kh_interval3_record_hash_func (ipmeta_record_t *rec) {
  khint32_t h = rec->id;
  return __ac_Wang_hash(h);
}

int _kh_interval3_record_hash_equal (ipmeta_record_t * rec1, ipmeta_record_t * rec2) {
  if (rec1->id == rec2->id) {
    return 1;
  }
  return 0;
}

KHASH_INIT(recordu32, ipmeta_record_t *, uint32_t, 1, _kh_interval3_record_hash_func, _kh_interval3_record_hash_equal)

typedef struct ipmeta_ds_intervaltree_state
{
  interval_tree_t *tree;

  /** Temporary hash to count #ips of unique records */
  /** Key: record pointers, Values: #ips */
  khash_t(recordu32) *record_cnt;
} ipmeta_ds_intervaltree_state_t;

ipmeta_ds_t *ipmeta_ds_intervaltree_alloc()
{
  return &ipmeta_ds_intervaltree;
}

int ipmeta_ds_intervaltree_init(ipmeta_ds_t *ds)
{
  /* the ds structure is malloc'd already, we just need to init the state */

  assert(STATE(ds) == NULL);

  if((ds->state = malloc(sizeof(ipmeta_ds_intervaltree_state_t)))
     == NULL)
    {
      ipmeta_log(__func__, "could not malloc ipmeta ds interval tree");
      return -1;
    }

  if (( STATE(ds)->tree = interval_tree_init() ) == NULL )
    {
      ipmeta_log(__func__, "could not malloc interval tree");
      return -1;  
    }

  STATE(ds)->record_cnt = kh_init(recordu32);

  return 0;
}

void ipmeta_ds_intervaltree_free(ipmeta_ds_t *ds)
{
  if(ds == NULL)
    {
      return;
    }

  if(STATE(ds) != NULL)
    {
      if(STATE(ds)->tree != NULL)
        {
          interval_tree_free(STATE(ds)->tree);
          STATE(ds)->tree = NULL;
        }

      if(STATE(ds)->record_cnt != NULL)
        {
          kh_destroy(recordu32, STATE(ds)->record_cnt);
          STATE(ds)->record_cnt = NULL;
        }

      free(STATE(ds));
      ds->state = NULL;
    }

  free(ds);

  return;
}

int ipmeta_ds_intervaltree_add_prefix(ipmeta_ds_t *ds,
				  uint32_t addr, uint8_t mask,
				  ipmeta_record_t *record)
{

  assert(ds != NULL && ds->state != NULL);
  interval_tree_t *tree = STATE(ds)->tree;
  assert(tree != NULL);

  interval_t interval;

  interval.start = ntohl(addr);
  interval.end = interval.start + pow(2,32-mask) - 1;
  interval.data = record;

  if (interval_tree_add_interval(tree, &interval) == -1)
    {
      ipmeta_log(__func__, "could not malloc to insert prefix in interval tree");
      return -1;
    }

  return 0;
}

int ipmeta_ds_intervaltree_lookup_records(ipmeta_ds_t *ds,
                                uint32_t addr, uint8_t mask,
                                ipmeta_record_set_t *records)
{
  assert(ds != NULL && ds->state != NULL);
  interval_tree_t *tree = STATE(ds)->tree;
  assert(tree != NULL);

  ipmeta_record_set_clear_records(records);

  interval_t interval;

  interval.start = ntohl(addr);
  interval.end = interval.start + pow(2,32-mask) - 1;
  interval.data = NULL;

  int num_matches;

  interval_t** matches = getOverlapping(tree, &interval, &num_matches);

  //printf ("Matches: %d\n", num_matches);

  // Clear record count hash
  khash_t(recordu32) *rec_h = STATE(ds)->record_cnt;
  kh_clear(recordu32, rec_h);

  // Map: Index matches by record ID
  khiter_t rec_k;
  int new_key;
  uint32_t ov_start;
  uint32_t ov_end;
  for (int i=0;i<num_matches;i++)
    {
      //printf("%u-%u\n", matches[i]->start, matches[i]->end);

      rec_k = kh_put(recordu32, rec_h, (ipmeta_record_t *)matches[i]->data, &new_key);
      if (new_key)
        {
          kh_value(rec_h, rec_k) = 0;
        } 

      // Calculate number of (overlapping) IPs in record match
      ov_start = (interval.start>matches[i]->start)?interval.start:matches[i]->start;
      ov_end = (interval.end<matches[i]->end)?interval.end:matches[i]->end;
      kh_value(rec_h, rec_k)+=ov_end - ov_start + 1;
    }

  // Reduce: unique records
  for (khiter_t rec_k = kh_begin(rec_h); rec_k != kh_end(rec_h); rec_k++)
    {
      if (kh_exist(rec_h, rec_k))
        {
          ipmeta_record_set_add_record(records, kh_key(rec_h, rec_k), kh_value(rec_h, rec_k));
        }
    }

  return records->n_recs;
}
