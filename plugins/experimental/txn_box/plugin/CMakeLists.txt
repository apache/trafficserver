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
cmake_minimum_required(VERSION 3.11)

project(txn_box)
set(CMAKE_CXX_STANDARD 17)

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
# set_property(TARGET plugin PROPERTY PREFIX "")
# set_property(TARGET plugin PROPERTY OUTPUT_NAME "txn_box")

target_link_libraries(txn_box PRIVATE libswoc::libswoc PkgConfig::PCRE2 OpenSSL::SSL ts::tsapi yaml-cpp::yaml-cpp
)#ts::tsapi needed?
#pcre2-8
target_include_directories(txn_box PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include ${YAMLCPP_INCLUDE_DIR})
# target_include_directories(txn_box PRIVATE ${PLUGIN_SOURCE_DIR}/include ${trafficserver_INCLUDE_DIRS} ${OPENSSL_INCLUDE_DIR}, libswoc)
if(CMAKE_COMPILER_IS_GNUCXX)
  target_compile_options(txn_box PRIVATE -Wall -Wextra -Werror -Wnon-virtual-dtor)
endif()

# install(TARGETS txn_box LIBRARY DESTINATION ${INSTALL_DIR}/lib)
