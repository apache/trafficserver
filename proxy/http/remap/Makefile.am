# Makefile.am for Http
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
	-I$(abs_top_srcdir)/lib/records \
	-I$(abs_top_srcdir)/proxy \
	-I$(abs_top_srcdir)/mgmt \
	-I$(abs_top_srcdir)/mgmt/utils \
	-I$(abs_top_srcdir)/proxy/hdrs \
	-I$(abs_top_srcdir)/proxy/shared \
	-I$(abs_top_srcdir)/proxy/http \
	$(TS_INCLUDES)

noinst_LIBRARIES = libhttp_remap.a

libhttp_remap_a_SOURCES = \
	AclFiltering.cc \
	AclFiltering.h \
	RemapConfig.cc \
	RemapConfig.h \
	RemapPluginInfo.cc \
	RemapPluginInfo.h \
	PluginDso.cc \
	PluginFactory.cc \
	PluginFactory.h \
	RemapPlugins.cc \
	RemapPlugins.h \
	RemapProcessor.cc \
	RemapProcessor.h \
	UrlMapping.cc \
	UrlMapping.h \
	UrlMappingPathIndex.cc \
	UrlMappingPathIndex.h \
	UrlRewrite.cc \
	UrlRewrite.h

clang-tidy-local: $(libhttp_remap_a_SOURCES)
	$(CXX_Clang_Tidy)

TESTS = $(check_PROGRAMS)
check_PROGRAMS =  test_PluginDso test_PluginFactory test_RemapPluginInfo

test_PluginDso_CPPFLAGS = $(AM_CPPFLAGS) -I$(abs_top_srcdir)/tests/include -DPLUGIN_DSO_TESTS
EXTRA_test_PluginDso_DEPENDENCIES = unit-tests/plugin_v1.la
test_PluginDso_LDADD = $(OPENSSL_LIBS)
test_PluginDso_LDFLAGS = $(AM_LDFLAGS) -L$(top_builddir)/src/tscore/.libs -ltscore
test_PluginDso_SOURCES = \
	unit-tests/test_PluginDso.cc \
	unit-tests/plugin_testing_common.cc \
	PluginDso.cc

test_PluginFactory_CPPFLAGS = $(AM_CPPFLAGS) -I$(abs_top_srcdir)/tests/include -DPLUGIN_DSO_TESTS
EXTRA_test_PluginFactory_DEPENDENCIES = unit-tests/plugin_v1.la
test_PluginFactory_LDADD = $(OPENSSL_LIBS)
test_PluginFactory_LDFLAGS = $(AM_LDFLAGS) -L$(top_builddir)/src/tscore/.libs -ltscore
test_PluginFactory_SOURCES = \
	unit-tests/test_PluginFactory.cc \
	unit-tests/plugin_testing_common.cc \
	PluginFactory.cc \
	PluginDso.cc \
	RemapPluginInfo.cc

test_RemapPluginInfo_CPPFLAGS = $(AM_CPPFLAGS) -I$(abs_top_srcdir)/tests/include -DPLUGIN_DSO_TESTS
EXTRA_test_RemapPluginInfo_DEPENDENCIES = \
	unit-tests/plugin_missing_init.la \
	unit-tests/plugin_missing_doremap.la \
	unit-tests/plugin_missing_deleteinstance.la \
	unit-tests/plugin_required_cb.la \
	unit-tests/plugin_missing_newinstance.la \
	unit-tests/plugin_testing_calls.la
test_RemapPluginInfo_LDADD = \
	$(OPENSSL_LIBS)
test_RemapPluginInfo_LDFLAGS = $(AM_LDFLAGS) -L$(top_builddir)/src/tscore/.libs -ltscore
test_RemapPluginInfo_SOURCES = \
	unit-tests/plugin_testing_common.cc \
	unit-tests/plugin_testing_calls.cc \
	unit-tests/test_RemapPlugin.cc \
	PluginDso.cc \
	RemapPluginInfo.cc


DSO_LDFLAGS = \
	-module \
	-shared \
	-avoid-version \
	-export-symbols-regex '^(TSRemapInit|TSRemapDone|TSRemapDoRemap|TSRemapNewInstance|TSRemapDeleteInstance|TSRemapOSResponse|TSRemapConfigReload|TSPluginInit|pluginDsoVersionTest|getPluginDebugObjectTest)$$'

# Build plugins for unit testing the plugin (re)load.
pkglib_LTLIBRARIES = \
	unit-tests/plugin_v1.la \
	unit-tests/plugin_v2.la \
	unit-tests/plugin_required_cb.la \
	unit-tests/plugin_missing_deleteinstance.la \
	unit-tests/plugin_missing_doremap.la \
	unit-tests/plugin_missing_init.la \
	unit-tests/plugin_missing_newinstance.la \
	unit-tests/plugin_testing_calls.la
unit_tests_plugin_v1_la_SOURCES = unit-tests/plugin_misc_cb.cc
unit_tests_plugin_v1_la_LDFLAGS = $(DSO_LDFLAGS)
unit_tests_plugin_v1_la_CPPFLAGS = -DPLUGINDSOVER=1 $(AM_CPPFLAGS)
unit_tests_plugin_v2_la_SOURCES = unit-tests/plugin_misc_cb.cc
unit_tests_plugin_v2_la_LDFLAGS = $(DSO_LDFLAGS)
unit_tests_plugin_v2_la_CPPFLAGS = -DPLUGINDSOVER=2 $(AM_CPPFLAGS)
unit_tests_plugin_required_cb_la_SOURCES = unit-tests/plugin_required_cb.cc
unit_tests_plugin_required_cb_la_LDFLAGS = $(DSO_LDFLAGS)
unit_tests_plugin_required_cb_la_CPPFLAGS = -DPLUGINDSOVER=1 $(AM_CPPFLAGS)
unit_tests_plugin_missing_deleteinstance_la_SOURCES = unit-tests/plugin_missing_deleteinstance.cc
unit_tests_plugin_missing_deleteinstance_la_LDFLAGS = $(DSO_LDFLAGS)
unit_tests_plugin_missing_deleteinstance_la_CPPFLAGS = -DPLUGINDSOVER=1 $(AM_CPPFLAGS)
unit_tests_plugin_missing_doremap_la_SOURCES = unit-tests/plugin_missing_doremap.cc
unit_tests_plugin_missing_doremap_la_LDFLAGS = $(DSO_LDFLAGS)
unit_tests_plugin_missing_doremap_la_CPPFLAGS = -DPLUGINDSOVER=1 $(AM_CPPFLAGS)
unit_tests_plugin_missing_init_la_SOURCES = unit-tests/plugin_missing_init.cc
unit_tests_plugin_missing_init_la_LDFLAGS = $(DSO_LDFLAGS)
unit_tests_plugin_missing_init_la_CPPFLAGS = -DPLUGINDSOVER=1 $(AM_CPPFLAGS)
unit_tests_plugin_missing_newinstance_la_SOURCES = unit-tests/plugin_missing_newinstance.cc
unit_tests_plugin_missing_newinstance_la_LDFLAGS = $(DSO_LDFLAGS)
unit_tests_plugin_missing_newinstance_la_CPPFLAGS = -DPLUGINDSOVER=1 $(AM_CPPFLAGS)
unit_tests_plugin_testing_calls_la_SOURCES = unit-tests/plugin_testing_calls.cc unit-tests/plugin_testing_common.cc
unit_tests_plugin_testing_calls_la_LDFLAGS = $(DSO_LDFLAGS)
unit_tests_plugin_testing_calls_la_CPPFLAGS = -DPLUGINDSOVER=1 $(AM_CPPFLAGS)
