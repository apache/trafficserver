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

AM_CXXFLAGS += -Wno-unused-variable
AM_CPPFLAGS += -I$(top_srcdir)/$(subdir)/src/include
AM_LDFLAGS = $(TS_PLUGIN_LDFLAGS)

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
