#
# Makefile.am for the Enterprise Management module.
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

bin_PROGRAMS += traffic_via/traffic_via

traffic_via_traffic_via_CPPFLAGS = \
    $(AM_CPPFLAGS) \
	$(iocore_include_dirs) \
	-I$(abs_top_srcdir)/lib \
	-I$(abs_top_srcdir)/mgmt/api/include \
	$(TS_INCLUDES)

traffic_via_traffic_via_LDFLAGS = \
    $(AM_LDFLAGS) \
	@OPENSSL_LDFLAGS@

traffic_via_traffic_via_SOURCES = \
	traffic_via/traffic_via.cc

traffic_via_traffic_via_LDADD = \
	$(top_builddir)/lib/ts/libtsutil.la \
	@LIBPCRE@

TESTS += \
	traffic_via/test_traffic_via
