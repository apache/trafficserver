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
  uri_signing_test
  uri_signing_test.cc
  ${PROJECT_SOURCE_DIR}/jwt.cc
  ${PROJECT_SOURCE_DIR}/common.cc
  ${PROJECT_SOURCE_DIR}/config.cc
  ${PROJECT_SOURCE_DIR}/cookie.cc
  ${PROJECT_SOURCE_DIR}/jwt.cc
  ${PROJECT_SOURCE_DIR}/match.cc
  ${PROJECT_SOURCE_DIR}/normalize.cc
  ${PROJECT_SOURCE_DIR}/parse.cc
  ${PROJECT_SOURCE_DIR}/timing.cc
)
target_compile_definitions(uri_signing_test PRIVATE UNITTEST)
target_include_directories(uri_signing_test PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(
  uri_signing_test
  PRIVATE catch2::catch2
          jansson::jansson
          cjose::cjose
          ts::tsapi
          ts::http
          ts::hdrs
          ts::proxy
          ts::configmanager
          ts::logging
          ts::inknet
          ts::overridable_txn_vars
          OpenSSL::SSL
          OpenSSL::Crypto
)
add_test(NAME uri_signing_test COMMAND uri_signing_test)
set_tests_properties(
  uri_signing_test
  PROPERTIES ENVIRONMENT "ASAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/uri_signing_test_leak_suppression.txt"
)
