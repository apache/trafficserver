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

AM_CPPFLAGS += \
	-I$(abs_top_srcdir)/iocore/eventsystem \
	-I$(abs_top_srcdir)/iocore/io_uring \
	-I$(abs_top_srcdir)/include \
	-I$(abs_top_srcdir)/lib \
	@HWLOC_INCLUDES@ \
	@SWOC_INCLUDES@ \
	$(TS_INCLUDES)

TESTS = test_AIO.sample

noinst_LIBRARIES = libinkaio.a
check_PROGRAMS = test_AIO

libinkaio_a_SOURCES = \
	AIO.cc \
	I_AIO.h \
	Inline.cc \
	P_AIO.h \
	AIO_fault_injection.h \
	AIO_fault_injection.cc

test_AIO_LDFLAGS = \
	@AM_LDFLAGS@ \
	@OPENSSL_LDFLAGS@ @SWOC_LDFLAGS@

test_AIO_SOURCES = \
	test_AIO.cc

test_AIO_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	$(iocore_include_dirs) \
	-I$(abs_top_srcdir)/proxy \
	-I$(abs_top_srcdir)/proxy/hdrs \
	-I$(abs_top_srcdir)/proxy/http \
	-I$(abs_top_srcdir)/proxy/logging \
	@SWOC_INCLUDES@ @OPENSSL_INCLUDES@

test_AIO_LDADD = \
	libinkaio.a \
	$(top_builddir)/iocore/eventsystem/libinkevent.a \
	$(top_builddir)/src/records/librecords_p.a \
  @start_group@ \
	$(top_builddir)/src/tscore/libtscore.a \
	$(top_builddir)/src/api/libtsapicore.a \
	$(top_builddir)/src/tscpp/util/libtscpputil.la \
  @end_group@ \
	@SWOC_LIBS@ @HWLOC_LIBS@ @YAMLCPP_LIBS@ @LIBPCRE@ @LIBCAP@

include $(top_srcdir)/mk/tidy.mk

clang-tidy-local: $(DIST_SOURCES)
	$(CXX_Clang_Tidy)
