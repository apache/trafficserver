# proxy Makefile.am
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

# Note that hdrs is targeted from ../Makefile.am
noinst_LIBRARIES = \
  liberror.a \
  libdiagsconfig.a \
  libUglyLogStubs.a \
  libxml.a

AM_CPPFLAGS = \
  $(iocore_include_dirs) \
  -I$(top_srcdir)/lib/records \
  -I$(top_srcdir)/mgmt \
  -I$(top_srcdir)/mgmt/api \
  -I$(top_srcdir)/mgmt/api/include \
  -I$(top_srcdir)/mgmt/utils \
  -I$(top_srcdir) \
  -I$(top_srcdir)/proxy/api/ts \
  -I$(top_srcdir)/proxy \
  -I$(top_srcdir)/proxy/http \
  -I$(top_srcdir)/proxy/hdrs \
  -I$(top_srcdir)/proxy/logging \
  -I$(top_srcdir)/lib

liberror_a_SOURCES = \
  Error.cc

libdiagsconfig_a_SOURCES = \
  DiagsConfig.cc

libUglyLogStubs_a_SOURCES = \
  UglyLogStubs.cc

libxml_a_SOURCES = \
  InkXml.cc \
  InkXml.h
