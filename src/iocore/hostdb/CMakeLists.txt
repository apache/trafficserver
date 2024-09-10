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

add_library(inkhostdb STATIC HostDB.cc Inline.cc RefCountCache.cc HostFile.cc HostDBInfo.cc)
add_library(ts::inkhostdb ALIAS inkhostdb)

target_link_libraries(inkhostdb PUBLIC ts::inkdns ts::inkevent ts::tscore)

clang_tidy_check(inkhostdb)

if(BUILD_TESTING)
  add_executable(benchmark_HostDB benchmark_HostDB.cc)
  target_link_libraries(
    benchmark_HostDB
    PRIVATE ts::tscore
            ts::tsutil
            ts::inkevent
            ts::http
            ts::http_remap
            ts::inkcache
            ts::inkhostdb
  )

  add_executable(test_HostFile test_HostFile.cc HostFile.cc HostDBInfo.cc)
  target_link_libraries(test_HostFile PRIVATE ts::tscore ts::tsutil ts::inkevent catch2::catch2)
  add_test(NAME test_hostdb_HostFile COMMAND $<TARGET_FILE:test_HostFile>)

  add_executable(test_RefCountCache test_RefCountCache.cc)
  target_link_libraries(test_RefCountCache PRIVATE ts::tscore ts::tsutil ts::inkevent catch2::catch2)
  add_test(NAME test_hostdb_RefCountCache COMMAND $<TARGET_FILE:test_RefCountCache>)

endif()
