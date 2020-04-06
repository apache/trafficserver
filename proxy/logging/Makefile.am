# Makefile.am for Logging
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

include $(top_srcdir)/build/tidy.mk

AM_CPPFLAGS += \
	$(iocore_include_dirs) \
	-I$(abs_top_srcdir)/include \
	-I$(abs_top_srcdir)/lib \
	-I$(abs_top_srcdir)/proxy \
	-I$(abs_top_srcdir)/proxy/http \
	-I$(abs_top_srcdir)/proxy/http/remap \
	-I$(abs_top_srcdir)/proxy/hdrs \
	-I$(abs_top_srcdir)/proxy/shared \
	-I$(abs_top_srcdir)/mgmt \
	-I$(abs_top_srcdir)/mgmt/utils \
	$(TS_INCLUDES) \
	@YAMLCPP_INCLUDES@

EXTRA_DIST = LogStandalone.cc

noinst_LIBRARIES = liblogging.a

liblogging_a_SOURCES = \
	Log.cc \
	Log.h \
	LogAccess.cc \
	LogAccess.h \
	LogBuffer.cc \
	LogBuffer.h \
	LogBufferSink.h \
	LogConfig.cc \
	LogConfig.h \
	LogField.cc \
	LogField.h \
	LogFieldAliasMap.cc \
	LogFieldAliasMap.h \
	LogFile.cc \
	LogFile.h \
	LogFilter.cc \
	LogFilter.h \
	LogFormat.cc \
	LogFormat.h \
	LogLimits.h \
	LogObject.cc \
	LogObject.h \
	LogUtils.cc \
	LogUtils.h \
	RolledLogDeleter.cc \
	RolledLogDeleter.h \
	YamlLogConfig.cc \
	YamlLogConfigDecoders.cc \
	YamlLogConfig.h

check_PROGRAMS = \
	test_LogUtils \
	test_RolledLogDeleter

TESTS = $(check_PROGRAMS)

test_LogUtils_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-DTEST_LOG_UTILS \
	-I$(abs_top_srcdir)/tests/include

test_LogUtils_SOURCES = \
	LogUtils.cc \
	unit-tests/test_LogUtils.cc

test_LogUtils_LDADD = \
	$(top_builddir)/src/tscore/libtscore.la \
	$(top_builddir)/src/tscpp/util/libtscpputil.la \
	$(top_builddir)/iocore/eventsystem/libinkevent.a

test_RolledLogDeleter_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-DTEST_LOG_UTILS \
	-I$(abs_top_srcdir)/tests/include

test_RolledLogDeleter_SOURCES = \
	RolledLogDeleter.cc \
	LogUtils.cc \
	unit-tests/test_RolledLogDeleter.cc

test_RolledLogDeleter_LDADD = \
	$(top_builddir)/src/tscore/libtscore.la \
	$(top_builddir)/src/tscpp/util/libtscpputil.la \
	$(top_builddir)/iocore/eventsystem/libinkevent.a

clang-tidy-local: $(liblogging_a_SOURCES) $(EXTRA_DIST)
	$(CXX_Clang_Tidy)
