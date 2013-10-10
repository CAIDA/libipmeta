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
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils.h"

#include "libipmeta_int.h"
#include "ipmeta_ds.h"

/** The list of names for metadata providers
 *
 * @note this list MUST be kept in sync with ipmeta_provider_id_t
 * @todo replace this with names like in corsaro plugins
 */
static const char *provider_names[] = {
  NULL,
  "maxmind",
  "netacq-edge",
  "pfx2as",
};

static void free_record(ipmeta_record_t *record)
{
  if(record == NULL)
    {
      return;
    }

  /* free the strings */
  if(record->city != NULL)
    {
      free(record->city);
      record->city = NULL;
    }

  if(record->post_code != NULL)
    {
      free(record->post_code);
      record->post_code = NULL;
    }

  if(record->asn != NULL)
    {
      free(record->asn);
      record->asn = NULL;
      record->asn_cnt = 0;
    }

  free(record);
  return;
}

/* --- Public functions below here -- */

const char *ipmeta_get_provider_name(ipmeta_provider_id_t id)
{
  assert(ARR_CNT(provider_names) == IPMETA_PROVIDER_MAX+1);

  if(id > IPMETA_PROVIDER_MAX || id <= 0)
    {
      return NULL;
    }

  return provider_names[id];
}

const char **ipmeta_get_provider_names()
{
  assert(ARR_CNT(provider_names) == IPMETA_PROVIDER_MAX+1);

  return provider_names;
}

ipmeta_provider_t *ipmeta_init_provider(ipmeta_t *ipmeta,
					ipmeta_provider_id_t provider_id,
					ipmeta_ds_id_t ds_id,
					ipmeta_provider_default_t set_default)
{
  assert(ipmeta != NULL);

  ipmeta_provider_t *provider;
  /* first, create the struct */
  if((provider = malloc_zero(sizeof(ipmeta_provider_t))) == NULL)
    {
      ipmeta_log(__func__, "could not malloc ipmeta_provider_t");
      return NULL;
    }

  assert(provider_id > 0 && provider_id <= IPMETA_PROVIDER_MAX);

  /* set the fields */
  provider->id = provider_id;
  provider->name = ipmeta_get_provider_name(provider_id);

  /* initialize the record hash */
  provider->all_records = kh_init(ipmeta_rechash);

  /* initialize the datastructure */
  if(ipmeta_ds_init(provider, ds_id) != 0)
    {
      ipmeta_log(__func__, "could not initialize datastructure");
      goto err;
    }

  provider->records = NULL;

  /* poke it into ipmeta */
  ipmeta->providers[provider_id - 1] = provider;

  if(set_default == IPMETA_PROVIDER_DEFAULT_YES)
    {
      ipmeta->provider_default = provider;
    }

  return provider;

 err:
  if(provider != NULL)
    {
      if(provider->ds != NULL)
	{
	  provider->ds->free(provider->ds);
	  provider->ds = NULL;
	}
      free(provider);
    }
  return NULL;

}


void ipmeta_free_provider(ipmeta_t *ipmeta,
			  ipmeta_provider_t *provider)
{
  /* short circuit if someone passed us a NULL pointer */
  if(provider == NULL)
    {
      return;
    }

  /* perhaps ipmeta was free'd before we were? */
  /** @todo check the assumption that it is possible to free ipmeta first */
  if(ipmeta != NULL)
    {
      /* ok, lets check if we were the default */
      if(ipmeta->provider_default == provider)
	{
	  ipmeta->provider_default = NULL;
	}

      /* remove the pointer from ipmeta */
      ipmeta->providers[provider->id - 1] = NULL;
    }

  /* attempt to clear the records (just in case) */
  ipmeta_provider_clear(provider);

  /* free the ds */
  if(provider->ds != NULL)
    {
      provider->ds->free(provider->ds);
      provider->ds = NULL;
    }

  /* free the records hash */
  if(provider->all_records != NULL)
    {
      /* this is where the records are free'd */
      kh_free_vals(ipmeta_rechash, provider->all_records, free_record);
      kh_destroy(ipmeta_rechash, provider->all_records);
      provider->all_records = NULL;
    }

  /* finally, free the actual provider structure */
  free(provider);

  return;
}

