# Makefile.am for the traffic/iocore/cache hierarchy
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
	-I$(abs_top_srcdir)/proxy/shared \
	-I$(abs_top_srcdir)/proxy/http/remap \
	-I$(abs_top_srcdir)/mgmt \
	-I$(abs_top_srcdir)/mgmt/utils \
	$(TS_INCLUDES)

noinst_LIBRARIES = libinkcache.a

libinkcache_a_SOURCES = \
	Cache.cc \
	CacheDir.cc \
	CacheDisk.cc \
	CacheHosting.cc \
	CacheHttp.cc \
	CacheLink.cc \
	CachePages.cc \
	CachePagesInternal.cc \
	CacheRead.cc \
	CacheVol.cc \
	CacheWrite.cc \
	I_Cache.h \
	I_CacheDefs.h \
	I_Store.h \
	Inline.cc \
	P_Cache.h \
	P_CacheArray.h \
	P_CacheDir.h \
	P_CacheDisk.h \
	P_CacheHosting.h \
	P_CacheHttp.h \
	P_CacheInternal.h \
	P_CacheVol.h \
	P_RamCache.h \
	RamCacheCLFUS.cc \
	RamCacheLRU.cc \
	Store.cc

if BUILD_TESTS
libinkcache_a_SOURCES += \
	CacheTest.cc \
	P_CacheTest.h
endif

TESTS = $(check_PROGRAMS)

test_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	$(iocore_include_dirs) \
	-I$(abs_top_srcdir)/include \
	-I$(abs_top_srcdir)/lib \
	-I$(abs_top_srcdir)/proxy \
	-I$(abs_top_srcdir)/proxy/http \
	-I$(abs_top_srcdir)/proxy/http2 \
	-I$(abs_top_srcdir)/proxy/http3 \
	-I$(abs_top_srcdir)/proxy/logging \
	-I$(abs_top_srcdir)/proxy/http/remap \
	-I$(abs_top_srcdir)/proxy/hdrs \
	-I$(abs_top_srcdir)/proxy/shared \
	-I$(abs_top_srcdir)/mgmt \
	-I$(abs_top_srcdir)/mgmt/utils \
	-I$(abs_top_srcdir)/tests/include \
	$(TS_INCLUDES) \
	@OPENSSL_INCLUDES@

test_LDADD = \
	$(top_builddir)/src/tscpp/util/libtscpputil.la \
	$(top_builddir)/iocore/cache/libinkcache.a \
	$(top_builddir)/proxy/libproxy.a \
	$(top_builddir)/proxy/http/libhttp.a \
	$(top_builddir)/proxy/http/remap/libhttp_remap.a \
	$(top_builddir)/proxy/libproxy.a \
	$(top_builddir)/iocore/net/libinknet.a \
	$(top_builddir)/iocore/dns/libinkdns.a \
	$(top_builddir)/iocore/hostdb/libinkhostdb.a \
	$(top_builddir)/proxy/logging/liblogging.a \
	$(top_builddir)/proxy/hdrs/libhdrs.a \
	$(top_builddir)/proxy/shared/libdiagsconfig.a \
	$(top_builddir)/mgmt/libmgmt_p.la \
	$(top_builddir)/iocore/utils/libinkutils.a \
	$(top_builddir)/iocore/aio/libinkaio.a \
	$(top_builddir)/src/tscore/libtscore.la \
	$(top_builddir)/lib/records/librecords_p.a \
	$(top_builddir)/iocore/eventsystem/libinkevent.a \
	@HWLOC_LIBS@ \
	@LIBPCRE@ \
	@LIBRESOLV@ \
	@LIBZ@ \
	@LIBLZMA@ \
	@LIBPROFILER@ \
	@OPENSSL_LIBS@ \
	@YAMLCPP_LIBS@ \
	-lm

check_PROGRAMS = \
  test_Cache \
  test_RWW \
  test_Alternate_L_to_S \
  test_Alternate_S_to_L \
  test_Alternate_L_to_S_remove_L \
  test_Alternate_L_to_S_remove_S \
  test_Alternate_S_to_L_remove_S \
  test_Alternate_S_to_L_remove_L \
  test_Update_L_to_S \
  test_Update_S_to_L \
  test_Update_header

test_main_SOURCES = \
  ./test/main.cc \
  ./test/stub.cc \
  ./test/CacheTestHandler.cc

