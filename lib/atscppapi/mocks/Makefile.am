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

AM_CPPFLAGS = \
  -I$(top_srcdir)/$(subdir)/ \
  -I$(top_builddir)/proxy/api \
  -I$(top_srcdir)/proxy/api \
  -I$(top_srcdir)/lib \
  -I/home/y/include

CXXFLAGS = \
  -g -pipe -Wall -O3 -feliminate-unused-debug-symbols -fno-strict-aliasing -Wno-invalid-offsetof -mcx16

# build the library
lib_LTLIBRARIES = libatscppapi_mock.la
libatscppapi_mock_la_LDFLAGS= $(TS_PLUGIN_LDFLAGS) $(LIB_GMOCK)

libatscppapi_mock_la_SOURCES = MockAtscppapi.cc


library_includedir=$(includedir)/atscppapi
base_include_folder = $(top_srcdir)/$(subdir)/atscppapi

library_include_HEADERS = $(base_include_folder)/GlobalPlugin.h \
			  $(base_include_folder)/Plugin.h \
			  $(base_include_folder)/PluginInit.h \
			  $(base_include_folder)/Transaction.h \
			  $(base_include_folder)/TransactionPlugin.h \
			  $(base_include_folder)/HttpMethod.h \
			  $(base_include_folder)/HttpStatus.h \
			  $(base_include_folder)/HttpVersion.h \
			  $(base_include_folder)/Headers.h \
			  $(base_include_folder)/Request.h \
			  $(base_include_folder)/CaseInsensitiveStringComparator.h \
			  $(base_include_folder)/ClientRequest.h \
	 		  $(base_include_folder)/Url.h \
		 	  $(base_include_folder)/Response.h \
			  $(base_include_folder)/utils.h \
			  $(base_include_folder)/TransformationPlugin.h \
			  $(base_include_folder)/Logger.h \
			  $(base_include_folder)/noncopyable.h \
			  $(base_include_folder)/Stat.h \
			  $(base_include_folder)/Mutex.h \
			  $(base_include_folder)/RemapPlugin.h \
			  $(base_include_folder)/shared_ptr.h \
			  $(base_include_folder)/Async.h \
			  $(base_include_folder)/AsyncHttpFetch.h \
			  $(base_include_folder)/GzipDeflateTransformation.h \
			  $(base_include_folder)/GzipInflateTransformation.h \
			  $(base_include_folder)/AsyncTimer.h \
			  $(base_include_folder)/InterceptPlugin.h
