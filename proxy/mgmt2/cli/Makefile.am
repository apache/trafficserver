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
  -I$(top_srcdir)/proxy/mgmt2 \
  -I$(top_srcdir)/proxy/mgmt2/api2 \
  -I$(top_srcdir)/proxy/mgmt2/api2/include \
  -I$(top_srcdir)/proxy/mgmt2/cluster \
  -I$(top_srcdir)/proxy/mgmt2/preparse \
  -I$(top_srcdir)/proxy/mgmt2/utils \
  -I$(top_srcdir)/proxy/mgmt2/web2 \
  -I$(top_srcdir)/proxy \
  -DPREFIX=\"$(prefix)\" \
  -DPKGSYSCONFDIR=\"$(pkgsysconfdir)\" \
  -DPKGLOCALSTATEDIR=\"$(pkglocalstatedir)\"

MGMT_DEFS = @MGMT_DEFS@
DEFS += $(MGMT_DEFS)

noinst_LIBRARIES = libcli_lm.a libcli_tl.a

bin_PROGRAMS = traffic_line

libcli_tl_a_SOURCES = \
  clientCLI.cc \
  clientCLI.h \
  CliUtils.cc \
  CliUtils.h

libcli_lm_a_SOURCES = \
  AbsEventHandler.h \
  CLI.cc \
  CLI.h \
  CLIconfigure.cc \
  CLIconfigure.h \
  CLIeventHandler.cc \
  CLIeventHandler.h \
  CLIhelp.cc \
  CLIlineBuffer.cc \
  CLIlineBuffer.h \
  CLImonitor.cc \
  CLImonitor.h \
  CliUtils.cc \
  CliUtils.h \
  FSM.cc \
  FSM.h

traffic_line_SOURCES = \
  clientCLI.h \
  CliMain.cc

traffic_line_LDFLAGS = @EXTRA_CXX_LDFLAGS@
traffic_line_LDADD = \
  libcli_tl.a \
  $(top_builddir)/libinktomi++/libinktomi++.a \
  $(top_builddir)/librecords/libreclocal.a \
  @LIBTHREAD@ @LIBSOCKET@ @LIBNSL@ @LIBRESOLV@ \
  @LIBTCL@ @LIBRT@ @LIBDL@ @LIBICONV@ @LIBEXECINFO@
