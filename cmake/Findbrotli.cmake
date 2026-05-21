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

# Findbrotli.cmake
#
# This will define the following variables
#
#     brotli_FOUND
#     brotlicommon_LIBRARY
#     brotlienc_LIBRARY
#     brotli_INCLUDE_DIRS
#
# and the following imported targets
#
#     brotli::brotlicommon
#     brotli::brotlienc
#

find_library(brotlicommon_LIBRARY NAMES brotlicommon)
find_library(brotlienc_LIBRARY NAMES brotlienc)
find_path(brotli_INCLUDE_DIR NAMES brotli/encode.h)

mark_as_advanced(brotli_FOUND brotlicommon_LIBRARY brotlienc_LIBRARY brotli_INCLUDE_DIR)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(brotli REQUIRED_VARS brotlicommon_LIBRARY brotlienc_LIBRARY brotli_INCLUDE_DIR)

if(brotli_FOUND)
  set(brotli_INCLUDE_DIRS "${brotli_INCLUDE_DIR}")
endif()

if(brotli_FOUND AND NOT TARGET brotli::brotlicommon)
  add_library(brotli::brotlicommon INTERFACE IMPORTED)
  target_include_directories(brotli::brotlicommon INTERFACE ${brotli_INCLUDE_DIRS})
  target_link_libraries(brotli::brotlicommon INTERFACE "${brotlicommon_LIBRARY}")
endif()

if(brotli_FOUND AND NOT TARGET brotli::brotlienc)
  add_library(brotli::brotlienc INTERFACE IMPORTED)
  target_include_directories(brotli::brotlienc INTERFACE ${brotli_INCLUDE_DIRS})
  target_link_libraries(brotli::brotlienc INTERFACE brotli::brotlicommon "${brotlienc_LIBRARY}")
endif()
