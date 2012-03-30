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
if STANDALONE_IOCORE
noinst_LIBRARIES = libTrafficServerStandalone.a
bin_PROGRAMS =
noinst_PROGRAMS =
else
SUBDIRS = congest http logging config stats
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
  -I$(top_srcdir)/lib/ts \
  -I$(srcdir)/http \
  -I$(srcdir)/logging \
  -I$(srcdir)/http/remap  \
  -I$(srcdir)/hdrs \
  -I$(top_srcdir)/mgmt \
  -I$(top_srcdir)/mgmt/preparse \
  -I$(top_srcdir)/mgmt/utils \
  -I$(srcdir)/api/ts \
  -I$(builddir) \
  -I$(builddir)/api/ts \
  -I$(top_srcdir)/lib

noinst_HEADERS = \
  ConfigParse.h \
  Show.h

EXTRA_PROGRAMS = \
  test_xml_parser

EXTRA_LIBRARIES	= \
  libTrafficServerStandalone.a

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
  Transform.cc \
  Transform.h \
  TransformInternal.h \
  Update.cc \
  Update.h

if BUILD_TESTS
  traffic_server_SOURCES +=  InkAPITest.cc  \
    RegressionSM.h \
    RegressionSM.cc \
    TestHook.cc
endif

traffic_server_LDFLAGS = @EXTRA_CXX_LDFLAGS@ @LIBTOOL_LINK_FLAGS@
traffic_server_LDADD = \
  http/libhttp.a \
  http/remap/libhttp_remap.a \
  congest/libCongestionControl.a \
  logging/liblogging.a \
  logging/liblogcollation.a \
  stats/libstats.a \
  hdrs/libhdrs.a  \
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
  @LIBTHREAD@ @LIBSOCKET@ @LIBNSL@ @LIBRESOLV@ @LIBRT@ \
  @LIBPCRE@ @LIBSSL@ @LIBTCL@ @LIBDL@ \
  @LIBEXPAT@ @LIBDEMANGLE@ @LIBICONV@ @LIBCAP@ @LIBHWLOC@ \
  @LIBMLD@ @LIBEXC@ -lm @LIBPROFILER@ @LIBEXECINFO@

traffic_logcat_SOURCES = logcat.cc
traffic_logcat_LDFLAGS = @EXTRA_CXX_LDFLAGS@ @LIBTOOL_LINK_FLAGS@
traffic_logcat_LDADD = \
  signals.o \
  InkXml.o \
  DiagsConfig.o \
  UglyLogStubs.o \
  logging/liblogging.a \
  $(top_builddir)/mgmt/utils/libutils_p.a \
  $(top_builddir)/mgmt/libmgmt_p.a \
  $(top_builddir)/lib/records/librecprocess.a \
  $(top_builddir)/iocore/eventsystem/libinkevent.a \
  $(top_builddir)/lib/ts/libtsutil.la \
  @LIBTHREAD@ @LIBSOCKET@ @LIBNSL@ @LIBRESOLV@ @LIBRT@ \
  @LIBPCRE@ @LIBSSL@ @LIBTCL@ @LIBDL@ \
  @LIBEXPAT@ @LIBDEMANGLE@ @LIBMLD@ @LIBEXC@ @LIBICONV@ -lm @LIBPROFILER@ \
  @LIBEXECINFO@

traffic_logstats_SOURCES = logstats.cc
traffic_logstats_LDFLAGS = @EXTRA_CXX_LDFLAGS@ @LIBTOOL_LINK_FLAGS@
traffic_logstats_LDADD = \
  signals.o \
  InkXml.o \
  DiagsConfig.o \
  UglyLogStubs.o \
  logging/liblogging.a \
  $(top_builddir)/mgmt/utils/libutils_p.a \
  $(top_builddir)/mgmt/libmgmt_p.a \
  $(top_builddir)/lib/records/librecprocess.a \
  $(top_builddir)/iocore/eventsystem/libinkevent.a \
  $(top_builddir)/lib/ts/libtsutil.la \
  @LIBTHREAD@ @LIBSOCKET@ @LIBNSL@ @LIBRESOLV@ @LIBRT@ \
  @LIBPCRE@ @LIBSSL@ @LIBTCL@ @LIBDL@ \
  @LIBEXPAT@ @LIBDEMANGLE@ @LIBMLD@ @LIBEXC@ @LIBICONV@ -lm @LIBPROFILER@ \
  @LIBEXECINFO@

traffic_sac_SOURCES = sac.cc
traffic_sac_LDFLAGS = @EXTRA_CXX_LDFLAGS@ @LIBTOOL_LINK_FLAGS@
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
  logging/liblogcollation.a \
  stats/libstats.a \
  hdrs/libhdrs.a \
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
  @LIBTHREAD@ @LIBSOCKET@ @LIBNSL@ @LIBRESOLV@ @LIBRT@ \
  @LIBPCRE@ @LIBSSL@ @LIBTCL@ @LIBDL@ \
  @LIBEXPAT@ @LIBDEMANGLE@ @LIBMLD@ @LIBEXC@ @LIBICONV@ -lm @LIBPROFILER@ \
  @LIBEXECINFO@

if BUILD_TESTS
  traffic_sac_LDADD += RegressionSM.o
endif

libTrafficServerStandalone_a_SOURCES = \
  signals.cc \
  Error.cc \
  ProxyConfig.cc \
  EventName.cc \
  DiagsConfig.cc \
  StatPages.cc \
  StatSystem.cc \
  AbstractBuffer.cc \
  Initialize.cc


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

