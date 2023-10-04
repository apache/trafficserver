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


add_library(http2 STATIC
        HPACK.cc
        HTTP2.cc
        Http2Frame.cc
        Http2ClientSession.cc
        Http2CommonSession.cc
        Http2ConnectionState.cc
        Http2DebugNames.cc
        Http2FrequencyCounter.cc
        Http2Stream.cc
        Http2SessionAccept.cc
        Http2ServerSession.cc
)
add_library(ts::http2 ALIAS http2)

target_include_directories(http2 PRIVATE
        ${IOCORE_INCLUDE_DIRS}
        ${PROXY_INCLUDE_DIRS}
        ${YAMLCPP_INCLUDE_DIR}
)

target_link_libraries(http2
    PUBLIC
        ts::inkevent
        ts::proxy
        ts::tscore
    PRIVATE
        ts::inkutils
)

if(BUILD_TESTING)
  add_executable(test_http2
    HTTP2.cc
    Http2Frame.cc
    HPACK.cc
    unit_tests/main.cc
    unit_tests/test_HTTP2.cc
    unit_tests/test_Http2Frame.cc
    unit_tests/test_HpackIndexingTable.cc
  )
  target_include_directories(test_http2 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROXY_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/include/iocore/utils
  )
  target_link_libraries(test_http2 PRIVATE catch2::catch2 records tscore hdrs inkevent)
  add_test(NAME test_http2 COMMAND test_http2)

  add_executable(test_Http2DependencyTree
    unit_tests/test_Http2DependencyTree.cc
  )
  target_link_libraries(test_Http2DependencyTree PRIVATE catch2::catch2 tscore libswoc)
  target_include_directories(test_Http2DependencyTree PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/proxy/hdrs
  )
  add_test(NAME test_Http2DependencyTree COMMAND test_Http2DependencyTree)

  add_executable(test_Http2FrequencyCounter
    Http2FrequencyCounter.cc
    unit_tests/test_Http2FrequencyCounter.cc
  )
  target_link_libraries(test_Http2FrequencyCounter PRIVATE catch2::catch2 tscore inkevent libswoc)
  target_include_directories(test_Http2FrequencyCounter PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/proxy/hdrs
  )
  add_test(NAME test_Http2FrequencyCounter COMMAND test_Http2FrequencyCounter)

  add_executable(test_HPACK
    test_HPACK.cc
    HPACK.cc
  )
  target_link_libraries(test_HPACK PRIVATE tscore hdrs inkevent)
  add_test(NAME test_HPACK
    COMMAND test_HPACK -i ${CMAKE_CURRENT_SOURCE_DIR}/hpack-tests -o ./results
  )
endif()
