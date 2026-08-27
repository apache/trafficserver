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

project(abuse_shield)

add_atsplugin(abuse_shield abuse_shield.cc config.cc fingerprint.cc ip_data.cc stats.cc)

target_include_directories(
  abuse_shield BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../jax_fingerprint
)
# Plugins resolve tsutil symbols from traffic_server. Linking the static tsutil
# library here would duplicate its globals in the plugin at load time.
target_link_libraries(abuse_shield PRIVATE yaml-cpp::yaml-cpp libswoc::libswoc)

verify_global_plugin(abuse_shield)

# Unit tests for token bucket and blocking logic
if(BUILD_TESTING)
  add_executable(test_abuse_shield_ip_data unit_tests/test_ip_data.cc ip_data.cc)
  target_include_directories(test_abuse_shield_ip_data PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(test_abuse_shield_ip_data PRIVATE Catch2::Catch2WithMain libswoc::libswoc)
  add_catch2_test(NAME test_abuse_shield_ip_data COMMAND test_abuse_shield_ip_data)

  add_executable(test_abuse_shield_config unit_tests/test_config.cc config.cc fingerprint.cc)
  target_include_directories(test_abuse_shield_config PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  target_include_directories(test_abuse_shield_config BEFORE PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../jax_fingerprint")
  target_link_libraries(
    test_abuse_shield_config PRIVATE Catch2::Catch2WithMain libswoc::libswoc ts::tsutil yaml-cpp::yaml-cpp
  )
  add_catch2_test(NAME test_abuse_shield_config COMMAND test_abuse_shield_config)
endif()
