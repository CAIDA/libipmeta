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

#include "libipmeta_int.h"
#include "ipmeta_provider_maxmind.h"

#define PROVIDER_NAME "maxmind"

#define STATE(provname)				\
  (IPMETA_MAXMIND_STATE(maxmind, provname))

static ipmeta_provider_t ipmeta_provider_maxmind = {
  IPMETA_PROVIDER_MAXMIND,
  PROVIDER_NAME,
  IPMETA_PROVIDER_GENERATE_PTRS(maxmind)
};

ipmeta_provider_t *ipmeta_provider_maxmind_alloc()
{
  return &ipmeta_provider_maxmind;
}

int ipmeta_provider_maxmind_init(ipmeta_provider_t *provider,
				 int argc, char ** argv)
{
  return 0;
}

void ipmeta_provider_maxmind_free(ipmeta_provider_t *provider)
{
  /* we have nothing in the provider->state field, so nothing to free */
  return;
}

ipmeta_record_t *ipmeta_provider_maxmind_lookup(ipmeta_provider_t *provider,
						uint32_t addr)
{
  /* just call the lookup helper func in provider manager */
  return NULL;
}

/* ----- Class Helper Functions below here ------ */

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
