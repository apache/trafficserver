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

AM_CPPFLAGS = \
  $(iocore_include_dirs) \
  -I$(top_srcdir)/librecords \
  -I$(top_srcdir)/proxy \
  -I$(top_srcdir)/proxy/mgmt2 \
  -I$(top_srcdir)/proxy/mgmt2/preparse \
  -I$(top_srcdir)/proxy/mgmt2/utils \
  -I$(top_srcdir)/proxy/hdrs \
  -I$(top_srcdir)/proxy/http2

noinst_LIBRARIES = libhttp_remap.a

libhttp_remap_a_SOURCES = \
  AclFiltering.cc \
  AclFiltering.h \
  RemapPluginInfo.cc \
  RemapPluginInfo.h \
  RemapPlugins.cc \
  RemapPlugins.h \
  RemapProcessor.cc \
  RemapProcessor.h \
  StringHash.cc \
  StringHash.h \
  UrlMapping.cc \
  UrlMapping.h \
  UrlRewrite.cc \
  UrlRewrite.h \
  Trie.h \
  UrlMappingPathIndex.h \
  UrlMappingPathIndex.cc
