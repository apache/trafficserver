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

add_library(records STATIC
        P_RecCore.cc
        RecConfigParse.cc
        RecCore.cc
        RecDebug.cc
        RecFile.cc
        RecHttp.cc
        RecMessage.cc
        RecMutex.cc
        RecUtils.cc
        RecYAMLDecoder.cc
        RecordsConfig.cc
        RecordsConfigUtils.cc
        RecRawStats.cc
        )
add_library(ts::records ALIAS records)

target_include_directories(records
    PUBLIC
        "${PROJECT_SOURCE_DIR}/include"
    PRIVATE
        "${CMAKE_SOURCE_DIR}/include/mgmt"
        "${CMAKE_SOURCE_DIR}/iocore/utils"
)

target_link_libraries(records
    PUBLIC
        ts::inkevent
        ts::tscore
        yaml-cpp::yaml-cpp
    PRIVATE
        ts::tsapicore
)

if(BUILD_TESTING)
  add_executable(test_records unit_tests/unit_test_main.cc unit_tests/test_RecHttp.cc)
  target_link_libraries(test_records PRIVATE records catch2::catch2 tscore libswoc)
  add_test(NAME test_records COMMAND test_records)

  add_executable(test_records_on_eventsystem
    unit_tests/unit_test_main_on_eventsystem.cc
    unit_tests/test_DynamicStats.cc
  )
  target_include_directories(test_records_on_eventsystem PRIVATE ${CMAKE_SOURCE_DIR}/iocore/utils)
  target_link_libraries(test_records_on_eventsystem PRIVATE records catch2::catch2 tscore libswoc)
  add_test(NAME test_records_on_eventsystem COMMAND test_records_on_eventsystem)
endif()
