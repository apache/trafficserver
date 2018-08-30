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

bin_PROGRAMS += traffic_wccp/traffic_wccp

traffic_wccp_traffic_wccp_CPPFLAGS = \
    $(AM_CPPFLAGS) \
    $(iocore_include_dirs) \
    -I$(abs_top_srcdir)/include \
    -I$(abs_top_srcdir)/include/records \
    -I$(abs_top_srcdir)/include/wccp \
    $(TS_INCLUDES) \
    @OPENSSL_INCLUDES@

traffic_wccp_traffic_wccp_LDFLAGS = \
    $(AM_LDFLAGS) \
    @OPENSSL_LDFLAGS@

traffic_wccp_traffic_wccp_SOURCES = \
	traffic_wccp/wccp_client.cc

traffic_wccp_traffic_wccp_LDADD = \
	$(top_builddir)/lib/tsconfig/libtsconfig.la \
	$(top_builddir)/src/wccp/libwccp.a \
	$(top_builddir)/src/tscore/libtscore.la \
	$(top_builddir)/src/tscpp/util/libtscpputil.la \
	@OPENSSL_LIBS@
