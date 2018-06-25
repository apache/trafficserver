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

AM_CPPFLAGS += \
  $(iocore_include_dirs) \
  -I$(abs_top_srcdir)/lib \
  -I$(abs_top_srcdir)/lib/records \
  -I$(abs_top_srcdir)/proxy \
  -I$(abs_top_srcdir)/proxy/hdrs \
  -I$(abs_top_srcdir)/proxy/shared \
  -I$(abs_top_srcdir)/proxy/logging \
  -I$(abs_top_srcdir)/proxy/http \
  -I$(abs_top_srcdir)/proxy/hq \
  -I$(abs_top_srcdir)/mgmt \
  -I$(abs_top_srcdir)/mgmt/utils \
  $(TS_INCLUDES) \
  @OPENSSL_INCLUDES@

noinst_LIBRARIES = libquic.a

if OPENSSL_IS_BORINGSSL
QUICTLS_impl = QUICTLS_boringssl.cc
QUICKeyGenerator_impl = QUICKeyGenerator_boringssl.cc
else
QUICTLS_impl = QUICTLS_openssl.cc
QUICKeyGenerator_impl = QUICKeyGenerator_openssl.cc
endif

libquic_a_SOURCES = \
  QUICGlobals.cc \
  QUICTypes.cc \
  QUICIntUtil.cc \
  QUICPacket.cc \
  QUICFrame.cc \
  QUICFrameDispatcher.cc \
  QUICVersionNegotiator.cc \
  QUICLossDetector.cc \
  QUICStreamManager.cc \
  QUICCongestionController.cc \
  QUICFlowController.cc \
  QUICStreamState.cc \
  QUICStream.cc \
  QUICHandshake.cc \
  QUICHandshakeProtocol.cc \
  QUICTLS.cc \
  $(QUICTLS_impl) \
  QUICKeyGenerator.cc \
  $(QUICKeyGenerator_impl) \
  QUICKeyGenerator.cc \
  QUICHKDF.cc \
  QUICTransportParameters.cc \
  QUICConnectionTable.cc \
  QUICAltConnectionManager.cc \
  QUICAckFrameCreator.cc \
  QUICConfig.cc \
  QUICDebugNames.cc \
  QUICApplication.cc \
  QUICApplicationMap.cc \
  QUICIncomingFrameBuffer.cc \
  QUICPacketReceiveQueue.cc \
  QUICPacketRetransmitter.cc \
  QUICPathValidator.cc \
  QUICStatelessRetry.cc

#
# Check Programs
#

check_PROGRAMS = \
  test_libquic

test_libquic_CPPFLAGS = \
  $(AM_CPPFLAGS) \
  -I$(abs_top_srcdir)/tests/include

test_libquic_LDFLAGS = \
  @AM_LDFLAGS@

test_libquic_LDADD = \
  libquic.a \
  $(top_builddir)/lib/records/librecords_p.a \
  $(top_builddir)/lib/ts/libtsutil.la \
  $(top_builddir)/iocore/eventsystem/libinkevent.a \
  $(top_builddir)/mgmt/libmgmt_p.la \
  $(top_builddir)/proxy/shared/libUglyLogStubs.a \
  @LIBTCL@ @OPENSSL_LIBS@ \
  @HWLOC_LIBS@

test_libquic_SOURCES = \
  ./test/event_processor_main.cc \
  ./test/stub.cc \
  ./test/test_QUICAckFrameCreator.cc \
  ./test/test_QUICFlowController.cc \
  ./test/test_QUICFrame.cc \
  ./test/test_QUICFrameDispatcher.cc \
  ./test/test_QUICLossDetector.cc \
  ./test/test_QUICHandshake.cc \
  ./test/test_QUICHandshakeProtocol.cc \
  ./test/test_QUICIncomingFrameBuffer.cc \
  ./test/test_QUICInvariants.cc \
  ./test/test_QUICKeyGenerator.cc \
  ./test/test_QUICPacket.cc \
  ./test/test_QUICPacketFactory.cc \
  ./test/test_QUICStream.cc \
  ./test/test_QUICStreamManager.cc \
  ./test/test_QUICStreamState.cc \
  ./test/test_QUICTransportParameters.cc \
  ./test/test_QUICTypeUtil.cc \
  ./test/test_QUICVersionNegotiator.cc

TESTS = $(check_PROGRAMS)

#
# clang-tidy
#
include $(top_srcdir)/build/tidy.mk

tidy-local: $(DIST_SOURCES)
	$(CXX_Clang_Tidy)
