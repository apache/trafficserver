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

add_atsplugin(
  jax_fingerprint
  plugin.cc
  context.cc
  userarg.cc
  header.cc
  log.cc
  ja3/ja3_method.cc
  ja3/ja3_utils.cc
  ja4/ja4_method.cc
  ja4/ja4.cc
  ja4/tls_client_hello_summary.cc
  ja4h/ja4h_method.cc
  ja4h/ja4h.cc
  ja4h/datasource.cc
)
target_link_libraries(jax_fingerprint PRIVATE OpenSSL::Crypto OpenSSL::SSL)
verify_global_plugin(jax_fingerprint)

if(BUILD_TESTING)
  add_executable(
    test_jax
    ja3/test_ja3.cc
    ja3/ja3_utils.cc
    ja4/test_ja4.cc
    ja4/ja4.cc
    ja4/tls_client_hello_summary.cc
    ja4h/test_ja4h.cc
    ja4h/ja4h.cc
    ja4h/datasource.cc
  )
  target_link_libraries(test_jax PRIVATE Catch2::Catch2WithMain OpenSSL::SSL)

  add_catch2_test(NAME test_jax COMMAND test_jax)
endif()
