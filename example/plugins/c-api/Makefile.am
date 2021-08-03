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

if BUILD_EXAMPLE_PLUGINS

example_Plugins = \
	add_header.la \
	append_transform.la \
	basic_auth.la \
	bnull_transform.la \
	cert_update.la \
	request_buffer.la \
	cache_scan.la \
	client_context_dump.la \
	denylist_0.la \
	denylist_1.la \
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
	query_remap.la \
	redirect_1.la \
	remap.la \
	remap_header_add.la \
	replace_header.la \
	response_header_1.la \
	secure_link.la \
	server_push.la \
	server_transform.la \
	session_hooks.la \
	ssl_preaccept.la \
	ssl_sni_allowlist.la \
	ssl_sni.la \
	statistic.la \
	thread_1.la \
	txn_data_sink.la \
	version.la \
	disable_http2.la \
	verify_cert.la \
	vconn_args.la

pkglib_LTLIBRARIES = $(example_Plugins)

endif

add_header_la_SOURCES = add_header/add_header.c
append_transform_la_SOURCES = append_transform/append_transform.c
basic_auth_la_SOURCES = basic_auth/basic_auth.c
denylist_0_la_SOURCES = denylist_0/denylist_0.c
denylist_1_la_SOURCES = denylist_1/denylist_1.c
bnull_transform_la_SOURCES = bnull_transform/bnull_transform.c
cert_update_la_SOURCES = cert_update/cert_update.cc
request_buffer_la_SOURCES = request_buffer/request_buffer.c
cache_scan_la_SOURCES = cache_scan/cache_scan.cc
client_context_dump_la_SOURCES = client_context_dump/client_context_dump.cc
file_1_la_SOURCES = file_1/file_1.c
hello_la_SOURCES = hello/hello.c
intercept_la_SOURCES = intercept/intercept.cc
lifecycle_plugin_la_SOURCES = lifecycle_plugin/lifecycle_plugin.c
null_transform_la_SOURCES = null_transform/null_transform.c
output_header_la_SOURCES = output_header/output_header.c
passthru_la_SOURCES = passthru/passthru.cc
protocol_la_SOURCES = protocol/Protocol.c protocol/TxnSM.c
protocol_stack_la_SOURCES = protocol_stack/protocol_stack.cc
psi_la_SOURCES = thread_pool/psi.c thread_pool/thread.c
query_remap_la_SOURCES = query_remap/query_remap.c
remap_header_add_la_SOURCES = remap_header_add/remap_header_add.cc
remap_la_SOURCES = remap/remap.cc
replace_header_la_SOURCES = replace_header/replace_header.c
response_header_1_la_SOURCES = response_header_1/response_header_1.c
secure_link_la_SOURCES = secure_link/secure_link.c
server_push_la_SOURCES = server_push/server_push.c
server_transform_la_SOURCES = server_transform/server_transform.c
ssl_preaccept_la_SOURCES = ssl_preaccept/ssl_preaccept.cc
ssl_sni_la_SOURCES = ssl_sni/ssl_sni.cc
ssl_sni_allowlist_la_SOURCES = ssl_sni_allowlist/ssl_sni_allowlist.cc
disable_http2_la_SOURCES = disable_http2/disable_http2.cc
verify_cert_la_SOURCES = verify_cert/verify_cert.cc
statistic_la_SOURCES = statistic/statistic.cc
thread_1_la_SOURCES = thread_1/thread_1.c
txn_data_sink_la_SOURCES = txn_data_sink/txn_data_sink.cc
version_la_SOURCES = version/version.c
redirect_1_la_SOURCES = redirect_1/redirect_1.c
session_hooks_la_SOURCES = session_hooks/session_hooks.c
vconn_args_la_SOURCES = vconn_args/vconn_args.cc

clang-tidy-local: $(DIST_SOURCES)
	$(CXX_Clang_Tidy)
	$(CC_Clang_Tidy)
