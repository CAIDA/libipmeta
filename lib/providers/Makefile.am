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

SUBDIRS =

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/common \
	-I$(top_srcdir)/common/libpatricia \
	-I$(top_srcdir)/common/libcsv \
	-I$(top_srcdir)/lib

noinst_LTLIBRARIES = libipmeta_providers.la

include_HEADERS =

libipmeta_providers_la_SOURCES = \
	ipmeta_provider_netacq_edge.c \
	ipmeta_provider_netacq_edge.h \
	ipmeta_provider_pfx2as.c \
	ipmeta_provider_pfx2as.h \
	ipmeta_provider_maxmind.c \
	ipmeta_provider_maxmind.h 

libipmeta_providers_la_LIBADD =

ACLOCAL_AMFLAGS = -I m4

CLEANFILES = *~
