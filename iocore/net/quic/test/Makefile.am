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

TESTS = $(check_PROGRAMS)
check_PROGRAMS = \
  test_QUICPacket \
  test_QUICPacketFactory \
  test_QUICFrame \
  test_QUICFrameDispatcher \
  test_QUICStreamState \
  test_QUICStream \
  test_QUICStreamManager \
  test_QUICTransportParameters \
  test_QUICKeyGenerator \
  test_QUICHandshakeProtocol \
  test_QUICLossDetector \
  test_QUICTypeUtil \
  test_QUICAckFrameCreator \
  test_QUICVersionNegotiator \
  test_QUICFlowController \
  test_QUICIncomingFrameBuffer \
  test_QUICHandshake

AM_CPPFLAGS += \
  $(iocore_include_dirs) \
  -I$(abs_top_srcdir)/lib \
  -I$(abs_top_srcdir)/lib/records \
  -I$(abs_top_srcdir)/mgmt \
  -I$(abs_top_srcdir)/mgmt/utils \
  -I$(abs_top_srcdir)/proxy \
  -I$(abs_top_srcdir)/proxy/hdrs \
  -I$(abs_top_srcdir)/proxy/http \
  -I$(abs_top_srcdir)/proxy/logging \
  -I$(abs_top_srcdir)/proxy/shared \
  -I$(abs_top_srcdir)/tests/include \
  $(TS_INCLUDES) \
  @OPENSSL_INCLUDES@

if OPENSSL_IS_BORINGSSL
QUICKeyGenerator_impl = ../QUICKeyGenerator_boringssl.cc
QUICTLS_impl = ../QUICTLS_boringssl.cc
else
QUICKeyGenerator_impl = ../QUICKeyGenerator_openssl.cc
QUICTLS_impl = ../QUICTLS_openssl.cc
endif

catch_main = \
  main.cc \
  stub_QUICConfig.cc \
  ../../SSLNextProtocolSet.cc

catch_event_processor_main = \
  event_processor_main.cc \
  stub_QUICConfig.cc \
  ../../SSLNextProtocolSet.cc

test_LDADD = \
  $(top_builddir)/lib/records/librecords_p.a \
  $(top_builddir)/lib/ts/libtsutil.la \
  $(top_builddir)/iocore/eventsystem/libinkevent.a \
  $(top_builddir)/iocore/net/quic/libquic.a \
  $(top_builddir)/mgmt/libmgmt_p.la \
  $(top_builddir)/proxy/shared/libUglyLogStubs.a \
  @LIBTCL@ \
  @HWLOC_LIBS@

#
# test_QUICPacket
#
test_QUICPacket_CPPFLAGS = \
  $(AM_CPPFLAGS)

test_QUICPacket_LDFLAGS = \
  @AM_LDFLAGS@

test_QUICPacket_SOURCES = \
  $(catch_main) \
  test_QUICPacket.cc \
  ../QUICPacket.cc \
  ../QUICHandshakeProtocol.cc \
  ../QUICTLS.cc \
  $(QUICTLS_impl) \
  ../QUICTypes.cc

test_QUICPacket_LDADD = \
  $(test_LDADD)

#
# test_QUICPacketFactory
#
test_QUICPacketFactory_CPPFLAGS = \
  $(AM_CPPFLAGS)

test_QUICPacketFactory_LDFLAGS = \
  @AM_LDFLAGS@

test_QUICPacketFactory_SOURCES = \
  $(catch_main) \
  test_QUICPacketFactory.cc \
  ../QUICPacket.cc \
  ../QUICHandshakeProtocol.cc \
  ../QUICTLS.cc \
  $(QUICTLS_impl) \
  ../QUICTypes.cc

test_QUICPacketFactory_LDADD = \
  $(test_LDADD)

#
# test_QUICFrame
#
test_QUICFrame_CPPFLAGS = \
  $(AM_CPPFLAGS)

test_QUICFrame_LDFLAGS = \
  @AM_LDFLAGS@

test_QUICFrame_SOURCES = \
  $(catch_main) \
  test_QUICFrame.cc \
  ../QUICGlobals.cc \
  ../QUICFrame.cc \
  ../QUICPacket.cc \
  ../QUICKeyGenerator.cc \
  $(QUICKeyGenerator_impl) \
  ../QUICHKDF.cc \
  ../QUICHandshakeProtocol.cc \
  ../QUICTLS.cc \
  $(QUICTLS_impl) \
  ../QUICTypes.cc \
  ../QUICIntUtil.cc \
  ../QUICStream.cc \
  ../QUICStreamState.cc \
  ../QUICIncomingFrameBuffer.cc \
  ../QUICDebugNames.cc \
  ../QUICFlowController.cc

