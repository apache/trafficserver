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


add_library(tscpputil SHARED
        ts_bwf.cc
        ts_ip.cc
        ts_diags.cc
        YamlCfg.cc
        ts_unit_parser.cc)
add_library(ts::tscpputil ALIAS tscpputil)
target_link_libraries(tscpputil
	PUBLIC
		libswoc
		yaml-cpp::yaml-cpp
                ts::tscore
)
install(TARGETS tscpputil)

if(BUILD_TESTING)
  add_executable(test_tscpputil
    unit_tests/test_LocalBuffer.cc
    unit_tests/test_PostScript.cc
    unit_tests/test_Strerror.cc
    unit_tests/test_ts_meta.cc
    unit_tests/test_time_parser.cc
    unit_tests/unit_test_main.cc
  )
  target_link_libraries(test_tscpputil PRIVATE tscpputil libswoc catch2::catch2)
  target_include_directories(test_tscpputil PRIVATE ${CMAKE_SOURCE_DIR}/include)

  add_test(NAME test_tscpputil COMMAND $<TARGET_FILE:test_tscpputil>)
endif()
