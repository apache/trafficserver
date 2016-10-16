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

AM_LDFLAGS = $(TS_PLUGIN_LDFLAGS)

example_PLUGINS = \
  example/add-header.la \
  example/append-transform.la \
  example/basic-auth.la \
  example/blacklist-0.la \
  example/blacklist-1.la \
  example/bnull-transform.la \
  example/cache-scan.la \
  example/file-1.la \
  example/hello.la \
  example/intercept.la \
  example/lifecycle-plugin.la \
  example/null-transform.la \
  example/output-header.la \
  example/passthru.la \
  example/protocol-stack.la \
  example/protocol.la \
  example/psi.la \
  example/query-remap.la \
  example/remap.la \
  example/remap_header_add.la \
  example/replace-header.la \
  example/response-header-1.la \
  example/secure-link.la \
  example/server-push.la \
  example/server-transform.la \
  example/ssl-preaccept.la \
  example/ssl-sni-whitelist.la \
  example/ssl-sni.la \
  example/statistic.la \
  example/thread-1.la \
  example/txn-data-sink.la \
  example/version.la

if BUILD_EXAMPLE_PLUGINS
pkglib_LTLIBRARIES += $(example_PLUGINS)
else
noinst_LTLIBRARIES += $(example_PLUGINS)
endif

example_add_header_la_LDFLAGS = $(TS_PLUGIN_LDFLAGS)
example_append_transform_la_LDFLAGS = $(TS_PLUGIN_LDFLAGS)
example_basic_auth_la_LDFLAGS = $(TS_PLUGIN_LDFLAGS)
example_blacklist_0_la_LDFLAGS = $(TS_PLUGIN_LDFLAGS)
example_blacklist_1_la_LDFLAGS = $(TS_PLUGIN_LDFLAGS)
example_bnull_transform_la_LDFLAGS = $(TS_PLUGIN_LDFLAGS)
example_cache_scan_la_LDFLAGS = $(TS_PLUGIN_LDFLAGS)
example_file_1_la_LDFLAGS = $(TS_PLUGIN_LDFLAGS)
example_hello_la_LDFLAGS = $(TS_PLUGIN_LDFLAGS)
example_intercept_la_LDFLAGS = $(TS_PLUGIN_LDFLAGS)
example_lifecycle_plugin_la_LDFLAGS = $(TS_PLUGIN_LDFLAGS)
example_null_transform_la_LDFLAGS = $(TS_PLUGIN_LDFLAGS)
example_output_header_la_LDFLAGS = $(TS_PLUGIN_LDFLAGS)
example_passthru_la_LDFLAGS = $(TS_PLUGIN_LDFLAGS)
example_protocol_la_LDFLAGS = $(TS_PLUGIN_LDFLAGS)
example_protocol_stack_la_LDFLAGS = $(TS_PLUGIN_LDFLAGS)
example_psi_la_LDFLAGS = $(TS_PLUGIN_LDFLAGS)
example_query_remap_la_LDFLAGS = $(TS_PLUGIN_LDFLAGS)
example_remap_header_add_la_LDFLAGS = $(TS_PLUGIN_LDFLAGS)
example_remap_la_LDFLAGS = $(TS_PLUGIN_LDFLAGS)
example_replace_header_la_LDFLAGS = $(TS_PLUGIN_LDFLAGS)
example_response_header_1_la_LDFLAGS = $(TS_PLUGIN_LDFLAGS)
example_secure_link_la_LDFLAGS = $(TS_PLUGIN_LDFLAGS)
example_server_push_la_LDFLAGS = $(TS_PLUGIN_LDFLAGS)
example_server_transform_la_LDFLAGS = $(TS_PLUGIN_LDFLAGS)
example_ssl_preaccept_la_LDFLAGS = $(TS_PLUGIN_LDFLAGS)
example_ssl_sni_la_LDFLAGS = $(TS_PLUGIN_LDFLAGS)
example_ssl_sni_whitelist_la_LDFLAGS = $(TS_PLUGIN_LDFLAGS)
example_statistic_la_LDFLAGS = $(TS_PLUGIN_LDFLAGS)
example_thread_1_la_LDFLAGS = $(TS_PLUGIN_LDFLAGS)
example_txn_data_sink_la_LDFLAGS = $(TS_PLUGIN_LDFLAGS)
example_version_la_LDFLAGS = $(TS_PLUGIN_LDFLAGS)

