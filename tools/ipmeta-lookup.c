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

#include "config.h"

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <wandio.h>

#include "libipmeta.h"
#include "ipmeta_ds.h"
#include "utils.h"

/** The length of the static line buffer */
#define BUFFER_LEN 1024

#define DEFAULT_COMPRESS_LEVEL 6

static ipmeta_t *ipmeta = NULL;
static uint32_t providermask = 0;
static ipmeta_provider_t *enabled_providers[IPMETA_PROVIDER_MAX];
static int enabled_providers_cnt = 0;
static ipmeta_record_set_t *records;

static int lookup(const char *addr_str, iow_t *outfile)
{
  char output_prefix[BUFFER_LEN];

  if (ipmeta_lookup(ipmeta, addr_str, providermask, records) < 0) {
    fprintf(stderr, "ERROR: invalid address or prefix \"%s\"\n", addr_str);
    return -1;
  }

  /* look it up using each provider */
  for (int id = 1; id <= IPMETA_PROVIDER_MAX; id++) {
    if ((providermask & IPMETA_PROV_TO_MASK(id)) == 0) {
      continue;
    }

    snprintf(output_prefix, sizeof(output_prefix), "%s|%s",
      ipmeta_get_provider_name(ipmeta_get_provider_by_id(ipmeta, id)),
      addr_str);
    ipmeta_write_record_set_by_provider(records, outfile, output_prefix, id);
  }

  return 0;
}

static void usage(const char *name)
{
  assert(ipmeta != NULL);
  ipmeta_provider_t **providers = NULL;
  int i;

  // skip directory part of name
  const char *p;
  while ((p = strchr(name, '/')))
    name = p + 1;

  const char **dsnames = ipmeta_ds_get_all();
  fprintf(stderr,
      "usage: %s {-p provider}... [<other options>] [-f infile] [addr...]\n"
      "options:\n"
      "    -p <provider> enable the given provider (repeatable).\n"
      "                  Use \"-p'<provider> -?'\" for help with provider.\n"
      "                  Available providers:\n",
      name);
  /* get the available plugins from ipmeta */
  providers = ipmeta_get_all_providers(ipmeta);
  for (i = 0; i < IPMETA_PROVIDER_MAX; i++) {
    assert(providers[i] != NULL);
    assert(ipmeta_get_provider_name(providers[i]));
    fprintf(stderr, "                   - %s\n",
            ipmeta_get_provider_name(providers[i]));
  }
  fprintf(stderr,
      "    -D <struct>   data structure to use for storing prefixes\n"
      "                  (default: %s)\n"
      "                  Available datastructures:\n",
      dsnames[IPMETA_DS_DEFAULT-1]);
  for (i = 0; i < IPMETA_DS_MAX; i++) {
    fprintf(stderr, "                   - %s\n", dsnames[i]);
  }
  free(dsnames);
  fprintf(stderr,
      "    -h            write out a header row with field names\n"
      "    -o <outfile>  write results to the given file\n"
      "    -c <level>    compression level to use for <outfile> "
      "(default: %d)\n"
      "    -f <infile>   look up addresses or prefixes listed in <infile>\n"
      "    <addr>        look up the given address or prefix\n",
      DEFAULT_COMPRESS_LEVEL);
}

