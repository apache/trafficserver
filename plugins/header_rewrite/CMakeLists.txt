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
  header_rewrite
  condition.cc
  conditions.cc
  factory.cc
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
    target_compile_definitions(test_header_rewrite PRIVATE TS_USE_HRW_MAXMINDDB=1)
    target_link_libraries(test_header_rewrite PRIVATE maxminddb::maxminddb)

    find_package(
      Python3
      COMPONENTS Interpreter
      QUIET
    )
    if(Python3_FOUND)
      execute_process(
        COMMAND "${Python3_EXECUTABLE}" -c "import mmdb_writer; import netaddr"
        RESULT_VARIABLE _mmdb_python_result
        OUTPUT_QUIET ERROR_QUIET
      )
      if(_mmdb_python_result EQUAL 0)
        set(_mmdb_test_dir "${CMAKE_CURRENT_BINARY_DIR}/test_mmdb")
        add_custom_command(
          OUTPUT "${_mmdb_test_dir}/test_flat_geo.mmdb" "${_mmdb_test_dir}/test_nested_geo.mmdb"
          COMMAND ${CMAKE_COMMAND} -E make_directory "${_mmdb_test_dir}"
          COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/generate_test_mmdb.py" "${_mmdb_test_dir}"
          DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/generate_test_mmdb.py"
          COMMENT "Generating test MMDB files for header_rewrite"
        )
        add_custom_target(
          generate_test_mmdb DEPENDS "${_mmdb_test_dir}/test_flat_geo.mmdb" "${_mmdb_test_dir}/test_nested_geo.mmdb"
        )
        add_dependencies(test_header_rewrite generate_test_mmdb)
        set_tests_properties(
          test_header_rewrite
          PROPERTIES
            ENVIRONMENT
            "MMDB_TEST_FLAT=${_mmdb_test_dir}/test_flat_geo.mmdb;MMDB_TEST_NESTED=${_mmdb_test_dir}/test_nested_geo.mmdb"
        )
      else()
        message(STATUS "Python modules 'mmdb-writer'/'netaddr' not found; skipping test MMDB generation")
      endif()
    endif()
  endif()

  # add_executable(test_matcher matcher_tests.cc matcher.cc lulu.cc regex_helper.cc)
  # add_catch2_test(NAME test_matcher COMMAND $<TARGET_FILE:test_matcher>)
  #
  # target_link_libraries(test_matcher PRIVATE Catch2::Catch2WithMain ts::tscore libswoc::libswoc PkgConfig::PCRE2)
  #
  # if(ENABLE_CRIPTS)
  #   target_link_libraries(test_matcher PRIVATE ts::cripts)
  #   target_compile_definitions(test_matcher PRIVATE TS_HAS_CRIPTS=1)
  # endif()

endif()
verify_global_plugin(header_rewrite)
verify_remap_plugin(header_rewrite)
