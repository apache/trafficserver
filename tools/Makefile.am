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

bin_SCRIPTS = tsxs tspush

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = trafficserver.pc

AM_LDFLAGS = @EXTRA_CXX_LDFLAGS@ @LIBTOOL_LINK_FLAGS@
AM_CPPFLAGS = $(iocore_include_dirs) \
  -I$(top_srcdir)/lib \
  -I$(top_srcdir)/lib/wccp

if BUILD_TEST_TOOLS
bin_PROGRAMS = jtest/jtest
else
noinst_PROGRAMS = jtest/jtest
endif

jtest_jtest_SOURCES = jtest/jtest.cc
jtest_jtest_LDADD = $(top_builddir)/lib/ts/libtsutil.la @OPENSSL_LIBS@

if BUILD_HTTP_LOAD

if BUILD_TEST_TOOLS
bin_PROGRAMS += http_load/http_load
else
noinst_PROGRAMS += http_load/http_load
endif

http_load_http_load_LDADD = @OPENSSL_LIBS@
http_load_http_load_SOURCES =  \
  http_load/http_load.c \
  http_load/timers.c \
  http_load/port.h \
  http_load/timers.h

endif

all-am: Makefile $(PROGRAMS) $(SCRIPTS) $(DATA)
	@sed "s/ -fPIE//" tsxs > tsxs.new
	@mv -f tsxs.new tsxs
