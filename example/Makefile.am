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

AM_CXXFLAGS += -Wno-unused-variable
AM_LDFLAGS = $(TS_PLUGIN_LD_FLAGS)

libatscppai = $(top_builddir)/lib/cppapi/libatscppapi.la

example_Plugins = \
	add_header.la \
	append_transform.la \
	basic_auth.la \
	blacklist_0.la \
	blacklist_1.la \
	bnull_transform.la \
	cache_scan.la \
	file_1.la \
	hello.la \
	intercept.la \
	lifecycle_plugin.la \
	null_transform.la \
	output_header.la \
	passthru.la \
	protocol_stack.la \
	protocol.la \
	psi.la \
	query-remap.la \
	remap.la \
	remap_header_add.la \
	replace-header.la \
	response-header-1.la \
	secure-link.la \
	server-push.la \
	server-transform.la \
	ssl-preaccept.la \
	ssl-sni-whitelist.la \
	ssl-sni.la \
	statistic.la \
	thread-1.la \
	txn-data-sink.la \
	version.la

example_Plugins += \
	cppapi/AsyncHttpFetch.la \
	cppapi/AsyncHttpFetchStreaming.la \
	cppapi/AsyncTimer.la \
	cppapi/ClientRedirect.la \
	cppapi/ClientRequest.la \
	cppapi/CustomErrorRemapPlugin.la \
	cppapi/CustomResponse.la \
	cppapi/GlobalHookPlugin.la \
	cppapi/GzipTransformationPlugin.la \
	cppapi/HelloWorldPlugin.la \
	cppapi/InternalTransactionHandling.la \
	cppapi/LoggerExample.la \
	cppapi/MultipleTransactionHookPlugins.la \
	cppapi/NullTransformationPlugin.la \
	cppapi/PostBuffer.la \
	cppapi/RemapPlugin.la \
	cppapi/ServerResponse.la \
	cppapi/StatExample.la \
	cppapi/TimeoutExamplePlugin.la \
	cppapi/TransactionHookPlugin.la \
	cppapi/WebSocket.la \
	cppapi/boom.la \
	cppapi/intercept.la

if BUILD_EXAMPLE_PLUGINS
pkglib_LTLIBRARIES = $(example_Plugins)
else
noinst_LTLIBRARIES = $(example_Plugins)
endif

add_header_la_SOURCES = add_header/add_header.c
append_transform_la_SOURCES = append_transform/append_transform.c
basic_auth_la_SOURCES = basic_auth/basic_auth.c
blacklist_0_la_SOURCES = blacklist_0/blacklist_0.c
blacklist_1_la_SOURCES = blacklist_1/blacklist_1.c
bnull_transform_la_SOURCES = bnull_transform/bnull_transform.c
cache_scan_la_SOURCES = cache_scan/cache_scan.cc
file_1_la_SOURCES = file_1/file_1.c
hello_la_SOURCES = hello/hello.c
intercept_la_SOURCES = intercept/intercept.cc
lifecycle_plugin_la_SOURCES = lifecycle_plugin/lifecycle_plugin.c
null_transform_la_SOURCES = null_transform/null_transform.c
output_header_la_SOURCES = output_header/output_header.c
passthru_la_SOURCES = passthru/passthru.cc
protocol_la_SOURCES = protocol/Protocol.c protocol/TxnSM.c
protocol_stack_la_SOURCES = protocol_stack/protocol_stack.cc
psi_la_SOURCES = thread-pool/psi.c thread-pool/thread.c
query_remap_la_SOURCES = query-remap/query-remap.c
remap_header_add_la_SOURCES = remap_header_add/remap_header_add.cc
remap_la_SOURCES = remap/remap.cc
replace_header_la_SOURCES = replace-header/replace-header.c
response_header_1_la_SOURCES = response-header-1/response-header-1.c
secure_link_la_SOURCES = secure-link/secure-link.c
server_push_la_SOURCES = server-push/server-push.c
server_transform_la_SOURCES = server-transform/server-transform.c
ssl_preaccept_la_SOURCES = ssl-preaccept/ssl-preaccept.cc
ssl_sni_la_SOURCES = ssl-sni/ssl-sni.cc
ssl_sni_whitelist_la_SOURCES = ssl-sni-whitelist/ssl-sni-whitelist.cc
statistic_la_SOURCES = statistic/statistic.cc
thread_1_la_SOURCES = thread-1/thread-1.c
txn_data_sink_la_SOURCES = txn-data-sink/txn-data-sink.c
version_la_SOURCES = version/version.c

# The following examples do not build:
#
# redirect_1_la_SOURCES = redirect-1/redirect-1.c
# session_1_la_SOURCES = session-1/session-1.c

