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


#ifndef __IPMETA_PROVIDER_H
#define __IPMETA_PROVIDER_H

#include <inttypes.h>

#include "libipmeta.h"

/** @file
 *
 * @brief Header file that exposes the ipmeta provider API
 *
 * @author Alistair King
 *
 */

/** Convenience macro to allow provider implementations to retrieve their state
 *  object
 */
#define IPMETA_PROVIDER_STATE(type, provider) \
  ((ipmeta_provider_##type##_state_t*)(provider)->state)

/** Convenience macro that defines all the function prototypes for the ipmeta
 * provider API
 */
#define IPMETA_PROVIDER_GENERATE_PROTOS(provname)			\
  ipmeta_provider_t * ipmeta_provider_##provname##_alloc();		\
  int ipmeta_provider_##provname##_init(ipmeta_provider_t *ds,		\
					int argc, char **argv);         \
  void ipmeta_provider_##provname##_free(ipmeta_provider_t *ds);	\
  int ipmeta_provider_##provname##_lookup(ipmeta_provider_t *provider,  \
                                          uint32_t addr, uint8_t mask,  \
                                          ipmeta_record_set_t *records); \
  int ipmeta_provider_##provname##_lookup_single(          \
                                                 ipmeta_provider_t *provider, \
                                                 uint32_t addr, \
                                                 ipmeta_record_set_t *found);

/** Convenience macro that defines all the function pointers for the ipmeta
 * provider API
 */
#define IPMETA_PROVIDER_GENERATE_PTRS(provname)	\
  ipmeta_provider_##provname##_init,		\
    ipmeta_provider_##provname##_free,		\
    ipmeta_provider_##provname##_lookup,	\
    ipmeta_provider_##provname##_lookup_single,	\
    0, NULL, NULL, NULL

/** Structure which represents a metadata provider */
struct ipmeta_provider
{
  /**
   * @name Provider information fields
   *
   * These fields are always filled, even if a provider is not enabled.
   *
   * @{ */

  /** The ID of the provider */
  ipmeta_provider_id_t id;

  /** The name of the provider */
  const char *name;

  /** }@ */

  /**
   * @name Provider function pointers
   *
   * These pointers are always filled, even if a provider is not enabled.
   * Until the provider is enabled, only the init function can be called.
   *
   * @{ */

  /** Initialize and enable this provider
   *
   * @param provider    The provider object to allocate
   * @param argc        The number of tokens in argv
   * @param argv        An array of strings parsed from the command line
   * @return 0 if the provider is successfully initialized, -1 otherwise
   *
   * @note the most common reason for returning -1 will likely be incorrect
   * command line arguments.
   *
   * @warning the strings contained in argv will be free'd once this function
   * returns. Ensure you make appropriate copies as needed.
   */
  int (*init)(struct ipmeta_provider *provider, int argc, char ** argv);

  /** Shutdown and free provider-specific state for this provider
   *
   * @param provider    The provider object to free
   *
   * @note providers should *only* free provider-specific state. All other state
   * will be free'd for them by the provider manager.
   */
  void (*free)(struct ipmeta_provider *provider);

  /** Perform an IP metadata lookup using this provider
   *
   * @param provider    The provider object to perform the lookup with
   * @param addr          The IPv4 network address part to lookup
   *                       (network byte ordering)
   * @param mask        The IPv4 network mask defining the prefix length (0-32)
   * @param records     Pointer to a record set to use for matches
   * @return            The number of (matched) records in the result set
   *
   * For the most part providers will simply pass this call back to the provider
   * manager lookup helper function to extract the appropriate record from the
   * datastructure, but this allows providers to do some arbitrary
   * pre/post-processing.
   */
  int (*lookup)(struct ipmeta_provider *provider, uint32_t addr, uint8_t mask,
                ipmeta_record_set_t *records);

  /** Look up the given single IP address using the given provider
   *
   * @param provider      The provider to perform the lookup with
   * @param addr          The address to retrieve the record for
   *                       (network byte ordering)
   * @param found         A pointer to a record set to store the matching
   *                       record in
   * @return A pointer to the matching record, or NULL if there were no matches
   */
  int (*lookup_single)(ipmeta_provider_t *provider, uint32_t addr,
                ipmeta_record_set_t *found);

  /** }@ */

  /**
   * @name Provider state fields
   *
   * These fields are only set if the provider is enabled (and initialized)
   * @note These fields should *not* be directly manipulated by
   * providers. Instead they should use accessor functions provided by the
   * provider manager.
   *
   * @{ */

  int enabled;

  /** A hash of id => record for all allocated records of this provider */
  khash_t(ipmeta_rechash) *all_records;

  /** The datastructure that will be used to perform IP => record lookups */
  struct ipmeta_ds *ds;

  /** An opaque pointer to provider-specific state if needed by the provider */
  void *state;

  /** }@ */
};

/**
 * @name Provider setup functions
 *
 * These functions are to be used solely by the ipmeta framework initializing
 * and freeing provider plugins.
 *
 * @{ */

