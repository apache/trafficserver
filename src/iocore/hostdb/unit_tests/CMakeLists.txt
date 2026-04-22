######################
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
######################

# test_hostdb
add_executable(test_hostdb test_HostDBInfo.cc "${CMAKE_CURRENT_SOURCE_DIR}/../HostDBInfo.cc")
target_link_libraries(test_hostdb PRIVATE Catch2::Catch2WithMain ts::tscore ts::tsutil ts::inkevent configmanager)
add_catch2_test(NAME test_hostdb COMMAND $<TARGET_FILE:test_hostdb>)

# test_HostFile
add_executable(
  test_HostFile test_HostFile.cc "${CMAKE_CURRENT_SOURCE_DIR}/../HostFile.cc"
                "${CMAKE_CURRENT_SOURCE_DIR}/../HostDBInfo.cc"
)
target_include_directories(test_HostFile PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(test_HostFile PRIVATE ts::tscore ts::tsutil ts::inkevent configmanager Catch2::Catch2WithMain)
add_catch2_test(NAME test_hostdb_HostFile COMMAND $<TARGET_FILE:test_HostFile>)

# test_RefCountCache
add_executable(test_RefCountCache test_RefCountCache.cc)
target_include_directories(test_RefCountCache PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(
  test_RefCountCache PRIVATE ts::tscore ts::tsutil ts::inkevent configmanager Catch2::Catch2WithMain
)
add_catch2_test(NAME test_hostdb_RefCountCache COMMAND $<TARGET_FILE:test_RefCountCache>)