ipmeta_record_t *ipmeta_init_record(ipmeta_provider_t *provider, uint32_t id)
{
  ipmeta_record_t *record;
  khiter_t khiter;
  int khret;

  if((record = malloc_zero(sizeof(ipmeta_record_t))) == NULL)
    {
      return NULL;
    }

  record->id = id;

  assert(kh_get(ipmeta_rechash, provider->all_records, id) ==
	 kh_end(provider->all_records));

  khiter = kh_put(ipmeta_rechash, provider->all_records, id, &khret);
  kh_value(provider->all_records, khiter) = record;

  assert(kh_get(ipmeta_rechash, provider->all_records, id) !=
	 kh_end(provider->all_records));

  return record;
}

ipmeta_record_t *ipmeta_get_record(ipmeta_provider_t *provider, uint32_t id)
{
  khiter_t khiter;

  /* grab the corresponding record from the hash */
  if((khiter = kh_get(ipmeta_rechash, provider->all_records, id))
     == kh_end(provider->all_records))
    {
      return NULL;
    }
  return kh_val(provider->all_records, khiter);
}

int ipmeta_get_all_records(ipmeta_provider_t *provider,
			   ipmeta_record_t ***records)
{
  ipmeta_record_t **rec_arr = NULL;
  ipmeta_record_t **rec_ptr = NULL;
  int rec_cnt = kh_size(provider->all_records);
  khiter_t i;

  /* if there are no records in the array, don't bother */
  if(rec_cnt == 0)
    {
      *records = NULL;
      return 0;
    }

  /* first we malloc an array to hold all the records */
  if((rec_arr = malloc(sizeof(ipmeta_record_t*) * rec_cnt)) == NULL)
    {
      return -1;
    }

  rec_ptr = rec_arr;
  /* insert all the records into the array */
  for(i = kh_begin(provider->all_records);
      i != kh_end(provider->all_records);
      ++i)
    {
      if(kh_exist(provider->all_records, i))
	{
	  *rec_ptr = kh_value(provider->all_records, i);
	  rec_ptr++;
	}
    }

  /* return the array and the count */
  *records = rec_arr;
  return rec_cnt;
}

int ipmeta_provider_associate_record(ipmeta_provider_t *provider,
				     uint32_t addr,
				     uint8_t mask,
				     ipmeta_record_t *record)
{
  assert(provider != NULL && record != NULL);
  assert(provider->ds != NULL);

  return provider->ds->add_prefix(provider->ds, addr, mask, record);
}


ipmeta_record_t *ipmeta_provider_lookup_record(ipmeta_provider_t *provider,
					       uint32_t addr)
{
  assert(provider != NULL);
  assert(provider->ds != NULL);

  return provider->ds->lookup_record(provider->ds, addr);
}

int ipmeta_provider_clear(ipmeta_provider_t *provider)
{
  ipmeta_record_t *this = NULL;
  int cnt = 0;

  assert(provider != NULL);

  while(provider->records != NULL)
    {
      this = provider->records;
      provider->records = provider->records->next;
      this->next = NULL;
      cnt++;
    }

  return cnt;
}

void ipmeta_provider_add_record(ipmeta_provider_t *provider,
				ipmeta_record_t *record)
{
  assert(provider != NULL);

  /* we allow a NULL record to be added as a convenience.  it won't actually be
     added to the list, but this allows the result of _lookup_record to be fed
     directly in here */
  if(record == NULL)
    {
      return;
    }

  assert(record->next == NULL);

  /* set the next of this record to the previous head */
  record->next = provider->records;

  /* set the head to this record */
  provider->records = record;

  return;
}

ipmeta_provider_t *ipmeta_get_default(ipmeta_t *ipmeta)
{
  assert(ipmeta != NULL);
  return ipmeta->provider_default;
}

ipmeta_provider_t *ipmeta_get_by_id(ipmeta_t *ipmeta,
				    ipmeta_provider_id_t id)
{
  assert(ipmeta != NULL);
  assert(id > 0 && id <= IPMETA_PROVIDER_MAX);
  return ipmeta->providers[id - 1];
}

