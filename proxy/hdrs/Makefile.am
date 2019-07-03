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

include $(top_srcdir)/build/tidy.mk

AM_CPPFLAGS += \
	$(iocore_include_dirs) \
	-I$(abs_top_srcdir)/include \
	-I$(abs_top_srcdir)/lib \
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
	HuffmanCodec.cc \
	HuffmanCodec.h \
	XPACK.cc \
	XPACK.h

if BUILD_TESTS
libhdrs_a_SOURCES += \
	HdrTest.cc \
	HdrTest.h
endif

load_http_hdr_SOURCES = \
	HTTP.h \
	HdrHeap.h \
	MIME.h \
	load_http_hdr.cc

load_http_hdr_LDADD = -L. -lhdrs \
	$(top_builddir)/src/tscore/libtscore.la \
	$(top_builddir)/src/tscpp/util/libtscpputil.la

check_PROGRAMS = \
	test_mime \
	test_proxy_hdrs \
	test_hdr_heap \
	test_Huffmancode

TESTS = $(check_PROGRAMS)

test_mime_LDADD = -L. -lhdrs \
	$(top_builddir)/src/tscore/libtscore.la \
	$(top_builddir)/src/tscpp/util/libtscpputil.la \
	$(top_builddir)/iocore/eventsystem/libinkevent.a \
	$(top_builddir)/lib/records/librecords_p.a \
	$(top_builddir)/mgmt/libmgmt_p.la \
	$(top_builddir)/proxy/shared/libUglyLogStubs.a \
	@HWLOC_LIBS@ \
	@LIBCAP@

test_mime_SOURCES = test_mime.cc

test_proxy_hdrs_CPPFLAGS = $(AM_CPPFLAGS) \
	-I$(abs_top_srcdir)/tests/include

test_proxy_hdrs_SOURCES = \
	unit_tests/unit_test_main.cc \
	unit_tests/test_Hdrs.cc \
	unit_tests/test_HdrUtils.cc

test_proxy_hdrs_LDADD = \
	$(top_builddir)/src/tscore/libtscore.la \
	-L. -lhdrs \
	$(top_builddir)/src/tscore/libtscore.la \
	$(top_builddir)/src/tscpp/util/libtscpputil.la \
	$(top_builddir)/iocore/eventsystem/libinkevent.a \
	$(top_builddir)/lib/records/librecords_p.a \
	$(top_builddir)/mgmt/libmgmt_p.la \
	$(top_builddir)/proxy/shared/libUglyLogStubs.a \
	@HWLOC_LIBS@ \
	@LIBCAP@

test_hdr_heap_CPPFLAGS = $(AM_CPPFLAGS) \
	-I$(abs_top_srcdir)/tests/include

test_hdr_heap_SOURCES = \
	unit_tests/unit_test_main.cc \
	unit_tests/test_HdrHeap.cc

test_hdr_heap_LDADD = -L. -lhdrs \
	$(top_builddir)/src/tscore/libtscore.la \
	-L. -lhdrs \
	$(top_builddir)/src/tscore/libtscore.la \
	$(top_builddir)/src/tscpp/util/libtscpputil.la \
	$(top_builddir)/iocore/eventsystem/libinkevent.a \
	$(top_builddir)/lib/records/librecords_p.a \
	$(top_builddir)/mgmt/libmgmt_p.la \
	$(top_builddir)/proxy/shared/libUglyLogStubs.a \
	@HWLOC_LIBS@ \
	@LIBCAP@

test_Huffmancode_LDADD = \
	$(top_builddir)/src/tscore/libtscore.la \
	$(top_builddir)/src/tscpp/util/libtscpputil.la

test_Huffmancode_SOURCES = \
	test_Huffmancode.cc \
	HuffmanCodec.cc \
	HuffmanCodec.h


#test_UNUSED_SOURCES = \
#  test_urlhash.cc

clang-tidy-local: $(libhdrs_a_SOURCES)
	$(CXX_Clang_Tidy)
