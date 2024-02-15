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

pkg_check_modules(PCRE2 REQUIRED IMPORTED_TARGET libpcre2-8)

add_atsplugin(
  txn_box
  src/0_static.cc
  src/txn_box.cc
  src/txn_box_remap.cc
  src/Accelerator.cc
  src/Comparison.cc
  src/Config.cc
  src/Context.cc
  src/Directive.cc
  src/Extractor.cc
  src/Ex_Base.cc
  src/Modifier.cc
  src/Machinery.cc
  src/Rxp.cc
  src/ts_util.cc
  src/util.cc
  src/yaml_util.cc
  src/Ex_HTTP.cc
  src/Ex_Ssn.cc
  src/ex_tcp_info.cc
  src/ip_space.cc
  src/query.cc
  src/stats.cc
  src/text_block.cc
)

target_link_libraries(txn_box PRIVATE libswoc::libswoc PkgConfig::PCRE2 OpenSSL::SSL yaml-cpp::yaml-cpp)

target_include_directories(txn_box PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
