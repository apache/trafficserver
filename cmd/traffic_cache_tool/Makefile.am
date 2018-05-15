#
# Cache Tool Makefile
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

AM_CPPFLAGS = -I $(top_srcdir)/lib -D__STDC_FORMAT_MACROS

noinst_PROGRAMS = traffic_cache_tool

traffic_cache_tool_SOURCES = CacheDefs.h CacheDefs.cc CacheTool.cc File.h File.cc Command.h Command.cc CacheScan.h CacheScan.cc
traffic_cache_tool_LDADD = \
	$(top_builddir)/lib/ts/.libs/ink_assert.o \
	$(top_builddir)/lib/ts/.libs/ink_code.o \
	$(top_builddir)/lib/ts/.libs/ink_error.o \
	$(top_builddir)/lib/ts/.libs/ink_file.o \
	$(top_builddir)/lib/ts/.libs/ink_memory.o \
	$(top_builddir)/lib/ts/.libs/ink_string.o \
	$(top_builddir)/lib/ts/.libs/BufferWriterFormat.o \
	$(top_builddir)/lib/ts/.libs/TextView.o \
	$(top_builddir)/lib/tsconfig/.libs/Errata.o \
	$(top_builddir)/lib/ts/.libs/Regex.o \
	$(top_builddir)/lib/ts/.libs/CryptoHash.o \
	$(top_builddir)/lib/ts/.libs/MMH.o \
	@OPENSSL_LIBS@ @LIBPCRE@ @LIBTCL@
	

all-am: Makefile $(PROGRAMS)

clang-tidy-local: $(DIST_SOURCES)
	$(CXX_Clang_Tidy)
