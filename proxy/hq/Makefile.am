#
#  Makefile.am for HTTP over QUIC
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
  -I$(abs_top_srcdir)/proxy/api/ts \
  -I$(abs_top_srcdir)/lib \
  -I$(abs_top_srcdir)/lib/records \
  -I$(abs_top_srcdir)/mgmt \
  -I$(abs_top_srcdir)/mgmt/utils \
  -I$(abs_top_srcdir)/proxy \
  -I$(abs_top_srcdir)/proxy/http \
  -I$(abs_top_srcdir)/proxy/hdrs \
  -I$(abs_top_srcdir)/proxy/shared \
  -I$(abs_top_srcdir)/proxy/http/remap \
  $(TS_INCLUDES)

noinst_LIBRARIES = libhq.a

libhq_a_SOURCES = \
  HQ.cc \
  HQSessionAccept.cc \
  HQClientSession.cc \
  HQClientTransaction.cc \
  HQFrame.cc \
  HQFrameCollector.cc \
  HQFrameDispatcher.cc \
  HQHeaderFramer.cc \
  HQDataFramer.cc \
  HQHeaderVIOAdaptor.cc \
  HQStreamDataVIOAdaptor.cc \
  QUICSimpleApp.cc

#
# Check Programs
#
check_PROGRAMS = \
  test_libhq

TESTS = $(check_PROGRAMS)

test_libhq_CPPFLAGS = \
  $(AM_CPPFLAGS) \
  -I$(abs_top_srcdir)/tests/include

test_libhq_LDFLAGS = \
  @AM_LDFLAGS@

test_libhq_SOURCES = \
  ./test/main.cc \
  ./test/test_HQFrame.cc \
  ./test/test_HQFrameDispatcher.cc

test_libhq_LDADD = \
  libhq.a \
  $(top_builddir)/iocore/net/quic/libquic.a \
  $(top_builddir)/lib/ts/libtsutil.la

#
# clang-tidy
#
include $(top_srcdir)/build/tidy.mk

tidy-local: $(libhq_a_SOURCES) \
	$(CXX_Clang_Tidy)
