#
# Makefile.am for the Enterprise Management module.
#

SUBDIRS = cli cluster cop \
	preparse tools utils web2 html2 stats api2 $(CLI_DIR)

AM_CPPFLAGS = \
  -I$(top_srcdir)/librecords \
  -I$(top_srcdir)/libinktomi++ \
  -I$(top_srcdir)/proxy/mgmt2 \
  -I$(top_srcdir)/proxy/mgmt2/api2 \
  -I$(top_srcdir)/proxy/mgmt2/api2/include \
  -I$(top_srcdir)/proxy/mgmt2/cli \
  -I$(top_srcdir)/proxy/mgmt2/cluster \
  -I$(top_srcdir)/proxy/mgmt2/preparse \
  -I$(top_srcdir)/proxy/mgmt2/stats \
  -I$(top_srcdir)/proxy/mgmt2/utils \
  -I$(top_srcdir)/proxy/mgmt2/web2 \
  $(iocore_include_dirs) \
  -I$(top_srcdir)/proxy \
  -I$(top_srcdir)/proxy/hdrs \
  -DPREFIX=\"$(prefix)\" \
  -DPKGLIBEXECDIR=\"$(pkglibexecdir)\" \
  -DPKGLOCALSTATEDIR=\"$(pkglocalstatedir)\" \
  -DPKGLOGDIR=\"$(pkglogdir)\" \
  -DPKGSYSCONFDIR=\"$(pkgsysconfdir)\"

MGMT_DEFS = @MGMT_DEFS@
DEFS += $(MGMT_DEFS)

noinst_LIBRARIES = libmgmt_p.a librecords.a 

bin_PROGRAMS = traffic_manager

libmgmt_p_a_SOURCES = \
  BaseManager.cc \
  BaseManager.h \
  BaseRecords.cc \
  BaseRecords.h \
  MgmtDBM.cc \
  MgmtDBM.h \
  MgmtDefs.h \
  ProcessManager.cc \
  ProcessManager.h \
  ProcessRecords.cc \
  ProcessRecords.h \
  RecordsConfig.cc \
  RecordsConfig.h

librecords_a_CPPFLAGS = -DFOR_INSTALL $(AM_CPPFLAGS)
librecords_a_SOURCES = \
  BaseRecords.h \
  MgmtDefs.h \
  RecordsConfig.cc \
  RecordsConfig.h

traffic_manager_CPPFLAGS = -DLOCAL_MANAGER $(AM_CPPFLAGS)
traffic_manager_SOURCES = \
  $(top_srcdir)/proxy/Error.cc \
  AddConfigFilesHere.cc \
  Alarms.cc \
  Alarms.h \
  AutoConf.cc \
  AutoConf.h \
  BaseManager.cc \
  BaseManager.h \
  BaseRecords.cc \
  BaseRecords.h \
  FileManager.cc \
  FileManager.h \
  LMRecords.cc \
  LMRecords.h \
  LocalManager.cc \
  LocalManager.h \
  Main.cc \
  Main.h \
  MgmtDBM.cc \
  MgmtDBM.h \
  MgmtDefs.h \
  MultiFile.cc \
  MultiFile.h \
  RecordsConfig.cc \
  RecordsConfig.h \
  Rollback.cc \
  Rollback.h \
  StatAggregation.cc \
  StatAggregation.h \
  WebPluginList.cc \
  WebPluginList.h

traffic_manager_LDFLAGS = @EXTRA_CXX_LDFLAGS@ @EXPAT_LDFLAGS@

traffic_manager_LDADD = \
  cluster/libcluster.a \
  preparse/libpreparse.a \
  stats/libstats.a \
  web2/libweb.a \
  cli/libcli_lm.a \
  api2/libmgmtapilocal.a \
  utils/libutils_lm.a \
  $(top_builddir)/proxy/hdrs/libhdrs.a \
  $(top_builddir)/librecords/libreclocal.a \
  $(top_builddir)/libinktomi++/libinktomi++.a \
  $(top_builddir)/proxy/mgmt2/tools/libinksysapi.a \
  $(top_builddir)/proxy/mgmt2/tools/libinkconfigapi.a \
  @LIBEXPAT@ \
  @LIBSSL@ @LIBDB@ @LIBREGEX@ @LIBTCL@ @LIBICONV@ \
  @LIBM@ @LIBDL@ @LIBSOCKET@ @LIBNSL@ @LIBRESOLV@ \
  @LIBTHREAD@ @LIBRT@
