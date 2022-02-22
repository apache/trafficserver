#
#  Makefile.am for HTTP/3
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
  -I$(abs_top_srcdir)/include \
  -I$(abs_top_srcdir)/proxy/api/ts \
  -I$(abs_top_srcdir)/lib \
  -I$(abs_top_srcdir)/lib/records \
  -I$(abs_top_srcdir)/mgmt \
  -I$(abs_top_srcdir)/mgmt/utils \
  -I$(abs_top_srcdir)/proxy \
  -I$(abs_top_srcdir)/proxy/http \
  -I$(abs_top_srcdir)/proxy/http2 \
  -I$(abs_top_srcdir)/proxy/hdrs \
  -I$(abs_top_srcdir)/proxy/shared \
  -I$(abs_top_srcdir)/proxy/http/remap \
  $(TS_INCLUDES) \
	@YAMLCPP_INCLUDES@

noinst_LIBRARIES = libhttp3.a

libhttp3_a_SOURCES = \
  Http09App.cc \
  Http3.cc \
  Http3Config.cc \
  Http3App.cc \
  Http3Types.cc \
  Http3SessionAccept.cc \
  Http3Session.cc \
  Http3Transaction.cc \
  Http3DebugNames.cc \
  Http3Frame.cc \
  Http3FrameCollector.cc \
  Http3FrameDispatcher.cc \
  Http3HeaderFramer.cc \
  Http3DataFramer.cc \
  Http3HeaderVIOAdaptor.cc \
  Http3StreamDataVIOAdaptor.cc \
  QPACK.cc

#
# Check Programs
#
check_PROGRAMS = \
  test_libhttp3 \
  test_qpack

TESTS = $(check_PROGRAMS)

test_CPPFLAGS = \
  $(AM_CPPFLAGS) \
  -I$(abs_top_srcdir)/proxy/logging \
  -I$(abs_top_srcdir)/tests/include

test_LDADD = \
  $(top_builddir)/iocore/net/quic/libquic.a \
  $(top_builddir)/iocore/net/TLSKeyLogger.o \
  $(top_builddir)/iocore/eventsystem/libinkevent.a \
  $(top_builddir)/lib/records/librecords_p.a \
  $(top_builddir)/mgmt/libmgmt_p.la \
  $(top_builddir)/src/tscore/libtscore.la \
  $(top_builddir)/src/tscpp/util/libtscpputil.la \
  $(top_builddir)/proxy/http2/libhttp2.a \
  $(top_builddir)/proxy/hdrs/libhdrs.a \
  $(top_builddir)/proxy/shared/libUglyLogStubs.a \
  @LIBPCRE@ \
  @OPENSSL_LIBS@ \
  @HWLOC_LIBS@

test_libhttp3_CPPFLAGS = $(test_CPPFLAGS)
test_libhttp3_LDFLAGS = @AM_LDFLAGS@
test_libhttp3_LDADD = $(test_LDADD)
test_libhttp3_SOURCES = \
  ./test/main.cc \
  ./test/test_Http3Frame.cc \
  ./Http3Config.cc \
  ./Http3Frame.cc

test_qpack_CPPFLAGS = $(test_CPPFLAGS)
test_qpack_LDFLAGS = @AM_LDFLAGS@
test_qpack_LDADD = $(test_LDADD)
test_qpack_SOURCES = \
  ./test/main_qpack.cc \
  ./test/test_QPACK.cc \
  ./QPACK.cc


#
# clang-tidy
#
include $(top_srcdir)/build/tidy.mk

clang-tidy-local: $(libhttp3_a_SOURCES) \
	$(CXX_Clang_Tidy)
