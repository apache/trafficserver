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

AM_CPPFLAGS = \
  -I$(top_srcdir)/libinktomi++ \
  -I$(top_srcdir)/librecords \
  -I$(top_srcdir)/proxy/mgmt2/utils \
  -I$(top_srcdir)/proxy/mgmt2/api2 \
  -I$(top_srcdir)/proxy/mgmt2/api2/include \
  -I$(top_srcdir)/proxy/mgmt2 \
  $(iocore_include_dirs) \
  -DPREFIX=\"$(prefix)\" \
  -DPKGLIBEXECDIR=\"$(pkglibexecdir)\" \
  -DPKGLOCALSTATEDIR=\"$(pkglocalstatedir)\" \
  -DPKGLOGDIR=\"$(pkglogdir)\" \
  -DPKGSYSCONFDIR=\"$(pkgsysconfdir)\"

MGMT_DEFS = @MGMT_DEFS@
DEFS += $(MGMT_DEFS)

noinst_PROGRAMS = traffic_vip_config traffic_net_config traffic_time_config traffic_shmem_clean traffic_mcast_snoop

noinst_LIBRARIES = libinksysapi.a libinkconfigapi.a

traffic_vip_config_SOURCES = VIPConfig.cc
traffic_vip_config_LDFLAGS = @EXTRA_CXX_LDFLAGS@
traffic_vip_config_LDADD = \
  $(top_builddir)/libinktomi++/libinktomi++.a \
  $(top_builddir)/iocore/utils/libinkutils.a \
  @LIBSOCKET@ @LIBTCL@ @LIBRT@ @LIBDL@ @LIBICONV@ @LIBEXECINFO@

traffic_net_config_SOURCES = NetConfig.cc
traffic_net_config_LDFLAGS = @EXTRA_CXX_LDFLAGS@
traffic_net_config_LDADD = \
  $(top_builddir)/libinktomi++/libinktomi++.a \
  $(top_builddir)/iocore/utils/libinkutils.a \
  @LIBSOCKET@ @LIBTCL@ @LIBRT@ @LIBICONV@ @LIBEXECINFO@

traffic_time_config_SOURCES = TimeConfig.cc
traffic_time_config_LDFLAGS = @EXTRA_CXX_LDFLAGS@

traffic_shmem_clean_SOURCES = ShmemClean.cc
traffic_shmem_clean_LDFLAGS = @EXTRA_CXX_LDFLAGS@

traffic_mcast_snoop_SOURCES = McastSnoop.cc
traffic_mcast_snoop_LDFLAGS = @EXTRA_CXX_LDFLAGS@
traffic_mcast_snoop_LDADD = \
  $(top_builddir)/libinktomi++/libinktomi++.a \
  $(top_builddir)/iocore/utils/libinkutils.a \
  @LIBSOCKET@ @LIBNSL@ @LIBTCL@ @LIBRT@ @LIBTHREAD@ @LIBDL@ @LIBICONV@ @LIBEXECINFO@

libinksysapi_a_SOURCES = SysAPI.cc SysAPI.h

libinkconfigapi_a_SOURCES = \
  ConfigAPI.cc \
  ConfigAPI.h \
  SysAPI.cc \
  SysAPI.h
