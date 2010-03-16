# Makefile.am for install
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

AM_CPPFLAGS = \
  -I$(top_srcdir)/libinktomi++ \
  -I$(top_srcdir)/librecords \
  -I$(top_srcdir)/proxy \
  -I$(top_srcdir)/proxy/mgmt2 \
  -I$(top_srcdir)/proxy/mgmt2/utils \
  -I$(top_srcdir)/proxy/cache \
  -I$(top_srcdir)/proxy/logging

dist_bin_SCRIPTS = \
  trafficserver

noinst_PROGRAMS = update_records

update_records_SOURCES = update_records.cc 
update_records_LDFLAGS = @EXTRA_CXX_LDFLAGS@
update_records_LDADD =	\
			$(top_builddir)/proxy/mgmt2/librecords.a \
			$(top_builddir)/libinktomi++/libinktomi++.a \
			@LIBICONV@ @LIBTHREAD@ @LIBRT@ @LIBTCL@ @LIBDL@ @LIBSSL@ @LIBEXECINFO@

