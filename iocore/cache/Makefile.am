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
	@SWOC_INCLUDES@
	$(TS_INCLUDES)

noinst_LIBRARIES = libinkcache.a

libinkcache_a_SOURCES = \
	Cache.cc \
	CacheDir.cc \
	CacheDisk.cc \
	CacheHosting.cc \
	CacheHttp.cc \
	CachePages.cc \
	CachePagesInternal.cc \
	CacheRead.cc \
	CacheVol.cc \
	CacheWrite.cc \
	I_Cache.h \
	I_CacheDefs.h \
	I_Store.h \
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
TESTS_ENVIRONMENT = LSAN_OPTIONS=suppressions=$(abs_top_srcdir)/ci/asan_leak_suppression/unit_tests.txt

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
	-I$(abs_top_srcdir)/lib/catch2 \
	@SWOC_INCLUDES@ \
	$(TS_INCLUDES) \
	@OPENSSL_INCLUDES@

test_LDADD = \
	$(top_builddir)/iocore/cache/libinkcache.a \
	$(top_builddir)/proxy/libproxy.a \
	$(top_builddir)/proxy/http/libhttp.a \
	$(top_builddir)/proxy/http/remap/libhttp_remap.a \
	$(top_builddir)/src/tscpp/util/libtscpputil.la \
	$(top_builddir)/proxy/libproxy.a \
	$(top_builddir)/iocore/net/libinknet.a \
	$(top_builddir)/iocore/dns/libinkdns.a \
	$(top_builddir)/iocore/hostdb/libinkhostdb.a \
	$(top_builddir)/proxy/logging/liblogging.a \
	$(top_builddir)/proxy/hdrs/libhdrs.a \
	$(top_builddir)/proxy/shared/libdiagsconfig.a \
	$(top_builddir)/iocore/utils/libinkutils.a \
	$(top_builddir)/iocore/aio/libinkaio.a \
	$(top_builddir)/src/records/librecords_p.a \
	$(top_builddir)/src/api/libtsapicore.a \
	$(top_builddir)/src/tscore/libtscore.a \
	$(top_builddir)/lib/fastlz/libfastlz.a \
	$(top_builddir)/iocore/eventsystem/libinkevent.a \
	$(top_builddir)/src/tscpp/util/libtscpputil.la \
	$(top_builddir)/mgmt/rpc/libjsonrpc_protocol.la \
	@SWOC_LIBS@ \
	@HWLOC_LIBS@ \
	@LIBPCRE@ \
	@LIBRESOLV@ \
	@LIBZ@ \
	@LIBLZMA@ \
	@LIBPROFILER@ \
	@OPENSSL_LIBS@ \
	@YAMLCPP_LIBS@ \
	@LIBCAP@ \
	-lm

if ENABLE_QUIC
test_LDADD += \
  $(top_builddir)/iocore/net/quic/libquic.a
endif

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
  test_Update_header \
  test_Populated_Cache

if ENABLE_DISK_FAILURE_TESTS
check_PROGRAMS += \
  test_Disk_Init_Failure_0 \
  test_Disk_Init_Failure_1 \
  test_Disk_Failure_5 \
	test_Disk_Failure_6 \
  test_Disk_Failure_7 \
  test_Disk_Failure_8 \
  test_Disk_Failure_9 \
  test_Disk_Failure_10 \
  test_Disk_Failure_11 \
  test_Disk_Failure_12 \
  test_Disk_Failure_13 \
  test_Disk_Failure_14 \
  test_Disk_Failure_15 \
  test_Disk_Failure_16 \
  test_Disk_Failure_17 \
  test_Disk_Failure_18 \
  test_Disk_Failure_19 \
  test_Disk_Failure_20 \
  test_Populated_Cache_Disk_Failure

test_Disk_Init_Failure_0_CPPFLAGS = $(test_CPPFLAGS) -DFAILURE_INDICES={0}
test_Disk_Init_Failure_0_LDFLAGS = AM_LDFLAGS@
test_Disk_Init_Failure_0_LDADD = $(test_LDADD)
test_Disk_Init_Failure_0_SOURCES = \
  $(test_main_SOURCES) \
  ./test/test_Disk_Init_Failure.cc

