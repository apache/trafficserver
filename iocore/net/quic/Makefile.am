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
  test_QUICAckFrameCreator \
  test_QUICFlowController \
  test_QUICFrame \
  test_QUICFrameDispatcher \
  test_QUICLossDetector \
  test_QUICHandshake \
  test_QUICHandshakeProtocol \
  test_QUICIncomingFrameBuffer \
  test_QUICInvariants \
  test_QUICKeyGenerator \
  test_QUICPacket \
  test_QUICPacketFactory \
  test_QUICStream \
  test_QUICStreamManager \
  test_QUICStreamState \
  test_QUICTransportParameters \
  test_QUICTypeUtil \
  test_QUICVersionNegotiator

TESTS = $(check_PROGRAMS)

test_CPPFLAGS = \
  $(AM_CPPFLAGS) \
  -I$(abs_top_srcdir)/tests/include

test_LDADD = \
  libquic.a \
  $(top_builddir)/lib/records/librecords_p.a \
  $(top_builddir)/lib/ts/libtsutil.la \
  $(top_builddir)/iocore/eventsystem/libinkevent.a \
  $(top_builddir)/mgmt/libmgmt_p.la \
  $(top_builddir)/proxy/shared/libUglyLogStubs.a \
  @LIBTCL@ @OPENSSL_LIBS@ \
  @HWLOC_LIBS@

test_event_main_SOURCES = \
  ./test/event_processor_main.cc \
  ./test/stub.cc

test_main_SOURCES = \
  ./test/main.cc \
  ./test/stub.cc

test_QUICAckFrameCreator_CPPFLAGS = $(test_CPPFLAGS)
test_QUICAckFrameCreator_LDFLAGS = @AM_LDFLAGS@
test_QUICAckFrameCreator_LDADD = $(test_LDADD)
test_QUICAckFrameCreator_SOURCES = \
  $(test_main_SOURCES) \
  ./test/test_QUICAckFrameCreator.cc

test_QUICFlowController_CPPFLAGS = $(test_CPPFLAGS)
test_QUICFlowController_LDFLAGS = @AM_LDFLAGS@
test_QUICFlowController_LDADD = $(test_LDADD)
test_QUICFlowController_SOURCES = \
  $(test_main_SOURCES) \
  ./test/test_QUICFlowController.cc

test_QUICFrame_CPPFLAGS = $(test_CPPFLAGS)
test_QUICFrame_LDFLAGS = @AM_LDFLAGS@
test_QUICFrame_LDADD = $(test_LDADD)
test_QUICFrame_SOURCES = \
  $(test_main_SOURCES) \
  ./test/test_QUICFrame.cc

test_QUICFrameDispatcher_CPPFLAGS = $(test_CPPFLAGS)
test_QUICFrameDispatcher_LDFLAGS = @AM_LDFLAGS@
test_QUICFrameDispatcher_LDADD = $(test_LDADD)
test_QUICFrameDispatcher_SOURCES = \
  $(test_event_main_SOURCES) \
  ./test/test_QUICFrameDispatcher.cc

test_QUICLossDetector_CPPFLAGS = $(test_CPPFLAGS)
test_QUICLossDetector_LDFLAGS = @AM_LDFLAGS@
test_QUICLossDetector_LDADD = $(test_LDADD)
test_QUICLossDetector_SOURCES = \
  $(test_event_main_SOURCES) \
  ./test/test_QUICLossDetector.cc

test_QUICHandshake_CPPFLAGS = $(test_CPPFLAGS)
test_QUICHandshake_LDFLAGS = @AM_LDFLAGS@
test_QUICHandshake_LDADD = $(test_LDADD)
test_QUICHandshake_SOURCES = \
  $(test_event_main_SOURCES) \
  ./test/test_QUICHandshake.cc

test_QUICHandshakeProtocol_CPPFLAGS = $(test_CPPFLAGS)
test_QUICHandshakeProtocol_LDFLAGS = @AM_LDFLAGS@
test_QUICHandshakeProtocol_LDADD = $(test_LDADD)
test_QUICHandshakeProtocol_SOURCES = \
  $(test_main_SOURCES) \
  ./test/test_QUICHandshakeProtocol.cc

