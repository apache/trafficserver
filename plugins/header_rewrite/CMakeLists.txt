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

# Native hrw4u parsing support
if(TARGET ts::hrw4u)
  set(HRW4U_NATIVE_DEFAULT ON)
else()
  set(HRW4U_NATIVE_DEFAULT OFF)
endif()

option(ENABLE_HRW4U_NATIVE "Enable native hrw4u parsing (requires ANTLR4)" ${HRW4U_NATIVE_DEFAULT})

if(ENABLE_HRW4U_NATIVE)
  if(TARGET ts::hrw4u)
    message(STATUS "header_rewrite: Native hrw4u parsing enabled")
    set(HRW4U_NATIVE_ENABLED TRUE)
  else()
    message(WARNING "header_rewrite: Native hrw4u parsing requested but ANTLR4 not available - disabled")
    set(HRW4U_NATIVE_ENABLED FALSE)
  endif()
else()
  message(STATUS "header_rewrite: Native hrw4u parsing disabled")
  set(HRW4U_NATIVE_ENABLED FALSE)
endif()

add_atsplugin(
  header_rewrite
  condition.cc
  conditions.cc
  factory.cc
  objtypes.cc
  header_rewrite.cc
  lulu.cc
  matcher.cc
  operator.cc
  operators.cc
  parser.cc
  regex_helper.cc
  resources.cc
  ruleset.cc
  statement.cc
  value.cc
)

# Add hrw4u integration if enabled
if(HRW4U_NATIVE_ENABLED)
  target_sources(header_rewrite PRIVATE hrw4u.cc)
  target_compile_definitions(header_rewrite PRIVATE ENABLE_HRW4U_NATIVE=1)
endif()

add_library(header_rewrite_parser STATIC parser.cc)
target_link_libraries(header_rewrite_parser PUBLIC libswoc::libswoc)

target_link_libraries(
  header_rewrite
  PRIVATE OpenSSL::Crypto
  PUBLIC libswoc::libswoc
)

if(ENABLE_CRIPTS)
  target_link_libraries(header_rewrite PRIVATE ts::cripts)
  target_compile_definitions(header_rewrite PRIVATE TS_HAS_CRIPTS=1)
endif()

if(HRW4U_NATIVE_ENABLED)
  target_link_libraries(header_rewrite PRIVATE ts::hrw4u)
endif()

if(maxminddb_FOUND)
  target_compile_definitions(header_rewrite PUBLIC TS_USE_HRW_MAXMINDDB=1)
  target_sources(header_rewrite PRIVATE conditions_geo_maxmind.cc)
  target_link_libraries(header_rewrite PRIVATE maxminddb::maxminddb)
endif()

if(BUILD_TESTING)
  add_executable(test_header_rewrite header_rewrite_test.cc)
  add_test(NAME test_header_rewrite COMMAND $<TARGET_FILE:test_header_rewrite>)

  target_link_libraries(test_header_rewrite PRIVATE header_rewrite_parser ts::inkevent ts::tscore)

  if(maxminddb_FOUND)
    target_link_libraries(test_header_rewrite PRIVATE maxminddb::maxminddb)
  endif()

  # This test has linker issue when cripts is enabled, so its commented for now
  #  add_executable(test_matcher matcher_tests.cc matcher.cc lulu.cc regex_helper.cc resources.cc)
  #  add_catch2_test(NAME test_matcher COMMAND $<TARGET_FILE:test_matcher>)
  #
  #  target_link_libraries(test_matcher PRIVATE Catch2::Catch2WithMain ts::tscore libswoc::libswoc PkgConfig::PCRE2)

endif()
verify_global_plugin(header_rewrite)
verify_remap_plugin(header_rewrite)
