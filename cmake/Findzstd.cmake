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
#  is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
#  or implied. See the License for the specific language governing permissions and limitations under
#  the License.
#
#######################

# Findzstd.cmake
#
# This will define the following variables
#
#     ZSTD_FOUND
#     ZSTD_LIBRARY
#     ZSTD_INCLUDE_DIRS
#
# and the following imported target
#
#     zstd::zstd
#

find_path(ZSTD_INCLUDE_DIR NAMES zstd.h)

find_library(ZSTD_LIBRARY_DEBUG NAMES zstdd zstd_staticd)
find_library(ZSTD_LIBRARY_RELEASE NAMES zstd zstd_static)

mark_as_advanced(ZSTD_LIBRARY ZSTD_INCLUDE_DIR)

include(SelectLibraryConfigurations)
select_library_configurations(ZSTD)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(ZSTD DEFAULT_MSG ZSTD_LIBRARY ZSTD_INCLUDE_DIR)

if(ZSTD_FOUND)
  set(ZSTD_INCLUDE_DIRS "${ZSTD_INCLUDE_DIR}")
endif()

if(ZSTD_FOUND AND NOT TARGET zstd::zstd)
  add_library(zstd::zstd INTERFACE IMPORTED)
  target_include_directories(zstd::zstd INTERFACE ${ZSTD_INCLUDE_DIRS})
  target_link_libraries(zstd::zstd INTERFACE "${ZSTD_LIBRARY}")
endif()
