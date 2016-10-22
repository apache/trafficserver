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

ESI_CPPFLAGS = \
	-Iplugins/esi \
	-Iplugins/esi/lib \
	-Iplugins/esi/fetcher \
	-Iplugins/esi/test \
	$(TS_PLUGIN_CPPFLAGS)

# Add -Wno-deprecated because ESI uses ext/hash_map.
ESI_CXXFLAGS = \
	$(AM_CXXFLAGS) \
	-Wno-deprecated

noinst_LTLIBRARIES += \
	plugins/esi/libesicore.la \
	plugins/esi/libtest.la

pkglib_LTLIBRARIES += \
	plugins/esi/esi.la \
	plugins/esi/combo_handler.la

check_PROGRAMS += \
	plugins/esi/docnode_test \
	plugins/esi/parser_test \
	plugins/esi/processor_test \
	plugins/esi/utils_test \
	plugins/esi/vars_test \
	plugins/esi/gzip_test

plugins_esi_libesicore_la_SOURCES = \
	plugins/esi/lib/Attribute.h \
	plugins/esi/lib/ComponentBase.h \
	plugins/esi/lib/DocNode.cc \
	plugins/esi/lib/DocNode.h \
	plugins/esi/lib/EsiGunzip.cc \
	plugins/esi/lib/EsiGunzip.h \
	plugins/esi/lib/EsiGzip.cc \
	plugins/esi/lib/EsiGzip.h \
	plugins/esi/lib/EsiParser.cc \
	plugins/esi/lib/EsiParser.h \
	plugins/esi/lib/EsiProcessor.cc \
	plugins/esi/lib/EsiProcessor.h \
	plugins/esi/lib/Expression.cc \
	plugins/esi/lib/Expression.h \
	plugins/esi/lib/FailureInfo.cc \
	plugins/esi/lib/FailureInfo.h \
	plugins/esi/lib/HandlerManager.h \
	plugins/esi/lib/HttpHeader.h \
	plugins/esi/lib/IncludeHandlerFactory.h \
	plugins/esi/lib/SpecialIncludeHandler.h \
	plugins/esi/lib/Stats.cc \
	plugins/esi/lib/Stats.h \
	plugins/esi/lib/StringHash.h \
	plugins/esi/lib/Utils.cc \
	plugins/esi/lib/Utils.h \
	plugins/esi/lib/Variables.cc \
	plugins/esi/lib/Variables.h \
	plugins/esi/lib/gzip.cc \
	plugins/esi/lib/gzip.h

plugins_esi_libesicore_la_CPPFLAGS = $(ESI_CPPFLAGS)
plugins_esi_libesicore_la_CXXFLAGS = $(ESI_CXXFLAGS)

# NOTE: HandlerManager::getHandler() is implemented differently in
# HandlerManager.cc and TestHandlerManager.cc. The unit tests depend
# on the TestHandlerManager.cc implementation, so don't accidentally
# link the wrong one into libtest.

plugins_esi_libtest_la_SOURCES = \
	plugins/esi/test/print_funcs.cc \
	plugins/esi/test/HandlerMap.cc \
	plugins/esi/test/StubIncludeHandler.cc \
	plugins/esi/test/TestHandlerManager.cc

plugins_esi_libtest_la_CPPFLAGS = $(ESI_CPPFLAGS)
plugins_esi_libtest_la_CXXFLAGS = $(ESI_CXXFLAGS)
plugins_esi_libtest_la_LIBADD = plugins/esi/libesicore.la

plugins_esi_esi_la_SOURCES =  \
	plugins/esi/esi.cc \
	plugins/esi/fetcher/HttpDataFetcherImpl.cc \
	plugins/esi/lib/HandlerManager.cc \
	plugins/esi/serverIntercept.cc

plugins_esi_esi_la_CPPFLAGS = $(ESI_CPPFLAGS)
plugins_esi_esi_la_CXXFLAGS = $(ESI_CXXFLAGS)
plugins_esi_esi_la_LDFLAGS = $(TS_PLUGIN_LDFLAGS)
plugins_esi_esi_la_LIBADD = plugins/esi/libesicore.la

plugins_esi_combo_handler_la_SOURCES = \
	plugins/esi/combo_handler.cc \
	plugins/esi/fetcher/HttpDataFetcherImpl.cc \
	plugins/esi/lib/HandlerManager.cc

plugins_esi_combo_handler_la_CPPFLAGS = $(ESI_CPPFLAGS)
plugins_esi_combo_handler_la_CXXFLAGS = $(ESI_CXXFLAGS)
plugins_esi_combo_handler_la_LDFLAGS = $(TS_PLUGIN_LDFLAGS)
plugins_esi_combo_handler_la_LIBADD = plugins/esi/libesicore.la

plugins_esi_docnode_test_CPPFLAGS = $(ESI_CPPFLAGS)
plugins_esi_docnode_test_CXXFLAGS = $(ESI_CXXFLAGS)
plugins_esi_docnode_test_LDADD = plugins/esi/libtest.la -lz
plugins_esi_docnode_test_SOURCES = plugins/esi/test/docnode_test.cc

plugins_esi_parser_test_CPPFLAGS = $(ESI_CPPFLAGS)
plugins_esi_parser_test_CXXFLAGS = $(ESI_CXXFLAGS)
plugins_esi_parser_test_LDADD = plugins/esi/libtest.la -lz
plugins_esi_parser_test_SOURCES = plugins/esi/test/parser_test.cc

plugins_esi_processor_test_CPPFLAGS = $(ESI_CPPFLAGS)
plugins_esi_processor_test_CXXFLAGS = $(ESI_CXXFLAGS)
plugins_esi_processor_test_LDADD = plugins/esi/libtest.la -lz
plugins_esi_processor_test_SOURCES = plugins/esi/test/processor_test.cc

plugins_esi_utils_test_CPPFLAGS = $(ESI_CPPFLAGS)
plugins_esi_utils_test_CXXFLAGS = $(ESI_CXXFLAGS)
plugins_esi_utils_test_LDADD = plugins/esi/libtest.la -lz
plugins_esi_utils_test_SOURCES = plugins/esi/test/utils_test.cc

plugins_esi_vars_test_CPPFLAGS = $(ESI_CPPFLAGS)
plugins_esi_vars_test_CXXFLAGS = $(ESI_CXXFLAGS)
plugins_esi_vars_test_LDADD = plugins/esi/libtest.la -lz
plugins_esi_vars_test_SOURCES = plugins/esi/test/vars_test.cc

plugins_esi_gzip_test_CPPFLAGS = $(ESI_CPPFLAGS)
plugins_esi_gzip_test_CXXFLAGS = $(ESI_CXXFLAGS)
plugins_esi_gzip_test_LDADD = plugins/esi/libtest.la -lz
plugins_esi_gzip_test_SOURCES = plugins/esi/test/gzip_test.cc
