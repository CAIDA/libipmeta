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

#ifndef __IPMETA_DS_H
#define __IPMETA_DS_H

#include "libipmeta_int.h"
#include "libipmeta.h"

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
#define IPMETA_DS_STATE(type, ds) ((ipmeta_ds_##type##_state_t *)(ds)->state)

/** Convenience macro that defines all the function prototypes for the ipmeta
 * datastructure API
 */
#define IPMETA_DS_GENERATE_PROTOS(datastructure)                               \
  ipmeta_ds_t *ipmeta_ds_##datastructure##_alloc(void);                        \
  int ipmeta_ds_##datastructure##_init(ipmeta_ds_t *ds);                       \
  void ipmeta_ds_##datastructure##_free(ipmeta_ds_t *ds);                      \
  int ipmeta_ds_##datastructure##_add_prefix(ipmeta_ds_t *ds, int family,      \
    void *addrp, uint8_t pfxlen, ipmeta_record_t *record);                     \
  int ipmeta_ds_##datastructure##_lookup_pfx(ipmeta_ds_t *ds, int family,      \
    void *addrp, uint8_t pfxlen, uint32_t providermask,                        \
    ipmeta_record_set_t *records);                                             \
  int ipmeta_ds_##datastructure##_lookup_addr(ipmeta_ds_t *ds, int family,     \
    void *addrp, uint32_t providermask, ipmeta_record_set_t *found);

/** Convenience macro that defines all the function pointers for the ipmeta
 * datastructure API
 */
#define IPMETA_DS_GENERATE_PTRS(datastructure)                                 \
  ipmeta_ds_##datastructure##_init, ipmeta_ds_##datastructure##_free,          \
    ipmeta_ds_##datastructure##_add_prefix,                                    \
    ipmeta_ds_##datastructure##_lookup_pfx,                                    \
    ipmeta_ds_##datastructure##_lookup_addr,

/** Structure which represents a metadata datastructure */
struct ipmeta_ds {
  /** The ID of this datastructure */
  enum ipmeta_ds_id id;

  /** The name of this datastructure */
  char *name;

  /** Pointer to init function */
  int (*init)(struct ipmeta_ds *ds);

  /** Pointer to free function */
  void (*free)(struct ipmeta_ds *ds);

  /** Pointer to add prefix function */
  int (*add_prefix)(struct ipmeta_ds *ds, int family, void *addrp,
                    uint8_t pfxlen, struct ipmeta_record *record);

  /** Pointer to lookup records function */
  int (*lookup_pfx)(struct ipmeta_ds *ds, int family, void *addrp,
                    uint8_t pfxlen, uint32_t providermask,
                    ipmeta_record_set_t *records);

  /** Pointer to lookup record single function */
  int (*lookup_addr)(struct ipmeta_ds *ds, int family, void *addrp,
                     uint32_t providermask, ipmeta_record_set_t *found);

  /** Pointer to a instance-specific state object */
  void *state;
};

/** Initialize the specified datastructure
 *
 * @param[out] ds       where to store the pointer to the datastructure
 * @param ds_id         id of the datastructure to initialize
 * @return 0 if initialization was successful, -1 otherwise
 */
int ipmeta_ds_init(struct ipmeta_ds **ds, ipmeta_ds_id_t ds_id);

#endif /* __IPMETA_DS_H */
