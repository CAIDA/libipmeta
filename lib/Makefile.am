#
# libipmeta
#
# Alistair King, CAIDA, UC San Diego
# corsaro-info@caida.org
#
# Copyright (C) 2012 The Regents of the University of California.
#
# This file is part of libipmeta.
#
# libipmeta is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# libipmeta is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with libipmeta.  If not, see <http://www.gnu.org/licenses/>.
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
	ipmeta_provider.h

libipmeta_la_LIBADD = $(top_builddir)/common/libipmetacommon.la \
	$(top_builddir)/lib/datastructures/libipmeta_datastructures.la \
	$(top_builddir)/lib/providers/libipmeta_providers.la

libipmeta_la_LDFLAGS = -version-info @LIBIPMETA_MAJOR_VERSION@:@LIBIPMETA_MID_VERSION@:@LIBIPMETA_MINOR_VERSION@

ACLOCAL_AMFLAGS = -I m4

CLEANFILES = *~
