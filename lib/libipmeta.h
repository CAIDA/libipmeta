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


#ifndef __LIBIPMETA_H
#define __LIBIPMETA_H

#include <stdint.h>

/** @file
 *
 * @brief Header file that exposes the public interface of libipmeta.
 *
 * @author Alistair King
 *
 */

/**
 * @name Public Opaque Data Structures
 *
 * @{ */

/** Opaque struct holding ipmeta state */
typedef struct ipmeta ipmeta_t;

/** Opaque struct holding state for a metadata provider */
typedef struct ipmeta_provider ipmeta_provider_t;

/** Opaque struct holding state for a metadata ds */
typedef struct ipmeta_ds ipmeta_ds_t;

/** @} */

/**
 * @name Public Data Structures
 *
 * @{ */

/** Structure which contains an IP meta-data record */
/** @todo use some sort of key-value record so that we don't have to extend
    this structure whenever a new provider is added */
typedef struct ipmeta_record
{
  /** A unique ID for this record (used to join the Blocks and Locations Files)
   *
   * This should be considered unique only within a single provider type
   * i.e. id's may not be unique across different ipmeta_provider_t objects
   */
  uint32_t id;

  /** 2 character string which holds the ISO2 country code */
  char country_code[3];

  /** Continent Code */
  int continent_code;

  /** 2 character string which represents the region the city is in */
  char region[3];

  /** String which contains the city name */
  char *city;

  /** String which contains the postal code
   * @note This cannot be an int as some countries (I'm looking at you, Canada)
   * use characters
   */
  char *post_code;

  /** Latitude of the city */
  double latitude;

  /** Longitude of the city */
  double longitude;

  /** Metro code */
  uint32_t metro_code;

  /** Area code */
  uint32_t area_code;

  /** Connection Speed/Type */
  char *conn_speed;

  /** Array of Autonomous System Numbers */
  uint32_t *asn;

  /** Number of ASNs in the asn array */
  int asn_cnt;

  /** Number of IP addresses that this ASN (or ASN group) 'owns' */
  uint32_t asn_ip_cnt;

  /* -- ADD NEW FIELDS ABOVE HERE -- */

  /** The next record in the list */
  struct ipmeta_record *next;

} ipmeta_record_t;

/** @} */

/**
 * @name Public Enums
 *
 * @{ */

/** Should this provider be set to be the default metadata provider
 * @todo make use of this
 */
typedef enum ipmeta_provider_default
  {
    /** This provider should *not* be the default geolocation result */
    IPMETA_PROVIDER_DEFAULT_NO   = 0,

    /** This provider should be the default geolocation result */
    IPMETA_PROVIDER_DEFAULT_YES  = 1,

  } ipmeta_provider_default_t;

/** A unique identifier for each metadata provider that libipmeta supports
 *
 * @note Remember to add the provider name to provider_names in
 * ipmeta_providers.c when you add a new provider ID below
 */
typedef enum ipmeta_provider_id
  {
    /** Geolocation data from Maxmind (Geo or GeoLite) */
    IPMETA_PROVIDER_MAXMIND      =  1,

#if 0
    /** Geolocation data from Net Acuity Edge */
    IPMETA_PROVIDER_NETACQ_EDGE  =  2,

    /** ASN data from CAIDA pfx2as */
    IPMETA_PROVIDER_PFX2AS       = 3,
#endif

    /** Highest numbered metadata provider ID */
    IPMETA_PROVIDER_MAX          = IPMETA_PROVIDER_MAXMIND,

  } ipmeta_provider_id_t;

/** A unique identifier for each metadata ds that libipmeta supports.
 *
 * @note When adding a datastructure to this list, there must also be a
 * corresponding entry added to the ds_alloc_functions array in ipmeta_ds.c
 */
typedef enum ipmeta_ds_id
  {
    /** Patricia Trie */
    IPMETA_DS_PATRICIA      =  1,

    /** @todo add Huge Array implementation */

    /** Highest numbered ds ID */
    IPMETA_DS_MAX          = IPMETA_DS_PATRICIA,

    /** Default Geolocation data-structure */
    IPMETA_DS_DEFAULT      = IPMETA_DS_PATRICIA,

  } ipmeta_ds_id_t;

/** @} */

/** Initialize a new libipmeta instance
 *
 * @return the ipmeta instance created, NULL if an error occurs
 */
ipmeta_t *ipmeta_init();

/** Free a libipmeta instance
 *
 * @param               The ipmeta instance to free
 */
