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

pkglib_LTLIBRARIES += experimental/cookie_remap/cookie_remap.la

experimental_cookie_remap_cookie_remap_la_SOURCES = \
	experimental/cookie_remap/cookie_remap.cc \
	experimental/cookie_remap/hash.c \
	experimental/cookie_remap/strip.c \
	experimental/cookie_remap/cookiejar.cc

experimental_cookie_remap_cookie_remap_la_LDFLAGS = \
	$(AM_LDFLAGS)

AM_CPPFLAGS += @YAMLCPP_INCLUDES@

check_PROGRAMS +=  \
	cookie_remap/test_cookiejar

cookie_remap_test_cookiejar_CPPFLAGS = $(AM_CPPFLAGS) -Iexperimental/cookie_remap -I$(abs_top_srcdir)/tests/include
cookie_remap_test_cookiejar_SOURCES = \
    experimental/cookie_remap/unit_tests/cookiejar/unit_test.cc \
    experimental/cookie_remap/strip.c \
    experimental/cookie_remap/cookiejar.cc

# vim: ft=make ts=8 sw=8 et:
