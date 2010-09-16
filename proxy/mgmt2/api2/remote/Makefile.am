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
  $(iocore_include_dirs) \
  -I$(top_srcdir)/proxy/mgmt2 \
  -I$(top_srcdir)/proxy/mgmt2/utils \
  -I$(top_srcdir)/proxy/mgmt2/api2 \
  -I$(top_srcdir)/proxy/mgmt2/api2/include

MGMT_DEFS = @MGMT_DEFS@
DEFS  += $(MGMT_DEFS)

noinst_PROGRAMS = traffic_api_cli_remote

noinst_LIBRARIES = libmgmtapiremote.a
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

traffic_api_cli_remote_SOURCES = APITestCliRemote.cc
traffic_api_cli_remote_LDADD = \
  libmgmtapiremote.a \
  $(top_builddir)/iocore/utils/libinkutils.a \
  $(top_builddir)/libinktomi++/libinktomi++.a \
  @LIBTHREAD@ @LIBSOCKET@ @LIBNSL@ @LIBRESOLV@ \
  @LIBTCL@ @LIBDB@ @LIBRT@ @LIBDL@ @LIBSSL@ @LIBICONV@ @LIBEXECINFO@
