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

if BUILD_WEBUI
SUBDIRS = cli cluster cop preparse tools utils web2 html2 stats api2 $(CLI_DIR)
else
SUBDIRS = cli cluster cop preparse tools utils web2 stats api2 $(CLI_DIR)
endif

DIST_SUBDIRS = $(SUBDIRS)

AM_CPPFLAGS = \
  $(iocore_include_dirs) \
  -I$(top_srcdir)/lib/records \
  -I$(top_srcdir)/proxy \
  -I$(top_srcdir)/proxy/hdrs \
  -I$(top_srcdir)/proxy/mgmt2 \
  -I$(top_srcdir)/proxy/mgmt2/api2/include \
  -I$(top_srcdir)/proxy/mgmt2/cluster \
  -I$(top_srcdir)/proxy/mgmt2/preparse \
  -I$(top_srcdir)/proxy/mgmt2/stats \
  -I$(top_srcdir)/proxy/mgmt2/utils \
  -I$(top_srcdir)/lib/tsconfig \
  -I$(top_srcdir)/proxy/mgmt2/web2

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
  $(top_builddir)/lib/records/libreclocal.a \
  $(top_builddir)/proxy/mgmt2/tools/libinksysapi.a \
  $(top_builddir)/proxy/mgmt2/tools/libinkconfigapi.a \
  $(top_builddir)/iocore/utils/libinkutils.a \
  $(top_builddir)/lib/ts/libts.a \
  @LIBEXPAT@ @LIBPCRE@ \
  @LIBSSL@ @LIBDB@ @LIBSQLITE3@ @LIBTCL@ @LIBICONV@ \
  @LIBM@ @LIBDL@ @LIBSOCKET@ @LIBNSL@ @LIBRESOLV@ \
  @LIBTHREAD@ @LIBRT@ @LIBEXECINFO@ @LIBCAP@ @LIBWCCP@ @LIBTSCONFIG@
