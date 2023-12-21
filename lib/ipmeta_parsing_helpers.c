/*
 * libipmeta
 *
 * Alistair King and Ken Keys, CAIDA, UC San Diego
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

#include "ipmeta_parsing_helpers.h"

const char *country_code_iso2[] = {
  "??",
  "AP",
  "EU",
  "AD",
  "AE",
  "AF",
  "AG",
  "AI",
  "AL",
  "AM",
  "CW",
  "AO",
  "AQ",
  "AR",
  "AS",
  "AT",
  "AU",
  "AW",
  "AZ",
  "BA",
  "BB",
  "BD",
  "BE",
  "BF",
  "BG",
  "BH",
  "BI",
  "BJ",
  "BM",
  "BN",
  "BO",
  "BR",
  "BS",
  "BT",
  "BV",
  "BW",
  "BY",
  "BZ",
  "CA",
  "CC",
  "CD",
  "CF",
  "CG",
  "CH",
  "CI",
  "CK",
  "CL",
  "CM",
  "CN",
  "CO",
  "CR",
  "CU",
  "CV",
  "CX",
  "CY",
  "CZ",
  "DE",
  "DJ",
  "DK",
  "DM",
  "DO",
  "DZ",
  "EC",
  "EE",
  "EG",
  "EH",
  "ER",
  "ES",
  "ET",
  "FI",
  "FJ",
  "FK",
  "FM",
  "FO",
  "FR",
  "SX",
  "GA",
  "GB",
  "GD",
  "GE",
  "GF",
  "GH",
  "GI",
  "GL",
  "GM",
  "GN",
  "GP",
  "GQ",
  "GR",
  "GS",
  "GT",
  "GU",
  "GW",
  "GY",
  "HK",
  "HM",
  "HN",
  "HR",
  "HT",
  "HU",
  "ID",
  "IE",
  "IL",
  "IN",
  "IO",
  "IQ",
  "IR",
  "IS",
  "IT",
  "JM",
  "JO",
  "JP",
  "KE",
  "KG",
  "KH",
  "KI",
  "KM",
  "KN",
  "KP",
  "KR",
  "KW",
  "KY",
  "KZ",
  "LA",
  "LB",
  "LC",
  "LI",
  "LK",
  "LR",
  "LS",
  "LT",
  "LU",
  "LV",
  "LY",
  "MA",
  "MC",
  "MD",
  "MG",
  "MH",
  "MK",
  "ML",
  "MM",
  "MN",
  "MO",
  "MP",
  "MQ",
  "MR",
  "MS",
  "MT",
  "MU",
  "MV",
  "MW",
  "MX",
  "MY",
  "MZ",
  "NA",
  "NC",
  "NE",
  "NF",
  "NG",
  "NI",
  "NL",
  "NO",
  "NP",
  "NR",
  "NU",
  "NZ",
  "OM",
  "PA",
  "PE",
  "PF",
  "PG",
  "PH",
  "PK",
  "PL",
  "PM",
  "PN",
  "PR",
  "PS",
  "PT",
  "PW",
  "PY",
  "QA",
  "RE",
  "RO",
  "RU",
  "RW",
  "SA",
  "SB",
  "SC",
  "SD",
  "SE",
  "SG",
  "SH",
  "SI",
  "SJ",
  "SK",
  "SL",
  "SM",
  "SN",
  "SO",
  "SR",
  "ST",
  "SV",
  "SY",
  "SZ",
  "TC",
  "TD",
  "TF",
  "TG",
  "TH",
  "TJ",
  "TK",
  "TM",
  "TN",
  "TO",
  "TL",
  "TR",
  "TT",
  "TV",
  "TW",
  "TZ",
  "UA",
  "UG",
  "UM",
  "US",
  "UY",
  "UZ",
  "VA",
  "VC",
  "VE",
  "VG",
  "VI",
  "VN",
  "VU",
  "WF",
  "WS",
  "YE",
  "YT",
  "RS",
  "ZA",
  "ZM",
  "ME",
  "ZW",
  "A1",
  "A2",
  "O1",
  "AX",
  "GG",
  "IM",
  "JE",
  "BL",
  "MF",
  "BQ",
  "SS",
  /* Alistair adds AN because Maxmind does not include it, but uses it */
  "AN",
  /* temporary code for Kosovo -- will change if ISO recognizes Kosovo in the
   * future. */
  "XK",
};

