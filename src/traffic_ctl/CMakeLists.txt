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
  traffic_ctl traffic_ctl.cc CtrlCommands.cc CtrlPrinters.cc FileConfigCommand.cc
              ${CMAKE_SOURCE_DIR}/src/shared/rpc/IPCSocketClient.cc
)

target_include_directories(traffic_ctl PRIVATE ${IOCORE_INCLUDE_DIRS} ${PROXY_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR}/mgmt)
target_link_libraries(traffic_ctl ts::tscore libswoc yaml-cpp::yaml-cpp)

install(TARGETS traffic_ctl)
