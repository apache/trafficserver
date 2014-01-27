# proxy Makefile.am
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

# Note that hdrs is targeted from ../Makefile.am
SUBDIRS = congest http logging config
noinst_LIBRARIES =
bin_PROGRAMS = \
  traffic_server \
  traffic_logcat \
  traffic_logstats \
  traffic_sac

noinst_PROGRAMS = \
  test_xml_parser

TESTS = \
  tests/test_logstats_json \
  tests/test_logstats_summary \
  test_xml_parser

AM_CPPFLAGS = \
  $(iocore_include_dirs) \
  -I$(top_srcdir)/lib/records \
  -I$(top_srcdir)/lib/ts \
  -I$(srcdir)/http \
  -I$(srcdir)/logging \
  -I$(srcdir)/http/remap  \
  -I$(srcdir)/hdrs \
  -I$(srcdir)/shared \
  -I$(top_srcdir)/mgmt \
  -I$(top_srcdir)/mgmt/preparse \
  -I$(top_srcdir)/mgmt/utils \
  -I$(top_builddir)/proxy/api/ts \
  -I$(top_srcdir)/lib

noinst_HEADERS = \
  ConfigParse.h \
  Show.h


if STATIC_LIBTS
   which_libts = $(top_builddir)/lib/ts/.libs/libtsutil.a
else
   which_libts = $(top_builddir)/lib/ts/libtsutil.la
endif

EXTRA_DIST = InkAPITestTool.cc example_alarm_bin.sh example_prep.sh

traffic_server_SOURCES = \
  AbstractBuffer.cc \
  AbstractBuffer.h \
  CacheControl.cc \
  CacheControl.h \
  ControlBase.cc \
  ControlBase.h \
  ControlMatcher.cc \
  ControlMatcher.h \
  CoreUtils.cc \
  CoreUtils.h \
  DynamicStats.h \
  EventName.cc \
  HttpTransStats.h \
  ICP.cc \
  ICP.h \
  ICPConfig.cc \
  ICPevents.h \
  ICPlog.h \
  ICPProcessor.cc \
  ICPProcessor.h \
  ICPStats.cc \
  InkAPI.cc \
  FetchSM.cc \
  InkAPIInternal.h \
  InkIOCoreAPI.cc \
  IPAllow.cc \
  IPAllow.h \
  Main.cc \
  Main.h \
  ParentSelection.cc \
  ParentSelection.h \
  Plugin.cc \
  Plugin.h \
  PluginVC.cc \
  PluginVC.h \
  Prefetch.cc \
  Prefetch.h \
  ReverseProxy.cc \
  ReverseProxy.h \
  SocksProxy.cc \
  StatPages.cc \
  StatPages.h \
  StatSystem.cc \
  StatSystem.h \
  Transform.cc \
  Transform.h \
  TransformInternal.h \
  Update.cc \
  Update.h

if BUILD_TESTS
  traffic_server_SOURCES +=  InkAPITest.cc  \
    RegressionSM.h \
    RegressionSM.cc
endif

traffic_server_LDFLAGS = @EXTRA_CXX_LDFLAGS@ @LIBTOOL_LINK_FLAGS@
traffic_server_LDADD = \
  http/libhttp.a \
  http/remap/libhttp_remap.a \
  congest/libCongestionControl.a \
  logging/liblogging.a \
  logging/liblogcollation.a \
  hdrs/libhdrs.a  \
  shared/liberror.a  \
  shared/libdiagsconfig.a  \
  shared/libsignals.a \
  shared/libxml.a \
  $(top_builddir)/mgmt/preparse/libpreparse.a \
  $(top_builddir)/mgmt/utils/libutils_p.a \
  $(top_builddir)/mgmt/libmgmt_p.a \
  $(top_builddir)/iocore/utils/libinkutils.a \
  $(top_builddir)/iocore/cluster/libinkcluster.a \
  $(top_builddir)/iocore/dns/libinkdns.a \
  $(top_builddir)/iocore/hostdb/libinkhostdb.a \
  $(top_builddir)/iocore/dns/libinkdns.a \
  $(top_builddir)/iocore/cluster/libinkcluster.a \
  $(top_builddir)/iocore/cache/libinkcache.a \
  $(top_builddir)/iocore/aio/libinkaio.a \
  $(top_builddir)/iocore/net/libinknet.a \
  $(top_builddir)/iocore/eventsystem/libinkevent.a \
  $(top_builddir)/lib/records/librecprocess.a \
  $(top_builddir)/iocore/eventsystem/libinkevent.a \
  $(which_libts) \
  @hwloc_LIBS@ \
  @LIBPCRE@ \
  @OPENSSL_LIBS@ \
  @LIBTCL@ \
  @LIBEXPAT@ \
  @LIBDEMANGLE@ \
  @LIBCAP@ \
  @LIBRESOLV@ \
  @LIBZ@ \
  @LIBLZMA@ \
  @LIBPROFILER@ \
  -lm

if BUILD_LUA_SUPPORT
traffic_server_LDFLAGS += @LUA_LUAJIT_LDFLAGS@
endif

traffic_logcat_SOURCES = logcat.cc
traffic_logcat_LDFLAGS = @EXTRA_CXX_LDFLAGS@ @LIBTOOL_LINK_FLAGS@
traffic_logcat_LDADD = \
  logging/liblogging.a \
  shared/libdiagsconfig.a \
  shared/libUglyLogStubs.a \
  shared/libsignals.a \
  shared/libxml.a \
  $(top_builddir)/mgmt/utils/libutils_p.a \
  $(top_builddir)/mgmt/libmgmt_p.a \
  $(top_builddir)/lib/records/librecprocess.a \
  $(top_builddir)/iocore/eventsystem/libinkevent.a \
  $(top_builddir)/lib/ts/libtsutil.la \
  @LIBRESOLV@ @LIBPCRE@ @OPENSSL_LIBS@ @LIBTCL@ \
  @LIBEXPAT@ @LIBDEMANGLE@ @LIBPROFILER@ -lm