const char *country_continent[] = {
  "??",
  "AS",
  "EU",
  "EU",
  "AS",
  "AS",
  "NA",
  "NA",
  "EU",
  "AS",
  "NA",
  "AF",
  "AN",
  "SA",
  "OC",
  "EU",
  "OC",
  "NA",
  "AS",
  "EU",
  "NA",
  "AS",
  "EU",
  "AF",
  "EU",
  "AS",
  "AF",
  "AF",
  "NA",
  "AS",
  "SA",
  "SA",
  "NA",
  "AS",
  "AN",
  "AF",
  "EU",
  "NA",
  "NA",
  "AS",
  "AF",
  "AF",
  "AF",
  "EU",
  "AF",
  "OC",
  "SA",
  "AF",
  "AS",
  "SA",
  "NA",
  "NA",
  "AF",
  "AS",
  "AS",
  "EU",
  "EU",
  "AF",
  "EU",
  "NA",
  "NA",
  "AF",
  "SA",
  "EU",
  "AF",
  "AF",
  "AF",
  "EU",
  "AF",
  "EU",
  "OC",
  "SA",
  "OC",
  "EU",
  "EU",
  "NA",
  "AF",
  "EU",
  "NA",
  "AS",
  "SA",
  "AF",
  "EU",
  "NA",
  "AF",
  "AF",
  "NA",
  "AF",
  "EU",
  "AN",
  "NA",
  "OC",
  "AF",
  "SA",
  "AS",
  "AN",
  "NA",
  "EU",
  "NA",
  "EU",
  "AS",
  "EU",
  "AS",
  "AS",
  "AS",
  "AS",
  "AS",
  "EU",
  "EU",
  "NA",
  "AS",
  "AS",
  "AF",
  "AS",
  "AS",
  "OC",
  "AF",
  "NA",
  "AS",
  "AS",
  "AS",
  "NA",
  "AS",
  "AS",
  "AS",
  "NA",
  "EU",
  "AS",
  "AF",
  "AF",
  "EU",
  "EU",
  "EU",
  "AF",
  "AF",
  "EU",
  "EU",
  "AF",
  "OC",
  "EU",
  "AF",
  "AS",
  "AS",
  "AS",
  "OC",
  "NA",
  "AF",
  "NA",
  "EU",
  "AF",
  "AS",
  "AF",
  "NA",
  "AS",
  "AF",
  "AF",
  "OC",
  "AF",
  "OC",
  "AF",
  "NA",
  "EU",
  "EU",
  "AS",
  "OC",
  "OC",
  "OC",
  "AS",
  "NA",
  "SA",
  "OC",
  "OC",
  "AS",
  "AS",
  "EU",
  "NA",
  "OC",
  "NA",
  "AS",
  "EU",
  "OC",
  "SA",
  "AS",
  "AF",
  "EU",
  "EU",
  "AF",
  "AS",
  "OC",
  "AF",
  "AF",
  "EU",
  "AS",
  "AF",
  "EU",
  "EU",
  "EU",
  "AF",
  "EU",
  "AF",
  "AF",
  "SA",
  "AF",
  "NA",
  "AS",
  "AF",
  "NA",
  "AF",
  "AN",
  "AF",
  "AS",
  "AS",
  "OC",
  "AS",
  "AF",
  "OC",
  "AS",
  "EU",
  "NA",
  "OC",
  "AS",
  "AF",
  "EU",
  "AF",
  "OC",
  "NA",
  "SA",
  "AS",
  "EU",
  "NA",
  "SA",
  "NA",
  "NA",
  "AS",
  "OC",
  "OC",
  "OC",
  "AS",
  "AF",
  "EU",
  "AF",
  "AF",
  "EU",
  "AF",
  "??",
  "??",
  "??",
  "EU",
  "EU",
  "EU",
  "EU",
  "NA",
  "NA",
  "NA",
  "AF",
  /* see above about AN */
  "NA",
  /* see above about XK */
  "EU",
};
