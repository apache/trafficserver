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
  test_QUICTransportParameters \
  test_QUICCrypto \
  test_QUICLossDetector \
  test_QUICTypeUtil \
  test_QUICAckFrameCreator \
  test_QUICVersionNegotiator


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
  @OPENSSL_INCLUDES@

if OPENSSL_IS_BORINGSSL
QUICCrypto_impl = ../QUICCrypto_boringssl.cc
else
QUICCrypto_impl = ../QUICCrypto_openssl.cc
endif

#
# test_QUICPacket
#
test_QUICPacket_CPPFLAGS = \
  $(AM_CPPFLAGS)

test_QUICPacket_LDFLAGS = \
  @AM_LDFLAGS@

test_QUICPacket_SOURCES = \
  event_processor_main.cc \
  test_QUICPacket.cc \
  ../QUICPacket.cc \
  ../QUICCrypto.cc \
  $(QUICCrypto_impl) \
  ../QUICTypes.cc

test_QUICPacket_LDADD = \
  $(top_builddir)/lib/ts/libtsutil.la \
  $(top_builddir)/iocore/eventsystem/libinkevent.a \
  $(top_builddir)/iocore/net/quic/libquic.a \
  $(top_builddir)/lib/records/librecords_p.a \
  $(top_builddir)/mgmt/libmgmt_p.la \
  $(top_builddir)/lib/ts/libtsutil.la \
  $(top_builddir)/proxy/shared/libUglyLogStubs.a \
  @LIBTCL@ \
  @HWLOC_LIBS@

#
# test_QUICPacketFactory
#
test_QUICPacketFactory_CPPFLAGS = \
  $(AM_CPPFLAGS)

test_QUICPacketFactory_LDFLAGS = \
  @AM_LDFLAGS@

test_QUICPacketFactory_SOURCES = \
  event_processor_main.cc \
  test_QUICPacketFactory.cc \
  ../QUICPacket.cc \
  ../QUICCrypto.cc \
  $(QUICCrypto_impl) \
  ../QUICTypes.cc

test_QUICPacketFactory_LDADD = \
  $(top_builddir)/lib/ts/libtsutil.la \
  $(top_builddir)/iocore/eventsystem/libinkevent.a \
  $(top_builddir)/iocore/net/quic/libquic.a \
  $(top_builddir)/lib/records/librecords_p.a \
  $(top_builddir)/mgmt/libmgmt_p.la \
  $(top_builddir)/lib/ts/libtsutil.la \
  $(top_builddir)/proxy/shared/libUglyLogStubs.a \
  @LIBTCL@ \
  @HWLOC_LIBS@

#
# test_QUICFrame
#
test_QUICFrame_CPPFLAGS = \
  $(AM_CPPFLAGS)

test_QUICFrame_LDFLAGS = \
  @AM_LDFLAGS@

test_QUICFrame_SOURCES = \
  main.cc \
  test_QUICFrame.cc \
  ../QUICFrame.cc \
  ../QUICPacket.cc \
  ../QUICCrypto.cc \
  $(QUICCrypto_impl) \
  ../QUICTypes.cc

test_QUICFrame_LDADD = \
  $(top_builddir)/lib/ts/libtsutil.la \
  $(top_builddir)/iocore/eventsystem/libinkevent.a

#
# test_QUICFrameDispatcher
#
test_QUICFrameDispatcher_CPPFLAGS = \
  $(AM_CPPFLAGS)

test_QUICFrameDispatcher_LDFLAGS = \
  @AM_LDFLAGS@

test_QUICFrameDispatcher_SOURCES = \
  main.cc \
  test_QUICFrameDispatcher.cc \
  ../QUICFrameDispatcher.cc \
  ../QUICConnectionManager.cc \
  ../QUICStreamManager.cc \
  ../QUICFlowController.cc \
  ../QUICCongestionController.cc \
  ../QUICLossDetector.cc \
  ../QUICFrame.cc \
  ../QUICPacket.cc \
  ../QUICStream.cc \
  ../QUICStreamState.cc \
  ../QUICApplication.cc \
  ../QUICHandshake.cc \
  ../QUICTypes.cc \
  ../QUICEchoApp.cc \
  ../QUICDebugNames.cc \
  ../QUICCrypto.cc \
  $(QUICCrypto_impl)

test_QUICFrameDispatcher_LDADD = \
  $(top_builddir)/lib/ts/libtsutil.la \
  $(top_builddir)/iocore/eventsystem/libinkevent.a \
  $(top_builddir)/lib/records/librecords_p.a \
  $(top_builddir)/mgmt/libmgmt_p.la \
  $(top_builddir)/lib/ts/libtsutil.la \
  $(top_builddir)/proxy/shared/libUglyLogStubs.a \
  @LIBTCL@ \
  @HWLOC_LIBS@


#
# test_QUICStreamState
#
test_QUICStreamState_CPPFLAGS = \
  $(AM_CPPFLAGS)

test_QUICStreamState_LDFLAGS = \
  @AM_LDFLAGS@

test_QUICStreamState_SOURCES = \
  main.cc \
  test_QUICStreamState.cc \
  ../QUICStreamState.cc

test_QUICStreamState_LDADD = \
  $(top_builddir)/lib/ts/libtsutil.la \
  $(top_builddir)/iocore/eventsystem/libinkevent.a \
  $(top_builddir)/iocore/net/quic/libquic.a \
  $(top_builddir)/lib/records/librecords_p.a \
  $(top_builddir)/mgmt/libmgmt_p.la \
  $(top_builddir)/lib/ts/libtsutil.la \
  $(top_builddir)/proxy/shared/libUglyLogStubs.a \
  @LIBTCL@ \
  @HWLOC_LIBS@

