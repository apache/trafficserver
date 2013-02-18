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

pkglibdir = ${pkglibexecdir}


AM_CXXFLAGS = \
  -I$(srcdir)/lib \
  -I$(srcdir)/fetcher \
  -I$(srcdir)/test \
  -I$(top_builddir)/proxy/api \
  -I$(top_srcdir)/proxy/api \
  -Wno-deprecated

pkglib_LTLIBRARIES = esi.la
lib_LTLIBRARIES = libesi.la libtest.la

check_PROGRAMS = docnode_test parser_test processor_test utils_test vars_test

libesi_la_SOURCES = \
	lib/DocNode.cc \
	lib/EsiParser.cc \
	lib/EsiProcessor.cc \
	lib/Expression.cc \
	lib/FailureInfo.cc \
	lib/HandlerManager.cc \
	lib/Stats.cc \
	lib/Utils.cc \
	lib/Variables.cc \
	lib/gzip.cc

libtest_la_SOURCES = \
	lib/DocNode.cc \
	lib/EsiParser.cc \
	lib/EsiProcessor.cc \
	lib/Expression.cc \
	lib/FailureInfo.cc \
	test/TestHandlerManager.cc \
	lib/Stats.cc \
	lib/Utils.cc \
	lib/Variables.cc \
	lib/gzip.cc

esi_la_SOURCES =  \
	fetcher/HttpDataFetcherImpl.cc \
	plugin.cc \
	serverIntercept.cc

esi_la_LIBADD = libesi.la
esi_la_LDFLAGS = -module -avoid-version -shared

docnode_test_SOURCES = test/docnode_test.cc test/print_funcs.cc
docnode_test_LDADD = libesi.la  @LIBDL@ -lz -lpthread
parser_test_SOURCES = test/parser_test.cc test/print_funcs.cc
parser_test_LDADD = libesi.la  @LIBDL@ -lz -lpthread
processor_test_SOURCES = test/processor_test.cc test/HandlerMap.cc test/StubIncludeHandler.cc test/print_funcs.cc
processor_test_LDADD = libtest.la  @LIBDL@ -lz -lpthread
utils_test_SOURCES = test/utils_test.cc test/print_funcs.cc
utils_test_LDADD = libesi.la  @LIBDL@ -lz -lpthread
vars_test_SOURCES = test/vars_test.cc test/print_funcs.cc
vars_test_LDADD = libesi.la @LIBDL@ -lz -lpthread

TESTS = $(check_PROGRAMS)

test:: $(TESTS)
	for f in $(TESTS) ; do ./$$f ; done
