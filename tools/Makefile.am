#
# Top-level Makefile.am for Traffic Server.
#
#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information
#  regarding copyright ownership.  The ASF licenses this file
#  to you under the Apache License, Version 2.0 (the
#  "License"); you may not use this file except in compliance
#  with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

bin_SCRIPTS = tsxs
bin_PROGRAMS = 

AM_CPPFLAGS = $(iocore_include_dirs) \
  -I$(top_srcdir)/lib -I$(top_builddir)/lib \
  -I$(top_srcdir)/lib/ts -I$(top_builddir)/lib/ts

noinst_PROGRAMS = jtest/jtest

jtest_jtest_SOURCES = jtest/jtest.cc
jtest_jtest_LDADD = $(top_builddir)/lib/ts/libtsutil.la \
    @LIBTOOL_LINK_FLAGS@ \
    @LIBSOCKET@ @LIBNSL@ \
    @LIBRT@ @LIBDL@


if BUILD_HTTP_LOAD
noinst_PROGRAMS += http_load/http_load

http_load_http_load_LDADD = @LIBSSL@
http_load_http_load_SOURCES =  \
  http_load/http_load.c \
  http_load/timers.c \
  http_load/port.h \
  http_load/timers.h

endif

# We only build tstop if libcurses and libcurl are available.
if BUILD_TSTOP

bin_PROGRAMS += tstop/tstop

tstop_tstop_CPPFLAGS = \
  -I$(top_srcdir)/mgmt/api/include
tstop_tstop_CXXFLAGS = \
  @CURL_CFLAGS@

tstop_tstop_SOURCES = tstop/tstop.cc
tstop_tstop_LDADD = \
  $(top_builddir)/mgmt/api/remote/libtsmgmt.la \
  @CURL_LIBS@ @CURSES_LIB@

endif
