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

# This is for tests that depend on multiple libraries where dependency order matters.

link_libraries(
  inkcache
  http
  http_remap
  http2
  logging
  hdrs
  diagsconfig
  inkutils
  inkdns
  inkhostdb
  proxy
  http
  inkutils
  inkdns
  inkhostdb
  proxy
  http
  hdrs
  http_remap
  logging
  fastlz
  aio
  records
  tscore
  tsapicore
  tscpputil
  proxy
  inknet
  inkevent
  yaml-cpp::yaml-cpp
  libswoc
  catch2::catch2
  jsonrpc_protocol
)
if(TS_USE_LINUX_IO_URING)
  link_libraries(inkuring uring)
endif(TS_USE_LINUX_IO_URING)

macro(add_cache_test name)
  add_executable(
    ${name}
    ${CMAKE_SOURCE_DIR}/src/iocore/cache/unit_tests/main.cc ${CMAKE_SOURCE_DIR}/src/iocore/cache/unit_tests/stub.cc
    ${CMAKE_SOURCE_DIR}/src/iocore/cache/unit_tests/CacheTestHandler.cc ${ARGN}
  )
  target_link_libraries(${name} PRIVATE ts::inknet ts::proxy ts::tsapicore)
  add_test(NAME test_cache_${name} COMMAND $<TARGET_FILE:${name}>)
endmacro()

macro(add_net_test name)
  add_executable(${name} ${CMAKE_SOURCE_DIR}/src/iocore/net/libinknet_stub.cc ${ARGN})
  add_test(NAME test_cache_${name} COMMAND $<TARGET_FILE:${name}>)
endmacro()

macro(add_stubbed_test name)
  add_executable(${name} ${CMAKE_SOURCE_DIR}/src/iocore/cache/unit_tests/stub.cc ${ARGN})
  target_link_libraries(${name} PRIVATE ts::proxy ts::http)
  add_test(NAME test_stubbed_${name} COMMAND $<TARGET_FILE:${name}>)
endmacro()

add_cache_test(Cache ${CMAKE_SOURCE_DIR}/src/iocore/cache/unit_tests/test_Cache.cc)
add_cache_test(Populated_Cache ${CMAKE_SOURCE_DIR}/src/iocore/cache/unit_tests/test_Populated_Cache.cc)
if(ENABLE_DISK_FAILURE_TESTS)
  foreach(i RANGE 1)
    add_cache_test(Disk_Init_Failure_${i} ${CMAKE_SOURCE_DIR}/src/iocore/cache/unit_tests/test_Disk_Init_Failure.cc)
    target_compile_definitions(Disk_Init_Failure_${i} PUBLIC FAILURE_INDICES={${i}})
  endforeach()
  foreach(i RANGE 5 20)
    add_cache_test(Disk_Failure_${i} ${CMAKE_SOURCE_DIR}/src/iocore/cache/unit_tests/test_Disk_Failure.cc)
    target_compile_definitions(Disk_Failure_${i} PUBLIC FAILURE_INDICES={${i}})
  endforeach()
  add_cache_test(
    Populated_Cache_Disk_Failure ${CMAKE_SOURCE_DIR}/src/iocore/cache/unit_tests/test_Populated_Cache_Disk_Failure.cc
  )
endif()
add_cache_test(CacheDir ${CMAKE_SOURCE_DIR}/src/iocore/cache/unit_tests/test_CacheDir.cc)
add_cache_test(CacheVol ${CMAKE_SOURCE_DIR}/src/iocore/cache/unit_tests/test_CacheVol.cc)
add_cache_test(RWW ${CMAKE_SOURCE_DIR}/src/iocore/cache/unit_tests/test_RWW.cc)
add_cache_test(Alternate_L_to_S ${CMAKE_SOURCE_DIR}/src/iocore/cache/unit_tests/test_Alternate_L_to_S.cc)
add_cache_test(Alternate_S_to_L ${CMAKE_SOURCE_DIR}/src/iocore/cache/unit_tests/test_Alternate_S_to_L.cc)
add_cache_test(
  Alternate_L_to_S_remove_L ${CMAKE_SOURCE_DIR}/src/iocore/cache/unit_tests/test_Alternate_L_to_S_remove_L.cc
)
add_cache_test(
  Alternate_L_to_S_remove_S ${CMAKE_SOURCE_DIR}/src/iocore/cache/unit_tests/test_Alternate_L_to_S_remove_S.cc
)
add_cache_test(
  Alternate_S_to_L_remove_L ${CMAKE_SOURCE_DIR}/src/iocore/cache/unit_tests/test_Alternate_S_to_L_remove_L.cc
)
add_cache_test(
  Alternate_S_to_L_remove_S ${CMAKE_SOURCE_DIR}/src/iocore/cache/unit_tests/test_Alternate_S_to_L_remove_S.cc
)
add_cache_test(Update_L_to_S ${CMAKE_SOURCE_DIR}/src/iocore/cache/unit_tests/test_Update_L_to_S.cc)
add_cache_test(Update_S_to_L ${CMAKE_SOURCE_DIR}/src/iocore/cache/unit_tests/test_Update_S_to_L.cc)
#add_cache_test(Update_Header ${CMAKE_SOURCE_DIR}/src/iocore/cache/unit_tests/test_Update_Header.cc)

add_executable(test_AIO ${CMAKE_SOURCE_DIR}/src/iocore/aio/test_AIO.cc)
add_test(
  NAME test_AIO
  COMMAND $<TARGET_FILE:test_AIO>
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/src/iocore/aio
)

add_net_test(
  test_net "${CMAKE_SOURCE_DIR}/src/iocore/net/unit_tests/test_ProxyProtocol.cc"
  "${CMAKE_SOURCE_DIR}/src/iocore/net/unit_tests/test_SSLSNIConfig.cc"
  "${CMAKE_SOURCE_DIR}/src/iocore/net/unit_tests/test_YamlSNIConfig.cc"
  "${CMAKE_SOURCE_DIR}/src/iocore/net/unit_tests/unit_test_main.cc"
)
set(LIBINKNET_UNIT_TEST_DIR "${CMAKE_SOURCE_DIR}/src/iocore/net/unit_tests")
target_compile_definitions(test_net PRIVATE LIBINKNET_UNIT_TEST_DIR=${LIBINKNET_UNIT_TEST_DIR})
target_link_libraries(test_net PRIVATE hdrs proxy)

add_stubbed_test(EventSystem ${CMAKE_SOURCE_DIR}/src/iocore/net/NetVCTest.cc ${CMAKE_SOURCE_DIR}/src/iocore/eventsystem/unit_tests/test_EventSystem.cc)
add_stubbed_test(IOBuffer ${CMAKE_SOURCE_DIR}/src/iocore/eventsystem/unit_tests/test_IOBuffer.cc)

add_stubbed_test(
  HttpTransact ${CMAKE_SOURCE_DIR}/src/proxy/http/unit_tests/main.cc
  ${CMAKE_SOURCE_DIR}/src/proxy/http/unit_tests/test_HttpTransact.cc
)

# maybe move this one back to iocore/eventsystem/CMakeLists.txt
#add_stubbed_test(MIOBufferWriter ${CMAKE_SOURCE_DIR}/iocore/eventsystem/unit_tests/test_MIOBufferWriter.cc)
#add_stubbed_test(ProxyAllocator ${CMAKE_SOURCE_DIR}/iocore/eventsystem/unit_tests/benchmark_ProxyAllocator.cc)