test_Disk_Init_Failure_1_CPPFLAGS = $(test_CPPFLAGS) -DFAILURE_INDICES={1}
test_Disk_Init_Failure_1_LDFLAGS = @AM_LDFLAGS@
test_Disk_Init_Failure_1_LDADD = $(test_LDADD)
test_Disk_Init_Failure_1_SOURCES = \
  $(test_main_SOURCES) \
  ./test/test_Disk_Init_Failure.cc

test_Disk_Failure_5_CPPFLAGS = $(test_CPPFLAGS) -DFAILURE_INDICES={5}
test_Disk_Failure_5_LDFLAGS = @AM_LDFLAGS@
test_Disk_Failure_5_LDADD = $(test_LDADD)
test_Disk_Failure_5_SOURCES = \
  $(test_main_SOURCES) \
  ./test/test_Disk_Failure.cc

test_Disk_Failure_6_CPPFLAGS = $(test_CPPFLAGS) -DFAILURE_INDICES={6}
test_Disk_Failure_6_LDFLAGS = @AM_LDFLAGS@
test_Disk_Failure_6_LDADD = $(test_LDADD)
test_Disk_Failure_6_SOURCES = \
  $(test_main_SOURCES) \
  ./test/test_Disk_Failure.cc

test_Disk_Failure_7_CPPFLAGS = $(test_CPPFLAGS) -DFAILURE_INDICES={7}
test_Disk_Failure_7_LDFLAGS = @AM_LDFLAGS@
test_Disk_Failure_7_LDADD = $(test_LDADD)
test_Disk_Failure_7_SOURCES = \
  $(test_main_SOURCES) \
  ./test/test_Disk_Failure.cc

test_Disk_Failure_8_CPPFLAGS = $(test_CPPFLAGS) -DFAILURE_INDICES={8}
test_Disk_Failure_8_LDFLAGS = @AM_LDFLAGS@
test_Disk_Failure_8_LDADD = $(test_LDADD)
test_Disk_Failure_8_SOURCES = \
  $(test_main_SOURCES) \
  ./test/test_Disk_Failure.cc

test_Disk_Failure_9_CPPFLAGS = $(test_CPPFLAGS) -DFAILURE_INDICES={9}
test_Disk_Failure_9_LDFLAGS = @AM_LDFLAGS@
test_Disk_Failure_9_LDADD = $(test_LDADD)
test_Disk_Failure_9_SOURCES = \
  $(test_main_SOURCES) \
  ./test/test_Disk_Failure.cc

test_Disk_Failure_10_CPPFLAGS = $(test_CPPFLAGS) -DFAILURE_INDICES={10}
test_Disk_Failure_10_LDFLAGS = @AM_LDFLAGS@
test_Disk_Failure_10_LDADD = $(test_LDADD)
test_Disk_Failure_10_SOURCES = \
  $(test_main_SOURCES) \
  ./test/test_Disk_Failure.cc

test_Disk_Failure_11_CPPFLAGS = $(test_CPPFLAGS) -DFAILURE_INDICES={11}
test_Disk_Failure_11_LDFLAGS = @AM_LDFLAGS@
test_Disk_Failure_11_LDADD = $(test_LDADD)
test_Disk_Failure_11_SOURCES = \
  $(test_main_SOURCES) \
  ./test/test_Disk_Failure.cc

test_Disk_Failure_12_CPPFLAGS = $(test_CPPFLAGS) -DFAILURE_INDICES={12}
test_Disk_Failure_12_LDFLAGS = @AM_LDFLAGS@
test_Disk_Failure_12_LDADD = $(test_LDADD)
test_Disk_Failure_12_SOURCES = \
  $(test_main_SOURCES) \
  ./test/test_Disk_Failure.cc

