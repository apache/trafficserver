#
# Makefile.am for the Enterprise Management module.
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
	-I$(abs_top_srcdir)/lib/records \
	-I$(abs_top_srcdir)/proxy \
	-I$(abs_top_srcdir)/proxy/http \
	-I$(abs_top_srcdir)/proxy/http2 \
	-I$(abs_top_srcdir)/proxy/logging \
	-I$(abs_top_srcdir)/proxy/http/remap \
	-I$(abs_top_srcdir)/proxy/hdrs \
	-I$(abs_top_srcdir)/proxy/shared \
	-I$(abs_top_srcdir)/mgmt \
	-I$(abs_top_srcdir)/mgmt/utils \
	$(TS_INCLUDES) \
	@OPENSSL_INCLUDES@

bin_PROGRAMS = traffic_server

traffic_server_SOURCES = \
	CoreUtils.cc \
	CoreUtils.h \
	Crash.cc \
	EventName.cc \
	EventName.h \
	FetchSM.cc \
	FetchSM.h \
	HostStatus.cc \
	InkAPI.cc \
	InkIOCoreAPI.cc \
	SocksProxy.cc \
	traffic_server.cc

if BUILD_TESTS
traffic_server_SOURCES += \
	InkAPITest.cc
endif

traffic_server_LDADD = \
	$(top_builddir)/proxy/http/libhttp.a \
	$(top_builddir)/proxy/http/remap/libhttp_remap.a \
	$(top_builddir)/proxy/http/remap/libhttp_remap.a \
	$(top_builddir)/proxy/http2/libhttp2.a \
	$(top_builddir)/proxy/logging/liblogging.a \
	$(top_builddir)/proxy/logging/liblogcollation.a \
	$(top_builddir)/proxy/hdrs/libhdrs.a \
	$(top_builddir)/proxy/shared/libdiagsconfig.a \
	$(top_builddir)/mgmt/libmgmt_p.la \
	$(top_builddir)/iocore/utils/libinkutils.a \
	$(top_builddir)/iocore/dns/libinkdns.a \
	$(top_builddir)/iocore/hostdb/libinkhostdb.a \
	$(top_builddir)/iocore/dns/libinkdns.a \
	$(top_builddir)/iocore/cache/libinkcache.a \
	$(top_builddir)/iocore/aio/libinkaio.a \
	$(top_builddir)/lib/ts/libtsutil.la \
	$(top_builddir)/lib/bindings/libbindings.la \
	$(top_builddir)/proxy/libproxy.a

# ToDo: When we remove this, we hould merge the two _LDADD rules here
if BUILD_LUAJIT
traffic_server_LDADD += \
	$(top_builddir)/lib/luajit/src/libluajit.a
endif

traffic_server_LDADD += \
	$(top_builddir)/iocore/net/libinknet.a \
	$(top_builddir)/iocore/eventsystem/libinkevent.a \
	$(top_builddir)/lib/records/librecords_p.a \
	$(top_builddir)/iocore/eventsystem/libinkevent.a \
	$(top_builddir)/lib/tsconfig/libtsconfig.la \
	@HWLOC_LIBS@ \
	@LIBPCRE@ \
	@LIBTCL@ \
	@LIBRESOLV@ \
	@LIBZ@ \
	@LIBLZMA@ \
	@LIBPROFILER@ \
	@OPENSSL_LIBS@ \
	-lm

if BUILD_LUAJIT
traffic_server_LDADD += $(LUAJIT:%=$(top_builddir)/lib/luajit/src/%)
endif

if SYSTEM_LUAJIT
traffic_server_LDADD += @LIBLUAJIT@
endif

versiondir = $(pkgsysconfdir)

include $(top_srcdir)/build/tidy.mk

clang-tidy-local: $(noinst_HEADERS) $(traffic_server_SOURCES)
	$(CXX_Clang_Tidy)
