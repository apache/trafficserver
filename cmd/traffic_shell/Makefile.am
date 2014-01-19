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
  $(iocore_include_dirs) \
  -I$(top_srcdir)/lib \
  -I$(top_srcdir)/lib/ts \
  -I$(top_srcdir)/mgmt/api/include

bin_PROGRAMS = traffic_shell
traffic_shell_SOURCES = \
  cliAppInit.cc \
  CliCreateCommands.h \
  traffic_shell.cc \
  CliMgmtUtils.h \
  ConfigCmd.h \
  createArgument.h \
  createCommand.h \
  definitions.h \
  hashtable.h \
  ShowCmd.h

noinst_LIBRARIES = libtrafficshell.a
libtrafficshell_a_SOURCES = \
  CliCreateCommands.cc \
  CliCreateCommands.h \
  CliDisplay.cc \
  CliDisplay.h \
  CliMgmtUtils.cc \
  CliMgmtUtils.h \
  cliParseArg.cc \
  commandOptions.h \
  ConfigCmd.cc \
  ConfigCmd.h \
  ConfigUpgradeCmd.h \
  ConfigUpgradeReadCmd.cc \
  ConfigUpgradeWriteCmd.cc \
  createArgument.cc \
  createArgument.h \
  createCommand.cc \
  createCommand.h \
  definitions.h \
  hashtable.cc \
  hashtable.h \
  processArgument.cc \
  ShowCmd.cc \
  ShowCmd.h \
  UtilCmds.cc \
  UtilCmds.h

traffic_shell_LDFLAGS =	@EXTRA_CXX_LDFLAGS@ @LIBTOOL_LINK_FLAGS@
traffic_shell_LDADD = \
  libtrafficshell.a \
  $(top_builddir)/mgmt/api/libtsmgmtshare.la \
  $(top_builddir)/mgmt/api/libtsmgmt.la \
  $(top_builddir)/mgmt/utils/libutils_lm.a \
  $(top_builddir)/lib/ts/libtsutil.la \
  $(LIBTCL) $(LIBREADLINE) \
  @LIBRESOLV@ @LIBEXPAT@ @OPENSSL_LIBS@ \
  @LIBPCRE@ @LIBREADLINE@ \
  -lm