/** Allocate all provider objects
 *
 * @param ipmeta        The ipmeta object to allocate providers for
 * @return 0 if all providers were successfully allocated, -1 otherwise
 */
int ipmeta_provider_alloc_all(ipmeta_t *ipmeta);

/** Initialize a provider object
 *
 * @param ipmeta        The ipmeta object to initialize the provider for
 * @param provider_id   The unique ID of the metadata provider
 * @param set_default   Set this provider as the default
 * @return the provider object created, NULL if an error occurred
 *
 * @note Default provider status overrides the requests of previous
 * plugins. Thus, the order in which users request the plugins to be run in can
 * have an effect on plugins which make use of the default provider
 * (e.g. corsaro_report).
 */
int ipmeta_provider_init(ipmeta_t *ipmeta,
			 ipmeta_provider_t *provider,
			 int argc, char **argv,
			 ipmeta_provider_default_t set_default);

/** Free the given provider object
 *
 * @param ipmeta          The ipmeta object to remove the provider from
 * @param provider        The provider object to free
 *
 * @note if this provider was the default, there will be *no* default provider set
 * after this function returns
 */
void ipmeta_provider_free(ipmeta_t *ipmeta,
			  ipmeta_provider_t *provider);

/** }@ */

/**
 * @name Provider convenience functions
 *
 * These functions are to be used solely by provider implementations to access
 * the record hash and the datastructure.
 *
 * @{ */

/** Register the state for a provider
 *
 * @param provider      The provider to register state for
 * @param state         A pointer to the state object to register
 */
void ipmeta_provider_register_state(ipmeta_provider_t *provider,
				    void *state);

/** Free the state for a provider
 *
 * @param provider    The provider to free state for
 */
void ipmeta_provider_free_state(ipmeta_provider_t *provider);

/** Allocate an empty metadata record for the given id
 *
 * @param provider      The metadata provider to associate the record with
 * @param id            The id to use to inialize the record
 * @return the new metadata record, NULL if an error occurred
 *
 * @note Most metadata providers will not want to allocate a record on the fly
 * for every lookup, instead they will allocate all needed records at init time,
 * and then use ipmeta_provider_add_record to add the appropriate record to the
 * results structure. These records are stored in the provider, and free'd when
 * ipmeta_free_provider is called. Also *ALL* char pointers in this structure
 * will be free'd.
 */
ipmeta_record_t *ipmeta_provider_init_record(ipmeta_provider_t *provider,
					     uint32_t id);

/** Get the metadata record for the given id
 *
 * @param provider      The metadata provider to retrieve the record from
 * @param id            The id of the record to retrieve
 * @return the corresponding metadata record, NULL if an error occurred
 */
ipmeta_record_t *ipmeta_provider_get_record(ipmeta_provider_t *provider,
					    uint32_t id);


/** Register a new prefix to record mapping for the given provider
 *
 * @param ipmeta        The ipmeta object associated with the provider
 * @param provider      The provider to register the mapping with
 * @param addr          The network byte-ordered component of the prefix
 * @param mask          The mask component of the prefix
 * @param record        The record to associate with the prefix
 * @return 0 if the prefix is successfully associated with the prefix, -1 if an
 * error occurs
 */
int ipmeta_provider_associate_record(ipmeta_provider_t *provider,
				     uint32_t addr,
				     uint8_t mask,
				     ipmeta_record_t *record);

/** Retrieves the records that correspond to the given prefix from the
 * associated datastructure.
 *
 * @param provider      The provider to perform the lookup with
 * @param addr          The network address to retrieve the records for
 *                       (network byte ordering)
 * @param mask          The CIDR network mask component of the prefix
 * @param records       A pointer to the record set structure where to return the matches
 * @return              The number of (matched) records in the result set
 */
int ipmeta_provider_lookup_records(ipmeta_provider_t *provider,
					       uint32_t addr, uint8_t mask,
                 ipmeta_record_set_t *records);

/** Retrieves the records that correspond to the given prefix from the
 * associated datastructure.
 *
 * @param provider    The provider object to perform the lookup with
 * @param addr        The IPv4 network address part to retrieve records for
 * @param mask        The IPv4 network mask defining the prefix length (0-32)
 * @param records     Pointer to a record set to use for matches
 * @return            The number of (matched) records in the result set
 *
 */
int ipmeta_provider_lookup_records(ipmeta_provider_t *provider,
                                   uint32_t addr, uint8_t mask,
                                   ipmeta_record_set_t *records);

/** Retrieves the one record that corresponds to the given single IP address
 * using the given provider
 *
 * @param provider      The provider to perform the lookup with
 * @param addr          The address to retrieve the record for
 *                       (network byte ordering)
 * @param found         A pointer to a record set to store the found record in
 * @return The number of successful matches (typically 0 or 1), or -1 if an
 *         error occurs.
 */
int ipmeta_provider_lookup_record_single(ipmeta_provider_t *provider,
                                         uint32_t addr,
                                         ipmeta_record_set_t *found);

 /** }@ */

#endif /* __IPMETA_PROVIDER_H */
