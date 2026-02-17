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

# hrw_confcmp - Configuration comparison tool
# Only build when ANTLR4 is available (for hrw4u support)
if(TARGET ts::hrw4u)
  # Create simplified parser that doesn't need Layout/hrw4u
  add_library(hrw_confcmp_parser STATIC ${CMAKE_SOURCE_DIR}/plugins/header_rewrite/parser.cc)
  target_include_directories(
    hrw_confcmp_parser
    PUBLIC ${CMAKE_SOURCE_DIR}/plugins/header_rewrite
    PUBLIC ${CMAKE_SOURCE_DIR}/include
  )
  target_link_libraries(hrw_confcmp_parser PUBLIC libswoc::libswoc ts::tscore)

  # Create a library with necessary header_rewrite components for the tool
  add_library(
    hrw_confcmp_lib STATIC
    ts_api_stubs.cc
    rules_factory.cc
    ${CMAKE_SOURCE_DIR}/plugins/header_rewrite/condition.cc
    ${CMAKE_SOURCE_DIR}/plugins/header_rewrite/conditions.cc
    ${CMAKE_SOURCE_DIR}/plugins/header_rewrite/factory.cc
    ${CMAKE_SOURCE_DIR}/plugins/header_rewrite/objtypes.cc
    ${CMAKE_SOURCE_DIR}/plugins/header_rewrite/header_rewrite.cc
    ${CMAKE_SOURCE_DIR}/plugins/header_rewrite/hrw4u.cc
    ${CMAKE_SOURCE_DIR}/plugins/header_rewrite/lulu.cc
    ${CMAKE_SOURCE_DIR}/plugins/header_rewrite/matcher.cc
    ${CMAKE_SOURCE_DIR}/plugins/header_rewrite/operator.cc
    ${CMAKE_SOURCE_DIR}/plugins/header_rewrite/operators.cc
    ${CMAKE_SOURCE_DIR}/plugins/header_rewrite/regex_helper.cc
    ${CMAKE_SOURCE_DIR}/plugins/header_rewrite/resources.cc
    ${CMAKE_SOURCE_DIR}/plugins/header_rewrite/ruleset.cc
    ${CMAKE_SOURCE_DIR}/plugins/header_rewrite/statement.cc
    ${CMAKE_SOURCE_DIR}/plugins/header_rewrite/value.cc
  )

  target_include_directories(
    hrw_confcmp_lib
    PUBLIC ${CMAKE_SOURCE_DIR}/plugins/header_rewrite
    PUBLIC ${CMAKE_SOURCE_DIR}/include
  )

  target_link_libraries(
    hrw_confcmp_lib
    PUBLIC hrw_confcmp_parser
    PUBLIC libswoc::libswoc
    PUBLIC OpenSSL::Crypto
    PUBLIC OpenSSL::SSL
    PUBLIC PkgConfig::PCRE2
    PUBLIC ts::tscore
    PUBLIC ts::tsutil
    PUBLIC ts::hrw4u
  )

  # Enable native .hrw4u parsing support in the header_rewrite code
  target_compile_definitions(hrw_confcmp_lib PUBLIC ENABLE_HRW4U_NATIVE)

  # Conditionally add maxminddb if available
  if(maxminddb_FOUND)
    target_compile_definitions(hrw_confcmp_lib PUBLIC TS_USE_HRW_MAXMINDDB=1)
    target_sources(hrw_confcmp_lib PRIVATE ${CMAKE_SOURCE_DIR}/plugins/header_rewrite/conditions_geo_maxmind.cc)
    target_link_libraries(hrw_confcmp_lib PUBLIC maxminddb::maxminddb)
  endif()

  add_executable(hrw_confcmp main.cc comparator.cc)

  # GNU ld's single-pass archive processing can miss symbols when objects
  # within the same static library reference each other (e.g. conditions.cc
  # references stubs in ts_api_stubs.cc). Wrapping with --whole-archive
  # ensures all objects are included. macOS ld iterates until convergence
  # so it doesn't need this.
  if(APPLE)
    target_link_libraries(
      hrw_confcmp
      PRIVATE hrw_confcmp_lib
      PRIVATE hrw_confcmp_parser
      PRIVATE ts::inkevent
      PRIVATE ts::records
    )
  else()
    target_link_libraries(
      hrw_confcmp
      PRIVATE -Wl,--whole-archive hrw_confcmp_lib -Wl,--no-whole-archive
      PRIVATE hrw_confcmp_parser
      PRIVATE ts::inkevent
      PRIVATE ts::records
    )
  endif()

  target_include_directories(
    hrw_confcmp
    PRIVATE ${CMAKE_SOURCE_DIR}/plugins/header_rewrite
    PRIVATE ${CMAKE_SOURCE_DIR}/include
  )

  message(STATUS "hrw_confcmp tool will be built (hrw4u support available)")

  # Copy test runner script to build directory for convenience
  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/run_tests.sh ${CMAKE_CURRENT_BINARY_DIR}/run_tests.sh COPYONLY)

  install(TARGETS hrw_confcmp RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/run_tests.sh DESTINATION ${CMAKE_INSTALL_BINDIR})
else()
  message(STATUS "hrw_confcmp tool skipped (requires ANTLR4)")
endif()
