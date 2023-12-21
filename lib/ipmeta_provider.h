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
#define IPMETA_PROVIDER_STATE(type, provider)                                  \
  ((ipmeta_provider_##type##_state_t *)(provider)->state)

/** Convenience macro that defines all the function prototypes for the ipmeta
 * provider API
 */
#define IPMETA_PROVIDER_GENERATE_PROTOS(provname)                              \
  ipmeta_provider_t *ipmeta_provider_##provname##_alloc(void);                 \
  int ipmeta_provider_##provname##_init(ipmeta_provider_t *ds, int argc,       \
                                        char **argv);                          \
  void ipmeta_provider_##provname##_free(ipmeta_provider_t *ds);               \
  int ipmeta_provider_##provname##_lookup_pfx(ipmeta_provider_t *provider,     \
      int family, void *addrp, uint8_t pfxlen, ipmeta_record_set_t *records);  \
  int ipmeta_provider_##provname##_lookup_addr(ipmeta_provider_t *provider,    \
      int family, void *addrp, ipmeta_record_set_t *found);                    \
  void ipmeta_provider_##provname##_free_record(ipmeta_record_t *record);

/** Convenience macro that defines all the function pointers for the ipmeta
 * provider API
 */
#define IPMETA_PROVIDER_GENERATE_PTRS(provname)                                \
  ipmeta_provider_##provname##_init, ipmeta_provider_##provname##_free,        \
    ipmeta_provider_##provname##_lookup_pfx,                                   \
    ipmeta_provider_##provname##_lookup_addr,                                  \
    ipmeta_provider_##provname##_free_record, 0, NULL, NULL, NULL

/** Structure which represents a metadata provider */
struct ipmeta_provider {
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
  int (*init)(struct ipmeta_provider *provider, int argc, char **argv);

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
   * @param family      The address family (AF_INET or AF_INET6)
   * @param addrp       Pointer to a struct in_addr or in6_addr containing the
   *                    address to look up
   * @param pfxlen      The prefix length (0-32 or 0-128)
   * @param records     Pointer to a record set to use for matches
   * @return            The number of (matched) records in the result set
   *
   * For the most part providers will simply pass this call back to the provider
   * manager lookup helper function to extract the appropriate record from the
   * datastructure, but this allows providers to do some arbitrary
   * pre/post-processing.
   */
  int (*lookup)(struct ipmeta_provider *provider, int family, void *addrp,
                uint8_t pfxlen, ipmeta_record_set_t *records);

  /** Look up the given single IP address using the given provider
   *
   * @param provider      The provider to perform the lookup with
   * @param family        The address family (AF_INET or AF_INET6)
   * @param addrp         Pointer to a struct in_addr or in6_addr containing the
   *                      address to look up
   * @param found         A pointer to a record set to store the matching
   *                       record in
   * @return A pointer to the matching record, or NULL if there were no matches
   */
  int (*lookup_addr)(ipmeta_provider_t *provider, int family, void *addrp,
                     ipmeta_record_set_t *found);

  /** Free a record that was generated by this particular provider.
   *
   * @param record          The record to be destroyed
   *
   * Some providers try to conserve memory by sharing repeating strings
   * (i.e. region names, timezones) across multiple records. In these cases,
   * the standard ipmeta_free_record() method is not suitable as it will
   * then perform multiple free()s on the same string. So now, we allow
   * providers to provide a custom method for freeing records that can
   * account for this situation.
   */
  void (*free_record)(ipmeta_record_t *record);
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
  khash_t(ipmeta_rechash) * all_records;

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
 * @param provider      A pointer to the metadata provider
 * @return the provider object created, NULL if an error occurred
 */
int ipmeta_provider_init(ipmeta_t *ipmeta, ipmeta_provider_t *provider,
                         int argc, char **argv);

/** Free the given provider object
 *
 * @param ipmeta          The ipmeta object to remove the provider from
 * @param provider        The provider object to free
 */
void ipmeta_provider_free(ipmeta_t *ipmeta, ipmeta_provider_t *provider);

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
void ipmeta_provider_register_state(ipmeta_provider_t *provider, void *state);

/** Free the state for a provider
 *
 * @param provider    The provider to free state for
 */
void ipmeta_provider_free_state(ipmeta_provider_t *provider);

/** Insert a metadata record with the given record->id
 *
 * @param provider      The metadata provider to associate the record with
 * @param record        Pointer to the record to be inserted
 * @return pointer to the record
 *
 * The record->id must be set before this function is called.
 * This function will set record->source and insert the record into the
 * provider's lookup table.
 *
 * The record will be free'd when ipmeta_free_provider() is called, including
 * *ALL* char pointers in the record.
 */
ipmeta_record_t *ipmeta_provider_insert_record(ipmeta_provider_t *provider,
                                               ipmeta_record_t *record);

/** Allocate an empty metadata record for the given id
 *
 * @param provider      The metadata provider to associate the record with
 * @param id            The id to use to inialize the record
 * @return the new metadata record, NULL if an error occurred
 *
 * Allocate an empty record, set record->id = id, and call
 * ipmeta_provider_insert_record(provider, record).
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
 * @param provider      The provider to register the mapping with
 * @param family        The address family (AF_INET or AF_INET6)
 * @param addrp         Pointer to a struct in_addr or in6_addr containing the
 *                      address to register
 * @param pfxlen        The prefix length
 * @param record        The record to associate with the prefix
 * @return 0 if the prefix is successfully associated with the prefix, -1 if an
 * error occurs
 */
int ipmeta_provider_associate_record(ipmeta_provider_t *provider, int family,
    void *addrp, uint8_t pfxlen, ipmeta_record_t *record);

/** Retrieves the records that correspond to the given prefix from the
 * associated datastructure.
 *
 * @param provider      The provider to perform the lookup with
 * @param family        The address family (AF_INET or AF_INET6)
 * @param addrp         Pointer to a struct in_addr or in6_addr containing the
 *                      address to look up
 * @param pfxlen        The prefix length
 * @param records       A pointer to the record set structure where to return
 * the matches
 * @return              The number of (matched) records in the result set
 */
int ipmeta_provider_lookup_pfx(ipmeta_provider_t *provider, int family,
    void *addrp, uint8_t pfxlen, ipmeta_record_set_t *records);

/** Retrieves the one record that corresponds to the given single IP address
 * using the given provider
 *
 * @param provider      The provider to perform the lookup with
 * @param family        The address family (AF_INET or AF_INET6)
 * @param addrp         Pointer to a struct in_addr or in6_addr containing the
 *                      address to look up
 * @param found         A pointer to a record set to store the found record in
 * @return The number of successful matches (typically 0 or 1), or -1 if an
 *         error occurs.
 */
int ipmeta_provider_lookup_addr(ipmeta_provider_t *provider, int family,
    void *addrp, ipmeta_record_set_t *found);

/** Deallocate all members of a record, but don't free the record itself.
 *
 * @param record   Pointer to the record to reset to its initial state.
 */
void ipmeta_clean_record(ipmeta_record_t *record);

/** Dealloacate a record.
 *
 * @param record   Pointer to the record to free.
 */
void ipmeta_free_record(ipmeta_record_t *record);

/** }@ */

#endif /* __IPMETA_PROVIDER_H */
