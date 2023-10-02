#
# libipmeta
#
# Alistair King, CAIDA, UC San Diego
# corsaro-info@caida.org
#
# Copyright (C) 2013-2020 The Regents of the University of California.
#
# This file is part of libipmeta.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice,
# this list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
# this list of conditions and the following disclaimer in the documentation
# and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#

SUBDIRS = datastructures providers
#providers
AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/common 	\
	-I$(top_srcdir)/common/libpatricia		\
	-I$(top_srcdir)/lib/datastructures 		\
	-I$(top_srcdir)/lib/providers

lib_LTLIBRARIES = libipmeta.la

include_HEADERS = libipmeta.h

libipmeta_la_SOURCES = 	\
	ipmeta.c 		\
	libipmeta.h		\
	libipmeta_int.h		\
	ipmeta_ds.c		\
	ipmeta_ds.h		\
	ipmeta_log.c		\
	ipmeta_provider.c	\
	ipmeta_provider.h \
    ipmeta_parsing_helpers.c \
    ipmeta_parsing_helpers.h

libipmeta_la_LIBADD = $(top_builddir)/common/libcccommon.la \
	$(top_builddir)/lib/datastructures/libipmeta_datastructures.la \
	$(top_builddir)/lib/providers/libipmeta_providers.la

libipmeta_la_LDFLAGS = -version-info @LIBIPMETA_LIBTOOL_VERSION@

ACLOCAL_AMFLAGS = -I m4

CLEANFILES = *~
