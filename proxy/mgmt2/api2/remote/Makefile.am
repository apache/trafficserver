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
  -I$(top_srcdir)/proxy/mgmt2 \
  -I$(top_srcdir)/proxy/mgmt2/cli \
  -I$(top_srcdir)/proxy/mgmt2/cluster \
  -I$(top_srcdir)/proxy/mgmt2/preparse \
  -I$(top_srcdir)/proxy/mgmt2/utils \
  -I$(top_srcdir)/proxy/mgmt2/web \
  -I$(top_srcdir)/proxy \
  -I$(top_srcdir)/proxy/iocore \
  -I$(top_srcdir)/proxy/hdrs \
  -I$(top_srcdir)/proxy/mgmt2/api2 \
  -I$(top_srcdir)/proxy/mgmt2/api2/remote \
  -I$(top_srcdir)/proxy/mgmt2/api2/include \
  -DPREFIX=\"$(prefix)\" \
  -DPKGLIBEXECDIR=\"$(pkglibexecdir)\" \
  -DPKGLOCALSTATEDIR=\"$(pkglocalstatedir)\" \
  -DPKGLOGDIR=\"$(pkglogdir)\" \
  -DPKGSYSCONFDIR=\"$(pkgsysconfdir)\"

MGMT_DEFS = @MGMT_DEFS@
DEFS  += $(MGMT_DEFS)

noinst_LIBRARIES = libmgmtapiremote.a 

bin_PROGRAMS = api_cli_remote

libmgmtapiremote_a_SOURCES = \
  ../CfgContextImpl.cc \
  ../CfgContextManager.cc \
  ../CfgContextUtils.cc \
  ../CoreAPIShared.cc \
  ../EventCallback.cc \
  ../GenericParser.cc \
  ../INKMgmtAPI.cc \
  CoreAPIRemote.cc \
  EventRegistration.cc \
  EventRegistration.h \
  NetworkUtilsRemote.cc \
  NetworkUtilsRemote.h

api_cli_remote_SOURCES = APITestCliRemote.cc 
api_cli_remote_LDADD = \
  libmgmtapiremote.a \
  $(top_builddir)/libinktomi++/libinktomi++.a \
  @LIBTHREAD@ @LIBSOCKET@ @LIBNSL@ @LIBRESOLV@ \
  @LIBTCL@ @LIBDB@ @LIBSQLITE3@ @LIBRT@ @LIBDL@ @LIBSSL@ @LIBICONV@
