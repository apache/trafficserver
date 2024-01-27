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

add_executable(
  test_stale_response unit_test_main.cc test_DirectiveParser.cc test_BodyData.cc
                      ${PROJECT_SOURCE_DIR}/DirectiveParser.cc
)

target_include_directories(test_stale_response PRIVATE "${PROJECT_SOURCE_DIR}")
target_link_libraries(test_stale_response PRIVATE ts::tsutil libswoc::libswoc catch2::catch2)

add_test(NAME test_stale_response COMMAND test_stale_response)
