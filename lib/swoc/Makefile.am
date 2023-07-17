# swoc Makefile.am
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

lib_LTLIBRARIES = libtsswoc.la

library_includedir=$(includedir)/swoc

AM_CPPFLAGS += @SWOC_INCLUDES@

libtsswoc_la_LDFLAGS = @AM_LDFLAGS@ -no-undefined -release 1.5.2
libtsswoc_la_SOURCES = \
	src/ArenaWriter.cc  src/bw_format.cc  src/bw_ip_format.cc  src/Errata.cc  src/MemArena.cc  src/RBTree.cc  src/swoc_file.cc  src/swoc_ip.cc  src/TextView.cc src/string_view_util.cc

if EXPORT_SWOC_HEADERS
library_include_HEADERS = \
        include/swoc/ArenaWriter.h \
        include/swoc/BufferWriter.h \
        include/swoc/bwf_base.h \
        include/swoc/bwf_ex.h \
        include/swoc/bwf_fwd.h \
        include/swoc/bwf_ip.h \
        include/swoc/bwf_std.h \
        include/swoc/DiscreteRange.h \
        include/swoc/Errata.h \
        include/swoc/IntrusiveDList.h \
        include/swoc/IntrusiveHashMap.h \
        include/swoc/IPAddr.h \
        include/swoc/IPEndpoint.h \
        include/swoc/IPRange.h \
        include/swoc/IPSrv.h \
        include/swoc/Lexicon.h \
        include/swoc/MemArena.h \
        include/swoc/MemSpan.h \
        include/swoc/RBTree.h \
        include/swoc/Scalar.h \
        include/swoc/swoc_file.h \
        include/swoc/swoc_ip.h \
        include/swoc/swoc_meta.h \
        include/swoc/swoc_version.h\
        include/swoc/string_view_util.h \
        include/swoc/TextView.h \
        include/swoc/Vectray.h \
        include/swoc/HashFNV.h
endif

clean-local:

clang-tidy-local: $(DIST_SOURCES)
	$(CXX_Clang_Tidy)
