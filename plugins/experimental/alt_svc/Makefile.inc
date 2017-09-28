
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

pkglib_LTLIBRARIES += experimental/alt_svc/alt_svc.la

noinst_LTLIBRARIES += experimental/alt_svc/prefix_parser.la
noinst_LTLIBRARIES += experimental/alt_svc/ip_host_map.la

experimental_alt_svc_alt_svc_la_SOURCES = \
  experimental/alt_svc/alt_svc.cc \
  experimental/alt_svc/default.h

experimental_alt_svc_prefix_parser_la_SOURCES = \
	experimental/alt_svc/prefix_parser.cc \
	experimental/alt_svc/prefix_parser.h \
	experimental/alt_svc/default.h

experimental_alt_svc_prefix_parser_la_CPPFLAGS = \
	$(AM_CPPFLAGS)

experimental_alt_svc_ip_host_map_la_SOURCES = \
	experimental/alt_svc/prefix_parser.cc \
	experimental/alt_svc/prefix_parser.h \
	experimental/alt_svc/single_service_file_map.cc \
	experimental/alt_svc/ip_host_map.h \
	experimental/alt_svc/default.h

experimental_alt_svc_ip_host_map_la_CPPFLAGS = \
	$(AM_CPPFLAGS)

libatscppapi = $(top_builddir)/lib/cppapi/libatscppapi.la

experimental_alt_svc_alt_svc_la_LIBADD = \
	experimental/alt_svc/ip_host_map.la \
	$(libatscppapi)

check_PROGRAMS += experimental/alt_svc/prefix_parser_test
experimental_alt_svc_prefix_parser_test_SOURCES = \
	experimental/alt_svc/prefix_parser_test.cc
experimental_alt_svc_prefix_parser_test_CPPFLAGS = $(AM_CPPFLAGS)\
	-I$(abs_top_srcdir)/tests/include
experimental_alt_svc_prefix_parser_test_LDFLAGS = -Wl,--as-needed
experimental_alt_svc_prefix_parser_test_LDADD = \
	experimental/alt_svc/prefix_parser.la \
	@AM_LDFLAGS@ \
	$(abs_top_builddir)/lib/ts/libtsutil.la  \
	$(libatscppapi)

check_PROGRAMS += experimental/alt_svc/ip_host_map_test
experimental_alt_svc_ip_host_map_test_SOURCES = \
	experimental/alt_svc/ip_host_map_test.cc
experimental_alt_svc_ip_host_map_test_CPPFLAGS = $(AM_CPPFLAGS)\
	-I$(abs_top_srcdir)/tests/include
experimental_alt_svc_ip_host_map_test_LDFLAGS = -Wl,--as-needed
experimental_alt_svc_ip_host_map_test_LDADD = \
	experimental/alt_svc/prefix_parser.la \
	experimental/alt_svc/ip_host_map.la \
	@AM_LDFLAGS@ \
	$(abs_top_builddir)/lib/ts/libtsutil.la  \
	$(libatscppapi)
