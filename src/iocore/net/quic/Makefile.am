# Makefile.am for the traffic/iocore/net hierarchy
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
  -I$(abs_top_srcdir)/include \
  -I$(abs_top_srcdir)/lib \
  -I$(abs_top_srcdir)/src/records \
  -I$(abs_top_srcdir)/proxy \
  -I$(abs_top_srcdir)/proxy/hdrs \
  -I$(abs_top_srcdir)/proxy/shared \
  -I$(abs_top_srcdir)/proxy/logging \
  -I$(abs_top_srcdir)/proxy/http \
  -I$(abs_top_srcdir)/proxy/http3 \
  $(TS_INCLUDES) @SWOC_INCLUDES@ \
  @OPENSSL_INCLUDES@ @YAMLCPP_INCLUDES@

noinst_LIBRARIES = libquic.a

libquic_a_SOURCES = \
  QUICApplication.cc \
  QUICApplicationMap.cc \
  QUICConfig.cc \
  QUICContext.cc \
  QUICConnectionTable.cc \
  QUICGlobals.cc \
  QUICTypes.cc \
  QUICIntUtil.cc \
  QUICStream.cc \
  QUICStream_quiche.cc \
  QUICStreamManager.cc \
  QUICStreamManager_quiche.cc \
  QUICStreamAdapter.cc \
  QUICStreamVCAdapter.cc

#
# Check Programs
#

check_PROGRAMS := test_MTHashTable

test_MTHashTable_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(abs_top_srcdir)/lib/catch2

test_MTHashTable_LDADD = \
	$(top_builddir)/src/iocore/eventsystem/libinkevent.a \
	$(top_builddir)/src/tscore/libtscore.a \
	$(top_builddir)/src/tscpp/util/libtscpputil.la \
	$(LIBPCRE) \
	$(LIBCAP) \
	@SWOC_LIBS@ @OPENSSL_LIBS@

test_MTHashTable_SOURCES = test_MTHashTable.cc

TESTS = $(check_PROGRAMS)

#
# clang-tidy
#
include $(top_srcdir)/mk/tidy.mk

clang-tidy-local: $(DIST_SOURCES)
	$(CXX_Clang_Tidy)