test_QUICFrame_LDADD = \
  $(test_LDADD)

#
# test_QUICFrameDispatcher
#
test_QUICFrameDispatcher_CPPFLAGS = \
  $(AM_CPPFLAGS)

test_QUICFrameDispatcher_LDFLAGS = \
  @AM_LDFLAGS@

test_QUICFrameDispatcher_SOURCES = \
  $(catch_main) \
  test_QUICFrameDispatcher.cc \
  ../QUICGlobals.cc \
  ../QUICConfig.cc \
  ../QUICVersionNegotiator.cc \
  ../QUICFrameDispatcher.cc \
  ../QUICTransportParameters.cc \
  ../QUICStreamManager.cc \
  ../QUICApplicationMap.cc \
  ../QUICCongestionController.cc \
  ../QUICFlowController.cc \
  ../QUICLossDetector.cc \
  ../QUICFrame.cc \
  ../QUICPacket.cc \
  ../QUICStream.cc \
  ../QUICStreamState.cc \
  ../QUICIncomingFrameBuffer.cc \
  ../QUICApplication.cc \
  ../QUICHandshake.cc \
  ../QUICTypes.cc \
  ../QUICIntUtil.cc \
  ../QUICEchoApp.cc \
  ../QUICDebugNames.cc \
  ../QUICHandshakeProtocol.cc \
  ../QUICTLS.cc \
  $(QUICTLS_impl) \
  ../QUICKeyGenerator.cc \
  $(QUICKeyGenerator_impl) \
  ../QUICHKDF.cc

test_QUICFrameDispatcher_LDADD = \
  $(test_LDADD)

#
# test_QUICStreamState
#
test_QUICStreamState_CPPFLAGS = \
  $(AM_CPPFLAGS)

test_QUICStreamState_LDFLAGS = \
  @AM_LDFLAGS@

test_QUICStreamState_SOURCES = \
  $(catch_main) \
  test_QUICStreamState.cc \
  ../QUICStreamState.cc

test_QUICStreamState_LDADD = \
  $(test_LDADD)

#
# test_QUICStream
#
test_QUICStream_CPPFLAGS = \
  $(AM_CPPFLAGS)

test_QUICStream_LDFLAGS = \
  @AM_LDFLAGS@

test_QUICStream_SOURCES = \
  $(catch_event_processor_main) \
  test_QUICStream.cc \
  ../QUICStream.cc \
  ../QUICIncomingFrameBuffer.cc \
  ../QUICFrameDispatcher.cc \
  ../QUICStreamManager.cc \
  ../QUICApplicationMap.cc \
  ../QUICCongestionController.cc

test_QUICStream_LDADD = \
  $(test_LDADD)

#
# test_QUICStreamManager
#
test_QUICStreamManager_CPPFLAGS = \
  $(AM_CPPFLAGS)

test_QUICStreamManager_LDFLAGS = \
  @AM_LDFLAGS@

test_QUICStreamManager_SOURCES = \
  $(catch_event_processor_main) \
  test_QUICStreamManager.cc \
  ../QUICStream.cc \
  ../QUICIncomingFrameBuffer.cc \
  ../QUICFrameDispatcher.cc \
  ../QUICStreamManager.cc \
  ../QUICApplicationMap.cc \
  ../QUICCongestionController.cc \
  ../QUICConfig.cc

test_QUICStreamManager_LDADD = \
  $(test_LDADD)

#
# test_QUICTransportParameters
#
test_QUICTransportParameters_CPPFLAGS = \
  $(AM_CPPFLAGS)

test_QUICTransportParameters_LDFLAGS = \
  @AM_LDFLAGS@

test_QUICTransportParameters_SOURCES = \
  $(catch_main) \
  test_QUICTransportParameters.cc \
  ../QUICGlobals.cc \
  ../QUICConfig.cc \
  ../QUICApplication.cc \
  ../QUICApplicationMap.cc \
  ../QUICHandshake.cc \
  ../QUICVersionNegotiator.cc \
  ../QUICHandshakeProtocol.cc \
  ../QUICTLS.cc \
  $(QUICTLS_impl) \
  ../QUICKeyGenerator.cc \
  $(QUICKeyGenerator_impl) \
  ../QUICHKDF.cc \
  ../QUICStream.cc \
  ../QUICIncomingFrameBuffer.cc \
  ../QUICStreamState.cc \
  ../QUICStreamManager.cc \
  ../QUICFlowController.cc \
  ../QUICPacket.cc \
  ../QUICFrame.cc \
  ../QUICDebugNames.cc \
  ../QUICTransportParameters.cc \
  ../QUICTypes.cc \
  ../QUICIntUtil.cc

