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

# Findprofiler.cmake
#
# This will define the following variables
#
#     profiler_FOUND
#     profiler_LIBRARY
#     profiler_INCLUDE_DIRS
#
# and the following imported targets
#
#     profiler::profiler
#

find_library(profiler_LIBRARY NAMES profiler)
find_path(
  profiler_INCLUDE_DIR
  NAMES profiler.h
  PATH_SUFFIXES gperftools
)

mark_as_advanced(profiler_FOUND profiler_LIBRARY profiler_INCLUDE_DIR)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(profiler REQUIRED_VARS profiler_LIBRARY profiler_INCLUDE_DIR)

if(profiler_FOUND)
  set(profiler_INCLUDE_DIRS ${profiler_INCLUDE_DIR})
endif()

if(profiler_FOUND AND NOT TARGET profiler::profiler)
  add_library(gperftools::profiler INTERFACE IMPORTED)
  target_include_directories(gperftools::profiler INTERFACE "${profiler_INCLUDE_DIRS}")
  target_link_libraries(gperftools::profiler INTERFACE "${profiler_LIBRARY}")
endif()
