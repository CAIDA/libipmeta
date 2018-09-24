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
#include <arpa/inet.h>

#include "interval_tree.h"

#include "libipmeta_int.h"
#include "ipmeta_ds_intervaltree.h"

#define DS_NAME "intervaltree"

#define STATE(ds)				\
  (IPMETA_DS_STATE(intervaltree, ds))

static ipmeta_ds_t ipmeta_ds_intervaltree = {
  IPMETA_DS_INTERVALTREE,
  DS_NAME,
  IPMETA_DS_GENERATE_PTRS(intervaltree)
  NULL
};

typedef struct ipmeta_ds_intervaltree_state
{
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
  STATE(ds)->providerid = 0;

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
  interval.end = interval.start + (1 << (32-mask)) - 1;
  interval.data = record;

  if (STATE(ds)->providerid == 0)
    {
      STATE(ds)->providerid = record->source;
    }
  else if (STATE(ds)->providerid != record->source)
    {
      ipmeta_log(__func__, "interval tree does not support storing records from multiple providers");
      ipmeta_log(__func__, "please use a separate ipmeta instance for each provider");
      return -1;
    }

  if (interval_tree_add_interval(tree, &interval) == -1)
    {
      ipmeta_log(__func__, "could not malloc to insert prefix in interval tree");
      return -1;
    }

  return 0;
}

int ipmeta_ds_intervaltree_lookup_records(ipmeta_ds_t *ds,
                                uint32_t addr, uint8_t mask,
                                uint32_t providermask,
                                ipmeta_record_set_t *records)
{
  interval_tree_t *tree = STATE(ds)->tree;
  interval_t interval;
  int num_matches = 0;
  interval_t** matches = NULL;
  uint32_t ov_start;
  uint32_t ov_end;
  int i;

  interval.start = ntohl(addr);
  interval.end = interval.start + (1 << (32-mask)) - 1;
  interval.data = NULL;

  matches = getOverlapping(tree, &interval, &num_matches);

  for(i=0; i<num_matches; i++)
    {
      /* Calculate number of (overlapping) IPs in record match */
      ov_start = (interval.start>matches[i]->start)?
        interval.start:matches[i]->start;

      ov_end = (interval.end<matches[i]->end)?interval.end:matches[i]->end;

      if(ipmeta_record_set_add_record(records,
                                      (ipmeta_record_t *)matches[i]->data,
                                      ov_end - ov_start + 1) != 0)
        {
          return -1;
        }
    }

  return records->n_recs;
}

int ipmeta_ds_intervaltree_lookup_record_single(ipmeta_ds_t *ds,
                                                uint32_t addr,
                                                uint32_t providermask,
                                                ipmeta_record_set_t *found)
{
  interval_tree_t *tree = STATE(ds)->tree;
  interval_t interval;
  int num_matches = 0, i;
  interval_t** matches = NULL;

  interval.start = ntohl(addr);
  interval.end = interval.start;
  interval.data = NULL;

  matches = getOverlapping(tree, &interval, &num_matches);

  /* we only have a single IP! */
  if(num_matches == 0)
    {
      return 0;
    }
  for (i = 0; i < num_matches; i++)
    {
      if(ipmeta_record_set_add_record(found,
          (ipmeta_record_t *)(matches[i]->data), 1) != 0)
        {
           return -1;
        }
    }

  return found->n_recs;
}
