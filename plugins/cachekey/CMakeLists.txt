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

add_atsplugin(cachekey cachekey.cc common.cc configs.cc pattern.cc plugin.cc)
verify_global_plugin(cachekey)
verify_remap_plugin(cachekey)

# Register test with CTest when testing is enabled.
if(BUILD_TESTING)
  add_executable(pattern_test unit_tests/pattern_test.cc pattern.cc common.cc)
  target_include_directories(pattern_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(pattern_test PRIVATE CACHEKEY_UNIT_TEST)
  target_link_libraries(pattern_test PRIVATE Catch2::Catch2WithMain libswoc::libswoc ts::tsutil)
  add_catch2_test(NAME pattern_test COMMAND pattern_test)
endif()
