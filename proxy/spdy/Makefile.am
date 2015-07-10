# Makefile.am for the traffic/iocore/net hierarchy
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
  -I$(top_srcdir)/proxy/api/ts \
  -I$(top_srcdir)/lib \
  -I$(top_srcdir)/lib/records \
  -I$(top_srcdir)/proxy \
  -I$(top_srcdir)/proxy/http \
  -I$(top_srcdir)/proxy/api \
  -I$(top_srcdir)/proxy/hdrs \
  -I$(top_srcdir)/proxy/shared \
  -I$(top_srcdir)/proxy/spdy \
  -I$(top_srcdir)/mgmt \
  -I$(top_srcdir)/mgmt/utils \
  @SPDYLAY_CFLAGS@

noinst_LIBRARIES = libspdy.a

libspdy_a_SOURCES = \
  SpdySessionAccept.h \
  SpdySessionAccept.cc

if BUILD_SPDY
libspdy_a_SOURCES += \
  SpdyCallbacks.cc \
  SpdyCallbacks.h \
  SpdyClientSession.cc \
  SpdyClientSession.h \
  SpdyCommon.cc \
  SpdyCommon.h
endif