ipmeta_provider_t *ipmeta_get_by_name(ipmeta_t *ipmeta, const char *name)
{
  ipmeta_provider_t *provider;
  int i;

  for(i = 1; i <= IPMETA_PROVIDER_MAX; i++)
    {
      if((provider = ipmeta_get_by_id(ipmeta, i)) != NULL &&
	 strncasecmp(provider->name, name, strlen(provider->name)) == 0)
	{
	  return provider;
	}
    }

  return NULL;
}

ipmeta_record_t *ipmeta_next_record(ipmeta_provider_t *provider,
				    ipmeta_record_t *record)
{
  assert(provider != NULL);

  /* they are asking for the beginning of the list, or there is no list */
  if(record == NULL || provider->records == NULL)
    {
      return provider->records;
    }

  /* otherwise, give them the next one */
  return record->next;
}

void ipmeta_dump_record(ipmeta_record_t *record)
{
  int i;
  if(record == NULL)
    {
      return;
    }

  fprintf(stdout,
	  "id: %"PRIu32", cc: %s, cont: %d, reg: %s, city: %s, post: %s, "
	  "lat: %f, long: %f, met: %"PRIu32", area: %"PRIu32", "
	  "speed: %s, asn: ",
	  record->id,
	  record->country_code,
	  record->continent_code,
	  record->region,
	  record->city,
	  record->post_code,
	  record->latitude,
	  record->longitude,
	  record->metro_code,
	  record->area_code,
	  record->conn_speed
	  );

      for(i=0; i<record->asn_cnt; i++)
	{
	  fprintf(stdout, "%d", record->asn[i]);
	  if(i<record->asn_cnt-1)
	    fprintf(stdout, "_");
	}
      fprintf(stdout, "\n");
}

/* ----- Class Helper Functions below here ------ */
/** @todo move these functions to the appropriate provider class */

/** Array of ISO 2char country codes. Extracted from libGeoIP v1.5.0 */
const char *ipmeta_geo_maxmind_country_code_iso2[] = {
  "--","AP","EU","AD","AE","AF","AG","AI","AL","AM","CW",
  "AO","AQ","AR","AS","AT","AU","AW","AZ","BA","BB",
  "BD","BE","BF","BG","BH","BI","BJ","BM","BN","BO",
  "BR","BS","BT","BV","BW","BY","BZ","CA","CC","CD",
  "CF","CG","CH","CI","CK","CL","CM","CN","CO","CR",
  "CU","CV","CX","CY","CZ","DE","DJ","DK","DM","DO",
  "DZ","EC","EE","EG","EH","ER","ES","ET","FI","FJ",
  "FK","FM","FO","FR","SX","GA","GB","GD","GE","GF",
  "GH","GI","GL","GM","GN","GP","GQ","GR","GS","GT",
  "GU","GW","GY","HK","HM","HN","HR","HT","HU","ID",
  "IE","IL","IN","IO","IQ","IR","IS","IT","JM","JO",
  "JP","KE","KG","KH","KI","KM","KN","KP","KR","KW",
  "KY","KZ","LA","LB","LC","LI","LK","LR","LS","LT",
  "LU","LV","LY","MA","MC","MD","MG","MH","MK","ML",
  "MM","MN","MO","MP","MQ","MR","MS","MT","MU","MV",
  "MW","MX","MY","MZ","NA","NC","NE","NF","NG","NI",
  "NL","NO","NP","NR","NU","NZ","OM","PA","PE","PF",
  "PG","PH","PK","PL","PM","PN","PR","PS","PT","PW",
  "PY","QA","RE","RO","RU","RW","SA","SB","SC","SD",
  "SE","SG","SH","SI","SJ","SK","SL","SM","SN","SO",
  "SR","ST","SV","SY","SZ","TC","TD","TF","TG","TH",
  "TJ","TK","TM","TN","TO","TL","TR","TT","TV","TW",
  "TZ","UA","UG","UM","US","UY","UZ","VA","VC","VE",
  "VG","VI","VN","VU","WF","WS","YE","YT","RS","ZA",
  "ZM","ME","ZW","A1","A2","O1","AX","GG","IM","JE",
  "BL","MF", "BQ", "SS", "O1",
  /* Alistair adds AN because Maxmind does not include it, but uses it */
  "AN",
};

