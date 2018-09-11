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
	-I$(abs_top_srcdir)/include/tsconfig \
	-I$(abs_top_srcdir)/proxy \
	-I$(abs_top_srcdir)/include/records \
	-I$(abs_top_srcdir)/include \
	$(TS_INCLUDES) \
	@OPENSSL_INCLUDES@

# header files used by other libraries
EXTRA_DIST = MgmtHashTable.h MgmtSocket.h

noinst_LTLIBRARIES = libutils_lm.la libutils_p.la
check_PROGRAMS = test_marshall

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

test_marshall_LDFLAGS = \
	@AM_LDFLAGS@ \
	@OPENSSL_LDFLAGS@

test_marshall_SOURCES = test_marshall.cc
test_marshall_LDADD = \
	libutils_p.la \
	$(top_builddir)/src/tscore/libtscore.la \
	$(top_builddir)/src/tscpp/util/libtscpputil.la

include $(top_srcdir)/build/tidy.mk

clang-tidy-local: $(DIST_SOURCES)
	$(CXX_Clang_Tidy)
