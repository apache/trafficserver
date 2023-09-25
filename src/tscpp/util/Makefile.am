# tscpp/util Makefile.am
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

check_PROGRAMS = test_tscpputil

TESTS = $(check_PROGRAMS)

lib_LTLIBRARIES = libtscpputil.la

AM_CPPFLAGS += -I$(abs_top_srcdir)/include @SWOC_INCLUDES@ @YAMLCPP_INCLUDES@

libtscpputil_la_LDFLAGS = @AM_LDFLAGS@ -no-undefined -version-info @TS_LIBTOOL_VERSION@ @SWOC_LDFLAGS@ @YAMLCPP_LDFLAGS@
libtscpputil_la_LIBADD = @SWOC_LIBS@ @YAMLCPP_LIBS@

libtscpputil_la_SOURCES = \
	ts_diags.cc ts_bwf.cc ts_errata.cc ts_ip.cc YamlCfg.cc ts_unit_parser.cc

test_tscpputil_CPPFLAGS = $(AM_CPPFLAGS)\
	-I$(abs_top_srcdir)/lib/catch2 @SWOC_INCLUDES@

test_tscpputil_CXXFLAGS = -Wno-array-bounds $(AM_CXXFLAGS)
test_tscpputil_LDADD = \
	libtscpputil.la \
	@SWOC_LIBS@
test_tscpputil_SOURCES = \
	unit_tests/unit_test_main.cc \
	unit_tests/test_LocalBuffer.cc \
	unit_tests/test_PostScript.cc \
	unit_tests/test_Strerror.cc \
	unit_tests/test_ts_meta.cc \
	unit_tests/test_time_parser.cc


clean-local:
	rm -f ParseRulesCType ParseRulesCTypeToLower ParseRulesCTypeToUpper

clang-tidy-local: $(DIST_SOURCES)
	$(CXX_Clang_Tidy)
