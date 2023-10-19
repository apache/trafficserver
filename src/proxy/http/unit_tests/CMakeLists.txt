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

add_executable(
  test_http
  main.cc
  "${PROJECT_SOURCE_DIR}/src/iocore/cache/unit_tests/stub.cc"
  test_error_page_selection.cc
  test_ForwardedConfig.cc
  test_HttpTransact.cc
  test_HttpUserAgent.cc
  test_PreWarm.cc
)

# transitive
target_include_directories(
  test_http
  PRIVATE "${PROJECT_BINARY_DIR}/include/ts"
          # private headers
          ${CMAKE_SOURCE_DIR}/src/iocore/net ${CMAKE_SOURCE_DIR}/src/iocore/dns ${CMAKE_SOURCE_DIR}/src/proxy/
)

target_link_libraries(
  test_http
  PRIVATE catch2::catch2
          ts::http
          ts::hdrs # transitive
          logging # transitive
          http_remap # transitive
          ts::proxy
          inkdns # transitive
          ts::inknet
          ts::jsonrpc_protocol
)

add_test(NAME test_http COMMAND $<TARGET_FILE:test_http>)
