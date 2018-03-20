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
  test_HQFrame \
  test_HQFrameDispatcher


AM_CPPFLAGS += \
  $(iocore_include_dirs) \
  -I$(abs_top_srcdir)/lib \
  -I$(abs_top_srcdir)/lib/records \
  -I$(abs_top_srcdir)/mgmt \
  -I$(abs_top_srcdir)/mgmt/utils \
  -I$(abs_top_srcdir)/proxy \
  -I$(abs_top_srcdir)/proxy/hdrs \
  -I$(abs_top_srcdir)/proxy/http \
  -I$(abs_top_srcdir)/proxy/hq \
  -I$(abs_top_srcdir)/proxy/logging \
  -I$(abs_top_srcdir)/proxy/shared \
  -I$(abs_top_srcdir)/tests/include \
  @OPENSSL_INCLUDES@

#
# test_HQFrame
#
test_HQFrame_CPPFLAGS = \
  $(AM_CPPFLAGS)

test_HQFrame_LDFLAGS = \
  @AM_LDFLAGS@

test_HQFrame_SOURCES = \
  main.cc \
  test_HQFrame.cc \
  ../HQFrame.cc

test_HQFrame_LDADD = \
  $(top_builddir)/iocore/net/quic/libquic.a \
  $(top_builddir)/lib/ts/libtsutil.la

#
# test_HQFrameDispatcher
#
test_HQFrameDispatcher_CPPFLAGS = \
  $(AM_CPPFLAGS)

test_HQFrameDispatcher_LDFLAGS = \
  @AM_LDFLAGS@

test_HQFrameDispatcher_SOURCES = \
  main.cc \
  test_HQFrameDispatcher.cc \
  ../HQFrameDispatcher.cc \
  ../HQFrame.cc

test_HQFrameDispatcher_LDADD = \
  $(top_builddir)/iocore/net/quic/libquic.a \
  $(top_builddir)/lib/ts/libtsutil.la


include $(top_srcdir)/build/tidy.mk

tidy-local: $(DIST_SOURCES)
	$(CXX_Clang_Tidy)
