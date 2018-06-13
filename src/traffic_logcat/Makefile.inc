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

bin_PROGRAMS += traffic_logcat/traffic_logcat

traffic_logcat_traffic_logcat_CPPFLAGS = \
    $(AM_CPPFLAGS) \
	$(iocore_include_dirs) \
	-I$(abs_top_srcdir)/lib \
	-I$(abs_top_srcdir)/lib/records \
	-I$(abs_top_srcdir)/proxy \
	-I$(abs_top_srcdir)/proxy/logging \
	-I$(abs_top_srcdir)/proxy/shared \
	-I$(abs_top_srcdir)/mgmt \
	-I$(abs_top_srcdir)/mgmt/utils \
	$(TS_INCLUDES)

traffic_logcat_traffic_logcat_SOURCES = \
    traffic_logcat/logcat.cc

traffic_logcat_traffic_logcat_LDADD = \
	$(top_builddir)/proxy/logging/liblogging.a \
	$(top_builddir)/proxy/shared/libdiagsconfig.a \
	$(top_builddir)/proxy/shared/libUglyLogStubs.a \
	$(top_builddir)/mgmt/libmgmt_p.la \
	$(top_builddir)/lib/records/librecords_p.a \
	$(top_builddir)/iocore/eventsystem/libinkevent.a \
	$(top_builddir)/lib/ts/libtsutil.la \
	$(top_builddir)/lib/bindings/libbindings.la

if BUILD_LUAJIT
traffic_logcat_traffic_logcat_LDADD += \
	$(top_builddir)/lib/luajit/src/libluajit.a
endif

traffic_logcat_traffic_logcat_LDADD += \
	@LIBTCL@ @HWLOC_LIBS@\
	@LIBPROFILER@ -lm

if SYSTEM_LUAJIT
traffic_logcat_traffic_logcat_LDADD += \
    @LIBLUAJIT@
endif
