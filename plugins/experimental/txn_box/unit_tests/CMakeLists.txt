# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
cmake_minimum_required(VERSION 3.12)
project(test_txn_box CXX)
set(CMAKE_CXX_STANDARD 17)

pkg_check_modules(PCRE2 REQUIRED IMPORTED_TARGET libpcre2-8)

add_executable(test_txn_box unit_test_main.cc test_txn_box.cc test_accl_utils.cc)

set_target_properties(test_txn_box PROPERTIES CLANG_FORMAT_DIRS ${CMAKE_CURRENT_SOURCE_DIR} ${YAMLCPP_INCLUDE_DIR})

target_link_libraries(
  test_txn_box PUBLIC libswoc::libswoc OpenSSL::SSL PkgConfig::PCRE2 catch2::catch2 yaml-cpp::yaml-cpp
)
# After fighting with CMake over the include paths, it's just not worth it to be correct.
# target_link_libraries should make this work but it doesn't. I can't figure out why.
target_include_directories(test_txn_box PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../plugin/include)
add_test(NAME test_txn_box COMMAND test_txn_box)
