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

set(ABUSE_SHIELD_FINGERPRINT_METHODS
    "ja3;ja4"
    CACHE STRING "Semicolon-separated ClientHello fingerprint providers to compile into abuse_shield"
)

list(LENGTH ABUSE_SHIELD_FINGERPRINT_METHODS _fingerprint_method_count)
if(_fingerprint_method_count EQUAL 0)
  message(FATAL_ERROR "ABUSE_SHIELD_FINGERPRINT_METHODS must list at least one provider")
endif()

set(_fingerprint_provider_sources "")
set(ABUSE_SHIELD_FINGERPRINT_INCLUDES "")
set(ABUSE_SHIELD_FINGERPRINT_ENTRIES "")
foreach(_method IN LISTS ABUSE_SHIELD_FINGERPRINT_METHODS)
  if(NOT _method MATCHES "^[a-zA-Z_][a-zA-Z0-9_]*$")
    message(FATAL_ERROR "Fingerprint provider '${_method}' is not a valid C++ namespace name")
  endif()
  if(NOT IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/fingerprints/${_method}")
    message(FATAL_ERROR "Unknown abuse_shield fingerprint provider '${_method}'")
  endif()

  file(GLOB _provider_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/fingerprints/${_method}/*.cc")
  list(APPEND _fingerprint_provider_sources ${_provider_sources})
  string(APPEND ABUSE_SHIELD_FINGERPRINT_INCLUDES "#include \"fingerprints/${_method}/method.h\"\n")
  string(APPEND ABUSE_SHIELD_FINGERPRINT_ENTRIES "    &abuse_shield::fingerprints::${_method}::method,\n")
endforeach()

configure_file(fingerprint_methods.cc.in fingerprint_methods.cc @ONLY)

set(_fingerprint_registry_sources fingerprint.cc ${CMAKE_CURRENT_BINARY_DIR}/fingerprint_methods.cc
                                  ${_fingerprint_provider_sources}
)

# The public providers reuse the exact calculators used by the read-only JAx plugin.
set(_fingerprint_algorithm_sources "")
if("ja3" IN_LIST ABUSE_SHIELD_FINGERPRINT_METHODS)
  list(APPEND _fingerprint_algorithm_sources ../jax_fingerprint/ja3/fingerprint.cc ../jax_fingerprint/ja3/utils.cc)
endif()
if("ja4" IN_LIST ABUSE_SHIELD_FINGERPRINT_METHODS)
  list(
    APPEND
    _fingerprint_algorithm_sources
    ../jax_fingerprint/common/utils.cc
    ../jax_fingerprint/ja4/datasource.cc
    ../jax_fingerprint/ja4/fingerprint.cc
    ../jax_fingerprint/ja4/ja4.cc
    ../jax_fingerprint/ja4/tls_client_hello_summary.cc
  )
endif()

add_atsplugin(
  abuse_shield
  abuse_shield.cc
  config.cc
  ip_data.cc
  stats.cc
  ${_fingerprint_registry_sources}
  ${_fingerprint_algorithm_sources}
)

target_include_directories(
  abuse_shield BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}
                              ${CMAKE_CURRENT_SOURCE_DIR}/../jax_fingerprint
)
# Plugins resolve tsutil symbols from traffic_server. Linking the static tsutil
# library here would duplicate its globals in the plugin at load time.
target_link_libraries(abuse_shield PRIVATE yaml-cpp::yaml-cpp libswoc::libswoc OpenSSL::Crypto OpenSSL::SSL)

verify_global_plugin(abuse_shield)

# Unit tests for token bucket and blocking logic
if(BUILD_TESTING)
  add_executable(test_abuse_shield_ip_data unit_tests/test_ip_data.cc ip_data.cc)
  target_include_directories(test_abuse_shield_ip_data PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(test_abuse_shield_ip_data PRIVATE Catch2::Catch2WithMain libswoc::libswoc)
  add_catch2_test(NAME test_abuse_shield_ip_data COMMAND test_abuse_shield_ip_data)

  add_executable(
    test_abuse_shield_config unit_tests/test_config.cc unit_tests/fingerprint_stubs.cc config.cc
                             ${_fingerprint_registry_sources}
  )
  target_include_directories(test_abuse_shield_config PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  target_include_directories(
    test_abuse_shield_config BEFORE PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../jax_fingerprint"
                                            "${CMAKE_CURRENT_BINARY_DIR}"
  )
  target_link_libraries(
    test_abuse_shield_config PRIVATE Catch2::Catch2WithMain libswoc::libswoc ts::tsutil yaml-cpp::yaml-cpp
                                     OpenSSL::Crypto OpenSSL::SSL
  )
  add_catch2_test(NAME test_abuse_shield_config COMMAND test_abuse_shield_config)
endif()
