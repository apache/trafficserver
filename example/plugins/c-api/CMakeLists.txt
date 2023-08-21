#######################
#
#  Licensed to the Apache Software Foundation (ASF) under one or more contributor license
#  agreements.  See the NOTICE file distributed with this work for additional information regarding
#  copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
#  (the "License"); you may not use this file except in compliance with the License.  You may obtain
#  a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software distributed under the License
#  is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
#  or implied. See the License for the specific language governing permissions and limitations under
#  the License.
#
#######################


include(add_atsplugin)

add_atsplugin(example_cache_scan ./cache_scan/cache_scan.cc)
add_atsplugin(example_lifecycle_plugin ./lifecycle_plugin/lifecycle_plugin.cc)
add_atsplugin(example_request_buffer ./request_buffer/request_buffer.cc)
add_atsplugin(example_secure_link ./secure_link/secure_link.cc)
target_link_libraries(example_secure_link PRIVATE OpenSSL::SSL)
add_atsplugin(example_remap ./remap/remap.cc)
add_atsplugin(example_redirect_1 ./redirect_1/redirect_1.cc)
add_atsplugin(example_query_remap ./query_remap/query_remap.cc)
add_atsplugin(example_thread_pool ./thread_pool/psi.cc ./thread_pool/thread.cc)
add_atsplugin(example_bnull_transform ./bnull_transform/bnull_transform.cc)
add_atsplugin(example_replace_header ./replace_header/replace_header.cc)
add_atsplugin(example_ssl_sni ./ssl_sni/ssl_sni.cc)
target_link_libraries(example_ssl_sni PRIVATE OpenSSL::SSL)
add_atsplugin(example_passthru ./passthru/passthru.cc)
add_atsplugin(example_response_header_1 ./response_header_1/response_header_1.cc)
add_atsplugin(example_denylist_1 ./denylist_1/denylist_1.cc)
add_atsplugin(example_denylist_0 ./denylist_0/denylist_0.cc)
add_atsplugin(example_server_push ./server_push/server_push.cc)
add_atsplugin(example_basic_auth ./basic_auth/basic_auth.cc)
add_atsplugin(example_verify_cert ./verify_cert/verify_cert.cc)
target_link_libraries(example_verify_cert PRIVATE OpenSSL::SSL)
add_atsplugin(example_intercept ./intercept/intercept.cc)
add_atsplugin(example_null_transform ./null_transform/null_transform.cc)
add_atsplugin(example_ssl_sni_allowlist ./ssl_sni_allowlist/ssl_sni_allowlist.cc)
target_link_libraries(example_ssl_sni_allowlist PRIVATE OpenSSL::SSL)
add_atsplugin(example_protocol ./protocol/Protocol.cc ./protocol/TxnSM.cc)
add_atsplugin(example_file_1 ./file_1/file_1.cc)
add_atsplugin(example_output_header ./output_header/output_header.cc)
add_atsplugin(example_session_hooks ./session_hooks/session_hooks.cc)
add_atsplugin(example_add_header ./add_header/add_header.cc)
add_atsplugin(example_version ./version/version.cc)
add_atsplugin(example_ssl_preaccept ./ssl_preaccept/ssl_preaccept.cc)
add_atsplugin(example_txn_data_sink ./txn_data_sink/txn_data_sink.cc)
add_atsplugin(example_remap_header_add ./remap_header_add/remap_header_add.cc)
add_atsplugin(example_disable_http2 ./disable_http2/disable_http2.cc)
target_link_libraries(example_disable_http2 PRIVATE OpenSSL::SSL)
add_atsplugin(example_server_transform ./server_transform/server_transform.cc)
add_atsplugin(example_append_transform ./append_transform/append_transform.cc)
add_atsplugin(example_thread_1 ./thread_1/thread_1.cc)
add_atsplugin(example_vconn_args ./vconn_args/vconn_args.cc)
add_atsplugin(example_cert_update ./cert_update/cert_update.cc)
add_atsplugin(example_hello ./hello/hello.cc)
add_atsplugin(example_statistic ./statistic/statistic.cc)
add_atsplugin(example_protocol_stack ./protocol_stack/protocol_stack.cc)
add_atsplugin(example_client_context_dump ./client_context_dump/client_context_dump.cc)
target_link_libraries(example_client_context_dump PRIVATE OpenSSL::SSL)
