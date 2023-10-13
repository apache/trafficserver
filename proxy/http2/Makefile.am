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

include $(top_srcdir)/mk/tidy.mk

AM_CPPFLAGS += \
	$(iocore_include_dirs) \
	-I$(abs_top_srcdir)/include \
	-I$(abs_top_srcdir)/lib \
	-I$(abs_top_srcdir)/proxy \
	-I$(abs_top_srcdir)/proxy/http \
	-I$(abs_top_srcdir)/proxy/hdrs \
	-I$(abs_top_srcdir)/proxy/shared \
	-I$(abs_top_srcdir)/proxy/http/remap \
	@SWOC_INCLUDES@ \
	$(TS_INCLUDES)

noinst_LIBRARIES = libhttp2.a

libhttp2_a_SOURCES = \
	HPACK.cc \
	HPACK.h \
	HTTP2.cc \
	HTTP2.h \
	Http2Frame.cc \
	Http2Frame.h \
	Http2ClientSession.cc \
	Http2ClientSession.h \
	Http2CommonSession.cc \
	Http2CommonSession.h \
	Http2ServerSession.cc \
	Http2ServerSession.h \
	Http2ConnectionState.cc \
	Http2ConnectionState.h \
	Http2DebugNames.cc \
	Http2DebugNames.h \
	Http2DependencyTree.h \
	Http2FrequencyCounter.h \
	Http2FrequencyCounter.cc \
	Http2Stream.cc \
	Http2Stream.h \
	Http2SessionAccept.cc \
	Http2SessionAccept.h

check_PROGRAMS = \
	test_libhttp2 \
	test_Http2DependencyTree \
	test_Http2FrequencyCounter \
	test_HPACK

TESTS = $(check_PROGRAMS)

# The order of libinkevent.a and libhdrs.a is sensitive for LLD on debug build.
# Be careful if you change the order. Details in GitHub #6666
test_libhttp2_LDADD = \
	HTTP2.o \
	Http2Frame.o \
	HPACK.o \
	$(top_builddir)/iocore/eventsystem/libinkevent.a \
	$(top_builddir)/src/records/librecords_p.a \
	$(top_builddir)/src/api/libtsapicore.a \
	$(top_builddir)/proxy/hdrs/libhdrs.a \
	$(top_builddir)/src/tscore/libtscore.a \
	$(top_builddir)/src/tscpp/util/libtscpputil.la \
	@SWOC_LIBS@ @HWLOC_LIBS@ @LIBPCRE@ @OPENSSL_LIBS@ @LIBCAP@

if OS_LINUX
test_libhttp2_LDFLAGS = $(AM_LDFLAGS)\
	-Wl,--unresolved-symbols=ignore-all
else
test_libhttp2_LDFLAGS = $(AM_LDFLAGS)\
    -Wl,-undefined -Wl,suppress -Wl,-flat_namespace -Wl,-dead_strip
endif
test_libhttp2_CPPFLAGS = $(AM_CPPFLAGS)\
	-I$(abs_top_srcdir)/lib/catch2

test_libhttp2_SOURCES = \
	unit_tests/test_HTTP2.cc \
	unit_tests/test_Http2Frame.cc \
	unit_tests/test_HpackIndexingTable.cc \
	unit_tests/main.cc

test_Http2DependencyTree_LDADD = \
	$(top_builddir)/src/tscore/libtscore.a \
	$(top_builddir)/src/api/libtsapicore.a \
	$(top_builddir)/src/tscpp/util/libtscpputil.la \
	@SWOC_LIBS@

test_Http2DependencyTree_CPPFLAGS = $(AM_CPPFLAGS)\
	-I$(abs_top_srcdir)/lib/catch2

test_Http2DependencyTree_SOURCES = \
	unit_tests/test_Http2DependencyTree.cc \
	Http2DependencyTree.h

test_Http2FrequencyCounter_LDADD = \
	$(top_builddir)/iocore/eventsystem/libinkevent.a \
	$(top_builddir)/src/tscore/libtscore.a \
	$(top_builddir)/src/api/libtsapicore.a \
	$(top_builddir)/src/tscpp/util/libtscpputil.la \
	@SWOC_LIBS@ \
	@LIBPCRE@ \
	@LIBCAP@

test_Http2FrequencyCounter_CPPFLAGS = $(AM_CPPFLAGS)\
	-I$(abs_top_srcdir)/lib/catch2

test_Http2FrequencyCounter_SOURCES = \
	unit_tests/test_Http2FrequencyCounter.cc \
	Http2FrequencyCounter.cc \
	Http2FequencyCounter.h

test_HPACK_LDADD = \
	$(top_builddir)/proxy/hdrs/libhdrs.a \
	$(top_builddir)/src/tscpp/util/libtscpputil.la \
	$(top_builddir)/iocore/eventsystem/libinkevent.a \
	$(top_builddir)/src/records/librecords_p.a \
	$(top_builddir)/src/tscore/libtscore.a \
	$(top_builddir)/src/api/libtsapicore.a \
	@SWOC_LIBS@ @HWLOC_LIBS@ @LIBPCRE@ @OPENSSL_LIBS@ @YAMLCPP_LIBS@ @LIBCAP@

test_HPACK_SOURCES = \
	test_HPACK.cc \
	HPACK.cc \
	HPACK.h

clang-tidy-local: $(libhttp2_a_SOURCES) $(test_Huffmancode_SOURCES) \
		$(test_Http2DependencyTree_SOURCES) $(test_HPACK_SOURCES)
	$(CXX_Clang_Tidy)