test_QUICIncomingFrameBuffer_CPPFLAGS = $(test_CPPFLAGS)
test_QUICIncomingFrameBuffer_LDFLAGS = @AM_LDFLAGS@
test_QUICIncomingFrameBuffer_LDADD = $(test_LDADD)
test_QUICIncomingFrameBuffer_SOURCES = \
  $(test_event_main_SOURCES) \
  ./test/test_QUICIncomingFrameBuffer.cc

test_QUICInvariants_CPPFLAGS = $(test_CPPFLAGS)
test_QUICInvariants_LDFLAGS = @AM_LDFLAGS@
test_QUICInvariants_LDADD = $(test_LDADD)
test_QUICInvariants_SOURCES = \
  $(test_main_SOURCES) \
  ./test/test_QUICInvariants.cc

test_QUICKeyGenerator_CPPFLAGS = $(test_CPPFLAGS)
test_QUICKeyGenerator_LDFLAGS = @AM_LDFLAGS@
test_QUICKeyGenerator_LDADD = $(test_LDADD)
test_QUICKeyGenerator_SOURCES = \
  $(test_main_SOURCES) \
  ./test/test_QUICKeyGenerator.cc

test_QUICPacket_CPPFLAGS = $(test_CPPFLAGS)
test_QUICPacket_LDFLAGS = @AM_LDFLAGS@
test_QUICPacket_LDADD = $(test_LDADD)
test_QUICPacket_SOURCES = \
  $(test_main_SOURCES) \
  ./test/test_QUICPacket.cc

test_QUICPacketFactory_CPPFLAGS = $(test_CPPFLAGS)
test_QUICPacketFactory_LDFLAGS = @AM_LDFLAGS@
test_QUICPacketFactory_LDADD = $(test_LDADD)
test_QUICPacketFactory_SOURCES = \
  $(test_main_SOURCES) \
  ./test/test_QUICPacketFactory.cc

test_QUICStream_CPPFLAGS = $(test_CPPFLAGS)
test_QUICStream_LDFLAGS = @AM_LDFLAGS@
test_QUICStream_LDADD = $(test_LDADD)
test_QUICStream_SOURCES = \
  $(test_event_main_SOURCES) \
  ./test/test_QUICStream.cc

test_QUICStreamManager_CPPFLAGS = $(test_CPPFLAGS)
test_QUICStreamManager_LDFLAGS = @AM_LDFLAGS@
test_QUICStreamManager_LDADD = $(test_LDADD)
test_QUICStreamManager_SOURCES = \
  $(test_event_main_SOURCES) \
  ./test/test_QUICStreamManager.cc

test_QUICStreamState_CPPFLAGS = $(test_CPPFLAGS)
test_QUICStreamState_LDFLAGS = @AM_LDFLAGS@
test_QUICStreamState_LDADD = $(test_LDADD)
test_QUICStreamState_SOURCES = \
  $(test_main_SOURCES) \
  ./test/test_QUICStreamState.cc

test_QUICTransportParameters_CPPFLAGS = $(test_CPPFLAGS)
test_QUICTransportParameters_LDFLAGS = @AM_LDFLAGS@
test_QUICTransportParameters_LDADD = $(test_LDADD)
test_QUICTransportParameters_SOURCES = \
  $(test_main_SOURCES) \
  ./test/test_QUICTransportParameters.cc

test_QUICTypeUtil_CPPFLAGS = $(test_CPPFLAGS)
test_QUICTypeUtil_LDFLAGS = @AM_LDFLAGS@
test_QUICTypeUtil_LDADD = $(test_LDADD)
test_QUICTypeUtil_SOURCES = \
  $(test_main_SOURCES) \
  ./test/test_QUICTypeUtil.cc

test_QUICVersionNegotiator_CPPFLAGS = $(test_CPPFLAGS)
test_QUICVersionNegotiator_LDFLAGS = @AM_LDFLAGS@
test_QUICVersionNegotiator_LDADD = $(test_LDADD)
test_QUICVersionNegotiator_SOURCES = \
  $(test_main_SOURCES) \
  ./test/test_QUICVersionNegotiator.cc

#
# clang-tidy
#
include $(top_srcdir)/build/tidy.mk

tidy-local: $(DIST_SOURCES)
	$(CXX_Clang_Tidy)
