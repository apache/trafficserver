# Makefile.am for the src/records
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

check_PROGRAMS = test_librecords test_librecords_on_eventsystem

AM_CPPFLAGS += \
	-I$(abs_top_srcdir)/iocore/eventsystem \
	-I$(abs_top_srcdir)/iocore/utils \
	-I$(abs_top_srcdir)/include \
	-I$(abs_top_srcdir)/lib \
	-I$(abs_top_srcdir)/ \
	@YAMLCPP_INCLUDES@\
	@SWOC_INCLUDES@\
	$(TS_INCLUDES)

noinst_LIBRARIES = librecords_p.a

librecords_COMMON = \
	I_RecCore.h \
	I_RecDefs.h \
	I_RecHttp.h \
	I_RecMutex.h \
	P_RecCore.cc \
	P_RecCore.h \
	P_RecDefs.h \
	P_RecFile.h \
	P_RecMessage.h \
	P_RecUtils.h \
	RecConfigParse.cc \
	RecYAMLDecoder.cc \
	RecCore.cc \
	RecDebug.cc \
	RecFile.cc \
	RecHttp.cc \
	RecMessage.cc \
	RecMutex.cc \
	RecRawStats.cc \
	RecUtils.cc \
	RecordsConfig.cc \
	RecordsConfigUtils.cc

librecords_p_a_SOURCES = $(librecords_COMMON)

TESTS = $(check_PROGRAMS)

test_librecords_CPPFLAGS = $(AM_CPPFLAGS)\
	-I$(abs_top_srcdir)/lib/catch2

test_librecords_SOURCES = \
    unit_tests/unit_test_main.cc \
    unit_tests/test_RecHttp.cc

test_librecords_LDADD = \
	$(top_builddir)/src/records/librecords_p.a \
	$(top_builddir)/iocore/eventsystem/libinkevent.a \
	$(top_builddir)/src/tscpp/util/libtscpputil.la \
	$(top_builddir)/src/tscore/libtscore.a \
	$(top_builddir)/src/api/libtsapicore.a \
	@SWOC_LIBS@ @HWLOC_LIBS@ @LIBCAP@ @YAMLCPP_LIBS@ @LIBPCRE@ @OPENSSL_LIBS@

test_librecords_on_eventsystem_CPPFLAGS = $(AM_CPPFLAGS)\
	-I$(abs_top_srcdir)/lib/catch2

test_librecords_on_eventsystem_SOURCES = \
    unit_tests/unit_test_main_on_eventsystem.cc
    
test_librecords_on_eventsystem_LDADD = \
	$(top_builddir)/src/records/librecords_p.a \
	$(top_builddir)/iocore/eventsystem/libinkevent.a \
	$(top_builddir)/src/tscpp/util/libtscpputil.la \
	$(top_builddir)/src/tscore/libtscore.a \
	$(top_builddir)/src/api/libtsapicore.a \
	@HWLOC_LIBS@ @LIBCAP@ @YAMLCPP_LIBS@ @SWOC_LIBS@ @LIBPCRE@ @OPENSSL_LIBS@

clang-tidy-local: $(sort $(DIST_SOURCES))
	$(CXX_Clang_Tidy)
