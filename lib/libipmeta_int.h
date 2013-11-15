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


#ifndef __LIBIPMETA_INT_H
#define __LIBIPMETA_INT_H

#include <inttypes.h>

#include "khash.h"

#include "libipmeta.h"

/** @file
 *
 * @brief Header file that contains the private components of libipmeta.
 *
 * @author Alistair King
 *
 */

KHASH_MAP_INIT_INT(ipmeta_rechash, struct ipmeta_record *)

/**
 * @name Internal Datastructures
 *
 * These datastructures are internal to libipmeta. Some may be exposed as opaque
 * structures by libipmeta.h (e.g ipmeta_t)
 *
 * @{ */

/** Structure which holds state for a libipmeta instance */
struct ipmeta
{

  /** Array of metadata providers
   * @note index of provider is given by (ipmeta_provider_id_t - 1)
   */
  struct ipmeta_provider *providers[IPMETA_PROVIDER_MAX];

  /** Default metadata provider */
  struct ipmeta_provider *provider_default;

};

/** @} */

#endif /* __LIBIPMETA_INT_H */
