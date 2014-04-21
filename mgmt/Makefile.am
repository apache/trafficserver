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

SUBDIRS = cluster preparse utils web2 stats api

# This is for traffic_manager only (local manager)
AM_CPPFLAGS = $(ink_with_modules_local) \
  $(iocore_include_dirs) \
  -I$(top_srcdir)/lib/records \
  -I$(top_srcdir)/lib/ts \
  -I$(top_srcdir)/proxy \
  -I$(top_srcdir)/proxy/hdrs \
  -I$(top_srcdir)/proxy/shared \
  -I$(top_srcdir)/mgmt \
  -I$(top_srcdir)/mgmt/api/include \
  -I$(top_srcdir)/mgmt/cluster \
  -I$(top_srcdir)/mgmt/preparse \
  -I$(top_srcdir)/mgmt/stats \
  -I$(top_srcdir)/mgmt/utils \
  -I$(top_srcdir)/mgmt/web2 \
  -I$(top_srcdir)/lib

MGMT_DEFS = @MGMT_DEFS@
DEFS += $(MGMT_DEFS)

noinst_LIBRARIES = libmgmt_p.a

bin_PROGRAMS = traffic_manager

# Compile this for "process manager"
libmgmt_p_a_CPPFLAGS = \
  $(iocore_include_dirs) \
  -I$(top_srcdir)/mgmt/api/include \
  -I$(top_srcdir)/mgmt/cluster \
  -I$(top_srcdir)/mgmt/preparse \
  -I$(top_srcdir)/mgmt/utils \
  -I$(top_srcdir)/mgmt/web2 \
  -I$(top_builddir)/proxy \
  -I$(top_builddir)/proxy/api/ts \
  -I$(top_srcdir)/proxy \
  -I$(top_srcdir)/proxy/hdrs \
  -I$(top_srcdir)/lib/records \
  -I$(top_srcdir)/lib/ts \
  -I$(top_srcdir)/lib

libmgmt_p_a_SOURCES = \
  BaseManager.cc \
  BaseManager.h \
  MgmtDefs.h \
  ProcessManager.cc \
  ProcessManager.h \
  ProxyConfig.cc \
  ProxyConfig.h \
  RecordsConfig.cc \
  RecordsConfig.h

traffic_manager_SOURCES = \
  AddConfigFilesHere.cc \
  Alarms.cc \
  Alarms.h \
  BaseManager.cc \
  BaseManager.h \
  FileManager.cc \
  FileManager.h \
  LocalManager.cc \
  LocalManager.h \
  Main.cc \
  Main.h \
  MgmtDefs.h \
  MultiFile.cc \
  MultiFile.h \
  RecordsConfig.cc \
  RecordsConfig.h \
  Rollback.cc \
  Rollback.h

traffic_manager_LDFLAGS = @EXTRA_CXX_LDFLAGS@ @EXPAT_LDFLAGS@ @LIBTOOL_LINK_FLAGS@
traffic_manager_LDADD = \
  cluster/libcluster.a \
  stats/libstats.a \
  web2/libweb.a \
  api/libmgmtapilocal.a \
  api/libtsmgmtshare.la \
  utils/libutils_lm.a \
  $(top_builddir)/proxy/hdrs/libhdrs.a \
  $(top_builddir)/lib/records/libreclocal.a \
  $(top_builddir)/lib/ts/libtsutil.la \
  $(top_builddir)/iocore/eventsystem/libinkevent.a \
  $(top_builddir)/proxy/shared/liberror.a \
  $(top_builddir)/proxy/shared/libdiagsconfig.a \
  @LIBRESOLV@ @LIBEXPAT@ @LIBPCRE@ @LIBTCL@ @LIBCAP@ @hwloc_LIBS@ \
  -lm

# Must do it this way or the dependencies aren't detected.
if BUILD_WCCP
traffic_manager_LDADD += \
  $(top_builddir)/lib/wccp/libwccp.a \
  $(top_builddir)/lib/tsconfig/libtsconfig.la
endif

traffic_manager_LDADD += @OPENSSL_LIBS@