test_Disk_Failure_13_CPPFLAGS = $(test_CPPFLAGS) -DFAILURE_INDICES={13}
test_Disk_Failure_13_LDFLAGS = @AM_LDFLAGS@
test_Disk_Failure_13_LDADD = $(test_LDADD)
test_Disk_Failure_13_SOURCES = \
  $(test_main_SOURCES) \
  ./test/test_Disk_Failure.cc

test_Disk_Failure_14_CPPFLAGS = $(test_CPPFLAGS) -DFAILURE_INDICES={14}
test_Disk_Failure_14_LDFLAGS = @AM_LDFLAGS@
test_Disk_Failure_14_LDADD = $(test_LDADD)
test_Disk_Failure_14_SOURCES = \
  $(test_main_SOURCES) \
  ./test/test_Disk_Failure.cc

test_Disk_Failure_15_CPPFLAGS = $(test_CPPFLAGS) -DFAILURE_INDICES={15}
test_Disk_Failure_15_LDFLAGS = @AM_LDFLAGS@
test_Disk_Failure_15_LDADD = $(test_LDADD)
test_Disk_Failure_15_SOURCES = \
  $(test_main_SOURCES) \
  ./test/test_Disk_Failure.cc

test_Disk_Failure_16_CPPFLAGS = $(test_CPPFLAGS) -DFAILURE_INDICES={16}
test_Disk_Failure_16_LDFLAGS = @AM_LDFLAGS@
test_Disk_Failure_16_LDADD = $(test_LDADD)
test_Disk_Failure_16_SOURCES = \
  $(test_main_SOURCES) \
  ./test/test_Disk_Failure.cc

test_Disk_Failure_17_CPPFLAGS = $(test_CPPFLAGS) -DFAILURE_INDICES={17}
test_Disk_Failure_17_LDFLAGS = @AM_LDFLAGS@
test_Disk_Failure_17_LDADD = $(test_LDADD)
test_Disk_Failure_17_SOURCES = \
  $(test_main_SOURCES) \
  ./test/test_Disk_Failure.cc

test_Disk_Failure_18_CPPFLAGS = $(test_CPPFLAGS) -DFAILURE_INDICES={18}
test_Disk_Failure_18_LDFLAGS = @AM_LDFLAGS@
test_Disk_Failure_18_LDADD = $(test_LDADD)
test_Disk_Failure_18_SOURCES = \
  $(test_main_SOURCES) \
  ./test/test_Disk_Failure.cc

test_Disk_Failure_19_CPPFLAGS = $(test_CPPFLAGS) -DFAILURE_INDICES={19}
test_Disk_Failure_19_LDFLAGS = @AM_LDFLAGS@
test_Disk_Failure_19_LDADD = $(test_LDADD)
test_Disk_Failure_19_SOURCES = \
  $(test_main_SOURCES) \
  ./test/test_Disk_Failure.cc

test_Disk_Failure_20_CPPFLAGS = $(test_CPPFLAGS) -DFAILURE_INDICES={20}
test_Disk_Failure_20_LDFLAGS = @AM_LDFLAGS@
test_Disk_Failure_20_LDADD = $(test_LDADD)
test_Disk_Failure_20_SOURCES = \
  $(test_main_SOURCES) \
  ./test/test_Disk_Failure.cc

test_Populated_Cache_Disk_Failure_CPPFLAGS = $(test_CPPFLAGS)
test_Populated_Cache_Disk_Failure_LDFLAGS = @AM_LDFLAGS@
test_Populated_Cache_Disk_Failure_LDADD = $(test_LDADD)
test_Populated_Cache_Disk_Failure_SOURCES = \
  $(test_main_SOURCES) \
  ./test/test_Populated_Cache_Disk_Failure.cc
endif

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

test_Populated_Cache_CPPFLAGS = $(test_CPPFLAGS)
test_Populated_Cache_LDFLAGS = @AM_LDFLAGS@
test_Populated_Cache_LDADD = $(test_LDADD)
test_Populated_Cache_SOURCES = \
  $(test_main_SOURCES) \
  ./test/test_Populated_Cache.cc

include $(top_srcdir)/mk/tidy.mk

clang-tidy-local: $(DIST_SOURCES)
	$(CXX_Clang_Tidy)