test_QUICTransportParameters_LDADD = \
  $(test_LDADD)

#
# test_QUICKeyGenerator
#
test_QUICKeyGenerator_CPPFLAGS = \
  $(AM_CPPFLAGS)

test_QUICKeyGenerator_LDFLAGS = \
  @AM_LDFLAGS@

test_QUICKeyGenerator_LDADD = \
  $(test_LDADD)

test_QUICKeyGenerator_SOURCES = \
  $(catch_main) \
  test_QUICKeyGenerator.cc \
  ../QUICKeyGenerator.cc \
  $(QUICKeyGenerator_impl) \
  ../QUICDebugNames.cc \
  ../QUICIncomingFrameBuffer.cc \
  ../QUICFlowController.cc \
  ../QUICPacket.cc \
  ../QUICStreamState.cc \
  ../QUICFrame.cc \
  ../QUICStream.cc \
  ../QUICHKDF.cc \
  ../QUICIntUtil.cc \
  ../QUICTypes.cc

#
# test_QUICHandshakeProtocol
#
test_QUICHandshakeProtocol_CPPFLAGS = \
  $(AM_CPPFLAGS)

test_QUICHandshakeProtocol_LDFLAGS = \
  @AM_LDFLAGS@ \
  @OPENSSL_LDFLAGS@

test_QUICHandshakeProtocol_LDADD = \
  $(test_LDADD)

test_QUICHandshakeProtocol_SOURCES = \
  $(catch_main) \
  test_QUICHandshakeProtocol.cc \
  ../QUICKeyGenerator.cc \
  $(QUICKeyGenerator_impl) \
  ../QUICHKDF.cc \
  ../QUICHandshakeProtocol.cc \
  ../QUICTLS.cc \
  $(QUICTLS_impl) \
  ../QUICHandshakeProtocol.h \
  ../QUICTypes.cc \
  ../QUICIntUtil.cc \
  ../QUICDebugNames.cc \
  ../QUICStreamState.cc \
  ../QUICFrame.cc \
  ../QUICStream.cc \
  ../QUICPacket.cc \
  ../QUICIncomingFrameBuffer.cc \
  ../QUICFlowController.cc \
  ../QUICGlobals.cc

#
# test_QUICLossDetector
#
test_QUICLossDetector_CPPFLAGS = \
  $(AM_CPPFLAGS)

test_QUICLossDetector_LDFLAGS = \
  @AM_LDFLAGS@

test_QUICLossDetector_LDADD = \
  $(test_LDADD)

test_QUICLossDetector_SOURCES = \
  $(catch_event_processor_main) \
  test_QUICLossDetector.cc \
  ../QUICLossDetector.cc \
  ../QUICTypes.cc \
  ../QUICPacket.cc \
  ../QUICHandshakeProtocol.cc \
  ../QUICTLS.cc \
  $(QUICTLS_impl) \
  ../QUICFrame.cc

#
# test_QUICTypeUtil
#
test_QUICTypeUtil_CPPFLAGS = \
  $(AM_CPPFLAGS)

test_QUICTypeUtil_LDFLAGS = \
  @AM_LDFLAGS@

test_QUICTypeUtil_LDADD = \
  $(test_LDADD)

test_QUICTypeUtil_SOURCES = \
  $(catch_main) \
  test_QUICTypeUtil.cc \
  ../QUICStream.cc \
  ../QUICIncomingFrameBuffer.cc \
  ../QUICStreamState.cc \
  ../QUICFlowController.cc \
  ../QUICDebugNames.cc \
  ../QUICFrame.cc \
  ../QUICPacket.cc \
  ../QUICKeyGenerator.cc \
  $(QUICKeyGenerator_impl) \
  ../QUICHKDF.cc \
  ../QUICHandshakeProtocol.cc \
  ../QUICTLS.cc \
  $(QUICTLS_impl) \
  ../QUICIntUtil.cc \
  ../QUICTypes.cc

#
# test_QUICAckFrameCreator
#
test_QUICAckFrameCreator_CPPFLAGS = \
  $(AM_CPPFLAGS)

test_QUICAckFrameCreator_LDFLAGS = \
  @AM_LDFLAGS@

test_QUICAckFrameCreator_LDADD = \
  $(test_LDADD)

