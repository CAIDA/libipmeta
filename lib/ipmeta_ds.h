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

#ifndef __IPMETA_DS_H
#define __IPMETA_DS_H

#include "libipmeta_int.h"

/** @file
 *
 * @brief Header file that exposes the ipmeta datastructure plugin interface
 *
 * @author Alistair King
 *
 */

/** Convenience macro to allow datastructure implementations to retrieve their
 *  state object
 */
#define IPMETA_DS_STATE(type, ds) ((ipmeta_ds_##type##_state_t*)(ds)->state)

/** Convenience macro that defines all the function prototypes for the ipmeta
 * datastructure API
 */
#define IPMETA_DS_GENERATE_PROTOS(datastructure)			\
  ipmeta_ds_t * ipmeta_ds_##datastructure##_alloc();			\
  int ipmeta_ds_##datastructure##_init(ipmeta_ds_t *ds);		\
  void ipmeta_ds_##datastructure##_free(ipmeta_ds_t *ds);		\
  int ipmeta_ds_##datastructure##_add_prefix(ipmeta_ds_t *ds, uint32_t addr, \
				 uint8_t mask, ipmeta_record_t *record); \
  ipmeta_record_t * ipmeta_ds_##datastructure##_lookup_record(ipmeta_ds_t *ds, \
						  uint32_t addr);

/** Convenience macro that defines all the function pointers for the ipmeta
 * datastructure API
 */
#define IPMETA_DS_GENERATE_PTRS(datastructure)	\
  ipmeta_ds_##datastructure##_init,		\
    ipmeta_ds_##datastructure##_free,		\
    ipmeta_ds_##datastructure##_add_prefix,	\
    ipmeta_ds_##datastructure##_lookup_record,

/** A unique identifier for each metadata ds that libipmeta supports.
 *
 * @note When adding a datastructure to this list, there must also be a
 * corresponding entry added to the ds_alloc_functions array in ipmeta_ds.c
 */
typedef enum ipmeta_ds_id
  {
    /** Patricia Trie */
    IPMETA_DS_PATRICIA      = 1,

    /** Big-Array */
    IPMETA_DS_BIGARRAY      = 2,

    /** Highest numbered ds ID */
    IPMETA_DS_MAX          = IPMETA_DS_BIGARRAY,

    /** Default Geolocation data-structure */
    IPMETA_DS_DEFAULT      = IPMETA_DS_PATRICIA,

  } ipmeta_ds_id_t;

/** Structure which represents a metadata datastructure */
struct ipmeta_ds
{
  /** The ID of this datastructure */
  enum ipmeta_ds_id id;

  /** The name of this datastructure */
  char *name;

  /** Pointer to init function */
  int (*init)(struct ipmeta_ds *ds);

  /** Pointer to free function */
  void (*free)(struct ipmeta_ds *ds);

  /** Pointer to add prefix function */
  int (*add_prefix)(struct ipmeta_ds *ds,
		    uint32_t addr, uint8_t mask,
		    struct ipmeta_record *record);

  /** Pointer to lookup record function */
  struct ipmeta_record *(*lookup_record)(struct ipmeta_ds *ds,
				    uint32_t addr);

  /** Pointer to a instance-specific state object */
  void *state;

};

/** Initialize the given datastructure and associate it with the given provider
 *
 * @param provider      pointer to provider instance to associate ds with
 * @param ds_id         id of the datastructure to initialize
 * @return 0 if initialization was successful, -1 otherwise
 */
int ipmeta_ds_init(struct ipmeta_provider *provider, enum ipmeta_ds_id ds_id);

/** Search for a datastructure with the given name and then initialize it
 *
 * @param provider      pointer to provider instance to associate ds with
 * @param name          name of the datastructure to initialize
 * @return 0 if initialization was successful, -1 otherwise
 */
int ipmeta_ds_init_by_name(ipmeta_provider_t *provider, const char *name);

/** Get an array of all available datastructure names
 *
 * @return an array of datastructure names. The array is guaranteed to have
 * length IPMETA_DS_MAX-1
 *
 * @note it is the caller's responsibility to free the returned array
 */
const char **ipmeta_ds_get_all();

#endif /* __IPMETA_DS_H */
