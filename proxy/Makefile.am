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

if STANDALONE_IOCORE
noinst_LIBRARIES = libTrafficServerStandalone.a
bin_PROGRAMS =
noinst_PROGRAMS =
else
SUBDIRS = congest http hdrs logging
if BUILD_WCCP
SUBDIRS += wccp
endif
SUBDIRS += mgmt config stats
noinst_LIBRARIES =
bin_PROGRAMS = \
  traffic_server \
  traffic_logcat \
  traffic_logstats \
  traffic_sac
endif

AM_CPPFLAGS = \
  $(iocore_include_dirs) \
  -I$(top_srcdir)/lib/records \
  -I$(srcdir)/http \
  -I$(srcdir)/logging \
  -I$(srcdir)/http/remap  \
  -I$(srcdir)/hdrs \
  -I$(srcdir)/mgmt \
  -I$(srcdir)/mgmt/preparse \
  -I$(srcdir)/mgmt/utils \
  -I$(srcdir)/api/ts

noinst_HEADERS = \
  ConfigParse.h \
  NTDefs.h \
  Show.h

EXTRA_PROGRAMS = \
  test_StateEventLogger \
  test_ClusterHashStandalone \
  test_xml_parser

EXTRA_LIBRARIES	= \
  libClusterHashStandalone.a \
  libTrafficServerStandalone.a

EXTRA_DIST = InkAPITestTool.cc example_alarm_bin.sh example_prep.sh

traffic_server_SOURCES = \
  AbstractBuffer.cc \
  AbstractBuffer.h \
  CacheControl.cc \
  CacheControl.h \
  ProxyConfig.cc \
  ProxyConfig.h \
  ControlBase.cc \
  ControlBase.h \
  ControlMatcher.cc \
  ControlMatcher.h \
  CoreUtils.cc \
  CoreUtils.h \
  DiagsConfig.cc \
  DiagsConfig.h \
  DynamicStats.h \
  Error.cc \
  Error.h \
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
  InkXml.cc \
  InkXml.h \
  IPAllow.cc \
  IPAllow.h \
  Main.cc \
  Main.h \
  ParentSelection.cc \
  ParentSelection.h \
  Plugin.cc \
  Plugin.h \
  PluginDB.cc \
  PluginDB.h \
  PluginVC.cc \
  PluginVC.h \
  Prefetch.cc \
  Prefetch.h \
  Raf.h \
  ReverseProxy.cc \
  ReverseProxy.h \
  RniTransStats.h \
  signals.cc \
  signals.h \
  SocksProxy.cc \
  StatPages.cc \
  StatPages.h \
  StatSystem.cc \
  StatSystem.h \
  StatAPITypes.cc \
  Transform.cc \
  Transform.h \
  TransformInternal.h \
  Update.cc \
  Update.h

if BUILD_V2STATS
  traffic_server_SOURCES += StatSystemV2.cc
endif

if BUILD_TESTS
  traffic_server_SOURCES +=  InkAPITest.cc  \
    RegressionSM.h \
    RegressionSM.cc \
    TestHook.cc
endif

traffic_server_LDFLAGS = @EXTRA_CXX_LDFLAGS@
traffic_server_LDADD = \
  http/libhttp.a \
  http/remap/libhttp_remap.a \
  congest/libCongestionControl.a \
  logging/liblogging.a \
  stats/libstats.a \
  hdrs/libhdrs.a  \
  mgmt/preparse/libpreparse.a \
  mgmt/utils/libutils_p.a \
  mgmt/libmgmt_p.a \
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
  $(top_builddir)/lib/ts/libts.a \
  @LIBTHREAD@ @LIBSOCKET@ @LIBNSL@ @LIBRESOLV@ @LIBRT@ \
  @LIBPCRE@ @LIBSSL@ @LIBTCL@ @LIBDL@ \
  @LIBEXPAT@ @LIBDEMANGLE@ @LIBICONV@ @LIBCAP@ \
  @LIBMLD@ @LIBEXC@ @LIBM@ @LIBEV@ @LIBPROFILER@ @LIBEXECINFO@

