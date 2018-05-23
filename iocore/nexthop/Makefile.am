# Makefile.am for the traffic/iocore/nexthop hierarchy
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
  -I$(abs_top_srcdir)/lib \
  -I$(abs_top_srcdir)/lib/records \
  -I$(abs_top_srcdir)/iocore/eventsystem \
  -I$(abs_top_srcdir)/iocore/utils \
  -I$(abs_top_srcdir)/lib/cppapi/include 

noinst_LIBRARIES = libtsnexthop.a 
 
libtsnexthop_a_SOURCES = #adding later

TESTS = $(check_PROGRAMS)
check_PROGRAMS = test_tsnexthop  

test_tsnexthop_SOURCES = \
  unit-tests/unit_test_main.cc \
  unit-tests/test_SharedExtendible.cc 
# add your catch based test file here 

test_tsnexthop_LD_FLAGS = \
  @AM_LDFLAGS@ \
  @OPENSSL_LDFLAGS@

test_tsnexthop_CPPFLAGS = $(AM_CPPFLAGS)\
	-I$(abs_top_srcdir)/tests/include 

test_tsnexthop_CXXFLAGS = -Wno-array-bounds $(AM_CXXFLAGS)

test_tsnexthop_LDADD = \
  libtsnexthop.a \
  $(top_builddir)/lib/records/librecords_p.a \
  $(top_builddir)/mgmt/libmgmt_p.la \
  $(top_builddir)/iocore/utils/libinkutils.a \
  $(top_builddir)/iocore/eventsystem/libinkevent.a \
  $(top_builddir)/lib/ts/libtsutil.la \
  $(top_builddir)/proxy/shared/libUglyLogStubs.a \
  @LIBTCL@ @HWLOC_LIBS@


TESTS_ENVIRONMENT = LSAN_OPTIONS=suppressions=suppression.txt

include $(top_srcdir)/build/tidy.mk

tidy-local: $(DIST_SOURCES)
	$(CXX_Clang_Tidy)
