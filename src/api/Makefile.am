# libts Makefile.am
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

check_PROGRAMS = test_Metrics

TESTS = $(check_PROGRAMS)

noinst_LIBRARIES = libtsapicore.a

lib_LTLIBRARIES = libtsapi.la

AM_CPPFLAGS += \
	$(iocore_include_dirs) \
	-I$(abs_top_srcdir)/include \
	-I$(abs_top_srcdir)/proxy \
	-I$(abs_top_srcdir)/proxy/hdrs \
	-I$(abs_top_srcdir)/proxy/http \
	-I$(abs_top_srcdir)/proxy/http2 \
	-I$(abs_top_srcdir)/proxy/http/remap \
	-I$(abs_top_srcdir)/proxy/logging \
	-I$(abs_top_srcdir)/include/mgmt/rpc \
	-I$(abs_top_srcdir)/include/mgmt/ \
	-I$(abs_top_srcdir)/include/iocore/eventsystem \
	-I$(abs_top_srcdir)/include/iocore/cache \
	-I$(abs_top_srcdir)/include/iocore/aio \
	-I$(abs_top_srcdir)/include/iocore/net \
	-I$(abs_top_srcdir)/include/iocore/dns \
	@SWOC_INCLUDES@ \
	@YAMLCPP_INCLUDES@ \
	$(TS_INCLUDES)

# libtsapi.la
libtsapi_la_LDFLAGS = @AM_LDFLAGS@ -version-info @TS_LIBTOOL_VERSION@ @SWOC_LDFLAGS@

libtsapi_la_LIBADD = \
	@SWOC_LIBS@

libtsapi_la_SOURCES = \
	InkAPI.cc \
	InkIOCoreAPI.cc

if BUILD_TESTS
libtsapi_la_SOURCES += \
	InkAPITest.cc
endif

# libtsapicore.a

libtsapicore_a_SOURCES = \
	APIHook.cc \
	APIHooks.cc \
	Metrics.cc \
	ConfigUpdateCbTable.cc \
	InkContInternal.cc \
	InkVConnInternal.cc \
	DbgCtl.cc \
	FetchSM.cc \
	LifecycleAPIHooks.cc

test_Metrics_SOURCES = test_Metrics.cc

test_Metrics_CPPFLAGS = $(AM_CPPFLAGS)\
	-I$(abs_top_srcdir)/lib/catch2

test_Metrics_LDADD = \
	$(top_builddir)/src/tscore/libtscore.a \
	libtsapicore.a


clang-tidy-local: $(DIST_SOURCES)
	$(CXX_Clang_Tidy)