void ipmeta_free(ipmeta_t *ipmeta);

/** Enable the given provider unless it is already enabled
 *
 * @param ipmeta        The ipmeta object to enable the provider for
 * @param provider_id   The ID of the provider to be enabled
 * @param ds_id         The ID of the datastructure to use
 * @param set_default   Set this provider as default if non-zero
 * @return 0 if the provider was initialized, -1 if an error occurred
 *
 * Once ipmeta_init is called, ipmeta_enable_provider should be called once for
 * each provider that is to be used. Enabling providers that are not necessary
 * should not impact runtime speed, but it may use considerable amounts of
 * memory.
 *
 * To obtain a pointer to a provider, use the ipmeta_get_provider_by_name or
 * ipmeta_get_provider_by_id functions. To enumerate a list of available
 * providers, the ipmeta_get_all_providers function can be used to get a list of
 * all providers and then ipmeta_get_provider_name can be used on each to get
 * their name.
 *
 * @note Default provider status overrides the requests of previous
 * plugins. Thus, the order in which users request the plugins to be run in can
 * have an effect on plugins which make use of the default provider
 * (e.g. corsaro_report).
 */
int ipmeta_enable_provider(ipmeta_t *ipmeta,
			   ipmeta_provider_t *provider,
			   ipmeta_ds_id_t ds_id,
			   const char *options,
			   ipmeta_provider_default_t set_default);

/** Retrieve the provider object for the default metadata provider
 *
 * @param ipmeta       The ipmeta object to retrieve the provider object from
 * @return the provider object for the default provider, NULL if there is no
 * default provider
 */
ipmeta_provider_t *ipmeta_get_default_provider(ipmeta_t *ipmeta);

/** Retrieve the provider object for the given provider ID
 *
 * @param ipmeta        The ipmeta object to retrieve the provider object from
 * @param id            The metadata provider ID to retrieve
 * @return the provider object for the given ID, NULL if there are no matches
 */
ipmeta_provider_t *ipmeta_get_provider_by_id(ipmeta_t *ipmeta,
					     ipmeta_provider_id_t id);

/** Retrieve the provider object for the given provider name
 *
 * @param ipmeta        The ipmeta object to retrieve the provider object from
 * @param name          The metadata provider name to retrieve
 * @return the provider object for the given name, NULL if there are no matches
 */
ipmeta_provider_t *ipmeta_get_provider_by_name(ipmeta_t *ipmeta,
					       const char *name);

/** Look up the given address using the given provider
 *
 * @param ipmeta        The ipmeta object associated with the provider
 * @param provider      The provider to perform the lookup with
 * @param addr          The address to retrieve the record for
 *                       (network byte ordering)
 * @return the record which best matches the address, NULL if no record is found
 */
ipmeta_record_t *ipmeta_lookup(ipmeta_provider_t *provider,
			       uint32_t addr);

/** Check if the given provider is enabled already
 *
 * @param provider      The provider to check the status of
 * @return 1 if the provider is enabled, 0 otherwise
 */
int ipmeta_is_provider_enabled(ipmeta_provider_t *provider);

/** Get the ID for the given provider
 *
 * @param provider      The provider object to retrieve the ID from
 * @return the ID of the given provider
 */
int ipmeta_get_provider_id(ipmeta_provider_t *provider);

/** Get the provider name for the given ID
 *
 * @param id            The provider ID to retrieve the name for
 * @return the name of the provider, NULL if an invalid ID was provided
 */
const char *ipmeta_get_provider_name(ipmeta_provider_t *provider);

/** Get an array of available providers
 *
 * @param ipmeta        The ipmeta object to get all the providers for
 * @return an array of provider objects
 *
 * @note the number of elements in the array will be exactly
 * IPMETA_PROVIDER_MAX.
 * @note not all providers in the list may be enabled. use
 * ipmeta_is_provider_enabled to check.
 */
ipmeta_provider_t **ipmeta_get_all_providers(ipmeta_t *ipmeta);

/** Dump the given metadata record to stdout
 *
 * @param record        The record to dump
 *
 * Each field in the record is written to stdout in pipe-delimited format.
 */
void ipmeta_dump_record(ipmeta_record_t *record);

/** Dump names of the fields in a record structure
 *
 * Each record field name is written to stdout in pipe-delimited format, and in
 * the same order as the contents are written out when using ipmeta_dump_record.
 */
void ipmeta_dump_record_header();

#endif /* __LIBIPMETA_H */
