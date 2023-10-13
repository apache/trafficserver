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
	-I$(abs_top_srcdir)/proxy/http \
	-I$(abs_top_srcdir)/proxy/http/remap \
	$(TS_INCLUDES) \
	@OPENSSL_INCLUDES@ \
	@YAMLCPP_INCLUDES@ \
	@SWOC_INCLUDES@

TESTS = $(check_PROGRAMS)
TESTS_ENVIRONMENT = LSAN_OPTIONS=suppressions=$(abs_top_srcdir)/ci/asan_leak_suppression/regression.txt

check_PROGRAMS = test_certlookup test_UDPNet test_libinknet
noinst_LIBRARIES = libinknet.a

test_certlookup_LDFLAGS = \
	@AM_LDFLAGS@ \
	@OPENSSL_LDFLAGS@ \
	@YAMLCPP_LDFLAGS@ \
	@SWOC_LDFLAGS@

test_certlookup_SOURCES = \
	test_certlookup.cc \
	SSLCertLookup.cc

test_certlookup_LDADD = \
	$(top_builddir)/iocore/eventsystem/libinkevent.a \
	$(top_builddir)/src/tscore/libtscore.a \
	$(top_builddir)/src/tscpp/util/libtscpputil.la \
	$(top_builddir)/proxy/ParentSelectionStrategy.o \
	@YAMLCPP_LIBS@ \
	@SWOC_LIBS@ \
	@OPENSSL_LIBS@ \
	@LIBPCRE@ \
	@LIBCAP@

test_UDPNet_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	$(iocore_include_dirs) \
	-I$(abs_top_srcdir)/proxy \
	-I$(abs_top_srcdir)/proxy/hdrs \
	-I$(abs_top_srcdir)/proxy/http \
	@OPENSSL_INCLUDES@ \
	@SWOC_INCLUDES@ \
	@LIBPCRE@


test_UDPNet_LDFLAGS = \
	@AM_LDFLAGS@ \
	@OPENSSL_LDFLAGS@ \
	@YAMLCPP_LDFLAGS@ \
	@SWOC_LDFLAGS@ \
	@LIBPCRE@


test_UDPNet_LDADD = \
	libinknet.a \
	$(top_builddir)/iocore/eventsystem/libinkevent.a \
	$(top_builddir)/src/records/librecords_p.a \
	$(top_builddir)/proxy/hdrs/libhdrs.a \
	$(top_builddir)/src/tscore/libtscore.a \
	$(top_builddir)/src/tscpp/util/libtscpputil.la \
	$(top_builddir)/src/api/libtsapicore.a \
	$(top_builddir)/proxy/ParentSelectionStrategy.o \
	@HWLOC_LIBS@ @OPENSSL_LIBS@ @LIBPCRE@ @YAMLCPP_LIBS@ @SWOC_LIBS@ @LIBPCRE@ @LIBCAP@
if ENABLE_QUIC
test_UDPNet_LDADD += \
	$(top_builddir)/iocore/net/quic/libquic.a
endif

test_UDPNet_SOURCES = \
	libinknet_stub.cc \
	test_I_UDPNet.cc

test_libinknet_SOURCES = \
	libinknet_stub.cc \
	unit_tests/unit_test_main.cc \
	unit_tests/test_ProxyProtocol.cc \
	unit_tests/test_SSLSNIConfig.cc \
	unit_tests/test_YamlSNIConfig.cc

test_libinknet_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	$(iocore_include_dirs) \
	-I$(abs_top_srcdir)/lib/catch2 \
	-I$(abs_top_srcdir)/proxy \
	-I$(abs_top_srcdir)/proxy/hdrs \
	-I$(abs_top_srcdir)/proxy/http \
	-DLIBINKNET_UNIT_TEST_DIR="$(abs_top_srcdir)/iocore/net/unit_tests" \
	@OPENSSL_INCLUDES@

test_libinknet_LDFLAGS = \
	@AM_LDFLAGS@ \
	@OPENSSL_LDFLAGS@ \
	@YAMLCPP_LDFLAGS@ \
	@SWOC_LDFLAGS@

