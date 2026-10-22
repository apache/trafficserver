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

# FindLZ4.cmake
#
# This will define the following variables
#
#     LZ4_FOUND
#     LZ4_LIBRARY
#     LZ4_INCLUDE_DIRS
#     LZ4_VERSION
#
# and the following imported target
#
#     LZ4::LZ4
#

find_library(LZ4_LIBRARY NAMES lz4 liblz4)
find_path(LZ4_INCLUDE_DIR NAMES lz4.h)

mark_as_advanced(LZ4_FOUND LZ4_LIBRARY LZ4_INCLUDE_DIR)

# The version lives in three separate macros in lz4.h; a config package would
# supply it, but this module has to read them out to satisfy a version request.
if(LZ4_INCLUDE_DIR AND EXISTS "${LZ4_INCLUDE_DIR}/lz4.h")
  set(_LZ4_version_parts "")
  foreach(_LZ4_part MAJOR MINOR RELEASE)
    file(STRINGS "${LZ4_INCLUDE_DIR}/lz4.h" _LZ4_line REGEX "^#define[ \t]+LZ4_VERSION_${_LZ4_part}[ \t]+[0-9]+")
    # The value may be followed by a comment, so capture it rather than
    # anchoring on the end of the line.
    if(_LZ4_line MATCHES "^#define[ \t]+LZ4_VERSION_${_LZ4_part}[ \t]+([0-9]+)")
      list(APPEND _LZ4_version_parts "${CMAKE_MATCH_1}")
    endif()
  endforeach()
  list(LENGTH _LZ4_version_parts _LZ4_version_count)
  if(_LZ4_version_count EQUAL 3)
    list(JOIN _LZ4_version_parts "." LZ4_VERSION)
  endif()
  unset(_LZ4_line)
  unset(_LZ4_part)
  unset(_LZ4_version_parts)
  unset(_LZ4_version_count)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
  LZ4
  REQUIRED_VARS LZ4_LIBRARY LZ4_INCLUDE_DIR
  VERSION_VAR LZ4_VERSION
)

if(LZ4_FOUND)
  set(LZ4_INCLUDE_DIRS "${LZ4_INCLUDE_DIR}")
endif()

if(LZ4_FOUND AND NOT TARGET LZ4::LZ4)
  add_library(LZ4::LZ4 INTERFACE IMPORTED)
  target_include_directories(LZ4::LZ4 INTERFACE ${LZ4_INCLUDE_DIRS})
  target_link_libraries(LZ4::LZ4 INTERFACE "${LZ4_LIBRARY}")
endif()
