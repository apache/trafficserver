# Makefile.am for the traffic/iocore/hostdb hierarchy
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
  -I$(top_srcdir)/proxy \
  -I$(top_srcdir)/proxy/hdrs \
  -I$(top_srcdir)/proxy/http \
  -I$(top_srcdir)/mgmt \
  -I$(top_srcdir)/mgmt/utils

EXTRA_DIST = I_HostDB.h

noinst_LIBRARIES = libinkhostdb.a

libinkhostdb_a_SOURCES = \
  HostDB.cc \
  I_HostDB.h \
  I_HostDBProcessor.h \
  Inline.cc \
  MultiCache.cc \
  P_HostDB.h \
  P_HostDBProcessor.h \
  P_MultiCache.h

#test_UNUSED_SOURCES = \
#  test_I_HostDB.cc \
#  test_P_HostDB.cc
