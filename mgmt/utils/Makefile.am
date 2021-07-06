#
# Makefile.am for the Enterprise Management module.
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
	-I$(abs_top_srcdir)/iocore/utils \
	-I$(abs_top_srcdir)/mgmt \
	-I$(abs_top_srcdir)/mgmt/api \
	-I$(abs_top_srcdir)/mgmt/api/include \
	-I$(abs_top_srcdir)/proxy \
	-I$(abs_top_srcdir)/include \
	-I$(abs_top_srcdir)/lib \
	$(TS_INCLUDES) \
	@OPENSSL_INCLUDES@

# header files used by other libraries
EXTRA_DIST = MgmtSocket.h

noinst_LTLIBRARIES = libutils_lm.la libutils_p.la
check_PROGRAMS = test_mgmt_utils

TESTS = $(check_PROGRAMS)

libutils_COMMON = \
	MgmtMarshall.cc \
	MgmtMarshall.h \
	MgmtSocket.cc \
	MgmtSocket.h \
	MgmtUtils.cc \
	MgmtUtils.h

libutils_lm_la_SOURCES = \
	$(libutils_COMMON) \
	ExpandingArray.cc \
	ExpandingArray.h \
	MgmtLocalCleanup.cc

libutils_p_la_SOURCES = \
	$(libutils_COMMON) \
	MgmtProcessCleanup.cc

test_mgmt_utils_CPPFLAGS = $(AM_CPPFLAGS) -I$(abs_top_srcdir)/tests/include

test_mgmt_utils_LDFLAGS = \
	@AM_LDFLAGS@ \
	@OPENSSL_LDFLAGS@

test_mgmt_utils_SOURCES = \
	unit_tests/unit_test_main.cc \
	unit_tests/test_marshall.cc

test_mgmt_utils_LDADD = \
	libutils_p.la \
	$(top_builddir)/src/tscore/libtscore.la

include $(top_srcdir)/build/tidy.mk

clang-tidy-local: $(DIST_SOURCES)
	$(CXX_Clang_Tidy)
