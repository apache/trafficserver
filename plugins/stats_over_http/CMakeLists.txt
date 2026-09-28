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

add_atsplugin(stats_over_http stats_over_http.cc prometheus_render.cc prometheus_rules.cc)

target_link_libraries(stats_over_http PRIVATE libswoc::libswoc yaml-cpp::yaml-cpp)

if(HAVE_BROTLI_ENCODE_H)
  target_link_libraries(stats_over_http PRIVATE brotli::brotlienc)
endif()
verify_global_plugin(stats_over_http)
verify_remap_plugin(stats_over_http)

if(BUILD_TESTING)
  add_executable(
    test_stats_over_http unit_tests/test_prometheus_render.cc unit_tests/test_prometheus_rules.cc prometheus_render.cc
                         prometheus_rules.cc
  )
  target_include_directories(test_stats_over_http PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(test_stats_over_http PRIVATE Catch2::Catch2WithMain ts::tsutil yaml-cpp::yaml-cpp)
  add_catch2_test(NAME test_stats_over_http COMMAND test_stats_over_http)
endif()
