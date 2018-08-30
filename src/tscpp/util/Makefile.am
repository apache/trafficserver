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

check_PROGRAMS = test_tscpputil

TESTS = $(check_PROGRAMS)

lib_LTLIBRARIES = libtscpputil.la

AM_CPPFLAGS += -I$(abs_top_srcdir)/include

libtscpputil_la_LDFLAGS = -no-undefined -version-info @TS_LIBTOOL_VERSION@

libtscpputil_la_SOURCES = \
	PostScript.h \
	TextView.h TextView.cc

test_tscpputil_CPPFLAGS = $(AM_CPPFLAGS)\
	-I$(abs_top_srcdir)/tests/include

test_tscpputil_CXXFLAGS = -Wno-array-bounds $(AM_CXXFLAGS)
test_tscpputil_LDADD = libtscpputil.la
test_tscpputil_SOURCES = \
	unit_tests/unit_test_main.cc \
	unit_tests/test_PostScript.cc \
	unit_tests/test_TextView.cc

clean-local:
	rm -f ParseRulesCType ParseRulesCTypeToLower ParseRulesCTypeToUpper

clang-tidy-local: $(DIST_SOURCES)
	$(CXX_Clang_Tidy)
	
