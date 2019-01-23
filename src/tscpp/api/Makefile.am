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

lib_LTLIBRARIES = libtscppapi.la

libtscppapi_la_CPPFLAGS = $(AM_CPPFLAGS) -I $(abs_top_srcdir)/include

libtscppapi_la_LDFLAGS=-lz -lpthread -version-info @TS_LIBTOOL_VERSION@

libtscppapi_la_SOURCES = \
	AsyncHttpFetch.cc \
	AsyncTimer.cc \
	CaseInsensitiveStringComparator.cc \
	ClientRequest.cc \
	Continuation.cc \
	GlobalPlugin.cc \
	GzipDeflateTransformation.cc \
	GzipInflateTransformation.cc \
	Headers.cc \
	HttpMethod.cc \
	HttpVersion.cc \
	InterceptPlugin.cc \
	Logger.cc \
	Plugin.cc \
	RemapPlugin.cc \
	Request.cc \
	Response.cc \
	Stat.cc \
	Transaction.cc \
	TransactionPlugin.cc \
	TransformationPlugin.cc \
	Url.cc \
	utils.cc \
	utils_internal.cc

clang-tidy-local: $(DIST_SOURCES)
	$(CXX_Clang_Tidy)

check_PROGRAMS = test_tscppapi

TESTS = $(check_PROGRAMS)

test_tscppapi_CPPFLAGS = $(AM_CPPFLAGS)\
	-I$(abs_top_srcdir)/tests/include -Wall -Wextra -Werror $(AM_CXXFLAGS)

# test_tscppapi_LDADD =

test_tscppapi_SOURCES = \
	Continuation.cc \
	unit_tests/unit_test_main.cc \
	unit_tests/test_Continuation.cc
