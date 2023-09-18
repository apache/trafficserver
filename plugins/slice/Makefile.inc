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

pkglib_LTLIBRARIES += slice/slice.la

slice_slice_la_SOURCES = \
    slice/client.cc \
    slice/client.h \
    slice/Config.cc \
    slice/Config.h \
    slice/ContentRange.cc \
    slice/ContentRange.h \
    slice/Data.h \
    slice/HttpHeader.cc \
    slice/HttpHeader.h \
    slice/intercept.cc \
    slice/intercept.h \
    slice/prefetch.cc \
    slice/prefetch.h \
    slice/Range.cc \
    slice/Range.h \
    slice/response.cc \
    slice/response.h \
    slice/server.cc \
    slice/server.h \
    slice/slice.cc \
    slice/slice.h \
    slice/Stage.h \
    slice/transfer.cc \
    slice/transfer.h \
    slice/util.cc \
    slice/util.h

check_PROGRAMS += slice/test_content_range

slice_test_content_range_CPPFLAGS = $(AM_CPPFLAGS) -I$(abs_top_srcdir)/lib/catch2 -DUNITTEST
slice_test_content_range_SOURCES = \
    slice/unit-tests/test_content_range.cc \
    slice/ContentRange.cc

slice_test_content_range_LDADD = @LIBPCRE@

check_PROGRAMS += slice/test_range

slice_test_range_CPPFLAGS = $(AM_CPPFLAGS) -I$(abs_top_srcdir)/lib/catch2 -DUNITTEST
slice_test_range_SOURCES = \
    slice/unit-tests/test_range.cc \
    slice/Range.cc

slice_test_range_LDADD = @LIBPCRE@

check_PROGRAMS += slice/test_config

slice_test_config_CPPFLAGS = $(AM_CPPFLAGS) -I$(abs_top_srcdir)/lib/catch2 -DUNITTEST
slice_test_config_SOURCES = \
    slice/unit-tests/test_config.cc \
    slice/Config.cc

slice_test_config_LDADD = @LIBPCRE@
