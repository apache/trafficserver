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
  traffic_ctl
  traffic_ctl.cc
  ConvertConfigCommand.cc
  CtrlCommands.cc
  CtrlPrinters.cc
  FileConfigCommand.cc
  PrintUtils.cc
  SSLMultiCertCommand.cc
  ${CMAKE_SOURCE_DIR}/src/shared/rpc/IPCSocketClient.cc
)

target_link_libraries(traffic_ctl ts::tscore ts::config libswoc::libswoc yaml-cpp::yaml-cpp ts::tsutil)

install(TARGETS traffic_ctl)

clang_tidy_check(traffic_ctl)

if(BUILD_TESTING)
  add_executable(test_traffic_ctl_status unit_tests/test_traffic_ctl_status.cc)
  target_include_directories(test_traffic_ctl_status PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(
    test_traffic_ctl_status Catch2::Catch2WithMain ts::tscore ts::tsutil libswoc::libswoc yaml-cpp::yaml-cpp
  )
  add_catch2_test(NAME test_traffic_ctl_status COMMAND test_traffic_ctl_status)
endif()
