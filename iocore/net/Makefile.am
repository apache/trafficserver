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

if ENABLE_QUIC
SUBDIRS = quic
endif

AM_CPPFLAGS += \
	$(iocore_include_dirs) \
	-I$(abs_top_srcdir)/include \
	-I$(abs_top_srcdir)/lib \
	-I$(abs_top_srcdir)/proxy \
	-I$(abs_top_srcdir)/proxy/hdrs \
	-I$(abs_top_srcdir)/proxy/shared \
	-I$(abs_top_srcdir)/proxy/logging \
	-I$(abs_top_srcdir)/mgmt \
	-I$(abs_top_srcdir)/mgmt/utils \
	-I$(abs_top_srcdir)/proxy/http \
	$(TS_INCLUDES) \
	@OPENSSL_INCLUDES@ \
	@BORINGOCSP_INCLUDES@ \
	@YAMLCPP_INCLUDES@

TESTS = $(check_PROGRAMS)

check_PROGRAMS = test_certlookup test_UDPNet test_libinknet
noinst_LIBRARIES = libinknet.a

test_certlookup_LDFLAGS = \
	@AM_LDFLAGS@ \
	@OPENSSL_LDFLAGS@ \
	@YAMLCPP_LDFLAGS@

test_certlookup_SOURCES = \
	test_certlookup.cc \
	SSLCertLookup.cc

test_certlookup_LDADD = \
	@OPENSSL_LIBS@ \
	$(top_builddir)/iocore/eventsystem/libinkevent.a \
	$(top_builddir)/src/tscore/libtscore.la $(top_builddir)/src/tscpp/util/libtscpputil.la \
	$(top_builddir)/proxy/ParentSelectionStrategy.o \
	@YAMLCPP_LIBS@

test_UDPNet_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	$(iocore_include_dirs) \
	-I$(abs_top_srcdir)/proxy \
	-I$(abs_top_srcdir)/proxy/hdrs \
	-I$(abs_top_srcdir)/proxy/http \
	-I$(abs_top_srcdir)/proxy/logging \
	-I$(abs_top_srcdir)/mgmt \
	-I$(abs_top_srcdir)/mgmt/utils \
	@OPENSSL_INCLUDES@

test_UDPNet_LDFLAGS = \
	@AM_LDFLAGS@ \
	@OPENSSL_LDFLAGS@ \
	@YAMLCPP_LDFLAGS@

test_UDPNet_LDADD = \
	libinknet.a \
	$(top_builddir)/iocore/eventsystem/libinkevent.a \
	$(top_builddir)/mgmt/libmgmt_p.la \
	$(top_builddir)/lib/records/librecords_p.a \
	$(top_builddir)/src/tscore/libtscore.la $(top_builddir)/src/tscpp/util/libtscpputil.la \
	$(top_builddir)/proxy/hdrs/libhdrs.a \
	$(top_builddir)/proxy/ParentSelectionStrategy.o \
	@HWLOC_LIBS@ @OPENSSL_LIBS@ @LIBPCRE@ @YAMLCPP_LIBS@

test_UDPNet_SOURCES = \
	libinknet_stub.cc \
	test_I_UDPNet.cc

test_libinknet_SOURCES = \
	unit_tests/test_ProxyProtocol.cc

test_libinknet_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	$(iocore_include_dirs) \
	-I$(abs_top_srcdir)/tests/include \
	-I$(abs_top_srcdir)/proxy \
	-I$(abs_top_srcdir)/proxy/hdrs \
	-I$(abs_top_srcdir)/proxy/http \
	-I$(abs_top_srcdir)/proxy/logging \
	-I$(abs_top_srcdir)/mgmt \
	-I$(abs_top_srcdir)/mgmt/utils \
	@OPENSSL_INCLUDES@

test_libinknet_LDFLAGS = \
	@AM_LDFLAGS@ \
	@OPENSSL_LDFLAGS@ \
	@YAMLCPP_LDFLAGS@

test_libinknet_LDADD = \
	libinknet.a \
	$(top_builddir)/iocore/eventsystem/libinkevent.a \
	$(top_builddir)/mgmt/libmgmt_p.la \
	$(top_builddir)/lib/records/librecords_p.a \
	$(top_builddir)/src/tscore/libtscore.la \
	$(top_builddir)/src/tscpp/util/libtscpputil.la \
	$(top_builddir)/proxy/ParentSelectionStrategy.o \
	@HWLOC_LIBS@ @OPENSSL_LIBS@ @LIBPCRE@ @YAMLCPP_LIBS@

libinknet_a_SOURCES = \
	ALPNSupport.cc \
	BIO_fastopen.cc \
	BIO_fastopen.h \
	BoringSSLUtils.cc \
	BoringSSLUtils.h \
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
	YamlSNIConfig.h \
	YamlSNIConfig.cc \
	Net.cc \
	NetVConnection.cc \
	P_ALPNSupport.h \
	P_SNIActionPerformer.h \
	P_CompletionUtil.h \
	P_Connection.h \
	P_Net.h \
	P_NetAccept.h \
	P_NetVConnection.h \
	P_Socks.h \
	P_SSLCertLookup.h \
	P_SSLConfig.h \
	P_SSLSecret.h \
	P_SSLNetAccept.h \
	P_SSLNetProcessor.h \
	P_SSLNetVConnection.h \
	P_SSLNextProtocolAccept.h \
	P_SSLNextProtocolSet.h \
	P_SSLSNI.h \
	P_SSLUtils.h \
	P_SSLClientCoordinator.h \
	P_SSLClientUtils.h \
	P_TLSKeyLogger.h \
	P_OCSPStapling.h \
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
	ProxyProtocol.h \
	ProxyProtocol.cc \
	Socks.cc \
	SSLCertLookup.cc \
	SSLClientCoordinator.cc \
	SSLClientUtils.cc \
	SSLConfig.cc \
	SSLSecret.cc \
	SSLDiags.cc \
	SSLInternal.cc \
	SSLNetAccept.cc \
	SSLNetProcessor.cc \
	SSLNetVConnection.cc \
	SSLNextProtocolAccept.cc \
	SSLNextProtocolSet.cc \
	SSLSNIConfig.cc \
	SSLStats.cc \
	SSLSessionCache.cc \
	SSLSessionTicket.cc \
	SSLUtils.cc \
	OCSPStapling.cc \
	TLSBasicSupport.cc \
	TLSKeyLogger.cc \
	TLSSessionResumptionSupport.cc \
	TLSSNISupport.cc \
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

if ENABLE_QUIC
libinknet_a_SOURCES += \
  P_QUICClosedConCollector.h \
  P_QUICPacketHandler.h \
  P_QUICNet.h \
  P_QUICNetProcessor.h \
  P_QUICNetVConnection.h \
  P_QUICNextProtocolAccept.h \
  QUICClosedConCollector.cc \
  QUICPacketHandler.cc \
  QUICMultiCertConfigLoader.cc \
  QUICNet.cc \
  QUICNetProcessor.cc \
  QUICNetVConnection.cc \
  QUICNextProtocolAccept.cc
endif

if BUILD_TESTS
libinknet_a_SOURCES += \
	NetVCTest.cc \
	P_NetVCTest.h
endif

include $(top_srcdir)/build/tidy.mk

clang-tidy-local: $(DIST_SOURCES)
	$(CXX_Clang_Tidy)
