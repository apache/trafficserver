# proxy Makefile.am
#
#

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
  logcat \
  logstats \
  sac \
  cust_log_fmt_cnvrt

dist_noinst_SCRIPTS = ts-instantiate.pl
dist_bin_SCRIPTS = example_alarm_bin.sh example_prep.sh

noinst_HEADERS = \
  api/include/InkAPIaaa.h \
  api/include/InkAPI.h \
  api/include/InkAPIHughes.h \
  api/include/InkAPIPrivateFrozen.h \
  api/include/InkAPIPrivate.h \
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

EXTRA_DIST = InkAPITestTool.cc

traffic_server_CPPFLAGS = \
  -DPREFIX=\"$(prefix)\" \
  -DPKGSYSCONFDIR=\"$(pkgsysconfdir)\" \
  -DPKGLOCALSTATEDIR=\"$(pkglocalstatedir)\" \
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
  RemapAPI.h \
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
  $(top_builddir)/iocore/cache/libinkcache.a \
  $(top_builddir)/iocore/cluster/libinkcluster.a \
  $(top_builddir)/iocore/aio/libinkaio.a \
  $(top_builddir)/iocore/net/libinknet.a \
  $(top_builddir)/iocore/eventsystem/libinkevent.a \
  $(top_builddir)/librecords/librecprocess.a \
  $(top_builddir)/iocore/eventsystem/libinkevent.a \
  $(top_builddir)/libinktomi++/libinktomi++.a \
  @LIBTHREAD@ @LIBSOCKET@ @LIBNSL@ @LIBRESOLV@ @LIBRT@ \
  @LIBREGEX@ @LIBDB@ @LIBSSL@ @LIBTCL@ @LIBDL@ \
  @LIBEXPAT@ @LIBDEMANGLE@ \
  @LIBMLD@ @LIBEXC@ -lm

logcat_SOURCES = logcat.cc
logcat_LDFLAGS = @EXTRA_CXX_LDFLAGS@
logcat_LDADD = \
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
  $(top_builddir)/iocore/cache/libinkcache.a \
  $(top_builddir)/iocore/cluster/libinkcluster.a \
  $(top_builddir)/iocore/utils/libinkutils.a \
  $(top_builddir)/iocore/aio/libinkaio.a \
  $(top_builddir)/iocore/net/libinknet.a \
  $(top_builddir)/iocore/eventsystem/libinkevent.a \
  $(top_builddir)/librecords/librecprocess.a \
  $(top_builddir)/iocore/eventsystem/libinkevent.a \
  $(top_builddir)/libinktomi++/libinktomi++.a \
  @LIBTHREAD@ @LIBSOCKET@ @LIBNSL@ @LIBRESOLV@ @LIBRT@ \
  @LIBREGEX@ @LIBDB@ @LIBSSL@ @LIBTCL@ @LIBDL@ \
  @LIBEXPAT@ @LIBDEMANGLE@ @LIBMLD@ @LIBEXC@ -lm

logstats_CPPFLAGS = \
  -DPKGLOGDIR=\"$(pkglogdir)\" \
  $(AM_CPPFLAGS)
logstats_SOURCES = logstats.cc
logstats_LDFLAGS = @EXTRA_CXX_LDFLAGS@
logstats_LDADD = \
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
  $(top_builddir)/iocore/cache/libinkcache.a \
  $(top_builddir)/iocore/cluster/libinkcluster.a \
  $(top_builddir)/iocore/utils/libinkutils.a \
  $(top_builddir)/iocore/aio/libinkaio.a \
  $(top_builddir)/iocore/net/libinknet.a \
  $(top_builddir)/iocore/eventsystem/libinkevent.a \
  $(top_builddir)/librecords/librecprocess.a \
  $(top_builddir)/iocore/eventsystem/libinkevent.a \
  $(top_builddir)/libinktomi++/libinktomi++.a \
  @LIBTHREAD@ @LIBSOCKET@ @LIBNSL@ @LIBRESOLV@ @LIBRT@ \
  @LIBREGEX@ @LIBDB@ @LIBSSL@ @LIBTCL@ @LIBDL@ \
  @LIBEXPAT@ @LIBDEMANGLE@ @LIBMLD@ @LIBEXC@ -lm

sac_SOURCES = sac.cc
sac_LDFLAGS = @EXTRA_CXX_LDFLAGS@
sac_LDADD = \
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
  $(top_builddir)/iocore/cache/libinkcache.a \
  $(top_builddir)/iocore/cluster/libinkcluster.a \
  $(top_builddir)/iocore/utils/libinkutils.a \
  $(top_builddir)/iocore/aio/libinkaio.a \
  $(top_builddir)/iocore/net/libinknet.a \
  $(top_builddir)/iocore/eventsystem/libinkevent.a \
  $(top_builddir)/librecords/librecprocess.a \
  $(top_builddir)/libinktomi++/libinktomi++.a \
  @LIBTHREAD@ @LIBSOCKET@ @LIBNSL@ @LIBRESOLV@ @LIBRT@ \
  @LIBREGEX@ @LIBDB@ @LIBSSL@ @LIBTCL@ @LIBDL@ \
  @LIBEXPAT@ @LIBDEMANGLE@ @LIBMLD@ @LIBEXC@ -lm

cust_log_fmt_cnvrt_SOURCES = cust_log_fmt_cnvrt.cc
cust_log_fmt_cnvrt_LDADD = \
  $(top_builddir)/libinktomi++/libinktomi++.a \
  @LIBTHREAD@ \
  @LIBTCL@ @LIBRT@ @LIBDL@

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
	$(INSTALL) -d -o nobody -g nobody $(pkglocalstatedir) $(pkglogdir)
	$(INSTALL) -d $(pkgsysconfdir) $(pkglibexecdir)

uninstall-local:
	-rm -rf $(pkglocalstatedir) $(pkglogdir) $(pkgsysconfdir) $(pkglibexecdir)
