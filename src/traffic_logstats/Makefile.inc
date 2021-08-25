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

bin_PROGRAMS += traffic_logstats/traffic_logstats

traffic_logstats_traffic_logstats_CPPFLAGS = \
    $(AM_CPPFLAGS) \
	$(iocore_include_dirs) \
	-I$(abs_top_srcdir)/include \
	-I$(abs_top_srcdir)/lib \
	-I$(abs_top_srcdir)/proxy \
	-I$(abs_top_srcdir)/proxy/logging \
	-I$(abs_top_srcdir)/proxy/shared \
	-I$(abs_top_srcdir)/mgmt \
	-I$(abs_top_srcdir)/mgmt/utils \
	$(TS_INCLUDES)

traffic_logstats_traffic_logstats_LDFLAGS = \
	$(AM_LDFLAGS) \
	@YAMLCPP_LDFLAGS@

TESTS += \
	traffic_logstats/tests/test_logstats_json \
	traffic_logstats/tests/test_logstats_summary

traffic_logstats_traffic_logstats_SOURCES = \
    traffic_logstats/logstats.cc

traffic_logstats_traffic_logstats_LDADD = \
	$(top_builddir)/proxy/logging/liblogging.a \
	$(top_builddir)/proxy/hdrs/libhdrs.a \
	$(top_builddir)/proxy/shared/libdiagsconfig.a \
	$(top_builddir)/proxy/shared/libUglyLogStubs.a \
	$(top_builddir)/mgmt/libmgmt_p.la \
	$(top_builddir)/lib/records/librecords_p.a \
	$(top_builddir)/iocore/eventsystem/libinkevent.a \
	$(top_builddir)/src/tscore/libtscore.la \
	$(top_builddir)/src/tscpp/util/libtscpputil.la

traffic_logstats_traffic_logstats_LDADD += \
  @HWLOC_LIBS@ \
  @YAMLCPP_LIBS@ \
  @LIBPROFILER@ -lm
