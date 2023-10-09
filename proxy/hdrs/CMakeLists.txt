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


add_library(hdrs STATIC
        HTTP.cc
        HdrHeap.cc
        HdrTSOnly.cc
        HdrToken.cc
        HdrUtils.cc
        HttpCompat.cc
        MIME.cc
        URL.cc
        VersionConverter.cc
        HuffmanCodec.cc
        XPACK.cc
        HeaderValidator.cc
)
add_library(ts::hdrs ALIAS hdrs)
set_target_properties(hdrs PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

target_include_directories(hdrs
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE
        ${IOCORE_INCLUDE_DIRS}
)

target_link_libraries(hdrs
    PUBLIC
        libswoc
        ts::tscore
    PRIVATE
        ts::inkevent
)

if(BUILD_TESTING)
  add_executable(test_proxy_hdrs
    unit_tests/test_HdrHeap.cc
    unit_tests/test_Hdrs.cc
    unit_tests/test_HdrUtils.cc
    unit_tests/test_HdrHeap.cc
    unit_tests/test_HeaderValidator.cc
    unit_tests/test_Huffmancode.cc
    unit_tests/test_mime.cc
    unit_tests/test_URL.cc
    unit_tests/unit_test_main.cc
  )
  target_link_libraries(test_proxy_hdrs PRIVATE ts::hdrs ts::tscore ts::inkevent catch2::catch2)
  add_test(NAME test_proxy_hdrs COMMAND test_proxy_hdrs)

  add_executable(test_proxy_hdrs_xpack
    XPACK.cc
    HuffmanCodec.cc
    unit_tests/test_XPACK.cc
  )
  target_include_directories(test_proxy_hdrs_xpack PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(test_proxy_hdrs_xpack PRIVATE ts::tscore libswoc catch2::catch2)
  add_test(NAME test_proxy_hdrs_xpack COMMAND test_proxy_hdrs_xpack)
endif()
