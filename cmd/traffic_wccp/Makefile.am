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


AM_CPPFLAGS = $(iocore_include_dirs) \
  -I$(top_srcdir)/lib \
  -I$(top_srcdir)/lib/records \
  -I$(top_srcdir)/lib/wccp \
  @OPENSSL_INCLUDES@

AM_LDFLAGS = \
  @EXTRA_CXX_LDFLAGS@ \
  @LIBTOOL_LINK_FLAGS@ \
  @OPENSSL_LDFLAGS@

bin_PROGRAMS = traffic_wccp

traffic_wccp_SOURCES = \
  wccp_client.cc

traffic_wccp_LDADD = \
  $(top_builddir)/lib/tsconfig/libtsconfig.la \
  $(top_builddir)/lib/wccp/libwccp.a \
  $(top_builddir)/lib/ts/libtsutil.la \
  @OPENSSL_LIBS@
