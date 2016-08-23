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

include $(top_srcdir)/build/plugins.mk

AM_CPPFLAGS += \
  -I$(srcdir)/lib \
  -I$(srcdir)/fetcher \
  -I$(srcdir)/test

AM_CXXFLAGS = \
  -Wno-deprecated

noinst_LTLIBRARIES = libesicore.la libtest.la
pkglib_LTLIBRARIES = esi.la combo_handler.la

check_PROGRAMS = docnode_test parser_test processor_test utils_test vars_test gzip_test

libesicore_la_SOURCES = \
	lib/DocNode.cc \
	lib/EsiParser.cc \
	lib/EsiGzip.cc \
	lib/EsiGunzip.cc \
	lib/EsiProcessor.cc \
	lib/Expression.cc \
	lib/FailureInfo.cc \
	lib/HandlerManager.cc \
	lib/Stats.cc \
	lib/Utils.cc \
	lib/Variables.cc \
	lib/gzip.cc

# NOTE: HandlerManager::getHandler() is implemented differently in
# HandlerManager.cc and TestHandlerManager.cc. The unit tests depend
# on the TestHandlerManager.cc implementation, so don't accidentally
# link the wrong one into libtest.

libtest_la_SOURCES = \
	lib/DocNode.cc \
	lib/EsiParser.cc \
	lib/EsiGzip.cc \
	lib/EsiGunzip.cc \
	lib/EsiProcessor.cc \
	lib/Expression.cc \
	lib/FailureInfo.cc \
	lib/Stats.cc \
	lib/Utils.cc \
	lib/Variables.cc \
	lib/gzip.cc \
	test/print_funcs.cc \
	test/HandlerMap.cc \
	test/StubIncludeHandler.cc \
	test/TestHandlerManager.cc

esi_la_SOURCES =  \
	esi.cc \
	fetcher/HttpDataFetcherImpl.cc \
	serverIntercept.cc

combo_handler_la_SOURCES = \
	combo_handler.cc \
	fetcher/HttpDataFetcherImpl.cc

esi_la_LIBADD = libesicore.la
esi_la_LDFLAGS = $(TS_PLUGIN_LDFLAGS)

combo_handler_la_LIBADD = libesicore.la
combo_handler_la_LDFLAGS = $(TS_PLUGIN_LDFLAGS)

docnode_test_SOURCES = test/docnode_test.cc
docnode_test_LDADD = libtest.la -lz

parser_test_SOURCES = test/parser_test.cc
parser_test_LDADD = libtest.la -lz

processor_test_SOURCES = test/processor_test.cc
processor_test_LDADD = libtest.la -lz

utils_test_SOURCES = test/utils_test.cc
utils_test_LDADD = libtest.la -lz

vars_test_SOURCES = test/vars_test.cc
vars_test_LDADD = libtest.la -lz

gzip_test_SOURCES = test/gzip_test.cc
gzip_test_LDADD = libtest.la -lz

TESTS = $(check_PROGRAMS)

include $(top_srcdir)/build/tidy.mk

tidy-local: $(DIST_SOURCES)
	$(CXX_Clang_Tidy)
