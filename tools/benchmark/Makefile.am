#
#  Collection of Catch2 based micro benchmark programs
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

noinst_PROGRAMS = \
    benchmark_FreeList \
    benchmark_ProxyAllocator \
    benchmark_SharedMutex

benchmark_LD_FLAGS = \
    @AM_LDFLAGS@ \
    @OPENSSL_LDFLAGS@ \
    @SWOC_LDFLAGS@ \
    @YAMLCPP_LDFLAGS@

benchmark_CPP_FLAGS = \
    -Wno-array-bounds \
    $(AM_CPPFLAGS) \
    $(iocore_include_dirs) \
    -I$(abs_top_srcdir)/include \
    -I$(abs_top_srcdir)/proxy \
    -I$(abs_top_srcdir)/proxy/hdrs \
    -I$(abs_top_srcdir)/proxy/http \
    -I$(abs_top_srcdir)/proxy/logging \
    -I$(abs_top_srcdir)/include/mgmt \
    -I$(abs_top_srcdir)/include/mgmt/utils \
    -I$(abs_top_srcdir)/lib/catch2 \
    @HWLOC_INCLUDES@ \
    @OPENSSL_INCLUDES@ \
    @SWOC_INCLUDES@

benchmark_LD_ADD = \
	$(top_builddir)/src/iocore/eventsystem/libinkevent.a \
	$(top_builddir)/src/records/librecords_p.a \
	$(top_builddir)/src/tscore/libtscore.a \
	$(top_builddir)/src/api/libtsapicore.a \
	$(top_builddir)/src/tscpp/util/libtscpputil.la \
	@HWLOC_LIBS@ \
    @LIBPCRE@ \
    @LIBCAP@ \
    @SWOC_LIBS@ \
    @YAMLCPP_LIBS@

benchmark_FreeList_SOURCES = benchmark_FreeList.cc
benchmark_FreeList_CPPFLAGS = $(benchmark_CPP_FLAGS)
benchmark_FreeList_LDFLAGS = $(benchmark_LD_FLAGS)
benchmark_FreeList_LDADD = $(benchmark_LD_ADD)

benchmark_ProxyAllocator_SOURCES = benchmark_ProxyAllocator.cc
benchmark_ProxyAllocator_CPPFLAGS = $(benchmark_CPP_FLAGS)
benchmark_ProxyAllocator_LDFLAGS = $(benchmark_LD_FLAGS)
benchmark_ProxyAllocator_LDADD = $(benchmark_LD_ADD)

benchmark_SharedMutex_SOURCES = benchmark_SharedMutex.cc
benchmark_SharedMutex_CPPFLAGS = $(benchmark_CPP_FLAGS)
benchmark_SharedMutex_LDFLAGS = $(benchmark_LD_FLAGS)
benchmark_SharedMutex_LDADD = $(benchmark_LD_ADD)

clang-tidy-local: $(DIST_SOURCES)
	$(CXX_Clang_Tidy)
