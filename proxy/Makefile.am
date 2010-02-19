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
SUBDIRS = congest dns_cache http2 hdrs logging mgmt2 config stats hdrs
else
SUBDIRS = congest dns_cache http2 hdrs logging mgmt2 config stats
endif

AM_CPPFLAGS = \
  -I$(top_srcdir)/libinktomi++ \
  -I$(top_srcdir)/proxy/dns_cache \
  -I$(top_srcdir)/librecords \
  $(iocore_include_dirs) \
  -I$(srcdir)/http2 \
  -I$(srcdir)/logging \
  -I$(srcdir)/http2/remap  \
  -I$(srcdir)/hdrs \
  -I$(srcdir)/mgmt2 \
  -I$(srcdir)/mgmt2/preparse \
  -I$(srcdir)/mgmt2/utils \
  -I$(srcdir)/mgmt2/web2 \
  -I$(srcdir)/api/include

bin_PROGRAMS = \
  traffic_server \
  traffic_logcat \
  traffic_logstats \
  traffic_sac

noinst_PROGRAMS =  traffic_cust_log_fmt_cnvrt

noinst_HEADERS = \
  api/include/ts.h \
  api/include/remap.h \
  api/include/ts_private.h \
  api/include/ts_private_frozen.h \
  api/include/InkAPIHughes.h \
  api/include/InkAPIaaa.h \
  api/include/InkAPIPrivateIOCore.h \
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

traffic_server_CPPFLAGS = \
  -DPREFIX=\"$(prefix)\" \
  -DPKGSYSCONFDIR=\"$(pkgsysconfdir)\" \
  -DPKGLOCALSTATEDIR=\"$(pkglocalstatedir)\" \
  -DPKGLOGDIR=\"$(pkglogdir)\" \
  -DPKGSYSUSER=\"$(pkgsysuser)\" \
  $(AM_CPPFLAGS)

traffic_server_SOURCES = \
  AbstractBuffer.cc \
  AbstractBuffer.h \
  CacheControl.cc \
  CacheControl.h \
  CacheInspectorAllow.cc \
  CacheInspectorAllow.h \
  Config.cc \
  Config.h \
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
  InkAPIInternal.h \
  InkAPITest.cc \
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
  TestHook.cc \
  Transform.cc \
  Transform.h \
  TransformInternal.h \
  Update.cc \
  Update.h

traffic_server_LDFLAGS = @EXTRA_CXX_LDFLAGS@
traffic_server_LDADD = \
  http2/libhttp.a \
  congest/libCongestionControl.a \
  dns_cache/libdnscache.a \
  logging/liblogging.a \
  stats/libstats.a \
  hdrs/libhdrs.a  \
  mgmt2/preparse/libpreparse.a \
  mgmt2/utils/libutils_p.a \
  mgmt2/libmgmt_p.a \
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
  $(top_builddir)/librecords/librecprocess.a \
  $(top_builddir)/iocore/eventsystem/libinkevent.a \
  $(top_builddir)/libinktomi++/libinktomi++.a \
  @LIBTHREAD@ @LIBSOCKET@ @LIBNSL@ @LIBRESOLV@ @LIBRT@ \
  @LIBREGEX@ @LIBPCRE@ @LIBDB@ @LIBSQLITE3@ @LIBSSL@ @LIBTCL@ @LIBDL@ \
  @LIBEXPAT@ @LIBDEMANGLE@ @LIBICONV@ \
  @LIBMLD@ @LIBEXC@ @LIBM@

traffic_logcat_CPPFLAGS = \
  -DPREFIX=\"$(prefix)\" \
  -DPKGLOGDIR=\"$(pkglogdir)\" \
  -DPKGLOCALSTATEDIR=\"$(pkglocalstatedir)\" \
  $(AM_CPPFLAGS)
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
  CacheInspectorAllow.o \
  ReverseProxy.o \
  Config.o \
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
  TestHook.o \
  Plugin.o \
  PluginDB.o \
  InkAPI.o \
  InkIOCoreAPI.o \
  InkXml.o \
  http2/libhttp.a \
  logging/liblogging.a \
  stats/libstats.a \
  hdrs/libhdrs.a \
  congest/libCongestionControl.a \
  dns_cache/libdnscache.a \
  mgmt2/preparse/libpreparse.a \
  mgmt2/utils/libutils_p.a \
  mgmt2/libmgmt_p.a \
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
  $(top_builddir)/librecords/librecprocess.a \
  $(top_builddir)/iocore/eventsystem/libinkevent.a \
  $(top_builddir)/libinktomi++/libinktomi++.a \
  @LIBTHREAD@ @LIBSOCKET@ @LIBNSL@ @LIBRESOLV@ @LIBRT@ \
  @LIBREGEX@ @LIBPCRE@ @LIBDB@ @LIBSQLITE3@ @LIBSSL@ @LIBTCL@ @LIBDL@ \
  @LIBEXPAT@ @LIBDEMANGLE@ @LIBMLD@ @LIBEXC@ @LIBICONV@ @LIBM@

