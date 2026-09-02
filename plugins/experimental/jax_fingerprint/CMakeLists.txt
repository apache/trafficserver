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

set(ENABLE_JAX_METHODS
    "ja3;ja4;ja4h"
    CACHE STRING "Semicolon-separated list of fingerprint methods to compile into jax_fingerprint"
)

list(LENGTH ENABLE_JAX_METHODS _jax_method_count)
if(_jax_method_count EQUAL 0)
  message(FATAL_ERROR "ENABLE_JAX_METHODS must list at least one method (e.g. ja3;ja4;ja4h)")
endif()

set(_jax_plugin_method_sources "")
set(_jax_test_method_sources "")
set(_jax_method_defs "")
foreach(_m IN LISTS ENABLE_JAX_METHODS)
  if(NOT IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${_m}")
    message(FATAL_ERROR "ENABLE_JAX_METHODS references unknown method '${_m}' (no ${_m}/ directory)")
  endif()
  file(GLOB _srcs CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${_m}/*.cc")
  set(_plugin_srcs ${_srcs})
  set(_test_srcs ${_srcs})
  list(FILTER _plugin_srcs EXCLUDE REGEX "/test\\.cc$")
  # method.cc and tls_client_hello_summary.cc bind algorithm logic to ATS APIs (TSClientHello
  # etc.) that the test binary does not link; everything else is pure algorithm/data and is
  # safe to include in test_jax.
  list(FILTER _test_srcs EXCLUDE REGEX "/(method|tls_client_hello_summary)\\.cc$")
  list(APPEND _jax_plugin_method_sources ${_plugin_srcs})
  list(APPEND _jax_test_method_sources ${_test_srcs})
  string(TOUPPER ${_m} _m_upper)
  list(APPEND _jax_method_defs "ENABLE_JAX_METHOD_${_m_upper}")
endforeach()

add_atsplugin(
  jax_fingerprint
  plugin.cc
  context.cc
  userarg.cc
  header.cc
  log.cc
  common/utils.cc
  ${_jax_plugin_method_sources}
)
target_link_libraries(jax_fingerprint PRIVATE OpenSSL::Crypto OpenSSL::SSL)
target_include_directories(jax_fingerprint BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(jax_fingerprint PRIVATE ${_jax_method_defs} JAX_FINGERPRINT_MAX_METHODS=${_jax_method_count})
verify_global_plugin(jax_fingerprint)
verify_remap_plugin(jax_fingerprint)

if(BUILD_TESTING)
  add_executable(test_jax common/utils.cc ${_jax_test_method_sources})
  target_link_libraries(test_jax PRIVATE Catch2::Catch2WithMain OpenSSL::Crypto OpenSSL::SSL)
  target_include_directories(test_jax BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(test_jax PRIVATE ${_jax_method_defs})

  add_catch2_test(NAME test_jax COMMAND test_jax)
endif()
