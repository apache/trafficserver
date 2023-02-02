# Makefile.am for the traffic/iocore/hostdb hierarchy
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
	$(iocore_include_dirs) \
	-I$(abs_top_srcdir)/include \
	-I$(abs_top_srcdir)/lib \
	-I$(abs_top_srcdir)/proxy \
	-I$(abs_top_srcdir)/proxy/hdrs \
	-I$(abs_top_srcdir)/proxy/http \
	-I$(abs_top_srcdir)/mgmt \
	-I$(abs_top_srcdir)/mgmt/utils \
	@SWOC_INCLUDES@ \
	$(TS_INCLUDES)

EXTRA_DIST = I_HostDB.h

noinst_LIBRARIES = libinkhostdb.a

libinkhostdb_a_SOURCES = \
	HostDB.cc \
	I_HostDB.h \
	I_HostDBProcessor.h \
	Inline.cc \
	P_HostDB.h \
	P_HostDBProcessor.h \
	P_RefCountCache.h \
	P_RefCountCacheSerializer.h \
	RefCountCache.cc \
	HostFile.h \
	HostFile.cc \
	HostDBInfo.cc

TESTS = $(check_PROGRAMS)
check_PROGRAMS = test_RefCountCache test_HostFile

test_RefCountCache_SOURCES = \
	test_RefCountCache.cc

#test_UNUSED_SOURCES = \
#  test_I_HostDB.cc \
#  test_P_HostDB.cc

test_LD_FLAGS = \
	@AM_LDFLAGS@ \
	@SWOC_LDFLAGS@ @OPENSSL_LDFLAGS@ @YAMLCPP_LDFLAGS@

test_CPP_FLAGS = \
	$(AM_CPPFLAGS) \
	$(iocore_include_dirs) \
	-I$(abs_top_srcdir)/proxy \
	-I$(abs_top_srcdir)/proxy/hdrs \
	-I$(abs_top_srcdir)/proxy/http \
	-I$(abs_top_srcdir)/proxy/logging \
	-I$(abs_top_srcdir)/mgmt \
	-I$(abs_top_srcdir)/mgmt/utils \
	@OPENSSL_INCLUDES@

test_LD_ADD = \
	$(top_builddir)/src/records/librecords_p.a \
	$(top_builddir)/mgmt/libmgmt_p.la \
	$(top_builddir)/mgmt/libmgmt_c.la \
	$(top_builddir)/iocore/eventsystem/libinkevent.a \
	$(top_builddir)/src/tscore/libtscore.la \
	$(top_builddir)/src/tscpp/util/libtscpputil.la \
	$(top_builddir)/proxy/shared/libUglyLogStubs.a \
	@SWOC_LIBS@ @HWLOC_LIBS@ @YAMLCPP_LIBS@

test_RefCountCache_CPPFLAGS = $(test_CPP_FLAGS)

test_RefCountCache_LDFLAGS = $(test_LD_FLAGS)

test_RefCountCache_LDADD = $(test_LD_ADD)

test_HostFile_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(abs_top_srcdir)/tests/include \
	-I$(abs_top_srcdir)/proxy/http/remap

test_HostFile_LDADD = \
	$(top_builddir)/src/tscore/libtscore.la \
	$(top_builddir)/src/tscpp/util/libtscpputil.la \
	$(top_builddir)/iocore/eventsystem/libinkevent.a

test_HostFile_SOURCES = \
	test_HostFile.cc \
	HostFile.h \
	HostFile.cc \
	HostDBInfo.cc

include $(top_srcdir)/build/tidy.mk

clang-tidy-local: $(DIST_SOURCES)
	$(CXX_Clang_Tidy)
