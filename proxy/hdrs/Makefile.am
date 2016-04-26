# Makefile.am for Hdrs
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

AM_CPPFLAGS = \
  $(iocore_include_dirs) \
  -I$(top_srcdir)/lib \
  -I$(top_srcdir)/lib/records

noinst_LIBRARIES = libhdrs.a
EXTRA_PROGRAMS = load_http_hdr

# Http library source files.
libhdrs_a_SOURCES = \
  HTTP.cc \
  HTTP.h \
  HdrHeap.cc \
  HdrHeap.h \
  HdrTSOnly.cc \
  HdrToken.cc \
  HdrToken.h \
  HdrUtils.cc \
  HdrUtils.h \
  HttpCompat.cc \
  HttpCompat.h \
  MIME.cc \
  MIME.h \
  URL.cc \
  URL.h

if BUILD_TESTS
  libhdrs_a_SOURCES += \
    HdrTest.cc \
    HdrTest.h
endif

load_http_hdr_SOURCES = \
  HTTP.h \
  HdrHeap.h \
  MIME.h \
  load_http_hdr.cc

load_http_hdr_LDADD = -L. -lhdrs \
  $(top_builddir)/lib/ts/libtsutil.la \
  @LIBTCL@
load_http_hdr_LDFLAGS = @EXTRA_CXX_LDFLAGS@ @LIBTOOL_LINK_FLAGS@

check_PROGRAMS = test_mime

TESTS = test_mime

test_mime_LDADD = -L. -lhdrs \
  $(top_builddir)/lib/ts/libtsutil.la \
  $(top_builddir)/iocore/eventsystem/libinkevent.a \
  $(top_builddir)/lib/records/librecords_p.a \
  $(top_builddir)/mgmt/libmgmt_p.la \
  $(top_builddir)/proxy/shared/libUglyLogStubs.a \
  @HWLOC_LIBS@
test_mime_LDFLAGS = @EXTRA_CXX_LDFLAGS@ @LIBTOOL_LINK_FLAGS@

test_mime_SOURCES = test_mime.cc

#test_UNUSED_SOURCES = \
#  test_header.cc \
#  test_urlhash.cc
