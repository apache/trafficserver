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

add_library(tsapi SHARED
	InkAPI.cc
	InkIOCoreAPI.cc
)
add_library(ts::tsapi ALIAS tsapi)
target_link_libraries(tsapi PRIVATE ts::tscore)

add_library(tsapicore STATIC
  APIHook.cc
  APIHooks.cc
  Metrics.cc
  ConfigUpdateCbTable.cc
  InkContInternal.cc
  InkVConnInternal.cc
  FetchSM.cc
)
add_library(ts::tsapicore ALIAS tsapicore)
target_link_libraries(tsapicore PRIVATE ts::tscore)

include_directories(
  ${IOCORE_INCLUDE_DIRS}
  ${PROXY_INCLUDE_DIRS}
  ${CMAKE_SOURCE_DIR}/mgmt
)

install(TARGETS tsapi)

if(APPLE)
  target_link_options(tsapi PRIVATE -undefined dynamic_lookup)
endif()

add_executable(test_Metrics
        test_Metrics.cc
        )

target_link_libraries(test_Metrics PRIVATE tsapicore tscore catch2::catch2)
target_include_directories(test_Metrics PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_test(NAME test_Metrics COMMAND $<TARGET_FILE:test_Metrics>)
