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
  -I$(top_srcdir)/proxy/mgmt2

MGMT_DEFS = @MGMT_DEFS@
DEFS += $(MGMT_DEFS)

bin_PROGRAMS = vip_config net_config time_config shmem_clean mcast_snoop

noinst_LIBRARIES = libinksysapi.a libinkconfigapi.a

vip_config_SOURCES = VIPConfig.cc
vip_config_LDFLAGS = @EXTRA_CXX_LDFLAGS@
vip_config_LDADD = \
  $(top_builddir)/libinktomi++/libinktomi++.a \
  @LIBSOCKET@ @LIBTCL@ @LIBRT@ @LIBDL@ @LIBICONV@

net_config_SOURCES = NetConfig.cc
net_config_LDFLAGS = @EXTRA_CXX_LDFLAGS@
net_config_LDADD = \
  $(top_builddir)/libinktomi++/libinktomi++.a \
  @LIBSOCKET@ @LIBTCL@ @LIBRT@ @LIBICONV@

time_config_SOURCES = TimeConfig.cc
time_config_LDFLAGS = @EXTRA_CXX_LDFLAGS@

shmem_clean_SOURCES = ShmemClean.cc
shmem_clean_LDFLAGS = @EXTRA_CXX_LDFLAGS@

mcast_snoop_SOURCES = McastSnoop.cc
mcast_snoop_LDFLAGS = @EXTRA_CXX_LDFLAGS@
mcast_snoop_LDADD = \
  $(top_builddir)/libinktomi++/libinktomi++.a \
  @LIBSOCKET@ @LIBNSL@ @LIBTCL@ @LIBRT@ @LIBTHREAD@ @LIBDL@ @LIBICONV@

libinksysapi_a_SOURCES = SysAPI.cc SysAPI.h

libinkconfigapi_a_SOURCES = \
  ConfigAPI.cc \
  ConfigAPI.h \
  SysAPI.cc \
  SysAPI.h
