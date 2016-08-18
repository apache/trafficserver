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

include $(top_srcdir)/build/plugins.mk
include $(top_srcdir)/build/tidy.mk

plugins = \
	AsyncHttpFetch.la \
	AsyncHttpFetchStreaming.la \
	AsyncTimer.la \
	ClientRedirect.la \
	ClientRequest.la \
	CustomErrorRemapPlugin.la \
	CustomResponse.la \
	GlobalHookPlugin.la \
	GzipTransformationPlugin.la \
	HelloWorldPlugin.la \
	InternalTransactionHandling.la \
	LoggerExample.la \
	MultipleTransactionHookPlugins.la \
	NullTransformationPlugin.la \
	PostBuffer.la \
	RemapPlugin.la \
	ServerResponse.la \
	StatExample.la \
	TimeoutExamplePlugin.la \
	TransactionHookPlugin.la \
	boom.la \
	intercept.la

if BUILD_EXAMPLE_PLUGINS
pkglib_LTLIBRARIES = $(plugins)
else
noinst_LTLIBRARIES = $(plugins)
endif

AM_CXXFLAGS += -Wno-unused-variable
AM_CPPFLAGS += -I$(top_srcdir)/$(subdir)/src/include
AM_LDFLAGS = $(TS_PLUGIN_LDFLAGS)

libatscppai = $(top_builddir)/lib/atscppapi/src/libatscppapi.la

AsyncHttpFetchStreaming_la_SOURCES = async_http_fetch_streaming/AsyncHttpFetchStreaming.cc
AsyncHttpFetch_la_SOURCES = async_http_fetch/AsyncHttpFetch.cc
AsyncTimer_la_SOURCES = async_timer/AsyncTimer.cc
ClientRedirect_la_SOURCES = clientredirect/ClientRedirect.cc
ClientRequest_la_SOURCES = clientrequest/ClientRequest.cc
CustomErrorRemapPlugin_la_SOURCES = custom_error_remap_plugin/CustomErrorRemapPlugin.cc
CustomResponse_la_SOURCES = customresponse/CustomResponse.cc
GlobalHookPlugin_la_SOURCES = globalhook/GlobalHookPlugin.cc
GzipTransformationPlugin_la_SOURCES = gzip_transformation/GzipTransformationPlugin.cc
HelloWorldPlugin_la_SOURCES = helloworld/HelloWorldPlugin.cc
InternalTransactionHandling_la_SOURCES = internal_transaction_handling/InternalTransactionHandling.cc
LoggerExample_la_SOURCES = logger_example/LoggerExample.cc
MultipleTransactionHookPlugins_la_SOURCES = multiple_transaction_hooks/MultipleTransactionHookPlugins.cc
NullTransformationPlugin_la_SOURCES = null_transformation_plugin/NullTransformationPlugin.cc
PostBuffer_la_SOURCES = post_buffer/PostBuffer.cc
RemapPlugin_la_SOURCES = remap_plugin/RemapPlugin.cc
ServerResponse_la_SOURCES = serverresponse/ServerResponse.cc
StatExample_la_SOURCES = stat_example/StatExample.cc
TimeoutExamplePlugin_la_SOURCES = timeout_example/TimeoutExamplePlugin.cc
TransactionHookPlugin_la_SOURCES = transactionhook/TransactionHookPlugin.cc
boom_la_SOURCES = boom/boom.cc
intercept_la_SOURCES = intercept/intercept.cc

AsyncHttpFetchStreaming_la_LIBADD = $(libatscppai)
AsyncHttpFetch_la_LIBADD = $(libatscppai)
AsyncTimer_la_LIBADD = $(libatscppai)
ClientRedirect_la_LIBADD = $(libatscppai)
ClientRequest_la_LIBADD = $(libatscppai)
CustomErrorRemapPlugin_la_LIBADD = $(libatscppai)
CustomResponse_la_LIBADD = $(libatscppai)
GlobalHookPlugin_la_LIBADD = $(libatscppai)
GzipTransformationPlugin_la_LIBADD = $(libatscppai)
HelloWorldPlugin_la_LIBADD = $(libatscppai)
InternalTransactionHandling_la_LIBADD = $(libatscppai)
LoggerExample_la_LIBADD = $(libatscppai)
MultipleTransactionHookPlugins_la_LIBADD = $(libatscppai)
NullTransformationPlugin_la_LIBADD = $(libatscppai)
PostBuffer_la_LIBADD = $(libatscppai)
RemapPlugin_la_LIBADD = $(libatscppai)
ServerResponse_la_LIBADD = $(libatscppai)
StatExample_la_LIBADD = $(libatscppai)
TimeoutExamplePlugin_la_LIBADD = $(libatscppai)
TransactionHookPlugin_la_LIBADD = $(libatscppai)
boom_la_LIBADD = $(libatscppai)
intercept_la_LIBADD = $(libatscppai)

tidy-local: $(DIST_SOURCES)
	$(CXX_Clang_Tidy)