cppapi_AsyncHttpFetchStreaming_la_SOURCES = cppapi/async_http_fetch_streaming/AsyncHttpFetchStreaming.cc
cppapi_AsyncHttpFetch_la_SOURCES = cppapi/async_http_fetch/AsyncHttpFetch.cc
cppapi_AsyncTimer_la_SOURCES = cppapi/async_timer/AsyncTimer.cc
cppapi_ClientRedirect_la_SOURCES = cppapi/clientredirect/ClientRedirect.cc
cppapi_ClientRequest_la_SOURCES = cppapi/clientrequest/ClientRequest.cc
cppapi_CustomErrorRemapPlugin_la_SOURCES = cppapi/custom_error_remap_plugin/CustomErrorRemapPlugin.cc
cppapi_CustomResponse_la_SOURCES = cppapi/customresponse/CustomResponse.cc
cppapi_GlobalHookPlugin_la_SOURCES = cppapi/globalhook/GlobalHookPlugin.cc
cppapi_GzipTransformationPlugin_la_SOURCES = cppapi/gzip_transformation/GzipTransformationPlugin.cc
cppapi_HelloWorldPlugin_la_SOURCES = cppapi/helloworld/HelloWorldPlugin.cc
cppapi_InternalTransactionHandling_la_SOURCES = cppapi/internal_transaction_handling/InternalTransactionHandling.cc
cppapi_LoggerExample_la_SOURCES = cppapi/logger_example/LoggerExample.cc
cppapi_MultipleTransactionHookPlugins_la_SOURCES = cppapi/multiple_transaction_hooks/MultipleTransactionHookPlugins.cc
cppapi_NullTransformationPlugin_la_SOURCES = cppapi/null_transformation_plugin/NullTransformationPlugin.cc
cppapi_PostBuffer_la_SOURCES = cppapi/post_buffer/PostBuffer.cc
cppapi_RemapPlugin_la_SOURCES = cppapi/remap_plugin/RemapPlugin.cc
cppapi_ServerResponse_la_SOURCES = cppapi/serverresponse/ServerResponse.cc
cppapi_StatExample_la_SOURCES = cppapi/stat_example/StatExample.cc
cppapi_TimeoutExamplePlugin_la_SOURCES = cppapi/timeout_example/TimeoutExamplePlugin.cc
cppapi_TransactionHookPlugin_la_SOURCES = cppapi/transactionhook/TransactionHookPlugin.cc
cppapi_WebSocket_la_SOURCES = cppapi/websocket/WebSocket.cc cppapi/websocket/WSBuffer.cc
cppapi_boom_la_SOURCES = cppapi/boom/boom.cc
cppapi_intercept_la_SOURCES = cppapi/intercept/intercept.cc

cppapi_AsyncHttpFetchStreaming_la_LIBADD = $(libatscppai)
cppapi_AsyncHttpFetch_la_LIBADD = $(libatscppai)
cppapi_AsyncTimer_la_LIBADD = $(libatscppai)
cppapi_ClientRedirect_la_LIBADD = $(libatscppai)
cppapi_ClientRequest_la_LIBADD = $(libatscppai)
cppapi_CustomErrorRemapPlugin_la_LIBADD = $(libatscppai)
cppapi_CustomResponse_la_LIBADD = $(libatscppai)
cppapi_GlobalHookPlugin_la_LIBADD = $(libatscppai)
cppapi_GzipTransformationPlugin_la_LIBADD = $(libatscppai)
cppapi_HelloWorldPlugin_la_LIBADD = $(libatscppai)
cppapi_InternalTransactionHandling_la_LIBADD = $(libatscppai)
cppapi_LoggerExample_la_LIBADD = $(libatscppai)
cppapi_MultipleTransactionHookPlugins_la_LIBADD = $(libatscppai)
cppapi_NullTransformationPlugin_la_LIBADD = $(libatscppai)
cppapi_PostBuffer_la_LIBADD = $(libatscppai)
cppapi_RemapPlugin_la_LIBADD = $(libatscppai)
cppapi_ServerResponse_la_LIBADD = $(libatscppai)
cppapi_StatExample_la_LIBADD = $(libatscppai)
cppapi_TimeoutExamplePlugin_la_LIBADD = $(libatscppai)
cppapi_TransactionHookPlugin_la_LIBADD = $(libatscppai)
cppapi_WebSocket_la_LIBADD = $(libatscppai)
cppapi_boom_la_LIBADD = $(libatscppai)
cppapi_intercept_la_LIBADD = $(libatscppai)

tidy-local: $(DIST_SOURCES)
	$(CXX_Clang_Tidy)
	$(CC_Clang_Tidy)
