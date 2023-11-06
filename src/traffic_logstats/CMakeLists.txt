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

add_executable(traffic_logstats logstats.cc)
target_link_libraries(
  traffic_logstats
  PRIVATE ts::logging
          ts::tscore
          ts::diagsconfig
)

install(TARGETS traffic_logstats)

if(BUILD_TESTING)
  add_test(
    NAME test_logstats_json
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_logstats_json ${CMAKE_SOURCE_DIR}/src
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/src
  )

  add_test(
    NAME test_logstats_summary
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_logstats_summary ${CMAKE_SOURCE_DIR}/src
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/src
  )
endif()
