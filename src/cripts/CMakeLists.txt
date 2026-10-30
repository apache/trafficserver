# ##############################################################################
#
# Licensed to the Apache Software Foundation (ASF) under one or more contributor
# license agreements.  See the NOTICE file distributed with this work for
# additional information regarding copyright ownership.  The ASF licenses this
# file to you under the Apache License, Version 2.0 (the "License"); you may not
# use this file except in compliance with the License.  You may obtain a copy of
# the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
# License for the specific language governing permissions and limitations under
# the License.
#
# ##############################################################################

# We use fmt with cripts, contrary to ATS / libswoc. We also require PCRE2 and not PCRE,
# but hopefully ATS will migrate to PCRE2 soon.
pkg_check_modules(PCRE2 REQUIRED IMPORTED_TARGET libpcre2-8)

# The source files, globbed so we can drop in local / custom Bundles and extensions.
file(GLOB CPP_FILES ${PROJECT_SOURCE_DIR}/src/cripts/*.cc ${PROJECT_SOURCE_DIR}/src/cripts/*/*.cc)
file(GLOB TEST_CPP_FILES ${PROJECT_SOURCE_DIR}/src/cripts/tests/*.cc ${PROJECT_SOURCE_DIR}/src/cripts/unit_tests/*.cc)
list(REMOVE_ITEM CPP_FILES ${TEST_CPP_FILES})

set(CRIPTS_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/cripts/Bundle.hpp
    ${PROJECT_SOURCE_DIR}/include/cripts/CacheGroup.hpp
    ${PROJECT_SOURCE_DIR}/include/cripts/Certs.hpp
    ${PROJECT_SOURCE_DIR}/include/cripts/Configs.hpp
    ${PROJECT_SOURCE_DIR}/include/cripts/ConfigsBase.hpp
    ${PROJECT_SOURCE_DIR}/include/cripts/Connections.hpp
    ${PROJECT_SOURCE_DIR}/include/cripts/Context.hpp
    ${PROJECT_SOURCE_DIR}/include/cripts/Crypto.hpp
    ${PROJECT_SOURCE_DIR}/include/cripts/Epilogue.hpp
    ${PROJECT_SOURCE_DIR}/include/cripts/Error.hpp
    ${PROJECT_SOURCE_DIR}/include/cripts/Files.hpp
    ${PROJECT_SOURCE_DIR}/include/cripts/Headers.hpp
    ${PROJECT_SOURCE_DIR}/include/cripts/Instance.hpp
    ${PROJECT_SOURCE_DIR}/include/cripts/Lulu.hpp
    ${PROJECT_SOURCE_DIR}/include/cripts/Matcher.hpp
    ${PROJECT_SOURCE_DIR}/include/cripts/Metrics.hpp
    ${PROJECT_SOURCE_DIR}/include/cripts/Plugins.hpp
    ${PROJECT_SOURCE_DIR}/include/cripts/Preamble.hpp
    ${PROJECT_SOURCE_DIR}/include/cripts/Time.hpp
    ${PROJECT_SOURCE_DIR}/include/cripts/Transaction.hpp
    ${PROJECT_SOURCE_DIR}/include/cripts/Urls.hpp
    ${PROJECT_SOURCE_DIR}/include/cripts/UUID.hpp
    ${PROJECT_SOURCE_DIR}/include/cripts/Configs.hpp
)

set(CRIPTS_BUNDLE_HEADERS
    ${PROJECT_SOURCE_DIR}/include/cripts/Bundles/Common.hpp ${PROJECT_SOURCE_DIR}/include/cripts/Bundles/Headers.hpp
    ${PROJECT_SOURCE_DIR}/include/cripts/Bundles/LogsMetrics.hpp
    ${PROJECT_SOURCE_DIR}/include/cripts/Bundles/Caching.hpp
)

add_library(cripts SHARED ${CPP_FILES})
add_library(ts::cripts ALIAS cripts)

target_link_libraries(cripts PUBLIC libswoc::libswoc OpenSSL::Crypto fmt::fmt PkgConfig::PCRE2 yaml-cpp::yaml-cpp)
set_target_properties(cripts PROPERTIES PUBLIC_HEADER "${CRIPTS_PUBLIC_HEADERS}")

set_target_properties(
  cripts PROPERTIES VERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH} SOVERSION
                    ${PROJECT_VERSION_MAJOR} MACHO_COMPATIBILITY_VERSION ${PROJECT_VERSION_MAJOR}.0.0
)

install(TARGETS cripts PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/cripts)

# The PUBLIC_HEADER target can't handle include files in a subdirectory, so do those manually.
install(FILES ${CRIPTS_BUNDLE_HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/cripts/Bundles)

if(APPLE)
  target_link_options(cripts PRIVATE -undefined dynamic_lookup)
endif()

# This generates the src/cripts/Configs.cc file from the ATS source file with
# overridable confiugrations. This needs to be generated whenever we add new
# configurations to the ATS source file.
# This needs to be done manually as the generated file is checked in.
add_custom_target(
  generate-cripts-config
  COMMAND
    ${PROJECT_SOURCE_DIR}/tools/cripts/genconfig.py ${PROJECT_SOURCE_DIR}/src/shared/overridable_txn_vars.cc >
    ${PROJECT_SOURCE_DIR}/include/cripts/Configs.hpp
  DEPENDS ${PROJECT_SOURCE_DIR}/src/shared/overridable_txn_vars.cc ${PROJECT_SOURCE_DIR}/tools/cripts/genconfig.py
  VERBATIM
)

clang_tidy_check(cripts)

if(BUILD_TESTING)
  # The cases run against the real plugin API and header heaps, without a running server. They are loaded
  # like a cript: a module resolving the TS API against an executable that carries the core.
  # Only the core's headers: linking its libraries would give the module private copies of the core's globals.
  add_library(test_cripts_cases MODULE unit_tests/test_CacheGroup.cc unit_tests/test_Headers.cc unit_tests/test_Urls.cc)
  target_include_directories(test_cripts_cases PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_BINARY_DIR}/include)
  target_link_libraries(test_cripts_cases PRIVATE Catch2::Catch2 ts::cripts)
  set_target_properties(
    test_cripts_cases PROPERTIES PREFIX "" SUFFIX ".so" LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  )
  if(APPLE)
    target_link_options(test_cripts_cases PRIVATE -undefined dynamic_lookup)
  endif()

  # The executable runs the cases the module registers, which only works with a single, shared, Catch2 registry.
  get_target_property(CRIPTS_CATCH2_TYPE Catch2::Catch2 TYPE)
  if(NOT CRIPTS_CATCH2_TYPE STREQUAL "SHARED_LIBRARY")
    message(FATAL_ERROR "test_cripts needs Catch2 built as a shared library, not ${CRIPTS_CATCH2_TYPE}")
  endif()

  add_executable(test_cripts unit_tests/main.cc)
  add_dependencies(test_cripts test_cripts_cases)
  target_compile_definitions(test_cripts PRIVATE CRIPTS_TEST_CASES_PATH="$<TARGET_FILE:test_cripts_cases>")
  # Relinking on a module change is what makes ctest rediscover the test cases. LINK_DEPENDS takes no generator
  # expressions before CMake 3.27, hence the module's pinned output directory and name.
  set_target_properties(
    test_cripts PROPERTIES ENABLE_EXPORTS ON LINK_DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/test_cripts_cases.so
  )
  target_link_libraries(
    test_cripts
    PRIVATE
      ${CMAKE_DL_LIBS}
      Catch2::Catch2
      tsapi_objects
      ts::tscore
      ts::overridable_txn_vars
      ts::tsutil
      ts::http
      ts::http_remap
      ts::http2
      ts::logging
      ts::hdrs
      ts::diagsconfig
      ts::inkutils
      ts::inkdns
      ts::inkhostdb
      ts::inkcache
      ts::aio
      ts::proxy
      ts::inknet
      ts::records
      ts::inkevent
      libswoc::libswoc
      ts::jsonrpc_protocol
      ts::jsonrpc_server
      ts::rpcpublichandlers
      ts::configmanager
  )
  add_catch2_test(NAME test_cripts COMMAND test_cripts)
endif()