test_libinknet_LDADD = \
	libinknet.a \
	$(top_builddir)/iocore/eventsystem/libinkevent.a \
	$(top_builddir)/src/records/librecords_p.a \
	$(top_builddir)/proxy/hdrs/libhdrs.a \
	$(top_builddir)/src/tscore/libtscore.a \
	$(top_builddir)/src/api/libtsapicore.a \
	$(top_builddir)/src/tscpp/util/libtscpputil.la \
	$(top_builddir)/proxy/ParentSelectionStrategy.o \
	@HWLOC_LIBS@ @OPENSSL_LIBS@ @LIBPCRE@ @YAMLCPP_LIBS@ @SWOC_LIBS@ @LIBCAP@

if ENABLE_QUIC
test_libinknet_LDADD += \
	$(top_builddir)/iocore/net/quic/libquic.a
endif

libinknet_a_SOURCES = \
	AcceptOptions.h \
	ALPNSupport.cc \
	AsyncSignalEventIO.cc \
	AsyncSignalEventIO.h \
	BIO_fastopen.cc \
	BIO_fastopen.h \
	BoringSSLUtils.cc \
	BoringSSLUtils.h \
	Connection.cc \
	EventIO.h \
	EventIO.cc \
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
	NetAcceptEventIO.h \
	NetAcceptEventIO.cc \
	NetHandler.h \
	NetHandler.cc \
	NetVCOptions.h \
	NetVCOptions.cc \
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
	P_SSLUtils.h \
	P_SSLClientCoordinator.h \
	P_SSLClientUtils.h \
	P_TLSKeyLogger.h \
	P_OCSPStapling.h \
	P_UDPConnection.h \
	P_UDPIOEvent.h \
	P_UDPNet.h \
	P_UnixCompletionUtil.h \
	P_UnixNet.h \
	P_UnixNetProcessor.h \
	P_UnixNetState.h \
	P_UnixNetVConnection.h \
	P_UnixPollDescriptor.h \
	P_UnixUDPConnection.h \
	PollCont.h \
	PollCont.cc \
	PreWarmManager.cc \
	ProxyProtocol.h \
	ProxyProtocol.cc \
	ReadWriteEventIO.h \
	ReadWriteEventIO.cc \
	Socks.cc \
	SSLAPIHooks.h \
	SSLAPIHooks.cc \
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
	SSLSNIConfig.h \
	SSLSNIConfig.cc \
	SSLStats.cc \
	SSLSessionCache.cc \
	SSLSessionTicket.cc \
	SSLUtils.cc \
	OCSPStapling.cc \
	TLSBasicSupport.cc \
	TLSEarlyDataSupport.cc \
	TLSKeyLogger.cc \
	TLSSessionResumptionSupport.cc \
	TLSSNISupport.cc \
	TLSTunnelSupport.cc \
	TLSCertSwitchSupport.cc \
	UDPEventIO.h \
	UDPEventIO.cc \
	UDPIOEvent.cc \
	UnixConnection.cc \
	UnixNet.cc \
	UnixNetAccept.cc \
	UnixNetPages.cc \
	UnixNetProcessor.cc \
	UnixNetVConnection.cc \
	UnixUDPConnection.cc \
	UnixUDPNet.cc \
	SSLDynlock.cc \
	SNIActionPerformer.cc

if ENABLE_QUIC
libinknet_a_SOURCES += \
	P_QUICClosedConCollector.h \
	P_QUICPacketHandler.h \
	P_QUICNetProcessor.h \
	P_QUICNetVConnection.h \
	P_QUICNextProtocolAccept.h \
	QUICClosedConCollector.cc \
	QUICMultiCertConfigLoader.cc \
	QUICNet.cc \
	QUICNetProcessor_quiche.cc \
	QUICNetVConnection_quiche.cc \
	QUICNextProtocolAccept_quiche.cc \
	QUICPacketHandler_quiche.cc \
	QUICSupport.cc
endif

if BUILD_TESTS
libinknet_a_SOURCES += \
	NetVCTest.cc \
	P_NetVCTest.h
endif

include $(top_srcdir)/mk/tidy.mk

clang-tidy-local: $(DIST_SOURCES)
	$(CXX_Clang_Tidy)
