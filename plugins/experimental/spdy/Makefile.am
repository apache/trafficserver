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

pkglibdir = ${pkglibexecdir}

if BUILD_HAVE_CXX_11

pkglib_LTLIBRARIES = spdy.la
lib_LTLIBRARIES = libspdy.la
check_PROGRAMS = zstream_test

AM_CPPFLAGS = \
  -I$(srcdir)/lib \
  -I$(top_builddir)/proxy/api \
  -D__STDC_FORMAT_MACROS=1

if BUILD_HAVE_LIBCXX
AM_CXXFLAGS = -stdlib=libc++
endif

spdy_la_SOURCES =  \
  http.cc \
  io.cc \
  protocol.cc \
  spdy.cc \
  stream.cc \
  strings.cc

libspdy_la_SOURCES = \
  lib/base/logging.cc \
  lib/spdy/message.cc \
  lib/spdy/zstream.cc

spdy_la_LIBADD = libspdy.la
spdy_la_LDFLAGS = -module -avoid-version -shared

zstream_test_SOURCES = tests/zstream_test.cc tests/stubs.cc
zstream_test_LDADD = libspdy.la -lz

TESTS = $(check_PROGRAMS)

test:: $(TESTS)
	for f in $(TESTS) ; do $$f ; done

endif # BUILD_HAVE_CXX_11
