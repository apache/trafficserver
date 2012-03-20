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
  -I$(top_srcdir)/lib \
  -I$(top_srcdir)/lib/ts \
  -I$(top_srcdir)/mgmt \
  -I$(top_srcdir)/mgmt/utils \
  -I$(top_srcdir)/mgmt/api \
  -I$(top_srcdir)/mgmt/api/include

MGMT_DEFS = @MGMT_DEFS@
DEFS  += $(MGMT_DEFS)

if BUILD_TESTS
  noinst_PROGRAMS = traffic_api_cli_remote
endif

lib_LTLIBRARIES = libtsmgmt.la

libtsmgmt_la_LDFLAGS = -version-info @TS_LIBTOOL_VERSION@
libtsmgmt_la_LIBADD = @LIBOBJS@ $(top_builddir)/lib/ts/libtsutil.la

libtsmgmt_la_SOURCES = \
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

if BUILD_TESTS
  traffic_api_cli_remote_SOURCES = APITestCliRemote.cc
endif

traffic_api_cli_remote_LDADD = \
  libtsmgmt.la \
  $(top_builddir)/lib/ts/libtsutil.la \
  @LIBTHREAD@ @LIBSOCKET@ @LIBNSL@ @LIBRESOLV@ \
  @LIBTCL@ @LIBRT@ @LIBDL@ @LIBSSL@ @LIBICONV@ @LIBEXECINFO@
