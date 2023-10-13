# Makefile.am for Hdrs
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
	@SWOC_INCLUDES@ \
	$(TS_INCLUDES)

noinst_LIBRARIES = libhdrs.a
EXTRA_PROGRAMS = load_http_hdr

# Http library source files.
libhdrs_a_SOURCES = \
	HTTP.cc \
	HTTP.h \
	HdrHeap.cc \
	HdrHeap.h \
	HdrTSOnly.cc \
	HdrToken.cc \
	HdrToken.h \
	HdrUtils.cc \
	HdrUtils.h \
	HttpCompat.cc \
	HttpCompat.h \
	MIME.cc \
	MIME.h \
	URL.cc \
	URL.h \
	VersionConverter.cc \
	VersionConverter.h \
	HuffmanCodec.cc \
	HuffmanCodec.h \
	XPACK.cc \
	XPACK.h \
	HeaderValidator.cc \
	HeaderValidator.h

load_http_hdr_SOURCES = \
	HTTP.h \
	HdrHeap.h \
	MIME.h \
	load_http_hdr.cc

load_http_hdr_LDADD =
	$(top_builddir)/proxy/hdrs/libhdrs.a \
	$(top_builddir)/src/tscore/libtscore.a \
	$(top_builddir)/src/tscpp/util/libtscpputil.la \
	@SWOC_LIBS@

check_PROGRAMS = \
	test_proxy_hdrs \
	test_hdr_heap \
	test_XPACK

TESTS = $(check_PROGRAMS)

test_proxy_hdrs_CPPFLAGS = $(AM_CPPFLAGS) \
	-I$(abs_top_srcdir)/lib/catch2

test_proxy_hdrs_SOURCES = \
	unit_tests/unit_test_main.cc \
	unit_tests/test_Hdrs.cc \
	unit_tests/test_HdrUtils.cc \
	unit_tests/test_URL.cc \
	unit_tests/test_mime.cc \
	unit_tests/test_Huffmancode.cc \
	unit_tests/test_HeaderValidator.cc

test_proxy_hdrs_LDFLAGS = @AM_LDFLAGS@ @SWOC_LDFLAGS@ @YAMLCPP_LDFLAGS@ @OPENSSL_LDFLAGS@
test_proxy_hdrs_LDADD = \
	$(top_builddir)/proxy/hdrs/libhdrs.a \
	$(top_builddir)/iocore/eventsystem/libinkevent.a \
	$(top_builddir)/src/records/librecords_p.a \
	$(top_builddir)/src/tscore/libtscore.a \
	$(top_builddir)/src/api/libtsapicore.a \
	$(top_builddir)/src/tscpp/util/libtscpputil.la \
	$(top_builddir)/src/api/libtsapicore.a \
	@SWOC_LIBS@ @YAMLCPP_LIBS@ @HWLOC_LIBS@ @LIBPCRE@ @OPENSSL_LIBS@ @LIBCAP@

test_hdr_heap_CPPFLAGS = $(AM_CPPFLAGS) \
	-I$(abs_top_srcdir)/lib/catch2

test_hdr_heap_SOURCES = \
	unit_tests/unit_test_main.cc \
	unit_tests/test_HdrHeap.cc

test_hdr_heap_LDADD = \
	$(top_builddir)/proxy/hdrs/libhdrs.a \
	$(top_builddir)/iocore/eventsystem/libinkevent.a \
	$(top_builddir)/src/records/librecords_p.a \
	$(top_builddir)/src/tscore/libtscore.a \
	$(top_builddir)/src/api/libtsapicore.a \
	$(top_builddir)/src/tscpp/util/libtscpputil.la \
	$(top_builddir)/src/api/libtsapicore.a \
	@SWOC_LIBS@ @HWLOC_LIBS@ \
	@LIBPCRE@ @OPENSSL_LIBS@ @LIBCAP@

test_XPACK_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(abs_top_srcdir)/lib/catch2

test_XPACK_LDADD = \
	$(top_builddir)/src/tscore/libtscore.a \
	$(top_builddir)/src/api/libtsapicore.a \
	$(top_builddir)/src/tscpp/util/libtscpputil.la \
	@SWOC_LIBS@ @LIBPCRE@ @LIBCAP@

test_XPACK_SOURCES = \
	unit_tests/test_XPACK.cc \
	HuffmanCodec.cc \
	XPACK.cc

#test_UNUSED_SOURCES = \
#  test_urlhash.cc

clang-tidy-local: $(libhdrs_a_SOURCES)
	$(CXX_Clang_Tidy)
