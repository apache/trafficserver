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

bin_PROGRAMS += traffic_ctl/traffic_ctl

traffic_ctl_traffic_ctl_CPPFLAGS = \
    $(AM_CPPFLAGS) \
    $(iocore_include_dirs) \
    -I$(abs_top_srcdir)/include \
    -I$(abs_top_srcdir)/lib \
    -I$(abs_top_srcdir)/mgmt \
    -I$(abs_top_srcdir)/mgmt/api/include \
    -I$(abs_top_srcdir)/proxy \
    $(TS_INCLUDES)

traffic_ctl_traffic_ctl_SOURCES = \
	traffic_ctl/alarm.cc \
	traffic_ctl/config.cc \
	traffic_ctl/metric.cc \
	traffic_ctl/plugin.cc \
	traffic_ctl/server.cc \
	traffic_ctl/storage.cc \
	traffic_ctl/host.cc \
	traffic_ctl/traffic_ctl.cc

traffic_ctl_traffic_ctl_LDADD = \
	$(top_builddir)/lib/records/librecords_p.a \
	$(top_builddir)/mgmt/libmgmt_p.la \
	$(top_builddir)/iocore/eventsystem/libinkevent.a \
	$(top_builddir)/proxy/shared/libUglyLogStubs.a \
	$(top_builddir)/mgmt/api/libtsmgmt.la \
	$(top_builddir)/src/tscore/libtscore.la \
	$(top_builddir)/src/tscpp/util/libtscpputil.la \
	@LIBTCL@ @HWLOC_LIBS@