traffic_logstats_SOURCES = logstats.cc
traffic_logstats_LDFLAGS = @EXTRA_CXX_LDFLAGS@ @LIBTOOL_LINK_FLAGS@
traffic_logstats_LDADD = \
  logging/liblogging.a \
  shared/libdiagsconfig.a \
  shared/libUglyLogStubs.a \
  shared/libsignals.a \
  shared/libxml.a \
  $(top_builddir)/mgmt/utils/libutils_p.a \
  $(top_builddir)/mgmt/libmgmt_p.a \
  $(top_builddir)/lib/records/librecprocess.a \
  $(top_builddir)/iocore/eventsystem/libinkevent.a \
  $(top_builddir)/lib/ts/libtsutil.la \
  @LIBRESOLV@ @LIBPCRE@ @OPENSSL_LIBS@ @LIBTCL@ \
  @LIBEXPAT@ @LIBDEMANGLE@ @LIBPROFILER@ -lm

traffic_sac_SOURCES = \
  sac.cc \
  ICP.cc \
  ICPConfig.cc \
  ICPProcessor.cc \
  ICPStats.cc \
  IPAllow.cc \
  ParentSelection.cc \
  ControlBase.cc \
  ControlMatcher.cc \
  CacheControl.cc \
  StatSystem.cc \
  ReverseProxy.cc \
  EventName.cc \
  StatPages.cc \
  PluginVC.cc \
  AbstractBuffer.cc \
  Transform.cc \
  Prefetch.cc \
  Update.cc \
  Plugin.cc \
  InkAPI.cc \
  FetchSM.cc \
  InkIOCoreAPI.cc

traffic_sac_LDFLAGS = @EXTRA_CXX_LDFLAGS@ @LIBTOOL_LINK_FLAGS@
traffic_sac_LDADD = \
  http/libhttp.a \
  shared/libdiagsconfig.a \
  http/remap/libhttp_remap.a \
  congest/libCongestionControl.a \
  logging/liblogging.a \
  logging/liblogcollation.a \
  hdrs/libhdrs.a \
  shared/liberror.a \
  shared/libsignals.a \
  shared/libxml.a \
  $(top_builddir)/mgmt/preparse/libpreparse.a \
  $(top_builddir)/mgmt/utils/libutils_p.a \
  $(top_builddir)/mgmt/libmgmt_p.a \
  $(top_builddir)/iocore/cluster/libinkcluster.a \
  $(top_builddir)/iocore/dns/libinkdns.a \
  $(top_builddir)/iocore/hostdb/libinkhostdb.a \
  $(top_builddir)/iocore/cluster/libinkcluster.a \
  $(top_builddir)/iocore/cache/libinkcache.a \
  $(top_builddir)/iocore/utils/libinkutils.a \
  $(top_builddir)/iocore/aio/libinkaio.a \
  $(top_builddir)/iocore/net/libinknet.a \
  $(top_builddir)/iocore/eventsystem/libinkevent.a \
  $(top_builddir)/lib/records/librecprocess.a \
  $(top_builddir)/lib/ts/libtsutil.la \
  @LIBRESOLV@ @LIBPCRE@ @OPENSSL_LIBS@ @LIBTCL@ \
  @LIBEXPAT@ @LIBDEMANGLE@ @LIBZ@ @LIBLZMA@ @LIBPROFILER@ -lm

if BUILD_TESTS
  traffic_sac_SOURCES += RegressionSM.cc
endif


test_xml_parser_SOURCES = test_xml_parser.cc
test_xml_parser_LDADD = \
  $(top_builddir)/lib/records/librecprocess.a \
  $(top_builddir)/mgmt/libmgmt_p.a \
  $(top_builddir)/mgmt/utils/libutils_p.a \
  $(top_builddir)/iocore/eventsystem/libinkevent.a \
  $(top_builddir)/lib/ts/libtsutil.la \
  shared/libsignals.a \
  shared/libxml.a \
  @LIBTCL@

versiondir = $(pkgsysconfdir)

install-data-local:
	if [ `id -un` != "root" ]; then \
		$(INSTALL) -d $(DESTDIR)$(pkglocalstatedir) $(DESTDIR)$(pkglogdir) $(DESTDIR)$(pkgruntimedir) \
				$(DESTDIR)$(pkgsysconfdir) $(DESTDIR)$(pkgdatadir) $(DESTDIR)$(pkgcachedir); \
	else \
		 $(INSTALL) -d $(DESTDIR)$(pkglocalstatedir); \
		 $(INSTALL) -d -o $(pkgsysuser) -g $(pkgsysgroup) $(DESTDIR)$(pkglogdir) \
				$(DESTDIR)$(pkgruntimedir) $(DESTDIR)$(pkgsysconfdir) \
				$(DESTDIR)$(pkgdatadir) $(DESTDIR)$(pkgcachedir); \
	fi

install-data-hook:
	if [ `id -un` == "root" ]; then \
		chown -R $(pkgsysuser):$(pkgsysgroup) $(DESTDIR)$(pkgsysconfdir) $(DESTDIR)$(pkgdatadir);\
	fi
	-echo "<TS_VERSION> $(PACKAGE_VERSION)" > $(DESTDIR)$(pkgsysconfdir)/trafficserver-release
