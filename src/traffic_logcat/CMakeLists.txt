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

add_executable(traffic_logcat logcat.cc LogEntryJson.cc)
target_link_libraries(traffic_logcat PRIVATE ts::logging ts::tscore ts::diagsconfig ts::inkevent)
install(TARGETS traffic_logcat)

if(BUILD_TESTING)
  add_executable(test_LogEntryJson unit-tests/test_LogEntryJson.cc LogEntryJson.cc)
  target_include_directories(test_LogEntryJson PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(
    test_LogEntryJson PRIVATE ts::logging ts::configmanager ts::inkevent records Catch2::Catch2WithMain
  )
  add_catch2_test(NAME test_LogEntryJson COMMAND test_LogEntryJson)
endif()

clang_tidy_check(traffic_logcat)
