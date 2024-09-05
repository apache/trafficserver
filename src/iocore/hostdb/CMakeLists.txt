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
  macro(add_hostdb_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE ts::inkhostdb catch2::catch2)
    add_test(NAME test_hostdb_${name} COMMAND $<TARGET_FILE:${name}>)
  endmacro()

  add_hostdb_test(benchmark_HostDB benchmark_HostDB.cc)
  add_hostdb_test(test_HostFile test_HostFile.cc)
  add_hostdb_test(test_RefCountCache test_RefCountCache.cc)
endif()