example_add_header_la_CPPFLAGS = $(TS_PLUGIN_CPPFLAGS)
example_append_transform_la_CPPFLAGS = $(TS_PLUGIN_CPPFLAGS)
example_basic_auth_la_CPPFLAGS = $(TS_PLUGIN_CPPFLAGS)
example_blacklist_0_la_CPPFLAGS = $(TS_PLUGIN_CPPFLAGS)
example_blacklist_1_la_CPPFLAGS = $(TS_PLUGIN_CPPFLAGS)
example_bnull_transform_la_CPPFLAGS = $(TS_PLUGIN_CPPFLAGS)
example_cache_scan_la_CPPFLAGS = $(TS_PLUGIN_CPPFLAGS)
example_file_1_la_CPPFLAGS = $(TS_PLUGIN_CPPFLAGS)
example_hello_la_CPPFLAGS = $(TS_PLUGIN_CPPFLAGS)
example_intercept_la_CPPFLAGS = $(TS_PLUGIN_CPPFLAGS)
example_lifecycle_plugin_la_CPPFLAGS = $(TS_PLUGIN_CPPFLAGS)
example_null_transform_la_CPPFLAGS = $(TS_PLUGIN_CPPFLAGS)
example_output_header_la_CPPFLAGS = $(TS_PLUGIN_CPPFLAGS)
example_passthru_la_CPPFLAGS = $(TS_PLUGIN_CPPFLAGS)
example_protocol_la_CPPFLAGS = $(TS_PLUGIN_CPPFLAGS)
example_protocol_stack_la_CPPFLAGS = $(TS_PLUGIN_CPPFLAGS)
example_psi_la_CPPFLAGS = $(TS_PLUGIN_CPPFLAGS)
example_query_remap_la_CPPFLAGS = $(TS_PLUGIN_CPPFLAGS)
example_remap_header_add_la_CPPFLAGS = $(TS_PLUGIN_CPPFLAGS)
example_remap_la_CPPFLAGS = $(TS_PLUGIN_CPPFLAGS)
example_replace_header_la_CPPFLAGS = $(TS_PLUGIN_CPPFLAGS)
example_response_header_1_la_CPPFLAGS = $(TS_PLUGIN_CPPFLAGS)
example_secure_link_la_CPPFLAGS = $(TS_PLUGIN_CPPFLAGS)
example_server_push_la_CPPFLAGS = $(TS_PLUGIN_CPPFLAGS)
example_server_transform_la_CPPFLAGS = $(TS_PLUGIN_CPPFLAGS)
example_ssl_preaccept_la_CPPFLAGS = $(TS_PLUGIN_CPPFLAGS)
example_ssl_sni_la_CPPFLAGS = $(TS_PLUGIN_CPPFLAGS)
example_ssl_sni_whitelist_la_CPPFLAGS = $(TS_PLUGIN_CPPFLAGS)
example_statistic_la_CPPFLAGS = $(TS_PLUGIN_CPPFLAGS)
example_thread_1_la_CPPFLAGS = $(TS_PLUGIN_CPPFLAGS)
example_txn_data_sink_la_CPPFLAGS = $(TS_PLUGIN_CPPFLAGS)
example_version_la_CPPFLAGS = $(TS_PLUGIN_CPPFLAGS)

example_add_header_la_SOURCES = example/add-header/add-header.c
example_append_transform_la_SOURCES = example/append-transform/append-transform.c
example_basic_auth_la_SOURCES = example/basic-auth/basic-auth.c
example_blacklist_0_la_SOURCES = example/blacklist-0/blacklist-0.c
example_blacklist_1_la_SOURCES = example/blacklist-1/blacklist-1.c
example_bnull_transform_la_SOURCES = example/bnull-transform/bnull-transform.c
example_cache_scan_la_SOURCES = example/cache-scan/cache-scan.cc
example_file_1_la_SOURCES = example/file-1/file-1.c
example_hello_la_SOURCES = example/hello/hello.c
example_intercept_la_SOURCES = example/intercept/intercept.cc
example_lifecycle_plugin_la_SOURCES = example/lifecycle-plugin/lifecycle-plugin.c
example_null_transform_la_SOURCES = example/null-transform/null-transform.c
example_output_header_la_SOURCES = example/output-header/output-header.c
example_passthru_la_SOURCES = example/passthru/passthru.cc
example_protocol_la_SOURCES = example/protocol/Protocol.c example/protocol/TxnSM.c
example_protocol_stack_la_SOURCES = example/protocol-stack/protocol-stack.cc
example_psi_la_SOURCES = example/thread-pool/psi.c example/thread-pool/thread.c
example_query_remap_la_SOURCES = example/query-remap/query-remap.c
example_remap_header_add_la_SOURCES = example/remap_header_add/remap_header_add.cc
example_remap_la_SOURCES = example/remap/remap.cc
example_replace_header_la_SOURCES = example/replace-header/replace-header.c
example_response_header_1_la_SOURCES = example/response-header-1/response-header-1.c
example_secure_link_la_SOURCES = example/secure-link/secure-link.c
example_server_push_la_SOURCES = example/server-push/server-push.c
example_server_transform_la_SOURCES = example/server-transform/server-transform.c
example_ssl_preaccept_la_SOURCES = example/ssl-preaccept/ssl-preaccept.cc
example_ssl_sni_la_SOURCES = example/ssl-sni/ssl-sni.cc
example_ssl_sni_whitelist_la_SOURCES = example/ssl-sni-whitelist/ssl-sni-whitelist.cc
example_statistic_la_SOURCES = example/statistic/statistic.cc
example_thread_1_la_SOURCES = example/thread-1/thread-1.c
example_txn_data_sink_la_SOURCES = example/txn-data-sink/txn-data-sink.c
example_version_la_SOURCES = example/version/version.c

# The following example do not build:
#
# redirect_1_la_SOURCES = redirect-1/redirect-1.c
# session_1_la_SOURCES = session-1/session-1.c

# vim: ft=make sw=8 ts=4 noet:
