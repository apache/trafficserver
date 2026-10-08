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

# FindZSTD.cmake
#
# This will define the following variables
#
#     ZSTD_FOUND
#     ZSTD_LIBRARY
#     ZSTD_INCLUDE_DIRS
#     ZSTD_VERSION
#
# and the following imported target
#
#     zstd::zstd
#

find_library(ZSTD_LIBRARY NAMES zstd libzstd)
find_path(ZSTD_INCLUDE_DIR NAMES zstd.h)

mark_as_advanced(ZSTD_FOUND ZSTD_LIBRARY ZSTD_INCLUDE_DIR)

# The version lives in three separate macros in zstd.h; a config package would
# supply it, but this module has to read them out to satisfy a version request.
if(ZSTD_INCLUDE_DIR AND EXISTS "${ZSTD_INCLUDE_DIR}/zstd.h")
  set(_ZSTD_version_parts "")
  foreach(_ZSTD_part MAJOR MINOR RELEASE)
    file(STRINGS "${ZSTD_INCLUDE_DIR}/zstd.h" _ZSTD_line REGEX "^#define[ \t]+ZSTD_VERSION_${_ZSTD_part}[ \t]+[0-9]+")
    # The value may be followed by a comment, so capture it rather than
    # anchoring on the end of the line.
    if(_ZSTD_line MATCHES "^#define[ \t]+ZSTD_VERSION_${_ZSTD_part}[ \t]+([0-9]+)")
      list(APPEND _ZSTD_version_parts "${CMAKE_MATCH_1}")
    endif()
  endforeach()
  list(LENGTH _ZSTD_version_parts _ZSTD_version_count)
  if(_ZSTD_version_count EQUAL 3)
    list(JOIN _ZSTD_version_parts "." ZSTD_VERSION)
  endif()
  unset(_ZSTD_line)
  unset(_ZSTD_part)
  unset(_ZSTD_version_parts)
  unset(_ZSTD_version_count)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
  ZSTD
  REQUIRED_VARS ZSTD_LIBRARY ZSTD_INCLUDE_DIR
  VERSION_VAR ZSTD_VERSION
)

if(ZSTD_FOUND)
  set(ZSTD_INCLUDE_DIRS "${ZSTD_INCLUDE_DIR}")
endif()

if(ZSTD_FOUND AND NOT TARGET zstd::zstd)
  add_library(zstd::zstd INTERFACE IMPORTED)
  target_include_directories(zstd::zstd INTERFACE ${ZSTD_INCLUDE_DIRS})
  target_link_libraries(zstd::zstd INTERFACE "${ZSTD_LIBRARY}")
endif()
