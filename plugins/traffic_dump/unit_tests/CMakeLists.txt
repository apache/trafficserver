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

find_package(Python3 3.11 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  add_test(NAME test_traffic_dump_post_process_encoding
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_post_process_encoding.py
  )
endif()

add_executable(test_traffic_dump test_json_utils.cc test_sensitive_fields.cc ${PROJECT_SOURCE_DIR}/json_utils.cc)

target_include_directories(test_traffic_dump PRIVATE "${PROJECT_SOURCE_DIR}")
target_link_libraries(test_traffic_dump PRIVATE Catch2::Catch2WithMain)

add_catch2_test(NAME test_traffic_dump COMMAND test_traffic_dump)

find_package(Python3 3.11 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  add_test(NAME test_traffic_dump_post_process COMMAND ${Python3_EXECUTABLE}
                                                       ${CMAKE_CURRENT_SOURCE_DIR}/test_post_process.py
  )
endif()
