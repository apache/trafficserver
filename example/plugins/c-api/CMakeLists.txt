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

add_atsplugin(cache_scan ./cache_scan/cache_scan.cc)
add_atsplugin(lifecycle_plugin ./lifecycle_plugin/lifecycle_plugin.cc)
add_atsplugin(request_buffer ./request_buffer/request_buffer.cc)
add_atsplugin(secure_link ./secure_link/secure_link.cc)
target_link_libraries(secure_link PRIVATE OpenSSL::SSL)
add_atsplugin(remap ./remap/remap.cc)
add_atsplugin(redirect_1 ./redirect_1/redirect_1.cc)
add_atsplugin(query_remap ./query_remap/query_remap.cc)
add_atsplugin(thread_pool ./thread_pool/psi.cc ./thread_pool/thread.cc)
add_atsplugin(bnull_transform ./bnull_transform/bnull_transform.cc)
add_atsplugin(replace_header ./replace_header/replace_header.cc)
add_atsplugin(ssl_sni ./ssl_sni/ssl_sni.cc)
target_link_libraries(ssl_sni PRIVATE OpenSSL::SSL)
add_atsplugin(passthru ./passthru/passthru.cc)
add_atsplugin(response_header_1 ./response_header_1/response_header_1.cc)
add_atsplugin(denylist_1 ./denylist_1/denylist_1.cc)
add_atsplugin(denylist_0 ./denylist_0/denylist_0.cc)
add_atsplugin(server_push ./server_push/server_push.cc)
add_atsplugin(basic_auth ./basic_auth/basic_auth.cc)
add_atsplugin(verify_cert ./verify_cert/verify_cert.cc)
target_link_libraries(verify_cert PRIVATE OpenSSL::SSL)
add_atsplugin(intercept ./intercept/intercept.cc)
add_atsplugin(null_transform ./null_transform/null_transform.cc)
add_atsplugin(ssl_sni_allowlist ./ssl_sni_allowlist/ssl_sni_allowlist.cc)
target_link_libraries(ssl_sni_allowlist PRIVATE OpenSSL::SSL)
add_atsplugin(protocol ./protocol/Protocol.cc ./protocol/TxnSM.cc)
add_atsplugin(file_1 ./file_1/file_1.cc)
add_atsplugin(output_header ./output_header/output_header.cc)
add_atsplugin(session_hooks ./session_hooks/session_hooks.cc)
add_atsplugin(add_header ./add_header/add_header.cc)
add_atsplugin(version ./version/version.cc)
add_atsplugin(ssl_preaccept ./ssl_preaccept/ssl_preaccept.cc)
target_link_libraries(ssl_preaccept PRIVATE libswoc)
add_atsplugin(txn_data_sink ./txn_data_sink/txn_data_sink.cc)
add_atsplugin(remap_header_add ./remap_header_add/remap_header_add.cc)
add_atsplugin(disable_http2 ./disable_http2/disable_http2.cc)
target_link_libraries(disable_http2 PRIVATE OpenSSL::SSL)
add_atsplugin(server_transform ./server_transform/server_transform.cc)
add_atsplugin(append_transform ./append_transform/append_transform.cc)
add_atsplugin(thread_1 ./thread_1/thread_1.cc)
add_atsplugin(vconn_args ./vconn_args/vconn_args.cc)
add_atsplugin(cert_update ./cert_update/cert_update.cc)
target_link_libraries(cert_update PRIVATE libswoc)
add_atsplugin(hello ./hello/hello.cc)
add_atsplugin(statistic ./statistic/statistic.cc)
add_atsplugin(protocol_stack ./protocol_stack/protocol_stack.cc)
add_atsplugin(client_context_dump ./client_context_dump/client_context_dump.cc)
target_link_libraries(client_context_dump PRIVATE OpenSSL::SSL libswoc)
