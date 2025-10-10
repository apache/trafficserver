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

add_atsplugin(xdebug xdebug.cc xdebug_headers.cc xdebug_transforms.cc xdebug_utils.cc)
target_link_libraries(xdebug PRIVATE libswoc::libswoc)
verify_global_plugin(xdebug)

if(BUILD_TESTING)
  add_executable(test_xdebug unit_tests/test_xdebug_utils.cc xdebug_utils.cc)
  target_include_directories(test_xdebug PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(test_xdebug PRIVATE Catch2::Catch2WithMain libswoc::libswoc)
  add_catch2_test(NAME test_xdebug COMMAND test_xdebug)
endif()
