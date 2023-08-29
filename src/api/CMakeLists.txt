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

add_library(tsapi SHARED Metrics.cc)
add_library(ts::tsapi ALIAS tsapi)
target_link_libraries(tsapi PRIVATE ts::tscore)


install(TARGETS tsapi)

if(APPLE)
  target_link_options(tsapi PRIVATE -undefined dynamic_lookup)
endif()

add_executable(test_Metrics
        test_Metrics.cc
        )

target_link_libraries(test_Metrics PRIVATE tsapi tscore)
target_include_directories(test_Metrics PRIVATE ${CMAKE_SOURCE_DIR}/include ${CATCH_INCLUDE_DIR})

add_test(NAME test_Metrics COMMAND $<TARGET_FILE:test_Metrics>)
