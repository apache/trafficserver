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

include $(top_srcdir)/build/cppapi-examples.mk

AM_LDFLAGS = $(TS_PLUGIN_LDFLAGS)

plugins = \
  async_http_fetch.la \
  async_http_fetch_streaming.la \
  async_timer.la \
  boom.la \
  clientredirect.la \
  clientrequest.la \
  customresponse.la \
  globalhook.la \
  gzip_transformation.la \
  helloworld.la \
  intercept.la \
  internal_transaction_handling.la \
  logger_example.la \
  multiple_transaction_hooks.la \
  null_transformation_plugin.la \
  post_buffer.la \
  remap_plugin.la \
  serverresponse.la \
  stat_example.la \
  timeout_example.la \
  transactionhook.la

if BUILD_EXAMPLE_PLUGINS
pkglib_LTLIBRARIES = $(plugins)
else
noinst_LTLIBRARIES = $(plugins)
endif

async_http_fetch_la_SOURCES = async_http_fetch/AsyncHttpFetch.cc
async_http_fetch_streaming_la_SOURCES = async_http_fetch_streaming/AsyncHttpFetchStreaming.cc
async_timer_la_SOURCES = async_timer/AsyncTimer.cc
boom_la_SOURCES = boom/boom.cc
clientredirect_la_SOURCES = clientredirect/ClientRedirect.cc
clientrequest_la_SOURCES = clientrequest/ClientRequest.cc
customresponse_la_SOURCES = customresponse/CustomResponse.cc
globalhook_la_SOURCES = globalhook/GlobalHookPlugin.cc
gzip_transformation_la_SOURCES = gzip_transformation/GzipTransformationPlugin.cc
helloworld_la_SOURCES = helloworld/HelloWorldPlugin.cc
intercept_la_SOURCES = intercept/intercept.cc
internal_transaction_handling_la_SOURCES = internal_transaction_handling/InternalTransactionHandling.cc
logger_example_la_SOURCES = logger_example/LoggerExample.cc
multiple_transaction_hooks_la_SOURCES = multiple_transaction_hooks/MultipleTransactionHookPlugins.cc
null_transformation_plugin_la_SOURCES = null_transformation_plugin/NullTransformationPlugin.cc
post_buffer_la_SOURCES = post_buffer/PostBuffer.cc
remap_plugin_la_SOURCES = remap_plugin/RemapPlugin.cc
serverresponse_la_SOURCES = serverresponse/ServerResponse.cc
stat_example_la_SOURCES = stat_example/StatExample.cc
timeout_example_la_SOURCES = timeout_example/TimeoutExamplePlugin.cc
transactionhook_la_SOURCES = transactionhook/TransactionHookPlugin.cc
