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

include_directories(
        ${IOCORE_INCLUDE_DIRS}
        ${PROXY_INCLUDE_DIRS}
        ${PROJECT_SOURCE_DIR}/mgmt
        ${CATCH_INCLUDE_DIR}
)

link_libraries(aio inkevent proxy tscore tscpputil hdrs inkcache inknet inkdns inkhostdb inkutils http http_remap logging fastlz)
if (TS_USE_LINUX_IO_URING)
    link_libraries(inkuring uring)
endif (TS_USE_LINUX_IO_URING)

macro(add_cache_test name)
    add_executable(${name}
            ${PROJECT_SOURCE_DIR}/iocore/cache/test/main.cc
            ${PROJECT_SOURCE_DIR}/iocore/cache/test/stub.cc
            ${PROJECT_SOURCE_DIR}/iocore/cache/test/CacheTestHandler.cc
            ${ARGN})
    add_test(NAME test_cache_${name} COMMAND $<TARGET_FILE:${name}>)
endmacro()

macro(add_stubbed_test name)
    add_executable(${name}
            ${PROJECT_SOURCE_DIR}/iocore/cache/test/stub.cc
            ${ARGN})
    add_test(NAME test_stubbed_${name} COMMAND $<TARGET_FILE:${name}>)
endmacro()

add_cache_test(Cache ${PROJECT_SOURCE_DIR}/iocore/cache/test/test_Cache.cc)
add_cache_test(RWW ${PROJECT_SOURCE_DIR}/iocore/cache/test/test_RWW.cc)
add_cache_test(Alternate_L_to_S ${PROJECT_SOURCE_DIR}/iocore/cache/test/test_Alternate_L_to_S.cc)
add_cache_test(Alternate_S_to_L ${PROJECT_SOURCE_DIR}/iocore/cache/test/test_Alternate_S_to_L.cc)
add_cache_test(Alternate_L_to_S_remove_L ${PROJECT_SOURCE_DIR}/iocore/cache/test/test_Alternate_L_to_S_remove_L.cc)
add_cache_test(Alternate_L_to_S_remove_S ${PROJECT_SOURCE_DIR}/iocore/cache/test/test_Alternate_L_to_S_remove_S.cc)
add_cache_test(Alternate_S_to_L_remove_L ${PROJECT_SOURCE_DIR}/iocore/cache/test/test_Alternate_S_to_L_remove_L.cc)
add_cache_test(Alternate_S_to_L_remove_S ${PROJECT_SOURCE_DIR}/iocore/cache/test/test_Alternate_S_to_L_remove_S.cc)
add_cache_test(Update_L_to_S ${PROJECT_SOURCE_DIR}/iocore/cache/test/test_Update_L_to_S.cc)
add_cache_test(Update_S_to_L ${PROJECT_SOURCE_DIR}/iocore/cache/test/test_Update_S_to_L.cc)
add_cache_test(Update_Header ${PROJECT_SOURCE_DIR}/iocore/cache/test/test_Update_Header.cc)

add_executable(test_AIO ${PROJECT_SOURCE_DIR}/iocore/aio/test_AIO.cc)
add_test(NAME test_AIO COMMAND $<TARGET_FILE:test_AIO>)

add_executable(test_net ${PROJECT_SOURCE_DIR}/iocore/net/unit_tests/test_ProxyProtocol.cc)
add_test(NAME test_net COMMAND $<TARGET_FILE:test_net>)

add_stubbed_test(EventSystem ${PROJECT_SOURCE_DIR}/iocore/eventsystem/unit_tests/test_EventSystem.cc)
add_stubbed_test(IOBuffer ${PROJECT_SOURCE_DIR}/iocore/eventsystem/unit_tests/test_IOBuffer.cc)
add_stubbed_test(MIOBufferWriter ${PROJECT_SOURCE_DIR}/iocore/eventsystem/unit_tests/test_MIOBufferWriter.cc)
add_stubbed_test(ProxyAllocator ${PROJECT_SOURCE_DIR}/iocore/eventsystem/unit_tests/benchmark_ProxyAllocator.cc)
