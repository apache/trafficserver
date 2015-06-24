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

plugins = \
  add-header.la \
  append-transform.la \
  basic-auth.la \
  blacklist-0.la \
  blacklist-1.la \
  bnull-transform.la \
  cache-scan.la \
  file-1.la \
  hello.la \
  intercept.la \
  lifecycle-plugin.la \
  null-transform.la \
  output-header.la \
  protocol.la \
  psi.la \
  query-remap.la \
  remap.la \
  replace-header.la \
  response-header-1.la \
  secure-link.la \
  server-transform.la \
  ssl-preaccept.la \
  remap_header_add.la \
  ssl-sni.la \
  ssl-sni-whitelist.la \
  thread-1.la \
  txn-data-sink.la \
  version.la

if BUILD_EXAMPLE_PLUGINS
pkglib_LTLIBRARIES = $(plugins)
else
noinst_LTLIBRARIES = $(plugins)
endif

add_header_la_SOURCES = add-header/add-header.c
append_transform_la_SOURCES = append-transform/append-transform.c
basic_auth_la_SOURCES = basic-auth/basic-auth.c
blacklist_0_la_SOURCES = blacklist-0/blacklist-0.c
blacklist_1_la_SOURCES = blacklist-1/blacklist-1.c
bnull_transform_la_SOURCES = bnull-transform/bnull-transform.c
cache_scan_la_SOURCES = cache-scan/cache-scan.cc
file_1_la_SOURCES = file-1/file-1.c
hello_la_SOURCES = hello/hello.c
intercept_la_SOURCES = intercept/intercept.cc
lifecycle_plugin_la_SOURCES = lifecycle-plugin/lifecycle-plugin.c
null_transform_la_SOURCES = null-transform/null-transform.c
output_header_la_SOURCES = output-header/output-header.c
protocol_la_SOURCES = protocol/Protocol.c protocol/TxnSM.c
query_remap_la_SOURCES = query-remap/query-remap.c
remap_la_SOURCES = remap/remap.cc
replace_header_la_SOURCES = replace-header/replace-header.c
remap_header_add_la_SOURCES = remap_header_add/remap_header_add.cc
response_header_1_la_SOURCES = response-header-1/response-header-1.c
server_transform_la_SOURCES = server-transform/server-transform.c
thread_1_la_SOURCES = thread-1/thread-1.c
txn_data_sink_la_SOURCES = txn-data-sink/txn-data-sink.c
psi_la_SOURCES = thread-pool/psi.c thread-pool/thread.c
version_la_SOURCES = version/version.c
secure_link_la_SOURCES = secure-link/secure-link.c
ssl_preaccept_la_SOURCES = ssl-preaccept/ssl-preaccept.cc 
ssl_sni_la_SOURCES = ssl-sni/ssl-sni.cc 
ssl_sni_whitelist_la_SOURCES = ssl-sni-whitelist/ssl-sni-whitelist.cc 

# The following examples do not build:
#
# redirect_1_la_SOURCES = redirect-1/redirect-1.c
# session_1_la_SOURCES = session-1/session-1.c