/** Array of ISO 3 char country codes. Extracted from libGeoIP v1.5.0 */
const char *ipmeta_geo_maxmind_country_code_iso3[] = {
  "--","AP","EU","AND","ARE","AFG","ATG","AIA","ALB","ARM","CUW",
  "AGO","ATA","ARG","ASM","AUT","AUS","ABW","AZE","BIH","BRB",
  "BGD","BEL","BFA","BGR","BHR","BDI","BEN","BMU","BRN","BOL",
  "BRA","BHS","BTN","BVT","BWA","BLR","BLZ","CAN","CCK","COD",
  "CAF","COG","CHE","CIV","COK","CHL","CMR","CHN","COL","CRI",
  "CUB","CPV","CXR","CYP","CZE","DEU","DJI","DNK","DMA","DOM",
  "DZA","ECU","EST","EGY","ESH","ERI","ESP","ETH","FIN","FJI",
  "FLK","FSM","FRO","FRA","SXM","GAB","GBR","GRD","GEO","GUF",
  "GHA","GIB","GRL","GMB","GIN","GLP","GNQ","GRC","SGS","GTM",
  "GUM","GNB","GUY","HKG","HMD","HND","HRV","HTI","HUN","IDN",
  "IRL","ISR","IND","IOT","IRQ","IRN","ISL","ITA","JAM","JOR",
  "JPN","KEN","KGZ","KHM","KIR","COM","KNA","PRK","KOR","KWT",
  "CYM","KAZ","LAO","LBN","LCA","LIE","LKA","LBR","LSO","LTU",
  "LUX","LVA","LBY","MAR","MCO","MDA","MDG","MHL","MKD","MLI",
  "MMR","MNG","MAC","MNP","MTQ","MRT","MSR","MLT","MUS","MDV",
  "MWI","MEX","MYS","MOZ","NAM","NCL","NER","NFK","NGA","NIC",
  "NLD","NOR","NPL","NRU","NIU","NZL","OMN","PAN","PER","PYF",
  "PNG","PHL","PAK","POL","SPM","PCN","PRI","PSE","PRT","PLW",
  "PRY","QAT","REU","ROU","RUS","RWA","SAU","SLB","SYC","SDN",
  "SWE","SGP","SHN","SVN","SJM","SVK","SLE","SMR","SEN","SOM",
  "SUR","STP","SLV","SYR","SWZ","TCA","TCD","ATF","TGO","THA",
  "TJK","TKL","TKM","TUN","TON","TLS","TUR","TTO","TUV","TWN",
  "TZA","UKR","UGA","UMI","USA","URY","UZB","VAT","VCT","VEN",
  "VGB","VIR","VNM","VUT","WLF","WSM","YEM","MYT","SRB","ZAF",
  "ZMB","MNE","ZWE","A1","A2","O1","ALA","GGY","IMN","JEY",
  "BLM","MAF", "BES", "SSD", "O1",
  /* see above about AN */
  "ANT",
};