#
# test_QUICStream
#
test_QUICStream_CPPFLAGS = \
  $(AM_CPPFLAGS)

test_QUICStream_LDFLAGS = \
  @AM_LDFLAGS@

test_QUICStream_SOURCES = \
  event_processor_main.cc \
  test_QUICStream.cc \
  ../QUICStream.cc \
  ../QUICFrameDispatcher.cc \
  ../QUICConnectionManager.cc \
  ../QUICStreamManager.cc \
  ../QUICFlowController.cc \
  ../QUICCongestionController.cc

test_QUICStream_LDADD = \
  $(top_builddir)/lib/ts/libtsutil.la \
  $(top_builddir)/iocore/eventsystem/libinkevent.a \
  $(top_builddir)/iocore/net/quic/libquic.a \
  $(top_builddir)/lib/records/librecords_p.a \
  $(top_builddir)/mgmt/libmgmt_p.la \
  $(top_builddir)/lib/ts/libtsutil.la \
  $(top_builddir)/proxy/shared/libUglyLogStubs.a \
  @LIBTCL@ \
  @HWLOC_LIBS@

#
# test_QUICTransportParameters
#
test_QUICTransportParameters_CPPFLAGS = \
  $(AM_CPPFLAGS)

test_QUICTransportParameters_LDFLAGS = \
  @AM_LDFLAGS@

test_QUICTransportParameters_SOURCES = \
  main.cc \
  test_QUICTransportParameters.cc \
  ../QUICTransportParameters.cc \
  ../QUICTypes.cc

test_QUICTransportParameters_LDADD = \
  $(top_builddir)/lib/ts/libtsutil.la \
  $(top_builddir)/iocore/eventsystem/libinkevent.a

#
# test_QUICCrypto
#
test_QUICCrypto_CPPFLAGS = \
  $(AM_CPPFLAGS)

test_QUICCrypto_LDFLAGS = \
  @AM_LDFLAGS@ \
  @OPENSSL_LDFLAGS@

test_QUICCrypto_LDADD = \
  @OPENSSL_LIBS@ \
  $(top_builddir)/lib/ts/libtsutil.la \
  $(top_builddir)/iocore/eventsystem/libinkevent.a

test_QUICCrypto_SOURCES = \
  main.cc \
  test_QUICCrypto.cc \
  ../QUICCrypto.cc \
  $(QUICCrypto_impl) \
  ../QUICCrypto.h

#
# test_QUICLossDetector
#
test_QUICLossDetector_CPPFLAGS = \
  $(AM_CPPFLAGS)

test_QUICLossDetector_LDFLAGS = \
  @AM_LDFLAGS@

test_QUICLossDetector_LDADD = \
  $(top_builddir)/lib/ts/libtsutil.la \
  $(top_builddir)/iocore/eventsystem/libinkevent.a \
  $(top_builddir)/iocore/net/quic/libquic.a \
  $(top_builddir)/lib/records/librecords_p.a \
  $(top_builddir)/mgmt/libmgmt_p.la \
  $(top_builddir)/lib/ts/libtsutil.la \
  $(top_builddir)/proxy/shared/libUglyLogStubs.a \
  @LIBTCL@ \
  @HWLOC_LIBS@

test_QUICLossDetector_SOURCES = \
  event_processor_main.cc \
  test_QUICLossDetector.cc \
  ../QUICLossDetector.cc \
  ../QUICTypes.cc \
  ../QUICPacket.cc \
  ../QUICCrypto.cc \
  $(QUICCrypto_impl) \
  ../QUICFrame.cc

#
# test_QUICTypeUtil
#
test_QUICTypeUtil_CPPFLAGS = \
  $(AM_CPPFLAGS)

test_QUICTypeUtil_LDFLAGS = \
  @AM_LDFLAGS@

test_QUICTypeUtil_LDADD = \
  $(top_builddir)/lib/ts/libtsutil.la

test_QUICTypeUtil_SOURCES = \
  main.cc \
  test_QUICTypeUtil.cc \
  ../QUICTypes.cc

#
# test_QUICAckFrameCreator
#
test_QUICAckFrameCreator_CPPFLAGS = \
  $(AM_CPPFLAGS)

test_QUICAckFrameCreator_LDFLAGS = \
  @AM_LDFLAGS@

test_QUICAckFrameCreator_LDADD = \
  $(top_builddir)/iocore/eventsystem/libinkevent.a \
  $(top_builddir)/lib/ts/libtsutil.la

test_QUICAckFrameCreator_SOURCES = \
  main.cc \
  test_QUICAckFrameCreator.cc \
  ../QUICAckFrameCreator.cc \
  ../QUICTypes.cc \
  ../QUICFrame.cc \
  ../QUICPacket.cc \
  ../QUICCrypto.cc \
  $(QUICCrypto_impl)

#
# test_QUICTypeUtil
#
test_QUICVersionNegotiator_CPPFLAGS = \
  $(AM_CPPFLAGS)

test_QUICVersionNegotiator_LDFLAGS = \
  @AM_LDFLAGS@

test_QUICVersionNegotiator_LDADD = \
  $(top_builddir)/lib/ts/libtsutil.la \
  $(top_builddir)/iocore/eventsystem/libinkevent.a \
  $(top_builddir)/proxy/shared/libUglyLogStubs.a \
  @LIBTCL@ \
  @HWLOC_LIBS@

test_QUICVersionNegotiator_SOURCES = \
  main.cc \
  test_QUICVersionNegotiator.cc \
  ../QUICTypes.cc \
  ../QUICPacket.cc \
  ../QUICCrypto.cc \
  $(QUICCrypto_impl) \
  ../QUICVersionNegotiator.cc

include $(top_srcdir)/build/tidy.mk

tidy-local: $(DIST_SOURCES)
	$(CXX_Clang_Tidy)
