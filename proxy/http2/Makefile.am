# Makefile.am for Http
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

include $(top_srcdir)/build/tidy.mk

AM_CPPFLAGS += \
	$(iocore_include_dirs) \
	-I$(abs_top_srcdir)/include \
	-I$(abs_top_srcdir)/lib \
	-I$(abs_top_srcdir)/mgmt \
	-I$(abs_top_srcdir)/mgmt/utils \
	-I$(abs_top_srcdir)/proxy \
	-I$(abs_top_srcdir)/proxy/http \
	-I$(abs_top_srcdir)/proxy/hdrs \
	-I$(abs_top_srcdir)/proxy/shared \
	-I$(abs_top_srcdir)/proxy/http/remap \
	$(TS_INCLUDES)

noinst_LIBRARIES = libhttp2.a

libhttp2_a_SOURCES = \
	HPACK.cc \
	HPACK.h \
	HTTP2.cc \
	HTTP2.h \
	Http2ClientSession.cc \
	Http2ClientSession.h \
	Http2ConnectionState.cc \
	Http2ConnectionState.h \
	Http2DebugNames.cc \
	Http2DebugNames.h \
	Http2DependencyTree.h \
	Http2Stream.cc \
	Http2Stream.h \
	Http2SessionAccept.cc \
	Http2SessionAccept.h \
	HuffmanCodec.cc \
	HuffmanCodec.h

if BUILD_TESTS
libhttp2_a_SOURCES += \
	RegressionHPACK.cc
endif

check_PROGRAMS = \
	test_Huffmancode \
	test_Http2DependencyTree \
	test_HPACK

TESTS = \
	test_Huffmancode \
	test_Http2DependencyTree \
	test_HPACK

test_Huffmancode_LDADD = \
	$(top_builddir)/src/tscore/libtscore.la \
	$(top_builddir)/src/tscpp/util/libtscpputil.la

test_Huffmancode_SOURCES = \
	test_Huffmancode.cc \
	HuffmanCodec.cc \
	HuffmanCodec.h

test_Http2DependencyTree_LDADD = \
	$(top_builddir)/src/tscore/libtscore.la \
	$(top_builddir)/src/tscpp/util/libtscpputil.la

test_Http2DependencyTree_CPPFLAGS = $(AM_CPPFLAGS)\
	-I$(abs_top_srcdir)/tests/include

test_Http2DependencyTree_SOURCES = \
	unit_tests/test_Http2DependencyTree.cc \
	Http2DependencyTree.h

test_HPACK_LDADD = \
	$(top_builddir)/proxy/hdrs/libhdrs.a \
	$(top_builddir)/src/tscore/libtscore.la \
	$(top_builddir)/src/tscpp/util/libtscpputil.la \
	$(top_builddir)/iocore/eventsystem/libinkevent.a \
	$(top_builddir)/lib/records/librecords_p.a \
	$(top_builddir)/mgmt/libmgmt_p.la \
	$(top_builddir)/proxy/shared/libUglyLogStubs.a \
	@LIBTCL@ \
	@HWLOC_LIBS@

test_HPACK_SOURCES = \
	test_HPACK.cc \
	HuffmanCodec.cc \
	HuffmanCodec.h \
	HPACK.cc \
	HPACK.h

clang-tidy-local: $(libhttp2_a_SOURCES) $(test_Huffmancode_SOURCES) \
		$(test_Http2DependencyTree_SOURCES) $(test_HPACK_SOURCES)
	$(CXX_Clang_Tidy)