test_QUICAckFrameCreator_SOURCES = \
  $(catch_main) \
  test_QUICAckFrameCreator.cc \
  ../QUICAckFrameCreator.cc \
  ../QUICGlobals.cc \
  ../QUICTypes.cc \
  ../QUICIntUtil.cc \
  ../QUICFrame.cc \
  ../QUICPacket.cc \
  ../QUICStream.cc \
  ../QUICIncomingFrameBuffer.cc \
  ../QUICStreamState.cc \
  ../QUICFlowController.cc \
  ../QUICDebugNames.cc \
  ../QUICKeyGenerator.cc \
  $(QUICKeyGenerator_impl) \
  ../QUICHKDF.cc \
  ../QUICHandshakeProtocol.cc \
  ../QUICTLS.cc \
  $(QUICTLS_impl)

#
# test_QUICTypeUtil
#
test_QUICVersionNegotiator_CPPFLAGS = \
  $(AM_CPPFLAGS)

test_QUICVersionNegotiator_LDFLAGS = \
  @AM_LDFLAGS@

test_QUICVersionNegotiator_LDADD = \
  $(test_LDADD)

test_QUICVersionNegotiator_SOURCES = \
  $(catch_main) \
  test_QUICVersionNegotiator.cc \
  ../QUICGlobals.cc \
  ../QUICTypes.cc \
  ../QUICIntUtil.cc \
  ../QUICPacket.cc \
  ../QUICKeyGenerator.cc \
  $(QUICKeyGenerator_impl) \
  ../QUICHKDF.cc \
  ../QUICHandshakeProtocol.cc \
  ../QUICTLS.cc \
  $(QUICTLS_impl) \
  ../QUICApplication.cc \
  ../QUICApplicationMap.cc \
  ../QUICHandshake.cc \
  ../QUICStream.cc \
  ../QUICIncomingFrameBuffer.cc \
  ../QUICStreamState.cc \
  ../QUICStreamManager.cc \
  ../QUICFlowController.cc \
  ../QUICFrame.cc \
  ../QUICDebugNames.cc \
  ../QUICVersionNegotiator.cc \
  ../QUICTransportParameters.cc \
  ../QUICConfig.cc

#
# test_QUICFlowController
#
test_QUICFlowController_CPPFLAGS = \
  $(AM_CPPFLAGS)

test_QUICFlowController_LDFLAGS = \
  @AM_LDFLAGS@

test_QUICFlowController_LDADD = \
  $(test_LDADD)

test_QUICFlowController_SOURCES = \
  $(catch_main) \
  test_QUICFlowController.cc \
  ../QUICFlowController.cc \
  ../QUICStream.cc \
  ../QUICIncomingFrameBuffer.cc \
  ../QUICStreamState.cc \
  ../QUICDebugNames.cc \
  ../QUICTypes.cc \
  ../QUICIntUtil.cc \
  ../QUICPacket.cc \
  ../QUICKeyGenerator.cc \
  $(QUICKeyGenerator_impl) \
  ../QUICHKDF.cc \
  ../QUICHandshakeProtocol.cc \
  ../QUICTLS.cc \
  $(QUICTLS_impl) \
  ../QUICFrame.cc

#
# test_QUICIncomingFrameBuffer
#
test_QUICIncomingFrameBuffer_CPPFLAGS = \
  $(AM_CPPFLAGS)

test_QUICIncomingFrameBuffer_LDFLAGS = \
  @AM_LDFLAGS@

test_QUICIncomingFrameBuffer_SOURCES = \
  $(catch_event_processor_main) \
  ../QUICStream.cc \
  ../QUICFrameDispatcher.cc \
  ../QUICStreamManager.cc \
  ../QUICApplicationMap.cc \
  ../QUICCongestionController.cc \
  ../QUICIncomingFrameBuffer.cc \
  test_QUICIncomingFrameBuffer.cc

test_QUICIncomingFrameBuffer_LDADD = \
  $(test_LDADD)

#
# test_QUICHandshake
#
test_QUICHandshake_CPPFLAGS = \
  $(AM_CPPFLAGS)

test_QUICHandshake_LDFLAGS = \
  @AM_LDFLAGS@

test_QUICHandshake_SOURCES = \
  $(catch_event_processor_main) \
  test_QUICHandshake.cc \
  ../QUICHandshake.cc

test_QUICHandshake_LDADD = \
  $(test_LDADD)

include $(top_srcdir)/build/tidy.mk

tidy-local: $(DIST_SOURCES)
	$(CXX_Clang_Tidy)
