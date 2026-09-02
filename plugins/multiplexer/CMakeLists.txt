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

add_atsplugin(
  multiplexer
  ats-multiplexer.cc
  chunk-decoder.cc
  dispatch.cc
  fetcher.cc
  original-request.cc
  post.cc
  ts.cc
)

target_compile_definitions(multiplexer PRIVATE PLUGIN_TAG="multiplexer")

verify_remap_plugin(multiplexer)

if(BUILD_TESTING)
  add_executable(test_multiplexer_chunk_decoder unit_tests/test_chunk_decoder.cc chunk-decoder.cc)
  target_include_directories(test_multiplexer_chunk_decoder PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(test_multiplexer_chunk_decoder PRIVATE Catch2::Catch2WithMain ts::tsutil)
  add_catch2_test(NAME test_multiplexer_chunk_decoder COMMAND test_multiplexer_chunk_decoder)
endif()
