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

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} $ENV{LIB_FUZZING_ENGINE}")

set(CMAKE_INSTALL_RPATH "$ORIGIN/lib")
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)

add_executable(fuzz_esi fuzz_esi.cc)
add_executable(fuzz_hpack fuzz_hpack.cc)
add_executable(fuzz_http fuzz_http.cc)
add_executable(fuzz_json fuzz_json.cc)
add_executable(fuzz_proxy_protocol fuzz_proxy_protocol.cc)
add_executable(fuzz_rec_http fuzz_rec_http.cc)
add_executable(fuzz_yamlcpp fuzz_yamlcpp.cc)

target_link_libraries(fuzz_esi PRIVATE esi-common esicore)
target_link_libraries(fuzz_hpack PRIVATE records tscore hdrs inkevent)
target_link_libraries(fuzz_http PRIVATE ts::hdrs ts::tscore ts::inkevent)
target_link_libraries(fuzz_json PRIVATE libswoc yaml-cpp ts::jsonrpc_protocol)
target_link_libraries(fuzz_proxy_protocol PRIVATE inknet inkevent ts::tscore yaml-cpp libswoc)
target_link_libraries(fuzz_rec_http PRIVATE records tscore libswoc)
target_link_libraries(fuzz_yamlcpp PRIVATE yaml-cpp)

target_sources(
  fuzz_hpack PRIVATE ${CMAKE_SOURCE_DIR}/src/proxy/http2/HTTP2.cc ${CMAKE_SOURCE_DIR}/src/proxy/http2/Http2Frame.cc
                     ${CMAKE_SOURCE_DIR}/src/proxy/http2/HPACK.cc
)
target_include_directories(fuzz_json PRIVATE ${CMAKE_SOURCE_DIR}/lib)
target_include_directories(fuzz_proxy_protocol PRIVATE ${CATCH_INCLUDE_DIR})

file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lib)
add_custom_command(
  TARGET fuzz_esi
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:libswoc> ${CMAKE_CURRENT_BINARY_DIR}/lib/
  COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:ts::tsapi> ${CMAKE_CURRENT_BINARY_DIR}/lib/
  COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:ts::tscpputil> ${CMAKE_CURRENT_BINARY_DIR}/lib/
)
