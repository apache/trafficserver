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

set(CRIPTS_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/cripts/Bundle.hpp
    ${PROJECT_SOURCE_DIR}/include/cripts/Configs.hpp
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

target_link_libraries(cripts PUBLIC libswoc::libswoc fmt::fmt PkgConfig::PCRE2 OpenSSL::Crypto)
set_target_properties(cripts PROPERTIES PUBLIC_HEADER "${CRIPTS_PUBLIC_HEADERS}")

set_target_properties(
  cripts
  PROPERTIES VERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}
             SOVERSION ${PROJECT_VERSION_MAJOR}
             MACHO_COMPATIBILITY_VERSION ${PROJECT_VERSION_MAJOR}.0.0
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
  COMMAND ${PROJECT_SOURCE_DIR}/tools/cripts/genconfig.py ${PROJECT_SOURCE_DIR}/src/shared/overridable_txn_vars.cc >
          ${PROJECT_SOURCE_DIR}/include/cripts/Configs.hpp
  DEPENDS ${PROJECT_SOURCE_DIR}/src/shared/overridable_txn_vars.cc ${PROJECT_SOURCE_DIR}/tools/cripts/genconfig.py
  VERBATIM
)

clang_tidy_check(cripts)
