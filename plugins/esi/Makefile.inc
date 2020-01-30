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
	-I$(srcdir)/esi \
	-I$(srcdir)/esi/lib \
	-I$(srcdir)/esi/fetcher \
	-I$(srcdir)/esi/test \
	$(AM_CPPFLAGS)

# Add -Wno-deprecated because ESI uses ext/hash_map.
ESI_CXXFLAGS = \
	$(AM_CXXFLAGS) \
	-Wno-deprecated

noinst_LTLIBRARIES += \
	esi/libesicore.la \
	esi/libtest.la

pkglib_LTLIBRARIES += \
	esi/esi.la \
	esi/combo_handler.la

check_PROGRAMS += \
	esi/docnode_test \
	esi/parser_test \
	esi/processor_test \
	esi/utils_test \
	esi/vars_test \
	esi/gzip_test

esi_libesicore_la_SOURCES = \
	esi/lib/Attribute.h \
	esi/lib/ComponentBase.h \
	esi/lib/DocNode.cc \
	esi/lib/DocNode.h \
	esi/lib/EsiGunzip.cc \
	esi/lib/EsiGunzip.h \
	esi/lib/EsiGzip.cc \
	esi/lib/EsiGzip.h \
	esi/lib/EsiParser.cc \
	esi/lib/EsiParser.h \
	esi/lib/EsiProcessor.cc \
	esi/lib/EsiProcessor.h \
	esi/lib/Expression.cc \
	esi/lib/Expression.h \
	esi/lib/HandlerManager.h \
	esi/lib/HttpHeader.h \
	esi/lib/IncludeHandlerFactory.h \
	esi/lib/SpecialIncludeHandler.h \
	esi/lib/Stats.cc \
	esi/lib/Stats.h \
	esi/lib/StringHash.h \
	esi/lib/Utils.cc \
	esi/lib/Utils.h \
	esi/lib/Variables.cc \
	esi/lib/Variables.h \
	esi/lib/gzip.cc \
	esi/lib/gzip.h

esi_libesicore_la_CPPFLAGS = $(ESI_CPPFLAGS)
esi_libesicore_la_CXXFLAGS = $(ESI_CXXFLAGS)

# NOTE: HandlerManager::getHandler() is implemented differently in
# HandlerManager.cc and TestHandlerManager.cc. The unit tests depend
# on the TestHandlerManager.cc implementation, so don't accidentally
# link the wrong one into libtest.

esi_libtest_la_SOURCES = \
	esi/test/print_funcs.cc \
	esi/test/HandlerMap.cc \
	esi/test/StubIncludeHandler.cc \
	esi/test/TestHandlerManager.cc

esi_libtest_la_CPPFLAGS = $(ESI_CPPFLAGS)
esi_libtest_la_CXXFLAGS = $(ESI_CXXFLAGS)
esi_libtest_la_LIBADD = esi/libesicore.la

esi_esi_la_SOURCES =  \
	esi/esi.cc \
	esi/fetcher/HttpDataFetcherImpl.cc \
	esi/lib/HandlerManager.cc \
	esi/serverIntercept.cc

esi_esi_la_CPPFLAGS = $(ESI_CPPFLAGS)
esi_esi_la_CXXFLAGS = $(ESI_CXXFLAGS)
esi_esi_la_LIBADD = esi/libesicore.la

esi_combo_handler_la_SOURCES = \
	esi/combo_handler.cc \
	esi/fetcher/HttpDataFetcherImpl.cc \
	esi/lib/HandlerManager.cc

esi_combo_handler_la_CPPFLAGS = $(ESI_CPPFLAGS)
esi_combo_handler_la_CXXFLAGS = $(ESI_CXXFLAGS)
esi_combo_handler_la_LIBADD = esi/libesicore.la

esi_docnode_test_CPPFLAGS = $(ESI_CPPFLAGS)
esi_docnode_test_CXXFLAGS = $(ESI_CXXFLAGS)
esi_docnode_test_LDADD = esi/libtest.la -lz
esi_docnode_test_SOURCES = esi/test/docnode_test.cc

esi_parser_test_CPPFLAGS = $(ESI_CPPFLAGS)
esi_parser_test_CXXFLAGS = $(ESI_CXXFLAGS)
esi_parser_test_LDADD = esi/libtest.la -lz
esi_parser_test_SOURCES = esi/test/parser_test.cc

esi_processor_test_CPPFLAGS = $(ESI_CPPFLAGS)
esi_processor_test_CXXFLAGS = $(ESI_CXXFLAGS)
esi_processor_test_LDADD = esi/libtest.la -lz
esi_processor_test_SOURCES = esi/test/processor_test.cc

esi_utils_test_CPPFLAGS = $(ESI_CPPFLAGS)
esi_utils_test_CXXFLAGS = $(ESI_CXXFLAGS)
esi_utils_test_LDADD = esi/libtest.la -lz
esi_utils_test_SOURCES = esi/test/utils_test.cc

esi_vars_test_CPPFLAGS = $(ESI_CPPFLAGS)
esi_vars_test_CXXFLAGS = $(ESI_CXXFLAGS)
esi_vars_test_LDADD = esi/libtest.la -lz
esi_vars_test_SOURCES = esi/test/vars_test.cc

esi_gzip_test_CPPFLAGS = $(ESI_CPPFLAGS)
esi_gzip_test_CXXFLAGS = $(ESI_CXXFLAGS)
esi_gzip_test_LDADD = esi/libtest.la -lz
esi_gzip_test_SOURCES = esi/test/gzip_test.cc