int main(int argc, char **argv)
{
  int rc = 1; // default to error
  int i;
  int opt;
  /* we MUST not use any of the getopt global vars outside of arg parsing */
  /* this is because the plugins can use get opt to parse their config */
  int lastopt;
  int error = 0;

  char *ip_file = NULL;
  io_t *file = NULL;
  char *p = NULL;
  char buffer[BUFFER_LEN];

  char *providers[IPMETA_PROVIDER_MAX];
  int providers_cnt = 0;
  char *provider_arg_ptr = NULL;
  ipmeta_provider_t *provider = NULL;

  records = ipmeta_record_set_init();
  assert(records != NULL);

  int headers_enabled = 0;

  int compress_level = DEFAULT_COMPRESS_LEVEL;
  char *outfile_name = NULL;
  iow_t *outfile = NULL;
  ipmeta_ds_id_t dstype = IPMETA_DS_DEFAULT;

  /* initialize the providers array to NULL first */
  memset(providers, 0, sizeof(char *) * IPMETA_PROVIDER_MAX);

  while ((opt = getopt(argc, argv, "D:c:f:o:p:hv?")) >= 0) {
    switch (opt) {
    case 'c':
      compress_level = atoi(optarg);
      break;

    case 'D':
      if ((dstype = ipmeta_ds_name_to_id(optarg)) == IPMETA_DS_NONE) {
        fprintf(stderr, "unknown data structure type \"%s\"\n", optarg);
        dstype = IPMETA_DS_DEFAULT;
        error = 1;
      }
      break;

    case 'f':
      ip_file = strdup(optarg);
      break;

    case 'h':
      headers_enabled = 1;
      break;

    case 'o':
      outfile_name = strdup(optarg);
      break;

    case 'p':
      providers[providers_cnt++] = strdup(optarg);
      break;

    case 'v':
      fprintf(stderr, "libipmeta package version %s\n", PACKAGE_VERSION);
      goto quit;

    case '?':
    default:
      error = 1;
      break;
    }
  }

  /* this must be called before usage is called */
  if ((ipmeta = ipmeta_init(dstype)) == NULL) {
    fprintf(stderr, "could not initialize libipmeta\n");
    goto quit;
  }

  if (error) {
    usage(argv[0]);
    goto quit;
  }

  /* store the value of the last index*/
  lastopt = optind;

  /* reset getopt for others */
  optind = 1;

  /* -- call NO library functions which may use getopt before here -- */
  /* this ESPECIALLY means ipmeta_enable_provider */

  /* ensure there is at least one provider given */
  if (providers_cnt == 0) {
    fprintf(stderr, "ERROR: At least one provider must be selected using -p\n");
    usage(argv[0]);
    goto quit;
  }

  /* if we have been given a file to write to, open this now */
  if (outfile_name != NULL) {
    if ((outfile = wandio_wcreate(outfile_name,
                                  wandio_detect_compression_type(outfile_name),
                                  compress_level, O_CREAT)) == NULL) {
      fprintf(stderr, "ERROR: Could not open %s for writing\n", outfile_name);
      goto quit;
    }
  }

  for (i = 0; i < providers_cnt; i++) {
    /* the string at providers[i] will contain the name of the plugin,
       optionally followed by a space and then the arguments to pass
       to the plugin */
    if ((provider_arg_ptr = strchr(providers[i], ' ')) != NULL) {
      /* set the space to a nul, which allows providers[i] to be used
         for the provider name, and then increment plugin_arg_ptr to
         point to the next character, which will be the start of the
         arg string (or at worst case, the terminating \0 */
      *provider_arg_ptr = '\0';
      provider_arg_ptr++;
    }

    /* lookup the provider using the name given */
    if ((provider = ipmeta_get_provider_by_name(ipmeta, providers[i])) ==
        NULL) {
      fprintf(stderr, "ERROR: Invalid provider name (%s)\n", providers[i]);
      usage(argv[0]);
      goto quit;
    }

    if (ipmeta_enable_provider(ipmeta, provider, provider_arg_ptr) != 0) {
      fprintf(stderr, "ERROR: Could not enable plugin %s\n", providers[i]);
      goto quit;
    }
    providermask |= IPMETA_PROV_TO_MASK(ipmeta_get_provider_id(provider));
    enabled_providers[enabled_providers_cnt++] = provider;
  }

  /* ensure there is either a ip file list, or some addresses on the cmd line */
  if (ip_file == NULL && (lastopt >= argc)) {
    fprintf(stderr, "ERROR: IP addresses must either be provided in a file "
                    "(using -f), or directly\n\ton the command line\n");
    usage(argv[0]);
    goto quit;
  }

  /* dump out the record header first */
  if (headers_enabled) {
    ipmeta_printf(outfile, "provider|");
    ipmeta_write_record_header(outfile);
  }

  rc = 0; // default to success

  /* try reading the file first */
  if (ip_file != NULL) {
    ipmeta_log(__func__, "processing ip file %s", ip_file);
    /* open the file */
    if ((file = wandio_create(ip_file)) == NULL) {
      fprintf(stderr, "ERROR: Could not open input file %s: %s\n", ip_file,
          strerror(errno));
      rc = 1;
    } else {
      while (wandio_fgets(file, &buffer, BUFFER_LEN, 1) > 0) {
        /* treat # as comment line, and ignore empty lines */
        if (buffer[0] == '#' || buffer[0] == '\0') {
          continue;
        }

        /* convenience to allow flowtuple files to be fed directly in */
        if ((p = strchr(buffer, '|')) != NULL) {
          *p = '\0';
        }

        if (lookup(buffer, outfile) != 0)
          rc = 1;
      }
    }
  }

  if (lastopt < argc) {
    ipmeta_log(__func__, "processing ips on command line");

    /* now try looking up addresses given on the command line */
    for (i = lastopt; i < argc; i++) {
      if (lookup(argv[i], outfile) != 0)
        rc = 1;
    }
  }

  ipmeta_log(__func__, "done");

quit:
  for (i = 0; i < providers_cnt; i++) {
    if (providers[i] != NULL) {
      free(providers[i]);
    }
  }

  if (ip_file != NULL) {
    free(ip_file);
  }

  if (outfile_name != NULL) {
    free(outfile_name);
  }

  if (ipmeta != NULL) {
    ipmeta_free(ipmeta);
  }

  if (file != NULL) {
    wandio_destroy(file);
  }

  if (outfile != NULL) {
    wandio_wdestroy(outfile);
  }

  if (records != NULL) {
    ipmeta_record_set_free(&records);
  }

  /* default rc is -1 */
  return rc;
}