traffic_logcat_SOURCES = logcat.cc
traffic_logcat_LDFLAGS = @EXTRA_CXX_LDFLAGS@
traffic_logcat_LDADD = \
  ICP.o \
  ICPConfig.o \
  ICPProcessor.o \
  ICPStats.o \
  IPAllow.o \
  ParentSelection.o \
  ControlBase.o \
  ControlMatcher.o CacheControl.o  \
  StatSystem.o \
  StatAPITypes.o \
  ReverseProxy.o \
  ProxyConfig.o \
  signals.o \
  Error.o \
  EventName.o \
  DiagsConfig.o \
  StatPages.o \
  PluginVC.o \
  AbstractBuffer.o \
  Transform.o \
  Prefetch.o \
  Update.o \
  Plugin.o \
  PluginDB.o \
  InkAPI.o \
  FetchSM.o \
  InkIOCoreAPI.o \
  InkXml.o \
  http/libhttp.a \
  http/remap/libhttp_remap.a \
  logging/liblogging.a \
  stats/libstats.a \
  hdrs/libhdrs.a \
  congest/libCongestionControl.a \
  mgmt/preparse/libpreparse.a \
  mgmt/utils/libutils_p.a \
  mgmt/libmgmt_p.a \
  $(top_builddir)/iocore/utils/libinkutils.a \
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
  $(top_builddir)/iocore/eventsystem/libinkevent.a \
  $(top_builddir)/lib/ts/libts.a \
  @LIBTHREAD@ @LIBSOCKET@ @LIBNSL@ @LIBRESOLV@ @LIBRT@ \
  @LIBPCRE@ @LIBSSL@ @LIBTCL@ @LIBDL@ @LIBEV@ \
  @LIBEXPAT@ @LIBDEMANGLE@ @LIBMLD@ @LIBEXC@ @LIBICONV@ @LIBM@ @LIBPROFILER@ \
  @LIBEXECINFO@

if BUILD_V2STATS
  traffic_logcat_LDADD += StatSystemV2.o
endif

if BUILD_TESTS
  traffic_logcat_LDADD += RegressionSM.o \
      TestHook.o
endif

traffic_logstats_SOURCES = logstats.cc
traffic_logstats_LDFLAGS = @EXTRA_CXX_LDFLAGS@
traffic_logstats_LDADD = \
  ICP.o \
  ICPConfig.o \
  ICPProcessor.o \
  ICPStats.o \
  IPAllow.o \
  ParentSelection.o \
  ControlBase.o \
  ControlMatcher.o \
  CacheControl.o  \
  StatSystem.o \
  StatAPITypes.o \
  ReverseProxy.o \
  ProxyConfig.o \
  signals.o \
  Error.o \
  EventName.o \
  DiagsConfig.o \
  StatPages.o \
  PluginVC.o \
  AbstractBuffer.o \
  Transform.o \
  Prefetch.o \
  Update.o \
  Plugin.o \
  PluginDB.o \
  InkAPI.o \
  FetchSM.o \
  InkIOCoreAPI.o \
  InkXml.o \
  http/libhttp.a \
  http/remap/libhttp_remap.a \
  logging/liblogging.a \
  stats/libstats.a \
  hdrs/libhdrs.a \
  congest/libCongestionControl.a \
  mgmt/preparse/libpreparse.a \
  mgmt/utils/libutils_p.a \
  mgmt/libmgmt_p.a \
  $(top_builddir)/iocore/utils/libinkutils.a \
  $(top_builddir)/iocore/cluster/libinkcluster.a \
  $(top_builddir)/iocore/dns/libinkdns.a \
  $(top_builddir)/iocore/hostdb/libinkhostdb.a \
  $(top_builddir)/iocore/dns/libinkdns.a \
  $(top_builddir)/iocore/cluster/libinkcluster.a \
  $(top_builddir)/iocore/cache/libinkcache.a \
  $(top_builddir)/iocore/utils/libinkutils.a \
  $(top_builddir)/iocore/aio/libinkaio.a \
  $(top_builddir)/iocore/net/libinknet.a \
  $(top_builddir)/iocore/eventsystem/libinkevent.a \
  $(top_builddir)/lib/records/librecprocess.a \
  $(top_builddir)/iocore/eventsystem/libinkevent.a \
  $(top_builddir)/lib/ts/libts.a \
  @LIBTHREAD@ @LIBSOCKET@ @LIBNSL@ @LIBRESOLV@ @LIBRT@ \
  @LIBPCRE@ @LIBSSL@ @LIBTCL@ @LIBDL@ @LIBEV@ \
  @LIBEXPAT@ @LIBDEMANGLE@ @LIBMLD@ @LIBEXC@ @LIBICONV@ @LIBM@ @LIBPROFILER@ \
  @LIBEXECINFO@

if BUILD_V2STATS
  traffic_logstats_LDADD += StatSystemV2.o
endif

if BUILD_TESTS
  traffic_logstats_LDADD += RegressionSM.o \
    TestHook.o
