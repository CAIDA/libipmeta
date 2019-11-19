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
#include <wandio.h>

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

/** Opaque struct holding a set of records */
typedef struct ipmeta_record_set ipmeta_record_set_t;

/** @} */

/**
 * @name Public Enums
 *
 * @{ */

/** A unique identifier for each metadata provider that libipmeta supports
 *
 * @note Remember to add the provider name to provider_names in
 * ipmeta_providers.c when you add a new provider ID below
 */
typedef enum ipmeta_provider_id {
  /** Geolocation data from Maxmind (Geo or GeoLite) */
  IPMETA_PROVIDER_MAXMIND = 1,

  /** Geolocation data from Net Acuity Edge */
  IPMETA_PROVIDER_NETACQ_EDGE = 2,

  /** @todo add a netacq-legacy provider */

  /** ASN data from CAIDA pfx2as */
  IPMETA_PROVIDER_PFX2AS = 3,

  /** Geolocation data from Maxmind (Geo or GeoLite) v2 */
  IPMETA_PROVIDER_MAXMIND_v2 = 4,

  /** Highest numbered metadata provider ID */
  IPMETA_PROVIDER_MAX = IPMETA_PROVIDER_MAXMIND_v2,

} ipmeta_provider_id_t;

/** A unique identifier for each metadata ds that libipmeta supports.
 *
 * @note When adding a datastructure to this list, there must also be a
 * corresponding entry added to the ds_alloc_functions array in ipmeta_ds.c
 */
typedef enum ipmeta_ds_id {
  /** Patricia Trie */
  IPMETA_DS_PATRICIA = 1,

  /** Big-Array */
  IPMETA_DS_BIGARRAY = 2,

  /** Interval-Tree */
  IPMETA_DS_INTERVALTREE = 3,

  /** Highest numbered ds ID */
  IPMETA_DS_MAX = IPMETA_DS_INTERVALTREE,

  /** Default Geolocation data-structure */
  IPMETA_DS_DEFAULT = IPMETA_DS_PATRICIA,

} ipmeta_ds_id_t;

/** @} */

/**
 * @name Public Data Structures
 *
 * @{ */

/** Structure which contains an IP meta-data record
 *
 * @todo use some sort of key-value record so that we don't have to extend this
 * structure whenever a new provider is added.
 *
 * @note you must update the ipmeta_dump_record and ipmeta_write_record (and
 * related _header functions) functions when making changes to this structure.
 */
typedef struct ipmeta_record {
  /** A unique ID for this record (used to join the Blocks and Locations Files)
   *
   * This should be considered unique only within a single provider type
   * i.e. id's may not be unique across different ipmeta_provider_t objects
   */
  uint32_t id;

  /** The provider that this record came from */
  ipmeta_provider_id_t source;

  /** 2 character string which holds the ISO2 country code */
  char country_code[3];

  /** 2 character string which holds the continent code */
  char continent_code[3];

  /** character string which represents the region the city is in */
  char *region;

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

  /** Region code
   * @note this code is internal to each provider. A lookup table must be used
   */
  uint16_t region_code;

  /** Connection Speed/Type */
  char *conn_speed;

  /** Array of Autonomous System Numbers */
  uint32_t *asn;

  /** Number of ASNs in the asn array */
  int asn_cnt;

  /** Number of IP addresses that this ASN (or ASN group) 'owns' */
  uint32_t asn_ip_cnt;

  /** Polygon IDs. Indexes SHOULD correspond to those in the polygon table list
      obtained from the provider */
  uint32_t *polygon_ids;

  /** Number of IDs in the Polygon IDs array */
  int polygon_ids_cnt;

  /** New variables added for Maxmind_v2 **/

  /** LOCATIONS */
  /** 2 char local code - GeoIP2 Col 1 */
  char locale_code[2];

  /** String which contains the country name - GeoIP2 Col 5 */
  char *country;

  /** String which contains the timezone - GeoIP2 Col 12 */
  char *timezone;

  /** Integer stating whether the IP is in EU or not */
  int in_eu;

  /** BLOCKS */
  /** Geoname id */
  uint32_t geonameid;

  /** CCGeonameID */
  uint32_t ccgeonameid;

  /** is proxy ? */
  int proxy;

  /** is sattelite provider? */
  int satprov;

  /** accuracy */
  uint32_t accuracy;

  /* -- ADD NEW FIELDS ABOVE HERE -- */

  /** The next record in the list */
  struct ipmeta_record *next;

} ipmeta_record_t;

