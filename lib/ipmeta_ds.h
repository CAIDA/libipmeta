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
 * This software is Copyright (c) 2013 The Regents of the University of
 * California. All Rights Reserved. Permission to copy, modify, and distribute this
 * software and its documentation for academic research and education purposes,
 * without fee, and without a written agreement is hereby granted, provided that
 * the above copyright notice, this paragraph and the following three paragraphs
 * appear in all copies. Permission to make use of this software for other than
 * academic research and education purposes may be obtained by contacting:
 * 
 * Office of Innovation and Commercialization
 * 9500 Gilman Drive, Mail Code 0910
 * University of California
 * La Jolla, CA 92093-0910
 * (858) 534-5815
 * invent@ucsd.edu
 * 
 * This software program and documentation are copyrighted by The Regents of the
 * University of California. The software program and documentation are supplied
 * "as is", without any accompanying services from The Regents. The Regents does
 * not warrant that the operation of the program will be uninterrupted or
 * error-free. The end-user understands that the program was developed for research
 * purposes and is advised not to rely exclusively on the program for any reason.
 * 
 * IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING LOST
 * PROFITS, ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF
 * THE UNIVERSITY OF CALIFORNIA HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE. THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE. THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS
 * IS" BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATIONS TO PROVIDE
 * MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
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