traffic_logstats_CPPFLAGS = \
  -DPREFIX=\"$(prefix)\" \
  -DPKGLOGDIR=\"$(pkglogdir)\" \
  -DPKGLOCALSTATEDIR=\"$(pkglocalstatedir)\" \
  $(AM_CPPFLAGS)
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
  CacheInspectorAllow.o \
  ReverseProxy.o \
  Config.o \
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
  TestHook.o \
  Plugin.o \
  PluginDB.o \
  InkAPI.o \
  InkIOCoreAPI.o \
  InkXml.o \
  http2/libhttp.a \
  logging/liblogging.a \
  stats/libstats.a \
  hdrs/libhdrs.a \
  congest/libCongestionControl.a \
  dns_cache/libdnscache.a \
  mgmt2/preparse/libpreparse.a \
  mgmt2/utils/libutils_p.a \
  mgmt2/libmgmt_p.a \
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
  $(top_builddir)/librecords/librecprocess.a \
  $(top_builddir)/iocore/eventsystem/libinkevent.a \
  $(top_builddir)/libinktomi++/libinktomi++.a \
  @LIBTHREAD@ @LIBSOCKET@ @LIBNSL@ @LIBRESOLV@ @LIBRT@ \
  @LIBREGEX@ @LIBPCRE@ @LIBDB@ @LIBSQLITE3@ @LIBSSL@ @LIBTCL@ @LIBDL@ \
  @LIBEXPAT@ @LIBDEMANGLE@ @LIBMLD@ @LIBEXC@ @LIBICONV@ @LIBM@

traffic_sac_CPPFLAGS = \
  -DPREFIX=\"$(prefix)\" \
  -DPKGLOGDIR=\"$(pkglogdir)\" \
  -DPKGLOCALSTATEDIR=\"$(pkglocalstatedir)\" \
  $(AM_CPPFLAGS)
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
  CacheInspectorAllow.o \
  ReverseProxy.o \
  Config.o \
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
  TestHook.o \
  Plugin.o \
  PluginDB.o \
  InkAPI.o \
  InkIOCoreAPI.o \
  InkXml.o \
  http2/libhttp.a \
  congest/libCongestionControl.a \
  dns_cache/libdnscache.a \
  logging/liblogging.a \
  stats/libstats.a \
  hdrs/libhdrs.a \
  mgmt2/preparse/libpreparse.a \
  mgmt2/utils/libutils_p.a \
  mgmt2/libmgmt_p.a \
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
  $(top_builddir)/librecords/librecprocess.a \
  $(top_builddir)/libinktomi++/libinktomi++.a \
  @LIBTHREAD@ @LIBSOCKET@ @LIBNSL@ @LIBRESOLV@ @LIBRT@ \
  @LIBREGEX@ @LIBPCRE@ @LIBDB@ @LIBSQLITE3@ @LIBSSL@ @LIBTCL@ @LIBDL@ \
  @LIBEXPAT@ @LIBDEMANGLE@ @LIBMLD@ @LIBEXC@ @LIBICONV@ @LIBM@

traffic_cust_log_fmt_cnvrt_SOURCES = cust_log_fmt_cnvrt.cc
traffic_cust_log_fmt_cnvrt_LDADD = \
  $(top_builddir)/libinktomi++/libinktomi++.a \
  @LIBTHREAD@ \
  @LIBTCL@ @LIBRT@ @LIBICONV@ @LIBDL@

libClusterHashStandalone_a_SOURCES = \
  ClusterHashStandalone.cc \
  $(top_srcdir)/libinktomi++/ParseRules.cc

libTrafficServerStandalone_a_SOURCES = \
  signals.cc \
  Error.cc \
  Config.cc \
  EventName.cc \
  DiagsConfig.cc \
  StatPages.cc \
  StatSystem.cc \
  AbstractBuffer.cc \
  Initialize.cc

test_StateEventLogger_SOURCES =	StateEventLogger.cc

test_ClusterHashStandalone_SOURCES = test_ClusterHashStandalone.c
test_ClusterHashStandalone_LDADD = libClusterHashStandalone.a

test_xml_parser_SOURCES = test_xml_parser.cc InkXml.cc DiagsConfig.cc

versiondir = $(pkglocalstatedir)
dist_version_DATA = TS_version

install-exec-local:
	$(INSTALL) -d -o $(pkgsysuser) -g $(pkgsysgroup) $(DESTDIR)$(pkglocalstatedir) $(DESTDIR)$(pkglogdir) $(DESTDIR)$(pkgsysconfdir) $(DESTDIR)$(pkgsysconfdir)/internal
	$(INSTALL) -d $(DESTDIR)$(pkglibexecdir)

uninstall-local:
	-rm -rf $(DESTDIR)$(pkglocalstatedir) $(DESTDIR)$(pkglogdir) $(DESTDIR)$(pkgsysconfdir) $(DESTDIR)$(pkglibexecdir)

install-exec-hook:
	-echo $(DESTDIR)$(prefix) > $(DESTDIR)$(prefix)/etc/traffic_server

uninstall-hook:
	-rm -f $(DESTDIR)$(prefix)/etc/traffic_server