/** @} */

/** Initialize a new libipmeta instance
 *
 * @param The name of the data structure to use for storing prefixes.
 *
 * @return the ipmeta instance created, NULL if an error occurs
 */
ipmeta_t *ipmeta_init(enum ipmeta_ds_id dstype);

/** Free a libipmeta instance
 *
 * @param               The ipmeta instance to free
 */
void ipmeta_free(ipmeta_t *ipmeta);

/** Enable the given provider unless it is already enabled
 *
 * @param ipmeta        The ipmeta object to enable the provider for
 * @param provider      Pointer to the provider to be enabled
 * @param options       Options string to pass to the provider
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
 */
int ipmeta_enable_provider(ipmeta_t *ipmeta, ipmeta_provider_t *provider,
                           const char *options);

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

/** Look up the given IP prefix using a set of known providers
 *
 * @param ipmeta        The ipmeta instance to use for the lookup
 * @param addr          The IPv4 network address part to lookup
 *                       (network byte ordering)
 * @param mask          The IPv4 network mask defining the prefix length (0>32)
 * @param provmask	A bitmask indicating which providers should be used.
 *                       Set to '0' to automatically use all active providers.
 * @param records       Pointer to a record set to use for matches
 * @return              The number of (matched) records in the result set
 */
int ipmeta_lookup(ipmeta_t *ipmeta, uint32_t addr, uint8_t mask,
                  uint32_t provmask, ipmeta_record_set_t *records);

/** Look up the given single IP address for a set of providers
 *
 * @param ipmeta        The ipmeta instance to use for the lookup
 * @param addr          The address to retrieve the record for
 *                       (network byte ordering)
 * @param providermask  A bitmask describing which providers to perform the
                         lookup with. Set to '0' to automatically use all
                         active providers.
 * @param found         Pointer to a record set to use for storing matches
 * @return The number of providers which we were able to successfully find a
 *         match for, or -1 if an error occured.
 */
int ipmeta_lookup_single(ipmeta_t *ipmeta, uint32_t addr, uint32_t providermask,
                         ipmeta_record_set_t *found);

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

/** Initialize a new record set instance
 *
 * @return the record set instance created, NULL if an error occurs
 *
 * @note an interval record set **DOES NOT** contain a unique set of
 * records. Records can (and might) be repeated.
 */
ipmeta_record_set_t *ipmeta_record_set_init(void);

/** Free a record set instance
 *
 * @param record_set_p  The pointer to the record set instance to free
 */
void ipmeta_record_set_free(ipmeta_record_set_t **record_set_p);

/** Clear a record set instance, without freeing the allocated memory.
 *
 * @param this          The record set to clear.
 *
 * @note this function allows you to reuse a record set without having
 * to free and re-initialise a record set instance.
 */
void ipmeta_record_set_clear(ipmeta_record_set_t *this);

/** Move the record set iterator pointer to the first element
 *
 * @param record_set    The record set instance
 */
void ipmeta_record_set_rewind(ipmeta_record_set_t *record_set);

/** Get the next record in the record set iterator
 *
 * @param record_set    The record set instance
 * @param[out] num_ips  Pointer to an int set to the number of matched IPs
 *                      (optional)
 *
 * @return a pointer to the record
 *
 * @note an interval record set **DOES NOT** contain a unique set of
 * records. Records can (and might) be repeated.
 */
ipmeta_record_t *ipmeta_record_set_next(ipmeta_record_set_t *record_set,
                                        uint32_t *num_ips);

/** Dump the given metadata record set to stdout
 *
 * @param record_set    The record set to dump
 * @param ip_str        The IP address/prefix string this record was looked up
 * for
 *
 * Each record is written in a new line and each record field is pipe-delimited.
 */
void ipmeta_dump_record_set(ipmeta_record_set_t *record_set, char *ip_str);

