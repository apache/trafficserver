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

SUBDIRS = utils api

# Decoder ring:
# 	libmgmt_c.la 	Configuration records (defaults).
# 	libmgmt_lm.la	libmgmt for Local Manager applications (traffic_manager)
# 	libmgmt_p.la	libmgmt for Process Manager applications (everything else)
noinst_LTLIBRARIES = libmgmt_c.la libmgmt_p.la libmgmt_lm.la

AM_CPPFLAGS += \
	$(iocore_include_dirs) \
	-I$(abs_top_srcdir)/include \
	-I$(abs_top_srcdir)/lib \
	-I$(abs_top_srcdir)/mgmt/api/include \
	-I$(abs_top_srcdir)/mgmt/utils \
	-I$(abs_top_srcdir)/proxy \
	-I$(abs_top_srcdir)/proxy/http \
	-I$(abs_top_srcdir)/proxy/hdrs \
	$(TS_INCLUDES)

libmgmt_c_la_SOURCES = \
	RecordsConfig.cc \
	RecordsConfig.h

libmgmt_COMMON = \
	BaseManager.cc \
	BaseManager.h \
	MgmtDefs.h \
	RecordsConfigUtils.cc

libmgmt_p_la_SOURCES = \
	$(libmgmt_COMMON) \
	ProcessManager.cc \
	ProcessManager.h \
	ProxyConfig.cc \
	ProxyConfig.h

libmgmt_lm_la_SOURCES = \
	$(libmgmt_COMMON) \
	Alarms.cc \
	Alarms.h \
	DerivativeMetrics.cc \
	DerivativeMetrics.h \
	FileManager.cc \
	FileManager.h \
	LocalManager.cc \
	LocalManager.h \
	ConfigManager.cc \
	ConfigManager.h \
	DerivativeMetrics.cc \
	DerivativeMetrics.h \
	WebMgmtUtils.cc \
	WebMgmtUtils.h

libmgmt_lm_la_LIBADD = \
	libmgmt_c.la \
	$(top_builddir)/mgmt/utils/libutils_lm.la

libmgmt_p_la_LIBADD = \
	libmgmt_c.la \
	$(top_builddir)/mgmt/utils/libutils_p.la

include $(top_srcdir)/build/tidy.mk

clang-tidy-local: $(DIST_SOURCES)
	$(CXX_Clang_Tidy)
