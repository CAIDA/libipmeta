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
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <wandio.h>

#include "libipmeta.h"
#include "utils.h"
#include "wandio_utils.h"

/** The length of the static line buffer */
#define BUFFER_LEN 1024

#define DEFAULT_COMPRESS_LEVEL 6

ipmeta_t *ipmeta = NULL;
ipmeta_provider_t *enabled_providers[IPMETA_PROVIDER_MAX];
char *provider_prefixes[IPMETA_PROVIDER_MAX];
int enabled_providers_cnt = 0;

static void lookup(char *addr_str, iow_t *outfile)
{
  uint32_t addr;
  uint8_t mask;
  int i;

  /* convert the prefix string to net/mask integers integer */
  char *pref_str = strdup(addr_str);	
  addr = inet_addr(strsep(&pref_str, "/"));
  mask = (pref_str && strlen(pref_str))?atoi(pref_str):32;

  // For now recycle the records each time
  ipmeta_record_set_t *recs = ipmeta_record_set_init();

  /* look it up using each provider */
  for(i = 0; i < enabled_providers_cnt; i++)
    {
      if(outfile == NULL)
	{
	  if(enabled_providers_cnt > 1)
	    {
	      fprintf(stdout, "%s|",
		      ipmeta_get_provider_name(enabled_providers[i]));
	    }

	    ipmeta_lookup(enabled_providers[i], addr, mask, recs);
	    ipmeta_dump_record_set(recs, addr_str);
	}
      else
	{
	  if(enabled_providers_cnt > 1)
	    {
	      wandio_printf(outfile, "%s|",
			    ipmeta_get_provider_name(enabled_providers[i]));
	    }

	    ipmeta_lookup(enabled_providers[i], addr, mask, recs);
	    ipmeta_write_record_set(recs, outfile, addr_str);
	}
    }

  ipmeta_record_set_free(&recs);

  return;
}

static void usage(const char *name)
{
  assert(ipmeta != NULL);
  ipmeta_provider_t **providers = NULL;
  int i;

  fprintf(stderr,
	  "usage: %s [-h] -p provider [-p provider] [-o outfile] [-f iplist]|[ip1 ip2...ipN]\n"
	  "       -c <level>    the compression level to use (default: %d)\n"
	  "       -f <iplist>   perform lookups on IP addresses listed in "
	  "the given file\n"
	  "       -h            write out a header row with field names\n"
	  "       -o <outfile>  write results to the given file\n"
	  "       -p <provider> enable the given provider,\n"
	  "                     -p can be used multiple times\n"
	  "                     available providers:\n",
	  name,
	  DEFAULT_COMPRESS_LEVEL);
  /* get the available plugins from ipmeta */
  providers = ipmeta_get_all_providers(ipmeta);

  for(i = 0; i < IPMETA_PROVIDER_MAX; i++)
    {
      assert(providers[i] != NULL);
      assert(ipmeta_get_provider_name(providers[i]));
      fprintf(stderr, "                      - %s\n",
	      ipmeta_get_provider_name(providers[i]));
    }
}

