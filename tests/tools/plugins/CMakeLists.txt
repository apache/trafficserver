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

add_autest_plugin(cert_update cert_update.cc)
add_autest_plugin(conf_remap_stripped conf_remap_stripped.cc)
add_autest_plugin(continuations_verify continuations_verify.cc)
add_autest_plugin(cont_schedule cont_schedule.cc)
add_autest_plugin(custom204plugin custom204plugin.cc)
add_autest_plugin(emergency_shutdown emergency_shutdown.cc)
add_autest_plugin(fatal_shutdown fatal_shutdown.cc)
add_autest_plugin(hook_add_plugin hook_add_plugin.cc)
add_autest_plugin(missing_mangled_definition
    missing_mangled_definition_c.c
    missing_mangled_definition_cpp.cc
)
add_autest_plugin(missing_ts_plugin_init missing_ts_plugin_init.cc)
add_autest_plugin(ssl_client_verify_test ssl_client_verify_test.cc)
add_autest_plugin(ssl_hook_test ssl_hook_test.cc)
add_autest_plugin(ssl_secret_load_test ssl_secret_load_test.cc)
add_autest_plugin(ssl_verify_test ssl_verify_test.cc)
add_autest_plugin(ssntxnorder_verify ssntxnorder_verify.cc)
add_autest_plugin(test_cppapi test_cppapi.cc)
target_link_libraries(test_cppapi PRIVATE ts::tscppapi)
add_autest_plugin(test_hooks test_hooks.cc)
add_autest_plugin(test_log_interface test_log_interface.cc)
add_autest_plugin(user_args user_args.cc)
add_autest_plugin(async_engine async_engine.c)
add_autest_plugin(hook_tunnel_plugin hook_tunnel_plugin.cc)

target_link_libraries(cert_update PRIVATE OpenSSL::SSL)
target_link_libraries(continuations_verify PRIVATE OpenSSL::SSL)
target_link_libraries(ssl_client_verify_test PRIVATE OpenSSL::SSL)
target_link_libraries(ssl_hook_test PRIVATE OpenSSL::SSL)
target_link_libraries(ssl_secret_load_test PRIVATE OpenSSL::SSL)
target_link_libraries(ssl_verify_test PRIVATE OpenSSL::SSL)
target_link_libraries(async_engine PRIVATE OpenSSL::SSL)
