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

AM_CPPFLAGS = \
  $(iocore_include_dirs) \
  -I$(top_srcdir)/lib \
  -I$(top_srcdir)/lib/records \
  -I$(top_srcdir)/proxy/api/ts \
  -I$(top_srcdir)/proxy \
  -I$(top_srcdir)/proxy/http \
  -I$(top_srcdir)/proxy/http/remap \
  -I$(top_srcdir)/proxy/hdrs \
  -I$(top_srcdir)/proxy/shared \
  -I$(top_srcdir)/mgmt \
  -I$(top_srcdir)/mgmt/utils

EXTRA_DIST = LogStandalone.cc

noinst_LIBRARIES = liblogging.a liblogcollation.a

liblogging_a_SOURCES = \
  Log.cc \
  Log.h \
  LogAccess.cc \
  LogAccess.h \
  LogAccessHttp.cc \
  LogAccessHttp.h \
  LogAccessICP.cc \
  LogAccessICP.h \
  LogBuffer.cc \
  LogBuffer.h \
  LogBufferSink.h \
  LogConfig.cc \
  LogConfig.h \
  LogField.cc \
  LogField.h \
  LogFieldAliasMap.cc \
  LogFieldAliasMap.h \
  LogFile.cc \
  LogFile.h \
  LogFilter.cc \
  LogFilter.h \
  LogFormat.cc \
  LogFormat.h \
  LogHost.cc \
  LogHost.h \
  LogLimits.h \
  LogObject.cc \
  LogObject.h \
  LogSock.cc \
  LogSock.h \
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

#test_UNUSED_SOURCES = \
#  LogAccessTest.cc \
#  LogAccessTest.h