/** Array of country names. Extracted from libGeoIP v1.4.8 */
const char *ipmeta_geo_maxmind_country_name[] = {
  "N/A","Asia/Pacific Region","Europe","Andorra","United Arab Emirates",
  "Afghanistan","Antigua and Barbuda","Anguilla","Albania","Armenia",
  "Cura" "\xe7" "ao","Angola","Antarctica","Argentina","American Samoa",
  "Austria","Australia","Aruba","Azerbaijan","Bosnia and Herzegovina",
  "Barbados","Bangladesh","Belgium","Burkina Faso","Bulgaria","Bahrain",
  "Burundi","Benin","Bermuda","Brunei Darussalam","Bolivia","Brazil",
  "Bahamas","Bhutan","Bouvet Island","Botswana","Belarus","Belize",
  "Canada","Cocos (Keeling) Islands","Congo, The Democratic Republic of the",
  "Central African Republic","Congo","Switzerland","Cote D'Ivoire",
  "Cook Islands","Chile","Cameroon","China","Colombia","Costa Rica","Cuba",
  "Cape Verde","Christmas Island","Cyprus","Czech Republic","Germany",
  "Djibouti","Denmark","Dominica","Dominican Republic","Algeria","Ecuador",
  "Estonia","Egypt","Western Sahara","Eritrea","Spain","Ethiopia","Finland",
  "Fiji","Falkland Islands (Malvinas)","Micronesia, Federated States of",
  "Faroe Islands","France","Sint Maarten (Dutch part)","Gabon",
  "United Kingdom","Grenada","Georgia","French Guiana","Ghana","Gibraltar",
  "Greenland","Gambia","Guinea","Guadeloupe","Equatorial Guinea","Greece",
  "South Georgia and the South Sandwich Islands","Guatemala","Guam",
  "Guinea-Bissau","Guyana","Hong Kong","Heard Island and McDonald Islands",
  "Honduras","Croatia","Haiti","Hungary","Indonesia","Ireland","Israel",
  "India","British Indian Ocean Territory","Iraq","Iran, Islamic Republic of",
  "Iceland","Italy","Jamaica","Jordan","Japan","Kenya","Kyrgyzstan","Cambodia",
  "Kiribati","Comoros","Saint Kitts and Nevis",
  "Korea, Democratic People's Republic of","Korea, Republic of","Kuwait",
  "Cayman Islands","Kazakhstan","Lao People's Democratic Republic","Lebanon",
  "Saint Lucia","Liechtenstein","Sri Lanka","Liberia","Lesotho","Lithuania",
  "Luxembourg","Latvia","Libyan Arab Jamahiriya","Morocco","Monaco",
  "Moldova, Republic of","Madagascar","Marshall Islands","Macedonia","Mali",
  "Myanmar","Mongolia","Macau","Northern Mariana Islands","Martinique",
  "Mauritania","Montserrat","Malta","Mauritius","Maldives","Malawi","Mexico",
  "Malaysia","Mozambique","Namibia","New Caledonia","Niger","Norfolk Island",
  "Nigeria","Nicaragua","Netherlands","Norway","Nepal","Nauru","Niue",
  "New Zealand","Oman","Panama","Peru","French Polynesia","Papua New Guinea",
  "Philippines","Pakistan","Poland","Saint Pierre and Miquelon",
  "Pitcairn Islands","Puerto Rico","Palestinian Territory","Portugal","Palau",
  "Paraguay","Qatar","Reunion","Romania","Russian Federation","Rwanda",
  "Saudi Arabia","Solomon Islands","Seychelles","Sudan","Sweden","Singapore",
  "Saint Helena","Slovenia","Svalbard and Jan Mayen","Slovakia","Sierra Leone",
  "San Marino","Senegal","Somalia","Suriname","Sao Tome and Principe",
  "El Salvador","Syrian Arab Republic","Swaziland","Turks and Caicos Islands",
  "Chad","French Southern Territories","Togo","Thailand","Tajikistan",
  "Tokelau","Turkmenistan","Tunisia","Tonga","Timor-Leste","Turkey",
  "Trinidad and Tobago","Tuvalu","Taiwan","Tanzania, United Republic of",
  "Ukraine","Uganda","United States Minor Outlying Islands","United States",
  "Uruguay","Uzbekistan","Holy See (Vatican City State)",
  "Saint Vincent and the Grenadines","Venezuela","Virgin Islands, British",
  "Virgin Islands, U.S.","Vietnam","Vanuatu","Wallis and Futuna","Samoa",
  "Yemen","Mayotte","Serbia","South Africa","Zambia","Montenegro","Zimbabwe",
  "Anonymous Proxy","Satellite Provider","Other","Aland Islands","Guernsey",
  "Isle of Man","Jersey","Saint Barthelemy","Saint Martin",
  "Bonaire, Saint Eustatius and Saba", "South Sudan", "Other",
  /* again, see above about AN */
  "Netherlands Antilles",
};

