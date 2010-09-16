# Makefile.am for Http
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

SUBDIRS = remap

AM_CPPFLAGS = \
  $(iocore_include_dirs) \
  -I$(top_srcdir)/proxy \
  -I$(top_srcdir)/librecords \
  -I$(top_srcdir)/proxy/mgmt2 \
  -I$(top_srcdir)/proxy/mgmt2/preparse \
  -I$(top_srcdir)/proxy/mgmt2/utils \
  -I$(top_srcdir)/proxy/mgmt2/web2 \
  -I$(top_srcdir)/proxy/hdrs \
  -I$(top_srcdir)/proxy/http2/remap \
  -I$(top_srcdir)/proxy/logging

noinst_HEADERS = HttpProxyServerMain.h
noinst_LIBRARIES = libhttp.a

libhttp_a_SOURCES = \
  Hash_Table.cc \
  Hash_Table.h \
  HttpAccept.cc \
  HttpAccept.h \
  HttpAssert.h \
  HttpBodyFactory.cc \
  HttpBodyFactory.h \
  HttpCacheSM.cc \
  HttpCacheSM.h \
  HttpClientSession.cc \
  HttpClientSession.h \
  HttpConfig.cc \
  HttpConfig.h \
  HttpConnectionCount.cc \
  HttpConnectionCount.h \
  HttpDebugNames.cc \
  HttpDebugNames.h \
  HttpMessageBody.cc \
  HttpMessageBody.h \
  HttpPages.cc \
  HttpPages.h \
  HttpProxyServerMain.cc \
  HttpServerSession.cc \
  HttpServerSession.h \
  HttpSessionManager.cc \
  HttpSessionManager.h \
  HttpSM.cc \
  HttpSM.h \
  HttpTransactCache.cc \
  HttpTransactCache.h \
  HttpTransact.cc \
  HttpTransact.h \
  HttpTransactHeaders.cc \
  HttpTransactHeaders.h \
  HttpTunnel.cc \
  HttpTunnel.h \
  HttpUpdateSM.cc \
  HttpUpdateSM.h \
  HttpUpdateTester.cc \
  remap/AclFiltering.cc \
  remap/AclFiltering.h \
  remap/RemapPluginInfo.cc \
  remap/RemapPluginInfo.h \
  remap/RemapPlugins.cc \
  remap/RemapPlugins.h \
  remap/RemapProcessor.cc \
  remap/RemapProcessor.h \
  remap/StringHash.cc \
  remap/StringHash.h \
  remap/UrlMapping.cc \
  remap/UrlMapping.h \
  remap/UrlRewrite.cc \
  remap/UrlRewrite.h \
  remap/Trie.h \
  remap/UrlMappingPathIndex.h \
  remap/UrlMappingPathIndex.cc

