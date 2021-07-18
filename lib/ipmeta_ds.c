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

#include <assert.h>

#include "ipmeta_ds_intervaltree.h"
#include "ipmeta_ds_bigarray.h"
#include "ipmeta_ds_patricia.h"
#include "utils.h"

#include "ipmeta_ds.h"
#include "ipmeta_provider.h"

/** Convenience typedef for the alloc function type */
typedef ipmeta_ds_t *(*ds_alloc_func_t)(void);

/** Array of datastructure allocation functions.
 *
 * @note the indexes of these functions must exactly match the ID in
 * ipmeta_ds_id_t. The element at index 0 MUST be NULL.
 */
static const ds_alloc_func_t ds_alloc_functions[] = {
  NULL, ipmeta_ds_patricia_alloc, ipmeta_ds_bigarray_alloc,
  ipmeta_ds_intervaltree_alloc};

int ipmeta_ds_init(struct ipmeta_ds **ds, ipmeta_ds_id_t ds_id)
{
  assert(ARR_CNT(ds_alloc_functions) == IPMETA_DS_MAX + 1);
  if (ds_id < 1 || ds_id > IPMETA_DS_MAX) {
    ipmeta_log(__func__, "ds_id %d out of range [%d,%d]",
        ds_id, 1, IPMETA_DS_MAX);
    return -1;
  }

  /* malloc some room for the datastructure */
  if ((*ds = malloc_zero(sizeof(ipmeta_ds_t))) == NULL) {
    ipmeta_log(__func__, "could not malloc ipmeta_ds_t");
    return -1;
  }

  /* allocate the datastructure */
  memcpy(*ds, ds_alloc_functions[ds_id](), sizeof(ipmeta_ds_t));

  assert(*ds != NULL);

  /** init the ds */
  if ((*ds)->init(*ds) != 0) {
    free(*ds);
    return -1;
  }

  return 0;
}

ipmeta_ds_id_t ipmeta_ds_name_to_id(const char *name)
{
  ipmeta_ds_t *tmp_ds;

  /* call each of the ds alloc functions and look for a name that matches the
     one we were given */

  for (unsigned i = 1; i < ARR_CNT(ds_alloc_functions); i++) {
    tmp_ds = ds_alloc_functions[i]();
    assert(tmp_ds != NULL);

    if (strcmp(tmp_ds->name, name) == 0) {
      return i;
    }
  }

  /* no matching datastructure */
  return IPMETA_DS_NONE;
}

const char **ipmeta_ds_get_all()
{
  const char **names;
  ipmeta_ds_t *tmp_ds;

  if ((names = malloc(sizeof(char *) * IPMETA_DS_MAX)) == NULL) {
    return NULL;
  }

  for (unsigned i = 1; i < ARR_CNT(ds_alloc_functions); i++) {
    tmp_ds = ds_alloc_functions[i]();
    assert(tmp_ds != NULL);

    names[i - 1] = tmp_ds->name;
  }

  return names;
}