/** Dump only records sourced from a specific provider in the given metadata
 *  record set to stdout
 *
 * @param this          The record set to dump
 * @param ip_str        The IP address/prefix string this record was looked up
 * for
 * @param providerid	The id number of the provider to limit our output to
 *
 * Each record is written in a new line and each record field is pipe-delimited.
 */
void ipmeta_dump_record_set_by_provider(ipmeta_record_set_t *this, char *ip_str,
                                        int providerid);

/** Write the given metadata record set to the given wandio file
 *
 * @param record_set    The record set to dump
 * @param file          The wandio file to write to
 * @param ip_str        The IP address/prefix string this record was looked up
 * for
 *
 * Each record is written in a new line and each record field is pipe-delimited.
 */
void ipmeta_write_record_set(ipmeta_record_set_t *record_set, iow_t *file,
                             char *ip_str);

/** Write only records sourced from a specific provider in the given metadata
 *  record set to a wandio file
 *
 * @param this          The record set to dump
 * @param file          The wandio file to write to
 * @param ip_str        The IP address/prefix string this record was looked up
 * for
 * @param providerid	The id number of the provider to limit our output to
 *
 * Each record is written in a new line and each record field is pipe-delimited.
 */
void ipmeta_write_record_set_by_provider(ipmeta_record_set_t *this, iow_t *file,
                                         char *ip_str, int providerid);

/** Dump the given metadata record to stdout
 *
 * @param record        The record to dump
 * @param ip_str        The IP address/prefix string this record was looked up
 * for
 * @param num_ips       The number of IPs from the prefix that this record
 * applies to
 *
 * Each field in the record is written to stdout in pipe-delimited format.
 */
void ipmeta_dump_record(ipmeta_record_t *record, char *ip_str, int num_ips);

/** Dump names of the fields in a record structure
 *
 * Each record field name is written to stdout in pipe-delimited format, and in
 * the same order as the contents are written out when using ipmeta_dump_record.
 */
void ipmeta_dump_record_header(void);

/** Write the given metadata record to the given wandio file
 *
 * @param file          The wandio file to write to
 * @param record        The record to dump
 * @param ip_str        The IP address/prefix string this record was looked up
 * for
 *
 * Each field in the record is written to the given file in pipe-delimited
 * format (prefixed with the IP string given)
 */
void ipmeta_write_record(iow_t *file, ipmeta_record_t *record, char *ip_str,
                         int num_ips);

/** Write names of the fields in a record structure to the given wandio file
 *
 * @param file          The wandio file to write to
 *
 * Each record field name is written in pipe-delimited format, and in the same
 * order as the contents are written out when using ipmeta_write_record.
 */
void ipmeta_write_record_header(iow_t *file);

/** Get an array of all the metadata records registered with the given
 *  provider
 *
 * @param provider      The metadata provider to retrieve the records from
 * @param[out] records  Returns an array of metadata records
 * @return the number of records in the array, -1 if an error occurs
 *
 * @note This function allocates and populates the array dynamically, so do not
 * call repeatedly. Also, it is the caller's responsibility to free the array.
 * DO NOT free the records contained in the array.
 */
int ipmeta_provider_get_all_records(ipmeta_provider_t *provider,
                                    ipmeta_record_t ***records);

/**
 * @name Logging functions
 *
 * Collection of convenience functions that allow libipmeta to log events
 * For now we just log to stderr, but this should be extended in future.
 *
 * @todo find (or write) good C logging library (that can also log to syslog)
 *
 * @{ */

void ipmeta_log(const char *func, const char *format, ...);

/** @} */

/**
 * @name Provider-specific convenience functions
 * @{ */

/** Convenience function to retrieve a list of ISO 2 character country codes
 *
 * @param countries[out]   The provided pointer is updated to point to an
 *                         array of 2 character country code strings
 * @return the number of elements in the array
 */
int ipmeta_provider_maxmind_get_iso2_list(const char ***countries);

/** Convenience function to retrieve a list of 2 character continent codes in
 * the same ordering as the countries returned by
 * ipmeta_provider_maxmind_get_iso2_list
 *
 * @param continents[out]   The provided pointer is updated to point to an
 *                          array of 2 character continent code strings
 * @return the number of elements in the array
 */
int ipmeta_provider_maxmind_get_country_continent_list(
  const char ***continents);

