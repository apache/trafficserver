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

bin_PROGRAMS += traffic_crashlog/traffic_crashlog

traffic_crashlog_traffic_crashlog_CPPFLAGS = \
    $(AM_CPPFLAGS) \
    $(iocore_include_dirs) \
    -I$(abs_top_srcdir)/include \
    -I$(abs_top_srcdir)/lib \
    -I$(abs_top_srcdir)/mgmt \
    -I$(abs_top_srcdir)/mgmt/utils \
    -I$(abs_top_srcdir)/mgmt/api/include \
    @SWOC_INCLUDES@ \
    $(TS_INCLUDES)

traffic_crashlog_traffic_crashlog_LDFLAGS = \
    $(AM_LDFLAGS) \
    @OPENSSL_LDFLAGS@

traffic_crashlog_traffic_crashlog_SOURCES = \
	traffic_crashlog/procinfo.cc \
	traffic_crashlog/traffic_crashlog.cc \
	traffic_crashlog/traffic_crashlog.h

traffic_crashlog_traffic_crashlog_LDADD = \
	$(top_builddir)/src/records/librecords_p.a \
	$(top_builddir)/mgmt/libmgmt_p.la \
	$(top_builddir)/proxy/shared/libUglyLogStubs.a \
	$(top_builddir)/iocore/eventsystem/libinkevent.a \
	$(top_builddir)/iocore/net/libinknet.a \
	$(top_builddir)/src/tscore/libtscore.la \
	$(top_builddir)/src/tscpp/util/libtscpputil.la \
	@HWLOC_LIBS@ @SWOC_LIBS@ @YAMLCPP_LIBS@
