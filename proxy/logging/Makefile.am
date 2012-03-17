# Makefile.am for Logging
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

DEFS += -DIOCORE_LOG_COLLATION

AM_CPPFLAGS = \
  $(iocore_include_dirs) \
  -I$(top_srcdir)/lib \
  -I$(top_srcdir)/lib/records \
  -I$(top_srcdir)/lib/ts \
  -I$(top_builddir)/proxy \
  -I$(top_builddir)/proxy/api/ts \
  -I$(top_srcdir)/proxy \
  -I$(top_srcdir)/proxy/http \
  -I$(top_srcdir)/proxy/http/remap \
  -I$(top_srcdir)/proxy/hdrs \
  -I$(top_srcdir)/mgmt \
  -I$(top_srcdir)/mgmt/preparse \
  -I$(top_srcdir)/mgmt/utils

EXTRA_DIST = LogStandalone.cc

noinst_LIBRARIES = liblogging.a liblogcollation.a

liblogging_a_SOURCES = \
  LogAccess.cc \
  LogAccess.h \
  LogAccessHttp.cc \
  LogAccessHttp.h \
  LogAccessICP.cc \
  LogAccessICP.h \
  LogBuffer.cc \
  LogBuffer.h \
  LogBufferSink.h \
  Log.cc \
  Log.h \
  LogConfig.cc \
  LogConfig.h \
  LogFieldAliasMap.cc \
  LogFieldAliasMap.h \
  LogField.cc \
  LogField.h \
  LogFile.cc \
  LogFile.h \
  LogFilter.cc \
  LogFilter.h \
  LogFormat.cc \
  LogFormat.h \
  LogFormatType.h \
  LogLimits.h \
  LogObject.cc \
  LogObject.h \
  LogSock.cc \
  LogSock.h \
  LogHost.cc \
  LogHost.h \
  LogUtils.cc \
  LogUtils.h

liblogcollation_a_SOURCES = \
  LogCollationAccept.cc \
  LogCollationAccept.h \
  LogCollationBase.h \
  LogCollationClientSM.cc \
  LogCollationClientSM.h \
  LogCollationHostSM.cc \
  LogCollationHostSM.h