/** Convenience function to retrieve a list of ISO 2 character country codes
 *
 * @param countries[out]   The provided pointer is updated to point to an
 *                         array of 2 character country code strings
 * @return the number of elements in the array
 */
int ipmeta_provider_maxmind_v2_get_iso2_list(const char ***countries);

/** Convenience function to retrieve a list of 2 character continent codes in
 * the same ordering as the countries returned by
 * ipmeta_provider_maxmind_v2_get_iso2_list
 *
 * @param continents[out]   The provided pointer is updated to point to an
 *                          array of 2 character continent code strings
 * @return the number of elements in the array
 */
int ipmeta_provider_maxmind_v2_get_country_continent_list(
  const char ***continents);

/** Information about a single Net Acuity region */
typedef struct ipmeta_provider_netacq_edge_region {
  /** A unique code for this region */
  uint32_t code;

  /** ISO 3166 3 letter country code */
  char country_iso[4];

  /** ISO 3166 region code */
  char region_iso[4];

  /* Region Name/Description */
  char *name;

} ipmeta_provider_netacq_edge_region_t;

/** Information about a single Net Acuity country */
typedef struct ipmeta_provider_netacq_edge_country {
  /** A unique code for this country */
  uint32_t code;

  /** ISO 3166 2 letter country code */
  char iso2[3];

  /** ISO 3166 3 letter country code */
  char iso3[4];

  /** Country Name */
  char *name;

  /** Binary field indicating if Net Acuity has region info */
  uint8_t regions;

  /** Numeric code for the continent */
  uint8_t continent_code;

  /** 2 Char Continent Abbreviation */
  char continent[3];

} ipmeta_provider_netacq_edge_country_t;

/** @todo move these defs and integrate more tightly with the top-level ipmeta
    library rather than just being in netacq */

/** Information about a single Polygon */
typedef struct ipmeta_polygon {
  /** A unique code for this polygon
      (0 is reserved for the "unknown polygon") */
  uint32_t id;

  /** Human-readable name of this polygon */
  char *name;

  /** Fully-qualified id of this polygon */
  char *fqid;

  /** User-provided code for this polygon */
  char *usercode;

} ipmeta_polygon_t;

/** Information about a Polygon table */
typedef struct ipmeta_polygon_table {
  /** Generated table ID
      (corresponds to the index in the polygon_ids array in a record) */
  uint32_t id;

  /** Official ASCII id of this table */
  char *ascii_id;

  /** Array of polygons in the table
      (polygon at 0 MUST be the unknown polygon) */
  ipmeta_polygon_t **polygons;

  /** Number of polygons in the table */
  int polygons_cnt;

} ipmeta_polygon_table_t;

/** Retrieve a list of Net Acuity region objects
 *
 * @param provider      The provider to retrieve the regions from
 * @param regions[out]  The provided pointer is updated to point to an array
 *                      of region objects
 * @return the number of regions in the array
 *
 * @note This function will return NULL unless regions info has been loaded by
 * using the -r option.
 */
int ipmeta_provider_netacq_edge_get_regions(
  ipmeta_provider_t *provider, ipmeta_provider_netacq_edge_region_t ***regions);

/** Retrieve a list of Net Acuity country objects
 *
 * @param provider        The provider to retrieve the countries from
 * @param countries[out]  The provided pointer is updated to point to an array
 *                        of country objects
 * @return the number of countries in the array
 *
 * @note This function will return NULL unless countries info has been loaded by
 * using the -c option.
 */
int ipmeta_provider_netacq_edge_get_countries(
  ipmeta_provider_t *provider,
  ipmeta_provider_netacq_edge_country_t ***countries);

/** Retrieve a list of Polygon table objects
 *
 * @param provider       The provider to retrieve the polygon tables from
 * @param[out] tables  The provided pointer is updated to point to an array
 *                        of polygon table objects
 * @return the number of polygon tables in the array
 *
 * @note This function will return NULL unless polygon info has been loaded by
 * using the -p option.
 */
int ipmeta_provider_netacq_edge_get_polygon_tables(
  ipmeta_provider_t *provider, ipmeta_polygon_table_t ***tables);

/** @} */

#endif /* __LIBIPMETA_H */
