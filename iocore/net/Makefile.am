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
  -I$(top_srcdir)/lib \
  -I$(top_srcdir)/lib/records \
  -I$(top_srcdir)/proxy \
  -I$(top_srcdir)/proxy/hdrs \
  -I$(top_srcdir)/proxy/shared \
  -I$(top_srcdir)/proxy/logging \
  -I$(top_srcdir)/mgmt \
  -I$(top_srcdir)/mgmt/utils \
  -I$(top_srcdir)/proxy/api/ts \
  -I$(top_srcdir)/proxy/http \
  @OPENSSL_INCLUDES@

TESTS = $(check_PROGRAMS)

check_PROGRAMS = test_certlookup
noinst_LIBRARIES = libinknet.a

test_certlookup_LDFLAGS = \
  @EXTRA_CXX_LDFLAGS@ \
  @LIBTOOL_LINK_FLAGS@ \
  @OPENSSL_LDFLAGS@

test_certlookup_SOURCES = \
  test_certlookup.cc \
  SSLCertLookup.cc

test_certlookup_LDADD = \
  @OPENSSL_LIBS@ \
  $(top_builddir)/lib/ts/libtsutil.la \
  $(top_builddir)/iocore/eventsystem/libinkevent.a

libinknet_a_SOURCES = \
  Connection.cc \
  I_Net.h \
  I_NetProcessor.h \
  I_NetVConnection.h \
  I_Socks.h \
  I_UDPConnection.h \
  I_UDPNet.h \
  I_UDPPacket.h \
  Inline.cc \
  I_SessionAccept.h \
  SessionAccept.cc \
  Net.cc \
  NetVConnection.cc \
  P_CompletionUtil.h \
  P_Connection.h \
  P_InkBulkIO.h \
  P_LibBulkIO.h \
  P_Net.h \
  P_NetAccept.h \
  P_NetVConnection.h \
  P_Socks.h \
  P_SSLCertLookup.h \
  P_SSLConfig.h \
  P_SSLNetAccept.h \
  P_SSLNetProcessor.h \
  P_SSLNetVConnection.h \
  P_SSLNextProtocolAccept.h \
  P_SSLNextProtocolSet.h \
  P_SSLUtils.h \
  P_SSLClientUtils.h \
  P_OCSPStapling.h \
  P_Socks.h \
  P_UDPConnection.h \
  P_UDPIOEvent.h \
  P_UDPNet.h \
  P_UDPPacket.h \
  P_UnixCompletionUtil.h \
  P_UnixNet.h \
  P_UnixNetProcessor.h \
  P_UnixNetState.h \
  P_UnixNetVConnection.h \
  P_UnixPollDescriptor.h \
  P_UnixUDPConnection.h \
  Socks.cc \
  SSLCertLookup.cc \
  SSLSessionCache.cc \
  SSLConfig.cc \
  SSLInternal.cc \
  SSLNetAccept.cc \
  SSLNetProcessor.cc \
  SSLNetVConnection.cc \
  SSLNextProtocolAccept.cc \
  SSLNextProtocolSet.cc \
  SSLUtils.cc \
  SSLClientUtils.cc \
  OCSPStapling.cc \
  Socks.cc \
  UDPIOEvent.cc \
  UnixConnection.cc \
  UnixNet.cc \
  UnixNetAccept.cc \
  UnixNetPages.cc \
  UnixNetProcessor.cc \
  UnixNetVConnection.cc \
  UnixUDPConnection.cc \
  UnixUDPNet.cc \
  SSLDynlock.cc

#test_UNUSED_SOURCES = \
#  NetTest-http-server.c \
#  test_I_Net.cc \
#  test_I_UDPNet.cc \
#  test_P_Net.cc \
#  test_P_UDPNet.cc

if BUILD_TESTS
  libinknet_a_SOURCES += \
     NetVCTest.cc \
     P_NetVCTest.h
endif
