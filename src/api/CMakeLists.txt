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

add_library(tsapi SHARED InkAPI.cc InkIOCoreAPI.cc)
add_library(ts::tsapi ALIAS tsapi)
target_include_directories(
  tsapi
  PUBLIC $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>
  PRIVATE # private headers
          ${CMAKE_SOURCE_DIR}/src/iocore/eventsystem
          ${CMAKE_SOURCE_DIR}/src/iocore/net
          ${CMAKE_SOURCE_DIR}/src/iocore/dns
          ${CMAKE_SOURCE_DIR}/src/iocore/hostdb
          ${CMAKE_SOURCE_DIR}/src/iocore/cache
          ${CMAKE_SOURCE_DIR}/src/iocore/aio
          ${CMAKE_SOURCE_DIR}/src/proxy/
)

set(TSAPI_PUBLIC_HEADERS
    ${CMAKE_SOURCE_DIR}/include/ts/ts.h
    ${CMAKE_SOURCE_DIR}/include/ts/remap.h
    ${CMAKE_SOURCE_DIR}/include/ts/DbgCtl.h
    ${CMAKE_SOURCE_DIR}/include/ts/TsException.h
    ${CMAKE_SOURCE_DIR}/include/ts/parentselectdefs.h
    ${CMAKE_SOURCE_DIR}/include/ts/parentselectdefs.h
    ${CMAKE_BINARY_DIR}/include/ts/apidefs.h
    ${CMAKE_SOURCE_DIR}/include/ts/experimental.h
)

target_link_libraries(tsapi PRIVATE ts::tscore)
set_target_properties(tsapi PROPERTIES PUBLIC_HEADER "${TSAPI_PUBLIC_HEADERS}")

add_library(
  tsapicore STATIC
  APIHook.cc
  APIHooks.cc
  Metrics.cc
  ConfigUpdateCbTable.cc
  InkContInternal.cc
  InkVConnInternal.cc
  FetchSM.cc
  LifecycleAPIHooks.cc
  DbgCtl.cc
)
add_library(ts::tsapicore ALIAS tsapicore)
target_include_directories(
  tsapicore
  PRIVATE # private headers
          ${CMAKE_SOURCE_DIR}/src/iocore/eventsystem ${CMAKE_SOURCE_DIR}/src/iocore/net
          ${CMAKE_SOURCE_DIR}/src/iocore/dns ${CMAKE_SOURCE_DIR}/src/proxy/
)
target_link_libraries(tsapicore PRIVATE ts::tscore)
set_target_properties(tsapicore PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

include_directories(${IOCORE_INCLUDE_DIRS} ${PROXY_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR}/include/mgmt)

install(TARGETS tsapi PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ts)

if(APPLE)
  target_link_options(tsapi PRIVATE -undefined dynamic_lookup)
endif()

if(BUILD_TESTING)
  add_executable(test_Metrics test_Metrics.cc)

  target_link_libraries(test_Metrics PRIVATE tsapicore tscore catch2::catch2)
  target_include_directories(test_Metrics PRIVATE ${CMAKE_SOURCE_DIR}/include)

  add_test(NAME test_Metrics COMMAND $<TARGET_FILE:test_Metrics>)
endif()
