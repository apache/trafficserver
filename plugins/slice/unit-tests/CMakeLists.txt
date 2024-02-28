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

add_executable(test_content_range test_content_range.cc ${PROJECT_SOURCE_DIR}/ContentRange.cc)
target_compile_definitions(test_content_range PRIVATE UNITTEST)
target_link_libraries(test_content_range PRIVATE catch2::catch2 ts::tsutil)
add_test(NAME test_content_range COMMAND test_content_range)

add_executable(test_range test_range.cc ${PROJECT_SOURCE_DIR}/Range.cc)
target_compile_definitions(test_range PRIVATE UNITTEST)
target_link_libraries(test_range PRIVATE catch2::catch2 ts::tsutil)
add_test(NAME test_range COMMAND test_range)

add_executable(test_config test_config.cc ${PROJECT_SOURCE_DIR}/Config.cc ${PROJECT_SOURCE_DIR}/cache.cc)
target_compile_definitions(test_config PRIVATE UNITTEST)
target_link_libraries(test_config PRIVATE PCRE::PCRE catch2::catch2 ts::tsutil)
add_test(NAME test_config COMMAND test_config)

add_executable(test_cache test_cache.cc ${PROJECT_SOURCE_DIR}/cache.cc)
target_compile_definitions(test_cache PRIVATE UNITTEST)
target_link_libraries(test_cache PRIVATE catch2::catch2 ts::tsutil)
add_test(NAME test_cache COMMAND test_cache)