endif

traffic_sac_SOURCES = sac.cc
traffic_sac_LDFLAGS = @EXTRA_CXX_LDFLAGS@
traffic_sac_LDADD = \
  ICP.o \
  ICPConfig.o \
  ICPProcessor.o \
  ICPStats.o \
  IPAllow.o \
  ParentSelection.o \
  ControlBase.o \
  ControlMatcher.o \
  CacheControl.o \
  StatSystem.o \
  StatAPITypes.o \
  ReverseProxy.o \
  ProxyConfig.o \
  signals.o \
  Error.o \
  EventName.o \
  DiagsConfig.o \
  StatPages.o \
  PluginVC.o \
  AbstractBuffer.o \
  Transform.o \
  Prefetch.o \
  Update.o \
  Plugin.o \
  PluginDB.o \
  InkAPI.o \
  FetchSM.o \
  InkIOCoreAPI.o \
  InkXml.o \
  http/libhttp.a \
  http/remap/libhttp_remap.a \
  congest/libCongestionControl.a \
  logging/liblogging.a \
  stats/libstats.a \
  hdrs/libhdrs.a \
  mgmt/preparse/libpreparse.a \
  mgmt/utils/libutils_p.a \
  mgmt/libmgmt_p.a \
  $(top_builddir)/iocore/utils/libinkutils.a \
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
  $(top_builddir)/lib/ts/libts.a \
  @LIBTHREAD@ @LIBSOCKET@ @LIBNSL@ @LIBRESOLV@ @LIBRT@ \
  @LIBPCRE@ @LIBSSL@ @LIBTCL@ @LIBDL@ @LIBEV@ \
  @LIBEXPAT@ @LIBDEMANGLE@ @LIBMLD@ @LIBEXC@ @LIBICONV@ @LIBM@ @LIBPROFILER@ \
  @LIBEXECINFO@

if BUILD_V2STATS
  traffic_sac_LDADD += StatSystemV2.o
endif

if BUILD_TESTS
  traffic_sac_LDADD += RegressionSM.o \
    TestHook.o
endif

libClusterHashStandalone_a_SOURCES = \
  ClusterHashStandalone.cc \
  $(top_srcdir)/lib/ts/ParseRules.cc

libTrafficServerStandalone_a_SOURCES = \
  signals.cc \
  Error.cc \
  ProxyConfig.cc \
  EventName.cc \
  DiagsConfig.cc \
  StatPages.cc \
  StatSystem.cc \
  StatAPITypes.cc \
  AbstractBuffer.cc \
  Initialize.cc

if BUILD_V2STATS
  libTrafficServerStandalone_a_SOURCES += StatSystemV2.cc
endif


test_StateEventLogger_SOURCES =	StateEventLogger.cc

test_ClusterHashStandalone_SOURCES = test_ClusterHashStandalone.c
test_ClusterHashStandalone_LDADD = libClusterHashStandalone.a

test_xml_parser_SOURCES = test_xml_parser.cc InkXml.cc DiagsConfig.cc

versiondir = $(pkgsysconfdir)

install-data-local:
	$(INSTALL) -d -o $(pkgsysuser) -g $(pkgsysgroup) $(DESTDIR)$(pkglocalstatedir) $(DESTDIR)$(pkglogdir) $(DESTDIR)$(pkgruntimedir) $(DESTDIR)$(pkgsysconfdir) $(DESTDIR)$(pkgsysconfdir)/internal $(DESTDIR)$(pkgdatadir) $(DESTDIR)$(pkgcachedir)

install-data-hook:
	-chown -R $(pkgsysuser):$(pkgsysgroup) $(DESTDIR)$(pkgsysconfdir) $(DESTDIR)$(pkgdatadir)
	-echo "<TS_VERSION> $(PACKAGE_VERSION)" > $(DESTDIR)$(pkgsysconfdir)/trafficserver-release

install-exec-local:
	$(INSTALL) -d -o $(pkgsysuser) -g $(pkgsysgroup) $(DESTDIR)$(pkglibexecdir)

install-exec-hook:
	-chown -R $(pkgsysuser):$(pkgsysgroup) $(DESTDIR)$(pkglibexecdir)

uninstall-hook:
	-rm -rf $(DESTDIR)$(pkglocalstatedir) $(DESTDIR)$(pkglogdir) $(DESTDIR)$(pkgruntimedir) $(DESTDIR)$(pkgsysconfdir) $(DESTDIR)$(pkglibexecdir) $(DESTDIR)$(pkgcachedir)


