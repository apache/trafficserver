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

add_library(inkuring STATIC io_uring.cc IOUringEventIO.cc)
add_library(ts::inkuring ALIAS inkuring)

include_directories(
  ${CMAKE_SOURCE_DIR}/iocore/dns ${CMAKE_SOURCE_DIR}/iocore/io_uring ${CMAKE_SOURCE_DIR}/iocore/net
  ${CMAKE_SOURCE_DIR}/iocore/cache
)

target_link_libraries(
  inkuring
  PUBLIC ts::tscore uring
  PRIVATE ts::tsapicore
)

add_executable(test_iouring unit_tests/test_diskIO.cc)
target_link_libraries(test_iouring PRIVATE inkuring libswoc tscpputil catch2::catch2)

target_include_directories(test_iouring PRIVATE ${CMAKE_SOURCE_DIR}/include ${CATCH_INCLUDE_DIR})

add_test(NAME test_iouring COMMAND $<TARGET_FILE:test_iouring>)