test_Cache_CPPFLAGS = $(test_CPPFLAGS)
test_Cache_LDFLAGS = @AM_LDFLAGS@
test_Cache_LDADD = $(test_LDADD)
test_Cache_SOURCES = \
  $(test_main_SOURCES) \
  ./test/test_Cache.cc

test_RWW_CPPFLAGS = $(test_CPPFLAGS)
test_RWW_LDFLAGS = @AM_LDFLAGS@
test_RWW_LDADD = $(test_LDADD)
test_RWW_SOURCES = \
  $(test_main_SOURCES) \
  ./test/test_RWW.cc

test_Alternate_L_to_S_CPPFLAGS = $(test_CPPFLAGS)
test_Alternate_L_to_S_LDFLAGS = @AM_LDFLAGS@
test_Alternate_L_to_S_LDADD = $(test_LDADD)
test_Alternate_L_to_S_SOURCES = \
  $(test_main_SOURCES) \
  ./test/test_Alternate_L_to_S.cc

test_Alternate_S_to_L_CPPFLAGS = $(test_CPPFLAGS)
test_Alternate_S_to_L_LDFLAGS = @AM_LDFLAGS@
test_Alternate_S_to_L_LDADD = $(test_LDADD)
test_Alternate_S_to_L_SOURCES = \
  $(test_main_SOURCES) \
  ./test/test_Alternate_S_to_L.cc

test_Alternate_L_to_S_remove_L_CPPFLAGS = $(test_CPPFLAGS)
test_Alternate_L_to_S_remove_L_LDFLAGS = @AM_LDFLAGS@
test_Alternate_L_to_S_remove_L_LDADD = $(test_LDADD)
test_Alternate_L_to_S_remove_L_SOURCES = \
  $(test_main_SOURCES) \
  ./test/test_Alternate_L_to_S_remove_L.cc

test_Alternate_L_to_S_remove_S_CPPFLAGS = $(test_CPPFLAGS)
test_Alternate_L_to_S_remove_S_LDFLAGS = @AM_LDFLAGS@
test_Alternate_L_to_S_remove_S_LDADD = $(test_LDADD)
test_Alternate_L_to_S_remove_S_SOURCES = \
  $(test_main_SOURCES) \
  ./test/test_Alternate_L_to_S_remove_S.cc

test_Alternate_S_to_L_remove_S_CPPFLAGS = $(test_CPPFLAGS)
test_Alternate_S_to_L_remove_S_LDFLAGS = @AM_LDFLAGS@
test_Alternate_S_to_L_remove_S_LDADD = $(test_LDADD)
test_Alternate_S_to_L_remove_S_SOURCES = \
  $(test_main_SOURCES) \
  ./test/test_Alternate_S_to_L_remove_S.cc

test_Alternate_S_to_L_remove_L_CPPFLAGS = $(test_CPPFLAGS)
test_Alternate_S_to_L_remove_L_LDFLAGS = @AM_LDFLAGS@
test_Alternate_S_to_L_remove_L_LDADD = $(test_LDADD)
test_Alternate_S_to_L_remove_L_SOURCES = \
  $(test_main_SOURCES) \
  ./test/test_Alternate_S_to_L_remove_L.cc

test_Update_L_to_S_CPPFLAGS = $(test_CPPFLAGS)
test_Update_L_to_S_LDFLAGS = @AM_LDFLAGS@
test_Update_L_to_S_LDADD = $(test_LDADD)
test_Update_L_to_S_SOURCES = \
  $(test_main_SOURCES) \
  ./test/test_Update_L_to_S.cc

test_Update_S_to_L_CPPFLAGS = $(test_CPPFLAGS)
test_Update_S_to_L_LDFLAGS = @AM_LDFLAGS@
test_Update_S_to_L_LDADD = $(test_LDADD)
test_Update_S_to_L_SOURCES = \
  $(test_main_SOURCES) \
  ./test/test_Update_S_to_L.cc

test_Update_header_CPPFLAGS = $(test_CPPFLAGS)
test_Update_header_LDFLAGS = @AM_LDFLAGS@
test_Update_header_LDADD = $(test_LDADD)
test_Update_header_SOURCES = \
  $(test_main_SOURCES) \
  ./test/test_Update_header.cc

include $(top_srcdir)/build/tidy.mk

clang-tidy-local: $(DIST_SOURCES)
	$(CXX_Clang_Tidy)