const char *ipmeta_geo_maxmind_country_continent[] = {
  "--", "AS","EU","EU","AS","AS","NA","NA","EU","AS","NA",
  "AF","AN","SA","OC","EU","OC","NA","AS","EU","NA",
  "AS","EU","AF","EU","AS","AF","AF","NA","AS","SA",
  "SA","NA","AS","AN","AF","EU","NA","NA","AS","AF",
  "AF","AF","EU","AF","OC","SA","AF","AS","SA","NA",
  "NA","AF","AS","AS","EU","EU","AF","EU","NA","NA",
  "AF","SA","EU","AF","AF","AF","EU","AF","EU","OC",
  "SA","OC","EU","EU","NA","AF","EU","NA","AS","SA",
  "AF","EU","NA","AF","AF","NA","AF","EU","AN","NA",
  "OC","AF","SA","AS","AN","NA","EU","NA","EU","AS",
  "EU","AS","AS","AS","AS","AS","EU","EU","NA","AS",
  "AS","AF","AS","AS","OC","AF","NA","AS","AS","AS",
  "NA","AS","AS","AS","NA","EU","AS","AF","AF","EU",
  "EU","EU","AF","AF","EU","EU","AF","OC","EU","AF",
  "AS","AS","AS","OC","NA","AF","NA","EU","AF","AS",
  "AF","NA","AS","AF","AF","OC","AF","OC","AF","NA",
  "EU","EU","AS","OC","OC","OC","AS","NA","SA","OC",
  "OC","AS","AS","EU","NA","OC","NA","AS","EU","OC",
  "SA","AS","AF","EU","EU","AF","AS","OC","AF","AF",
  "EU","AS","AF","EU","EU","EU","AF","EU","AF","AF",
  "SA","AF","NA","AS","AF","NA","AF","AN","AF","AS",
  "AS","OC","AS","AF","OC","AS","EU","NA","OC","AS",
  "AF","EU","AF","OC","NA","SA","AS","EU","NA","SA",
  "NA","NA","AS","OC","OC","OC","AS","AF","EU","AF",
  "AF","EU","AF","--","--","--","EU","EU","EU","EU",
  "NA","NA","NA","AF","--",
  /* see above about AN */
  "NA",
};

#define COUNTRY_CNT ((unsigned)(					\
		  sizeof(ipmeta_geo_maxmind_country_code_iso2) /	\
		  sizeof(ipmeta_geo_maxmind_country_code_iso2[0])))

const char *ipmeta_geo_get_maxmind_iso2(int country_id)
{
  assert(country_id < COUNTRY_CNT);
  return ipmeta_geo_maxmind_country_code_iso2[country_id];
}

int ipmeta_geo_get_maxmind_iso2_list(const char ***countries)
{
  *countries = ipmeta_geo_maxmind_country_code_iso2;
  return COUNTRY_CNT;
}

const char *ipmeta_geo_get_maxmind_iso3(int country_id)
{
  assert(country_id < COUNTRY_CNT);
  return ipmeta_geo_maxmind_country_code_iso3[country_id];
}

int ipmeta_geo_get_maxmind_iso3_list(const char ***countries)
{
  *countries = ipmeta_geo_maxmind_country_code_iso3;
  return COUNTRY_CNT;
}

const char *ipmeta_geo_get_maxmind_country_name(int country_id)
{
  assert(country_id < COUNTRY_CNT);
  return ipmeta_geo_maxmind_country_name[country_id];
}

int ipmeta_geo_get_maxmind_country_name_list(const char ***countries)
{
  *countries = ipmeta_geo_maxmind_country_name;
  return COUNTRY_CNT;
}

const char *ipmeta_geo_get_maxmind_continent(int country_id)
{
  assert(country_id < COUNTRY_CNT);
  return ipmeta_geo_maxmind_country_continent[country_id];
}

int ipmeta_geo_get_maxmind_country_continent_list(const char ***continents)
{
  *continents = ipmeta_geo_maxmind_country_continent;
  return COUNTRY_CNT;
}