int main(int argc, char **argv)
{
  int rc = -1;
  int i;
  int opt;
  int prevoptind;
  /* we MUST not use any of the getopt global vars outside of arg parsing */
  /* this is because the plugins can use get opt to parse their config */
  int lastopt;

  char *ip_file = NULL;
  io_t *file = NULL;
  char *p = NULL;
  char buffer[BUFFER_LEN];

  char *providers[IPMETA_PROVIDER_MAX];
  int providers_cnt = 0;
  char *provider_arg_ptr = NULL;
  ipmeta_provider_t *provider = NULL;

  int headers_enabled = 0;

  int compress_level = DEFAULT_COMPRESS_LEVEL;
  char *outfile_name = NULL;
  iow_t *outfile = NULL;

  /* this must be called before usage is called */
  if((ipmeta = ipmeta_init()) == NULL)
    {
      fprintf(stderr, "could not initialize libipmeta\n");
      goto quit;
    }

  /* initialize the providers array to NULL first */
  memset(providers, 0, sizeof(char*)*IPMETA_PROVIDER_MAX);

  while(prevoptind = optind,
	(opt = getopt(argc, argv, ":c:f:o:p:hv?")) >= 0)
    {
      if (optind == prevoptind + 2 && *optarg == '-' ) {
        opt = ':';
        -- optind;
      }
      switch(opt)
	{
	case 'c':
	  compress_level = atoi(optarg);
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

	case ':':
	  fprintf(stderr, "ERROR: Missing option argument for -%c\n", optopt);
	  usage(argv[0]);
	  return -1;
	  break;

	case '?':
	case 'v':
	  fprintf(stderr, "libipmeta version %d.%d.%d\n",
		  LIBIPMETA_MAJOR_VERSION,
		  LIBIPMETA_MID_VERSION,
		  LIBIPMETA_MINOR_VERSION);
	  usage(argv[0]);
	  goto quit;
	  break;

	default:
	  usage(argv[0]);
	  goto quit;
	}
    }

  /* store the value of the last index*/
  lastopt = optind;

  /* reset getopt for others */
  optind = 1;

  /* -- call NO library functions which may use getopt before here -- */
  /* this ESPECIALLY means ipmeta_enable_provider */

  /* ensure there is at least one provider given */
  if(providers_cnt == 0)
    {
      fprintf(stderr,
	      "ERROR: At least one provider must be selected using -p\n");
      usage(argv[0]);
      goto quit;
    }

  /* ensure there is either a ip file list, or some addresses on the cmd line */
  if(ip_file == NULL && (lastopt >= argc))
    {
      fprintf(stderr,
	      "ERROR: IP addresses must either be provided in a file "
	      "(using -f), or directly\n\ton the command line\n");
      usage(argv[0]);
      goto quit;
    }

  /* if we have been given a file to write to, open this now */
  if(outfile_name != NULL)
    {
      if((outfile = wandio_wcreate(outfile_name,
				   wandio_detect_compression_type(outfile_name),
				   compress_level,
				   O_CREAT)) == NULL)
	{
	  fprintf(stderr, "ERROR: Could not open %s for writing\n",
		  outfile_name);
	  goto quit;
	}
    }

  for(i=0;i<providers_cnt;i++)
    {
      /* the string at providers[i] will contain the name of the plugin,
	 optionally followed by a space and then the arguments to pass
	 to the plugin */
      if((provider_arg_ptr = strchr(providers[i], ' ')) != NULL)
	{
	  /* set the space to a nul, which allows providers[i] to be used
	     for the provider name, and then increment plugin_arg_ptr to
	     point to the next character, which will be the start of the
	     arg string (or at worst case, the terminating \0 */
	  *provider_arg_ptr = '\0';
	  provider_arg_ptr++;
	}

      /* lookup the provider using the name given */
      if((provider = ipmeta_get_provider_by_name(ipmeta, providers[i])) == NULL)
	{
	  fprintf(stderr, "ERROR: Invalid provider name (%s)\n",
		  providers[i]);
	  usage(argv[0]);
	  goto quit;
	}

      if(ipmeta_enable_provider(ipmeta, provider,
				provider_arg_ptr,
				IPMETA_PROVIDER_DEFAULT_NO) != 0)
	{
	  fprintf(stderr, "ERROR: Could not enable plugin %s\n",
		  providers[i]);
	  usage(argv[0]);
	  goto quit;
	}

      enabled_providers[enabled_providers_cnt++] = provider;
    }

  ipmeta_log(__func__, "dumping record headers");

  /* dump out the record header first */
  if(headers_enabled != 0)
    {
      if(enabled_providers_cnt > 1)
	{
	  if(outfile != NULL)
	    {
	      wandio_printf(outfile, "provider|");
	    }
	  else
	    {
	      fprintf(stdout, "provider|");
	    }
	}

      if(outfile != NULL)
	{
	  ipmeta_write_record_header(outfile);
	}
      else
	{
	  ipmeta_dump_record_header();
	}

    }

  ipmeta_log(__func__, "processing ip file");

  /* try reading the file first */
  if(ip_file != NULL)
    {
      /* open the file */
      if((file = wandio_create(ip_file)) == NULL)
	{
	  fprintf(stderr, "ERROR: Could not open IP file (%s)\n", ip_file);
	  usage(argv[0]);
	  goto quit;
	}

      while(wandio_fgets(file, &buffer, BUFFER_LEN, 1) > 0)
	{
	  /* treat # as comment line, and ignore empty lines */
	  if(buffer[0] == '#' || buffer[0] == '\0')
	    {
	      continue;
	    }

	  /* convenience to allow flowtuple files to be fed directly in */
	  if((p = strchr(buffer, '|')) != NULL)
	    {
	      *p = '\0';
	    }

	  lookup(buffer, outfile);
	}
    }

  ipmeta_log(__func__, "processing ips on command line");

  /* now try looking up addresses given on the command line */
  for(i=lastopt; i<argc; i++)
    {
      lookup(argv[i], outfile);
    }

  ipmeta_log(__func__, "done");

  rc=0;

 quit:
  for(i=0;i<providers_cnt;i++)
    {
      if(providers[i] != NULL)
	{
	  free(providers[i]);
	}
    }

  if(ip_file != NULL)
    {
      free(ip_file);
    }

  if(outfile_name != NULL)
    {
      free(outfile_name);
    }

  if(ipmeta != NULL)
    {
      ipmeta_free(ipmeta);
    }

  if(file != NULL)
    {
      wandio_destroy(file);
    }

  if(outfile != NULL)
    {
      wandio_wdestroy(outfile);
    }

  /* default rc is -1 */
  return rc;
}
