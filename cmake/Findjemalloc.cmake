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

# Findjemalloc.cmake
#
# This will define the following variables
#
#     jemalloc_FOUND
#     jemalloc_LIBRARY
#     jemalloc_INCLUDE_DIRS
#
# and the following imported targets
#
#     jemalloc::jemalloc
#

find_library(jemalloc_LIBRARY NAMES jemalloc)
find_path(
  jemalloc_INCLUDE_DIR
  NAMES jemalloc.h
  PATH_SUFFIXES jemalloc
)

mark_as_advanced(jemalloc_FOUND jemalloc_LIBRARY jemalloc_INCLUDE_DIR)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(jemalloc REQUIRED_VARS jemalloc_LIBRARY jemalloc_INCLUDE_DIR)

if(jemalloc_FOUND)
  set(jemalloc_INCLUDE_DIRS ${jemalloc_INCLUDE_DIR})
endif()

if(jemalloc_FOUND AND NOT TARGET jemalloc::jemalloc)
  add_library(jemalloc::jemalloc INTERFACE IMPORTED)
  target_include_directories(jemalloc::jemalloc INTERFACE ${jemalloc_INCLUDE_DIRS})
  target_link_libraries(jemalloc::jemalloc INTERFACE ${jemalloc_LIBRARY})
endif()
