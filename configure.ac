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
# This software is Copyright (c) 2013 The Regents of the University of
# California. All Rights Reserved. Permission to copy, modify, and distribute this
# software and its documentation for academic research and education purposes,
# without fee, and without a written agreement is hereby granted, provided that
# the above copyright notice, this paragraph and the following three paragraphs
# appear in all copies. Permission to make use of this software for other than
# academic research and education purposes may be obtained by contacting:
# 
# Office of Innovation and Commercialization
# 9500 Gilman Drive, Mail Code 0910
# University of California
# La Jolla, CA 92093-0910
# (858) 534-5815
# invent@ucsd.edu
# 
# This software program and documentation are copyrighted by The Regents of the
# University of California. The software program and documentation are supplied
# "as is", without any accompanying services from The Regents. The Regents does
# not warrant that the operation of the program will be uninterrupted or
# error-free. The end-user understands that the program was developed for research
# purposes and is advised not to rely exclusively on the program for any reason.
# 
# IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO ANY PARTY FOR
# DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING LOST
# PROFITS, ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF
# THE UNIVERSITY OF CALIFORNIA HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
# DAMAGE. THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY WARRANTIES,
# INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
# FITNESS FOR A PARTICULAR PURPOSE. THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS
# IS" BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATIONS TO PROVIDE
# MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
#

AC_PREREQ([2.68])

# Package version should use semantic versioning.
AC_INIT([libipmeta], [3.1.1], [software@caida.org])
AM_INIT_AUTOMAKE([foreign])

# Version numbers for the libtool-created library (libipmeta) are unrelated
# to the overall package version.  For details on Library Versioning, see
# https://www.sourceware.org/autobook/autobook/autobook_61.html
LIBIPMETA_LIBTOOL_VERSION=5:1:1

LT_INIT

AM_INIT_AUTOMAKE

AC_CONFIG_MACRO_DIR([m4])

AC_CONFIG_SRCDIR([lib/ipmeta.c])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_LIBTOOL
AC_PROG_CXX
AM_PROG_LIBTOOL
AC_PROG_CC_C99

AC_SYS_LARGEFILE

AH_VERBATIM([_GNU_SOURCE],
[/* Enable GNU extensions on systems that have them.  */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif])

AC_CHECK_FUNCS([gettimeofday memset strdup strstr strsep strlcpy vasprintf])

# should we dump debug output to stderr and not optmize the build?

AC_MSG_CHECKING([whether to build with debug information])
AC_ARG_ENABLE([debug],
    [AS_HELP_STRING([--enable-debug],
        [enable debug data generation (def=no)])],
    [debugit="$enableval"],
    [debugit=no])
AC_MSG_RESULT([$debugit])

if test x"$debugit" = x"yes"; then
    AC_DEFINE([DEBUG],[],[Debug Mode])
fi

# Checks for typedefs, structures, and compiler characteristics.
#AC_C_INLINE # 2014-07-23 AK removes because it causes problems with clang3.4
AC_TYPE_SIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_REALLOC

# Checks for libraries.
# we use libwandio for threaded IO
AC_SEARCH_LIBS([wandio_create], [wandio trace], [with_wandio=yes],
                 [AC_MSG_ERROR(
		 [libwandio required (included with libtrace since 3.0.15)]
		 )])
AM_CONDITIONAL([WITH_WANDIO], [test "x$with_wandio" == xyes])

# Checks for header files.
AC_CHECK_HEADERS([arpa/inet.h inttypes.h limits.h math.h stdlib.h string.h \
			      time.h sys/time.h])

# we may want to come back later and add compile-time configuration for things
# like datastructure providers, but for now it will all get compiled

AC_HEADER_ASSERT

AC_SUBST(LIBIPMETA_LIBTOOL_VERSION)

AC_CONFIG_FILES([Makefile
		common/Makefile
		common/libpatricia/Makefile
		common/libinterval3/rb_tree/Makefile
		common/libinterval3/Makefile
		common/libcsv/Makefile
		common/libjsmn/Makefile
		lib/Makefile
		lib/datastructures/Makefile
		lib/providers/Makefile
		tools/Makefile
		])
AC_OUTPUT
