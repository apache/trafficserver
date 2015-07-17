# Makefile.am for the traffic/iocore/aio hierarchy
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
  -I$(top_srcdir)/iocore/eventsystem \
  -I$(top_srcdir)/lib \
  -I$(top_srcdir)/lib/records

TESTS = test_AIO.sample

noinst_LIBRARIES = libinkaio.a
check_PROGRAMS = test_AIO

libinkaio_a_SOURCES = \
  AIO.cc \
  I_AIO.h \
  Inline.cc \
  P_AIO.h

test_AIO_LDFLAGS = \
  @EXTRA_CXX_LDFLAGS@ \
  @LIBTOOL_LINK_FLAGS@ \
  @OPENSSL_LDFLAGS@

test_AIO_SOURCES = \
  test_AIO.cc

test_AIO_CPPFLAGS = \
  $(AM_CPPFLAGS) \
  $(iocore_include_dirs) \
  -I$(top_srcdir)/proxy/api/ts \
  -I$(top_srcdir)/proxy/api \
  -I$(top_srcdir)/proxy \
  -I$(top_srcdir)/proxy/hdrs \
  -I$(top_srcdir)/proxy/http \
  -I$(top_srcdir)/proxy/logging \
  -I$(top_srcdir)/mgmt \
  -I$(top_srcdir)/mgmt/utils \
  @OPENSSL_INCLUDES@

test_AIO_LDADD = \
  libinkaio.a \
  $(top_builddir)/lib/records/librecords_p.a \
  $(top_builddir)/mgmt/libmgmt_p.la \
  $(top_builddir)/iocore/eventsystem/libinkevent.a \
  $(top_builddir)/lib/ts/libtsutil.la \
  $(top_builddir)/proxy/shared/libUglyLogStubs.a \
  @LIBTCL@ @HWLOC_LIBS@
